#include "SongPage.h"
#include <array>
#include <cmath>

namespace
{
juce::Colour partColour(groove::SongPart part)
{
    switch (part)
    {
        case groove::SongPart::intro:     return juce::Colour(0xff8aa0ae);
        case groove::SongPart::verse:     return juce::Colour(0xff3ba7ff);
        case groove::SongPart::prechorus: return juce::Colour(0xff46d6d8);
        case groove::SongPart::chorus:    return juce::Colour(0xffff8a22);
        case groove::SongPart::bridge:    return juce::Colour(0xffb85cff);
        case groove::SongPart::breakdown: return juce::Colour(0xffff4f8a);
        case groove::SongPart::fill:      return juce::Colour(0xffffc438);
        case groove::SongPart::outro:     return juce::Colour(0xff8ed044);
        default:                          return juce::Colour(0xff3ba7ff);
    }
}

int divisionId(float d)
{
    return d < 0.375f ? 1 : d < 0.75f ? 2 : d < 1.5f ? 3 : d < 3.0f ? 4 : 5;
}

float divisionFromId(int id)
{
    const float values[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
    return values[juce::jlimit(1, 5, id) - 1];
}

void fillCountBox(juce::ComboBox& box, int first, int last, int selected)
{
    box.clear(juce::dontSendNotification);
    for (int v = first; v <= last; ++v)
        box.addItem(juce::String(v), v - first + 1);
    const int id = juce::jlimit(1, juce::jmax(1, last - first + 1), selected - first + 1);
    box.setSelectedId(id, juce::dontSendNotification);
}

void setupLinear(juce::Slider& s, double min, double max)
{
    s.setRange(min, max, 0.01);
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
}
}

SongPage::SongPage(groove::GrooveEngine& e)
    : engine(e)
{
    setOpaque(true);
    followButton.setClickingTogglesState(true);
    followButton.onClick = [this]
    {
        engine.setSongFollow(followButton.getToggleState());
        if (onSongChanged) onSongChanged();
        repaint();
    };
    addAndMakeVisible(followButton);

    auto bindAdd = [this](juce::TextButton& b, groove::SongPart part)
    {
        b.onClick = [this, part] { addPart(part); };
        addAndMakeVisible(b);
    };
    bindAdd(introButton, groove::SongPart::intro);
    bindAdd(verseButton, groove::SongPart::verse);
    bindAdd(preButton, groove::SongPart::prechorus);
    bindAdd(chorusButton, groove::SongPart::chorus);
    bindAdd(bridgeButton, groove::SongPart::bridge);
    bindAdd(fillButton, groove::SongPart::fill);
    bindAdd(outroButton, groove::SongPart::outro);

    static constexpr groove::SongPart kAllParts[] = {
        groove::SongPart::intro, groove::SongPart::verse, groove::SongPart::prechorus,
        groove::SongPart::chorus, groove::SongPart::bridge, groove::SongPart::breakdown,
        groove::SongPart::fill, groove::SongPart::outro
    };
    for (const auto part : kAllParts)
        partBox.addItem(groove::songPartName(part), (int) part + 1);
    partBox.onChange = [this]
    {
        if (refreshing) return;
        engine.setSongSectionPart(engine.state().song.current,
                                  (groove::SongPart) (partBox.getSelectedId() - 1));
        if (onSongChanged) onSongChanged();
        repaint();
    };
    addAndMakeVisible(partBox);

    for (int i = 1; i <= 16; ++i)
        barsBox.addItem(juce::String(i) + (i == 1 ? " BAR" : " BARS"), i);
    barsBox.onChange = [this]
    {
        if (refreshing) return;
        engine.setSongSectionBars(engine.state().song.current, barsBox.getSelectedId());
        if (onSongChanged) onSongChanged();
        repaint();
    };
    addAndMakeVisible(barsBox);
    barMinus.setTooltip("Shrink this section by half (8 → 4 bars)");
    barPlus.setTooltip("Stretch this section by doubling (4 → 8 bars, repeats)");
    barMinus.onClick = [this] { expandOrContractBars(engine.state().song.current, false); };
    barPlus.onClick = [this] { expandOrContractBars(engine.state().song.current, true); };
    addAndMakeVisible(barMinus);
    addAndMakeVisible(barPlus);

    for (int i = 0; i < groove::kMeterCount; ++i)
        meterBox.addItem(groove::meterName((groove::Meter) i), i + 1);
    meterBox.onChange = [this]
    {
        if (refreshing) return;
        engine.setMeter((groove::Meter) (meterBox.getSelectedId() - 1));
        refreshFromEngine();
        if (onSongChanged) onSongChanged();
    };
    addAndMakeVisible(meterBox);
    for (int i = 0; i < groove::kMeterTransformCount; ++i)
        meterTransformBox.addItem(groove::meterTransformName((groove::MeterTransform) i), i + 1);
    meterTransformBox.setTooltip("CROP drops extra beats. REFLOW keeps the groove and moves bar lines. SQUEEZE fits the old bar into the new one.");
    meterTransformBox.onChange = [this]
    {
        if (refreshing) return;
        engine.setMeterTransform((groove::MeterTransform) (meterTransformBox.getSelectedId() - 1));
        if (onSongChanged) onSongChanged();
        repaint();
    };
    addAndMakeVisible(meterTransformBox);

    recButton.setClickingTogglesState(true);
    recButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffc62828));
    recButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    recButton.onClick = [this]
    {
        engine.setRecording(recButton.getToggleState());
        refreshFromEngine();
        if (onSongChanged) onSongChanged();
        repaint();
    };
    addAndMakeVisible(recButton);

    quantizeButton.setClickingTogglesState(true);
    quantizeButton.setToggleState(true, juce::dontSendNotification);
    quantizeButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff0f80d8));
    quantizeButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    quantizeButton.setTooltip("Snap recorded notes to the grid when you take");
    quantizeButton.onClick = [this]
    {
        engine.setRecordQuantize(quantizeButton.getToggleState());
        quantizeBox.setEnabled(quantizeButton.getToggleState());
        if (quantizeButton.getToggleState() && quantizeBox.getSelectedId() <= 0)
            quantizeBox.setSelectedId(groove::kDefaultQuantizeNote + 1, juce::dontSendNotification);
    };
    addAndMakeVisible(quantizeButton);
    for (int i = 0; i < groove::kQuantizeNoteCount; ++i)
        quantizeBox.addItem(groove::kQuantizeNoteNames[i], i + 1);
    quantizeBox.setSelectedId(groove::kDefaultQuantizeNote + 1, juce::dontSendNotification);
    quantizeBox.setTooltip("Quantize grid");
    quantizeBox.onChange = [this]
    {
        if (refreshing) return;
        engine.setRecordQuantizeNote(quantizeBox.getSelectedId() - 1);
    };
    addAndMakeVisible(quantizeBox);

    keepTakeButton.onClick = [this]
    {
        engine.keepCurrentTake();
        refreshFromEngine();
        if (onSongChanged) onSongChanged();
        repaint();
    };
    addAndMakeVisible(keepTakeButton);
    deleteTakeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3b1010));
    deleteTakeButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffff8a8a));
    deleteTakeButton.onClick = [this]
    {
        engine.deleteCurrentTake();
        refreshFromEngine();
        if (onSongChanged) onSongChanged();
        repaint();
    };
    addAndMakeVisible(deleteTakeButton);

    for (int i = 0; i < groove::kMidiLanes; ++i)
    {
        laneHits[(size_t) i].onPick = [this, i](bool openUi)
        {
            const int ch = groove::midiLaneChannel(i);
            if (onChannelClicked)
                onChannelClicked(ch);
            if (openUi && onInstrumentUiClicked)
                onInstrumentUiClicked(ch);
        };
        addAndMakeVisible(laneHits[(size_t) i]);
    }

    duplicateButton.onClick = [this]
    {
        engine.duplicateSongSection(engine.state().song.current);
        refreshFromEngine();
        if (onSongChanged) onSongChanged();
    };
    deleteButton.onClick = [this]
    {
        engine.removeSongSection(engine.state().song.current);
        refreshFromEngine();
        if (onSongChanged) onSongChanged();
    };
    addAndMakeVisible(duplicateButton);
    addAndMakeVisible(deleteButton);

    for (int t = 0; t < groove::kTracks; ++t)
    {
        auto& row = rows[(size_t) t];
        row.name.setText(groove::voiceName(t), juce::dontSendNotification);
        row.name.setColour(juce::Label::textColourId, juce::Colour(0xffd5ebf7));
        row.name.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        row.name.setBorderSize({});
        row.name.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(row.name);

        row.division.addItem("1/4x", 1);
        row.division.addItem("1/2x", 2);
        row.division.addItem("1x", 3);
        row.division.addItem("2x", 4);
        row.division.addItem("4x", 5);
        setupLinear(row.probability, 0.0, 1.0);
        setupLinear(row.velocity, 0.0, 1.2);

        auto bind = [this, t](juce::ComboBox& box)
        {
            box.onChange = [this, t]
            {
                if (! refreshing)
                    commitTrackRow(t);
            };
            addAndMakeVisible(box);
        };
        bind(row.steps);
        bind(row.pulses);
        bind(row.rotate);
        bind(row.division);

        row.probability.onValueChange = [this, t]
        {
            if (! refreshing)
                commitTrackRow(t);
        };
        row.velocity.onValueChange = [this, t]
        {
            if (! refreshing)
                commitTrackRow(t);
        };
        addAndMakeVisible(row.probability);
        addAndMakeVisible(row.velocity);
    }

    refreshFromEngine();
    startTimerHz(24);
}

void SongPage::setActiveMidiChannel(int channel)
{
    activeMidiChannel = juce::jlimit(1, 16, channel > 0 ? channel : 1);
    repaint();
}

void SongPage::showPartMenu(int sectionIndex, juce::Point<int> screenPos)
{
    const auto& sections = engine.state().song.sections;
    if (sectionIndex < 0 || sectionIndex >= (int) sections.size())
        return;

    juce::PopupMenu menu;
    static constexpr groove::SongPart kAllParts[] = {
        groove::SongPart::intro, groove::SongPart::verse, groove::SongPart::prechorus,
        groove::SongPart::chorus, groove::SongPart::bridge, groove::SongPart::breakdown,
        groove::SongPart::fill, groove::SongPart::outro
    };
    const auto current = sections[(size_t) sectionIndex].part;
    for (const auto part : kAllParts)
        menu.addItem((int) part + 1, groove::songPartName(part), true, part == current);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                           { screenPos.x, screenPos.y, 1, 1 }),
        [this, sectionIndex](int result)
        {
            if (result <= 0)
                return;
            engine.setSongSectionPart(sectionIndex, (groove::SongPart) (result - 1));
            refreshFromEngine();
            if (onSongChanged) onSongChanged();
        });
}

juce::Rectangle<int> SongPage::sectionNameArea(int index) const
{
    auto tile = sectionTile(index);
    if (tile.isEmpty())
        return {};
    return tile.reduced(8, 8).removeFromTop(22).withTrimmedRight(52);
}

juce::Rectangle<int> SongPage::sectionDeleteArea(int index) const
{
    auto tile = sectionTile(index);
    if (tile.isEmpty())
        return {};
    return tile.reduced(6, 6).removeFromTop(18).removeFromRight(18);
}

juce::Rectangle<int> SongPage::sectionResizeHandle(int index) const
{
    auto tile = sectionTile(index);
    if (tile.isEmpty())
        return {};
    return tile.removeFromRight(juce::jlimit(18, 28, tile.getWidth() / 4));
}

int SongPage::midiLaneAt(juce::Point<int> pos) const
{
    auto r = lanesPanel;
    r.removeFromTop(34);
    if (! r.contains(pos))
        return -1;
    const int rowH = juce::jmax(18, r.getHeight() / groove::kMidiLanes);
    const int lane = (pos.y - r.getY()) / rowH;
    if (lane < 0 || lane >= groove::kMidiLanes)
        return -1;
    return lane;
}

int SongPage::midiStepAt(juce::Point<int> pos, int lane) const
{
    if (lane < 0 || lane >= groove::kMidiLanes)
        return -1;
    auto r = lanesPanel.reduced(16, 0);
    r.removeFromTop(34);
    const int rowH = juce::jmax(18, r.getHeight() / groove::kMidiLanes);
    auto row = r.removeFromTop(rowH * (lane + 1)).removeFromBottom(rowH).reduced(0, 1);
    row.removeFromLeft(kLaneLabelW);
    const auto& srcLanes = engine.state().midiLanes;
    if (! srcLanes[(size_t) lane].patches.empty())
        row.removeFromRight(90);
    auto dots = row.reduced(6, 5);
    if (! dots.contains(pos) || dots.getWidth() <= 0)
        return -1;
    const int step = (int) ((pos.x - dots.getX()) * groove::kSteps / (float) dots.getWidth());
    return juce::jlimit(0, groove::kSteps - 1, step);
}

bool SongPage::deleteSelectedNote()
{
    if (selectedLane < 0 || selectedLaneStep < 0)
        return false;
    engine.deleteLaneNotesAt(selectedLane, selectedLaneStep);
    selectedLane = -1;
    selectedLaneStep = -1;
    refreshFromEngine();
    if (onSongChanged) onSongChanged();
    return true;
}

void SongPage::expandOrContractBars(int index, bool expand)
{
    const auto& sections = engine.state().song.sections;
    if (index < 0 || index >= (int) sections.size())
        return;
    const int bars = juce::jmax(1, sections[(size_t) index].bars);
    const int next = expand ? juce::jmin(16, bars * 2) : juce::jmax(1, bars / 2);
    if (next == bars)
        return;
    engine.setSongSectionBars(index, next);
    refreshFromEngine();
    if (onSongChanged) onSongChanged();
}

void SongPage::addPart(groove::SongPart part)
{
    engine.addSongSection(part);
    refreshFromEngine();
    if (onSongChanged) onSongChanged();
}

void SongPage::fillRhythmBoxes(int track, const groove::TrackShape& sh)
{
    auto& row = rows[(size_t) track];
    const int steps = juce::jlimit(1, groove::kSteps, sh.generatorSteps);
    fillCountBox(row.steps, 1, groove::kSteps, steps);
    fillCountBox(row.pulses, 0, steps, juce::jlimit(0, steps, sh.pulses));
    const int rot = ((sh.rotate % steps) + steps) % steps;
    fillCountBox(row.rotate, 0, juce::jmax(0, steps - 1), rot);
    row.division.setSelectedId(divisionId(sh.division), juce::dontSendNotification);
}

void SongPage::commitTrackRow(int track)
{
    const auto& song = engine.state().song;
    if (song.sections.empty() || track < 0 || track >= groove::kTracks)
        return;

    auto& row = rows[(size_t) track];
    groove::TrackShape sh;
    sh.generatorSteps = juce::jlimit(1, groove::kSteps, row.steps.getSelectedId());
    sh.pulses = juce::jlimit(0, sh.generatorSteps, row.pulses.getSelectedId() - 1);
    sh.rotate = juce::jmax(0, row.rotate.getSelectedId() - 1);
    sh.division = divisionFromId(row.division.getSelectedId());
    sh.probability = (float) row.probability.getValue();
    sh.velocity = (float) row.velocity.getValue();
    engine.setSectionTrackShape(song.current, track, sh);

    refreshing = true;
    fillRhythmBoxes(track, sh);
    refreshing = false;
    if (onSongChanged) onSongChanged();
    repaint();
}

void SongPage::refreshFromEngine()
{
    refreshing = true;
    const auto& song = engine.state().song;
    followButton.setToggleState(song.follow, juce::dontSendNotification);
    if (! song.sections.empty())
    {
        const int i = juce::jlimit(0, (int) song.sections.size() - 1, song.current);
        const auto& section = song.sections[(size_t) i];
        partBox.setSelectedId((int) section.part + 1, juce::dontSendNotification);
        barsBox.setSelectedId(juce::jlimit(1, 16, section.bars), juce::dontSendNotification);
        meterBox.setSelectedId((int) section.meter + 1, juce::dontSendNotification);
        meterTransformBox.setSelectedId((int) engine.state().meterTransform + 1, juce::dontSendNotification);
        for (int t = 0; t < groove::kTracks; ++t)
        {
            const auto& sh = section.shapes[(size_t) t];
            fillRhythmBoxes(t, sh);
            rows[(size_t) t].probability.setValue(sh.probability, juce::dontSendNotification);
            rows[(size_t) t].velocity.setValue(sh.velocity, juce::dontSendNotification);
        }
    }
    recButton.setToggleState(engine.isRecording(), juce::dontSendNotification);
    quantizeButton.setToggleState(engine.isRecordQuantize(), juce::dontSendNotification);
    quantizeBox.setSelectedId(engine.getRecordQuantizeNote() + 1, juce::dontSendNotification);
    quantizeBox.setEnabled(engine.isRecordQuantize());
    deleteTakeButton.setEnabled(! song.sections.empty());
    rebuildTakeButtons();
    refreshing = false;
    repaint();
}

void SongPage::timerCallback()
{
    recButton.setToggleState(engine.isRecording(), juce::dontSendNotification);
    followButton.setToggleState(engine.state().song.follow, juce::dontSendNotification);
    repaint();
}

void SongPage::rebuildTakeButtons()
{
    takeButtons.clear();
    if (engine.state().song.sections.empty())
        return;
    const int i = juce::jlimit(0, (int) engine.state().song.sections.size() - 1,
                               engine.state().song.current);
    const auto& section = engine.state().song.sections[(size_t) i];
    for (int t = 0; t < (int) section.takes.size(); ++t)
    {
        auto* b = takeButtons.add(new juce::TextButton(section.takes[(size_t) t].label));
        b->setClickingTogglesState(true);
        b->setRadioGroupId(77);
        b->setToggleState(section.currentTake == t, juce::dontSendNotification);
        b->onClick = [this, t]
        {
            engine.restoreTake(t);
            refreshFromEngine();
            if (onSongChanged) onSongChanged();
        };
        addAndMakeVisible(b);
    }
    resized();
}

int SongPage::sectionIndexAt(juce::Point<int> pos) const
{
    const auto& sections = engine.state().song.sections;
    for (int i = 0; i < (int) sections.size(); ++i)
        if (sectionTile(i).contains(pos))
            return i;
    return -1;
}

juce::Rectangle<int> SongPage::sectionTile(int index) const
{
    auto r = arrangePanel.reduced(16);
    r.removeFromTop(78);
    const auto& sections = engine.state().song.sections;
    if (sections.empty() || index < 0 || index >= (int) sections.size())
        return {};

    int totalBars = 0;
    for (const auto& s : sections)
        totalBars += juce::jmax(1, s.bars);
    totalBars = juce::jmax(1, totalBars);

    int before = 0;
    for (int i = 0; i < index; ++i)
        before += juce::jmax(1, sections[(size_t) i].bars);

    const int gap = 8;
    const int usable = r.getWidth() - gap * juce::jmax(0, (int) sections.size() - 1);
    const int x = r.getX() + (usable * before) / totalBars + index * gap;
    const int w = juce::jmax(72, (usable * juce::jmax(1, sections[(size_t) index].bars)) / totalBars);
    return { x, r.getY(), w, r.getHeight() };
}

int SongPage::insertIndexForX(int x) const
{
    const auto& sections = engine.state().song.sections;
    const int n = (int) sections.size();
    for (int i = 0; i < n; ++i)
        if (x < sectionTile(i).getCentreX())
            return i;
    return n;
}

void SongPage::resized()
{
    auto bounds = getLocalBounds().reduced(16, 12);
    editPanel = bounds.removeFromBottom(220);
    bounds.removeFromBottom(8);
    lanesPanel = bounds.removeFromBottom(132);
    bounds.removeFromBottom(8);
    arrangePanel = bounds;

    auto er = editPanel.reduced(16, 0);
    auto title = er.removeFromTop(38);
    recButton.setBounds(title.removeFromRight(64).reduced(0, 4));
    title.removeFromRight(8);
    deleteTakeButton.setBounds(title.removeFromRight(96).reduced(0, 4));
    title.removeFromRight(8);
    keepTakeButton.setBounds(title.removeFromRight(92).reduced(0, 4));
    title.removeFromRight(8);
    quantizeBox.setBounds(title.removeFromRight(58).reduced(0, 4));
    title.removeFromRight(4);
    quantizeButton.setBounds(title.removeFromRight(86).reduced(0, 4));
    auto tools = er.removeFromTop(32);
    followButton.setBounds(tools.removeFromLeft(118));
    tools.removeFromLeft(8);
    partBox.setBounds(tools.removeFromLeft(104));
    tools.removeFromLeft(8);
    barMinus.setBounds(tools.removeFromLeft(28).reduced(1, 2));
    barsBox.setBounds(tools.removeFromLeft(88));
    barPlus.setBounds(tools.removeFromLeft(28).reduced(1, 2));
    tools.removeFromLeft(8);
    meterBox.setBounds(tools.removeFromLeft(72));
    tools.removeFromLeft(8);
    meterTransformBox.setBounds(tools.removeFromLeft(82));
    tools.removeFromLeft(8);
    duplicateButton.setBounds(tools.removeFromLeft(110));
    tools.removeFromLeft(8);
    deleteButton.setBounds(tools.removeFromLeft(90));

    auto takeRow = er.removeFromTop(28);
    const int takeW = juce::jmax(36, juce::jmin(72, takeRow.getWidth() / juce::jmax(1, takeButtons.size())));
    for (auto* b : takeButtons)
        b->setBounds(takeRow.removeFromLeft(takeW).reduced(2, 2));

    er.removeFromTop(18);
    const int rowH = juce::jmax(22, er.getHeight() / groove::kTracks);
    const int nameW = 58;
    const int comboW = juce::jmax(54, (er.getWidth() - nameW - 180) / 4);
    for (int t = 0; t < groove::kTracks; ++t)
    {
        auto row = er.removeFromTop(rowH).reduced(0, 1);
        auto& r = rows[(size_t) t];
        r.name.setBounds(row.removeFromLeft(nameW));
        r.steps.setBounds(row.removeFromLeft(comboW).reduced(2, 1));
        r.pulses.setBounds(row.removeFromLeft(comboW).reduced(2, 1));
        r.rotate.setBounds(row.removeFromLeft(comboW).reduced(2, 1));
        r.division.setBounds(row.removeFromLeft(comboW).reduced(2, 1));
        r.probability.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(4, 2));
        r.velocity.setBounds(row.reduced(4, 2));
    }

    auto top = arrangePanel.reduced(16, 0);
    top.removeFromTop(42);
    top = top.removeFromTop(32);
    const int bw = juce::jmax(70, top.getWidth() / 7);
    introButton.setBounds(top.removeFromLeft(bw).reduced(3, 0));
    verseButton.setBounds(top.removeFromLeft(bw).reduced(3, 0));
    preButton.setBounds(top.removeFromLeft(bw).reduced(3, 0));
    chorusButton.setBounds(top.removeFromLeft(bw).reduced(3, 0));
    bridgeButton.setBounds(top.removeFromLeft(bw).reduced(3, 0));
    fillButton.setBounds(top.removeFromLeft(bw).reduced(3, 0));
    outroButton.setBounds(top.removeFromLeft(bw).reduced(3, 0));

    auto laneBody = lanesPanel.reduced(16, 0);
    laneBody.removeFromTop(34);
    const int laneH = juce::jmax(18, laneBody.getHeight() / groove::kMidiLanes);
    for (int i = 0; i < groove::kMidiLanes; ++i)
    {
        laneHits[(size_t) i].setBounds(laneBody.removeFromTop(laneH).reduced(0, 1));
        laneHits[(size_t) i].toFront(false);
    }
}

void SongPage::mouseDown(const juce::MouseEvent& e)
{
    dragging = false;
    dragFrom = -1;
    dragInsert = -1;
    resizeIndex = -1;

    const int lane = midiLaneAt(e.getPosition());
    if (lane >= 0)
    {
        const int ch = groove::midiLaneChannel(lane);
        auto label = lanesPanel.reduced(16, 0);
        label.removeFromTop(34);
        const bool openUi = e.x < label.getX() + kLaneLabelW;
        if (onChannelClicked)
            onChannelClicked(ch);
        if (openUi && onInstrumentUiClicked)
        {
            onInstrumentUiClicked(ch);
            return;
        }
        const int step = midiStepAt(e.getPosition(), lane);
        if (step >= 0)
        {
            selectedLane = lane;
            selectedLaneStep = step;
            if (e.mods.isPopupMenu() || e.mods.isRightButtonDown() || e.mods.isAltDown())
                deleteSelectedNote();
            else
                repaint();
        }
        return;
    }

    const auto& sections = engine.state().song.sections;
    for (int i = 0; i < (int) sections.size(); ++i)
    {
        const auto tile = sectionTile(i);
        if (! tile.contains(e.getPosition()))
            continue;

        if (sectionDeleteArea(i).contains(e.getPosition()))
        {
            engine.removeSongSection(i);
            refreshFromEngine();
            if (onSongChanged) onSongChanged();
            return;
        }
        if (sectionNameArea(i).contains(e.getPosition()) && ! e.mods.isShiftDown())
        {
            engine.selectSongSection(i);
            refreshFromEngine();
            if (onSongChanged) onSongChanged();
            showPartMenu(i, e.getScreenPosition());
            return;
        }
        if (sectionResizeHandle(i).contains(e.getPosition()) && ! e.mods.isShiftDown())
        {
            resizeIndex = i;
            resizeStartBars = juce::jmax(1, sections[(size_t) i].bars);
            resizeTileLeft = tile.getX();
            resizePxPerBar = (float) tile.getWidth() / (float) resizeStartBars;
            if (resizePxPerBar < 8.0f)
                resizePxPerBar = 8.0f;
            return;
        }
        engine.selectSongSection(i, e.mods.isShiftDown());
        refreshFromEngine();
        if (onSongChanged) onSongChanged();
        if (e.mods.isShiftDown())
            return;
        dragFrom = i;
        dragOffset = e.getPosition() - tile.getPosition();
        return;
    }
}

void SongPage::mouseDrag(const juce::MouseEvent& e)
{
    if (resizeIndex >= 0)
    {
        const auto& sections = engine.state().song.sections;
        if (resizeIndex < (int) sections.size())
        {
            const int next = juce::jlimit(1, 16,
                (int) std::round((float) (e.x - resizeTileLeft) / resizePxPerBar));
            if (next != sections[(size_t) resizeIndex].bars)
            {
                engine.setSongSectionBars(resizeIndex, next);
                if (onSongChanged) onSongChanged();
            }
        }
        repaint();
        return;
    }
    if (dragFrom < 0)
        return;
    if (! dragging && e.getDistanceFromDragStart() > 6)
        dragging = true;
    if (! dragging)
        return;
    dragGhostPos = e.position - dragOffset.toFloat();
    dragInsert = insertIndexForX(e.x);
    repaint();
}

void SongPage::mouseMove(const juce::MouseEvent& e)
{
    const auto& sections = engine.state().song.sections;
    for (int i = 0; i < (int) sections.size(); ++i)
        if (sectionResizeHandle(i).contains(e.getPosition()))
        {
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            return;
        }
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void SongPage::mouseExit(const juce::MouseEvent&)
{
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void SongPage::mouseUp(const juce::MouseEvent&)
{
    if (resizeIndex >= 0)
    {
        resizeIndex = -1;
        refreshFromEngine();
        if (onSongChanged) onSongChanged();
        return;
    }
    if (dragging && dragFrom >= 0 && dragInsert >= 0)
    {
        engine.moveSongSection(dragFrom, dragInsert);
        refreshFromEngine();
        if (onSongChanged) onSongChanged();
    }
    dragging = false;
    dragFrom = -1;
    dragInsert = -1;
    repaint();
}

void SongPage::paintSectionTile(juce::Graphics& g, int index, juce::Rectangle<float> tile,
                               bool selected, bool playing, float alpha)
{
    const auto& section = engine.state().song.sections[(size_t) index];
    const auto colour = partColour(section.part);

    g.setColour(colour.withAlpha(0.42f * alpha + (selected ? 0.46f : 0.0f) * alpha));
    g.fillRoundedRectangle(tile, 6.0f);
    const bool queued = (index == engine.queuedSongSection());
    const bool beatJump = (index == engine.pendingBeatJumpSection());
    g.setColour((selected ? juce::Colours::white
                 : (queued || beatJump) ? juce::Colour(0xff7ac8ff)
                 : colour.brighter(0.2f)).withAlpha(alpha));
    g.drawRoundedRectangle(tile, 6.0f, (selected || queued || beatJump) ? 2.0f : 1.0f);

    if (playing)
    {
        auto fill = tile.reduced(3.0f);
        fill.setWidth(fill.getWidth() * (float) engine.songSectionProgress());
        g.setColour(juce::Colours::white.withAlpha(0.22f * alpha));
        g.fillRoundedRectangle(fill, 4.0f);
    }

    auto inner = tile.reduced(8, 8);
    g.setColour(juce::Colour(0xff071018).withAlpha(alpha));
    g.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    auto title = inner.removeFromTop(22);
    auto del = title.removeFromRight(16);
    const bool canDelete = engine.state().song.sections.size() > 1;
    g.setColour((canDelete ? juce::Colour(0xff3b1010) : juce::Colour(0xff1a2830)).withAlpha(alpha));
    g.fillRoundedRectangle(del.toFloat().reduced(1.0f), 3.0f);
    g.setColour((canDelete ? juce::Colour(0xffff8a8a) : juce::Colour(0xff4a5a66)).withAlpha(alpha));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("X", del, juce::Justification::centred);
    juce::String name = juce::String(groove::songPartName(section.part)) + "  ▾";
    g.drawText(name, title, juce::Justification::centredLeft);
    if (selected && engine.isRecording())
    {
        g.setColour(juce::Colour(0xffff3b3b).withAlpha(alpha));
        g.fillEllipse(title.removeFromRight(16).reduced(3).toFloat());
    }
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xff071018).withAlpha(alpha));
    g.drawText(groove::meterName(section.meter), title, juce::Justification::centredRight);
    g.setFont(juce::FontOptions(12.0f));
    juce::String meta = juce::String(section.bars) + (section.bars == 1 ? " BAR" : " BARS");
    if (section.currentTake >= 0 && section.currentTake < (int) section.takes.size())
        meta += "  ·  " + section.takes[(size_t) section.currentTake].label;
    g.drawText(meta, inner.removeFromTop(16), juce::Justification::centredLeft);

    const int recLane = groove::midiLaneIndexForChannel(activeMidiChannel);
    if (selected && recLane >= 0)
    {
        juce::String rec = "MIDI  ·  CH" + juce::String(activeMidiChannel)
            + "  " + juce::String(groove::midiLaneName(recLane));
        if (engine.isRecording())
            rec = "REC  ·  " + rec;
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.setColour((engine.isRecording() ? juce::Colour(0xff3b1010)
                                         : juce::Colour(0xff071018)).withAlpha(alpha));
        g.drawText(rec, inner.removeFromTop(16), juce::Justification::centredLeft);
    }

    auto ticks = inner.removeFromBottom(26);
    const float tw = ticks.getWidth() / (float) groove::kTracks;
    for (int t = 0; t < groove::kTracks; ++t)
    {
        const auto& sh = section.shapes[(size_t) t];
        const float dens = sh.generatorSteps > 0
            ? juce::jlimit(0.0f, 1.0f, (float) sh.pulses / (float) sh.generatorSteps)
            : 0.0f;
        const float h = juce::jmax(2.0f, ticks.getHeight() * dens);
        auto bar = juce::Rectangle<float>(ticks.getX() + (float) t * tw + 1.5f,
                                          ticks.getBottom() - h, tw - 3.0f, h);
        g.setColour(colour.brighter(0.15f).withAlpha(0.85f * alpha));
        g.fillRoundedRectangle(bar, 1.2f);
    }

    auto grip = juce::Rectangle<float>(tile.getRight() - 12.0f, tile.getY() + 8.0f,
                                      8.0f, juce::jmax(12.0f, tile.getHeight() - 16.0f));
    g.setColour(juce::Colour(0xff071018).withAlpha(0.45f * alpha));
    g.fillRoundedRectangle(grip, 3.0f);
    g.setColour(juce::Colours::white.withAlpha(0.35f * alpha));
    for (int k = 0; k < 2; ++k)
        g.fillRect(grip.getX() + 2.0f + (float) k * 2.5f, grip.getY() + 6.0f, 1.2f, grip.getHeight() - 12.0f);
    if (queued || beatJump)
    {
        g.setColour(juce::Colour(0xff7ac8ff).withAlpha(alpha));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(beatJump ? "BEAT" : "NEXT",
                   tile.reduced(8, 6).removeFromBottom(16), juce::Justification::centredRight);
    }
}

void SongPage::paintMidiLanes(juce::Graphics& g)
{
    auto r = lanesPanel.reduced(16, 0);
    r.removeFromTop(34);
    if (r.getHeight() < 20)
        return;

    const auto& song = engine.state().song;
    const bool hasSection = ! song.sections.empty();
    const int current = hasSection
        ? juce::jlimit(0, (int) song.sections.size() - 1, song.current) : -1;
    const auto& liveLanes = engine.state().midiLanes;
    const auto* sectionLanes = (current >= 0) ? &song.sections[(size_t) current].midiLanes : nullptr;
    const int recLane = groove::midiLaneIndexForChannel(activeMidiChannel);
    const int rowH = juce::jmax(18, r.getHeight() / groove::kMidiLanes);

    for (int lane = 0; lane < groove::kMidiLanes; ++lane)
    {
        auto row = r.removeFromTop(rowH).reduced(0, 1).toFloat();
        const bool armed = (lane == recLane);
        const bool recordingHere = armed && engine.isRecording();
        g.setColour(recordingHere ? juce::Colour(0xff3a1820)
                    : armed ? juce::Colour(0xff1a4a62)
                    : juce::Colour(0xff0d1c26));
        g.fillRoundedRectangle(row, 4.0f);
        if (armed)
        {
            g.setColour(recordingHere ? juce::Colour(0xffff6a6a) : juce::Colour(0xff7ac8ff));
            g.drawRoundedRectangle(row.reduced(0.5f), 4.0f, 1.6f);
        }

        auto label = row.removeFromLeft((float) kLaneLabelW).reduced(6.0f, 0.0f);
        auto uiChip = label.removeFromRight(22.0f).reduced(0.0f, 4.0f);
        g.setColour((armed ? juce::Colour(0xff2e8ec4) : juce::Colour(0xff1a3a4e)).withAlpha(0.95f));
        g.fillRoundedRectangle(uiChip, 3.0f);
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.setColour(armed ? juce::Colours::white : juce::Colour(0xff8aa0ae));
        g.drawText("UI", uiChip, juce::Justification::centred);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.setColour(armed ? juce::Colour(0xffe8f6ff) : juce::Colour(0xff8aa0ae));
        juce::String name = juce::String(groove::midiLaneName(lane))
            + "  ·  CH" + juce::String(groove::midiLaneChannel(lane));
        if (recordingHere)
            name += "  ·  REC";
        g.drawText(name, label.reduced(2.0f, 0.0f), juce::Justification::centredLeft);

        const auto& srcLanes = (current >= 0 && engine.isRecording())
            ? liveLanes
            : (sectionLanes != nullptr ? *sectionLanes : liveLanes);
        const auto& notes = srcLanes[(size_t) lane].notes;
        const auto& patches = srcLanes[(size_t) lane].patches;
        if (! patches.empty())
        {
            g.setColour(juce::Colour(0xffc8e8ff).withAlpha(armed ? 0.95f : 0.65f));
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            g.drawText(patches.back().name, row.removeFromRight(90.0f).reduced(6.0f, 0.0f),
                       juce::Justification::centredRight);
        }

        auto dots = row.reduced(6.0f, 5.0f);
        const float stepW = dots.getWidth() / (float) groove::kSteps;
        std::array<int, groove::kSteps> hits {};
        for (const auto& n : notes)
        {
            if (n.step < 0 || n.step >= groove::kSteps)
                continue;
            const int span = juce::jmax(1, n.lengthSteps);
            for (int k = 0; k < span && k < groove::kSteps; ++k)
                hits[(size_t) ((n.step + k) % groove::kSteps)] += 1;
        }
        for (int s = 0; s < groove::kSteps; ++s)
        {
            auto d = juce::Rectangle<float>(dots.getX() + (float) s * stepW + 1.0f,
                                            dots.getY(), stepW - 2.0f, dots.getHeight());
            g.setColour(hits[(size_t) s] > 0
                ? (armed ? juce::Colour(0xff7ac8ff) : juce::Colour(0xff3d6a80))
                : juce::Colour(0xff152430));
            g.fillRoundedRectangle(d, 1.2f);
        }
    }
}

void SongPage::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff050c12));

    auto panel = [](juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour(juce::Colour(0xff0a151e));
        g.fillRoundedRectangle(r.toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff1c3443));
        g.drawRoundedRectangle(r.toFloat(), 5.0f, 1.0f);
        auto header = r.removeFromTop(34);
        g.setColour(juce::Colour(0xff0d1c27));
        g.fillRoundedRectangle(header.toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff2c98e8));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(title, header.reduced(12, 0), juce::Justification::centredLeft);
    };

    juce::String arrangeTitle = "ARRANGEMENT  ·  CLICK TO CUE  ·  SHIFT-CLICK JUMPS ON BEAT";
    panel(g, arrangePanel, arrangeTitle);
    const int recLane = groove::midiLaneIndexForChannel(activeMidiChannel);
    juce::String lanesTitle = "MIDI LANES  ·  CLICK A NOTE  ·  RIGHT-CLICK OR DELETE REMOVES IT";
    if (recLane >= 0)
    {
        lanesTitle += "  ·  CH" + juce::String(activeMidiChannel)
            + "  " + juce::String(groove::midiLaneName(recLane));
        if (engine.isRecording())
            lanesTitle += "  ·  RECORDING";
    }
    panel(g, lanesPanel, lanesTitle);
    panel(g, editPanel, "SECTION");
    paintMidiLanes(g);

    const auto& song = engine.state().song;
    const int current = song.sections.empty() ? -1
        : juce::jlimit(0, (int) song.sections.size() - 1, song.current);

    for (int i = 0; i < (int) song.sections.size(); ++i)
    {
        const bool selected = (i == current) || (i == engine.queuedSongSection())
            || (i == engine.pendingBeatJumpSection());
        const bool playing = (i == current) && engine.isPlaying();
        const float alpha = (dragging && i == dragFrom) ? 0.28f : 1.0f;
        paintSectionTile(g, i, sectionTile(i).toFloat(), selected, playing, alpha);
    }

    if (dragging && dragFrom >= 0 && dragFrom < (int) song.sections.size())
    {
        const auto src = sectionTile(dragFrom);
        auto ghost = src.toFloat();
        ghost.setPosition(dragGhostPos);
        paintSectionTile(g, dragFrom, ghost, true, false, 0.78f);

        int lineX = 0;
        if (song.sections.empty())
            lineX = arrangePanel.getCentreX();
        else if (dragInsert >= (int) song.sections.size())
            lineX = sectionTile((int) song.sections.size() - 1).getRight() + 4;
        else
            lineX = sectionTile(juce::jmax(0, dragInsert)).getX() - 4;

        auto band = arrangePanel.reduced(16);
        band.removeFromTop(78);
        g.setColour(juce::Colour(0xff7ac8ff));
        g.fillRect(lineX, band.getY(), 3, band.getHeight());
    }

    auto col = editPanel.reduced(16, 0);
    col.removeFromTop(98);
    auto head = col.removeFromTop(22);
    head.removeFromLeft(58);
    const int comboW = juce::jmax(54, (head.getWidth() - 180) / 4);
    g.setColour(juce::Colour(0xff8aa0ae));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    const char* headers[] = { "STEPS", "PULSES", "ROTATE", "DIV", "PROB", "VEL" };
    for (int i = 0; i < 4; ++i)
        g.drawText(headers[i], head.removeFromLeft(comboW), juce::Justification::centred);
    g.drawText("PROB", head.removeFromLeft(head.getWidth() / 2), juce::Justification::centred);
    g.drawText("VEL", head, juce::Justification::centred);

    g.setColour(juce::Colour(0xff8aa0ae));
    g.setFont(juce::FontOptions(12.0f));
    juce::String status;
    if (current >= 0)
    {
        const auto& section = song.sections[(size_t) current];
        status = juce::String(groove::songPartName(section.part));
        if (engine.isPlaying() && song.follow)
            status += "  ·  BAR " + juce::String(engine.songBarInSection())
                   + " / " + juce::String(section.bars);
        else if (song.follow)
            status += "  ·  PLAY walks the arrangement";
        else
            status += "  ·  stays here until you cue another";
        const int queued = engine.queuedSongSection();
        if (queued >= 0 && queued < (int) song.sections.size())
            status += "  ·  NEXT "
                + juce::String(groove::songPartName(song.sections[(size_t) queued].part));
        const int beatJump = engine.pendingBeatJumpSection();
        if (beatJump >= 0 && beatJump < (int) song.sections.size())
            status += "  ·  ON BEAT "
                + juce::String(groove::songPartName(song.sections[(size_t) beatJump].part));
        if (engine.isRecording())
        {
            const int recLane = groove::midiLaneIndexForChannel(activeMidiChannel);
            status = juce::String(groove::songPartName(section.part)) + "  ·  REC  ·  CH"
                + juce::String(activeMidiChannel);
            if (recLane >= 0)
                status += "  " + juce::String(groove::midiLaneName(recLane));
        }
    }
    g.drawText(status, editPanel.removeFromTop(34).reduced(88, 0).withTrimmedRight(430),
               juce::Justification::centredLeft);
}
