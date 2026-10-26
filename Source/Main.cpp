#include "MainComponent.h"

class AntiHowlApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "AntiHowl AI"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override
    {
        juce::Process::setPriority(juce::Process::HighPriority);   // keep the audio thread fed under load
        juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }
    void shutdown() override
    {
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }
    void systemRequestedQuit() override { quit(); }

    class MainWindow : public juce::DocumentWindow {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name, ui::kBg, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            setResizeLimits(1100, 700, 4000, 4000);
            centreWithSize(1480, 960);
            setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

private:
    ui::AhLookAndFeel lnf;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(AntiHowlApplication)
