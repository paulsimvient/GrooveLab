#pragma once
#include <JuceHeader.h>
#include <functional>
#include <vector>

class InstrumentBrowser : public juce::Component,
                          private juce::ListBoxModel
{
public:
    InstrumentBrowser();

    struct Entry
    {
        juce::String name;
        juce::String manufacturer;
        juce::String format;
        juce::File file;
    };

    void scanInstalledInstruments();
    void refresh();
    void paint(juce::Graphics&) override;
    void resized() override;

    std::function<void(const juce::File&)> onInstrumentChosen;
    std::function<void()> onClose;

private:
    int getNumRows() override;
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override;
    void selectedRowsChanged(int) override;
    void listBoxItemDoubleClicked(int, const juce::MouseEvent&) override;
    void applyFilter();
    void chooseRow(int);
    void addCandidatesFrom(const juce::File&, const juce::String& extension);

    juce::AudioPluginFormatManager formats;
    std::vector<Entry> entries;
    std::vector<int> filtered;
    juce::StringArray manufacturers;

    juce::TextEditor search;
    juce::ComboBox vendorFilter;
    juce::ListBox list;
    juce::Label title, count;
    juce::TextButton rescan { "RESCAN" }, load { "LOAD" }, close { "CLOSE" };
};

class InstrumentBrowserWindow : public juce::DocumentWindow
{
public:
    InstrumentBrowserWindow(juce::LookAndFeel&, const juce::String& title);
    ~InstrumentBrowserWindow() override;
    void closeButtonPressed() override { setVisible(false); }
    void showBrowser(const juce::String& slotName,
                     std::function<void(const juce::File&)> chooser);

    InstrumentBrowser browser;
private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InstrumentBrowserWindow)
};
