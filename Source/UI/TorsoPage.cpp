#include "TorsoPage.h"
#include "../Audio/DrumMidi.h"

namespace
{
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

    addAndMakeVisible(steps);
    addAndMakeVisible(pulses);
    addAndMakeVisible(rotate);
    addAndMakeVisible(division);
    addAndMakeVisible(velocity);
    addAndMakeVisible(kitNote);
    addAndMakeVisible(probability);
    addAndMakeVisible(repeats);
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
        if (refreshing) return;
        const float d[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
        engine.setTrackDivision(engine.state().selectedTrack,
                                d[juce::jlimit(1, 5, division.getSelectedId()) - 1]);
        if (onPatternChanged) onPatternChanged();
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
    const int t = engine.state().selectedTrack;
    engine.setTrackSteps(t, (int) steps.getValue());
    engine.setTrackPulses(t, (int) pulses.getValue());
    engine.setTrackRotate(t, (int) rotate.getValue());
    repaint();
    if (onPatternChanged) onPatternChanged();
}

void TorsoPage::refreshFromEngine()
{
    refreshing = true;
    const auto& st = engine.state();
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
    const int gap = 8;
    const int w = (r.getWidth() - 7 * gap) / 8;
    return { r.getX() + index * (w + gap), r.getY(), w, r.getHeight() };
}

void TorsoPage::resized()
{
    auto bounds = getLocalBounds().reduced(16, 12);
    trackPanel = bounds.removeFromBottom(96);
    bounds.removeFromBottom(10);
    stepPanel = bounds.removeFromBottom(168);
    bounds.removeFromBottom(10);

    shapePanel = bounds.removeFromLeft(juce::jmax(220, bounds.getWidth() / 5));
    bounds.removeFromLeft(10);
    pulsePanel = bounds;

    auto s = shapePanel.reduced(16);
    s.removeFromTop(40);
    const int kn = s.getWidth() / 2;
    steps.setBounds(s.getX(), s.getY(), kn, 110);
    pulses.setBounds(s.getX() + kn, s.getY(), kn, 110);
    rotate.setBounds(s.getX(), s.getY() + 118, kn, 110);
    division.setBounds(s.getX() + kn + 8, s.getY() + 140, kn - 16, 28);

    auto sp = stepPanel.reduced(14);
    sp.removeFromTop(38);
    auto buttons = sp.removeFromRight(108);
    playStep.setBounds(buttons.removeFromTop(28));
    buttons.removeFromTop(8);
    clearStep.setBounds(buttons.removeFromTop(28));

    const int n = 4 + groove::paramCount;
    const int colW = juce::jmax(52, sp.getWidth() / n);
    velocity.setBounds(sp.getX(), sp.getY(), colW, sp.getHeight());
    kitNote.setBounds(sp.getX() + colW + 4, sp.getY() + 28, colW - 8, 28);
    probability.setBounds(sp.getX() + 2 * colW, sp.getY(), colW, sp.getHeight());
    repeats.setBounds(sp.getX() + 3 * colW, sp.getY(), colW, sp.getHeight());
    for (int i = 0; i < groove::paramCount; ++i)
        soundSliders[(size_t) i].setBounds(sp.getX() + (4 + i) * colW, sp.getY(), colW, sp.getHeight());

    for (int i = 0; i < groove::kTracks; ++i)
    {
        auto cell = trackPad(i).withTrimmedTop(18);
        trackPlay[(size_t) i].setBounds(cell.removeFromBottom(26).reduced(3, 1));
    }
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
    const int t = st.selectedTrack;
    const auto& tr = st.tracks[t];
    const int play = engine.currentStepForTrack(t);

    panel(shapePanel, "SHAPE");
    panel(pulsePanel, "GRID  ·  CLICK ON / OFF  ·  SHIFT SELECT");
    panel(stepPanel, "STEP " + juce::String(st.selectedStep + 1) + "  ·  " + groove::voiceName(t)
                      + "  ·  NOTE " + groove::ujamKitName(engine.effectiveMidiNote(t, st.selectedStep))
                      + (tr.steps[st.selectedStep].hasAnyLock() ? "  ·  SOUND LOCKED" : ""));
    panel(trackPanel, "");

    g.setColour(juce::Colour(0xff8a7a68));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText("STEPS", shapePanel.getX() + 22, shapePanel.getY() + 36, 70, 14, juce::Justification::centredLeft);
    g.drawText("PULSES", shapePanel.getX() + 22 + shapePanel.getWidth() / 2 - 16,
               shapePanel.getY() + 36, 70, 14, juce::Justification::centredLeft);
    g.drawText("ROTATE", shapePanel.getX() + 22, shapePanel.getY() + 154, 70, 14, juce::Justification::centredLeft);
    g.drawText("DIVISION", shapePanel.getX() + 22 + shapePanel.getWidth() / 2 - 16,
               shapePanel.getY() + 154, 80, 14, juce::Justification::centredLeft);

    auto labelRow = stepPanel.reduced(14).removeFromTop(38);
    labelRow.removeFromLeft(4);
    const int n = 4 + groove::paramCount;
    const int colW = juce::jmax(52, (stepPanel.getWidth() - 14 - 14 - 108) / n);
    const char* extra[] = { "VELOCITY", "NOTE", "PROB", "REPEATS" };
    for (int i = 0; i < 4; ++i)
        g.drawText(extra[i], labelRow.getX() + i * colW, labelRow.getY() + 20, colW, 14,
                   juce::Justification::centred);
    for (int i = 0; i < groove::paramCount; ++i)
        g.drawText(displayName(parameterOrder[(size_t) i]),
                   labelRow.getX() + (4 + i) * colW, labelRow.getY() + 20, colW, 14,
                   juce::Justification::centred);

    g.setColour(juce::Colour(0xffffc38a));
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawText(groove::voiceName(t) + "  ·  " + juce::String(tr.pulses) + "/" + juce::String(tr.generatorSteps)
               + "  rot " + juce::String(tr.rotate),
               pulsePanel.reduced(16, 12).removeFromTop(28),
               juce::Justification::centredLeft);

    for (int step = 0; step < groove::kSteps; ++step)
    {
        auto pad = pulsePad(step).toFloat();
        const auto& ss = tr.steps[step];
        const bool outside = step >= tr.generatorSteps;
        const bool gen = ! outside && engine.isGeneratedHit(t, step);
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

        if (ss.hasAnyLock())
        {
            g.setColour(juce::Colour(0xffffbf4d));
            g.fillEllipse(pad.getRight() - 9.0f, pad.getY() + 4.0f, 5.0f, 5.0f);
        }
        if (ss.midiNote.has_value())
        {
            g.setColour(juce::Colour(0xffffd9a8));
            g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
            g.drawText(groove::ujamKitName(*ss.midiNote), pad.toNearestInt().reduced(2),
                       juce::Justification::bottomRight);
        }
        if (ss.ratchet > 1)
        {
            g.setColour(juce::Colour(0xffe4f6ff));
            g.setFont(juce::FontOptions(8.0f));
            g.drawText(juce::String(ss.ratchet) + "x", pad.toNearestInt().reduced(3),
                       juce::Justification::bottomLeft);
        }
        if (ss.overrideMode == groove::StepOverrideMode::forceOff)
        {
            g.setColour(juce::Colour(0xffff6363));
            g.drawLine(pad.getX() + 6, pad.getY() + 6, pad.getRight() - 6, pad.getBottom() - 6, 1.4f);
        }
    }

    g.setColour(juce::Colour(0xffff8a22));
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText("TRACKS  ·  PLAY SOUND  ·  CMD MUTE  ·  ALT SOLO",
               trackPanel.reduced(16, 4).removeFromTop(16), juce::Justification::centredLeft);

    for (int i = 0; i < groove::kTracks; ++i)
    {
        auto pad = trackPad(i).toFloat().withTrimmedTop(18).withTrimmedBottom(28);
        const auto& trk = st.tracks[i];
        const bool sel = (i == t);
        const bool silent = ! st.trackIsAudible(i);
        auto c = trackColour(i);
        g.setColour(sel ? c.withAlpha(silent ? 0.12f : 0.35f)
                        : juce::Colour(silent ? 0xff0a0908 : 0xff12100e));
        g.fillRoundedRectangle(pad, 6.0f);
        g.setColour(trk.soloed ? juce::Colour(0xffffd76a) : sel ? c : juce::Colour(0xff3a3228));
        g.drawRoundedRectangle(pad, 6.0f, (sel || trk.soloed) ? 2.0f : 1.0f);
        g.setColour(silent ? juce::Colour(0xff6a5e52) : (sel ? juce::Colours::white : c));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(juce::String(i + 1) + "  " + groove::voiceName(i),
                   pad.toNearestInt().reduced(6, 0).removeFromTop(22), juce::Justification::centredLeft);
        g.setColour(sel ? juce::Colour(0xffffd9a8) : juce::Colour(0xff8a7a68));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(groove::ujamKitName(engine.effectiveMidiNote(i, st.selectedStep)),
                   pad.toNearestInt().reduced(6, 2).removeFromBottom(16), juce::Justification::centredLeft);
        if (trk.muted)
        {
            g.setColour(juce::Colour(0xffff6363));
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            g.drawText("M", pad.toNearestInt().removeFromRight(20), juce::Justification::centred);
        }
        else if (trk.soloed)
        {
            g.setColour(juce::Colour(0xffffd76a));
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            g.drawText("S", pad.toNearestInt().removeFromRight(20), juce::Justification::centred);
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
    for (int step = 0; step < groove::kSteps; ++step)
    {
        if (! pulsePad(step).contains(e.getPosition()))
            continue;

        const int t = engine.state().selectedTrack;
        const auto& tr = engine.state().tracks[t];
        const bool outside = step >= tr.generatorSteps;
        const bool currentlyOn = ! outside && engine.isResolvedHit(t, step);
        engine.selectStep(t, step);
        if (! e.mods.isShiftDown())
            engine.setPulseEnabled(t, step, ! currentlyOn);
        engine.auditionSelected();
        refreshFromEngine();
        if (onPatternChanged) onPatternChanged();
        repaint();
        return;
    }

    for (int i = 0; i < groove::kTracks; ++i)
    {
        auto nameHit = trackPad(i).withTrimmedTop(18);
        nameHit.removeFromBottom(28);
        if (! nameHit.contains(e.getPosition()))
            continue;

        if (e.mods.isCommandDown())
            engine.toggleMute(i);
        else if (e.mods.isAltDown())
            engine.toggleSolo(i);
        else
            playTrack(i);
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
