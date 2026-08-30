#pragma once
#include <JuceHeader.h>
#include "../Audio/ExternalPluginHost.h"
#include <functional>
#include <vector>

class PatchBrowser : public juce::Component,
                     private juce::ListBoxModel
{
public:
    explicit PatchBrowser(groove::ExternalPluginHost&);

    void paint(juce::Graphics&) override;
    void resized() override;
    void refreshFromHost();

    std::function<void()> onPatchChosen;
    std::function<void()> onClose;

private:
    class TagList : public juce::ListBox, public juce::ListBoxModel
    {
    public:
        TagList();
        void setItems(juce::StringArray, const juce::String& selected);
        juce::String getSelected() const;
        int getNumRows() override { return items.size(); }
        void paintListBoxItem(int, juce::Graphics&, int, int, bool) override;
        void selectedRowsChanged(int) override;
        std::function<void()> onChange;

    private:
        juce::StringArray items;
    };

    int getNumRows() override;
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override;
    void selectedRowsChanged(int) override;
    void listBoxItemDoubleClicked(int, const juce::MouseEvent&) override;

    void rebuildFilters();
    void applyFilters();
    bool matches(const groove::ExternalPluginHost::PatchInfo&) const;
    void showPatch(int hostIndex, bool load);
    void stepFiltered(int delta);
    void loadInit();

    groove::ExternalPluginHost& host;
    TagList collections, categories, types, timbres;
    juce::ListBox patchList;
    juce::TextEditor search;
    juce::Label nameL, authorL, notesL, countL;
    juce::Label tagRow;
    juce::TextButton prevBtn { "PREV" }, nextBtn { "NEXT" };
    juce::TextButton initBtn { "INIT" }, closeBtn { "CLOSE" };
    juce::StringArray headerLabels { "COLLECTION", "CATEGORY", "TYPES", "TIMBRES", "PATCH" };
    std::vector<int> filtered;
    bool refreshing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatchBrowser)
};

class PatchBrowserWindow : public juce::DocumentWindow
{
public:
    PatchBrowserWindow(groove::ExternalPluginHost&, juce::LookAndFeel&,
                       const juce::String& title);
    ~PatchBrowserWindow() override;
    void closeButtonPressed() override { setVisible(false); }
    void showAndRefresh();

    PatchBrowser browser;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatchBrowserWindow)
};
