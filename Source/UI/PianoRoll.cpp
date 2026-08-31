#include "PianoRoll.h"

namespace
{
const juce::Colour bg { 0xff07121a };
const juce::Colour panel { 0xff0b1b25 };
const juce::Colour grid { 0xff20333f };
const juce::Colour gridStrong { 0xff3a5665 };
const juce::Colour noteGreen { 0xff83c94d };
const juce::Colour selectedGreen { 0xffb9ef76 };
const juce::Colour text { 0xffdcebf2 };
const juce::Colour mutedText { 0xff829bab };
}

PianoRoll::PianoRoll(groove::GrooveEngine& e) : engine(e)
{
    setWantsKeyboardFocus(true);
    title.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, noteGreen);
    addAndMakeVisible(title);

    snapBox.addItem("1/16", 1);
    snapBox.addItem("1/8", 2);
    snapBox.addItem("1/4", 3);
    snapBox.addItem("1 BAR", 4);
    snapBox.setSelectedId(1);
    snapBox.onChange = [this]
    {
        static constexpr int values[] = { 1, 2, 4, 16 };
        snapSteps = values[juce::jlimit(1, 4, snapBox.getSelectedId()) - 1];
    };
    addAndMakeVisible(snapBox);

    octaveDown.onClick = [this] { lowNote = juce::jmax(0, lowNote - 12); highNote = juce::jmax(lowNote + 12, highNote - 12); repaint(); };
    octaveUp.onClick = [this] { highNote = juce::jmin(127, highNote + 12); lowNote = juce::jmin(highNote - 12, lowNote + 12); repaint(); };
    deleteButton.onClick = [this] { deleteSelected(); };
    addAndMakeVisible(octaveDown);
    addAndMakeVisible(octaveUp);
    addAndMakeVisible(deleteButton);
    refresh();
}

void PianoRoll::setLane(int laneIndex)
{
    lane = juce::jlimit(1, groove::kMidiLanes - 1, laneIndex);
    selectedNote = -1;
    refresh();
}

void PianoRoll::refresh()
{
    title.setText(laneTitle() + "  ·  NOTE SEQUENCER", juce::dontSendNotification);
    if (const auto* l = laneState(); l != nullptr && selectedNote >= (int) l->notes.size())
        selectedNote = -1;
    repaint();
}

const groove::MidiLane* PianoRoll::laneState() const
{
    if (lane < 0 || lane >= groove::kMidiLanes) return nullptr;
    return &engine.state().midiLanes[(size_t) lane];
}

groove::MidiLaneNote PianoRoll::noteAtIndex(int index) const
{
    if (const auto* l = laneState(); l != nullptr && index >= 0 && index < (int) l->notes.size())
        return l->notes[(size_t) index];
    return {};
}

juce::String PianoRoll::laneTitle() const
{
    const auto* l = laneState();
    if (l == nullptr) return "MIDI";
    return "CH" + juce::String(l->channel) + "  ·  " + l->name;
}

bool PianoRoll::isBlackKey(int note)
{
    const int pc = note % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

void PianoRoll::resized()
{
    auto r = getLocalBounds();
    headerArea = r.removeFromTop(44);
    inspectorArea = r.removeFromRight(230);
    velocityArea = r.removeFromBottom(112);
    pianoArea = r.removeFromLeft(76);
    gridArea = r;

    auto h = headerArea.reduced(10, 7);
    title.setBounds(h.removeFromLeft(330));
    h.removeFromLeft(8);
    snapBox.setBounds(h.removeFromLeft(90));
    h.removeFromLeft(8);
    octaveDown.setBounds(h.removeFromLeft(30));
    octaveUp.setBounds(h.removeFromLeft(30));
    deleteButton.setBounds(h.removeFromRight(78));
}

int PianoRoll::snap(int step) const
{
    return juce::jmax(0, ((step + snapSteps / 2) / snapSteps) * snapSteps);
}

int PianoRoll::stepFromX(int x) const
{
    const int total = juce::jmax(1, engine.midiTimelineSteps());
    const float norm = juce::jlimit(0.0f, 0.999999f, (x - gridArea.getX()) / (float) juce::jmax(1, gridArea.getWidth()));
    return juce::jlimit(0, total - 1, (int) std::floor(norm * total));
}

int PianoRoll::noteFromY(int y) const
{
    const int rows = juce::jmax(1, highNote - lowNote + 1);
    const float rowH = gridArea.getHeight() / (float) rows;
    const int row = juce::jlimit(0, rows - 1, (int) ((y - gridArea.getY()) / juce::jmax(1.0f, rowH)));
    return juce::jlimit(0, 127, highNote - row);
}

float PianoRoll::velocityFromY(int y) const
{
    const float v = (velocityArea.getBottom() - y) / (float) juce::jmax(1, velocityArea.getHeight() - 20);
    return juce::jlimit(1.0f / 127.0f, 1.0f, v);
}

juce::Rectangle<float> PianoRoll::noteRect(const groove::MidiLaneNote& n) const
{
    const int total = juce::jmax(1, engine.midiTimelineSteps());
    const int rows = juce::jmax(1, highNote - lowNote + 1);
    const float rowH = gridArea.getHeight() / (float) rows;
    const float x = gridArea.getX() + gridArea.getWidth() * (n.step / (float) total);
    const float w = juce::jmax(5.0f, gridArea.getWidth() * (juce::jmax(1, n.lengthSteps) / (float) total));
    const float y = gridArea.getY() + (highNote - n.note) * rowH;
    return { x + 1.0f, y + 1.0f, w - 2.0f, juce::jmax(3.0f, rowH - 2.0f) };
}

int PianoRoll::noteIndexAt(juce::Point<int> p, bool includeVelocity) const
{
    const auto* l = laneState();
    if (l == nullptr) return -1;
    if (includeVelocity && velocityArea.contains(p))
    {
        const int total = juce::jmax(1, engine.midiTimelineSteps());
        for (int i = (int) l->notes.size() - 1; i >= 0; --i)
        {
            const auto& n = l->notes[(size_t) i];
            const float x = gridArea.getX() + gridArea.getWidth() * (n.step / (float) total);
            if (std::abs(p.x - (int) x) <= 7) return i;
        }
    }
    for (int i = (int) l->notes.size() - 1; i >= 0; --i)
        if (noteRect(l->notes[(size_t) i]).expanded(2.0f).contains(p.toFloat())) return i;
    return -1;
}

void PianoRoll::selectNote(int index)
{
    selectedNote = index;
    grabKeyboardFocus();
    repaint();
}

void PianoRoll::addNoteAt(juce::Point<int> p)
{
    if (! gridArea.contains(p)) return;
    const int step = snap(stepFromX(p.x));
    const int note = noteFromY(p.y);
    selectedNote = engine.addMidiLaneNote(lane, step, note, 0.8f, juce::jmax(1, snapSteps));
    repaint();
}

void PianoRoll::deleteSelected()
{
    if (selectedNote < 0) return;
    if (engine.deleteMidiLaneNote(lane, selectedNote)) selectedNote = -1;
    repaint();
}

void PianoRoll::mouseDown(const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (velocityArea.contains(p))
    {
        const int i = noteIndexAt(p, true);
        if (i >= 0)
        {
            selectNote(i);
            dragMode = DragMode::velocity;
            auto n = noteAtIndex(i);
            n.velocity = velocityFromY(p.y);
            engine.updateMidiLaneNote(lane, i, n);
        }
        return;
    }
    if (! gridArea.contains(p)) return;

    const int i = noteIndexAt(p);
    if (i < 0)
    {
        addNoteAt(p);
        if (selectedNote >= 0)
        {
            dragMode = DragMode::resize;
            dragStart = p;
            dragOriginal = noteAtIndex(selectedNote);
        }
        return;
    }

    selectNote(i);
    dragStart = p;
    dragOriginal = noteAtIndex(i);
    const auto rect = noteRect(dragOriginal);
    dragMode = (p.x >= (int) rect.getRight() - 7) ? DragMode::resize : DragMode::move;
}

void PianoRoll::mouseDrag(const juce::MouseEvent& e)
{
    if (selectedNote < 0 || dragMode == DragMode::none) return;
    auto n = dragOriginal;
    if (dragMode == DragMode::velocity)
    {
        n = noteAtIndex(selectedNote);
        n.velocity = velocityFromY(e.y);
    }
    else if (dragMode == DragMode::resize)
    {
        const int endStep = snap(stepFromX(e.x));
        n.lengthSteps = juce::jmax(1, endStep - n.step + snapSteps);
    }
    else if (dragMode == DragMode::move)
    {
        const int oldStep = stepFromX(dragStart.x);
        const int newStep = stepFromX(e.x);
        n.step = juce::jmax(0, snap(dragOriginal.step + newStep - oldStep));
        n.note = juce::jlimit(0, 127, dragOriginal.note + noteFromY(e.y) - noteFromY(dragStart.y));
    }
    engine.updateMidiLaneNote(lane, selectedNote, n);
    repaint();
}

void PianoRoll::mouseUp(const juce::MouseEvent&)
{
    dragMode = DragMode::none;
}

bool PianoRoll::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
    {
        deleteSelected();
        return true;
    }
    return false;
}

void PianoRoll::paint(juce::Graphics& g)
{
    g.fillAll(bg);
    g.setColour(panel); g.fillRect(headerArea); g.fillRect(inspectorArea); g.fillRect(velocityArea);

    const int total = juce::jmax(1, engine.midiTimelineSteps());
    const int rows = juce::jmax(1, highNote - lowNote + 1);
    const float rowH = gridArea.getHeight() / (float) rows;

    // Piano keyboard and horizontal pitch rows.
    for (int note = highNote; note >= lowNote; --note)
    {
        const int row = highNote - note;
        const float y = gridArea.getY() + row * rowH;
        const bool black = isBlackKey(note);
        g.setColour(black ? juce::Colour(0xff11171b) : juce::Colour(0xffe9eef0));
        g.fillRect((float) pianoArea.getX(), y, (float) pianoArea.getWidth(), rowH);
        g.setColour(black ? juce::Colour(0xff44535c) : juce::Colour(0xffc0c9ce));
        g.drawRect((float) pianoArea.getX(), y, (float) pianoArea.getWidth(), rowH, 0.5f);
        if (note % 12 == 0 || rowH >= 12.0f)
        {
            g.setColour(black ? text : juce::Colour(0xff26343b));
            g.setFont(juce::FontOptions(juce::jlimit(7.0f, 10.0f, rowH - 2.0f)));
            g.drawText(juce::MidiMessage::getMidiNoteName(note, true, true, 3), pianoArea.reduced(5, 0).withY((int)y).withHeight((int)rowH), juce::Justification::centredRight);
        }
        g.setColour((note % 12 == 0) ? gridStrong.withAlpha(0.50f) : grid.withAlpha(0.45f));
        g.drawHorizontalLine((int) y, (float) gridArea.getX(), (float) gridArea.getRight());
    }

    // Musical time grid: 16th steps, stronger quarter/bar lines.
    for (int s = 0; s <= total; ++s)
    {
        const float x = gridArea.getX() + gridArea.getWidth() * (s / (float) total);
        const bool bar = (s % 16) == 0;
        const bool beat = (s % 4) == 0;
        g.setColour(bar ? gridStrong : beat ? gridStrong.withAlpha(0.65f) : grid.withAlpha(0.55f));
        g.drawVerticalLine((int) x, (float) gridArea.getY(), (float) velocityArea.getBottom());
        if (beat && s < total)
        {
            const int barNo = s / 16 + 1;
            const int beatNo = (s % 16) / 4 + 1;
            g.setColour(mutedText);
            g.setFont(juce::FontOptions(9.0f));
            g.drawText(juce::String(barNo) + "." + juce::String(beatNo), (int)x + 3, gridArea.getY() + 2, 34, 14, juce::Justification::centredLeft);
        }
    }

    const auto* l = laneState();
    if (l != nullptr)
    {
        for (int i = 0; i < (int) l->notes.size(); ++i)
        {
            const auto& n = l->notes[(size_t) i];
            if (n.note < lowNote || n.note > highNote) continue;
            const auto r = noteRect(n);
            g.setColour(i == selectedNote ? selectedGreen : noteGreen);
            g.fillRoundedRectangle(r, 2.0f);
            g.setColour(juce::Colour(0xff203611));
            g.drawRoundedRectangle(r, 2.0f, i == selectedNote ? 1.8f : 0.8f);
        }

        // Velocity lane.
        g.setColour(text); g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText("VELOCITY", velocityArea.getX() + 8, velocityArea.getY() + 5, 90, 16, juce::Justification::centredLeft);
        g.setColour(mutedText); g.setFont(juce::FontOptions(9.0f));
        g.drawText("127", velocityArea.getX() + 8, velocityArea.getY() + 22, 28, 12, juce::Justification::left);
        g.drawText("64", velocityArea.getX() + 8, velocityArea.getCentreY() - 5, 28, 12, juce::Justification::left);
        g.drawText("0", velocityArea.getX() + 8, velocityArea.getBottom() - 16, 28, 12, juce::Justification::left);
        for (int i = 0; i < (int) l->notes.size(); ++i)
        {
            const auto& n = l->notes[(size_t) i];
            const float x = gridArea.getX() + gridArea.getWidth() * (n.step / (float) total);
            const float base = (float) velocityArea.getBottom() - 10.0f;
            const float top = base - (velocityArea.getHeight() - 30.0f) * juce::jlimit(0.0f, 1.0f, n.velocity);
            g.setColour(i == selectedNote ? selectedGreen : noteGreen);
            g.drawLine(x, base, x, top, i == selectedNote ? 2.5f : 1.5f);
            g.fillEllipse(x - 3.5f, top - 3.5f, 7.0f, 7.0f);
        }
    }

    // Playhead.
    const float playX = gridArea.getX() + gridArea.getWidth() * (engine.currentMidiTimelineStep() / (float) total);
    g.setColour(juce::Colour(0xff5ee0ff));
    g.drawVerticalLine((int) playX, (float) gridArea.getY(), (float) velocityArea.getBottom());

    // Note inspector.
    auto ir = inspectorArea.reduced(14);
    g.setColour(text); g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("NOTE EDIT", ir.removeFromTop(24), juce::Justification::centredLeft);
    if (l != nullptr && selectedNote >= 0 && selectedNote < (int) l->notes.size())
    {
        const auto& n = l->notes[(size_t) selectedNote];
        g.setColour(mutedText); g.setFont(juce::FontOptions(10.0f));
        auto line = [&](juce::String label, juce::String value)
        {
            auto r = ir.removeFromTop(34);
            g.setColour(mutedText); g.drawText(label, r.removeFromLeft(86), juce::Justification::centredLeft);
            g.setColour(text); g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
            g.drawText(value, r, juce::Justification::centredLeft);
            g.setFont(juce::FontOptions(10.0f));
        };
        line("NOTE", juce::MidiMessage::getMidiNoteName(n.note, true, true, 3) + "  " + juce::String(n.note));
        line("LENGTH", juce::String(n.lengthSteps) + " steps");
        line("VELOCITY", juce::String(juce::jlimit(1, 127, (int) std::round(n.velocity * 127.0f))));
        line("START", juce::String(n.step + 1));
        g.setColour(noteGreen); g.setFont(juce::FontOptions(9.5f));
        g.drawFittedText("Drag note = move pitch/time\nDrag right edge = length\nDrag velocity stem = velocity\nClick empty grid = create", ir.removeFromTop(86), juce::Justification::topLeft, 5);
    }
    else
    {
        g.setColour(mutedText); g.setFont(juce::FontOptions(10.5f));
        g.drawFittedText("Click the piano roll to create a MIDI note. Note width is duration; velocity appears below.", ir.removeFromTop(70), juce::Justification::topLeft, 4);
    }
}
