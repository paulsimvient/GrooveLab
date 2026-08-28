#include "SongPage.h"
#include <array>

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
        barsBox.setSelectedId(juce::jlimit(1, 16, section.bars), juce::dontSendNotification);
        meterBox.setSelectedId((int) section.meter + 1, juce::dontSendNotification);
        for (int t = 0; t < groove::kTracks; ++t)
        {
            const auto& sh = section.shapes[(size_t) t];
            fillRhythmBoxes(t, sh);
            rows[(size_t) t].probability.setValue(sh.probability, juce::dontSendNotification);
            rows[(size_t) t].velocity.setValue(sh.velocity, juce::dontSendNotification);
        }
    }
    refreshing = false;
    repaint();
}

void SongPage::timerCallback()
{
    repaint();
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
    editPanel = bounds.removeFromBottom(268);
    bounds.removeFromBottom(10);
    arrangePanel = bounds;

    auto er = editPanel.reduced(16, 0);
    er.removeFromTop(38);
    auto tools = er.removeFromTop(32);
    followButton.setBounds(tools.removeFromLeft(130));
    tools.removeFromLeft(12);
    barsBox.setBounds(tools.removeFromLeft(110));
    tools.removeFromLeft(8);
    meterBox.setBounds(tools.removeFromLeft(72));
    tools.removeFromLeft(8);
    duplicateButton.setBounds(tools.removeFromLeft(110));
    tools.removeFromLeft(8);
    deleteButton.setBounds(tools.removeFromLeft(90));

    er.removeFromTop(22);
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
}

void SongPage::mouseDown(const juce::MouseEvent& e)
{
    dragging = false;
    dragFrom = -1;
    dragInsert = -1;
    const auto& sections = engine.state().song.sections;
    for (int i = 0; i < (int) sections.size(); ++i)
    {
        const auto tile = sectionTile(i);
        if (tile.contains(e.getPosition()))
        {
            dragFrom = i;
            dragOffset = e.getPosition() - tile.getPosition();
            engine.selectSongSection(i);
            refreshFromEngine();
            if (onSongChanged) onSongChanged();
            return;
        }
    }
}

void SongPage::mouseDrag(const juce::MouseEvent& e)
{
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

void SongPage::mouseUp(const juce::MouseEvent&)
{
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
    g.setColour((selected ? juce::Colours::white : colour.brighter(0.2f)).withAlpha(alpha));
    g.drawRoundedRectangle(tile, 6.0f, selected ? 2.0f : 1.0f);

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
    g.drawText(groove::songPartName(section.part), title, juce::Justification::centredLeft);
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText(groove::meterName(section.meter), title, juce::Justification::centredRight);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText(juce::String(section.bars) + (section.bars == 1 ? " BAR" : " BARS"),
               inner.removeFromTop(18), juce::Justification::centredLeft);

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

    panel(g, arrangePanel, "ARRANGEMENT  ·  DRAG TO REORDER  ·  CLICK TO EDIT");
    panel(g, editPanel, "SECTION");

    const auto& song = engine.state().song;
    const int current = song.sections.empty() ? -1
        : juce::jlimit(0, (int) song.sections.size() - 1, song.current);

    for (int i = 0; i < (int) song.sections.size(); ++i)
    {
        const bool selected = (i == current);
        const bool playing = selected && engine.isPlaying() && song.follow;
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
    col.removeFromTop(70);
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
            status += "  ·  FOLLOW off";
    }
    g.drawText(status, editPanel.removeFromTop(34).reduced(88, 0), juce::Justification::centredRight);
}
