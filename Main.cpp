// SPDX-License-Identifier: AGPL-3.0-only
#include <juce_audio_utils/juce_audio_utils.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>
#include <memory>

// Streaming playback plus a small, independently implemented pattern sequencer.
class Studio final : public juce::AudioAppComponent, private juce::Timer
{
public:
    Studio()
    {
        formats.registerBasicFormats();
        diskThread.startThread();
        for (auto* b : { &importButton, &playButton, &stopButton, &rackButton }) addAndMakeVisible(b);
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
        tempo.onValueChange = [this] { beatsPerMinute.store(tempo.getValue()); updateTimelineRange(); repaint(); };
        position.setRange(0.0, 16.0, 0.01);
        position.setTextBoxStyle(juce::Slider::TextBoxRight, false, 65, 22);
        position.onDragStart = [this] { scrubbing = true; };
        position.onDragEnd = [this] { seekTo(position.getValue()); scrubbing = false; };
        position.onValueChange = [this] { if (scrubbing) seekTo(position.getValue()); };
        tempoLabel.setText("BPM", juce::dontSendNotification);
        positionLabel.setText("Posicao", juce::dontSendNotification);
        status.setText("Marque passos no Channel Rack e compassos na Playlist; importe MP3/WAV para pistas de audio.", juce::dontSendNotification);
        for (int i = 0; i < 16; ++i)
        {
            steps[i] = std::make_unique<juce::TextButton>(juce::String());
            steps[i]->setClickingTogglesState(true);
            steps[i]->onClick = [this] { updatePatternMasks(); };
            addAndMakeVisible(*steps[i]);
        }
        for (int i = 0; i < 8; ++i)
        {
            clips[i] = std::make_unique<juce::TextButton>("+");
            clips[i]->setClickingTogglesState(true);
            clips[i]->onClick = [this] { updatePatternMasks(); };
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
        updatePatternMasks();
        setSize(1100, 700);
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
            kickAge = -1.0;
        }
        auto seconds = songSeconds.load();
        const auto bpm = beatsPerMinute.load();
        const auto loopSeconds = 8.0 * 4.0 * 60.0 / bpm;
        const auto activeSteps = stepMask.load();
        const auto activeClips = clipMask.load();
        for (int i = 0; i < info.numSamples; ++i)
        {
            if (!hasAudioTracks.load() && seconds >= loopSeconds)
            {
                seconds = 0.0;
                previousStep = -1;
            }
            const int globalStep = static_cast<int>(seconds * bpm / 60.0 * 4.0);
            if (globalStep != previousStep)
            {
                previousStep = globalStep;
                const int bar = (globalStep / 16) % 8;
                const int step = globalStep % 16;
                if ((activeClips & (1u << bar)) && (activeSteps & (1u << step)))
                {
                    kickAge = 0.0;
                    kickPhase = 0.0;
                }
            }
            if (kickAge >= 0.0 && kickAge < 0.32)
            {
                const auto frequency = 48.0 + 115.0 * std::exp(-kickAge * 24.0);
                kickPhase += juce::MathConstants<double>::twoPi * frequency / outputSampleRate;
                const auto sample = static_cast<float>(0.38 * std::sin(kickPhase) * std::exp(-kickAge * 15.0));
                for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel)
                    info.buffer->addSample(channel, info.startSample + i, sample);
                kickAge += 1.0 / outputSampleRate;
            }
            seconds += 1.0 / outputSampleRate;
        }
        if (seekRevision.load() == revision) songSeconds.store(seconds);
    }
    void resized() override
    {
        constexpr int browserWidth = 190, trackHeaderWidth = 122;
        const int left = browserWidth + trackHeaderWidth;
        const int playlistWidth = juce::jmax(80, getWidth() - left - 14);
        playButton.setBounds(12, 10, 62, 28);
        stopButton.setBounds(78, 10, 62, 28);
        tempoLabel.setBounds(154, 10, 34, 28);
        tempo.setBounds(188, 10, 106, 28);
        positionLabel.setBounds(312, 10, 58, 28);
        position.setBounds(370, 10, juce::jmax(80, getWidth() - 600), 28);
        rackButton.setBounds(getWidth() - 196, 10, 88, 28);
        importButton.setBounds(getWidth() - 104, 10, 94, 28);
        for (int i = 0; i < 8; ++i)
            clips[i]->setBounds(left + i * playlistWidth / 8 + 2, 151,
                                juce::jmax(8, playlistWidth / 8 - 4), 32);
        const int rackY = getHeight() - 166;
        for (int i = 0; i < 16; ++i)
        {
            steps[i]->setVisible(rackVisible);
            steps[i]->setBounds(left + 114 + i * (playlistWidth - 130) / 16 + 1, rackY + 56,
                                juce::jmax(8, (playlistWidth - 130) / 16 - 3), 20);
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
        g.drawText("PATTERN 1", left + 8, 55, 100, 22, juce::Justification::centredLeft);
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
        for (int bar = 0; bar < 8; ++bar)
        {
            const int x = left + bar * gridWidth / 8;
            g.setColour(bar % 2 == 0 ? juce::Colour(0xff273746) : juce::Colour(0xff243340));
            g.fillRect(x, 148, gridWidth / 8 + 1, gridBottom - 148);
            g.setColour(juce::Colour(0xff4e6373));
            g.drawVerticalLine(x, 123.0f, static_cast<float>(gridBottom));
            g.setColour(ink);
            g.setFont(11.0f);
            g.drawText(juce::String(bar + 1), x + 5, 125, 32, 18, juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff35495a));
            for (int beat = 1; beat < 4; ++beat)
                g.drawVerticalLine(x + beat * gridWidth / 32, 148.0f, static_cast<float>(gridBottom));
        }
        for (int row = 0; row < 10; ++row)
        {
            const int y = 148 + row * 40;
            if (y >= gridBottom) break;
            g.setColour(juce::Colour(0xff3c4e5c));
            g.drawHorizontalLine(y, static_cast<float>(browserWidth), static_cast<float>(right));
            g.setColour(row == 0 ? juce::Colour(0xffb2df87) : ink);
            g.setFont(11.0f);
            const juce::String rowName = row == 0 ? juce::String("Pattern 1") : juce::String("Audio ") + juce::String(row);
            g.drawText(rowName,
                       browserWidth + 7, y + 3, trackHeaderWidth - 12, 17,
                       juce::Justification::centredLeft, true);
        }
        for (size_t i = 0; i < tracks.size(); ++i)
        {
            const int y = 188 + static_cast<int>(i) * 40;
            if (y >= gridBottom) break;
            const float clipWidth = static_cast<float>(juce::jlimit(0.0, timelineDuration(), tracks[i]->transport.getLengthInSeconds()) / timelineDuration() * gridWidth);
            g.setColour(juce::Colour(0xff4e9ba8));
            g.fillRoundedRectangle(static_cast<float>(left + 2), static_cast<float>(y + 2),
                                   juce::jmax(2.0f, clipWidth - 4.0f), 34.0f, 3.0f);
            g.setColour(juce::Colours::white);
            g.drawText(tracks[i]->name, left + 8, y + 6,
                       juce::jmax(8, static_cast<int>(clipWidth) - 12), 18,
                       juce::Justification::centredLeft, true);
        }
        const float playheadX = static_cast<float>(left) + static_cast<float>(songSeconds.load() / timelineDuration()) * gridWidth;
        g.setColour(juce::Colour(0xfff6a96c));
        g.drawLine(playheadX, 123.0f, playheadX, static_cast<float>(gridBottom), 1.5f);
        if (rackVisible)
        {
            const int y = getHeight() - 166;
            g.setColour(juce::Colour(0xff18212a));
            g.fillRoundedRectangle(static_cast<float>(left), static_cast<float>(y),
                                   static_cast<float>(gridWidth), 112.0f, 4.0f);
            g.setColour(juce::Colour(0xff506778));
            g.drawRoundedRectangle(static_cast<float>(left), static_cast<float>(y),
                                   static_cast<float>(gridWidth), 112.0f, 4.0f, 1.0f);
            g.setColour(ink);
            g.setFont(12.0f);
            g.drawText("CHANNEL RACK  /  Pattern 1", left + 12, y + 8,
                       gridWidth - 24, 22, juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff263745));
            g.fillRect(left + 8, y + 49, gridWidth - 16, 35);
            g.setColour(ink);
            g.setFont(11.0f);
            g.drawText("Kick", left + 18, y + 56, 80, 20, juce::Justification::centredLeft);
            g.setColour(muted);
            g.drawText("Clique nos passos para programar o bumbo", left + 12, y + 88,
                       gridWidth - 24, 18, juce::Justification::centredLeft);
        }
        g.setColour(juce::Colour(0xff202a35));
        g.fillRect(0, getHeight() - 32, getWidth(), 32);
    }
private:
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
    void updatePatternMasks()
    {
        uint16_t newSteps = 0;
        uint8_t newClips = 0;
        for (int i = 0; i < 16; ++i)
        {
            if (steps[i]->getToggleState()) newSteps |= static_cast<uint16_t>(1u << i);
            steps[i]->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffb9e67a));
        }
        for (int i = 0; i < 8; ++i)
        {
            if (clips[i]->getToggleState()) newClips |= static_cast<uint8_t>(1u << i);
            clips[i]->setButtonText(clips[i]->getToggleState() ? "Pattern 1" : "");
            clips[i]->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff79c8d4));
        }
        stepMask.store(newSteps);
        clipMask.store(newClips);
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
    std::array<std::unique_ptr<juce::TextButton>, 16> steps;
    std::array<std::unique_ptr<juce::TextButton>, 8> clips;
    juce::Label status, tempoLabel, positionLabel;
    juce::Slider tempo, position;
    std::atomic<double> beatsPerMinute { 120.0 }, songSeconds { 0.0 };
    std::atomic<uint64_t> seekRevision { 0 };
    std::atomic<uint16_t> stepMask { 0 };
    std::atomic<uint8_t> clipMask { 0 };
    std::atomic<bool> sequencerPlaying { false }, hasAudioTracks { false };
    double outputSampleRate = 0.0, kickAge = -1.0, kickPhase = 0.0;
    int previousStep = -1;
    uint64_t seenRevision = 0;
    bool scrubbing = false, rackVisible = true;
    std::unique_ptr<juce::FileChooser> chooser;
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
