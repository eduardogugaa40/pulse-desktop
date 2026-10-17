// SPDX-License-Identifier: AGPL-3.0-only
#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>
#include <memory>

// Initial streaming playback foundation. Editing and plugin hosting are pending.
class Studio final : public juce::AudioAppComponent, private juce::Timer
{
public:
    Studio()
    {
        formats.registerBasicFormats();
        diskThread.startThread();
        for (auto* b : { &importButton, &playButton, &stopButton }) addAndMakeVisible(b);
        addAndMakeVisible(status);
        status.setText("Importe MP3/WAV. Cada arquivo ocupa uma pista, a partir do inicio.", juce::dontSendNotification);
        importButton.onClick = [this] { chooseFiles(); };
        playButton.onClick = [this] {
            for (auto& t : tracks) t->transport.start();
        };
        stopButton.onClick = [this] {
            for (auto& t : tracks) { t->transport.stop(); t->transport.setPosition(0); }
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
    void prepareToPlay(int blockSize, double sampleRate) override { mixer.prepareToPlay(blockSize, sampleRate); }
    void releaseResources() override { mixer.releaseResources(); }
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
    {
        mixer.getNextAudioBlock(info);
        // Fixed headroom for the current eight-track limit.
        info.buffer->applyGain(info.startSample, info.numSamples, 0.125f);
    }
    void resized() override
    {
        importButton.setBounds(20, 64, 160, 32);
        playButton.setBounds(190, 64, 90, 32);
        stopButton.setBounds(290, 64, 90, 32);
        status.setBounds(20, getHeight() - 44, getWidth() - 40, 30);
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff141820));
        g.setColour(juce::Colour(0xffb9e67a));
        g.setFont(28.0f);
        g.drawText("PULSE / desktop", 20, 15, 500, 36, juce::Justification::centredLeft);
        g.setFont(14.0f);
        const float width = static_cast<float>(getWidth() - 260);
        double duration = 10;
        for (auto& t : tracks) duration = juce::jmax(duration, t->transport.getLengthInSeconds());
        int y = 120;
        for (auto& t : tracks)
        {
            g.setColour(juce::Colours::white);
            g.drawText(t->name, 20, y, 210, 52, juce::Justification::centredLeft, true);
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
            g.drawText("Biblioteca vazia - importe arquivos para ouvir as pistas juntas.", 20, 125, getWidth() - 40, 40, juce::Justification::centredLeft);
        }
    }
private:
    struct Track
    {
        juce::String name;
        std::unique_ptr<juce::AudioFormatReaderSource> reader;
        juce::AudioTransportSource transport;
        ~Track() { transport.setSource(nullptr); }
    };
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
                for (auto& t : safe->tracks) { t->transport.stop(); t->transport.setPosition(0); }
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
                    safe->mixer.addInputSource(&track->transport, false);
                    safe->tracks.push_back(std::move(track));
                }
                safe->setAudioChannels(0, 2);
                safe->status.setText(juce::String(static_cast<int>(safe->tracks.size())) + " pistas. Ignorados/invalidos: " + juce::String(failed) + ". Limite inicial: 8 pistas.", juce::dontSendNotification);
                safe->repaint();
            });
    }
    void timerCallback() override { repaint(); }
    juce::AudioFormatManager formats;
    juce::TimeSliceThread diskThread { "Pulse disk reader" };
    std::vector<std::unique_ptr<Track>> tracks;
    juce::MixerAudioSource mixer;
    juce::TextButton importButton { "Importar MP3 / WAV" }, playButton { "Play" }, stopButton { "Stop" };
    juce::Label status;
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
