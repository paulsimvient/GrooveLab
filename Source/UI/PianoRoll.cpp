#include "PianoRoll.h"
#include <algorithm>
#include <cmath>

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
    deleteButton.setTooltip("Delete the selected note (Delete / Backspace)");
    deleteButton.onClick = [this] { deleteSelected(); };
    clearListenButton.setTooltip("Remove the bassline LISTEN wrote into this lane");
    clearListenButton.onClick = [this]
    {
        if (onClearListenClicked) onClearListenClicked();
    };
    resetButton.setTooltip("Restore the original recorded notes and return to STEP mode");
    resetButton.onClick = [this]
    {
        engine.resetMidiLaneToSource(lane);
        selectedNote = -1;
        refresh();
        fitNotes();
    };
    fitButton.onClick = [this] { fitNotes(); };
    newTakeButton.setTooltip("Generate another deterministic variation without touching the original recording");
    newTakeButton.onClick = [this] { engine.newMidiLaneGeneratedTake(lane); refresh(); };
    keepButton.setTooltip("Commit the current generated ANSWER into the piano roll");
    keepButton.onClick = [this] { engine.keepMidiLaneGeneratedTake(lane); selectedNote = -1; refresh(); fitNotes(); };

    recordButton.setClickingTogglesState(true);
    recordButton.setTooltip("Record MIDI into this selected instrument lane");
    recordButton.onClick = [this]
    {
        engine.setRecording(recordButton.getToggleState());
        refresh();
    };

    muteButton.setClickingTogglesState(true);
    muteButton.setTooltip("Mute this instrument track");
    muteButton.onClick = [this]
    {
        engine.setMidiLaneMuted(lane, muteButton.getToggleState());
        refresh();
    };

    // Euclidean rhythm editing intentionally lives only on the shared EUC page.
    // The piano roll is pitch/duration/velocity + mute/quantize/overwrite territory.

    addAndMakeVisible(octaveDown);
    addAndMakeVisible(octaveUp);
    addAndMakeVisible(deleteButton);
    addAndMakeVisible(clearListenButton);
    addAndMakeVisible(resetButton);
    addAndMakeVisible(fitButton);
    addAndMakeVisible(newTakeButton);
    addAndMakeVisible(keepButton);
    addAndMakeVisible(muteButton);
    addAndMakeVisible(recordButton);

    listenStatus.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    listenStatus.setColour(juce::Label::textColourId, juce::Colour(0xff9ef0ff));
    listenStatus.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(listenStatus);

    listenBarsBox.addItem("2 bars", 2);
    listenBarsBox.addItem("4 bars", 4);
    listenBarsBox.addItem("8 bars", 8);
    listenBarsBox.setSelectedId(2, juce::dontSendNotification);
    listenBarsBox.setTooltip("How long LISTEN captures audio input");
    addAndMakeVisible(listenBarsBox);

    static const char* keyNames[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    for (int i = 0; i < 12; ++i)
        listenKeyBox.addItem(keyNames[i], i + 1);
    listenKeyBox.setSelectedId(1, juce::dontSendNotification); // C
    listenKeyBox.setTooltip("Key center for generated bass");
    addAndMakeVisible(listenKeyBox);

    listenModeBox.addItem("AUTO", 1);
    listenModeBox.addItem("MAJOR", 2);
    listenModeBox.addItem("MINOR", 3);
    listenModeBox.addItem("DORIAN", 4);
    listenModeBox.addItem("PHRYGIAN", 5);
    listenModeBox.addItem("LYDIAN", 6);
    listenModeBox.addItem("MIXOLYDIAN", 7);
    listenModeBox.addItem("LOCRIAN", 8);
    listenModeBox.addItem("HARM MIN", 9);
    listenModeBox.addItem("MEL MIN", 10);
    listenModeBox.addItem("PENT MAJ", 11);
    listenModeBox.addItem("PENT MIN", 12);
    listenModeBox.addItem("BLUES", 13);
    listenModeBox.setSelectedId(1, juce::dontSendNotification);
    listenModeBox.setTooltip("AUTO follows what was heard; other modes lock bass to that scale");
    auto applyKeyMode = [this]
    {
        if (listenHasResult && onRegenClicked)
            onRegenClicked();
        else
            updateListenChrome();
    };
    listenKeyBox.onChange = applyKeyMode;
    listenModeBox.onChange = applyKeyMode;
    addAndMakeVisible(listenModeBox);

    listenButton.setClickingTogglesState(true);
    listenButton.setTooltip("Capture audio input, detect harmony/rhythm, write a bassline here (or MOOG if drums are selected)");
    listenButton.onClick = [this]
    {
        if (onListenClicked) onListenClicked();
    };
    monitorButton.setTooltip("Open live input monitor — levels and what LISTEN is reading");
    monitorButton.onClick = [this]
    {
        if (onMonitorClicked) onMonitorClicked();
    };
    regenButton.setTooltip("Regenerate bass from the last listen");
    regenButton.onClick = [this] { if (onRegenClicked) onRegenClicked(); };
    simplifyButton.setTooltip("Simpler bass (more roots)");
    simplifyButton.onClick = [this] { if (onSimplifyClicked) onSimplifyClicked(); };
    moveButton.setTooltip("More melodic movement");
    moveButton.onClick = [this] { if (onMoreMoveClicked) onMoreMoveClicked(); };
    followButton.setTooltip("Follow detected rhythm accents");
    followButton.onClick = [this] { if (onFollowRhythmClicked) onFollowRhythmClicked(); };
    addAndMakeVisible(listenButton);
    addAndMakeVisible(monitorButton);
    addAndMakeVisible(regenButton);
    addAndMakeVisible(simplifyButton);
    addAndMakeVisible(moveButton);
    addAndMakeVisible(followButton);
    updateListenChrome();
    refresh();
}

void PianoRoll::setLane(int laneIndex)
{
    lane = juce::jlimit(0, groove::kMidiLanes - 1, laneIndex);
    selectedNote = -1;
    refresh();
}

void PianoRoll::refresh()
{
    if (isDrumMode())
        title.setText("CH1  |  DRUMS  |  MIDI KEY VIEW", juce::dontSendNotification);
    else if (const auto* l = laneState(); l != nullptr)
        title.setText(laneTitle() + "  |  " + groove::rhythmModeName(l->rhythmMode), juce::dontSendNotification);
    else
        title.setText(laneTitle() + "  |  NOTE SEQUENCER", juce::dontSendNotification);
    if (const auto* l = laneState(); l != nullptr)
    {
        if (selectedNote >= (int) l->notes.size()) selectedNote = -1;
        muteButton.setToggleState(l->muted, juce::dontSendNotification);
    }
    recordButton.setToggleState(engine.isRecording(), juce::dontSendNotification);
    recordButton.setButtonText(engine.isRecording() ? "REC ON" : "REC");
    const bool melodic = ! isDrumMode();
    octaveDown.setVisible(melodic); octaveUp.setVisible(melodic);
    resetButton.setVisible(melodic); fitButton.setVisible(melodic);
    newTakeButton.setVisible(melodic); keepButton.setVisible(melodic);
    muteButton.setVisible(melodic);
    repaint();
}

const groove::MidiLane* PianoRoll::laneState() const
{
    if (lane <= 0 || lane >= groove::kMidiLanes) return nullptr;
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
    if (isDrumMode()) return "CH1  |  DRUMS";
    const auto* l = laneState();
    if (l == nullptr) return "MIDI";
    return "CH" + juce::String(l->channel) + "  |  " + juce::String(groove::midiLaneName(lane));
}

bool PianoRoll::isBlackKey(int note)
{
    const int pc = note % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

void PianoRoll::resized()
{
    auto r = getLocalBounds();
    headerArea = r.removeFromTop(40);
    listenArea = r.removeFromTop(72);
    inspectorArea = r.removeFromRight(230);
    velocityArea = r.removeFromBottom(112);
    pianoArea = r.removeFromLeft(76);
    gridArea = r;

    auto h = headerArea.reduced(10, 6);
    title.setBounds(h.removeFromLeft(160));
    h.removeFromLeft(4);
    snapBox.setBounds(h.removeFromLeft(58));
    h.removeFromLeft(4);
    muteButton.setBounds(h.removeFromLeft(50));
    h.removeFromLeft(4);
    recordButton.setBounds(h.removeFromLeft(48));
    h.removeFromLeft(6);
    octaveDown.setBounds(h.removeFromLeft(28));
    octaveUp.setBounds(h.removeFromLeft(28));
    deleteButton.setBounds(h.removeFromRight(64));
    h.removeFromRight(4);
    keepButton.setBounds(h.removeFromRight(52));
    h.removeFromRight(4);
    newTakeButton.setBounds(h.removeFromRight(72));
    h.removeFromRight(4);
    resetButton.setBounds(h.removeFromRight(58));
    h.removeFromRight(4);
    fitButton.setBounds(h.removeFromRight(70));

    auto strip = listenArea.reduced(8, 4);
    auto row1 = strip.removeFromTop(30);
    strip.removeFromTop(4);
    auto row2 = strip;

    listenButton.setBounds(row1.removeFromLeft(96));
    row1.removeFromLeft(4);
    monitorButton.setBounds(row1.removeFromLeft(56));
    row1.removeFromLeft(6);
    listenBarsBox.setBounds(row1.removeFromLeft(72));
    row1.removeFromLeft(6);
    listenKeyBox.setBounds(row1.removeFromLeft(54));
    row1.removeFromLeft(4);
    listenModeBox.setBounds(row1.removeFromLeft(108));
    row1.removeFromLeft(6);
    clearListenButton.setBounds(row1.removeFromRight(92));
    row1.removeFromRight(4);
    listenStatus.setBounds(row1);

    regenButton.setBounds(row2.removeFromLeft(58));
    row2.removeFromLeft(3);
    simplifyButton.setBounds(row2.removeFromLeft(58));
    row2.removeFromLeft(3);
    moveButton.setBounds(row2.removeFromLeft(54));
    row2.removeFromLeft(3);
    followButton.setBounds(row2.removeFromLeft(62));
}

int PianoRoll::getListenBars() const
{
    return juce::jlimit(2, 8, listenBarsBox.getSelectedId());
}

int PianoRoll::getListenKeyRoot() const
{
    return juce::jlimit(0, 11, listenKeyBox.getSelectedId() - 1);
}

int PianoRoll::getListenScaleMode() const
{
    // Combo ids 1..13 map to ScaleMode 0..12
    return juce::jlimit(0, 12, listenModeBox.getSelectedId() - 1);
}

void PianoRoll::setListenKeyRoot(int rootPc)
{
    listenKeyBox.setSelectedId(juce::jlimit(0, 11, rootPc) + 1, juce::dontSendNotification);
}

void PianoRoll::setListenArmed(bool armed, float progress01, bool waitingForNote)
{
    listenArmed = armed;
    listenWaiting = armed && waitingForNote;
    listenProgress = juce::jlimit(0.0f, 1.0f, progress01);
    updateListenChrome();
    repaint(listenArea);
}

void PianoRoll::setListenResult(bool hasResult, int noteCount, const juce::String& summary)
{
    listenHasResult = hasResult;
    listenNoteCount = juce::jmax(0, noteCount);
    listenSummary = summary;
    updateListenChrome();
    repaint();
}

void PianoRoll::updateListenChrome()
{
    listenButton.setToggleState(listenArmed, juce::dontSendNotification);
    if (listenArmed && listenWaiting)
    {
        listenButton.setButtonText("WAIT FOR NOTE");
        listenButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff8a5a18));
        listenButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        listenButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        listenStatus.setText("Waiting for a note… play into the mic to start  |  click LISTEN to cancel",
                             juce::dontSendNotification);
        listenStatus.setColour(juce::Label::textColourId, juce::Colour(0xffffd28a));
    }
    else if (listenArmed)
    {
        listenButton.setButtonText("LISTENING " + juce::String((int) std::round(listenProgress * 100.0f)) + "%");
        listenButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffb43328));
        listenButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        listenButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        listenStatus.setText("Capturing note-for-note  |  keep playing  |  click LISTEN to cancel",
                             juce::dontSendNotification);
        listenStatus.setColour(juce::Label::textColourId, juce::Colour(0xffffc9a8));
    }
    else if (listenHasResult)
    {
        listenButton.setButtonText("LISTEN");
        listenButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff167a8c));
        listenButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        listenButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        juce::String msg = "Bass from audio  |  " + juce::String(listenNoteCount) + " notes";
        if (listenSummary.isNotEmpty())
            msg += "  |  " + listenSummary;
        msg += "  |  CLEAR BASS removes them  |  DELETE removes selection";
        listenStatus.setText(msg, juce::dontSendNotification);
        listenStatus.setColour(juce::Label::textColourId, juce::Colour(0xff9ef0ff));
    }
    else
    {
        listenButton.setButtonText("LISTEN");
        listenButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1aa0b8));
        listenButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        listenButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        listenStatus.setText("AUDIO IN -> BASS  |  waits for first note, then learns note-for-note  |  LISTEN / INPUT",
                             juce::dontSendNotification);
        listenStatus.setColour(juce::Label::textColourId, juce::Colour(0xff9ef0ff));
    }

    monitorButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff245a6a));
    monitorButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

    const bool laneHasNotes = [&]()
    {
        if (const auto* l = laneState(); l != nullptr)
            return ! l->notes.empty() || (l->sourceSnapshotValid && ! l->sourceNotes.empty());
        return false;
    }();
    const bool canClear = listenHasResult || laneHasNotes;
    regenButton.setEnabled(listenHasResult);
    simplifyButton.setEnabled(listenHasResult);
    moveButton.setEnabled(listenHasResult);
    followButton.setEnabled(listenHasResult);
    clearListenButton.setEnabled(canClear);
    clearListenButton.setColour(juce::TextButton::buttonColourId,
                                canClear ? juce::Colour(0xff8a3a18) : juce::Colour(0xff10202c));
    clearListenButton.setColour(juce::TextButton::textColourOffId,
                                canClear ? juce::Colours::white : mutedText);
}

void PianoRoll::fitNotes()
{
    const auto* l = laneState();
    if (l == nullptr || l->notes.empty())
    {
        lowNote = 36;  // C2
        highNote = 60; // C4
        repaint();
        return;
    }
    int lo = 127, hi = 0;
    for (const auto& n : l->notes)
    {
        lo = juce::jmin(lo, n.note);
        hi = juce::jmax(hi, n.note);
    }
    // Keep context around the material while ensuring a useful minimum range.
    lo = juce::jmax(0, lo - 7);
    hi = juce::jmin(127, hi + 7);
    if (hi - lo < 24)
    {
        const int extra = (24 - (hi - lo)) / 2;
        lo = juce::jmax(0, lo - extra);
        hi = juce::jmin(127, lo + 24);
        lo = juce::jmax(0, hi - 24);
    }
    lowNote = lo;
    highNote = hi;
    repaint();
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

int PianoRoll::drumTrackFromY(int y) const
{
    const float rowH = gridArea.getHeight() / (float) groove::kTracks;
    return juce::jlimit(0, groove::kTracks - 1, (int) ((y - gridArea.getY()) / juce::jmax(1.0f, rowH)));
}

int PianoRoll::drumStepFromX(int x) const
{
    const float norm = juce::jlimit(0.0f, 0.999999f, (x - gridArea.getX()) / (float) juce::jmax(1, gridArea.getWidth()));
    return juce::jlimit(0, groove::kSteps - 1, (int) std::floor(norm * groove::kSteps));
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
    if (selectedNote < 0)
    {
        if (listenHasResult && onClearListenClicked)
            onClearListenClicked();
        return;
    }
    if (engine.deleteMidiLaneNote(lane, selectedNote))
    {
        selectedNote = -1;
        if (listenHasResult && listenNoteCount > 0)
        {
            --listenNoteCount;
            if (listenNoteCount <= 0)
                setListenResult(false);
            else
                updateListenChrome();
        }
    }
    repaint();
}

void PianoRoll::mouseDown(const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (isDrumMode())
    {
        if (! gridArea.contains(p)) return;
        const int track = drumTrackFromY(p.y);
        const int step = drumStepFromX(p.x);
        engine.selectUnifiedTarget(track);
        engine.selectStep(track, step);
        // A click is a true toggle even in EUCLID/HYBRID: generated hits can
        // be turned off immediately, and empty cells can be forced on.
        const auto& tr = engine.state().tracks[(size_t) track];
        const auto mode = tr.steps[(size_t) step].overrideMode;
        const bool currentlyOn = groove::Sequencer::resolvedStepActive(tr, step);
        if (currentlyOn)
        {
            if (mode == groove::StepOverrideMode::inherit)
            {
                engine.toggleStep(track, step); // inherit -> forceOn
                engine.toggleStep(track, step); // forceOn -> forceOff
            }
            else if (mode == groove::StepOverrideMode::forceOn)
                engine.toggleStep(track, step); // -> forceOff
        }
        else
        {
            if (mode == groove::StepOverrideMode::forceOff)
            {
                engine.toggleStep(track, step); // -> inherit
                if (! groove::Sequencer::resolvedStepActive(engine.state().tracks[(size_t) track], step))
                    engine.toggleStep(track, step); // -> forceOn
            }
            else
                engine.toggleStep(track, step); // inherit -> forceOn
        }
        selectedNote = track * groove::kSteps + step;
        grabKeyboardFocus();
        refresh();
        return;
    }
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
        if (isDrumMode() && selectedNote >= 0)
        {
            const int track = selectedNote / groove::kSteps;
            const int step = selectedNote % groove::kSteps;
            auto& tr = engine.state().tracks[(size_t) juce::jlimit(0, groove::kTracks - 1, track)];
            auto& st = tr.steps[(size_t) juce::jlimit(0, groove::kSteps - 1, step)];
            st.active = false;
            st.overrideMode = groove::StepOverrideMode::forceOff;
            engine.saveAutosave();
            repaint();
            return true;
        }
        deleteSelected();
        return true;
    }
    return false;
}

void PianoRoll::paint(juce::Graphics& g)
{
    g.fillAll(bg);
    g.setColour(panel);
    g.fillRect(headerArea);
    g.fillRect(inspectorArea);
    g.fillRect(velocityArea);

    // Dedicated LISTEN strip so audio-in → bass is always visible.
    {
        const auto stripColour = listenArmed
                              ? (listenWaiting ? juce::Colour(0xff3a2a10) : juce::Colour(0xff3a1814))
                              : listenHasResult ? juce::Colour(0xff0d2c34)
                              : juce::Colour(0xff0a2430);
        g.setColour(stripColour);
        g.fillRect(listenArea);
        g.setColour(listenArmed
                   ? (listenWaiting ? juce::Colour(0xffffb84a) : juce::Colour(0xffff6a4a))
                   : listenHasResult ? juce::Colour(0xff3fd0e8)
                   : juce::Colour(0xff1aa0b8));
        g.fillRect(listenArea.getX(), listenArea.getBottom() - 2, listenArea.getWidth(), 2);

        if (listenArmed && ! listenWaiting)
        {
            auto bar = listenArea.reduced(8, 0).removeFromBottom(4).withTrimmedBottom(2);
            g.setColour(juce::Colour(0xff401810));
            g.fillRoundedRectangle(bar.toFloat(), 2.0f);
            g.setColour(juce::Colour(0xffff8a5a));
            g.fillRoundedRectangle(bar.withWidth(juce::jmax(4, (int) std::round(bar.getWidth() * listenProgress))).toFloat(), 2.0f);
        }
        else if (listenArmed && listenWaiting)
        {
            // Soft pulse bar while waiting for the first note.
            auto bar = listenArea.reduced(8, 0).removeFromBottom(4).withTrimmedBottom(2);
            const float pulse = 0.35f + 0.35f * std::sin((float) juce::Time::getMillisecondCounter() * 0.008f);
            g.setColour(juce::Colour(0xff403010));
            g.fillRoundedRectangle(bar.toFloat(), 2.0f);
            g.setColour(juce::Colour(0xffffb84a).withAlpha(0.55f + 0.35f * pulse));
            g.fillRoundedRectangle(bar.withWidth(juce::jmax(4, (int) std::round(bar.getWidth() * pulse))).toFloat(), 2.0f);
        }
    }

    // Drums use the same key/timeline language as melodic lanes: each drum voice
    // is a labelled MIDI row, with the same 32-step horizontal time grid.
    if (isDrumMode())
    {
        const float rowH = gridArea.getHeight() / (float) groove::kTracks;
        const float stepW = gridArea.getWidth() / (float) groove::kSteps;
        const auto& st = engine.state();

        for (int t = 0; t < groove::kTracks; ++t)
        {
            const float y = gridArea.getY() + t * rowH;
            g.setColour((t % 2) ? juce::Colour(0xff0a1821) : juce::Colour(0xff0d202b));
            g.fillRect((float) gridArea.getX(), y, (float) gridArea.getWidth(), rowH);
            g.setColour(juce::Colour(0xffe9eef0));
            g.fillRect((float) pianoArea.getX(), y, (float) pianoArea.getWidth(), rowH);
            g.setColour(juce::Colour(0xff26343b));
            g.setFont(juce::FontOptions(juce::jlimit(8.0f, 11.0f, rowH - 3.0f), juce::Font::bold));
            g.drawText(groove::voiceName(t),
                       pianoArea.withY((int) y).withHeight((int) rowH).reduced(5, 0),
                       juce::Justification::centredRight);
            g.setColour(grid.withAlpha(0.55f));
            g.drawHorizontalLine((int) y, (float) gridArea.getX(), (float) gridArea.getRight());
        }

        for (int s = 0; s <= groove::kSteps; ++s)
        {
            const float x = gridArea.getX() + s * stepW;
            const bool bar = (s % 16) == 0;
            const bool beat = (s % 4) == 0;
            g.setColour(bar ? gridStrong : beat ? gridStrong.withAlpha(0.68f) : grid.withAlpha(0.52f));
            g.drawVerticalLine((int) x, (float) gridArea.getY(), (float) velocityArea.getBottom());
            if (beat && s < groove::kSteps)
            {
                g.setColour(mutedText);
                g.setFont(juce::FontOptions(9.0f));
                g.drawText(juce::String(s / 16 + 1) + "." + juce::String((s % 16) / 4 + 1),
                           (int) x + 3, gridArea.getY() + 2, 34, 14,
                           juce::Justification::centredLeft);
            }
        }

        for (int t = 0; t < groove::kTracks; ++t)
        {
            const auto& tr = st.tracks[(size_t) t];
            for (int step = 0; step < groove::kSteps; ++step)
            {
                if (! groove::Sequencer::resolvedStepActive(tr, step))
                    continue;

                const float x = gridArea.getX() + step * stepW;
                const float y = gridArea.getY() + t * rowH;
                auto r = juce::Rectangle<float>(x + 1.5f, y + 2.0f,
                                                juce::jmax(3.0f, stepW - 3.0f),
                                                juce::jmax(3.0f, rowH - 4.0f));
                const bool sel = selectedNote == t * groove::kSteps + step;
                g.setColour(sel ? selectedGreen : noteGreen);
                g.fillRoundedRectangle(r, 2.0f);
                g.setColour(juce::Colour(0xff203611));
                g.drawRoundedRectangle(r, 2.0f, sel ? 1.8f : 0.8f);
            }
        }

        const float playX = gridArea.getX() + stepW * engine.currentMidiTimelineStep();
        g.setColour(juce::Colour(0xff5ee0ff));
        g.drawVerticalLine((int) playX, (float) gridArea.getY(), (float) gridArea.getBottom());

        auto ir = inspectorArea.reduced(14);
        g.setColour(text);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText("DRUM NOTE", ir.removeFromTop(24), juce::Justification::centredLeft);
        g.setColour(mutedText);
        g.setFont(juce::FontOptions(10.0f));
        g.drawFittedText("Click a row/cell to toggle a drum hit. Delete or Backspace clears the selected hit. STEP, EUCLID and HYBRID use this same view.",
                         ir.removeFromTop(96), juce::Justification::topLeft, 6);
        return;
    }

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
        const bool generated = l->rhythmMode != groove::RhythmMode::step;
        const auto& source = (generated && l->sourceSnapshotValid && ! l->sourceNotes.empty()) ? l->sourceNotes : l->notes;

        // In generated modes the original performance remains visible underneath
        // as transparent ghost notes. Nothing destructive happens to the recording.
        for (int i = 0; i < (int) source.size(); ++i)
        {
            const auto& n = source[(size_t) i];
            if (n.note < lowNote || n.note > highNote) continue;
            const auto r = noteRect(n);
            if (generated)
            {
                g.setColour(noteGreen.withAlpha(0.18f));
                g.fillRoundedRectangle(r, 2.0f);
                g.setColour(noteGreen.withAlpha(0.32f));
                g.drawRoundedRectangle(r, 2.0f, 0.8f);
            }
            else
            {
                const auto fill = listenHasResult
                    ? (i == selectedNote ? juce::Colour(0xffb8f7ff) : juce::Colour(0xff3fd0e8))
                    : (i == selectedNote ? selectedGreen : noteGreen);
                g.setColour(fill);
                g.fillRoundedRectangle(r, 2.0f);
                g.setColour(listenHasResult ? juce::Colour(0xff0a3a44) : juce::Colour(0xff203611));
                g.drawRoundedRectangle(r, 2.0f, i == selectedNote ? 1.8f : 0.8f);
            }
        }

        if (generated && ! source.empty())
        {
            const int totalSteps = juce::jmax(1, engine.midiTimelineSteps());
            std::vector<groove::MidiLaneNote> preview;

            auto addPreview = [&](int atStep, int pitch, float vel = 0.85f, int len = 1)
            {
                groove::MidiLaneNote n;
                n.step = juce::jlimit(0, totalSteps - 1, atStep);
                n.note = juce::jlimit(0, 127, pitch);
                n.velocity = juce::jlimit(0.05f, 1.0f, vel);
                n.lengthSteps = juce::jmax(1, len);
                preview.push_back(n);
            };

            if (l->rhythmMode == groove::RhythmMode::euclid || l->rhythmMode == groove::RhythmMode::hybrid)
            {
                std::vector<int> starts;
                for (const auto& n : source)
                    if (std::find(starts.begin(), starts.end(), n.step) == starts.end()) starts.push_back(n.step);
                std::sort(starts.begin(), starts.end());
                int ordinal = 0;
                const int cycle = juce::jmax(1, l->euclidSteps);
                for (int st = 0; st < totalSteps; ++st)
                {
                    const int local = st % cycle;
                    bool gate = groove::Sequencer::euclideanHit(local, l->euclidSteps, l->euclidPulses, l->euclidRotate);
                    if (l->rhythmMode == groove::RhythmMode::hybrid)
                    {
                        const auto ov = l->gateOverrides[(size_t) local];
                        if (ov == groove::StepOverrideMode::forceOn) gate = true;
                        else if (ov == groove::StepOverrideMode::forceOff) gate = false;
                    }
                    if (! gate || starts.empty()) continue;
                    const int sourceStep = starts[(size_t) (ordinal++ % (int) starts.size())];
                    for (const auto& n : source)
                        if (n.step == sourceStep)
                            addPreview(st, n.note + 12 * l->euclidOctave, n.velocity * l->euclidVelocity, 1);
                }
            }
            else if (l->rhythmMode == groove::RhythmMode::answer)
            {
                std::vector<int> pitches; float avgV = 0.0f;
                for (const auto& n : source) { if (std::find(pitches.begin(), pitches.end(), n.note) == pitches.end()) pitches.push_back(n.note); avgV += n.velocity; }
                std::sort(pitches.begin(), pitches.end()); avgV /= juce::jmax(1,(int)source.size());
                const int spacing = juce::jlimit(1,16,l->generatorRate);
                const float amount = juce::jlimit(0.25f,1.0f,l->generatorDepth/4.0f);
                for (int st=0; st<totalSteps && !pitches.empty(); ++st)
                {
                    bool occupied=false,recent=false; for(const auto& n:source){ if(n.step==st) occupied=true; if(n.step<st&&n.step>=juce::jmax(0,st-8)) recent=true; }
                    if(occupied||!recent||(st%spacing)!=0) continue;
                    const juce::uint32 h=(juce::uint32)((l->generatorSeed+1)*2654435761u)^(juce::uint32)((st+1)*2246822519u);
                    if(((float)(h&0xffffu)/65535.0f)>amount) continue;
                    const int idx=(l->generatorSeed+st/spacing)%(int)pitches.size();
                    int pitch=pitches[(size_t)((int)pitches.size()-1-idx)]; if(((h>>18)&3u)==0u) pitch=juce::jlimit(0,127,pitch+12);
                    addPreview(st,pitch,juce::jlimit(0.05f,1.0f,avgV*0.88f),juce::jmax(1,spacing));
                }
            }
            else
            {
                std::vector<int> pitches;
                float v = 0.0f;
                for (const auto& n : source)
                {
                    if (std::find(pitches.begin(), pitches.end(), n.note) == pitches.end()) pitches.push_back(n.note);
                    v += n.velocity;
                }
                std::sort(pitches.begin(), pitches.end());
                const float avgV = v / juce::jmax(1, (int) source.size());
                const int rate = juce::jlimit(1, 16, l->generatorRate);
                for (int st = 0, ordinal = 0; st < totalSteps; st += rate, ++ordinal)
                {
                    if (pitches.empty()) break;
                    int pitch = pitches.front();
                    if (l->rhythmMode == groove::RhythmMode::arp)
                    {
                        const int depth = juce::jlimit(1, 4, l->generatorDepth);
                        pitch = pitches[(size_t) (ordinal % (int) pitches.size())]
                              + 12 * ((ordinal / (int) pitches.size()) % depth);
                    }
                    else
                    {
                        int idx = l->generatorSeed % (int) pitches.size();
                        const int depth = juce::jlimit(1, 4, l->generatorDepth);
                        for (int i = 0; i < ordinal; ++i)
                        {
                            const juce::uint32 h = (juce::uint32) ((l->generatorSeed + 1) * 2654435761u)
                                                 ^ (juce::uint32) ((i + 1) * 2246822519u);
                            int jump = (int) (h % (juce::uint32) (2 * depth + 1)) - depth;
                            if (jump == 0) jump = ((h >> 8) & 1u) ? 1 : -1;
                            idx = juce::jlimit(0, (int) pitches.size() - 1, idx + jump);
                        }
                        pitch = pitches[(size_t) idx];
                    }
                    addPreview(st, pitch, avgV, rate);
                }
            }

            const auto previewColour = l->rhythmMode == groove::RhythmMode::walk
                ? juce::Colour(0xff55bfe8) : l->rhythmMode == groove::RhythmMode::answer ? juce::Colour(0xffffb24d) : selectedGreen;
            for (const auto& n : preview)
            {
                if (n.note < lowNote || n.note > highNote) continue;
                const auto r = noteRect(n);
                g.setColour(previewColour.withAlpha(0.88f));
                g.fillRoundedRectangle(r, 2.0f);
                g.setColour(previewColour.brighter(0.25f));
                g.drawRoundedRectangle(r, 2.0f, 1.0f);
            }

            g.setColour(mutedText);
            g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
            g.drawText("GHOST = ORIGINAL RECORDING    SOLID = GENERATED    RESET = RESTORE ORIGINAL",
                       gridArea.getX() + 8, gridArea.getY() + 20, juce::jmin(620, gridArea.getWidth() - 16), 16,
                       juce::Justification::centredLeft);
        }

        // Velocity lane.
        g.setColour(text); g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText("VELOCITY", velocityArea.getX() + 8, velocityArea.getY() + 5, 90, 16, juce::Justification::centredLeft);
        g.setColour(mutedText); g.setFont(juce::FontOptions(9.0f));
        g.drawText("127", velocityArea.getX() + 8, velocityArea.getY() + 22, 28, 12, juce::Justification::left);
        g.drawText("64", velocityArea.getX() + 8, velocityArea.getCentreY() - 5, 28, 12, juce::Justification::left);
        g.drawText("0", velocityArea.getX() + 8, velocityArea.getBottom() - 16, 28, 12, juce::Justification::left);
        for (int i = 0; i < (int) source.size(); ++i)
        {
            const auto& n = source[(size_t) i];
            const float x = gridArea.getX() + gridArea.getWidth() * (n.step / (float) total);
            const float base = (float) velocityArea.getBottom() - 10.0f;
            const float top = base - (velocityArea.getHeight() - 30.0f) * juce::jlimit(0.0f, 1.0f, n.velocity);
            g.setColour(generated ? noteGreen.withAlpha(0.25f) : (i == selectedNote ? selectedGreen : noteGreen));
            g.drawLine(x, base, x, top, generated ? 1.0f : (i == selectedNote ? 2.5f : 1.5f));
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
        g.drawFittedText("Drag note = move pitch/time\nDrag right edge = length\nDrag velocity stem = velocity\nDELETE / Backspace removes selection\nCLEAR BASS removes listen notes",
                         ir.removeFromTop(100), juce::Justification::topLeft, 6);
    }
    else
    {
        g.setColour(mutedText); g.setFont(juce::FontOptions(10.5f));
        juce::String help = "Click the piano roll to create a MIDI note. Note width is duration; velocity appears below.";
        if (listenHasResult)
            help = "Cyan notes came from LISTEN. CLEAR BASS removes all of them. Select one and press DELETE to remove just that note.";
        else if (! listenArmed)
            help = "Use the cyan LISTEN strip: pick bars, press LISTEN, play into audio input — a bassline is written here.";
        g.drawFittedText(help, ir.removeFromTop(90), juce::Justification::topLeft, 5);
    }
}
