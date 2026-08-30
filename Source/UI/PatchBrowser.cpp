#include "PatchBrowser.h"

namespace
{
constexpr auto kBg      = 0xff071018;
constexpr auto kPanel   = 0xff0a151e;
constexpr auto kHeader  = 0xff0d1c27;
constexpr auto kLine    = 0xff1c3443;
constexpr auto kSel     = 0xff0f80d8;
constexpr auto kText    = 0xffd5ebf7;
constexpr auto kDim     = 0xff8aa0ae;
constexpr auto kAll     = "ALL";
}

PatchBrowser::TagList::TagList()
{
    setModel(this);
    setColour(juce::ListBox::backgroundColourId, juce::Colour(kPanel));
    setColour(juce::ListBox::outlineColourId, juce::Colour(kLine));
    setRowHeight(22);
    setOutlineThickness(1);
}

void PatchBrowser::TagList::setItems(juce::StringArray next, const juce::String& selected)
{
    items = std::move(next);
    if (! items.contains(kAll, true))
        items.insert(0, kAll);
    updateContent();
    const int row = juce::jmax(0, items.indexOf(selected, true));
    selectRow(row);
}

juce::String PatchBrowser::TagList::getSelected() const
{
    const int r = getSelectedRow();
    if (r < 0 || r >= items.size())
        return kAll;
    return items[r];
}

void PatchBrowser::TagList::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= items.size())
        return;
    if (selected)
    {
        g.setColour(juce::Colour(kSel));
        g.fillRect(0, 0, w, h);
    }
    g.setColour(selected ? juce::Colours::white : juce::Colour(kText));
    g.setFont(juce::FontOptions(12.0f, row == 0 ? juce::Font::bold : juce::Font::plain));
    g.drawText(items[row], 8, 0, w - 12, h, juce::Justification::centredLeft, true);
}

void PatchBrowser::TagList::selectedRowsChanged(int)
{
    if (onChange != nullptr)
        onChange();
}

PatchBrowser::PatchBrowser(groove::ExternalPluginHost& h)
    : host(h)
{
    setOpaque(true);

    nameL.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    nameL.setColour(juce::Label::textColourId, juce::Colours::white);
    authorL.setColour(juce::Label::textColourId, juce::Colour(kDim));
    notesL.setColour(juce::Label::textColourId, juce::Colour(0xffb7cddd));
    tagRow.setColour(juce::Label::textColourId, juce::Colour(0xff7ac8ff));
    countL.setColour(juce::Label::textColourId, juce::Colour(kDim));
    countL.setJustificationType(juce::Justification::centredRight);
    notesL.setMinimumHorizontalScale(0.7f);

    search.setTextToShowWhenEmpty("search patches", juce::Colour(0xff587486));
    search.onTextChange = [this] { applyFilters(); };

    patchList.setModel(this);
    patchList.setColour(juce::ListBox::backgroundColourId, juce::Colour(kPanel));
    patchList.setColour(juce::ListBox::outlineColourId, juce::Colour(kLine));
    patchList.setRowHeight(22);
    patchList.setOutlineThickness(1);

    auto bind = [this](TagList& list)
    {
        list.onChange = [this]
        {
            if (! refreshing)
            {
                rebuildFilters();
                applyFilters();
            }
        };
        addAndMakeVisible(list);
    };
    bind(collections);
    bind(categories);
    bind(types);
    bind(timbres);

    for (auto* c : { &nameL, &authorL, &notesL, &tagRow, &countL })
        addAndMakeVisible(*c);
    addAndMakeVisible(search);
    addAndMakeVisible(patchList);

    for (auto* b : { &prevBtn, &nextBtn, &initBtn, &closeBtn })
    {
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff10202c));
        addAndMakeVisible(*b);
    }
    prevBtn.onClick = [this] { stepFiltered(-1); };
    nextBtn.onClick = [this] { stepFiltered(1); };
    initBtn.onClick = [this] { loadInit(); };
    closeBtn.onClick = [this]
    {
        if (onClose != nullptr)
            onClose();
    };

    refreshFromHost();
}

void PatchBrowser::refreshFromHost()
{
    rebuildFilters();
    applyFilters();
    showPatch(host.getKitIndex(), false);
}

void PatchBrowser::rebuildFilters()
{
    const juce::ScopedValueSetter<bool> sv(refreshing, true);
    const auto keepC = collections.getSelected();
    const auto keepA = categories.getSelected();
    const auto keepT = types.getSelected();
    const auto keepM = timbres.getSelected();

    juce::StringArray cols, cats, typ, tim;
    const int n = host.getKitCount();
    for (int i = 0; i < n; ++i)
    {
        const auto p = host.getPatchInfo(i);
        if (p.collection.isNotEmpty())
            cols.addIfNotAlreadyThere(p.collection);
        if (p.category.isNotEmpty())
            cats.addIfNotAlreadyThere(p.category);
        for (const auto& t : p.types)
            typ.addIfNotAlreadyThere(t);
        for (const auto& t : p.timbres)
            tim.addIfNotAlreadyThere(t);
    }
    cols.sortNatural();
    cats.sortNatural();
    typ.sortNatural();
    tim.sortNatural();
    collections.setItems(std::move(cols), keepC);
    categories.setItems(std::move(cats), keepA);
    types.setItems(std::move(typ), keepT);
    timbres.setItems(std::move(tim), keepM);
}

bool PatchBrowser::matches(const groove::ExternalPluginHost::PatchInfo& p) const
{
    const auto col = collections.getSelected();
    if (col != kAll && ! p.collection.equalsIgnoreCase(col))
        return false;
    const auto cat = categories.getSelected();
    if (cat != kAll && ! p.category.equalsIgnoreCase(cat))
        return false;
    const auto typ = types.getSelected();
    if (typ != kAll && ! p.types.contains(typ, true))
        return false;
    const auto tim = timbres.getSelected();
    if (tim != kAll && ! p.timbres.contains(tim, true))
        return false;
    const auto q = search.getText().trim();
    if (q.isNotEmpty() && ! p.name.containsIgnoreCase(q)
        && ! p.author.containsIgnoreCase(q)
        && ! p.notes.containsIgnoreCase(q))
        return false;
    return true;
}

void PatchBrowser::applyFilters()
{
    filtered.clear();
    const int n = host.getKitCount();
    const int current = host.getKitIndex();
    int select = 0;
    for (int i = 0; i < n; ++i)
    {
        if (! matches(host.getPatchInfo(i)))
            continue;
        if (i == current)
            select = (int) filtered.size();
        filtered.push_back(i);
    }
    countL.setText(juce::String((int) filtered.size()) + " / " + juce::String(n),
                   juce::dontSendNotification);
    patchList.updateContent();
    if (! filtered.empty())
        patchList.selectRow(select);
    else
        patchList.deselectAllRows();
}

int PatchBrowser::getNumRows()
{
    return (int) filtered.size();
}

void PatchBrowser::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) filtered.size())
        return;
    if (selected)
    {
        g.setColour(juce::Colour(kSel));
        g.fillRect(0, 0, w, h);
    }
    const auto p = host.getPatchInfo(filtered[(size_t) row]);
    g.setColour(selected ? juce::Colours::white : juce::Colour(kText));
    g.setFont(juce::FontOptions(12.5f));
    g.drawText(p.name, 8, 0, w - 12, h, juce::Justification::centredLeft, true);
}

void PatchBrowser::selectedRowsChanged(int row)
{
    if (refreshing || row < 0 || row >= (int) filtered.size())
        return;
    showPatch(filtered[(size_t) row], true);
}

void PatchBrowser::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= (int) filtered.size())
        return;
    showPatch(filtered[(size_t) row], true);
    if (onClose != nullptr)
        onClose();
}

void PatchBrowser::showPatch(int hostIndex, bool load)
{
    if (host.getKitCount() <= 0)
        return;
    hostIndex = juce::jlimit(0, host.getKitCount() - 1, hostIndex);
    if (load && host.getKitIndex() != hostIndex)
    {
        host.setKitIndex(hostIndex);
        if (onPatchChosen != nullptr)
            onPatchChosen();
    }
    const auto p = host.getPatchInfo(hostIndex);
    nameL.setText(p.name, juce::dontSendNotification);
    authorL.setText(p.author.isNotEmpty() ? p.author : " ", juce::dontSendNotification);
    notesL.setText(p.notes.isNotEmpty() ? p.notes : " ", juce::dontSendNotification);
    juce::StringArray tags;
    if (p.category.isNotEmpty())
        tags.add(p.category);
    tags.addArray(p.types);
    tags.addArray(p.timbres);
    tagRow.setText(tags.joinIntoString("   ·   "), juce::dontSendNotification);
}

void PatchBrowser::stepFiltered(int delta)
{
    if (filtered.empty())
        return;
    int row = patchList.getSelectedRow();
    if (row < 0)
        row = 0;
    row = (row + delta + (int) filtered.size()) % (int) filtered.size();
    patchList.selectRow(row);
    patchList.scrollToEnsureRowIsOnscreen(row);
}

void PatchBrowser::loadInit()
{
    const int n = host.getKitCount();
    for (int i = 0; i < n; ++i)
    {
        const auto name = host.getPatchInfo(i).name;
        if (name.equalsIgnoreCase("Init") || name.equalsIgnoreCase("Default"))
        {
            showPatch(i, true);
            applyFilters();
            return;
        }
    }
}

void PatchBrowser::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kBg));
    auto r = getLocalBounds().reduced(10);
    auto head = r.removeFromTop(92);
    g.setColour(juce::Colour(kPanel));
    g.fillRoundedRectangle(head.toFloat(), 6.0f);
    g.setColour(juce::Colour(kLine));
    g.drawRoundedRectangle(head.toFloat(), 6.0f, 1.0f);

    auto cols = r;
    cols.removeFromBottom(40);
    const int colW = cols.getWidth() / 5;
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    for (int i = 0; i < 5; ++i)
    {
        auto cell = cols.removeFromLeft(colW).reduced(2, 0);
        auto title = cell.removeFromTop(22);
        g.setColour(juce::Colour(kHeader));
        g.fillRect(title);
        g.setColour(juce::Colour(0xff2c98e8));
        g.drawText(headerLabels[i], title.reduced(6, 0), juce::Justification::centredLeft);
    }
}

void PatchBrowser::resized()
{
    auto r = getLocalBounds().reduced(10);
    auto head = r.removeFromTop(92).reduced(12, 8);
    countL.setBounds(head.removeFromRight(90));
    nameL.setBounds(head.removeFromTop(26));
    authorL.setBounds(head.removeFromTop(16));
    notesL.setBounds(head.removeFromTop(16));
    tagRow.setBounds(head);

    auto foot = r.removeFromBottom(36);
    r.removeFromBottom(6);
    initBtn.setBounds(foot.removeFromLeft(70).reduced(2));
    prevBtn.setBounds(foot.removeFromLeft(64).reduced(2));
    nextBtn.setBounds(foot.removeFromLeft(64).reduced(2));
    closeBtn.setBounds(foot.removeFromRight(80).reduced(2));

    const int colW = r.getWidth() / 5;
    auto place = [&](juce::Component& c, bool withSearch)
    {
        auto cell = r.removeFromLeft(colW).reduced(2, 0);
        cell.removeFromTop(22);
        if (withSearch)
        {
            search.setBounds(cell.removeFromTop(24).reduced(0, 1));
            cell.removeFromTop(2);
        }
        c.setBounds(cell);
    };
    place(collections, false);
    place(categories, false);
    place(types, false);
    place(timbres, false);
    place(patchList, true);
}

PatchBrowserWindow::PatchBrowserWindow(groove::ExternalPluginHost& host,
                                       juce::LookAndFeel& look,
                                       const juce::String& title)
    : DocumentWindow(title,
                     juce::Colour(kBg),
                     DocumentWindow::closeButton | DocumentWindow::minimiseButton),
      browser(host)
{
    setUsingNativeTitleBar(true);
    setLookAndFeel(&look);
    browser.setLookAndFeel(&look);
    setContentNonOwned(&browser, false);
    setResizable(true, false);
    setResizeLimits(820, 420, 1600, 900);
    centreWithSize(1040, 560);
    browser.onClose = [this] { setVisible(false); };
}

PatchBrowserWindow::~PatchBrowserWindow()
{
    clearContentComponent();
    setLookAndFeel(nullptr);
    browser.setLookAndFeel(nullptr);
}

void PatchBrowserWindow::showAndRefresh()
{
    browser.refreshFromHost();
    setVisible(true);
    toFront(true);
}
