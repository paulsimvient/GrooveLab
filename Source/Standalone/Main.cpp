#include <JuceHeader.h>
#include "../UI/MainComponent.h"

class GrooveLabApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "the lil' God Projector"; }
    const juce::String getApplicationVersion() override { return "0.7.0"; }

    void initialise(const juce::String&) override
    {
        window = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        window.reset();
    }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow(juce::String name)
            : DocumentWindow(name,
                             juce::Colour(0xff020405),
                             DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            centreWithSize(getWidth(), getHeight());
            setResizable(true, true);
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION(GrooveLabApplication)
