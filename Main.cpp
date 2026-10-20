// SPDX-License-Identifier: AGPL-3.0-only
#include <juce_audio_utils/juce_audio_utils.h>
#include "SequencerTiming.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>
#include <memory>
#include <functional>

// An empty slot is part of the grid, not a large button. It remains clickable.
class PatternSlotButton final : public juce::TextButton
{
public:
    std::function<void()> onRightClick;
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.mods.isRightButtonDown())
        {
            if (onRightClick) onRightClick();
            return;
        }
        juce::TextButton::mouseDown(event);
    }
    void paintButton(juce::Graphics& g, bool highlighted, bool pressed) override
    {
        auto area = getLocalBounds().toFloat().reduced(1.0f, 1.0f);
        if (getToggleState())
        {
            g.setColour(juce::Colour(pressed ? 0xff90bdc4 : 0xff77c4cf));
            g.fillRoundedRectangle(area, 3.0f);
            g.setColour(juce::Colour(0xffbdebf1));
            g.fillRect(area.getX(), area.getY(), area.getWidth(), 3.0f);
            g.setColour(juce::Colour(0xff10242c));
            g.setFont(11.0f);
            g.drawFittedText(getButtonText(), getLocalBounds().reduced(8, 2),
                             juce::Justification::centredLeft, 1);
        }
        else if (highlighted)
        {
            g.setColour(juce::Colour(0x334f9aa5));
            g.fillRoundedRectangle(area, 3.0f);
        }
    }
};

// Streaming playback plus a small, independently implemented pattern sequencer.
class Studio final : public juce::AudioAppComponent, private juce::Timer
{
public:
    Studio()
    {
        for (auto& pattern : patternMasks)
            for (auto& mask : pattern) mask.store(0);
        for (auto& clip : clipPatterns) clip.store(0);
        for (auto& gain : gains) gain.store(1.0f);
        for (auto& pan : pans) pan.store(0.0f);
        formats.registerBasicFormats();
        diskThread.startThread();
        for (auto* b : { &importButton, &playButton, &stopButton, &rackButton,
                         &newPatternButton, &clonePatternButton, &addChannelButton }) addAndMakeVisible(b);
        addAndMakeVisible(patternSelector);
        patternSelector.addItem("Pattern 1", 1);
        patternSelector.setSelectedId(1, juce::dontSendNotification);
        patternSelector.onChange = [this] {
            selectedPattern = patternSelector.getSelectedId();
            showSelectedPattern();
            repaint();
        };
        newPatternButton.onClick = [this] { createPattern(false); };
        clonePatternButton.onClick = [this] { createPattern(true); };
        addChannelButton.onClick = [this] { addChannel(); };
        rackButton.setClickingTogglesState(true);
        rackButton.setToggleState(true, juce::dontSendNotification);
        rackButton.onClick = [this] { rackVisible = rackButton.getToggleState(); resized(); repaint(); };
        addAndMakeVisible(status);
        addAndMakeVisible(tempo);
        addAndMakeVisible(tempoLabel);
        addAndMakeVisible(position);
        addAndMakeVisible(positionLabel);
        tempo.setRange(40, 240, 1);
        tempo.setValue(120);
        tempo.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 22);
        tempo.onValueChange = [this] { beatsPerMinute.store(tempo.getValue()); updateTimelineRange(); resized(); repaint(); };
        position.setRange(0.0, 16.0, 0.01);
        position.setTextBoxStyle(juce::Slider::TextBoxRight, false, 65, 22);
        position.onDragStart = [this] { scrubbing = true; };
        position.onDragEnd = [this] { seekTo(position.getValue()); scrubbing = false; };
        position.onValueChange = [this] { if (scrubbing) seekTo(position.getValue()); };
        tempoLabel.setText("BPM", juce::dontSendNotification);
        positionLabel.setText("Posicao", juce::dontSendNotification);
        status.setText("Marque passos no Channel Rack e compassos na Playlist; importe MP3/WAV para pistas de audio.", juce::dontSendNotification);
        const std::array<juce::String, 4> defaults { "Kick", "Snare", "Closed Hat", "Open Hat" };
        for (int channel = 0; channel < maxChannels; ++channel)
        {
            channelNames[channel] = channel < 4 ? defaults[static_cast<size_t>(channel)] : "Sample " + juce::String(channel - 3);
            channelButtons[channel] = std::make_unique<juce::TextButton>(channelNames[channel]);
            channelButtons[channel]->onClick = [this, channel] { loadSample(channel); };
            addAndMakeVisible(*channelButtons[channel]);
            loadButtons[channel] = std::make_unique<juce::TextButton>("Load");
            loadButtons[channel]->onClick = [this, channel] { loadSample(channel); };
            addAndMakeVisible(*loadButtons[channel]);
            channelGains[channel] = std::make_unique<juce::Slider>();
            channelGains[channel]->setRange(0.0, 1.5, 0.01);
            channelGains[channel]->setValue(1.0, juce::dontSendNotification);
            channelGains[channel]->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            channelGains[channel]->setTooltip("Volume do canal");
            channelGains[channel]->onValueChange = [this, channel] { gains[channel].store(static_cast<float>(channelGains[channel]->getValue())); };
            addAndMakeVisible(*channelGains[channel]);
            channelPans[channel] = std::make_unique<juce::Slider>();
            channelPans[channel]->setRange(-1.0, 1.0, 0.01);
            channelPans[channel]->setValue(0.0, juce::dontSendNotification);
            channelPans[channel]->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            channelPans[channel]->setTooltip("Panorama: esquerda / direita");
            channelPans[channel]->onValueChange = [this, channel] { pans[channel].store(static_cast<float>(channelPans[channel]->getValue())); };
            addAndMakeVisible(*channelPans[channel]);
            removeButtons[channel] = std::make_unique<juce::TextButton>("x");
            removeButtons[channel]->setTooltip("Remover este canal de sample");
            removeButtons[channel]->onClick = [this, channel] { removeChannel(channel); };
            addAndMakeVisible(*removeButtons[channel]);
            for (int step = 0; step < stepsPerBar; ++step)
            {
                auto& button = channelSteps[channel][step];
                button = std::make_unique<juce::TextButton>(juce::String());
                button->setClickingTogglesState(true);
                button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffb9e67a));
                button->onClick = [this, channel] { updatePatternMasks(channel); };
                addAndMakeVisible(*button);
            }
        }
        for (int i = 0; i < 8; ++i)
        {
            clips[i] = std::make_unique<PatternSlotButton>();
            clips[i]->onClick = [this, i] {
                const int current = clipPatterns[i].load();
                clipPatterns[i].store(current == selectedPattern ? 0 : selectedPattern);
                updateClipButtons();
            };
            clips[i]->onRightClick = [this, i] { clipPatterns[i].store(0); updateClipButtons(); };
            addAndMakeVisible(*clips[i]);
        }
        importButton.onClick = [this] { chooseFiles(); };
        playButton.onClick = [this] {
            const bool playing = sequencerPlaying.load();
            sequencerPlaying.store(!playing);
            for (auto& t : tracks) playing ? t->transport.stop() : t->transport.start();
            playButton.setButtonText(playing ? "Play" : "Pause");
        };
        stopButton.onClick = [this] {
            sequencerPlaying.store(false);
            for (auto& t : tracks) { t->transport.stop(); t->transport.setPosition(0); }
            songSeconds.store(0.0);
            seekRevision.fetch_add(1);
            playButton.setButtonText("Play");
            position.setValue(0.0, juce::dontSendNotification);
        };
        updateClipButtons();
        showSelectedPattern();
        setSize(1000, 640);
        setAudioChannels(0, 2);
        startTimerHz(15);
    }
    ~Studio() override
    {
        stopTimer();
        shutdownAudio();
        mixer.removeAllInputs();
        tracks.clear();
        diskThread.stopThread(3000);
    }
    void prepareToPlay(int blockSize, double sampleRate) override
    {
        outputSampleRate = sampleRate;
        mixer.prepareToPlay(blockSize, sampleRate);
    }
    void releaseResources() override { mixer.releaseResources(); }
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
    {
        mixer.getNextAudioBlock(info);
        if (!sequencerPlaying.load() || outputSampleRate <= 0.0) return;
        const auto revision = seekRevision.load();
        if (revision != seenRevision)
        {
            seenRevision = revision;
            previousStep = -1;
            for (auto& voices : channelVoices)
                for (auto& voice : voices) voice.age = -1.0;
        }
        auto seconds = songSeconds.load();
        const auto bpm = beatsPerMinute.load();
        const auto loopSeconds = 8.0 * 4.0 * 60.0 / bpm;
        std::array<std::shared_ptr<const SampleData>, maxChannels> loadedSamples;
        for (int channel = 0; channel < channelCount.load(); ++channel)
            loadedSamples[channel] = std::atomic_load(&samples[channel]);
        for (int i = 0; i < info.numSamples; ++i)
        {
            if (!hasAudioTracks.load() && seconds >= loopSeconds)
            {
                seconds = 0.0;
                previousStep = -1;
            }
            const int globalStep = pulse::stepAtTime(seconds, bpm);
            if (globalStep != previousStep)
            {
                previousStep = globalStep;
                const int bar = pulse::barAtStep(globalStep);
                const int step = pulse::stepInBar(globalStep);
                const int pattern = clipPatterns[bar].load();
                if (pattern > 0)
                {
                    for (int channel = 0; channel < channelCount.load(); ++channel)
                        if (pulse::stepEnabled(patternMasks[pattern - 1][channel].load(), step))
                        {
                            auto& voices = channelVoices[channel];
                            auto& voice = voices[nextVoice[channel]++ % voices.size()];
                            voice = {};
                            voice.age = 0.0;
                        }
                }
            }
            float drumLeft = 0.0f, drumRight = 0.0f;
            for (int channel = 0; channel < channelCount.load(); ++channel)
            {
                for (auto& voice : channelVoices[channel])
                {
                    auto& age = voice.age;
                    if (age < 0.0) continue;
                    float left = 0.0f, right = 0.0f;
                    if (const auto& sample = loadedSamples[channel])
                    {
                        const auto index = static_cast<size_t>(voice.samplePosition);
                        if (index + 1 >= sample->left.size()) { age = -1.0; continue; }
                        const auto fraction = static_cast<float>(voice.samplePosition - static_cast<double>(index));
                        left = sample->left[index] + (sample->left[index + 1] - sample->left[index]) * fraction;
                        right = sample->right[index] + (sample->right[index + 1] - sample->right[index]) * fraction;
                        voice.samplePosition += sample->rate / outputSampleRate;
                    }
                    else
                    {
                        const auto noise = nextNoise(channel);
                        const auto& pi = juce::MathConstants<double>::twoPi;
                        if (channel == 0 && age < 0.36)
                        {
                            const auto frequency = 48.0 + 115.0 * std::exp(-age * 24.0);
                            voice.phase += pi * frequency / outputSampleRate;
                            left = static_cast<float>(std::sin(voice.phase) * std::exp(-age * 15.0));
                        }
                        else if (channel == 1 && age < 0.26)
                        {
                            voice.phase += pi * 185.0 / outputSampleRate;
                            left = static_cast<float>((0.72 * noise + 0.28 * std::sin(voice.phase)) * std::exp(-age * 18.0));
                        }
                        else if ((channel == 2 || channel == 3) && age < (channel == 2 ? 0.12 : 0.5))
                        {
                            const float high = noise - voice.previousNoise;
                            voice.previousNoise = noise;
                            left = static_cast<float>(0.55 * high * std::exp(-age * (channel == 2 ? 35.0 : 8.0)));
                        }
                        else { age = -1.0; continue; }
                        right = left;
                    }
                    const auto gain = 0.28f * gains[channel].load();
                    const auto pan = pans[channel].load();
                    drumLeft += left * gain * pulse::panGain(pan, true);
                    drumRight += right * gain * pulse::panGain(pan, false);
                    age += 1.0 / outputSampleRate;
                }
            }
            info.buffer->addSample(0, info.startSample + i, drumLeft);
            if (info.buffer->getNumChannels() > 1)
                info.buffer->addSample(1, info.startSample + i, drumRight);
            seconds += 1.0 / outputSampleRate;
        }
        if (seekRevision.load() == revision) songSeconds.store(seconds);
    }
    void resized() override
    {
        constexpr int browserWidth = 190, trackHeaderWidth = 122;
        const int left = browserWidth + trackHeaderWidth;
        const int playlistWidth = juce::jmax(80, getWidth() - left - 14);
        const int barWidth = juce::jmax(8, playlistWidth / visibleBars());
        playButton.setBounds(12, 10, 62, 28);
        stopButton.setBounds(78, 10, 62, 28);
        tempoLabel.setBounds(154, 10, 34, 28);
        tempo.setBounds(188, 10, 106, 28);
        positionLabel.setBounds(312, 10, 58, 28);
        position.setBounds(370, 10, juce::jmax(80, getWidth() - 600), 28);
        rackButton.setBounds(getWidth() - 196, 10, 88, 28);
        importButton.setBounds(getWidth() - 104, 10, 94, 28);
        patternSelector.setBounds(left + 84, 54, 112, 26);
        newPatternButton.setBounds(left + 200, 54, 62, 26);
        clonePatternButton.setBounds(left + 266, 54, 68, 26);
        for (int i = 0; i < 8; ++i)
            clips[i]->setBounds(left + i * barWidth + 1, 151,
                                juce::jmax(8, barWidth - 2), 32);
        const int rackY = getHeight() - 294;
        addChannelButton.setVisible(rackVisible && channelCount.load() < maxChannels);
        addChannelButton.setBounds(getWidth() - 123, rackY + 7, 98, 25);
        const int stepWidth = juce::jmax(8, (playlistWidth - 246) / stepsPerBar);
        for (int channel = 0; channel < maxChannels; ++channel)
        {
            const bool show = rackVisible && channel < channelCount.load();
            const int y = rackY + 39 + channel * 32;
            channelButtons[channel]->setVisible(show);
            channelButtons[channel]->setBounds(left + 10, y, 82, 24);
            loadButtons[channel]->setVisible(show);
            loadButtons[channel]->setBounds(left + 94, y, 42, 24);
            channelGains[channel]->setVisible(show);
            channelGains[channel]->setBounds(left + 138, y, 38, 24);
            channelPans[channel]->setVisible(show);
            channelPans[channel]->setBounds(left + 178, y, 38, 24);
            removeButtons[channel]->setVisible(show && channel >= 4);
            removeButtons[channel]->setBounds(left + 218, y, 22, 24);
            for (int step = 0; step < stepsPerBar; ++step)
            {
                channelSteps[channel][step]->setVisible(show);
                channelSteps[channel][step]->setBounds(left + 244 + step * stepWidth, y + 2,
                                                       juce::jmax(8, stepWidth - 3), 20);
            }
        }
        status.setBounds(12, getHeight() - 27, getWidth() - 24, 22);
        int rowY = 188;
        for (auto& t : tracks)
        {
            t->gain.setBounds(browserWidth + 4, rowY + 20, 76, 19);
            t->mute.setBounds(browserWidth + 81, rowY + 20, 20, 19);
            t->solo.setBounds(browserWidth + 103, rowY + 20, 20, 19);
            rowY += 40;
        }
    }
    void paint(juce::Graphics& g) override
    {
        constexpr int browserWidth = 190, trackHeaderWidth = 122;
        const int left = browserWidth + trackHeaderWidth;
        const int right = getWidth() - 14;
        const int gridWidth = juce::jmax(80, right - left);
        const double visibleSeconds = visibleBars() * 4.0 * 60.0 / beatsPerMinute.load();
        const int gridBottom = getHeight() - 32;
        const auto ink = juce::Colour(0xffd9e2e7);
        const auto muted = juce::Colour(0xff8797a2);
        g.fillAll(juce::Colour(0xff151b23));
        g.setColour(juce::Colour(0xff202a35));
        g.fillRect(0, 0, getWidth(), 46);
        g.setColour(juce::Colour(0xff354453));
        g.drawHorizontalLine(45, 0.0f, static_cast<float>(getWidth()));
        g.setColour(juce::Colour(0xffb2df87));
        g.setFont(12.0f);
        g.drawText("PULSE", 12, 55, 80, 22, juce::Justification::centredLeft);
        g.setColour(muted);
        g.setFont(11.0f);
        g.drawText("PLAYLIST", browserWidth + 9, 55, 100, 22, juce::Justification::centredLeft);
        g.drawText("PATTERN", left + 8, 55, 76, 22, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff1d2630));
        g.fillRect(0, 86, browserWidth, gridBottom - 86);
        g.setColour(juce::Colour(0xff2c3a47));
        g.drawVerticalLine(browserWidth, 86.0f, static_cast<float>(gridBottom));
        g.setColour(ink);
        g.setFont(12.0f);
        g.drawText("BROWSER", 12, 94, browserWidth - 24, 22, juce::Justification::centredLeft);
        g.setColour(muted);
        g.setFont(11.0f);
        g.drawText("Arquivos do projeto", 12, 124, browserWidth - 24, 20, juce::Justification::centredLeft);
        if (tracks.empty())
            g.drawText("Importe MP3 ou WAV acima", 12, 150, browserWidth - 24, 24, juce::Justification::centredLeft);
        for (size_t i = 0; i < tracks.size(); ++i)
        {
            g.setColour(i % 2 == 0 ? juce::Colour(0xff283846) : juce::Colour(0xff23313e));
            g.fillRoundedRectangle(8.0f, static_cast<float>(150 + static_cast<int>(i) * 26),
                                   static_cast<float>(browserWidth - 16), 23.0f, 3.0f);
            g.setColour(ink);
            g.drawText(tracks[i]->name, 16, 151 + static_cast<int>(i) * 26,
                       browserWidth - 32, 21, juce::Justification::centredLeft, true);
        }
        g.setColour(juce::Colour(0xff202b36));
        g.fillRect(browserWidth + 1, 86, trackHeaderWidth - 1, gridBottom - 86);
        g.setColour(ink);
        g.setFont(12.0f);
        g.drawText("Playlist", browserWidth + 9, 94, trackHeaderWidth - 16, 22, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff263441));
        g.fillRect(left, 122, gridWidth, gridBottom - 122);
        const int barCount = visibleBars();
        for (int bar = 0; bar < barCount; ++bar)
        {
            const int x = left + bar * gridWidth / barCount;
            g.setColour(bar % 2 == 0 ? juce::Colour(0xff273746) : juce::Colour(0xff243340));
            g.fillRect(x, 148, gridWidth / barCount + 1, gridBottom - 148);
            g.setColour(juce::Colour(0xff4e6373));
            g.drawVerticalLine(x, 123.0f, static_cast<float>(gridBottom));
            g.setColour(ink);
            g.setFont(11.0f);
            g.drawText(juce::String(bar + 1), x + 5, 125, 32, 18, juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff35495a));
            for (int beat = 1; beat < 4; ++beat)
                g.drawVerticalLine(x + beat * gridWidth / (barCount * 4), 148.0f, static_cast<float>(gridBottom));
        }
        for (int row = 0; row < 10; ++row)
        {
            const int y = 148 + row * 40;
            if (y >= gridBottom) break;
            g.setColour(juce::Colour(0xff3c4e5c));
            g.drawHorizontalLine(y, static_cast<float>(browserWidth), static_cast<float>(right));
            g.setColour(row == 0 ? juce::Colour(0xffb2df87) : ink);
            g.setFont(11.0f);
            const juce::String rowName = row == 0 ? juce::String("Patterns") : juce::String("Audio ") + juce::String(row);
            g.drawText(rowName,
                       browserWidth + 7, y + 3, trackHeaderWidth - 12, 17,
                       juce::Justification::centredLeft, true);
        }
        for (size_t i = 0; i < tracks.size(); ++i)
        {
            const int y = 188 + static_cast<int>(i) * 40;
            if (y >= gridBottom) break;
            const float clipWidth = static_cast<float>(juce::jlimit(0.0, visibleSeconds, tracks[i]->transport.getLengthInSeconds()) / visibleSeconds * gridWidth);
            g.setColour(juce::Colour(0xff4e9ba8));
            g.fillRoundedRectangle(static_cast<float>(left + 2), static_cast<float>(y + 2),
                                   juce::jmax(2.0f, clipWidth - 4.0f), 34.0f, 3.0f);
            g.setColour(juce::Colours::white);
            g.drawText(tracks[i]->name, left + 8, y + 6,
                       juce::jmax(8, static_cast<int>(clipWidth) - 12), 18,
                       juce::Justification::centredLeft, true);
        }
        const float playheadX = static_cast<float>(left) + static_cast<float>(songSeconds.load() / visibleSeconds) * gridWidth;
        g.setColour(juce::Colour(0xfff6a96c));
        g.drawLine(playheadX, 123.0f, playheadX, static_cast<float>(gridBottom), 1.5f);
        if (rackVisible)
        {
            const int y = getHeight() - 294;
            g.setColour(juce::Colour(0xff18212a));
            g.fillRoundedRectangle(static_cast<float>(left), static_cast<float>(y),
                                   static_cast<float>(gridWidth), 254.0f, 4.0f);
            g.setColour(juce::Colour(0xff506778));
            g.drawRoundedRectangle(static_cast<float>(left), static_cast<float>(y),
                                   static_cast<float>(gridWidth), 254.0f, 4.0f, 1.0f);
            g.setColour(ink);
            g.setFont(12.0f);
            g.drawText("CHANNEL RACK  /  Pattern " + juce::String(selectedPattern), left + 12, y + 8,
                       gridWidth - 130, 22, juce::Justification::centredLeft);
            for (int channel = 0; channel < channelCount.load(); ++channel)
            {
                g.setColour(channel % 2 == 0 ? juce::Colour(0xff263745) : juce::Colour(0xff21313f));
                g.fillRect(left + 8, y + 36 + channel * 32, gridWidth - 16, 29);
            }
            g.setColour(muted);
            g.setFont(10.0f);
            g.drawText("Canal/Load: WAV ou MP3; controles: volume e pan. Clique no compasso para inserir; direito remove.",
                       left + 12, y + 233, gridWidth - 24, 17, juce::Justification::centredLeft);
        }
        g.setColour(juce::Colour(0xff202a35));
        g.fillRect(0, getHeight() - 32, getWidth(), 32);
    }
private:
    static constexpr int maxChannels = 6, maxPatterns = 4, stepsPerBar = 16;
    struct SampleData
    {
        std::vector<float> left, right;
        double rate = 44100.0;
    };
    struct Voice
    {
        double age = -1.0, phase = 0.0, samplePosition = 0.0;
        float previousNoise = 0.0f;
    };
    void addChannel()
    {
        if (channelCount.load() >= maxChannels) return;
        const int channel = channelCount.fetch_add(1);
        channelNames[channel] = "Sample " + juce::String(channel - 3);
        channelButtons[channel]->setButtonText(channelNames[channel]);
        resized(); repaint();
    }
    void removeChannel(int channel)
    {
        const int count = channelCount.load();
        if (channel < 4 || channel >= count) return;
        const bool wasPlaying = sequencerPlaying.exchange(false);
        shutdownAudio();
        for (int source = channel + 1; source < count; ++source)
        {
            const int destination = source - 1;
            channelNames[destination] = channelNames[source];
            channelButtons[destination]->setButtonText(channelNames[destination]);
            channelGains[destination]->setValue(channelGains[source]->getValue());
            channelPans[destination]->setValue(channelPans[source]->getValue());
            std::atomic_store(&samples[destination], std::atomic_load(&samples[source]));
            for (int pattern = 0; pattern < maxPatterns; ++pattern)
                patternMasks[pattern][destination].store(patternMasks[pattern][source].load());
        }
        const int last = count - 1;
        channelCount.store(last);
        std::atomic_store(&samples[last], std::shared_ptr<const SampleData>{});
        channelGains[last]->setValue(1.0);
        channelPans[last]->setValue(0.0);
        for (int pattern = 0; pattern < maxPatterns; ++pattern) patternMasks[pattern][last].store(0);
        showSelectedPattern();
        seekRevision.fetch_add(1);
        setAudioChannels(0, 2);
        sequencerPlaying.store(wasPlaying);
        status.setText("Canal removido; os demais canais mantiveram seus passos e samples.", juce::dontSendNotification);
        resized(); repaint();
    }
    int visibleBars() const
    {
        const double secondsPerBar = 4.0 * 60.0 / beatsPerMinute.load();
        return juce::jlimit(8, 64, static_cast<int>(std::ceil(timelineDuration() / secondsPerBar)));
    }
    double timelineDuration() const
    {
        double duration = 8.0 * 4.0 * 60.0 / beatsPerMinute.load();
        for (const auto& t : tracks) duration = juce::jmax(duration, t->transport.getLengthInSeconds());
        return duration;
    }
    void updateTimelineRange()
    {
        position.setRange(0.0, timelineDuration(), 0.01);
    }
    void updatePatternMasks(int channel)
    {
        uint16_t mask = 0;
        for (int step = 0; step < stepsPerBar; ++step)
            if (channelSteps[channel][step]->getToggleState()) mask |= static_cast<uint16_t>(1u << step);
        patternMasks[selectedPattern - 1][channel].store(mask);
    }
    void showSelectedPattern()
    {
        for (int channel = 0; channel < maxChannels; ++channel)
        {
            const auto mask = patternMasks[selectedPattern - 1][channel].load();
            for (int step = 0; step < stepsPerBar; ++step)
                channelSteps[channel][step]->setToggleState((mask & (1u << step)) != 0, juce::dontSendNotification);
        }
    }
    void updateClipButtons()
    {
        for (int i = 0; i < 8; ++i)
        {
            const int pattern = clipPatterns[i].load();
            clips[i]->setToggleState(pattern != 0, juce::dontSendNotification);
            clips[i]->setButtonText(pattern == 0 ? "" : "Pattern " + juce::String(pattern));
        }
    }
    void createPattern(bool clone)
    {
        if (patternCount >= maxPatterns) return;
        const int source = selectedPattern - 1;
        const int destination = patternCount++;
        for (int channel = 0; channel < maxChannels; ++channel)
            patternMasks[destination][channel].store(clone ? patternMasks[source][channel].load() : 0);
        patternSelector.addItem("Pattern " + juce::String(patternCount), patternCount);
        patternSelector.setSelectedId(patternCount);
        if (patternCount == maxPatterns)
        {
            newPatternButton.setEnabled(false);
            clonePatternButton.setEnabled(false);
        }
        status.setText(clone ? "Pattern clonado; altere os passos e posicione-o na Playlist."
                             : "Novo pattern criado; programe os canais e posicione-o na Playlist.", juce::dontSendNotification);
    }
    float nextNoise(int channel)
    {
        auto& state = noiseStates[channel];
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(static_cast<int32_t>(state)) / 2147483648.0f;
    }
    void loadSample(int channel)
    {
        sampleChooser = std::make_unique<juce::FileChooser>("Carregar sample WAV ou MP3", juce::File{}, "*.wav;*.mp3");
        auto safe = juce::Component::SafePointer<Studio>(this);
        sampleChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe, channel](const juce::FileChooser& dialog) {
                if (safe == nullptr) return;
                const auto file = dialog.getResult();
                if (!file.existsAsFile()) return;
                std::unique_ptr<juce::AudioFormatReader> reader(safe->formats.createReaderFor(file));
                if (!reader || reader->lengthInSamples < 2 || reader->lengthInSamples > reader->sampleRate * 30.0)
                {
                    safe->status.setText("Sample WAV/MP3 invalido ou maior que 30 segundos.", juce::dontSendNotification);
                    return;
                }
                const int length = static_cast<int>(reader->lengthInSamples);
                juce::AudioBuffer<float> buffer(2, length);
                if (!reader->read(&buffer, 0, length, 0, true, true))
                {
                    safe->status.setText("Nao foi possivel ler o sample.", juce::dontSendNotification);
                    return;
                }
                auto sample = std::make_shared<SampleData>();
                sample->rate = reader->sampleRate;
                sample->left.assign(buffer.getReadPointer(0), buffer.getReadPointer(0) + length);
                if (reader->numChannels == 1)
                    sample->right = sample->left;
                else
                    sample->right.assign(buffer.getReadPointer(1), buffer.getReadPointer(1) + length);
                std::atomic_store(&safe->samples[channel], std::shared_ptr<const SampleData>(sample));
                safe->channelNames[channel] = file.getFileNameWithoutExtension();
                safe->channelButtons[channel]->setButtonText(safe->channelNames[channel]);
                safe->status.setText("Sample carregado no canal " + juce::String(channel + 1) + ": " + file.getFileName(), juce::dontSendNotification);
                safe->repaint();
            });
    }
    struct Track
    {
        juce::String name;
        std::unique_ptr<juce::AudioFormatReaderSource> reader;
        juce::AudioTransportSource transport;
        juce::Slider gain;
        juce::ToggleButton mute { "Mute" }, solo { "Solo" };
        ~Track() { transport.setSource(nullptr); }
    };
    void updateGains()
    {
        const bool anySolo = std::any_of(tracks.begin(), tracks.end(), [](const auto& t) { return t->solo.getToggleState(); });
        const float headroom = 1.0f / static_cast<float>(juce::jmax(size_t{1}, tracks.size()));
        for (auto& t : tracks)
            t->transport.setGain((t->mute.getToggleState() || (anySolo && !t->solo.getToggleState()))
                                     ? 0.0f : headroom * static_cast<float>(t->gain.getValue()));
    }
    void seekTo(double seconds)
    {
        for (auto& t : tracks) t->transport.setPosition(juce::jlimit(0.0, t->transport.getLengthInSeconds(), seconds));
        songSeconds.store(seconds);
        seekRevision.fetch_add(1);
    }
    void chooseFiles()
    {
        chooser = std::make_unique<juce::FileChooser>("Importar audio", juce::File{}, "*.wav;*.mp3");
        auto safe = juce::Component::SafePointer<Studio>(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
            [safe](const juce::FileChooser& dialog) {
                if (safe == nullptr) return;
                const auto files = dialog.getResults();
                if (files.isEmpty()) return;
                // Reconfigure only while the device callback is disconnected.
                safe->shutdownAudio();
                safe->sequencerPlaying.store(false);
                for (auto& t : safe->tracks) { t->transport.stop(); t->transport.setPosition(0); }
                safe->songSeconds.store(0.0);
                safe->seekRevision.fetch_add(1);
                safe->playButton.setButtonText("Play");
                int failed = 0;
                for (const auto& file : files)
                {
                    if (safe->tracks.size() >= 8) { ++failed; continue; }
                    std::unique_ptr<juce::AudioFormatReader> reader(safe->formats.createReaderFor(file));
                    if (!reader) { ++failed; continue; }
                    auto track = std::make_unique<Track>();
                    track->name = file.getFileName();
                    track->mute.setButtonText("M");
                    track->solo.setButtonText("S");
                    const auto rate = reader->sampleRate;
                    track->reader = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
                    track->transport.setSource(track->reader.get(), 32768, &safe->diskThread, rate);
                    track->gain.setRange(0.0, 1.0, 0.01);
                    track->gain.setValue(1.0);
                    track->gain.setTextBoxStyle(juce::Slider::TextBoxRight, false, 45, 20);
                    track->gain.onValueChange = [safe] { if (safe != nullptr) safe->updateGains(); };
                    track->mute.onClick = [safe] { if (safe != nullptr) safe->updateGains(); };
                    track->solo.onClick = [safe] { if (safe != nullptr) safe->updateGains(); };
                    safe->addAndMakeVisible(track->gain);
                    safe->addAndMakeVisible(track->mute);
                    safe->addAndMakeVisible(track->solo);
                    safe->mixer.addInputSource(&track->transport, false);
                    safe->tracks.push_back(std::move(track));
                }
                safe->updateGains();
                safe->hasAudioTracks.store(!safe->tracks.empty());
                safe->updateTimelineRange();
                safe->setAudioChannels(0, 2);
                safe->status.setText(juce::String(static_cast<int>(safe->tracks.size())) + " pistas. Ignorados/invalidos: " + juce::String(failed) + ". Limite inicial: 8 pistas.", juce::dontSendNotification);
                safe->repaint();
                safe->resized();
            });
    }
    void timerCallback() override
    {
        if (!scrubbing)
            position.setValue(songSeconds.load(), juce::dontSendNotification);
        repaint();
    }
    juce::AudioFormatManager formats;
    juce::TimeSliceThread diskThread { "Pulse disk reader" };
    std::vector<std::unique_ptr<Track>> tracks;
    juce::MixerAudioSource mixer;
    juce::TextButton importButton { "Importar" }, playButton { "Play" }, stopButton { "Stop" }, rackButton { "Rack" };
    juce::TextButton newPatternButton { "Novo" }, clonePatternButton { "Clonar" }, addChannelButton { "+ Canal" };
    juce::ComboBox patternSelector;
    std::array<std::array<std::unique_ptr<juce::TextButton>, stepsPerBar>, maxChannels> channelSteps;
    std::array<std::unique_ptr<juce::TextButton>, maxChannels> channelButtons, loadButtons;
    std::array<std::unique_ptr<juce::TextButton>, maxChannels> removeButtons;
    std::array<std::unique_ptr<juce::Slider>, maxChannels> channelGains, channelPans;
    std::array<juce::String, maxChannels> channelNames;
    std::array<std::unique_ptr<PatternSlotButton>, 8> clips;
    juce::Label status, tempoLabel, positionLabel;
    juce::Slider tempo, position;
    std::atomic<double> beatsPerMinute { 120.0 }, songSeconds { 0.0 };
    std::atomic<uint64_t> seekRevision { 0 };
    std::array<std::array<std::atomic<uint16_t>, maxChannels>, maxPatterns> patternMasks {};
    std::array<std::atomic<uint8_t>, 8> clipPatterns {};
    std::array<std::atomic<float>, maxChannels> gains {};
    std::array<std::atomic<float>, maxChannels> pans {};
    std::array<std::shared_ptr<const SampleData>, maxChannels> samples;
    std::atomic<int> channelCount { 4 };
    std::atomic<bool> sequencerPlaying { false }, hasAudioTracks { false };
    std::array<std::array<Voice, 4>, maxChannels> channelVoices {};
    std::array<size_t, maxChannels> nextVoice {};
    std::array<uint32_t, maxChannels> noiseStates { 0x12345678u, 0x8abcdef0u, 0x76543210u, 0xfedcba98u, 0x13579bdfu, 0x2468ace0u };
    double outputSampleRate = 0.0;
    int previousStep = -1;
    uint64_t seenRevision = 0;
    int selectedPattern = 1, patternCount = 1;
    bool scrubbing = false, rackVisible = true;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::FileChooser> sampleChooser;
};

class PulseApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Pulse"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String&) override { window = std::make_unique<Window>(); }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }
private:
    class Window final : public juce::DocumentWindow
    {
    public:
        Window() : DocumentWindow("Pulse - development", juce::Colours::black, allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new Studio(), true);
            setResizable(true, false);
            setResizeLimits(800, 640, 3840, 2160);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION(PulseApplication)
