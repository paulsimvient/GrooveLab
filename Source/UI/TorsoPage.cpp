#include "TorsoPage.h"
#include "../Audio/DrumMidi.h"
#include "../Sequencer/Sequencer.h"

namespace
{
constexpr int kEuclidTargets = groove::kUnifiedTracks;

constexpr std::array<groove::Param, groove::paramCount> parameterOrder {
    groove::Param::pitch, groove::Param::decay, groove::Param::transient, groove::Param::noise,
    groove::Param::filter, groove::Param::drive, groove::Param::space, groove::Param::blend
};

const char* displayName(groove::Param p)
{
    switch (p)
    {
        case groove::Param::pitch:     return "PITCH";
        case groove::Param::decay:     return "DECAY";
        case groove::Param::transient: return "TRANSIENT";
        case groove::Param::noise:     return "TEXTURE";
        case groove::Param::filter:    return "FILTER";
        case groove::Param::drive:     return "DRIVE";
        case groove::Param::space:     return "SPACE";
        case groove::Param::blend:     return "BLEND";
        default:                       return "?";
    }
}

juce::Colour trackColour(int t)
{
    static const juce::Colour colours[] = {
        juce::Colour(0xffff8a22), juce::Colour(0xff3ba7ff), juce::Colour(0xffb85cff), juce::Colour(0xffffc438),
        juce::Colour(0xff8ed044), juce::Colour(0xff46d6d8), juce::Colour(0xffff4f8a), juce::Colour(0xffb9d9ec)
    };
    return colours[juce::jlimit(0, groove::kTracks - 1, t)];
}

void setupRotary(juce::Slider& s, double min, double max, double step)
{
    s.setRange(min, max, step);
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 52, 16);
}
}

TorsoPage::TorsoPage(groove::GrooveEngine& e)
    : engine(e)
{
    setOpaque(true);
    setupRotary(steps, 1.0, (double) groove::kSteps, 1.0);
    setupRotary(pulses, 0.0, (double) groove::kSteps, 1.0);
    setupRotary(rotate, 0.0, (double) groove::kSteps - 1, 1.0);
    setupRotary(velocity, 0.0, 1.2, 0.01);
    groove::fillUjamKitCombo(kitNote);
    setupRotary(probability, 0.0, 1.0, 0.01);
    setupRotary(repeats, 1.0, 4.0, 1.0);
    setupRotary(melodicVelocity, 0.0, 1.2, 0.01);
    setupRotary(melodicProbability, 0.0, 1.0, 0.01);
    setupRotary(melodicRepeats, 1.0, 4.0, 1.0);
    setupRotary(melodicOctave, -3.0, 3.0, 1.0);
    setupRotary(melodicGate, 0.05, 1.0, 0.01);

    for (int i = 0; i < groove::paramCount; ++i)
    {
        auto p = parameterOrder[(size_t) i];
        auto& sl = soundSliders[(size_t) i];
        if (p == groove::Param::pitch) setupRotary(sl, 30.0, 1600.0, 1.0);
        else if (p == groove::Param::decay) setupRotary(sl, 20.0, 1800.0, 1.0);
        else setupRotary(sl, 0.0, 1.0, 0.01);
        addAndMakeVisible(sl);
    }

    division.addItem("1/4x", 1);
    division.addItem("1/2x", 2);
    division.addItem("1x", 3);
    division.addItem("2x", 4);
    division.addItem("4x", 5);

    rhythmMode.addItem("STEP", 1);
    rhythmMode.addItem("EUCLID", 2);
    rhythmMode.addItem("HYBRID", 3);
    rhythmMode.addItem("ARP", 4);
    rhythmMode.addItem("WALK", 5);
    rhythmMode.addItem("ANSWER", 6);

    addAndMakeVisible(steps);
    addAndMakeVisible(pulses);
    addAndMakeVisible(rotate);
    addAndMakeVisible(division);
    addAndMakeVisible(rhythmMode);
    addAndMakeVisible(velocity);
    addAndMakeVisible(kitNote);
    addAndMakeVisible(probability);
    addAndMakeVisible(repeats);
    addAndMakeVisible(melodicVelocity);
    addAndMakeVisible(melodicProbability);
    addAndMakeVisible(melodicRepeats);
    addAndMakeVisible(melodicOctave);
    addAndMakeVisible(melodicGate);
    addAndMakeVisible(playStep);
    addAndMakeVisible(clearStep);

    playStep.onClick = [this] { playTrack(engine.state().selectedTrack); };
    for (int i = 0; i < groove::kTracks; ++i)
    {
        auto& b = trackPlay[(size_t) i];
        b.setButtonText("PLAY");
        b.onClick = [this, i] { playTrack(i); };
        addAndMakeVisible(b);
    }
    clearStep.onClick = [this]
    {
        engine.clearAllLocks(engine.state().selectedTrack, engine.state().selectedStep);
        refreshFromEngine();
        if (onPatternChanged) onPatternChanged();
        repaint();
    };

    bindGeneratorKnobs();
    bindStepKnobs();
    refreshFromEngine();
    startTimerHz(24);
}

void TorsoPage::setMidiLane(int laneIndex)
{
    midiLane = (laneIndex > 0 && laneIndex < groove::kMidiLanes) ? laneIndex : -1;
    if (midiLane > 0)
        engine.selectUnifiedTarget(groove::unifiedTrackForMidiLane(midiLane));
    else if (groove::unifiedTrackIsDrum(engine.state().selectedTarget))
        engine.selectUnifiedTarget(engine.state().selectedTrack);
    rhythmMode.setItemEnabled(3, true);
    rhythmMode.setItemEnabled(4, midiLane > 0);
    rhythmMode.setItemEnabled(5, midiLane > 0);
    rhythmMode.setItemEnabled(6, midiLane > 0);
    refreshFromEngine();
    resized();
    repaint();
}


void TorsoPage::setCompactMelodicMode(bool compact)
{
    if (compactMelodicMode == compact) return;
    compactMelodicMode = compact;
    resized();
    repaint();
}

void TorsoPage::bindGeneratorKnobs()
{
    auto onGen = [this] { commitGenerator(); };
    steps.onValueChange = onGen;
    pulses.onValueChange = onGen;
    rotate.onValueChange = onGen;
    steps.onDragEnd = [this] { refreshFromEngine(); if (onPatternChanged) onPatternChanged(); };
    pulses.onDragEnd = [this] { refreshFromEngine(); if (onPatternChanged) onPatternChanged(); };
    rotate.onDragEnd = [this] { refreshFromEngine(); if (onPatternChanged) onPatternChanged(); };

    division.onChange = [this]
    {
        if (refreshing || editingMidiLane()) return;
        const float d[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
        engine.setTrackDivision(engine.state().selectedTrack,
                                d[juce::jlimit(1, 5, division.getSelectedId()) - 1]);
        if (onPatternChanged) onPatternChanged();
    };

    rhythmMode.onChange = [this]
    {
        if (refreshing) return;
        const int maxId = editingMidiLane() ? 6 : 3;
        const int id = juce::jlimit(1, maxId, rhythmMode.getSelectedId());
        if (editingMidiLane())
        {
            engine.setMidiLaneRhythmMode(midiLane, (groove::RhythmMode) (id - 1));
        }
        else
        {
            engine.setTrackRhythmMode(engine.state().selectedTrack, (groove::RhythmMode) (id - 1));
        }
        refreshFromEngine();
        if (onPatternChanged) onPatternChanged();
        repaint();
    };
}

void TorsoPage::bindStepKnobs()
{
    velocity.onValueChange = [this]
    {
        if (refreshing) return;
        engine.setVelocity(engine.state().selectedTrack, engine.state().selectedStep,
                           (float) velocity.getValue());
        if (onPatternChanged) onPatternChanged();
        repaint();
    };

    kitNote.onChange = [this]
    {
        if (refreshing) return;
        const int t = engine.state().selectedTrack;
        const int s = engine.state().selectedStep;
        const int id = kitNote.getSelectedId();
        if (id <= 0) return;
        const int note = id - 1;
        engine.setTrackMidiNote(t, note);
        engine.setStepMidiNote(t, s, note);
        if (onPatternChanged) onPatternChanged();
        playTrack(t);
        refreshFromEngine();
        repaint();
    };

    probability.onValueChange = [this]
    {
        if (refreshing) return;
        engine.setProbability(engine.state().selectedTrack, engine.state().selectedStep,
                              (float) probability.getValue());
        if (onPatternChanged) onPatternChanged();
    };

    repeats.onValueChange = [this]
    {
        if (refreshing) return;
        engine.setRatchet(engine.state().selectedTrack, engine.state().selectedStep,
                          (int) repeats.getValue());
        if (onPatternChanged) onPatternChanged();
        repaint();
    };

    auto commitMelodicPerformance = [this]
    {
        if (refreshing || ! editingMidiLane()) return;
        engine.setMidiLaneEuclidPerformance(midiLane,
            (float) melodicVelocity.getValue(),
            (float) melodicProbability.getValue(),
            (int) melodicRepeats.getValue(),
            (int) melodicOctave.getValue(),
            (float) melodicGate.getValue());
        if (onPatternChanged) onPatternChanged();
        repaint();
    };
    melodicVelocity.onValueChange = commitMelodicPerformance;
    melodicProbability.onValueChange = commitMelodicPerformance;
    melodicRepeats.onValueChange = commitMelodicPerformance;
    melodicOctave.onValueChange = commitMelodicPerformance;
    melodicGate.onValueChange = commitMelodicPerformance;

    for (int i = 0; i < groove::paramCount; ++i)
    {
        auto p = parameterOrder[(size_t) i];
        auto& sl = soundSliders[(size_t) i];
        sl.onValueChange = [this, p, &sl]
        {
            if (refreshing) return;
            engine.setStepParam(engine.state().selectedTrack,
                                engine.state().selectedStep,
                                p,
                                (float) sl.getValue(),
                                true);
            if (onPatternChanged) onPatternChanged();
            repaint();
        };
    }
}

void TorsoPage::commitGenerator()
{
    if (refreshing) return;

    if (editingMidiLane())
    {
        const auto mode = engine.state().midiLanes[(size_t) midiLane].rhythmMode;
        if (mode == groove::RhythmMode::arp || mode == groove::RhythmMode::walk || mode == groove::RhythmMode::answer)
        {
            const int rate = juce::jlimit(1, 16, (int) steps.getValue());
            const int depth = juce::jlimit(1, 4, (int) pulses.getValue());
            const int seed = juce::jlimit(0, 31, (int) rotate.getValue());
            engine.setMidiLaneGeneratorControls(midiLane, rate, depth, seed);
        }
        else
        {
            const int st = juce::jlimit(1, groove::kSteps, (int) steps.getValue());
            const int pu = juce::jlimit(0, st, (int) pulses.getValue());
            const int ro = juce::jmax(0, (int) rotate.getValue());
            engine.setMidiLaneEuclid(midiLane, true, st, pu, ro);
            rhythmMode.setSelectedId(2, juce::dontSendNotification);
        }
        repaint();
        if (onPatternChanged) onPatternChanged();
        return;
    }

    const int t = engine.state().selectedTrack;
    const auto mode = engine.state().tracks[(size_t) t].rhythmMode;
    if (mode == groove::RhythmMode::step)
        engine.setTrackRhythmMode(t, groove::RhythmMode::euclid);

    engine.setTrackSteps(t, (int) steps.getValue());
    engine.setTrackPulses(t, (int) pulses.getValue());
    engine.setTrackRotate(t, (int) rotate.getValue());
    rhythmMode.setSelectedId((int) engine.state().tracks[(size_t) t].rhythmMode + 1,
                             juce::dontSendNotification);
    repaint();
    if (onPatternChanged) onPatternChanged();
}

void TorsoPage::refreshFromEngine()
{
    refreshing = true;
    const auto& st = engine.state();

    // selectedTarget is the one selection state shared across GRID / EUC / NOTES.
    const int target = juce::jlimit(0, groove::kUnifiedTracks - 1, st.selectedTarget);
    midiLane = groove::unifiedTrackMidiLane(target);
    rhythmMode.setItemEnabled(3, true);
    rhythmMode.setItemEnabled(4, editingMidiLane());
    rhythmMode.setItemEnabled(5, editingMidiLane());
    rhythmMode.setItemEnabled(6, editingMidiLane());

    if (editingMidiLane())
    {
        const auto& lane = st.midiLanes[(size_t) midiLane];
        const bool noteGenerator = lane.rhythmMode == groove::RhythmMode::arp || lane.rhythmMode == groove::RhythmMode::walk || lane.rhythmMode == groove::RhythmMode::answer;
        if (noteGenerator)
        {
            steps.setRange(1.0, 16.0, 1.0);
            pulses.setRange(1.0, 4.0, 1.0);
            rotate.setRange(0.0, 31.0, 1.0);
            steps.setValue((double) lane.generatorRate, juce::dontSendNotification);
            pulses.setValue((double) lane.generatorDepth, juce::dontSendNotification);
            rotate.setValue((double) lane.generatorSeed, juce::dontSendNotification);
        }
        else
        {
            steps.setRange(1.0, (double) groove::kSteps, 1.0);
            pulses.setRange(0.0, (double) groove::kSteps, 1.0);
            rotate.setRange(0.0, (double) groove::kSteps - 1, 1.0);
            steps.setValue((double) lane.euclidSteps, juce::dontSendNotification);
            pulses.setValue((double) lane.euclidPulses, juce::dontSendNotification);
            const int rr = ((lane.euclidRotate % juce::jmax(1, lane.euclidSteps))
                           + juce::jmax(1, lane.euclidSteps)) % juce::jmax(1, lane.euclidSteps);
            rotate.setValue((double) rr, juce::dontSendNotification);
        }
        rhythmMode.setSelectedId((int) lane.rhythmMode + 1, juce::dontSendNotification);
        division.setSelectedId(3, juce::dontSendNotification);
        melodicVelocity.setValue(lane.euclidVelocity, juce::dontSendNotification);
        melodicProbability.setValue(lane.euclidProbability, juce::dontSendNotification);
        melodicRepeats.setValue(lane.euclidRepeats, juce::dontSendNotification);
        melodicOctave.setValue(lane.euclidOctave, juce::dontSendNotification);
        melodicGate.setValue(lane.euclidGate, juce::dontSendNotification);
        refreshing = false;
        return;
    }

    steps.setRange(1.0, (double) groove::kSteps, 1.0);
    pulses.setRange(0.0, (double) groove::kSteps, 1.0);
    rotate.setRange(0.0, (double) groove::kSteps - 1, 1.0);
    const auto& tr = st.tracks[st.selectedTrack];
    const auto& step = tr.steps[st.selectedStep];
    const auto p = st.effectiveParams(st.selectedTrack, st.selectedStep);

    steps.setValue((double) tr.generatorSteps, juce::dontSendNotification);
    pulses.setValue((double) tr.pulses, juce::dontSendNotification);
    const int rr = ((tr.rotate % juce::jmax(1, tr.generatorSteps)) + juce::jmax(1, tr.generatorSteps))
                   % juce::jmax(1, tr.generatorSteps);
    rotate.setValue((double) rr, juce::dontSendNotification);
    const int did = tr.division < 0.375f ? 1 : tr.division < 0.75f ? 2
                  : tr.division < 1.5f ? 3 : tr.division < 3.0f ? 4 : 5;
    division.setSelectedId(did, juce::dontSendNotification);
    rhythmMode.setSelectedId((int) tr.rhythmMode + 1, juce::dontSendNotification);

    velocity.setValue(step.velocity, juce::dontSendNotification);
    kitNote.setSelectedId(engine.effectiveMidiNote(st.selectedTrack, st.selectedStep) + 1,
                          juce::dontSendNotification);
    probability.setValue(step.probability, juce::dontSendNotification);
    repeats.setValue((double) juce::jmax(1, step.ratchet), juce::dontSendNotification);

    soundSliders[(int) groove::Param::pitch].setValue(p.pitchHz, juce::dontSendNotification);
    soundSliders[(int) groove::Param::decay].setValue(p.decayMs, juce::dontSendNotification);
    soundSliders[(int) groove::Param::transient].setValue(p.transient, juce::dontSendNotification);
    soundSliders[(int) groove::Param::noise].setValue(p.noise, juce::dontSendNotification);
    soundSliders[(int) groove::Param::filter].setValue(p.filter, juce::dontSendNotification);
    soundSliders[(int) groove::Param::drive].setValue(p.drive, juce::dontSendNotification);
    soundSliders[(int) groove::Param::space].setValue(p.space, juce::dontSendNotification);
    soundSliders[(int) groove::Param::blend].setValue(p.blend, juce::dontSendNotification);

    refreshing = false;
}

void TorsoPage::timerCallback()
{
    repaint();
}

void TorsoPage::playTrack(int track)
{
    engine.selectStep(track, engine.state().selectedStep);
    engine.auditionSelected();
    refreshFromEngine();
    if (onPatternChanged) onPatternChanged();
    repaint();
}

juce::Rectangle<int> TorsoPage::pulsePad(int index) const
{
    auto r = pulsePanel.reduced(16);
    r.removeFromTop(48);
    const int cols = 8;
    const int rows = 4;
    const int gap = 6;
    const int w = (r.getWidth() - (cols - 1) * gap) / cols;
    const int h = (r.getHeight() - (rows - 1) * gap) / rows;
    const int col = index % cols;
    const int row = index / cols;
    return { r.getX() + col * (w + gap), r.getY() + row * (h + gap), w, h };
}

juce::Rectangle<int> TorsoPage::trackPad(int index) const
{
    auto r = trackPanel.reduced(16, 12);
    const int gap = 6;
    const int w = juce::jmax(54, (r.getWidth() - (kEuclidTargets - 1) * gap) / kEuclidTargets);
    return { r.getX() + index * (w + gap), r.getY(), w, r.getHeight() };
}

void TorsoPage::resized()
{
    auto bounds = getLocalBounds().reduced(12, 8);
    const bool melodic = editingMidiLane();
    const bool compact = melodic && compactMelodicMode;

    // Tracks live in the global InstrumentDock — never here.
    trackPanel = {};
    for (auto& b : trackPlay)
    {
        b.setVisible(false);
        b.setBounds({});
    }

    // STEP EDIT sits under the rhythm grid with a fixed, comfortable height.
    if (compact)
    {
        stepPanel = {};
    }
    else
    {
        const int stepH = melodic ? 96 : 132;
        stepPanel = bounds.removeFromBottom(juce::jmin(stepH, juce::jmax(88, bounds.getHeight() / 3)));
        bounds.removeFromBottom(8);
    }

    // SHAPE column is fixed-width so knobs never spill into the rhythm grid.
    const int shapeW = juce::jlimit(200, 260, bounds.getWidth() / 4);
    shapePanel = bounds.removeFromLeft(shapeW);
    bounds.removeFromLeft(8);
    pulsePanel = bounds;

    // --- SHAPE ---
    auto s = shapePanel.reduced(12, 10);
    s.removeFromTop(26); // title
    rhythmMode.setBounds(s.removeFromTop(24));
    s.removeFromTop(18); // MODE label + gap for STEPS/PULSES labels

    if (! melodic)
    {
        const int gap = 6;
        const int cols = 4;
        const int col = juce::jmax(44, (s.getWidth() - gap * (cols - 1)) / cols);
        const int knobH = juce::jlimit(48, 64, s.getHeight());
        const int y = s.getY() + juce::jmax(0, (s.getHeight() - knobH) / 2);
        steps.setBounds(s.getX(), y, col, knobH);
        pulses.setBounds(s.getX() + (col + gap), y, col, knobH);
        rotate.setBounds(s.getX() + 2 * (col + gap), y, col, knobH);
        division.setBounds(s.getX() + 3 * (col + gap),
                           y + juce::jmax(4, (knobH - 24) / 2), col, 24);
    }
    else
    {
        const int gap = 6;
        const int col = juce::jmax(70, (s.getWidth() - gap) / 2);
        const int knobH = juce::jlimit(52, compact ? 64 : 72, (s.getHeight() - gap) / 2);
        steps.setBounds(s.getX(), s.getY(), col, knobH);
        pulses.setBounds(s.getX() + col + gap, s.getY(), col, knobH);
        rotate.setBounds(s.getX(), s.getY() + knobH + gap, col, knobH);
        division.setBounds({});
    }

    division.setVisible(! melodic);
    velocity.setVisible(! melodic && ! compact);
    kitNote.setVisible(! melodic && ! compact);
    probability.setVisible(! melodic && ! compact);
    repeats.setVisible(! melodic && ! compact);
    playStep.setVisible(! melodic && ! compact);
    clearStep.setVisible(! melodic && ! compact);

    // Drum sound macros: only when the step panel is tall enough.
    const bool showSound = ! melodic && ! compact && stepPanel.getHeight() >= 118;
    for (auto& sl : soundSliders)
        sl.setVisible(showSound);

    melodicVelocity.setVisible(melodic && ! compact);
    melodicProbability.setVisible(melodic && ! compact);
    melodicRepeats.setVisible(melodic && ! compact);
    melodicOctave.setVisible(melodic && ! compact);
    melodicGate.setVisible(melodic && ! compact);

    if (compact)
    {
        velocity.setBounds({});
        kitNote.setBounds({});
        probability.setBounds({});
        repeats.setBounds({});
        playStep.setBounds({});
        clearStep.setBounds({});
        for (auto& sl : soundSliders) sl.setBounds({});
        melodicVelocity.setBounds({});
        melodicProbability.setBounds({});
        melodicRepeats.setBounds({});
        melodicOctave.setBounds({});
        melodicGate.setBounds({});
        return;
    }

    auto sp = stepPanel.reduced(12, 8);
    sp.removeFromTop(22); // panel title

    auto buttons = sp.removeFromRight(96);
    playStep.setBounds(buttons.removeFromTop(26));
    buttons.removeFromTop(6);
    clearStep.setBounds(buttons.removeFromTop(26));
    sp.removeFromRight(8);

    if (melodic)
    {
        const int mCols = 5;
        const int mW = juce::jmax(70, sp.getWidth() / mCols);
        const int knobH = juce::jlimit(48, 68, sp.getHeight());
        const int y = sp.getY() + juce::jmax(0, (sp.getHeight() - knobH) / 2);
        melodicVelocity.setBounds(sp.getX(), y, mW, knobH);
        melodicProbability.setBounds(sp.getX() + mW, y, mW, knobH);
        melodicRepeats.setBounds(sp.getX() + 2 * mW, y, mW, knobH);
        melodicOctave.setBounds(sp.getX() + 3 * mW, y, mW, knobH);
        melodicGate.setBounds(sp.getX() + 4 * mW, y, mW, knobH);
        velocity.setBounds({});
        kitNote.setBounds({});
        probability.setBounds({});
        repeats.setBounds({});
        for (auto& sl : soundSliders) sl.setBounds({});
        return;
    }

    // Row 1: VELOCITY · NOTE · PROB · REPEATS
    auto row1 = showSound ? sp.removeFromTop(juce::jlimit(48, 58, sp.getHeight() / 2)) : sp;
    const int gap = 8;
    const int colW = juce::jmax(72, (row1.getWidth() - gap * 3) / 4);
    const int knobH = juce::jlimit(44, 56, row1.getHeight());
    const int y1 = row1.getY() + juce::jmax(0, (row1.getHeight() - knobH) / 2);
    velocity.setBounds(row1.getX(), y1, colW, knobH);
    kitNote.setBounds(row1.getX() + colW + gap, y1 + juce::jmax(4, (knobH - 24) / 2), colW, 24);
    probability.setBounds(row1.getX() + 2 * (colW + gap), y1, colW, knobH);
    repeats.setBounds(row1.getX() + 3 * (colW + gap), y1, colW, knobH);

    if (showSound)
    {
        sp.removeFromTop(4);
        const int n = groove::paramCount;
        const int soundW = juce::jmax(52, sp.getWidth() / n);
        const int soundH = juce::jlimit(44, 56, sp.getHeight());
        const int y2 = sp.getY() + juce::jmax(0, (sp.getHeight() - soundH) / 2);
        for (int i = 0; i < n; ++i)
            soundSliders[(size_t) i].setBounds(sp.getX() + i * soundW, y2, soundW, soundH);
    }
    else
    {
        for (auto& sl : soundSliders) sl.setBounds({});
    }

    melodicVelocity.setBounds({});
    melodicProbability.setBounds({});
    melodicRepeats.setBounds({});
    melodicOctave.setBounds({});
    melodicGate.setBounds({});
}

void TorsoPage::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff05070a));

    auto panel = [&g](juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour(juce::Colour(0xff0b1014));
        g.fillRoundedRectangle(r.toFloat(), 8.0f);
        g.setColour(juce::Colour(0xff2a2118));
        g.drawRoundedRectangle(r.toFloat(), 8.0f, 1.2f);
        g.setColour(juce::Colour(0xffff8a22));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(title, r.reduced(16, 10).removeFromTop(22), juce::Justification::centredLeft);
    };

    const auto& st = engine.state();

    if (editingMidiLane())
    {
        const auto& lane = st.midiLanes[(size_t) midiLane];
        const auto name = juce::String(groove::midiLaneName(midiLane));
        panel(shapePanel, "CREATE  ·  " + name);
        const bool generatorMode = lane.rhythmMode == groove::RhythmMode::arp || lane.rhythmMode == groove::RhythmMode::walk || lane.rhythmMode == groove::RhythmMode::answer;
        panel(pulsePanel, generatorMode ? "GENERATED NOTE PERFORMANCE  ·  ORIGINAL RECORDING STAYS INTACT"
                                        : "EUCLIDEAN GATE  ·  SAME WINDOW AS DRUMS");
        if (! compactMelodicMode)
            panel(stepPanel, generatorMode ? "PERFORMANCE SOURCE  ·  ARP / WALK / ANSWER READ THE ORIGINAL RECORDING"
                                           : "MELODIC PERFORMANCE  ·  EUCLID PULSES ADVANCE THROUGH PIANO-ROLL NOTES");

        g.setColour(juce::Colour(0xff8a7a68));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText("MODE", shapePanel.getX() + 22, shapePanel.getY() + 34, 70, 14, juce::Justification::centredLeft);
        const bool noteGenerator = lane.rhythmMode == groove::RhythmMode::arp || lane.rhythmMode == groove::RhythmMode::walk || lane.rhythmMode == groove::RhythmMode::answer;
        g.drawText(noteGenerator ? (lane.rhythmMode == groove::RhythmMode::answer ? "SPACE" : "RATE") : "STEPS", shapePanel.getX() + 22, shapePanel.getY() + 72, 70, 14, juce::Justification::centredLeft);
        g.drawText(noteGenerator ? (lane.rhythmMode == groove::RhythmMode::arp ? "OCTAVES" : lane.rhythmMode == groove::RhythmMode::answer ? "AMOUNT" : "JUMP") : "PULSES",
                   shapePanel.getX() + 22 + shapePanel.getWidth() / 2 - 16,
                   shapePanel.getY() + 72, 70, 14, juce::Justification::centredLeft);
        g.drawText(noteGenerator ? (lane.rhythmMode == groove::RhythmMode::answer ? "TAKE" : "SEED") : "ROTATE", shapePanel.getX() + 22, shapePanel.getY() + 168, 70, 14, juce::Justification::centredLeft);

        g.setColour(juce::Colour(0xffffc38a));
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        juce::String modeSummary = name + "  ·  " + groove::rhythmModeName(lane.rhythmMode);
        if (noteGenerator)
            modeSummary += "  ·  rate " + juce::String(lane.generatorRate) + "  depth " + juce::String(lane.generatorDepth) + "  seed " + juce::String(lane.generatorSeed);
        else
            modeSummary += "  ·  " + juce::String(lane.euclidPulses) + "/" + juce::String(lane.euclidSteps) + "  rot " + juce::String(lane.euclidRotate);
        g.drawText(modeSummary, pulsePanel.reduced(16, 12).removeFromTop(28), juce::Justification::centredLeft);

        const int play = engine.currentStep();
        for (int step = 0; step < groove::kSteps; ++step)
        {
            auto pad = pulsePad(step).toFloat();
            const bool noteGenerator = lane.rhythmMode == groove::RhythmMode::arp || lane.rhythmMode == groove::RhythmMode::walk || lane.rhythmMode == groove::RhythmMode::answer;
            const bool outside = noteGenerator ? false : step >= lane.euclidSteps;
            bool gate = false;
            if (noteGenerator)
                gate = (step % juce::jmax(1, lane.generatorRate)) == 0;
            else
                gate = lane.rhythmMode != groove::RhythmMode::step && ! outside
                    && groove::Sequencer::euclideanHit(step, lane.euclidSteps, lane.euclidPulses, lane.euclidRotate);
            if (lane.rhythmMode == groove::RhythmMode::hybrid && ! outside)
            {
                const auto ov = lane.gateOverrides[(size_t) step];
                if (ov == groove::StepOverrideMode::forceOn) gate = true;
                else if (ov == groove::StepOverrideMode::forceOff) gate = false;
            }
            bool hasNote = false;
            const auto& sourceNotes = (lane.sourceSnapshotValid && ! lane.sourceNotes.empty()) ? lane.sourceNotes : lane.notes;
            for (const auto& n : sourceNotes)
                if (n.step == step) { hasNote = true; break; }

            g.setColour(outside ? juce::Colour(0xff0a0c0e) : juce::Colour(0xff16120e));
            g.fillRoundedRectangle(pad, 5.0f);
            if (gate)
            {
                g.setColour(lane.rhythmMode == groove::RhythmMode::walk ? juce::Colour(0xff55bfe8) : juce::Colour(0xff57d98f));
                g.fillRoundedRectangle(pad.reduced(2), 4.0f);
            }
            if (hasNote)
            {
                g.setColour(juce::Colours::white.withAlpha(0.9f));
                g.fillEllipse(pad.getCentreX() - 3.0f, pad.getBottom() - 10.0f, 6.0f, 6.0f);
            }
            const bool playing = (step == (play % juce::jmax(1, groove::kSteps)));
            g.setColour(playing ? juce::Colours::white : juce::Colour(0xff3a2e22));
            g.drawRoundedRectangle(pad, 5.0f, playing ? 2.0f : 1.0f);
            g.setColour(outside ? juce::Colour(0xff33302c) : juce::Colour(0xfff3e2cc));
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText(juce::String(step + 1), pad.toNearestInt().reduced(2, 2).removeFromTop(18), juce::Justification::centred);
        }

        if (! compactMelodicMode)
        {
            auto mLabel = stepPanel.reduced(14).removeFromTop(38);
            const int mW = juce::jmax(74, (stepPanel.getWidth() - 28) / 5);
            const char* melodicLabels[] = { "VELOCITY", "PROBABILITY", "REPEATS", "OCTAVE", "GATE" };
            g.setColour(juce::Colour(0xff8a7a68));
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            for (int i = 0; i < 5; ++i)
                g.drawText(melodicLabels[i], mLabel.getX() + i * mW, mLabel.getY() + 20, mW, 14, juce::Justification::centred);
            g.setColour(juce::Colour(0xffd8e5dd));
            g.setFont(juce::FontOptions(10.5f));
            const juce::String help = lane.rhythmMode == groove::RhythmMode::arp
                ? "ARP reads the original recording as a note pool · source notes stay visible as ghosts"
                : lane.rhythmMode == groove::RhythmMode::walk
                    ? "WALK moves deterministically through the recorded note pool · change SEED for a new path"
                    : lane.rhythmMode == groove::RhythmMode::answer
                        ? "ANSWER uses your phrase as a silent source and plays only the generated response · original stays visible as ghost notes"
                        : "Piano roll supplies pitch/chords · Euclidean pulses advance through the recorded note events";
            g.drawText(help, stepPanel.reduced(18).removeFromBottom(20), juce::Justification::centred);
        }
        if (! trackPanel.isEmpty())
        {
            g.setColour(juce::Colour(0xffff8a22));
            g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
            g.drawText("TRACKS", trackPanel.reduced(16, 4).removeFromTop(16), juce::Justification::centredLeft);
        }

        for (int i = 0; i < (trackPanel.isEmpty() ? 0 : kEuclidTargets); ++i)
        {
            auto pad = trackPad(i).toFloat().withTrimmedTop(18);
            const bool isDrum = i < groove::kTracks;
            const int laneIndex = isDrum ? -1 : (i - groove::kTracks + 1);
            const bool sel = isDrum ? false : laneIndex == midiLane;
            const bool muted = isDrum ? st.tracks[(size_t) i].muted
                                      : st.midiLanes[(size_t) laneIndex].muted;
            const juce::String name = isDrum ? juce::String(groove::voiceName(i))
                                             : juce::String(groove::midiLaneName(laneIndex));
            const auto c = isDrum ? trackColour(i) : juce::Colour(0xff57d98f);

            g.setColour(sel ? c.withAlpha(muted ? 0.12f : 0.34f)
                            : juce::Colour(muted ? 0xff0a0908 : 0xff12100e));
            g.fillRoundedRectangle(pad, 6.0f);
            g.setColour(sel ? c : juce::Colour(0xff3a3228));
            g.drawRoundedRectangle(pad, 6.0f, sel ? 2.0f : 1.0f);
            g.setColour(muted ? juce::Colour(0xff6a5e52) : (sel ? juce::Colours::white : c));
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText(name, pad.toNearestInt().reduced(5, 1), juce::Justification::centred);
            if (muted)
            {
                g.setColour(juce::Colour(0xffff6363));
                g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
                g.drawText("M", pad.toNearestInt().removeFromRight(16), juce::Justification::centred);
            }
        }
        return;
    }

    const int t = st.selectedTrack;
    const auto& tr = st.tracks[t];
    const int play = engine.currentStepForTrack(t);

    panel(shapePanel, "SHAPE  ·  " + juce::String(groove::voiceName(t)));
    panel(pulsePanel, "RHYTHM  ·  CLICK TO TOGGLE  ·  SHIFT TO SELECT");
    panel(stepPanel, "STEP EDIT  ·  " + juce::String(st.selectedStep + 1));

    g.setColour(juce::Colour(0xff8a7a68));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText("MODE", shapePanel.getX() + 14, shapePanel.getY() + 34, 70, 14, juce::Justification::centredLeft);
    auto shapeLabels = shapePanel.reduced(12, 10);
    shapeLabels.removeFromTop(50); // title + mode combo
    const int shapeGap = 6;
    const int shapeCol = juce::jmax(44, (shapeLabels.getWidth() - shapeGap * 3) / 4);
    const char* shapeNames[] = { "STEPS", "PULSES", "ROTATE", "DIV" };
    for (int i = 0; i < 4; ++i)
        g.drawText(shapeNames[i], shapeLabels.getX() + i * (shapeCol + shapeGap), shapeLabels.getY(),
                   shapeCol, 12, juce::Justification::centred);

    auto labelArea = stepPanel.reduced(12, 8);
    labelArea.removeFromTop(2);
    labelArea.removeFromRight(104);
    const bool showSoundLabels = stepPanel.getHeight() >= 118;
    auto row1 = showSoundLabels ? labelArea.removeFromTop(14) : labelArea.removeFromTop(14);
    const int labelGap = 8;
    const int colW = juce::jmax(72, (row1.getWidth() - labelGap * 3) / 4);
    const char* extra[] = { "VELOCITY", "NOTE", "PROB", "REPEATS" };
    for (int i = 0; i < 4; ++i)
        g.drawText(extra[i], row1.getX() + i * (colW + labelGap), row1.getY(), colW, 12,
                   juce::Justification::centred);
    if (showSoundLabels)
    {
        labelArea.removeFromTop(juce::jlimit(48, 58, (stepPanel.getHeight() - 30) / 2) - 2);
        const int soundW = juce::jmax(52, labelArea.getWidth() / groove::paramCount);
        for (int i = 0; i < groove::paramCount; ++i)
            g.drawText(displayName(parameterOrder[(size_t) i]),
                       labelArea.getX() + i * soundW, labelArea.getY(), soundW, 12,
                       juce::Justification::centred);
    }

    g.setColour(juce::Colour(0xffffc38a));
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    const char* modeName = tr.rhythmMode == groove::RhythmMode::step ? "STEP"
                         : tr.rhythmMode == groove::RhythmMode::euclid ? "EUCLID" : "HYBRID";
    auto rhythmSummary = pulsePanel.reduced(16, 10).removeFromTop(22);
    rhythmSummary.removeFromLeft(360);
    g.drawText(juce::String(modeName) + "  " + juce::String(tr.pulses) + "/"
               + juce::String(tr.generatorSteps) + "  ROT " + juce::String(tr.rotate),
               rhythmSummary, juce::Justification::centredRight);

    for (int step = 0; step < groove::kSteps; ++step)
    {
        auto pad = pulsePad(step).toFloat();
        const auto& ss = tr.steps[step];
        const bool outside = step >= tr.generatorSteps;
        const bool generatorActive = tr.rhythmMode != groove::RhythmMode::step;
        const bool gen = generatorActive && ! outside && engine.isGeneratedHit(t, step);
        const bool resolved = ! outside && engine.isResolvedHit(t, step);
        const bool selected = (step == st.selectedStep);
        const bool playing = (step == play);

        if (outside)
            g.setColour(juce::Colour(0xff0a0c0e));
        else
            g.setColour(juce::Colour(0xff16120e));
        g.fillRoundedRectangle(pad, 5.0f);

        if (groove::meterIsBarLine(st.meter, step))
        {
            g.setColour(juce::Colour(0xffffc38a).withAlpha(0.55f));
            g.drawRoundedRectangle(pad, 5.0f, 1.6f);
        }
        else if (groove::meterIsBeatLine(st.meter, step))
        {
            g.setColour(juce::Colour(0xffff7a18).withAlpha(0.28f));
            g.drawRoundedRectangle(pad, 5.0f, 1.1f);
        }

        if (resolved)
        {
            auto fill = pad.withTrimmedTop(pad.getHeight() * (1.0f - juce::jlimit(0.12f, 1.0f, ss.velocity)));
            g.setColour(juce::Colour(0xffff7a18));
            g.fillRoundedRectangle(fill, 5.0f);
        }
        else if (gen)
        {
            g.setColour(juce::Colour(0xffff7a18).withAlpha(0.22f));
            g.fillRoundedRectangle(pad.reduced(2), 4.0f);
        }

        g.setColour(selected ? juce::Colours::white
                             : playing ? juce::Colour(0xffffd9a8)
                                       : juce::Colour(0xff3a2e22));
        g.drawRoundedRectangle(pad, 5.0f, selected ? 2.2f : 1.0f);

        g.setColour(outside ? juce::Colour(0xff33302c) : juce::Colour(0xfff3e2cc));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(juce::String(step + 1), pad.toNearestInt().reduced(2, 2).removeFromTop(18),
                   juce::Justification::centred);

        // v3.7.7: keep the rhythm grid visually clean.  Step locks, note
        // overrides, ratchets and force-off state remain functional and are
        // edited in the step inspector, but are not drawn as extra glyphs
        // over every rhythm cell.
    }

    if (! trackPanel.isEmpty())
    {
        g.setColour(juce::Colour(0xffff8a22));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText("TRACKS", trackPanel.reduced(16, 4).removeFromTop(16), juce::Justification::centredLeft);
    }

    for (int i = 0; i < (trackPanel.isEmpty() ? 0 : kEuclidTargets); ++i)
    {
        const bool isDrum = i < groove::kTracks;
        const int laneIndex = isDrum ? -1 : (i - groove::kTracks + 1);
        auto pad = trackPad(i).toFloat().withTrimmedTop(18);
        if (isDrum)
            pad = pad.withTrimmedBottom(28);

        const bool sel = (i == st.selectedTarget);
        const bool muted = isDrum ? st.tracks[(size_t) i].muted
                                  : st.midiLanes[(size_t) laneIndex].muted;
        const juce::String name = isDrum ? juce::String(i + 1) + "  " + groove::voiceName(i)
                                         : juce::String(groove::midiLaneName(laneIndex));
        const auto c = isDrum ? trackColour(i) : juce::Colour(0xff57d98f);

        g.setColour(sel ? c.withAlpha(muted ? 0.12f : 0.35f)
                        : juce::Colour(muted ? 0xff0a0908 : 0xff12100e));
        g.fillRoundedRectangle(pad, 6.0f);
        g.setColour(sel ? c : juce::Colour(0xff3a3228));
        g.drawRoundedRectangle(pad, 6.0f, sel ? 2.0f : 1.0f);
        g.setColour(muted ? juce::Colour(0xff6a5e52) : (sel ? juce::Colours::white : c));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(name, pad.toNearestInt().reduced(5, 0).removeFromTop(22), juce::Justification::centredLeft);

        if (isDrum)
        {
            g.setColour(sel ? juce::Colour(0xffffd9a8) : juce::Colour(0xff8a7a68));
            g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
            g.drawText(groove::ujamKitName(engine.effectiveMidiNote(i, st.selectedStep)),
                       pad.toNearestInt().reduced(5, 2).removeFromBottom(14), juce::Justification::centredLeft);
        }
        else
        {
            const auto& lane = st.midiLanes[(size_t) laneIndex];
            g.setColour(juce::Colour(0xff8a7a68));
            g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
            juce::String laneFooter = groove::rhythmModeName(lane.rhythmMode);
            if (lane.rhythmMode == groove::RhythmMode::euclid || lane.rhythmMode == groove::RhythmMode::hybrid)
                laneFooter += " " + juce::String(lane.euclidPulses) + "/" + juce::String(lane.euclidSteps);
            else if (lane.rhythmMode == groove::RhythmMode::arp || lane.rhythmMode == groove::RhythmMode::walk || lane.rhythmMode == groove::RhythmMode::answer)
                laneFooter += " r" + juce::String(lane.generatorRate);
            g.drawText(laneFooter,
                       pad.toNearestInt().reduced(5, 2).removeFromBottom(14), juce::Justification::centredLeft);
        }

        if (muted)
        {
            g.setColour(juce::Colour(0xffff6363));
            g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
            g.drawText("M", pad.toNearestInt().removeFromRight(16), juce::Justification::centred);
        }
    }

    if (midiDragOver)
    {
        g.setColour(juce::Colour(0xffff8a22).withAlpha(0.18f));
        g.fillRect(getLocalBounds());
        g.setColour(juce::Colour(0xffff8a22));
        g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
        g.drawText("DROP UJAM PHRASE TO LOAD GRID", getLocalBounds(), juce::Justification::centred);
    }
}

void TorsoPage::mouseDown(const juce::MouseEvent& e)
{
    // The bottom selector is shared by every Euclidean target.  Select a drum
    // voice or a melodic instrument here; the SAME Steps/Pulses/Rotate controls
    // above immediately edit that target.
    for (int i = 0; i < (trackPanel.isEmpty() ? 0 : kEuclidTargets); ++i)
    {
        auto hit = trackPad(i).withTrimmedTop(18);
        if (! hit.contains(e.getPosition()))
            continue;

        if (i < groove::kTracks)
        {
            if (e.mods.isCommandDown())
                engine.toggleMute(i);
            else if (e.mods.isAltDown())
                engine.toggleSolo(i);
            else
            {
                midiLane = -1;
                rhythmMode.setItemEnabled(3, true);
                rhythmMode.setItemEnabled(4, false);
                rhythmMode.setItemEnabled(5, false);
                engine.selectStep(i, engine.state().selectedStep);
                engine.selectUnifiedTarget(i);
                if (onMidiChannelSelected)
                    onMidiChannelSelected(groove::kMidiChDrums);
            }
        }
        else
        {
            const int laneIndex = i - groove::kTracks + 1;
            if (e.mods.isCommandDown())
                engine.setMidiLaneMuted(laneIndex, ! engine.state().midiLanes[(size_t) laneIndex].muted);
            else
            {
                engine.selectUnifiedTarget(i);
                setMidiLane(laneIndex);
                if (onMidiChannelSelected)
                    onMidiChannelSelected(groove::midiLaneChannel(laneIndex));
            }
        }

        refreshFromEngine();
        resized();
        if (onPatternChanged) onPatternChanged();
        repaint();
        return;
    }

    if (editingMidiLane())
    {
        // The melodic Euclidean page is a gate preview. Notes are edited in
        // the piano roll; this shared page edits the gate shape identically
        // for MOOG / MAXPOLY / KEYS.
        for (int step = 0; step < groove::kSteps; ++step)
        {
            if (pulsePad(step).contains(e.getPosition()))
            {
                const auto mode = engine.state().midiLanes[(size_t) midiLane].rhythmMode;
                engine.state().selectedStep = step;
                // Pure EUCLID: single-click selects; double-click promotes to HYBRID.
                // HYBRID: click toggles a force-on/off gate override. STEP remains note-as-written.
                if (mode == groove::RhythmMode::euclid)
                {
                    if (e.getNumberOfClicks() >= 2)
                        engine.toggleMidiLaneGateOverride(midiLane, step);
                }
                else if (mode == groove::RhythmMode::hybrid)
                {
                    engine.toggleMidiLaneGateOverride(midiLane, step);
                }
                refreshFromEngine();
                if (onPatternChanged) onPatternChanged();
                repaint();
                return;
            }
        }
        return;
    }

    for (int step = 0; step < groove::kSteps; ++step)
    {
        if (! pulsePad(step).contains(e.getPosition()))
            continue;

        const int t = engine.state().selectedTrack;
        const auto& tr = engine.state().tracks[(size_t) t];
        const bool pureEuclid = tr.rhythmMode == groove::RhythmMode::euclid;
        engine.selectStep(t, step);

        // Pure EUCLID remains an editable *generator*: single-click selects a
        // step for velocity/probability/Param Locks without altering the
        // generated rhythm. Double-click explicitly changes placement, which
        // promotes the track to HYBRID in GrooveEngine::toggleStep().
        if (pureEuclid)
        {
            if (e.getNumberOfClicks() >= 2)
                engine.toggleStep(t, step);
        }
        else if (! e.mods.isShiftDown())
        {
            const bool outside = step >= tr.generatorSteps;
            const bool currentlyOn = ! outside && engine.isResolvedHit(t, step);
            engine.setPulseEnabled(t, step, ! currentlyOn);
        }

        engine.auditionSelected();
        refreshFromEngine();
        if (onPatternChanged) onPatternChanged();
        repaint();
        return;
    }

}

bool TorsoPage::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& path : files)
        if (groove::looksLikeMidiFile(juce::File(path)))
            return true;
    return false;
}

void TorsoPage::fileDragEnter(const juce::StringArray&, int, int)
{
    midiDragOver = true;
    repaint();
}

void TorsoPage::fileDragExit(const juce::StringArray&)
{
    midiDragOver = false;
    repaint();
}

void TorsoPage::filesDropped(const juce::StringArray& files, int, int)
{
    midiDragOver = false;
    juce::String error = "No MIDI file in drop";
    for (const auto& path : files)
    {
        const juce::File f(path);
        if (! f.existsAsFile())
            continue;
        if (engine.importMidiFile(f, error))
        {
            refreshFromEngine();
            if (onPatternChanged) onPatternChanged();
            if (onStatusMessage)
                onStatusMessage("Loaded UJAM phrase · " + f.getFileName());
            repaint();
            return;
        }
    }
    if (onStatusMessage)
        onStatusMessage(error);
    repaint();
}
