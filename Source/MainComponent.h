#pragma once
#include "AudioEngine.h"
#include "Panels.h"
#include "SpectrumEq.h"
#include "UiKit.h"

// Scrollable content: spectrum/EQ, editors, side panels, mixer.
class Content : public juce::Component {
public:
    explicit Content(AudioEngine& e);

    void tick(int n);                       // 30 Hz from MainComponent
    void selectChannel(int c);
    void refreshDeviceNames(const juce::StringArray& in, const juce::StringArray& out);
    void channelCountChanged();
    int preferredHeight() const;
    void syncFromEngine();

    std::function<void()> onLayoutChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    AudioEngine& eng;
    int sel = 0;
    SpectrumEq graph;
    panels::EqEditorPanel eq;
    panels::NotchPanel notch;
    panels::DynamicsPanel dyn;
    panels::AfsPanel afs;
    panels::FxPanel fx;
    panels::TestPanel test;
    panels::MixerPanel mixer;
    std::array<juce::TextButton, ah::kMaxStrips> tabs;
    juce::Label domL;
    juce::Rectangle<int> specBounds;
    juce::StringArray inNames, outNames;
};

class MainComponent : public juce::Component, private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void openDeviceDialog();
    void refreshNames();
    void saveAll();
    void updateStatus();

    AudioEngine engine;
    juce::AudioDeviceManager dm;
    bool firstRun = true;
    std::unique_ptr<juce::XmlElement> savedAudio;
    int settingsLoaded = 0;     // initialised by loadSettings() before 'content' is built, so panels show saved values
    Content content;
    juce::Viewport viewport;
    juce::TextButton devBtn, afsBtn;
    juce::Label statusL, cpuL;
    int tickCount = 0, lastNumIn = -1, lastNumOut = -1, lastBuf = -1;
    bool autoChannels = true;
};
