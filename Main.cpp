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
        for (auto* b : { &importButton, &playButton, &stopButton }) addAndMakeVisible(b);
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
            steps[i] = std::make_unique<juce::TextButton>(juce::String(i + 1));
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
        importButton.setBounds(20, 64, 160, 32);
        playButton.setBounds(190, 64, 90, 32);
        stopButton.setBounds(290, 64, 90, 32);
        tempoLabel.setBounds(400, 64, 95, 32);
        tempo.setBounds(490, 64, 155, 32);
        positionLabel.setBounds(660, 64, 68, 32);
        position.setBounds(725, 64, getWidth() - 745, 32);
        const int playlistWidth = getWidth() - 260;
        for (int i = 0; i < 8; ++i)
            clips[i]->setBounds(240 + i * playlistWidth / 8, 149, playlistWidth / 8 - 4, 43);
        const int rackY = getHeight() - 145;
        for (int i = 0; i < 16; ++i)
            steps[i]->setBounds(240 + i * (getWidth() - 260) / 16, rackY + 30,
                                (getWidth() - 260) / 16 - 3, 32);
        status.setBounds(20, getHeight() - 44, getWidth() - 40, 30);
        int rowY = 205;
        for (auto& t : tracks)
        {
            t->gain.setBounds(240, rowY + 48, 165, 24);
            t->mute.setBounds(415, rowY + 48, 65, 24);
            t->solo.setBounds(485, rowY + 48, 65, 24);
            rowY += 56;
        }
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff141820));
        g.setColour(juce::Colour(0xffb9e67a));
        g.setFont(28.0f);
        g.drawText("PULSE / desktop", 20, 15, 500, 36, juce::Justification::centredLeft);
        g.setFont(14.0f);
        g.setColour(juce::Colours::white);
        g.drawText("Playlist - Pattern 1", 20, 120, 200, 24, juce::Justification::centredLeft);
        g.drawText("Channel Rack - Kick", 20, getHeight() - 145, 210, 55, juce::Justification::centredLeft);
        const float width = static_cast<float>(getWidth() - 260);
        const double duration = timelineDuration();
        const double secondsPerBeat = 60.0 / tempo.getValue();
        g.setColour(juce::Colour(0xff4a5569));
        for (double beat = 0; beat < duration / secondsPerBeat && beat < 1000; beat += 4)
        {
            const auto x = 240.0f + static_cast<float>(beat * secondsPerBeat / duration) * width;
            g.drawVerticalLine(static_cast<int>(x), 108.0f, static_cast<float>(juce::jmin(getHeight() - 155, 205 + static_cast<int>(tracks.size()) * 56)));
            g.drawText(juce::String(static_cast<int>(beat / 4) + 1), static_cast<int>(x) + 2, 99, 32, 18, juce::Justification::centredLeft);
        }
        g.setColour(juce::Colours::white);
        const auto playheadX = 240.0f + static_cast<float>(songSeconds.load() / duration) * width;
        g.drawLine(playheadX, 145.0f, playheadX, 194.0f, 2.0f);
        int y = 205;
        for (auto& t : tracks)
        {
            g.setColour(juce::Colours::white);
            g.drawText(t->name, 20, y, 210, 46, juce::Justification::centredLeft, true);
            g.setColour(juce::Colour(0xff234552));
            g.fillRect(240.0f, static_cast<float>(y), width, 46.0f);
            g.setColour(juce::Colour(0xff79c8d4));
            g.fillRect(240.0f, static_cast<float>(y), static_cast<float>(t->transport.getLengthInSeconds() / duration) * width, 46.0f);
            g.setColour(juce::Colours::white);
            const auto x = 240.0f + static_cast<float>(t->transport.getCurrentPosition() / duration) * width;
            g.drawLine(x, static_cast<float>(y), x, static_cast<float>(y + 46), 2.0f);
            y += 56;
        }
        if (tracks.empty())
        {
            g.setColour(juce::Colours::lightgrey);
            g.drawText("Sem audio importado. Ative passos e compassos para criar um ritmo.", 20, 210, getWidth() - 40, 40, juce::Justification::centredLeft);
        }
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
            clips[i]->setButtonText(clips[i]->getToggleState() ? "P1" : "+");
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
    juce::TextButton importButton { "Importar MP3 / WAV" }, playButton { "Play" }, stopButton { "Stop" };
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
    bool scrubbing = false;
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
