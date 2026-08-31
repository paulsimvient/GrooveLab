#include "InstrumentBrowser.h"
#include <algorithm>

namespace
{
const juce::Colour bg      { 0xff071018 };
const juce::Colour panel   { 0xff0b1720 };
const juce::Colour line    { 0xff1b3443 };
const juce::Colour blue    { 0xff32a9f4 };
const juce::Colour text    { 0xffd9eaf2 };
const juce::Colour dim     { 0xff839aa8 };
}

InstrumentBrowser::InstrumentBrowser()
{
    formats.addDefaultFormats();
    setOpaque(true);

    title.setText("INSTALLED INSTRUMENTS", juce::dontSendNotification);
    title.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, text);
    count.setColour(juce::Label::textColourId, dim);
    count.setJustificationType(juce::Justification::centredRight);

    search.setTextToShowWhenEmpty("Search instruments...", dim);
    search.onTextChange = [this] { applyFilter(); };

    vendorFilter.addItem("ALL MANUFACTURERS", 1);
    vendorFilter.setSelectedId(1, juce::dontSendNotification);
    vendorFilter.onChange = [this] { applyFilter(); };

    list.setModel(this);
    list.setRowHeight(34);
    list.setColour(juce::ListBox::backgroundColourId, panel);
    list.setColour(juce::ListBox::outlineColourId, line);
    list.setOutlineThickness(1);

    for (auto* c : std::initializer_list<juce::Component*>{ &title, &count, &search, &vendorFilter, &list, &rescan, &load, &close })
        addAndMakeVisible(*c);

    rescan.onClick = [this] { scanInstalledInstruments(); };
    load.onClick = [this] { chooseRow(list.getSelectedRow()); };
    close.onClick = [this] { if (onClose) onClose(); };

    scanInstalledInstruments();
}

void InstrumentBrowser::addCandidatesFrom(const juce::File& dir, const juce::String& extension)
{
    if (! dir.isDirectory()) return;
    juce::Array<juce::File> files;
    dir.findChildFiles(files, juce::File::findFilesAndDirectories, false, "*" + extension);

    for (const auto& file : files)
    {
        juce::OwnedArray<juce::PluginDescription> descriptions;
        for (int i = 0; i < formats.getNumFormats(); ++i)
        {
            auto* format = formats.getFormat(i);
            juce::OwnedArray<juce::PluginDescription> found;
            format->findAllTypesForFile(found, file.getFullPathName());
            for (auto* d : found)
            {
                if (! d->isInstrument) continue;
                Entry e;
                e.name = d->name.isNotEmpty() ? d->name : file.getFileNameWithoutExtension();
                e.manufacturer = d->manufacturerName.isNotEmpty() ? d->manufacturerName : "OTHER";
                e.format = d->pluginFormatName;
                e.file = file;

                // Prefer VST3 if AU + VST3 describe the same instrument.
                auto same = std::find_if(entries.begin(), entries.end(), [&](const Entry& x)
                {
                    return x.name.equalsIgnoreCase(e.name)
                        && x.manufacturer.equalsIgnoreCase(e.manufacturer);
                });
                if (same == entries.end())
                    entries.push_back(e);
                else if (e.format.containsIgnoreCase("VST3") && ! same->format.containsIgnoreCase("VST3"))
                    *same = e;
            }
        }
    }
}

void InstrumentBrowser::scanInstalledInstruments()
{
    entries.clear();
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    addCandidatesFrom(juce::File("/Library/Audio/Plug-Ins/VST3"), ".vst3");
    addCandidatesFrom(home.getChildFile("Library/Audio/Plug-Ins/VST3"), ".vst3");
    addCandidatesFrom(juce::File("/Library/Audio/Plug-Ins/Components"), ".component");
    addCandidatesFrom(home.getChildFile("Library/Audio/Plug-Ins/Components"), ".component");

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b)
    {
        const int vendor = a.manufacturer.compareIgnoreCase(b.manufacturer);
        return vendor == 0 ? a.name.compareIgnoreCase(b.name) < 0 : vendor < 0;
    });

    manufacturers.clear();
    for (const auto& e : entries) manufacturers.addIfNotAlreadyThere(e.manufacturer);
    manufacturers.sortNatural();
    vendorFilter.clear(juce::dontSendNotification);
    vendorFilter.addItem("ALL MANUFACTURERS", 1);
    for (int i = 0; i < manufacturers.size(); ++i)
        vendorFilter.addItem(manufacturers[i], i + 2);
    vendorFilter.setSelectedId(1, juce::dontSendNotification);
    applyFilter();
}

void InstrumentBrowser::refresh() { applyFilter(); }

void InstrumentBrowser::applyFilter()
{
    filtered.clear();
    const auto q = search.getText().trim();
    const int vendorId = vendorFilter.getSelectedId();
    const auto vendor = vendorId > 1 && vendorId - 2 < manufacturers.size()
                      ? manufacturers[vendorId - 2] : juce::String();
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];
        if (vendor.isNotEmpty() && ! e.manufacturer.equalsIgnoreCase(vendor)) continue;
        if (q.isNotEmpty() && ! e.name.containsIgnoreCase(q)
                           && ! e.manufacturer.containsIgnoreCase(q)) continue;
        filtered.push_back(i);
    }
    count.setText(juce::String((int) filtered.size()) + " instruments", juce::dontSendNotification);
    list.updateContent();
    if (! filtered.empty()) list.selectRow(0); else list.deselectAllRows();
}

int InstrumentBrowser::getNumRows() { return (int) filtered.size(); }

void InstrumentBrowser::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) filtered.size()) return;
    const auto& e = entries[(size_t) filtered[(size_t) row]];
    if (selected) { g.setColour(juce::Colour(0xff153448)); g.fillRect(0, 0, w, h); }
    g.setColour(selected ? juce::Colours::white : text);
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawText(e.name, 10, 2, w - 150, h / 2, juce::Justification::centredLeft, true);
    g.setColour(dim);
    g.setFont(juce::FontOptions(10.5f));
    g.drawText(e.manufacturer + "  ·  " + e.format, 10, h / 2 - 1, w - 20, h / 2, juce::Justification::centredLeft, true);
}

void InstrumentBrowser::selectedRowsChanged(int) {}
void InstrumentBrowser::listBoxItemDoubleClicked(int row, const juce::MouseEvent&) { chooseRow(row); }
void InstrumentBrowser::chooseRow(int row)
{
    if (row < 0 || row >= (int) filtered.size()) return;
    if (onInstrumentChosen) onInstrumentChosen(entries[(size_t) filtered[(size_t) row]].file);
}

void InstrumentBrowser::paint(juce::Graphics& g)
{
    g.fillAll(bg);
    auto r = getLocalBounds().reduced(10);
    g.setColour(panel); g.fillRoundedRectangle(r.toFloat(), 6.0f);
    g.setColour(line); g.drawRoundedRectangle(r.toFloat(), 6.0f, 1.0f);
}

void InstrumentBrowser::resized()
{
    auto r = getLocalBounds().reduced(22);
    auto head = r.removeFromTop(32);
    title.setBounds(head.removeFromLeft(320));
    count.setBounds(head.removeFromRight(140));
    r.removeFromTop(8);
    auto filters = r.removeFromTop(30);
    search.setBounds(filters.removeFromLeft(360).reduced(1));
    filters.removeFromLeft(8);
    vendorFilter.setBounds(filters.removeFromLeft(260).reduced(1));
    filters.removeFromLeft(8);
    rescan.setBounds(filters.removeFromLeft(88).reduced(1));
    r.removeFromTop(8);
    auto foot = r.removeFromBottom(34);
    load.setBounds(foot.removeFromRight(90).reduced(2));
    close.setBounds(foot.removeFromRight(90).reduced(2));
    r.removeFromBottom(6);
    list.setBounds(r);
}

InstrumentBrowserWindow::InstrumentBrowserWindow(juce::LookAndFeel& look, const juce::String& windowTitle)
    : DocumentWindow(windowTitle, bg, DocumentWindow::closeButton | DocumentWindow::minimiseButton)
{
    setUsingNativeTitleBar(true);
    setLookAndFeel(&look);
    browser.setLookAndFeel(&look);
    setContentNonOwned(&browser, false);
    setResizable(true, false);
    setResizeLimits(620, 440, 1300, 900);
    centreWithSize(760, 650);
    browser.onClose = [this] { setVisible(false); };
}

InstrumentBrowserWindow::~InstrumentBrowserWindow()
{
    clearContentComponent();
    browser.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void InstrumentBrowserWindow::showBrowser(const juce::String& slotName,
                                           std::function<void(const juce::File&)> chooser)
{
    setName("INSTRUMENTS  ·  " + slotName);
    browser.onInstrumentChosen = [this, chooser = std::move(chooser)](const juce::File& f)
    {
        if (chooser) chooser(f);
        setVisible(false);
    };
    browser.refresh();
    setVisible(true);
    toFront(true);
}
