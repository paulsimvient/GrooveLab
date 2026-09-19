#include "MainComponent.h"
#include "../Audio/DrumMidi.h"
#include <BinaryData.h>
#include <initializer_list>

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
        case groove::Param::pitch: return "BODY";
        case groove::Param::decay: return "DECAY";
        case groove::Param::transient: return "TRANSIENT";
        case groove::Param::noise: return "TEXTURE";
        case groove::Param::filter: return "FILTER";
        case groove::Param::drive: return "DRIVE";
        case groove::Param::space: return "SPACE";
        case groove::Param::blend: return "BLEND";
        default: return "?";
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

class AudioPreferencesPanel : public juce::Component
{
public:
    AudioPreferencesPanel(juce::AudioDeviceManager& dm, std::function<void()> saveDefaults)
        : selector(dm, 0, 0, 2, 8, false, false, true, true), saveInstrumentDefaults(std::move(saveDefaults))
    {
        heading.setText("AUDIO DEVICE", juce::dontSendNotification);
        heading.setJustificationType(juce::Justification::centredLeft);
        heading.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        heading.setColour(juce::Label::textColourId, juce::Colour(0xffff8a22));
        hint.setText("Output device, sample rate, and buffer size. Saved automatically.",
                     juce::dontSendNotification);
        hint.setColour(juce::Label::textColourId, juce::Colour(0xff8aa0ae));

        addAndMakeVisible(heading);
        addAndMakeVisible(hint);
        addAndMakeVisible(selector);
        setSize(620, 460);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(16);
        heading.setBounds(r.removeFromTop(24));
        hint.setBounds(r.removeFromTop(22));
        r.removeFromTop(8);
        selector.setBounds(r);
    }

private:
    juce::Label heading, hint, instrumentHeading, instrumentHint;
    juce::TextButton saveDefaultsButton;
    juce::AudioDeviceSelectorComponent selector;
    std::function<void()> saveInstrumentDefaults;
};
}


class MixerWindow : public juce::DocumentWindow
{
public:
    class MixerContent : public juce::Component
    {
    public:
        MixerContent(MixStrip& stripIn, SynthKeyboard& keyboardIn)
            : strip(stripIn), keyboard(keyboardIn)
        {
            addAndMakeVisible(strip);
            addAndMakeVisible(keyboard);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            const int kb = juce::jlimit(74, 104, r.getHeight() / 4);
            keyboard.setBounds(r.removeFromBottom(kb).reduced(4, 3));
            strip.setBounds(r.reduced(2));
        }

    private:
        MixStrip& strip;
        SynthKeyboard& keyboard;
    };

    MixerWindow(MixStrip& stripIn, SynthKeyboard& keyboardIn,
                std::function<void()> onCloseFn, std::function<void()> onMoveFn)
        : juce::DocumentWindow("MIXER", juce::Colour(0xff07131c),
                               juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
          strip(stripIn), keyboard(keyboardIn), content(stripIn, keyboardIn),
          onClose(std::move(onCloseFn)), onMove(std::move(onMoveFn))
    {
        setUsingNativeTitleBar(true);
        setResizable(true, false);
        setResizeLimits(680, 300, 1800, 820);
        strip.setVisible(true);
        keyboard.setVisible(true);
        setContentNonOwned(&content, false);
        setAlwaysOnTop(false);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
        if (onClose) onClose();
    }

    void moved() override
    {
        juce::DocumentWindow::moved();
        if (onMove) onMove();
    }

    void resized() override
    {
        juce::DocumentWindow::resized();
        strip.setVisible(true);
        keyboard.setVisible(true);
        content.resized();
        strip.repaint();
        keyboard.repaint();
        if (onMove) onMove();
    }

private:
    MixStrip& strip;
    SynthKeyboard& keyboard;
    MixerContent content;
    std::function<void()> onClose, onMove;
};
MainComponent::MainComponent()
{
    addAndMakeVisible(pianoRoll);
    pianoRoll.setVisible(false);

    addAndMakeVisible(instrumentDock);
    instrumentDock.setSelectedTarget(engine.state().selectedTarget);
    instrumentDock.onTargetSelected = [this](int target)
    {
        engine.selectUnifiedTarget(target);
        if (groove::unifiedTrackIsDrum(target))
        {
            engine.selectStep(target, engine.state().selectedStep);
            selectMidiChannelFromUi(groove::kMidiChDrums);
            torsoPage.setMidiLane(-1);
            pianoRoll.setLane(0);
        }
        else
        {
            const int lane = groove::unifiedTrackMidiLane(target);
            selectMidiChannelFromUi(groove::midiLaneChannel(lane));
            torsoPage.setMidiLane(lane);
            pianoRoll.setLane(lane);
        }
        torsoPage.refreshFromEngine();
        songPage.refreshFromEngine();
        refreshContextInspector();
        resized();
        repaint();
    };
    instrumentDock.onOpenInstrumentUi = [this](int ch) { showInstrumentEditor(ch); };
    instrumentDock.onFloatingChanged = [this](bool floating)
    {
        // Floating only within the application hierarchy; never system-wide always-on-top.
        instrumentDock.setAlwaysOnTop(false);
        if (floating)
            instrumentDock.setBounds(juce::jmax(12, getWidth() - 960), 52, juce::jmin(930, getWidth() - 24), instrumentDockHeight());
        resized();
        repaint();
    };

    setLookAndFeel(&look);
    setOpaque(true);
    setWantsKeyboardFocus(true);
    setFocusContainerType(juce::Component::FocusContainerType::focusContainer);

    projectName.setText(engine.state().name);
    projectName.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    projectName.setJustification(juce::Justification::centredLeft);
    projectName.setIndents(8, 0);
    projectName.setBorder({ 1, 1, 1, 1 });
    projectName.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0c1822));
    projectName.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff213848));
    projectName.onReturnKey = [this]
    {
        engine.state().name = projectName.getText().trim();
        saveCurrentGroove();
    };
    projectName.onFocusLost = [this]
    {
        engine.state().name = projectName.getText().trim();
        engine.saveAutosave();
    };
    addAndMakeVisible(projectName);

    addSmallLabel(contextTitle, "CONTEXT");
    contextTitle.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    contextTitle.setColour(juce::Label::textColourId, juce::Colour(0xffd8eaf4));
    addSmallLabel(contextDetail, "");
    addSmallLabel(contextHint, "");
    contextHint.setColour(juce::Label::textColourId, juce::Colour(0xff738b99));
    addAndMakeVisible(contextPrimary);
    addAndMakeVisible(contextSecondary);
    contextPrimary.onClick = [this]
    {
        const int target = engine.state().selectedTarget;
        if (groove::unifiedTrackIsDrum(target)) engine.toggleMute(target);
        else
        {
            const int lane = groove::unifiedTrackMidiLane(target);
            engine.setMidiLaneMuted(lane, ! engine.state().midiLanes[(size_t) lane].muted);
        }
        refreshContextInspector();
        repaint();
    };
    contextSecondary.onClick = [this]
    {
        if (! groove::unifiedTrackIsDrum(engine.state().selectedTarget) && currentPage == 1)
            pianoRoll.fitToNotes();
        else
            showInstrumentEditor(selectedTargetChannel());
    };

    addSmallLabel(bpmLabel, "BPM");
    bpm.setRange(40.0, 260.0, 1.0);
    bpm.setSliderStyle(juce::Slider::LinearHorizontal);
    bpm.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 38, 20);
    bpm.setSliderSnapsToMousePosition(true);
    bpm.setMouseDragSensitivity(180);
    bpm.onValueChange=[this]{ if(!refreshing){ engine.state().bpm=bpm.getValue(); mixStrip.setBpm(engine.state().bpm); engine.saveAutosave(); }};
    addAndMakeVisible(bpm);
    tapTempoButton.setTooltip("Tap tempo · press T");
    tapTempoButton.setWantsKeyboardFocus(false);
    tapTempoButton.onClick = [this] { tapTempo(); };
    addAndMakeVisible(tapTempoButton);
    addSmallLabel(meterLabel, "METER");
    for (int i = 0; i < groove::kMeterCount; ++i)
        meterBox.addItem(groove::meterName((groove::Meter) i), i + 1);
    meterBox.onChange = [this]
    {
        if (refreshing) return;
        engine.setMeter((groove::Meter) (meterBox.getSelectedId() - 1));
        songPage.refreshFromEngine();
        torsoPage.refreshFromEngine();
        refreshFromSelection();
        repaint();
    };
    addAndMakeVisible(meterBox);
    for (int i = 0; i < groove::kMeterTransformCount; ++i)
        meterTransformBox.addItem(groove::meterTransformName((groove::MeterTransform) i), i + 1);
    meterTransformBox.setTooltip("CROP drops extra beats. REFLOW keeps the groove and moves bar lines. SQUEEZE fits the old bar into the new one.");
    meterTransformBox.onChange = [this]
    {
        if (refreshing) return;
        engine.setMeterTransform((groove::MeterTransform) (meterTransformBox.getSelectedId() - 1));
        refreshFromSelection();
        repaint();
    };
    addAndMakeVisible(meterTransformBox);

    for (auto* b : {&playButton,&recordButton,&resetButton,&auditionButton,&performButton,&commitPerformButton,
                    &clearLocks,&deleteNote,&sparse,&syncopate,&human,&dense,&soundEvolve,
                    &pageT1Button,&pageSongButton,&fileMenuButton,&labMenuButton,&mixWindowButton,&keyboardButton,&densityButton})
        addAndMakeVisible(*b);
#if GROOVELAB_LABS_ENSEMBLE
    // v3.6: BEAT/PERFORM is no longer a separate SEQ subview. Keep the legacy
    // components compiled for compatibility, but never expose them in the UI.
    addChildComponent(seqEditButton);
    addChildComponent(seqPerformButton);
#endif

    pageT1Button.setClickingTogglesState(true);
    pageSongButton.setClickingTogglesState(true);
    pageT1Button.setRadioGroupId(42);
    pageSongButton.setRadioGroupId(42);
    pageT1Button.setToggleState(true, juce::dontSendNotification);
    pageT1Button.onClick = [this] { setPage(1); };
    pageSongButton.onClick = [this] { setPage(2); };
#if GROOVELAB_LABS_ENSEMBLE
    seqEditButton.setClickingTogglesState(true);
    seqPerformButton.setClickingTogglesState(true);
    seqEditButton.setRadioGroupId(43);
    seqPerformButton.setRadioGroupId(43);
    seqEditButton.setToggleState(true, juce::dontSendNotification);
    seqEditButton.onClick = [this] { setSeqMode(0); };
    seqPerformButton.onClick = [this] { setSeqMode(1); };
    seqEditButton.setTooltip("SEQ edit: notes + rhythm");
    seqPerformButton.setTooltip("SEQ perform: live beat/performance workspace");
#endif
    fileMenuButton.setTooltip("Save / Load");
    labMenuButton.setTooltip("Capture / Evolve");
    fileMenuButton.onClick = [this] { showFileMenu(); };
    labMenuButton.onClick = [this] { showLabMenu(); };
    mixWindowButton.setTooltip("Show / hide floating mixer");
    mixWindowButton.setClickingTogglesState(true);
    mixWindowButton.onClick = [this]
    {
        const bool visible = mixerWindow != nullptr && mixerWindow->isVisible();
        if (visible)
        {
            saveMixerWindowBounds();
            mixerWindow->setVisible(false);
            mixWindowButton.setToggleState(false, juce::dontSendNotification);
        }
        else
        {
            showMixerWindow(true);
        }
    };
    keyboardButton.setTooltip("Show / hide on-screen keyboard");
    keyboardButton.setClickingTogglesState(true);
    keyboardButton.setToggleState(true, juce::dontSendNotification);
    keyboardButton.onClick = [this] { setKeyboardVisible(keyboardButton.getToggleState()); };
    densityButton.setTooltip("UI density: compact / normal / large");
    densityButton.onClick = [this] { cycleUiDensity(); };

    saveButton.onClick = [this] { saveCurrentGroove(); };
    saveAsButton.onClick = [this] { saveGrooveAs(); };
    loadButton.onClick = [this] { showLoadMenu(); };

    engine.setInternalSynthEnabled(false);
    soundMode.store(2);
    refreshMidiOutputs();
    pluginHost.setEditorIdentity("DRUMS  ·  UJAM", { 40, 70 });
    pluginHost.setPluginMidiChannel(1);
    synthHost.setEditorIdentity("MOOG  ·  Mini-Moog", { 90, 110 });
    synthHost.setPluginMidiChannel(1);
    keysHost.setEditorIdentity("KEYS  ·  Electra 88", { 140, 150 });
    keysHost.setPluginMidiChannel(1);
    polymaxHost.setEditorIdentity("MAXPOLY  ·  PolyMAX", { 190, 190 });
    polymaxHost.setPluginMidiChannel(1);

    addAndMakeVisible(torsoPage);
    torsoPage.setVisible(false);
    torsoPage.onPatternChanged = [this]
    {
        // One source of truth for melodic mode: the lane state drives the
        // generator panel, piano-roll title/ghost rendering, playback and footer.
        refreshFromSelection();
        if (! groove::unifiedTrackIsDrum(engine.state().selectedTarget))
        {
            const int lane = groove::unifiedTrackMidiLane(engine.state().selectedTarget);
            pianoRoll.setLane(lane);
            pianoRoll.refresh();
        }
        songPage.refreshFromEngine();
        refreshContextInspector();
        repaint();
    };
    torsoPage.onStatusMessage = [this](const juce::String& text)
    {
        evolutionStatus.setText(text, juce::dontSendNotification);
    };
    torsoPage.onMidiChannelSelected = [this](int ch)
    {
        // Selecting MOOG / PROPHET / KEYS inside EUC also selects that
        // instrument everywhere else, while keeping the shared EUC page open.
        selectMidiChannelFromUi(ch);
        torsoPage.setMidiLane(groove::midiLaneIndexForChannel(ch));
        torsoPage.refreshFromEngine();
    };

    addAndMakeVisible(songPage);
    songPage.setVisible(false);
    songPage.onChannelClicked = [this](int ch) { selectMidiChannelFromUi(ch); };
    songPage.onInstrumentUiClicked = [this](int ch) { showInstrumentEditor(ch); };
    songPage.onStatusMessage = [this](const juce::String& text)
    {
        evolutionStatus.setText(text, juce::dontSendNotification);
    };
    songPage.onSongChanged = [this]
    {
        refreshFromSelection();
        torsoPage.refreshFromEngine();
    };
#if GROOVELAB_LABS_ENSEMBLE
    addAndMakeVisible(ensembleView);
    ensembleView.setVisible(false);
#endif

    synthKeyboard.onNoteOn = [this](int note, float velocity)
    {
        // The on-screen keyboard should always be playable. If MOOG is the
        // selected target and the hosted instrument has not been restored yet,
        // load it before enqueueing the first note.
        if (keyboardMidiChannel() == groove::kMidiChMoog && ! synthHost.isLoaded())
            tryLoadMiniMoog();
        auto msg = juce::MidiMessage::noteOn(keyboardMidiChannel(), note, velocity);
        if (engine.isRecording() || engine.isPerformanceTapOn())
            engine.pushIncomingMidi(msg);
        midiCollector.addMessageToQueue(msg);
    };
    synthKeyboard.onNoteOff = [this](int note)
    {
        auto msg = juce::MidiMessage::noteOff(keyboardMidiChannel(), note);
        if (engine.isRecording() || engine.isPerformanceTapOn())
            engine.pushIncomingMidi(msg);
        midiCollector.addMessageToQueue(msg);
    };
    synthKeyboard.onOctaveChanged = [this]
    {
        engine.state().lastSynthOctave = synthKeyboard.getOctaveOffset();
        engine.saveAutosave();
    };
    synthKeyboard.onTargetChanged = [this]
    {
        engine.state().lastKeyboardTarget = synthKeyboard.getTarget();
        applyLiveMidiChannel(keyboardMidiChannel());
        engine.saveAutosave();
    };
    synthKeyboard.onUserPickedTarget = [this]
    {
        selectMidiChannelFromUi(keyboardMidiChannel());
    };
    addAndMakeVisible(synthKeyboard);

    // The mixer has its own keyboard instance so the floating mixer can keep a
    // playable keyboard at its bottom without moving the main-window keyboard.
    mixerKeyboard.onNoteOn = [this](int note, float velocity)
    {
        if (keyboardMidiChannel() == groove::kMidiChMoog && ! synthHost.isLoaded())
            tryLoadMiniMoog();
        auto msg = juce::MidiMessage::noteOn(keyboardMidiChannel(), note, velocity);
        if (engine.isRecording() || engine.isPerformanceTapOn())
            engine.pushIncomingMidi(msg);
        midiCollector.addMessageToQueue(msg);
    };
    mixerKeyboard.onNoteOff = [this](int note)
    {
        auto msg = juce::MidiMessage::noteOff(keyboardMidiChannel(), note);
        if (engine.isRecording() || engine.isPerformanceTapOn())
            engine.pushIncomingMidi(msg);
        midiCollector.addMessageToQueue(msg);
    };
    mixerKeyboard.onOctaveChanged = [this]
    {
        synthKeyboard.setOctaveOffset(mixerKeyboard.getOctaveOffset());
        engine.state().lastSynthOctave = mixerKeyboard.getOctaveOffset();
        engine.saveAutosave();
    };
    mixerKeyboard.onTargetChanged = [this]
    {
        synthKeyboard.setTarget(mixerKeyboard.getTarget());
        engine.state().lastKeyboardTarget = mixerKeyboard.getTarget();
        applyLiveMidiChannel(keyboardMidiChannel());
        engine.saveAutosave();
    };
    mixerKeyboard.onUserPickedTarget = [this]
    {
        synthKeyboard.setTarget(mixerKeyboard.getTarget());
        selectMidiChannelFromUi(keyboardMidiChannel());
    };
    mixerKeyboard.setOctaveOffset(synthKeyboard.getOctaveOffset());
    mixerKeyboard.setTarget(synthKeyboard.getTarget());
    applyLiveMidiChannel(keyboardMidiChannel());

    mixStrip.onChannelClicked = [this](int ch) { selectMidiChannelFromUi(ch); };
    mixStrip.onOpenInstrumentUi = [this](int ch) { showInstrumentEditor(ch); };
    mixStrip.onCompactModeChanged = [this](bool compact)
    {
        if (mixerWindow == nullptr) return;
        saveMixerWindowBoundsForMode(! compact);
        restoreMixerWindowBoundsForMode(compact);
        saveMixerWindowBoundsForMode(compact);
    };
    mixStrip.onChanged = [this]
    {
        mixStrip.saveTo(engine.state().mix);
        pushMixToDsp();
        const auto& fx = engine.state().mix.sharedFx;

        if (paradiseGuitarHost.isLoaded())
        {
            paradiseGuitarHost.setParameterByName({ "input", "in" }, fx.paradiseInput);
            paradiseGuitarHost.setParameterByName({ "gate", "threshold" }, fx.paradiseGate);
            paradiseGuitarHost.setParameterByName({ "amp", "amp level" }, fx.paradiseAmp);
            paradiseGuitarHost.setParameterByName({ "room" }, fx.paradiseRoom);
            paradiseGuitarHost.setParameterByName({ "mix", "wet", "blend" }, 1.0f);
            paradiseGuitarHost.setParameterByName({ "output", "out", "volume", "vol" }, fx.paradiseOutput);
        }
        if (centuryTubeHost.isLoaded())
        {
            centuryTubeHost.setParameterByName({ "preamp", "input", "gain" }, fx.centuryPreamp);
            centuryTubeHost.setParameterByName({ "low" }, fx.centuryLow);
            centuryTubeHost.setParameterByName({ "mid" }, fx.centuryMid);
            centuryTubeHost.setParameterByName({ "mid freq", "mid frequency" }, fx.centuryMidFreq);
            centuryTubeHost.setParameterByName({ "high", "treble" }, fx.centuryHigh);
            centuryTubeHost.setParameterByName({ "compress", "leveler", "comp" }, fx.centuryComp);
            centuryTubeHost.setParameterByName({ "output", "out", "volume" }, fx.centuryOutput);
        }
        if (galaxyTapeHost.isLoaded())
        {
            galaxyTapeHost.setParameterByName({ "delay time", "delay" }, fx.galaxyDelay);
            galaxyTapeHost.setParameterByName({ "feedback", "repeat" }, fx.galaxyFeedback);
            galaxyTapeHost.setParameterByName({ "echo volume", "echo" }, fx.galaxyEcho);
            galaxyTapeHost.setParameterByName({ "reverb volume", "reverb" }, fx.galaxyReverb);
            galaxyTapeHost.setParameterByName({ "treble" }, fx.galaxyTreble);
            galaxyTapeHost.setParameterByName({ "bass" }, fx.galaxyBass);
            galaxyTapeHost.setParameterByName({ "input", "drive" }, fx.galaxyInput);
        }
        if (capitolReverbHost.isLoaded())
        {
            capitolReverbHost.setParameterByName({ "chamber", "size" }, fx.capitolSize);
            capitolReverbHost.setParameterByName({ "decay", "time" }, fx.capitolDecay);
            capitolReverbHost.setParameterByName({ "wet solo", "wetsolo" }, 1.0f);
            capitolReverbHost.setParameterByName({ "mix", "wet" }, 1.0f);
            capitolReverbHost.setParameterByName({ "pre delay", "predelay", "pre-delay" }, fx.capitolPreDelay);
            capitolReverbHost.setParameterByName({ "width", "stereo" }, fx.capitolWidth);
            capitolReverbHost.setParameterByName({ "bass" }, fx.capitolBass);
            capitolReverbHost.setParameterByName({ "mid" }, fx.capitolMid);
            capitolReverbHost.setParameterByName({ "treble", "high" }, fx.capitolTreble);
            capitolReverbHost.setParameterByName({ "level", "volume", "output", "out" }, fx.capitolVolume);
        }
        engine.saveAutosave();
    };
    // The mixer lives in its own persistent modeless window and shares live state
    // with every page. Start in full detail and make it available immediately.
    mixStrip.setCompactMode(false);
    mixWindowButton.setToggleState(true, juce::dontSendNotification);

    playButton.onClick=[this]{ toggleTransport(); };
    recordButton.setClickingTogglesState(true);
    recordButton.setTooltip("Record the currently selected instrument from any screen · shortcut R");
    recordButton.onClick = [this]
    {
        const bool on = recordButton.getToggleState();
        engine.setRecording(on);
        if (on && ! engine.isPlaying())
            engine.togglePlaying();
        playButton.setButtonText(engine.isPlaying() ? "STOP" : "PLAY");
        torsoPage.refreshFromEngine();
        songPage.refreshFromEngine();
        repaint();
    };
    resetButton.onClick=[this]{ engine.resetTransport(); torsoPage.refreshFromEngine(); songPage.refreshFromEngine(); refreshFromSelection(); repaint(); };
    auditionButton.onClick=[this]{ engine.auditionSelected(); };
    captureButton.onClick=[this]{
        evolutionStatus.setText("Captured node "+juce::String(engine.capture("manual")),juce::dontSendNotification);
        if (evolutionWindow != nullptr) evolutionWindow->lab.repaint();
        repaint();
    };
    backButton.onClick=[this]{
        auto ok=engine.back();
        evolutionStatus.setText(ok?"Returned to parent":"No parent",juce::dontSendNotification);
        refreshFromSelection();
        if (evolutionWindow != nullptr) evolutionWindow->lab.repaint();
        repaint();
    };
    evolveButton.onClick=[this]{ toggleEvolutionWindow(); };

    performButton.setClickingTogglesState(true);
    performButton.onClick=[this]
    {
        if (performButton.getToggleState())
        {
            engine.beginPerform();
            evolutionStatus.setText("PERFORM layer active",juce::dontSendNotification);
        }
        else
        {
            engine.endPerform(false);
            evolutionStatus.setText("PERFORM reverted",juce::dontSendNotification);
            refreshFromSelection();
        }
        repaint();
    };
    commitPerformButton.onClick=[this]
    {
        if (engine.isPerforming())
        {
            engine.endPerform(true);
            performButton.setToggleState(false,juce::dontSendNotification);
            evolutionStatus.setText("Temporary performance captured",juce::dontSendNotification);
            refreshFromSelection(); repaint();
        }
    };

    addSmallLabel(soundScopeLabel, "SOUND EDIT");
    soundScope.addItem("STEP", 1);
    soundScope.addItem("VOICE", 2);
    soundScope.setSelectedId(1, juce::dontSendNotification); // per-step editing by default
    soundScope.onChange = [this]
    {
        if (!refreshing)
        {
            evolutionStatus.setText(soundScope.getSelectedId() == 1
                ? "Sound knobs edit selected STEP"
                : "Sound knobs edit base VOICE",
                                juce::dontSendNotification);
        refreshFromSelection();
        repaint();
        }
    };
    addAndMakeVisible(soundScope);

    mixStrip.drumSound.onChange = [this]
    {
        if (refreshing) return;
        const int id = mixStrip.drumSound.getSelectedId();
        if (id <= 0) return;
        pluginHost.setKitIndex(id - 1);
        engine.state().lastPluginProgram = id - 1;
        engine.state().lastPluginPatch = pluginHost.getCurrentPatchName();
        auto status = pluginHost.getKitName(id - 1);
        const auto style = pluginHost.getStyleName();
        if (style.isNotEmpty())
            status += "  ·  " + style;
        evolutionStatus.setText("DRUMS · " + status, juce::dontSendNotification);
    };
    mixStrip.drumPrev.onClick = [this]
    {
        pluginHost.stepKit(-1);
        engine.state().lastPluginProgram = pluginHost.getKitIndex();
        engine.state().lastPluginPatch = pluginHost.getCurrentPatchName();
        refreshVstKitUi(false);
        auto status = pluginHost.getKitName(pluginHost.getKitIndex());
        const auto style = pluginHost.getStyleName();
        if (style.isNotEmpty())
            status += "  ·  " + style;
        evolutionStatus.setText("DRUMS · " + status, juce::dontSendNotification);
    };
    mixStrip.drumNext.onClick = [this]
    {
        pluginHost.stepKit(1);
        engine.state().lastPluginProgram = pluginHost.getKitIndex();
        engine.state().lastPluginPatch = pluginHost.getCurrentPatchName();
        refreshVstKitUi(false);
        auto status = pluginHost.getKitName(pluginHost.getKitIndex());
        const auto style = pluginHost.getStyleName();
        if (style.isNotEmpty())
            status += "  ·  " + style;
        evolutionStatus.setText("DRUMS · " + status, juce::dontSendNotification);
    };
    mixStrip.synthSound.onChange = [this]
    {
        if (refreshing) return;
        const int id = mixStrip.synthSound.getSelectedId();
        if (id <= 0) return;
        synthHost.setKitIndex(id - 1);
        engine.state().lastSynthPatch = synthHost.getCurrentPatchName();
        if (engine.state().lastSynthPatch.isEmpty())
            engine.state().lastSynthPatch = synthHost.getKitName(id - 1);
        evolutionStatus.setText("SYNTH · " + synthHost.getKitName(id - 1), juce::dontSendNotification);
        engine.saveAutosave();
    };
    mixStrip.synthPrev.onClick = [this]
    {
        synthHost.stepKit(-1);
        engine.state().lastSynthPatch = synthHost.getCurrentPatchName();
        if (engine.state().lastSynthPatch.isEmpty())
            engine.state().lastSynthPatch = synthHost.getKitName(synthHost.getKitIndex());
        refreshSynthKitUi(false);
        evolutionStatus.setText("SYNTH · " + synthHost.getKitName(synthHost.getKitIndex()),
                                juce::dontSendNotification);
        engine.saveAutosave();
    };
    mixStrip.drumUi.onClick = [this]
    {
        if (pluginHost.isLoaded())
            pluginHost.showEditor();
        else
            evolutionStatus.setText("DRUMS · load a kit first (Sound menu)", juce::dontSendNotification);
    };
    mixStrip.synthUi.onClick = [this]
    {
        if (synthHost.isLoaded())
            synthHost.showEditor();
        else
            tryLoadMiniMoog();
    };
    mixStrip.synthNext.onClick = [this]
    {
        synthHost.stepKit(1);
        engine.state().lastSynthPatch = synthHost.getCurrentPatchName();
        if (engine.state().lastSynthPatch.isEmpty())
            engine.state().lastSynthPatch = synthHost.getKitName(synthHost.getKitIndex());
        refreshSynthKitUi(false);
        evolutionStatus.setText("SYNTH · " + synthHost.getKitName(synthHost.getKitIndex()),
                                juce::dontSendNotification);
        engine.saveAutosave();
    };
    mixStrip.keysSound.onChange = [this]
    {
        if (refreshing) return;
        const int id = mixStrip.keysSound.getSelectedId();
        if (id <= 0) return;
        keysHost.setKitIndex(id - 1);
        storeCurrentKeysPatch();
        evolutionStatus.setText("KEYS · " + keysHost.getKitName(id - 1), juce::dontSendNotification);
        engine.saveAutosave();
    };
    mixStrip.keysPrev.onClick = [this]
    {
        keysHost.stepKit(-1);
        storeCurrentKeysPatch();
        refreshKeysKitUi(false);
        evolutionStatus.setText("KEYS · " + keysHost.getKitName(keysHost.getKitIndex()),
                                juce::dontSendNotification);
        engine.saveAutosave();
    };
    mixStrip.keysNext.onClick = [this]
    {
        keysHost.stepKit(1);
        storeCurrentKeysPatch();
        refreshKeysKitUi(false);
        evolutionStatus.setText("KEYS · " + keysHost.getKitName(keysHost.getKitIndex()),
                                juce::dontSendNotification);
        engine.saveAutosave();
    };
    mixStrip.keysUi.onClick = [this]
    {
        if (keysHost.isLoaded())
            keysHost.showEditor();
        else
            tryLoadKeys();
    };
    mixStrip.polySound.onClick = [this] { showProphetBrowser(); };
    mixStrip.polyPrev.onClick = [this]
    {
        polymaxHost.stepKit(-1);
        storeCurrentPolyPatch();
        refreshPolyKitUi(false);
        evolutionStatus.setText("MAXPOLY · " + polymaxHost.getKitName(polymaxHost.getKitIndex()),
                                juce::dontSendNotification);
        engine.saveAutosave();
    };
    mixStrip.polyNext.onClick = [this]
    {
        polymaxHost.stepKit(1);
        storeCurrentPolyPatch();
        refreshPolyKitUi(false);
        evolutionStatus.setText("MAXPOLY · " + polymaxHost.getKitName(polymaxHost.getKitIndex()),
                                juce::dontSendNotification);
        engine.saveAutosave();
    };
    mixStrip.polyUi.onClick = [this]
    {
        if (polymaxHost.isLoaded())
            polymaxHost.showEditor();
        else
            tryLoadPolymax();
    };
    mixStrip.polyBrowse.onClick = [this] { showProphetBrowser(); };

    clearLocks.onClick=[this]
    {
        engine.clearAllLocks(engine.state().selectedTrack,engine.state().selectedStep);
        evolutionStatus.setText("Step sound unlocked", juce::dontSendNotification);
        refreshFromSelection();
        repaint();
    };
    deleteNote.onClick=[this]
    {
        engine.deleteNote(engine.state().selectedTrack, engine.state().selectedStep);
        evolutionStatus.setText("Note deleted  ·  " + groove::voiceName(engine.state().selectedTrack)
                                    + " step " + juce::String(engine.state().selectedStep + 1),
                                juce::dontSendNotification);
        refreshFromSelection();
        songPage.refreshFromEngine();
        torsoPage.refreshFromEngine();
        repaint();
    };
    for (int i = 0; i < groove::paramCount; ++i)
    {
        auto p = parameterOrder[(size_t) i];
        auto& b = lockChips[(size_t) i];
        b.setButtonText(displayName(p));
        b.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd8e8f1));
        b.onClick = [this, p]
        {
            if (refreshing) return;
            engine.toggleParamLock(engine.state().selectedTrack, engine.state().selectedStep, p);
            evolutionStatus.setText(juce::String(displayName(p)) + " lock toggled",
                                    juce::dontSendNotification);
            refreshFromSelection();
            torsoPage.refreshFromEngine();
            repaint();
        };
        addAndMakeVisible(b);
    }
    addSmallLabel(selectedLabel,"SELECTED"); selectedLabel.setFont(juce::FontOptions(13.0f,juce::Font::bold));
    addSmallLabel(evolutionStatus,"READY");
    addSmallLabel(footer,"SAVE stores this groove  ·  SAVE AS makes a named copy  ·  ⌘S save  ·  ⌘⇧S save as  ·  ⌘O load");

    for(int i=0;i<groove::paramCount;++i)
    {
        auto p=parameterOrder[(size_t)i]; addSmallLabel(soundLabels[(size_t)i],displayName(p));
        auto& sl=soundSliders[(size_t)i];
        if(p==groove::Param::pitch) configureRotary(sl,30,1600,1);
        else if(p==groove::Param::decay) configureRotary(sl,20,1800,1);
        else configureRotary(sl,0,1,0.01);
        bindSoundSlider(sl,p);
        sl.setVisible(false);
        soundLabels[(size_t)i].setVisible(false);
    }

    configureRotary(velocity,0,1,0.01); groove::configureMidiNoteSlider(midiNote); configureRotary(probability,0,1,0.01);
    addAndMakeVisible(velocity); addAndMakeVisible(midiNote); addAndMakeVisible(probability);
    velocity.onValueChange=[this]{ if(!refreshing) engine.setVelocity(engine.state().selectedTrack,engine.state().selectedStep,(float)velocity.getValue()); };
    midiNote.onValueChange=[this]{ if(!refreshing){ engine.setTrackMidiNote(engine.state().selectedTrack,(int)midiNote.getValue()); engine.setStepMidiNote(engine.state().selectedTrack,engine.state().selectedStep,(int)midiNote.getValue()); refreshFromSelection(); }};
    probability.onValueChange=[this]{ if(!refreshing) engine.setProbability(engine.state().selectedTrack,engine.state().selectedStep,(float)probability.getValue()); };

    for(int i=1;i<=4;++i) ratchet.addItem(juce::String(i)+"x",i);
    ratchet.onChange=[this]{ if(!refreshing) engine.setRatchet(engine.state().selectedTrack,engine.state().selectedStep,ratchet.getSelectedId()); }; addAndMakeVisible(ratchet);
    role.addItem("NORMAL",1); role.addItem("ANCHOR",2); role.addItem("GHOST",3); role.addItem("FILL",4);
    role.onChange=[this]{ if(!refreshing) engine.setStepRole(engine.state().selectedTrack,engine.state().selectedStep,(groove::StepRole)(role.getSelectedId()-1)); }; addAndMakeVisible(role);

    for(int i=1;i<=groove::kSteps;++i){ trackSteps.addItem(juce::String(i),i); trackPulses.addItem(juce::String(i-1),i); trackRotate.addItem(juce::String(i-1),i); }
    trackPulses.addItem(juce::String(groove::kSteps),groove::kSteps+1);
    trackSteps.onChange=[this]{ if(!refreshing){ engine.setTrackSteps(engine.state().selectedTrack,trackSteps.getSelectedId()); refreshFromSelection(); repaint(); }};
    trackPulses.onChange=[this]{ if(!refreshing){ engine.setTrackPulses(engine.state().selectedTrack,trackPulses.getSelectedId()-1); repaint(); }};
    trackRotate.onChange=[this]{ if(!refreshing){ engine.setTrackRotate(engine.state().selectedTrack,trackRotate.getSelectedId()-1); repaint(); }};
    for(auto* c:{&trackSteps,&trackPulses,&trackRotate}) addAndMakeVisible(*c);

    trackDivision.addItem("1/4x",1); trackDivision.addItem("1/2x",2); trackDivision.addItem("1x",3); trackDivision.addItem("2x",4); trackDivision.addItem("4x",5);
    trackDivision.onChange=[this]{ if(!refreshing){ const float d[]={0.25f,0.5f,1,2,4}; engine.setTrackDivision(engine.state().selectedTrack,d[juce::jlimit(1,5,trackDivision.getSelectedId())-1]); }}; addAndMakeVisible(trackDivision);

    configureLinear(trackProbability,0,1,0.01); configureLinear(trackVelocity,0,1.2,0.01); addAndMakeVisible(trackProbability); addAndMakeVisible(trackVelocity);
    trackProbability.onValueChange=[this]{ if(!refreshing) engine.setTrackProbability(engine.state().selectedTrack,(float)trackProbability.getValue()); };
    trackVelocity.onValueChange=[this]{ if(!refreshing) engine.setTrackVelocity(engine.state().selectedTrack,(float)trackVelocity.getValue()); };

    evolvePolicy.addItem("PROTECT",1); evolvePolicy.addItem("ANCHORS ONLY",2); evolvePolicy.addItem("FREE",3);
    evolvePolicy.onChange=[this]{ if(!refreshing) engine.setTrackEvolutionPolicy(engine.state().selectedTrack,(groove::EvolutionPolicy)(evolvePolicy.getSelectedId()-1)); }; addAndMakeVisible(evolvePolicy);
    addSmallLabel(policyLabel, "POLICY");
    addSmallLabel(trackProbLabel, "TRACK PROB");
    addSmallLabel(trackVelLabel, "TRACK VEL");
    policyLabel.setBorderSize({});
    trackProbLabel.setBorderSize({});
    trackVelLabel.setBorderSize({});
    policyLabel.setInterceptsMouseClicks(false, false);
    trackProbLabel.setInterceptsMouseClicks(false, false);
    trackVelLabel.setInterceptsMouseClicks(false, false);
    configureLinear(evolveAmount,0,1,0.01); configureLinear(similarity,0,1,0.01); configureLinear(lockResistance,0,1,0.01);
    addAndMakeVisible(evolveAmount); addAndMakeVisible(similarity); addAndMakeVisible(lockResistance);
    evolveAmount.onValueChange=[this]{ if(!refreshing) engine.setTrackEvolveAmount(engine.state().selectedTrack,(float)evolveAmount.getValue()); };
    similarity.onValueChange=[this]{ if(!refreshing){ engine.state().similarity=(float)similarity.getValue(); engine.saveAutosave(); }};
    lockResistance.onValueChange=[this]{ if(!refreshing){ engine.state().lockResistance=(float)lockResistance.getValue(); engine.saveAutosave(); }};
    for(int i=1;i<=8;++i) surprise.addItem(juce::String(i),i);
    surprise.onChange=[this]{ if(!refreshing){ engine.state().surpriseBudget=surprise.getSelectedId(); engine.saveAutosave(); }}; addAndMakeVisible(surprise);

    sparse.onClick=[this]{ runEvolution(groove::EvolutionEngine::Mode::sparse); };
    syncopate.onClick=[this]{ runEvolution(groove::EvolutionEngine::Mode::syncopate); };
    human.onClick=[this]{ runEvolution(groove::EvolutionEngine::Mode::human); };
    dense.onClick=[this]{ runEvolution(groove::EvolutionEngine::Mode::dense); };
    soundEvolve.onClick=[this]{ runEvolution(groove::EvolutionEngine::Mode::sound); };

    // Listen for SPACE even when a child control currently owns keyboard focus.
    for (auto* child : getChildren())
        child->addKeyListener(this);
    torsoPage.addKeyListener(this);
    songPage.addKeyListener(this);

    auto settingsXml = juce::XmlDocument::parse(audioSettingsFile());
    const bool hadBuffer = settingsXml != nullptr
        && (settingsXml->hasAttribute("bufferSize")
            || settingsXml->hasAttribute("audioDeviceBufferSize"));
    setAudioChannels(0, 2, settingsXml.get());
    if (! hadBuffer)
        preferLowLatencyBuffer();
    deviceManager.addChangeListener(this);
    if (settingsXml != nullptr)
    {
        midiInIdentifier = settingsXml->getStringAttribute("midiInputIdentifier");
        midiInAuto = settingsXml->getBoolAttribute("midiInputAuto", midiInIdentifier.isEmpty());
    }
    refreshMidiInputs();
    if (midiInAuto || midiInIdentifier.isEmpty())
        autoSelectMidiInput();
    else
    {
        int found = -1;
        for (int i = 0; i < midiInDevices.size(); ++i)
            if (midiInDevices[i].identifier == midiInIdentifier)
            {
                found = i;
                break;
            }
        if (found >= 0)
            setMidiInput(found);
        else
            autoSelectMidiInput();
    }
    loadInstrumentDefaultsIntoState();
    applyLoadedSession();
    tryLoadCapitolChambers();
    tryLoadParadiseGuitarStudio();
    tryLoadCenturyTube();
    tryLoadGalaxyTapeEcho();
    mixStrip.addKeyListener(this);
    mixStrip.loadFrom(engine.state().mix);
    if (mixStrip.onChanged) mixStrip.onChanged(); // sync shared plug-in macros and disable legacy inserts
    pushMixToDsp();

    evolutionWindow = std::make_unique<EvolutionWindow>(engine, look);
    evolutionWindow->lab.onLayout = [this] { layoutEvolutionLab(); };
    for (auto* c : std::initializer_list<juce::Component*>{
            &similarity, &surprise, &lockResistance, &evolveAmount,
            &sparse, &syncopate, &human, &dense, &soundEvolve, &evolutionStatus,
            &evolvePolicy, &policyLabel, &trackProbLabel, &trackVelLabel,
            &trackProbability, &trackVelocity})
    {
        evolutionWindow->lab.addAndMakeVisible(c);
        c->addKeyListener(this);
    }

    {
        juce::PropertiesFile prefs(instrumentPreferenceOptions());
        uiDensity = juce::jlimit(0, 2, prefs.getIntValue("uiDensity", 0));
        keyboardVisible = prefs.getBoolValue("keyboardVisible", true);
    }
    applyUiDensity();
    keyboardButton.setToggleState(keyboardVisible, juce::dontSendNotification);
    setSize(1360, 820); refreshFromSelection(); refreshContextInspector(); refreshVstKitUi(true); refreshSynthKitUi(true); refreshKeysKitUi(true); refreshPolyKitUi(true);
    showMixerWindow(true);
    startTimerHz(30);
    playButton.setButtonText(engine.isPlaying()?"STOP":"PLAY");
#if JUCE_MAC
    extraAppleMenu.addItem(200, "Preferences...");
    juce::MenuBarModel::setMacMainMenu(this, &extraAppleMenu);
#endif
}

MainComponent::~MainComponent()
{
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(nullptr);
#endif
    deviceManager.removeChangeListener(this);
    midiInput.reset();
    if (midiOutput != nullptr)
        midiOutput->stopBackgroundThread();
    midiOutput.reset();
    synthKeyboard.allNotesOff();
    saveAudioSettings();
    shutdownAudio();
    pluginHost.hideEditor();
    synthHost.hideEditor();
    keysHost.hideEditor();
    polymaxHost.hideEditor();
    evolutionWindow.reset();
    prophetBrowser.reset();
    instrumentBrowser.reset();
    saveMixerWindowBounds();
    mixerWindow.reset();
    setLookAndFeel(nullptr);
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    midiCollector.reset(sampleRate);
    engine.prepare(sampleRate, samplesPerBlockExpected);
    pluginHost.prepare(sampleRate, samplesPerBlockExpected);
    synthHost.prepare(sampleRate, samplesPerBlockExpected);
    keysHost.prepare(sampleRate, samplesPerBlockExpected);
    polymaxHost.prepare(sampleRate, samplesPerBlockExpected);
    capitolReverbHost.prepare(sampleRate, samplesPerBlockExpected);
    paradiseGuitarHost.prepare(sampleRate, samplesPerBlockExpected);
    centuryTubeHost.prepare(sampleRate, samplesPerBlockExpected);
    galaxyTapeHost.prepare(sampleRate, samplesPerBlockExpected);
    juce::dsp::ProcessSpec fxSpec { sampleRate, (juce::uint32) juce::jmax(1, samplesPerBlockExpected), 2 };
    fxBusCompressor.prepare(fxSpec);
    mixBus.prepare(sampleRate, samplesPerBlockExpected);
    const int n = juce::jmax(samplesPerBlockExpected, 512);
    drumStem.setSize(2, n, false, false, true);
    synthStem.setSize(2, n, false, false, true);
    keysStem.setSize(2, n, false, false, true);
    polyStem.setSize(2, n, false, false, true);
    fxSendStem.setSize(2, n, false, false, true);
    fxReturnStem.setSize(2, n, false, false, true);
    for (auto* midi : { &engineMidiScratch, &drumMidiScratch, &synthMidiScratch, &liveMidiScratch,
                        &keysMidiScratch, &polyMidiScratch, &laneMidiScratch, &hardwareMidiScratch })
        midi->ensureSize(32768);
}

void MainComponent::releaseResources()
{
    pluginHost.release();
    synthHost.release();
    keysHost.release();
    polymaxHost.release();
    capitolReverbHost.release();
    paradiseGuitarHost.release();
    centuryTubeHost.release();
    galaxyTapeHost.release();
    fxBusCompressor.reset();
    mixBus.reset();
}

void MainComponent::pushMixToDsp()
{
    const auto& m = engine.state().mix;
    mixBus.drumVol.store(m.drumVol);
    mixBus.drumLeft.store(m.drumLeft);
    mixBus.drumRight.store(m.drumRight);
    mixBus.synthVol.store(m.synthVol);
    mixBus.synthLeft.store(m.synthLeft);
    mixBus.synthRight.store(m.synthRight);
    mixBus.keysVol.store(m.keysVol);
    mixBus.keysLeft.store(m.keysLeft);
    mixBus.keysRight.store(m.keysRight);
    mixBus.polyVol.store(m.polyVol);
    mixBus.polyLeft.store(m.polyLeft);
    mixBus.polyRight.store(m.polyRight);
    mixBus.busComp.store(0.0f); // compressor now lives on the shared FX return
    mixBus.masterVol.store(m.masterVol);
    mixBus.pushChannelFx(m);
    mixStrip.setBpm(engine.state().bpm);
    mixStrip.setSelectedStep(engine.state().selectedStep);
    for (int i = 0; i < groove::kEqBands; ++i)
        mixBus.eqGainDb[(size_t) i].store(m.eqGainDb[(size_t) i]);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& info)
{
    auto view = juce::AudioBuffer<float>(info.buffer->getArrayOfWritePointers(),
                                         info.buffer->getNumChannels(),
                                         info.startSample,
                                         info.numSamples);
    const int n = info.numSamples;
    // All stem storage is allocated in prepareToPlay(). Never resize in the audio callback.
    if (n > drumStem.getNumSamples() || n > synthStem.getNumSamples()
        || n > keysStem.getNumSamples() || n > polyStem.getNumSamples() || n > fxSendStem.getNumSamples()
        || n > fxReturnStem.getNumSamples())
    {
        view.clear();
        return;
    }

    juce::AudioBuffer<float> drums(drumStem.getArrayOfWritePointers(), 2, n);
    juce::AudioBuffer<float> synth(synthStem.getArrayOfWritePointers(), 2, n);
    juce::AudioBuffer<float> keys(keysStem.getArrayOfWritePointers(), 2, n);
    juce::AudioBuffer<float> poly(polyStem.getArrayOfWritePointers(), 2, n);
    juce::AudioBuffer<float> fxSend(fxSendStem.getArrayOfWritePointers(), 2, n);
    juce::AudioBuffer<float> fxReturn(fxReturnStem.getArrayOfWritePointers(), 2, n);
    drums.clear();
    synth.clear();
    keys.clear();
    poly.clear();

    auto& midi = engineMidiScratch;
    auto& drumMidi = drumMidiScratch;
    auto& synthMidi = synthMidiScratch;
    auto& liveMidi = liveMidiScratch;
    auto& keysMidi = keysMidiScratch;
    auto& polyMidi = polyMidiScratch;
    auto& laneMidi = laneMidiScratch;
    midi.clear(); drumMidi.clear(); synthMidi.clear(); liveMidi.clear();
    keysMidi.clear(); polyMidi.clear(); laneMidi.clear();

    engine.process(drums, midi);
    splitDrumAndSynthMidi(midi, drumMidi, synthMidi);
    midiCollector.removeNextBlockOfMessages(liveMidi, n);
    engine.takeLaneMidi(laneMidi);
    // Live hardware/onscreen keyboard always follows the selected instrument slot.
    // Song/lane MIDI keeps its recorded channel routing.
    routeExternalMidiToSelected(liveMidi, drumMidi, synthMidi, keysMidi, polyMidi);
    routeChannelMidi(laneMidi, drumMidi, synthMidi, keysMidi, polyMidi);

    // Hardware MIDI OUT is explicit and sequencer/song-only. Never echo live
    // controller input back to the same hardware device: that can create MIDI loops.
    if (midiOutput != nullptr)
    {
        auto& hardwareOut = hardwareMidiScratch;
        hardwareOut.clear();
        hardwareOut.addEvents(midi, 0, n, 0);
        const double sr = deviceManager.getCurrentAudioDevice() != nullptr
            ? deviceManager.getCurrentAudioDevice()->getCurrentSampleRate() : 44100.0;
        midiOutput->sendBlockOfMessages(hardwareOut, juce::Time::getMillisecondCounterHiRes(), sr);
    }

    const int mode = soundMode.load();
    if (mode >= 2 && pluginHost.isLoaded())
        pluginHost.process(drums, drumMidi, mode == 2);
    if (synthHost.isLoaded())
        synthHost.process(synth, synthMidi, true);
    if (keysHost.isLoaded())
        keysHost.process(keys, keysMidi, true);
    if (polymaxHost.isLoaded())
        polymaxHost.process(poly, polyMidi, true);

    mixBus.setPlayStep(engine.currentStep());
    mixBus.setBpm(engine.state().bpm);

    const auto& mixState = engine.state().mix;
    groove::MixBus::applyStemGain(drums, mixBus.drumVol.load(), mixBus.drumLeft.load(), mixBus.drumRight.load());
    groove::MixBus::applyStemGain(synth, mixBus.synthVol.load(), mixBus.synthLeft.load(), mixBus.synthRight.load());
    groove::MixBus::applyStemGain(poly, mixBus.polyVol.load(), mixBus.polyLeft.load(), mixBus.polyRight.load());
    groove::MixBus::applyStemGain(keys, mixBus.keysVol.load(), mixBus.keysLeft.load(), mixBus.keysRight.load());

    // Five parallel aux buses. Every instrument has an independent send amount
    // to each shared effect instance; dry stems remain untouched.
    fxReturn.clear();
    const juce::AudioBuffer<float>* stems[] = { &drums, &synth, &poly, &keys };
    auto buildAux = [&](const std::array<bool, groove::kMixChannels>& on,
                        const std::array<float, groove::kMixChannels>& amount)
    {
        fxSend.clear();
        for (int c = 0; c < groove::kMixChannels; ++c)
        {
            if (!on[(size_t)c]) continue;
            const float send = juce::jlimit(0.0f, 1.0f, amount[(size_t)c]);
            if (send <= 0.0001f) continue;
            for (int ch = 0; ch < 2; ++ch)
                fxSend.addFrom(ch, 0, *stems[c], juce::jmin(ch, stems[c]->getNumChannels()-1), 0, n, send);
        }
    };
    auto addAuxReturn = [&]
    {
        for (int ch=0; ch<2; ++ch) fxReturn.addFrom(ch, 0, fxSend, ch, 0, n);
    };

    auto& sfx = mixState.sharedFx;
    if (sfx.paradiseOn && paradiseGuitarHost.isLoaded())
    {
        buildAux(sfx.paradiseSendOn, sfx.paradiseSend);
        paradiseGuitarHost.processEffect(fxSend, 1.0f); addAuxReturn();
    }
    if (sfx.centuryOn && centuryTubeHost.isLoaded())
    {
        buildAux(sfx.centurySendOn, sfx.centurySend);
        centuryTubeHost.processEffect(fxSend, 1.0f); addAuxReturn();
    }
    if (sfx.compressorOn)
    {
        buildAux(sfx.compressorSendOn, sfx.compressorSend);
        fxBusCompressor.setThreshold(juce::jmap(sfx.compThreshold, 0.0f, 1.0f, 0.0f, -48.0f));
        fxBusCompressor.setRatio(juce::jmap(sfx.compRatio, 0.0f, 1.0f, 1.0f, 20.0f));
        fxBusCompressor.setAttack(juce::jmap(sfx.compAttack, 0.0f, 1.0f, 0.2f, 100.0f));
        fxBusCompressor.setRelease(juce::jmap(sfx.compRelease, 0.0f, 1.0f, 20.0f, 800.0f));
        juce::dsp::AudioBlock<float> b(fxSend); juce::dsp::ProcessContextReplacing<float> ctx(b); fxBusCompressor.process(ctx);
        fxSend.applyGain(juce::Decibels::decibelsToGain(juce::jmap(sfx.compMakeup, 0.0f, 1.0f, -6.0f, 12.0f))); addAuxReturn();
    }
    if (sfx.galaxyOn && galaxyTapeHost.isLoaded())
    {
        buildAux(sfx.galaxySendOn, sfx.galaxySend);
        galaxyTapeHost.processEffect(fxSend, 1.0f); addAuxReturn();
    }
    if (sfx.capitolOn && capitolReverbHost.isLoaded())
    {
        buildAux(sfx.capitolSendOn, sfx.capitolSend);
        capitolReverbHost.processEffect(fxSend, 1.0f); addAuxReturn();
    }
    fxReturn.applyGain(juce::jlimit(0.0f, 1.5f, sfx.returnLevel));

    view.clear();
    const int outCh = view.getNumChannels();
    for (int ch = 0; ch < outCh; ++ch)
    {
        view.addFrom(ch, 0, drums, juce::jmin(ch, 1), 0, n);
        view.addFrom(ch, 0, synth, juce::jmin(ch, 1), 0, n);
        view.addFrom(ch, 0, keys, juce::jmin(ch, 1), 0, n);
        view.addFrom(ch, 0, poly, juce::jmin(ch, 1), 0, n);
        view.addFrom(ch, 0, fxReturn, juce::jmin(ch, 1), 0, n);
    }

    mixBus.setBpm(engine.state().bpm);
    mixBus.process(view);
}

void MainComponent::setSoundMode(int mode)
{
    const int id = juce::jlimit(1, 3, mode);
    soundMode.store(id);
    engine.setInternalSynthEnabled(id != 2);
    engine.state().soundMode = id;
}

void MainComponent::setMidiOutput(int deviceIndex)
{
    if (midiOutput != nullptr)
        midiOutput->stopBackgroundThread();
    midiOutput.reset();
    midiOutIndex = -1;
    if (deviceIndex < 0 || deviceIndex >= midiDevices.size())
        return;
    midiOutIndex = deviceIndex;
    midiOutput = juce::MidiOutput::openDevice(midiDevices[deviceIndex].identifier);
    if (midiOutput != nullptr)
        midiOutput->startBackgroundThread();
}

bool MainComponent::isVirtualMidiName(const juce::String& name)
{
    const auto n = name.toLowerCase();
    return n.contains("iac") || n.contains("network") || n.contains("session")
        || n.contains("groove") || n.contains("lil god")
        || n.contains("virtual") || n.contains("loopback");
}

bool MainComponent::looksLikeKeyboardMidi(const juce::String& name)
{
    const auto n = name.toLowerCase();
    return n.contains("arturia") || n.contains("keylab") || n.contains("minilab")
        || n.contains("keystep") || n.contains("launchkey") || n.contains("komplete")
        || n.contains("oxygen") || n.contains("axiom") || n.contains("mpk")
        || n.contains("mikro") || n.contains("lumi") || n.contains("keyboard")
        || n.contains("keytar") || n.contains("piano") || n.contains("stage");
}

void MainComponent::refreshMidiInputs()
{
    const auto previousId = midiInIdentifier;
    midiInDevices = juce::MidiInput::getAvailableDevices();
    if (previousId.isEmpty())
        return;
    midiInIndex = -1;
    for (int i = 0; i < midiInDevices.size(); ++i)
        if (midiInDevices[i].identifier == previousId)
        {
            midiInIndex = i;
            return;
        }
    if (midiInput != nullptr)
    {
        midiInput.reset();
        midiInIndex = -1;
    }
}

void MainComponent::autoSelectMidiInput()
{
    midiInAuto = true;
    refreshMidiInputs();
    int arturia = -1, keyboard = -1, hardware = -1;
    for (int i = 0; i < midiInDevices.size(); ++i)
    {
        const auto& name = midiInDevices[i].name;
        if (isVirtualMidiName(name))
            continue;
        if (hardware < 0)
            hardware = i;
        if (looksLikeKeyboardMidi(name) && keyboard < 0)
            keyboard = i;
        if (name.containsIgnoreCase("arturia") && arturia < 0)
            arturia = i;
    }
    const int pick = arturia >= 0 ? arturia : (keyboard >= 0 ? keyboard : hardware);
    if (pick < 0)
    {
        setMidiInput(-1);
        midiInAuto = true;
        return;
    }
    setMidiInput(pick);
    midiInAuto = true;
}

void MainComponent::setMidiInput(int deviceIndex)
{
    midiInput.reset();
    midiInIndex = -1;
    midiInIdentifier.clear();
    if (deviceIndex < 0 || deviceIndex >= midiInDevices.size())
    {
        evolutionStatus.setText("MIDI IN · Off", juce::dontSendNotification);
        return;
    }
    midiInIndex = deviceIndex;
    midiInIdentifier = midiInDevices[deviceIndex].identifier;
    midiInput = juce::MidiInput::openDevice(midiInDevices[deviceIndex].identifier, this);
    if (midiInput != nullptr)
        midiInput->start();
    evolutionStatus.setText("MIDI IN · " + midiInDevices[deviceIndex].name
                                + "  ·  LIVE → selected instrument",
                            juce::dontSendNotification);
}

void MainComponent::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
{
    // Preserve the controller's note number, velocity and controller data exactly.
    // Only the destination channel is changed so the currently selected instrument
    // owns the live keyboard. This prevents CH1 controllers from implicitly becoming
    // "drums" and, critically, never substitutes a fixed kick note.
    const int sourceChannel = message.getChannel();
    juce::MidiMessage routed = message;
    if (sourceChannel > 0)
        routed = groove::withMidiChannel(routed, liveMidiChannel.load());

    if (engine.isRecording() || engine.isPerformanceTapOn())
        engine.pushIncomingMidi(routed);
    midiCollector.addMessageToQueue(routed);

    const int destChannel = liveMidiChannel.load();
    const bool noteMsg = message.isNoteOnOrOff();
    const int note = noteMsg ? message.getNoteNumber() : -1;
    const int velocity = noteMsg ? (int) message.getVelocity() : 0;
    const bool down = noteMsg && message.isNoteOn() && message.getVelocity() > 0;

    juce::Component::SafePointer<SynthKeyboard> kb(&synthKeyboard);
    juce::Component::SafePointer<MainComponent> self(this);
    juce::MessageManager::callAsync([kb, self, note, velocity, down, destChannel, noteMsg]
    {
        if (kb != nullptr && noteMsg)
            kb->setExternalHeld(note, down);
        if (self == nullptr || ! noteMsg || ! down)
            return;

        juce::String dest = "CH" + juce::String(destChannel);
        if (destChannel == groove::kMidiChDrums) dest = "DRUMS";
        else if (destChannel == groove::kMidiChMoog) dest = "MOOG";
        else if (destChannel == groove::kMidiChPoly) dest = "MAXPOLY";
        else if (destChannel == groove::kMidiChKeys) dest = "KEYS";

        const auto noteName = juce::MidiMessage::getMidiNoteName(note, true, true, 3);
        self->evolutionStatus.setText("MIDI IN · " + noteName
                                        + " · " + juce::String(note)
                                        + " · VEL " + juce::String(velocity)
                                        + " → " + dest,
                                      juce::dontSendNotification);
    });
}
juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Sound", "MIDI" };
}

juce::PopupMenu MainComponent::getMenuForIndex(int topLevelIndex, const juce::String&)
{
    juce::PopupMenu menu;
    if (topLevelIndex == 0)
    {
        menu.addItem(230, "New Groove");
        menu.addItem(231, "Open...");
        menu.addSeparator();
        menu.addItem(232, "Save");
        menu.addItem(233, "Save As...");
#if ! JUCE_MAC
        menu.addSeparator();
        menu.addItem(200, "Preferences...");
#endif
        return menu;
    }

    if (topLevelIndex == 1)
    {
        const int mode = soundMode.load();
        menu.addItem(210, "Internal", true, mode == 1);
        menu.addItem(211, "UJAM / VST", true, mode == 2);
        menu.addItem(212, "Both", true, mode == 3);
        menu.addSeparator();

        juce::PopupMenu plugins;
        ujamPluginFiles.clear();
        const auto vst3Dir = juce::File("/Library/Audio/Plug-Ins/VST3");
        const auto auDir = juce::File("/Library/Audio/Plug-Ins/Components");
        auto addUjam = [&](const juce::String& stem, const juce::String& label)
        {
            const auto vst3 = vst3Dir.getChildFile(stem + ".vst3");
            const auto au = auDir.getChildFile(stem + ".component");
            const auto file = vst3.exists() ? vst3 : au;
            if (! file.exists())
                return;
            ujamPluginFiles.add(file);
            const bool current = pluginHost.isLoaded() && pluginHost.getFile() == file;
            plugins.addItem(ujamPluginFiles.size(), label, true, current);
        };
        addUjam("VD-BRUTE",    "Virtual Drummer Brute");
        addUjam("VD-DEEP",     "Virtual Drummer Deep");
        addUjam("VD-HEAVY",    "Virtual Drummer Heavy");
        addUjam("VD-HOT",      "Virtual Drummer Hot");
        addUjam("VD-LEGEND",   "Virtual Drummer Legend");
        addUjam("VD-SOLID",    "Virtual Drummer Solid");
        addUjam("BM-CIRCUITS", "Beatmaker Circuits");
        if (ujamPluginFiles.isEmpty())
            plugins.addItem(999, "(No UJAM plugins found)", false, false);
        plugins.addSeparator();
        menu.addSubMenu("Load UJAM / VST", plugins);

        const bool loaded = pluginHost.isLoaded();
        menu.addItem(220, loaded ? ("Show " + pluginHost.getName() + " UI") : "Show Plugin UI",
                     loaded, pluginHost.isEditorOpen());
        menu.addSeparator();
        const bool synthLoaded = synthHost.isLoaded();
        menu.addItem(221, synthLoaded ? ("Show " + synthHost.getName() + " UI") : "Show Mini-Moog UI",
                     synthLoaded, synthHost.isEditorOpen());
        menu.addItem(222, "Load Mini-Moog");
        menu.addSeparator();
        const bool keysLoaded = keysHost.isLoaded();
        menu.addItem(223, keysLoaded ? ("Show " + keysHost.getName() + " UI") : "Show Keys UI",
                     keysLoaded, keysHost.isEditorOpen());
        menu.addItem(224, "Load Electra 88", true, keysLoaded);
        const bool polyLoaded = polymaxHost.isLoaded();
        menu.addItem(225, polyLoaded ? ("Show " + polymaxHost.getName() + " UI") : "Show MAXPOLY UI",
                     polyLoaded, polymaxHost.isEditorOpen());
        menu.addItem(227, "Load MAXPOLY", true, polyLoaded);
        menu.addItem(228, "Browse MAXPOLY patches...", polyLoaded);
        menu.addSeparator();
        menu.addItem(226, "Show All Instrument UIs");
        return menu;
    }

    refreshMidiOutputs();
    juce::PopupMenu outputs;
    outputs.addItem(300, "Off", true, midiOutIndex < 0);
    for (int i = 0; i < midiDevices.size(); ++i)
        outputs.addItem(301 + i, midiDevices[i].name, true, midiOutIndex == i);
    menu.addSubMenu("Output", outputs);

    refreshMidiInputs();
    juce::PopupMenu inputs;
    inputs.addItem(399, "Auto (Arturia / keyboard)", true, midiInAuto);
    inputs.addItem(400, "Off", true, ! midiInAuto && midiInIndex < 0);
    for (int i = 0; i < midiInDevices.size(); ++i)
        inputs.addItem(401 + i, midiInDevices[i].name, true,
                       ! midiInAuto && midiInIndex == i);
    menu.addSubMenu("Input", inputs);
    return menu;
}

void MainComponent::menuItemSelected(int menuItemID, int)
{
    if (menuItemID == 200) { showPreferences(); return; }
    if (menuItemID == 210) { setSoundMode(1); return; }
    if (menuItemID == 211)
    {
        setSoundMode(2);
        if (! pluginHost.isLoaded())
            evolutionStatus.setText("Sound · Load UJAM / VST to host Hot",
                                    juce::dontSendNotification);
        return;
    }
    if (menuItemID == 212) { setSoundMode(3); return; }
    if (menuItemID == 220)
    {
        if (pluginHost.isLoaded())
            pluginHost.showEditor();
        return;
    }
    if (menuItemID == 221)
    {
        if (synthHost.isLoaded())
            synthHost.showEditor();
        return;
    }
    if (menuItemID == 222) { tryLoadMiniMoog(); return; }
    if (menuItemID == 223)
    {
        if (keysHost.isLoaded())
            keysHost.showEditor();
        else
            tryLoadKeys();
        return;
    }
    if (menuItemID == 224) { tryLoadKeys(); return; }
    if (menuItemID == 225)
    {
        if (polymaxHost.isLoaded())
            polymaxHost.showEditor();
        else
            tryLoadPolymax();
        return;
    }
    if (menuItemID == 226) { showAllInstrumentEditors(); return; }
    if (menuItemID == 227) { tryLoadPolymax(); return; }
    if (menuItemID == 228) { showProphetBrowser(); return; }
    if (menuItemID == 230) { newGroove(); return; }
    if (menuItemID == 231) { showLoadMenu(); return; }
    if (menuItemID == 232) { saveCurrentGroove(); return; }
    if (menuItemID == 233) { saveGrooveAs(); return; }
    if (menuItemID == 300) { setMidiOutput(-1); return; }
    if (menuItemID >= 301 && menuItemID - 301 < midiDevices.size())
    {
        setMidiOutput(menuItemID - 301);
        return;
    }
    if (menuItemID == 399) { autoSelectMidiInput(); saveAudioSettings(); return; }
    if (menuItemID == 400) { midiInAuto = false; setMidiInput(-1); saveAudioSettings(); return; }
    if (menuItemID >= 401 && menuItemID - 401 < midiInDevices.size())
    {
        midiInAuto = false;
        setMidiInput(menuItemID - 401);
        saveAudioSettings();
        return;
    }
    if (menuItemID == 1000) { browseForPlugin(); return; }
    if (menuItemID >= 1 && menuItemID <= ujamPluginFiles.size())
        loadPluginFromFile(ujamPluginFiles[menuItemID - 1]);
}

void MainComponent::tryLoadUjamHot()
{
    const auto vst3 = juce::File("/Library/Audio/Plug-Ins/VST3/VD-HOT.vst3");
    const auto au = juce::File("/Library/Audio/Plug-Ins/Components/VD-HOT.component");
    const auto file = vst3.exists() ? vst3 : au;
    if (! file.exists())
    {
        evolutionStatus.setText("UJAM Hot not found — Sound menu · Load UJAM / VST", juce::dontSendNotification);
        return;
    }
    if (pluginHost.isLoaded() && pluginHost.getFile() == file)
        return;

    juce::String error;
    if (pluginHost.loadFromFile(file, error))
    {
        setSoundMode(2);
        engine.state().lastPluginPath = file.getFullPathName();
        engine.saveAutosave();
        pluginHost.prepare(deviceManager.getCurrentAudioDevice() != nullptr
                               ? deviceManager.getCurrentAudioDevice()->getCurrentSampleRate()
                               : 44100.0,
                           deviceManager.getCurrentAudioDevice() != nullptr
                               ? deviceManager.getCurrentAudioDevice()->getCurrentBufferSizeSamples()
                               : 512);
        evolutionStatus.setText("UJAM · Virtual Drummer Hot", juce::dontSendNotification);
        pluginHost.showEditor();
        applyStoredPluginKit();
        refreshVstKitUi(true);
    }
    else
    {
        evolutionStatus.setText("UJAM Hot failed: " + error, juce::dontSendNotification);
    }
}

void MainComponent::browseForPlugin()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Audio Unit or VST3",
        juce::File("/Library/Audio/Plug-Ins/Components"),
        juce::String(),
        true);

    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::canSelectDirectories;

    fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file.exists())
            loadPluginFromFile(file);
    });
}

void MainComponent::loadPluginFromFile(const juce::File& file)
{
    juce::String error;
    if (pluginHost.loadFromFile(file, error))
    {
        setSoundMode(2);
        engine.state().lastPluginPath = file.getFullPathName();
        engine.saveAutosave();
        pluginHost.prepare(deviceManager.getCurrentAudioDevice() != nullptr
                               ? deviceManager.getCurrentAudioDevice()->getCurrentSampleRate()
                               : 44100.0,
                           deviceManager.getCurrentAudioDevice() != nullptr
                               ? deviceManager.getCurrentAudioDevice()->getCurrentBufferSizeSamples()
                               : 512);
        evolutionStatus.setText("PLUGIN · " + pluginHost.getName(), juce::dontSendNotification);
        pluginHost.showEditor();
        refreshVstKitUi(true);
    }
    else
    {
        setSoundMode(1);
        evolutionStatus.setText("Plugin load failed: " + error + " — using INTERNAL",
                            juce::dontSendNotification);
        refreshVstKitUi(true);
    }
}

juce::File MainComponent::findCapitolChambersFile()
{
    // Capitol Chambers exists as both legacy UAD/UAD-2 DSP and native UADx.
    // Lil God Projector must host the native UADx plug-in only.
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    const juce::File roots[] = {
        juce::File("/Library/Audio/Plug-Ins/VST3"),
        home.getChildFile("Library/Audio/Plug-Ins/VST3"),
        juce::File("/Library/Audio/Plug-Ins/Components"),
        home.getChildFile("Library/Audio/Plug-Ins/Components")
    };

    juce::File best;
    int bestScore = -1;

    for (const auto& dir : roots)
    {
        if (! dir.isDirectory())
            continue;

        juce::Array<juce::File> files;
        dir.findChildFiles(files, juce::File::findFilesAndDirectories, true);

        for (const auto& f : files)
        {
            if (! (f.hasFileExtension("vst3") || f.hasFileExtension("component")))
                continue;

            const auto name = f.getFileNameWithoutExtension();
            const auto full = f.getFullPathName();

            if (! name.containsIgnoreCase("Capitol")
                || ! name.containsIgnoreCase("Chambers"))
                continue;

            // Explicitly reject known legacy/DSP naming when it is identifiable.
            const bool saysUadx = name.containsIgnoreCase("UADx")
                               || name.startsWithIgnoreCase("uaudio_")
                               || full.containsIgnoreCase("/UADx/");
            const bool saysLegacyUad = (name.startsWithIgnoreCase("UAD ")
                                     || name.startsWithIgnoreCase("UAD-")
                                     || full.containsIgnoreCase("/UAD-2/"))
                                    && ! saysUadx;
            if (saysLegacyUad || !saysUadx)
                continue;

            int score = 0;
            if (saysUadx) score += 1000;             // Native UA build is mandatory/preferred.
            if (f.hasFileExtension("vst3")) score += 100; // Prefer VST3 over AU when both exist.
            if (name.startsWithIgnoreCase("UADx")) score += 50;
            if (full.containsIgnoreCase("Universal Audio")) score += 10;

            if (score > bestScore)
            {
                bestScore = score;
                best = f;
            }
        }
    }

    return best;
}

void MainComponent::tryLoadCapitolChambers()
{
    const auto file = findCapitolChambersFile();
    if (!file.exists()) { evolutionStatus.setText("UADx Capitol Chambers not found", juce::dontSendNotification); return; }
    juce::String error;
    if (!capitolReverbHost.loadFromFile(file, error))
        evolutionStatus.setText("Capitol Chambers load issue · " + error, juce::dontSendNotification);
}


juce::File MainComponent::findParadiseGuitarStudioFile()
{
    // Paradise Guitar Studio is a native UAD plug-in. Prefer its VST3 build,
    // then Audio Unit, and reject identifiable UAD-2/DSP paths.
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    const juce::File roots[] = {
        juce::File("/Library/Audio/Plug-Ins/VST3"),
        home.getChildFile("Library/Audio/Plug-Ins/VST3"),
        juce::File("/Library/Audio/Plug-Ins/Components"),
        home.getChildFile("Library/Audio/Plug-Ins/Components")
    };

    juce::File best;
    int bestScore = -1;

    for (const auto& dir : roots)
    {
        if (! dir.isDirectory())
            continue;

        juce::Array<juce::File> files;
        dir.findChildFiles(files, juce::File::findFilesAndDirectories, true);

        for (const auto& f : files)
        {
            if (! (f.hasFileExtension("vst3") || f.hasFileExtension("component")))
                continue;

            const auto name = f.getFileNameWithoutExtension();
            const auto full = f.getFullPathName();

            if (! name.containsIgnoreCase("Paradise")
                || ! name.containsIgnoreCase("Guitar"))
                continue;

            const bool uadxNative = name.containsIgnoreCase("UADx")
                                 || name.startsWithIgnoreCase("uaudio_")
                                 || full.containsIgnoreCase("/UADx/");
            const bool legacyDsp = full.containsIgnoreCase("/UAD-2/")
                                || name.startsWithIgnoreCase("UAD-");
            if (legacyDsp || !uadxNative)
                continue;

            int score = 1000;
            if (f.hasFileExtension("vst3")) score += 100;
            if (full.containsIgnoreCase("Universal Audio")) score += 10;
            if (name.containsIgnoreCase("Paradise Guitar Studio")) score += 50;

            if (score > bestScore)
            {
                bestScore = score;
                best = f;
            }
        }
    }
    return best;
}

void MainComponent::tryLoadParadiseGuitarStudio()
{
    const auto file = findParadiseGuitarStudioFile();
    if (!file.exists()) { evolutionStatus.setText("UADx Paradise Guitar Studio not found", juce::dontSendNotification); return; }
    juce::String error;
    if (!paradiseGuitarHost.loadFromFile(file, error))
        evolutionStatus.setText("Paradise Guitar Studio load issue · " + error, juce::dontSendNotification);
}

static juce::File findNativeUaNamed(const juce::StringArray& terms)
{
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    const juce::File roots[] = { juce::File("/Library/Audio/Plug-Ins/VST3"), home.getChildFile("Library/Audio/Plug-Ins/VST3"),
                                 juce::File("/Library/Audio/Plug-Ins/Components"), home.getChildFile("Library/Audio/Plug-Ins/Components") };
    juce::File best; int bestScore=-1;
    for (const auto& dir: roots)
    {
        if(!dir.isDirectory()) continue; juce::Array<juce::File> files; dir.findChildFiles(files, juce::File::findFilesAndDirectories, true);
        for(const auto& f: files)
        {
            if(!(f.hasFileExtension("vst3")||f.hasFileExtension("component"))) continue;
            const auto name=f.getFileNameWithoutExtension(); const auto full=f.getFullPathName(); bool match=true;
            for(const auto& t:terms) if(!name.containsIgnoreCase(t)){match=false;break;} if(!match) continue;
            const bool uadx=name.containsIgnoreCase("UADx")||name.startsWithIgnoreCase("uaudio_")||full.containsIgnoreCase("/UADx/");
            const bool legacy=(name.startsWithIgnoreCase("UAD-")||full.containsIgnoreCase("/UAD-2/"))&&!uadx; if(legacy || !uadx) continue;
            int score=(uadx?1000:0)+(f.hasFileExtension("vst3")?100:0)+(full.containsIgnoreCase("Universal Audio")?10:0);
            if(score>bestScore){bestScore=score;best=f;}
        }
    }
    return best;
}

juce::File MainComponent::findCenturyTubeFile() { return findNativeUaNamed({ "Century", "Tube" }); }
juce::File MainComponent::findGalaxyTapeEchoFile() { return findNativeUaNamed({ "Galaxy", "Tape" }); }

void MainComponent::tryLoadCenturyTube()
{
    const auto f=findCenturyTubeFile(); if(!f.exists()) return; juce::String error; centuryTubeHost.loadFromFile(f,error);
}
void MainComponent::tryLoadGalaxyTapeEcho()
{
    const auto f=findGalaxyTapeEchoFile(); if(!f.exists()) return; juce::String error; galaxyTapeHost.loadFromFile(f,error);
}

juce::File MainComponent::findMiniMoogFile()
{
    const juce::File candidates[] = {
        juce::File("/Library/Audio/Plug-Ins/VST3/uaudio_minimoog.vst3"),
        juce::File("/Library/Audio/Plug-Ins/VST3/Minimoog.vst3"),
        juce::File("/Library/Audio/Plug-Ins/Components/uaudio_minimoog.component"),
        juce::File("/Library/Audio/Plug-Ins/Components/Minimoog.component"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Library/Audio/Plug-Ins/VST3/uaudio_minimoog.vst3"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Library/Audio/Plug-Ins/VST3/Minimoog.vst3"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Library/Audio/Plug-Ins/Components/uaudio_minimoog.component"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Library/Audio/Plug-Ins/Components/Minimoog.component")
    };
    for (const auto& file : candidates)
        if (file.exists())
            return file;

    // UADx naming can vary slightly by release. Fall back to a shallow native
    // plug-in scan instead of leaving the in-app keyboard connected to silence.
    const juce::File roots[] = {
        juce::File("/Library/Audio/Plug-Ins/VST3"),
        juce::File("/Library/Audio/Plug-Ins/Components"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/Audio/Plug-Ins/VST3"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/Audio/Plug-Ins/Components")
    };
    for (const auto& root : roots)
    {
        if (! root.isDirectory()) continue;
        juce::Array<juce::File> found;
        root.findChildFiles(found, juce::File::findFilesAndDirectories, true, "*minimoog*");
        for (const auto& file : found)
            if (file.getFileName().containsIgnoreCase("minimoog")
                && (file.hasFileExtension("vst3") || file.hasFileExtension("component")))
                return file;
    }
    return {};
}

juce::File MainComponent::findElectraFile()
{
    const juce::File candidates[] = {
        juce::File("/Library/Audio/Plug-Ins/VST3/uaudio_electra.vst3"),
        juce::File("/Library/Audio/Plug-Ins/Components/uaudio_electra.component"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Library/Audio/Plug-Ins/VST3/uaudio_electra.vst3"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Library/Audio/Plug-Ins/Components/uaudio_electra.component")
    };
    for (const auto& file : candidates)
        if (file.exists())
            return file;
    return {};
}

juce::File MainComponent::findPolymaxFile()
{
    const juce::File candidates[] = {
        juce::File("/Library/Audio/Plug-Ins/VST3/uaudio_polymax.vst3"),
        juce::File("/Library/Audio/Plug-Ins/Components/uaudio_polymax.component"),
        juce::File("/Library/Audio/Plug-Ins/VST3/PolyMAX.vst3"),
        juce::File("/Library/Audio/Plug-Ins/Components/PolyMAX.component"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Library/Audio/Plug-Ins/VST3/uaudio_polymax.vst3"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Library/Audio/Plug-Ins/Components/uaudio_polymax.component"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Library/Audio/Plug-Ins/VST3/PolyMAX.vst3"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Library/Audio/Plug-Ins/Components/PolyMAX.component")
    };
    for (const auto& file : candidates)
        if (file.exists())
            return file;

    const juce::File roots[] = {
        juce::File("/Library/Audio/Plug-Ins/VST3"),
        juce::File("/Library/Audio/Plug-Ins/Components"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/Audio/Plug-Ins/VST3"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/Audio/Plug-Ins/Components")
    };
    for (const auto& root : roots)
    {
        if (! root.isDirectory()) continue;
        juce::Array<juce::File> found;
        root.findChildFiles(found, juce::File::findFilesAndDirectories, true, "*polymax*");
        for (const auto& file : found)
            if ((file.hasFileExtension("vst3") || file.hasFileExtension("component"))
                && (file.getFileName().containsIgnoreCase("polymax")
                    || file.getFileName().containsIgnoreCase("maxpoly")))
                return file;
    }
    return {};
}

void MainComponent::tryLoadKeys()
{
    auto file = findElectraFile();
    if (! file.exists())
        file = juce::File(engine.state().lastKeysPluginPath);
    if (! file.exists() || file.getFileName().containsIgnoreCase("polymax"))
    {
        evolutionStatus.setText("Electra 88 not found", juce::dontSendNotification);
        return;
    }
    if (keysHost.isLoaded() && keysHost.getFile() == file)
    {
        applyStoredKeysPatch();
        refreshKeysKitUi(true);
        keysHost.showEditor();
        return;
    }

    juce::String error;
    if (keysHost.loadFromFile(file, error))
    {
        engine.state().lastKeysPluginPath = file.getFullPathName();
        keysHost.prepare(deviceManager.getCurrentAudioDevice() != nullptr
                             ? deviceManager.getCurrentAudioDevice()->getCurrentSampleRate()
                             : 44100.0,
                         deviceManager.getCurrentAudioDevice() != nullptr
                             ? deviceManager.getCurrentAudioDevice()->getCurrentBufferSizeSamples()
                             : 512);
        evolutionStatus.setText("KEYS · " + keysHost.getName() + " · CH"
                                    + juce::String(groove::kMidiChKeys),
                                juce::dontSendNotification);
        keysHost.showEditor();
        applyStoredKeysPatch();
        refreshKeysKitUi(true);
    }
    else
    {
        evolutionStatus.setText("Keys failed: " + error, juce::dontSendNotification);
    }
}

void MainComponent::tryLoadPolymax()
{
    auto file = juce::File(engine.state().lastPolymaxPluginPath);
    if (! file.exists() || file.getFileName().containsIgnoreCase("prophet"))
        file = findPolymaxFile();
    if (! file.exists())
    {
        evolutionStatus.setText("PolyMAX / MAXPOLY not found", juce::dontSendNotification);
        return;
    }
    if (polymaxHost.isLoaded() && polymaxHost.getFile() == file)
    {
        applyStoredPolyPatch();
        refreshPolyKitUi(true);
        polymaxHost.showEditor();
        return;
    }

    juce::String error;
    if (polymaxHost.loadFromFile(file, error))
    {
        engine.state().lastPolymaxPluginPath = file.getFullPathName();
        polymaxHost.prepare(deviceManager.getCurrentAudioDevice() != nullptr
                                ? deviceManager.getCurrentAudioDevice()->getCurrentSampleRate()
                                : 44100.0,
                            deviceManager.getCurrentAudioDevice() != nullptr
                                ? deviceManager.getCurrentAudioDevice()->getCurrentBufferSizeSamples()
                                : 512);
        evolutionStatus.setText("MAXPOLY · " + polymaxHost.getName() + " · CH"
                                    + juce::String(groove::kMidiChPoly),
                                juce::dontSendNotification);
        polymaxHost.showEditor();
        applyStoredPolyPatch();
        refreshPolyKitUi(true);
    }
    else
    {
        evolutionStatus.setText("PolyMAX failed: " + error, juce::dontSendNotification);
    }
}

int MainComponent::keyboardMidiChannel() const
{
    switch (synthKeyboard.getTarget())
    {
        case 1:  return groove::kMidiChKeys;
        case 2:  return groove::kMidiChPoly;
        case 3:  return groove::kMidiChDrums;
        default: return groove::kMidiChMoog;
    }
}

void MainComponent::applyLiveMidiChannel(int channel)
{
    const int ch = juce::jlimit(1, 16, channel > 0 ? channel : groove::kMidiChMoog);
    liveMidiChannel.store(ch);
    const int laneForSelection = groove::midiLaneIndexForChannel(ch);
    if (laneForSelection > 0)
        engine.selectUnifiedTarget(groove::unifiedTrackForMidiLane(laneForSelection));
    else if (ch == groove::kMidiChDrums)
        engine.selectUnifiedTarget(engine.state().selectedTrack);
    mixStrip.setActiveMidiChannel(ch);
    songPage.setActiveMidiChannel(ch);
    instrumentDock.setSelectedChannel(ch);
    torsoPage.setMidiLane(groove::midiLaneIndexForChannel(ch));

    int target = -1;
    juce::String dest = "CH" + juce::String(ch);
    if (ch == groove::kMidiChDrums) { target = 3; dest = "DRUMS"; }
    else if (ch == groove::kMidiChMoog) { target = 0; dest = "MOOG"; }
    else if (ch == groove::kMidiChPoly) { target = 2; dest = "MAXPOLY"; }
    else if (ch == groove::kMidiChKeys) { target = 1; dest = "KEYS"; }

    if (target >= 0)
    {
        synthKeyboard.setTarget(target);
        mixerKeyboard.setTarget(target);
    }

    evolutionStatus.setText("MIDI · CH" + juce::String(ch) + "  →  " + dest,
                            juce::dontSendNotification);
}

void MainComponent::selectMidiChannelFromUi(int channel)
{
    const int melodicLane = groove::midiLaneIndexForChannel(channel);
    if (melodicLane >= 0) pianoRoll.setLane(melodicLane);

    const int ch = juce::jlimit(1, 16, channel > 0 ? channel : groove::kMidiChMoog);
    if (ch != liveMidiChannel.load() && engine.isRecording())
        engine.setRecording(false);
    uiChannelLocked.store(true);
    applyLiveMidiChannel(ch);
    if (currentPage == 2)
        songPage.refreshFromEngine();
    else if (currentPage == 1)
        torsoPage.refreshFromEngine();

    pianoRoll.refresh();
    resized();
    repaint();
}

void MainComponent::splitDrumAndSynthMidi(const juce::MidiBuffer& src,
                                          juce::MidiBuffer& drums, juce::MidiBuffer& synth) const
{
    for (const auto metadata : src)
    {
        const auto msg = metadata.getMessage();
        const int pos = metadata.samplePosition;
        if (msg.isNoteOnOrOff() || msg.isAftertouch())
        {
            if (groove::isUjamKitNote(msg.getNoteNumber()))
                drums.addEvent(groove::withMidiChannel(msg, groove::kMidiChDrums), pos);
            else
                synth.addEvent(groove::withMidiChannel(msg, groove::kMidiChMoog), pos);
            continue;
        }
        if (msg.isPitchWheel() || (msg.isController() && msg.getControllerNumber() != 7
                                   && msg.getControllerNumber() != 11
                                   && (msg.getControllerNumber() < 20 || msg.getControllerNumber() > 27)))
        {
            synth.addEvent(groove::withMidiChannel(msg, groove::kMidiChMoog), pos);
            continue;
        }
        drums.addEvent(groove::withMidiChannel(msg, groove::kMidiChDrums), pos);
    }
}

void MainComponent::routeExternalMidiToSelected(const juce::MidiBuffer& live,
                                                juce::MidiBuffer& drums, juce::MidiBuffer& moog,
                                                juce::MidiBuffer& keys, juce::MidiBuffer& poly) const
{
    // Hardware keyboards commonly transmit on CH1. CH1 must not implicitly mean
    // drums during normal live play. The selected mixer/keyboard target is the
    // destination; the plug-in host will normalize the plug-in-facing channel.
    const int dest = liveMidiChannel.load();
    for (const auto metadata : live)
    {
        auto msg = metadata.getMessage();
        const int pos = metadata.samplePosition;

        if (dest == groove::kMidiChDrums)
            drums.addEvent(groove::withMidiChannel(msg, groove::kMidiChDrums), pos);
        else if (dest == groove::kMidiChMoog)
            moog.addEvent(groove::withMidiChannel(msg, groove::kMidiChMoog), pos);
        else if (dest == groove::kMidiChPoly)
            poly.addEvent(groove::withMidiChannel(msg, groove::kMidiChPoly), pos);
        else if (dest == groove::kMidiChKeys)
            keys.addEvent(groove::withMidiChannel(msg, groove::kMidiChKeys), pos);
    }
}

void MainComponent::routeChannelMidi(const juce::MidiBuffer& source,
                                    juce::MidiBuffer& drums, juce::MidiBuffer& moog,
                                    juce::MidiBuffer& keys, juce::MidiBuffer& poly) const
{
    for (const auto metadata : source)
    {
        const auto msg = metadata.getMessage();
        const int pos = metadata.samplePosition;
        const int ch = msg.getChannel();
        if (ch == groove::kMidiChMoog)
            moog.addEvent(msg, pos);
        else if (ch == groove::kMidiChKeys)
            keys.addEvent(msg, pos);
        else if (ch == groove::kMidiChPoly)
            poly.addEvent(msg, pos);
        else if (ch == groove::kMidiChDrums)
            drums.addEvent(msg, pos);
    }
}

void MainComponent::tryLoadMiniMoog()
{
    auto file = juce::File(engine.state().lastSynthPluginPath);
    if (! file.exists())
        file = findMiniMoogFile();
    if (! file.exists())
    {
        evolutionStatus.setText("Mini-Moog not found — Sound menu · Load Mini-Moog",
                                juce::dontSendNotification);
        return;
    }
    if (synthHost.isLoaded() && synthHost.getFile() == file)
        return;
    loadSynthFromFile(file);
}

void MainComponent::showInstrumentBrowser(int channel)
{
    if (instrumentBrowser == nullptr)
        instrumentBrowser = std::make_unique<InstrumentBrowserWindow>(look, "INSTRUMENTS");

    juce::String slot = "CH" + juce::String(channel);
    if (channel == groove::kMidiChDrums) slot = "DRUMS · CH1";
    else if (channel == groove::kMidiChMoog) slot = "CH2";
    else if (channel == groove::kMidiChPoly) slot = "CH3";
    else if (channel == groove::kMidiChKeys) slot = "CH4";

    instrumentBrowser->showBrowser(slot, [this, channel](const juce::File& f)
    {
        loadInstrumentForChannel(channel, f);
    });
}

void MainComponent::loadInstrumentForChannel(int channel, const juce::File& file)
{
    if (channel == groove::kMidiChMoog)
    {
        loadSynthFromFile(file);
        selectMidiChannelFromUi(channel);
        return;
    }

    groove::ExternalPluginHost* host = nullptr;
    juce::String* storedPath = nullptr;
    juce::String label;
    if (channel == groove::kMidiChDrums)
    {
        host = &pluginHost; storedPath = &engine.state().lastPluginPath; label = "DRUMS";
    }
    else if (channel == groove::kMidiChPoly)
    {
        host = &polymaxHost; storedPath = &engine.state().lastPolymaxPluginPath; label = "CH3";
    }
    else if (channel == groove::kMidiChKeys)
    {
        host = &keysHost; storedPath = &engine.state().lastKeysPluginPath; label = "CH4";
    }
    if (host == nullptr || storedPath == nullptr) return;

    juce::String error;
    if (! host->loadFromFile(file, error))
    {
        evolutionStatus.setText(label + " · instrument failed: " + error, juce::dontSendNotification);
        return;
    }
    *storedPath = file.getFullPathName();
    auto* dev = deviceManager.getCurrentAudioDevice();
    host->prepare(dev != nullptr ? dev->getCurrentSampleRate() : 44100.0,
                  dev != nullptr ? dev->getCurrentBufferSizeSamples() : 512);
    engine.saveAutosave();
    selectMidiChannelFromUi(channel);
    evolutionStatus.setText(label + " · " + host->getName(), juce::dontSendNotification);
    host->showEditor();
    if (channel == groove::kMidiChDrums) refreshVstKitUi(true);
    else if (channel == groove::kMidiChPoly) refreshPolyKitUi(true);
    else if (channel == groove::kMidiChKeys) refreshKeysKitUi(true);
}

void MainComponent::browseForSynth()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Mini-Moog or synth",
        juce::File("/Library/Audio/Plug-Ins/VST3"),
        juce::String(),
        true);

    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::canSelectDirectories;

    fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file.exists())
            loadSynthFromFile(file);
    });
}

void MainComponent::loadSynthFromFile(const juce::File& file)
{
    juce::String error;
    if (synthHost.loadFromFile(file, error))
    {
        engine.state().lastSynthPluginPath = file.getFullPathName();
        engine.saveAutosave();
        synthHost.prepare(deviceManager.getCurrentAudioDevice() != nullptr
                              ? deviceManager.getCurrentAudioDevice()->getCurrentSampleRate()
                              : 44100.0,
                          deviceManager.getCurrentAudioDevice() != nullptr
                              ? deviceManager.getCurrentAudioDevice()->getCurrentBufferSizeSamples()
                              : 512);
        evolutionStatus.setText("SYNTH · " + synthHost.getName(), juce::dontSendNotification);
        synthHost.showEditor();
        applyStoredSynthPatch();
        refreshSynthKitUi(true);
    }
    else
    {
        evolutionStatus.setText("Mini-Moog failed: " + error, juce::dontSendNotification);
    }
}

void MainComponent::captureSessionIntoState()
{
    engine.state().name = projectName.getText().trim();
    if (engine.state().name.isEmpty())
        engine.state().name = "Lil God Projector";
    engine.state().soundMode = soundMode.load();
    if (pluginHost.isLoaded())
        engine.state().lastPluginPath = pluginHost.getFile().getFullPathName();
    if (synthHost.isLoaded())
        engine.state().lastSynthPluginPath = synthHost.getFile().getFullPathName();
    engine.state().lastPluginProgram = pluginHost.getKitIndex();
    engine.state().lastPluginPatch = pluginHost.getCurrentPatchName();
    engine.state().lastSynthPatch = synthHost.getCurrentPatchName();
    if (engine.state().lastSynthPatch.isEmpty())
        engine.state().lastSynthPatch = synthHost.getKitName(synthHost.getKitIndex());
    engine.state().lastSynthOctave = synthKeyboard.getOctaveOffset();
    engine.state().lastKeyboardTarget = synthKeyboard.getTarget();
    if (keysHost.isLoaded())
        engine.state().lastKeysPluginPath = keysHost.getFile().getFullPathName();
    if (polymaxHost.isLoaded())
        engine.state().lastPolymaxPluginPath = polymaxHost.getFile().getFullPathName();
    storeCurrentKeysPatch();
    storeCurrentPolyPatch();
    mixStrip.saveTo(engine.state().mix);
}

void MainComponent::applyLoadedSession()
{
    const auto& st = engine.state();
    projectName.setText(st.name, false);
    const int mode = juce::jlimit(1, 3, st.soundMode);
    setSoundMode(mode);

    const auto pluginFile = juce::File(st.lastPluginPath);
    const bool alreadyLoaded = pluginHost.isLoaded()
        && (! pluginFile.exists() || pluginHost.getFile() == pluginFile);

    // Keep a live UJAM instance. Reloading / re-preparing it on New or LOAD
    // tears down its buses and the kit goes silent.
    if (! alreadyLoaded)
    {
        if (pluginFile.exists())
        {
            juce::String error;
            if (pluginHost.loadFromFile(pluginFile, error))
            {
                setSoundMode(2);
                pluginHost.prepare(deviceManager.getCurrentAudioDevice() != nullptr
                                       ? deviceManager.getCurrentAudioDevice()->getCurrentSampleRate()
                                       : 44100.0,
                                   deviceManager.getCurrentAudioDevice() != nullptr
                                       ? deviceManager.getCurrentAudioDevice()->getCurrentBufferSizeSamples()
                                       : 512);
                evolutionStatus.setText("LOADED · " + st.name + " · " + pluginHost.getName(),
                                        juce::dontSendNotification);
        pluginHost.showEditor();
        applyStoredPluginKit();
        refreshVstKitUi(true);
            }
            else
            {
                tryLoadUjamHot();
            }
        }
        else
        {
            tryLoadUjamHot();
        }
    }
    else if (pluginHost.isLoaded())
    {
        evolutionStatus.setText("LOADED · " + st.name + " · " + pluginHost.getName(),
                                juce::dontSendNotification);
        applyStoredPluginKit();
        refreshVstKitUi(true);
    }

    torsoPage.refreshFromEngine();
    songPage.refreshFromEngine();
    refreshFromSelection();

    if (soundMode.load() >= 2 && ! pluginHost.isLoaded())
    {
        setSoundMode(1);
        evolutionStatus.setText("UJAM not loaded — using INTERNAL. Sound menu · Load UJAM / VST to host Hot.",
                                juce::dontSendNotification);
    }

    if (pluginHost.isLoaded())
        refreshVstKitUi(vstKitListCount < 0);

    const auto synthFile = juce::File(st.lastSynthPluginPath);
    const bool synthReady = synthHost.isLoaded()
        && (! synthFile.exists() || synthHost.getFile() == synthFile);
    if (! synthReady)
        tryLoadMiniMoog();
    else if (synthHost.isLoaded())
    {
        applyStoredSynthPatch();
        refreshSynthKitUi(true);
    }

    mixStrip.loadFrom(st.mix);
    pushMixToDsp();
    synthKeyboard.setOctaveOffset(st.lastSynthOctave);
    synthKeyboard.setTarget(st.lastKeyboardTarget);
    mixerKeyboard.setOctaveOffset(st.lastSynthOctave);
    mixerKeyboard.setTarget(st.lastKeyboardTarget);
    tryLoadKeys();
    tryLoadPolymax();
    applyLiveMidiChannel(keyboardMidiChannel());
    showAllInstrumentEditors();
    repaint();
}

void MainComponent::showAllInstrumentEditors()
{
    // One plug-in editor at a time keeps the desktop usable. Restore only the
    // globally selected instrument rather than spawning four floating windows.
    showInstrumentEditor(selectedTargetChannel());
}

void MainComponent::showInstrumentEditor(int channel)
{
    // Reuse the plug-in-editor role: before showing one instrument, close the
    // other hosted editors.  This preserves modeless native plug-in UIs without
    // creating a pile of overlapping windows.
    pluginHost.hideEditor();
    synthHost.hideEditor();
    keysHost.hideEditor();
    polymaxHost.hideEditor();

    if (channel == groove::kMidiChDrums)
    {
        if (pluginHost.isLoaded()) pluginHost.showEditor();
        else evolutionStatus.setText("DRUMS · load a kit first", juce::dontSendNotification);
        return;
    }
    if (channel == groove::kMidiChMoog)
    {
        if (! synthHost.isLoaded()) tryLoadMiniMoog();
        if (synthHost.isLoaded()) synthHost.showEditor();
        return;
    }
    if (channel == groove::kMidiChPoly)
    {
        if (! polymaxHost.isLoaded()) tryLoadPolymax();
        if (polymaxHost.isLoaded()) polymaxHost.showEditor();
        return;
    }
    if (channel == groove::kMidiChKeys)
    {
        if (! keysHost.isLoaded()) tryLoadKeys();
        if (keysHost.isLoaded()) keysHost.showEditor();
    }
}

void MainComponent::saveCurrentGroove()
{
    captureSessionIntoState();
    juce::String error;
    if (engine.saveStoredGroove(engine.state().name, error))
    {
        projectName.setText(engine.state().name, false);
        evolutionStatus.setText("SAVED · " + engine.state().name, juce::dontSendNotification);
    }
    else
    {
        evolutionStatus.setText("Save failed: " + error, juce::dontSendNotification);
    }
}

void MainComponent::saveGrooveAs()
{
    captureSessionIntoState();
    auto current = engine.state().name.trim();
    if (current.isEmpty())
        current = "Lil God Projector";
    const auto suggested = current.endsWithIgnoreCase(" copy") ? current : current + " copy";

    auto* w = new juce::AlertWindow("Save As",
                                    "Saves a new named groove. The current file is left as-is.",
                                    juce::MessageBoxIconType::QuestionIcon);
    w->addTextEditor("name", suggested, "Name");
    w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int result)
    {
        if (result != 1)
            return;
        auto name = w->getTextEditorContents("name").trim();
        if (name.isEmpty())
            name = "Lil God Projector";
        juce::String error;
        if (engine.saveStoredGroove(name, error))
        {
            projectName.setText(engine.state().name, false);
            evolutionStatus.setText("SAVED AS · " + engine.state().name, juce::dontSendNotification);
        }
        else
        {
            evolutionStatus.setText("Save failed: " + error, juce::dontSendNotification);
        }
    }), true);
}

void MainComponent::saveGrooveAsFile()
{
    captureSessionIntoState();
    fileChooser = std::make_unique<juce::FileChooser>(
        "Save Groove",
        engine.groovesDir().getChildFile(engine.legalGrooveName(engine.state().name) + ".groove.json"),
        "*.groove.json;*.json");
    const auto flags = juce::FileBrowserComponent::saveMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::warnAboutOverwriting;
    fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file.getFullPathName().isEmpty())
            return;
        if (! file.hasFileExtension(".json"))
            file = file.withFileExtension("groove.json");
        juce::String error;
        if (engine.saveGrooveFile(file, error))
        {
            projectName.setText(engine.state().name, false);
            evolutionStatus.setText("SAVED FILE · " + file.getFileName(), juce::dontSendNotification);
        }
        else
            evolutionStatus.setText("Save failed: " + error, juce::dontSendNotification);
    });
}

void MainComponent::loadGrooveFromFile(const juce::File& file)
{
    juce::String error;
    if (! engine.loadGrooveFile(file, error))
    {
        evolutionStatus.setText("Load failed: " + error, juce::dontSendNotification);
        return;
    }
    applyLoadedSession();
    evolutionStatus.setText("LOADED · " + engine.state().name, juce::dontSendNotification);
}

void MainComponent::newGroove()
{
    engine.newProject();
    loadInstrumentDefaultsIntoState();
    applyLoadedSession();
    evolutionStatus.setText("NEW GROOVE", juce::dontSendNotification);
}

void MainComponent::showFileMenu()
{
    juce::PopupMenu menu;
    menu.addItem(2001, "Save");
    menu.addItem(2002, "Save As...");
    menu.addSeparator();
    const auto stored = engine.listStoredGrooves();
    juce::Array<juce::File> files;
    if (stored.isEmpty())
        menu.addItem(1, "(No saved grooves yet)", false, false);
    else
    {
        for (const auto& item : stored)
        {
            files.add(item.file);
            menu.addItem(files.size(), item.name);
        }
    }
    menu.addSeparator();
    menu.addItem(1000, "New Groove");
    menu.addItem(1001, "Open File...");
    menu.addItem(1002, "Export File...");
    menu.addItem(1003, "Reveal Store Folder");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&fileMenuButton),
        [this, files](int result)
        {
            if (result == 2001) { saveCurrentGroove(); return; }
            if (result == 2002) { saveGrooveAs(); return; }
            if (result == 1000) { newGroove(); return; }
            if (result == 1001)
            {
                fileChooser = std::make_unique<juce::FileChooser>(
                    "Open Groove", engine.groovesDir(), "*.groove.json;*.json");
                const auto flags = juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectFiles;
                fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc)
                {
                    const auto file = fc.getResult();
                    if (file.existsAsFile())
                        loadGrooveFromFile(file);
                });
                return;
            }
            if (result == 1002) { saveGrooveAsFile(); return; }
            if (result == 1003)
            {
                engine.groovesDir().revealToUser();
                return;
            }
            if (result >= 1 && result <= files.size())
                loadGrooveFromFile(files[result - 1]);
        });
}


void MainComponent::showViewMenu()
{
    juce::PopupMenu menu;
    const bool mixerVisible = mixerWindow != nullptr && mixerWindow->isVisible();
    menu.addItem(1, "Mixer", true, mixerVisible);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&mixWindowButton),
        [this](int result)
        {
            if (result != 1)
                return;

            const bool mixerVisible = mixerWindow != nullptr && mixerWindow->isVisible();
            if (mixerVisible)
            {
                saveMixerWindowBounds();
                mixerWindow->setVisible(false);
                mixWindowButton.setToggleState(false, juce::dontSendNotification);
            }
            else
            {
                showMixerWindow(true);
            }
        });
}

void MainComponent::showLabMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Capture");
    menu.addItem(2, "Back");
    menu.addItem(3, "Open Evolve Lab");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&labMenuButton),
        [this](int result)
        {
            if (result == 1) captureButton.triggerClick();
            else if (result == 2) backButton.triggerClick();
            else if (result == 3) evolveButton.triggerClick();
        });
}

void MainComponent::showLoadMenu()
{
    showFileMenu();
}

void MainComponent::refreshMidiOutputs()
{
    const auto previousId = (midiOutIndex >= 0 && midiOutIndex < midiDevices.size())
        ? midiDevices[midiOutIndex].identifier : juce::String();
    midiDevices = juce::MidiOutput::getAvailableDevices();
    midiOutIndex = -1;
    if (previousId.isNotEmpty())
    {
        for (int i = 0; i < midiDevices.size(); ++i)
        {
            if (midiDevices[i].identifier == previousId)
            {
                midiOutIndex = i;
                break;
            }
        }
        if (midiOutIndex < 0)
        {
            if (midiOutput != nullptr)
                midiOutput->stopBackgroundThread();
            midiOutput.reset();
        }
    }
}

void MainComponent::addSmallLabel(juce::Label& l, const juce::String& text)
{
    l.setText(text, juce::dontSendNotification);
    l.setColour(juce::Label::textColourId, juce::Colour(0xffa8c0cf));
    l.setFont(juce::FontOptions(11.0f));
    addAndMakeVisible(l);
}

void MainComponent::drawPanel(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
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
}

void MainComponent::runEvolution(groove::EvolutionEngine::Mode mode)
{
    const auto r = engine.evolve(mode);
    evolutionStatus.setText("EVOLVED · " + juce::String(r.appliedChanges) + " changes",
                            juce::dontSendNotification);
    refreshFromSelection();
    if (evolutionWindow != nullptr)
        evolutionWindow->lab.repaint();
    repaint();
}

void MainComponent::toggleTransport(){ engine.togglePlaying(); playButton.setButtonText(engine.isPlaying()?"STOP":"PLAY"); repaint(); }

juce::File MainComponent::audioSettingsFile()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("Groove Lab");
    dir.createDirectory();
    return dir.getChildFile("audio.xml");
}

void MainComponent::saveAudioSettings()
{
    auto xml = deviceManager.createStateXml();
    if (xml == nullptr)
        xml = juce::XmlDocument::parse(audioSettingsFile());
    if (xml == nullptr)
        xml = std::make_unique<juce::XmlElement>("DEVICESETUP");
    xml->setAttribute("midiInputIdentifier", midiInIdentifier);
    xml->setAttribute("midiInputAuto", midiInAuto);
    xml->writeTo(audioSettingsFile());
}

void MainComponent::preferLowLatencyBuffer()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return;

    auto setup = deviceManager.getAudioDeviceSetup();
    const auto sizes = device->getAvailableBufferSizes();
    int want = 0;
    for (int candidate : { 128, 256, 64 })
        if (sizes.contains(candidate))
        {
            want = candidate;
            break;
        }
    if (want <= 0)
    {
        for (int s : sizes)
            if (s > 0 && s <= 256 && s > want)
                want = s;
    }
    if (want <= 0 || setup.bufferSize == want)
        return;
    setup.bufferSize = want;
    deviceManager.setAudioDeviceSetup(setup, true);
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    saveAudioSettings();
    refreshMidiInputs();
    if (midiInAuto && midiInput == nullptr)
        autoSelectMidiInput();
    if (auto* device = deviceManager.getCurrentAudioDevice())
        evolutionStatus.setText("Audio · " + device->getName() + " · "
                                + juce::String((int) device->getCurrentSampleRate()) + " Hz",
                                juce::dontSendNotification);
}

void MainComponent::saveMixerWindowBoundsForMode(bool compact)
{
    if (mixerWindow == nullptr) return;
    juce::PropertiesFile prefs(instrumentPreferenceOptions());
    prefs.setValue(compact ? "mixerCompactBounds" : "mixerDetailBounds", mixerWindow->getBounds().toString());
    prefs.setValue("mixerWindowVisible", mixerWindow->isVisible());
    prefs.saveIfNeeded();
}

void MainComponent::saveMixerWindowBounds()
{
    saveMixerWindowBoundsForMode(mixStrip.isCompactMode());
}

void MainComponent::restoreMixerWindowBoundsForMode(bool compact)
{
    if (mixerWindow == nullptr) return;
    juce::PropertiesFile prefs(instrumentPreferenceOptions());
    auto bounds = juce::Rectangle<int>::fromString(prefs.getValue(compact ? "mixerCompactBounds" : "mixerDetailBounds"));
    if (bounds.getWidth() < 660 || bounds.getHeight() < 190)
    {
        auto current = mixerWindow->getBounds();
        current.setWidth(compact ? 880 : 1040);
        current.setHeight(compact ? 330 : 480);
        bounds = current;
    }
    // Older builds could remember a desktop-wide compact mixer.  Migrate those
    // preferences to the new console-sized geometry instead of restoring them.
    if (compact)
    {
        bounds.setWidth(juce::jlimit(760, 940, bounds.getWidth()));
        bounds.setHeight(juce::jlimit(300, 360, bounds.getHeight()));
    }
    mixerWindow->setBounds(bounds);
}

void MainComponent::showMixerWindow(bool makeVisible)
{
    if (mixerWindow == nullptr)
    {
        mixerWindow = std::make_unique<MixerWindow>(mixStrip, mixerKeyboard,
            [this]
            {
                mixWindowButton.setToggleState(false, juce::dontSendNotification);
                saveMixerWindowBounds();
            },
            [this] { saveMixerWindowBounds(); });
        mixerWindow->setLookAndFeel(&look);

        juce::PropertiesFile prefs(instrumentPreferenceOptions());
        auto key = mixStrip.isCompactMode() ? "mixerCompactBounds" : "mixerDetailBounds";
        auto bounds = juce::Rectangle<int>::fromString(prefs.getValue(key));
        if (bounds.getWidth() < 660 || bounds.getHeight() < 190)
        {
            auto main = getScreenBounds();
            const int w = juce::jmin(mixStrip.isCompactMode() ? 880 : 1040, juce::jmax(680, main.getWidth() - 200));
            const int h = mixStrip.isCompactMode() ? 330 : 480;
            bounds = { main.getX() + 24, main.getY() + 52, w, h };
        }
        if (mixStrip.isCompactMode())
        {
            bounds.setWidth(juce::jlimit(760, 940, bounds.getWidth()));
            bounds.setHeight(juce::jlimit(300, 360, bounds.getHeight()));
        }
        mixerWindow->setBounds(bounds);
    }

    if (makeVisible)
    {
        // Reassert the content component on every show. JUCE native windows can
        // occasionally restore before their non-owned child has repainted.
        mixStrip.setVisible(true);
        mixerWindow->setVisible(true);
        mixerWindow->toFront(true);
        mixStrip.resized();
        mixStrip.repaint();
        mixWindowButton.setToggleState(true, juce::dontSendNotification);
    }
}

juce::PropertiesFile::Options MainComponent::instrumentPreferenceOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "Lil God Projector";
    options.filenameSuffix = "settings";
    options.folderName = "Lil God Projector";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}

void MainComponent::saveInstrumentDefaults()
{
    captureSessionIntoState();
    juce::PropertiesFile prefs(instrumentPreferenceOptions());
    prefs.setValue("defaultDrums", engine.state().lastPluginPath);
    prefs.setValue("defaultCh2", engine.state().lastSynthPluginPath);
    prefs.setValue("defaultCh3", engine.state().lastPolymaxPluginPath);
    prefs.setValue("defaultCh4", engine.state().lastKeysPluginPath);
    prefs.saveIfNeeded();
    evolutionStatus.setText("PREFERENCES · instrument defaults saved", juce::dontSendNotification);
}

void MainComponent::loadInstrumentDefaultsIntoState()
{
    juce::PropertiesFile prefs(instrumentPreferenceOptions());
    auto& st = engine.state();
    const auto drum = prefs.getValue("defaultDrums");
    const auto ch2 = prefs.getValue("defaultCh2");
    const auto ch3 = prefs.getValue("defaultCh3");
    const auto ch4 = prefs.getValue("defaultCh4");
    if (drum.isNotEmpty()) st.lastPluginPath = drum;
    if (ch2.isNotEmpty()) st.lastSynthPluginPath = ch2;
    if (ch3.isNotEmpty()) st.lastPolymaxPluginPath = ch3;
    if (ch4.isNotEmpty()) st.lastKeysPluginPath = ch4;
}

void MainComponent::showPreferences()
{
    auto* panel = new AudioPreferencesPanel(deviceManager, [this] { saveInstrumentDefaults(); });
    panel->setLookAndFeel(&look);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(panel);
    opts.dialogTitle = "Preferences";
    opts.dialogBackgroundColour = juce::Colour(0xff071018);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = true;
    opts.componentToCentreAround = this;
    opts.launchAsync();
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& path : files)
        if (groove::looksLikeMidiFile(juce::File(path)))
            return true;
    return false;
}

void MainComponent::fileDragEnter(const juce::StringArray&, int, int)
{
    midiDragOver = true;
    repaint();
}

void MainComponent::fileDragExit(const juce::StringArray&)
{
    midiDragOver = false;
    repaint();
}

void MainComponent::filesDropped(const juce::StringArray& files, int x, int y)
{
    midiDragOver = false;
    if (currentPage == 2)
    {
        const auto local = songPage.getLocalPoint(this, juce::Point<int>(x, y));
        const int idx = songPage.sectionIndexAt(local);
        if (idx >= 0)
            engine.selectSongSection(idx);
    }
    importDroppedMidi(files);
    repaint();
}

bool MainComponent::importDroppedMidi(const juce::StringArray& files)
{
    juce::String error = "No MIDI file in drop";
    for (const auto& path : files)
    {
        const juce::File f(path);
        if (! f.existsAsFile())
            continue;
        if (engine.importMidiFile(f, error))
        {
            evolutionStatus.setText("Loaded UJAM phrase · " + f.getFileName(),
                                    juce::dontSendNotification);
            if (currentPage == 1)
                torsoPage.refreshFromEngine();
            songPage.refreshFromEngine();
            refreshFromSelection();
            repaint();
            return true;
        }
    }
    evolutionStatus.setText(error, juce::dontSendNotification);
    return false;
}

void MainComponent::tapTempo()
{
    const auto now = juce::Time::currentTimeMillis();

    // A pause starts a fresh tap sequence rather than averaging unrelated taps.
    if (tapCount > 0 && now - tapTimesMs[(size_t) juce::jmin(tapCount - 1, 4)] > 2000)
        tapCount = 0;

    if (tapCount < (int) tapTimesMs.size())
    {
        tapTimesMs[(size_t) tapCount++] = now;
    }
    else
    {
        for (size_t i = 1; i < tapTimesMs.size(); ++i)
            tapTimesMs[i - 1] = tapTimesMs[i];
        tapTimesMs.back() = now;
    }

    if (tapCount < 2)
    {
        evolutionStatus.setText("TAP TEMPO · tap T again", juce::dontSendNotification);
        return;
    }

    const int count = juce::jmin(tapCount, (int) tapTimesMs.size());
    const int first = tapCount < (int) tapTimesMs.size() ? 0 : 0;
    double totalMs = 0.0;
    int intervals = 0;
    for (int i = first + 1; i < count; ++i)
    {
        const auto dt = tapTimesMs[(size_t) i] - tapTimesMs[(size_t) (i - 1)];
        if (dt >= 230 && dt <= 1500)
        {
            totalMs += (double) dt;
            ++intervals;
        }
    }

    if (intervals == 0)
        return;

    const double tappedBpm = juce::jlimit(40.0, 260.0, 60000.0 / (totalMs / intervals));
    bpm.setValue(juce::roundToInt(tappedBpm), juce::sendNotificationSync);
    evolutionStatus.setText("TAP TEMPO · " + juce::String(juce::roundToInt(tappedBpm)) + " BPM",
                            juce::dontSendNotification);
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && focusMode)
    {
        setFocusMode(false);
        return true;
    }
    if ((key.getKeyCode() == 'f' || key.getKeyCode() == 'F')
        && ! key.getModifiers().isCommandDown()
        && ! key.getModifiers().isCtrlDown()
        && ! key.getModifiers().isAltDown())
    {
        setFocusMode(! focusMode);
        return true;
    }
    if (key == juce::KeyPress::spaceKey)
    {
        toggleTransport();
        return true;
    }
    if ((key.getKeyCode() == 't' || key.getKeyCode() == 'T')
        && ! key.getModifiers().isCommandDown()
        && ! key.getModifiers().isCtrlDown()
        && ! key.getModifiers().isAltDown())
    {
        tapTempo();
        return true;
    }
    if ((key.getKeyCode() == 'r' || key.getKeyCode() == 'R')
        && ! key.getModifiers().isCommandDown()
        && ! key.getModifiers().isCtrlDown()
        && ! key.getModifiers().isAltDown())
    {
        const bool on = ! engine.isRecording();
        engine.setRecording(on);
        recordButton.setToggleState(on, juce::dontSendNotification);
        playButton.setButtonText(engine.isPlaying() ? "STOP" : "PLAY");
        torsoPage.refreshFromEngine();
        songPage.refreshFromEngine();
        repaint();
        return true;
    }
    if (key.getKeyCode() == 'm' || key.getKeyCode() == 'M')
    {
        const int target = engine.state().selectedTarget;
        if (groove::unifiedTrackIsDrum(target))
            engine.toggleMute(target);
        else
        {
            const int lane = groove::unifiedTrackMidiLane(target);
            engine.setMidiLaneMuted(lane, ! engine.state().midiLanes[(size_t) lane].muted);
        }
        repaint();
        return true;
    }
    if ((key.getKeyCode() == 's' || key.getKeyCode() == 'S') && ! key.getModifiers().isCommandDown())
    {
        engine.toggleSolo(engine.state().selectedTrack);
        repaint();
        return true;
    }
    if (key == juce::KeyPress(',', juce::ModifierKeys::commandModifier, 0)
        || key == juce::KeyPress(',', juce::ModifierKeys::ctrlModifier, 0))
    {
        showPreferences();
        return true;
    }
    if (key == juce::KeyPress('s', juce::ModifierKeys::commandModifier, 0)
        || key == juce::KeyPress('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        if (key.getModifiers().isShiftDown())
            saveGrooveAs();
        else
            saveCurrentGroove();
        return true;
    }
    if (key == juce::KeyPress('o', juce::ModifierKeys::commandModifier, 0))
    {
        showLoadMenu();
        return true;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (currentPage == 2)
            return songPage.deleteSelectedNote();
        engine.deleteNote(engine.state().selectedTrack, engine.state().selectedStep);
        refreshFromSelection();
        torsoPage.refreshFromEngine();
        songPage.refreshFromEngine();
        repaint();
        return true;
    }
    if (key == juce::KeyPress('n', juce::ModifierKeys::commandModifier, 0))
    {
        newGroove();
        return true;
    }
    return false;
}

bool MainComponent::keyPressed(const juce::KeyPress& key, juce::Component* origin)
{
    if (origin == &synthKeyboard)
    {
        if (key == juce::KeyPress::spaceKey)
        {
            toggleTransport();
            return true;
        }
        if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown())
            return keyPressed(key);
        return false;
    }
    return keyPressed(key);
}

juce::String MainComponent::selectedTargetName() const
{
    const int target = juce::jlimit(0, groove::kUnifiedTracks - 1, engine.state().selectedTarget);
    if (groove::unifiedTrackIsDrum(target)) return groove::voiceName(target);
    const int lane = groove::unifiedTrackMidiLane(target);
    return groove::midiLaneName(lane);
}

int MainComponent::selectedTargetChannel() const
{
    const int target = engine.state().selectedTarget;
    if (groove::unifiedTrackIsDrum(target)) return groove::kMidiChDrums;
    return groove::midiLaneChannel(groove::unifiedTrackMidiLane(target));
}

void MainComponent::setKeyboardVisible(bool visible)
{
    keyboardVisible = visible;
    keyboardButton.setToggleState(visible, juce::dontSendNotification);
    juce::PropertiesFile prefs(instrumentPreferenceOptions());
    prefs.setValue("keyboardVisible", keyboardVisible);
    prefs.saveIfNeeded();
    resized();
    repaint();
}

void MainComponent::cycleUiDensity()
{
    uiDensity = (uiDensity + 1) % 3;
    applyUiDensity();
    juce::PropertiesFile prefs(instrumentPreferenceOptions());
    prefs.setValue("uiDensity", uiDensity);
    prefs.saveIfNeeded();
    resized();
    repaint();
}

void MainComponent::applyUiDensity()
{
    static const float scales[] { 0.88f, 1.0f, 1.15f };
    static const char* labels[] { "UI:C", "UI:N", "UI:L" };
    look.setUiScale(scales[juce::jlimit(0, 2, uiDensity)]);
    densityButton.setButtonText(labels[juce::jlimit(0, 2, uiDensity)]);
    projectName.setFont(juce::FontOptions((uiDensity == 0 ? 10.5f : uiDensity == 1 ? 12.0f : 13.0f), juce::Font::bold));
    sendLookAndFeelChange();
}

void MainComponent::setFocusMode(bool on)
{
    focusMode = on;
    if (focusMode)
        evolutionStatus.setText("FOCUS · F or Esc to restore", juce::dontSendNotification);
    resized();
    repaint();
}

void MainComponent::refreshContextInspector()
{
    const int target = engine.state().selectedTarget;
    instrumentDock.setSelectedTarget(target);
    const bool drum = groove::unifiedTrackIsDrum(target);
    contextTitle.setText(selectedTargetName(), juce::dontSendNotification);
    if (drum)
    {
        const auto& tr = engine.state().tracks[(size_t) juce::jlimit(0, groove::kTracks - 1, target)];
        const char* mode = tr.rhythmMode == groove::RhythmMode::step ? "STEP" : (tr.rhythmMode == groove::RhythmMode::euclid ? "EUCLID" : "HYBRID");
        contextDetail.setText(juce::String(mode) + " · " + juce::String(tr.pulses) + "/" + juce::String(tr.generatorSteps), juce::dontSendNotification);
        contextHint.setText("SEQ records + edits drums directly\nSONG handles section arrangement", juce::dontSendNotification);
        contextSecondary.setButtonText("DRUM UI");
        contextPrimary.setButtonText(tr.muted ? "UNMUTE" : "MUTE");
    }
    else
    {
        const int lane = groove::unifiedTrackMidiLane(target);
        const auto& ml = engine.state().midiLanes[(size_t) lane];
        const char* mode = groove::rhythmModeName(ml.rhythmMode);
        contextDetail.setText(juce::String(mode) + " · " + juce::String(ml.euclidPulses) + "/" + juce::String(ml.euclidSteps), juce::dontSendNotification);
        contextHint.setText(currentPage == 1 ? "Notes + rhythm + generation\nREC captures this instrument here" : "Selection persists across both workspaces", juce::dontSendNotification);
        contextSecondary.setButtonText(currentPage == 1 ? "FIT NOTES" : "PLUGIN");
        contextPrimary.setButtonText(ml.muted ? "UNMUTE" : "MUTE");
    }
}

void MainComponent::configureRotary(juce::Slider& s,double min,double max,double step){ s.setRange(min,max,step); s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,68,18); }
void MainComponent::configureLinear(juce::Slider& s,double min,double max,double step){ s.setRange(min,max,step); s.setSliderStyle(juce::Slider::LinearHorizontal); s.setTextBoxStyle(juce::Slider::TextBoxRight,false,50,20); }
void MainComponent::bindSoundSlider(juce::Slider& s, groove::Param p)
{
    s.onValueChange = [this, &s, p]
    {
        if (refreshing) return;

        const bool editSelectedStep = (soundScope.getSelectedId() == 1);
        engine.setStepParam(engine.state().selectedTrack,
                            engine.state().selectedStep,
                            p,
                            (float)s.getValue(),
                            editSelectedStep);

        evolutionStatus.setText(editSelectedStep
            ? "STEP SOUND · " + juce::String(displayName(p)) + " locked"
            : "VOICE SOUND · " + juce::String(displayName(p)) + " changed",
            juce::dontSendNotification);
        repaint();
    };
}

void MainComponent::drawGrid(juce::Graphics& g)
{
    auto area=sequencerPanel.reduced(10); area.removeFromTop(gridTopPad); area.removeFromBottom(gridBottomPad); auto labels=area.removeFromLeft(gridLabelWidth);
    float sw=(float)area.getWidth()/groove::kSteps, rh=(float)area.getHeight()/groove::kTracks; const auto& state=engine.state();
    const auto meter = state.meter;
    for (int s = 0; s < groove::kSteps; ++s)
    {
        if (! groove::meterIsBeatLine(meter, s) && ! groove::meterIsBarLine(meter, s))
            continue;
        const bool bar = groove::meterIsBarLine(meter, s);
        g.setColour(bar ? juce::Colour(0xff9ec4d8) : juce::Colour(0xff6a8796));
        g.setFont(juce::FontOptions(bar ? 10.0f : 9.0f, bar ? juce::Font::bold : juce::Font::plain));
        g.drawText(juce::String(s + 1), area.getX() + (int) (s * sw), area.getY() - 19, 30, 16,
                   juce::Justification::centredLeft);
        g.setColour(bar ? juce::Colour(0xff4a7a94).withAlpha(0.55f) : juce::Colour(0xff2a4452).withAlpha(0.45f));
        g.fillRect((float) area.getX() + s * sw, (float) area.getY(), bar ? 2.0f : 1.0f, (float) area.getHeight());
    }
    for(int t=0;t<groove::kTracks;++t)
    {
        int y=area.getY()+(int)(t*rh); auto lr=juce::Rectangle<int>(labels.getX(),y,labels.getWidth()-6,(int)rh-3); auto c=trackColour(t);
        const auto& tr=state.tracks[t];
        const bool silent = ! state.trackIsAudible(t);
        g.setColour(t==state.selectedTrack?juce::Colour(0xff102536):juce::Colour(0xff0b1923));
        if (silent) g.setColour(juce::Colour(0xff071016));
        g.fillRoundedRectangle(lr.toFloat(),4);
        const auto muteDot = juce::Rectangle<float>((float)lr.getX()+6,(float)lr.getCentreY()-7,14,14);
        g.setColour(silent ? juce::Colour(0xff3d4c56) : (tr.soloed?juce::Colour(0xffffd76a):c));
        g.fillEllipse(muteDot);
        g.setColour(silent ? juce::Colour(0xff6a7680) : c.darker(0.35f));
        g.drawEllipse(muteDot, 1.2f);
        if (silent)
        {
            g.setColour(juce::Colour(0xff8a96a0));
            g.drawLine(muteDot.getX()+3, muteDot.getY()+3, muteDot.getRight()-3, muteDot.getBottom()-3, 1.6f);
        }
        if (tr.muted){ g.setColour(juce::Colour(0xffff6363)); g.setFont(juce::FontOptions(10.0f,juce::Font::bold)); g.drawText("M",lr.removeFromRight(16),juce::Justification::centred); }
        else if (tr.soloed){ g.setColour(juce::Colour(0xffffd76a)); g.setFont(juce::FontOptions(10.0f,juce::Font::bold)); g.drawText("S",lr.removeFromRight(16),juce::Justification::centred); }
        g.setColour(silent?juce::Colour(0xff4a5a66):c);
        g.setFont(juce::FontOptions(11.0f,juce::Font::bold));
        g.drawText(juce::String(t+1)+"   "+groove::voiceName(t)+" "+groove::ujamKitName(state.effectiveMidiNote(t,state.selectedStep))+"   "+juce::String(tr.pulses)+"/"+juce::String(tr.generatorSteps),lr.withTrimmedLeft(26),juce::Justification::centredLeft);
        for(int s=0;s<groove::kSteps;++s)
        {
            juce::Rectangle<float> pad(area.getX()+s*sw+1.5f,(float)y+2,sw-3,rh-6); const auto& st=tr.steps[s];
            const bool generatorActive = tr.rhythmMode != groove::RhythmMode::step;
            bool outside=s>=tr.generatorSteps, gen=generatorActive&&!outside&&engine.isGeneratedHit(t,s), resolved=!outside&&engine.isResolvedHit(t,s), selected=t==state.selectedTrack&&s==state.selectedStep, play=s==engine.currentStepForTrack(t);
            if(outside) g.setColour(juce::Colour(0xff091118));
            else if(silent) g.setColour(resolved ? juce::Colour(0xff2a333a) : juce::Colour(0xff12181c));
            else if(resolved) g.setColour(c);
            else g.setColour(juce::Colour(0xff17252f));
            g.fillRoundedRectangle(pad,2.5f);
            if(gen && !resolved && !silent){ g.setColour(c.withAlpha(0.22f)); g.fillRoundedRectangle(pad.reduced(2),2); }
            g.setColour(selected?juce::Colours::white:play?juce::Colour(0xff7bcaff):juce::Colour(0xff253d4b).withAlpha(silent?0.35f:1.0f)); g.drawRoundedRectangle(pad,2.5f,selected?2.0f:play?1.5f:0.8f);
            if(st.overrideMode==groove::StepOverrideMode::forceOn){ g.setColour(juce::Colour(0xffffffff)); g.fillEllipse(pad.getX()+2,pad.getY()+2,3,3); }
            if(st.overrideMode==groove::StepOverrideMode::forceOff){ g.setColour(juce::Colour(0xffff6363)); g.drawLine(pad.getX()+3,pad.getY()+3,pad.getRight()-3,pad.getBottom()-3,1.5f); }
            if(st.hasAnyLock()){ g.setColour(juce::Colour(0xffffbf4d)); g.fillEllipse(pad.getRight()-5,pad.getY()+2,3,3); }
            if(st.role==groove::StepRole::anchor){ g.setColour(juce::Colour(0xffffd576)); g.drawLine(pad.getX()+3,pad.getBottom()-3,pad.getRight()-3,pad.getBottom()-3,1.4f); }
            if(st.ratchet>1){ g.setColour(juce::Colour(0xffe4f6ff)); g.setFont(juce::FontOptions(7.0f)); g.drawText(juce::String(st.ratchet),pad.toNearestInt().reduced(2),juce::Justification::bottomLeft); }
        }
    }
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff050c12));
    const int header = headerHeight();
    const int foot = footerHeight();
    g.setColour(juce::Colour(0xff08131c));
    g.fillRect(0, 0, getWidth(), header);
    {
        auto icon = juce::ImageCache::getFromMemory(BinaryData::GrooveLabIcon_png,
                                                    BinaryData::GrooveLabIcon_pngSize);
        const int is = juce::jmax(24, header - 10);
        g.drawImageWithin(icon, 6, 5, is, is, juce::RectanglePlacement::centred);
    }

    if (! focusMode && ! inspectorPanel.isEmpty())
    {
        g.setColour(juce::Colour(0xff09151e));
        g.fillRoundedRectangle(inspectorPanel.toFloat(), 6.0f);
        g.setColour(juce::Colour(0xff243b49));
        g.drawRoundedRectangle(inspectorPanel.toFloat().reduced(0.5f), 6.0f, 1.0f);
    }

    g.setColour(juce::Colour(0xff112631));
    g.fillRect(0, getHeight() - foot, getWidth(), foot);

    if (midiDragOver)
    {
        const int kb = (!focusMode && keyboardVisible) ? synthKeyboardHeight() : 0;
        g.setColour(juce::Colour(0xffff8a22).withAlpha(0.18f));
        g.fillRect(getLocalBounds().withTrimmedTop(header).withTrimmedBottom(foot + kb));
        g.setColour(juce::Colour(0xffff8a22));
        g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        g.drawText("DROP MIDI TO LOAD", getLocalBounds(), juce::Justification::centred);
    }
}

void MainComponent::resized()
{
    const int margin = uiDensity == 0 ? 6 : 8;
    const int header = headerHeight();
    const int foot = footerHeight();
    const int kbH = (! focusMode && keyboardVisible) ? synthKeyboardHeight() : 0;
    const int dockH = (! focusMode && ! instrumentDock.isFloating()) ? instrumentDockHeight() : 0;
    const int dockGap = dockH > 0 ? 4 : 0;

    auto pageBounds = getLocalBounds().withTrimmedTop(header).withTrimmedBottom(foot + kbH + dockH + dockGap);
    pageBounds = pageBounds.reduced(margin, margin);

    const int inspectorW = focusMode ? 0 : (uiDensity == 0 ? 148 : (uiDensity == 1 ? 164 : 180));
    inspectorPanel = {};
    if (inspectorW > 0 && pageBounds.getWidth() > 700)
    {
        inspectorPanel = pageBounds.removeFromRight(inspectorW);
        pageBounds.removeFromRight(6);
    }
    sequencerPanel = pageBounds;

    // One dense transport row: global controls left, workspace tabs right.
    const int hy = juce::jmax(4, (header - (uiDensity == 0 ? 22 : 26)) / 2);
    const int hh = header - 2 * hy;
    const int hg = uiDensity == 0 ? 2 : 3;
    int x = header + 2;
    auto placeLeft = [&](juce::Component& c, int w)
    {
        c.setBounds(x, hy, w, hh);
        x += w + hg;
    };
    projectName.setBounds(x, hy, uiDensity == 0 ? 82 : 96, hh); x += projectName.getWidth() + hg;
    placeLeft(fileMenuButton, 34);
    placeLeft(labMenuButton, 30);
    placeLeft(resetButton, 29);
    placeLeft(playButton, 40);
    placeLeft(recordButton, 34);
    placeLeft(mixWindowButton, 42);
    placeLeft(keyboardButton, 36);
    placeLeft(densityButton, 34);

    bpmLabel.setBounds(x, hy, 22, hh); x += 22;
    bpm.setBounds(x, hy, uiDensity == 0 ? 74 : 88, hh); x += bpm.getWidth() + hg;
    tapTempoButton.setBounds(x, hy, 32, hh); x += 32 + hg;
    meterLabel.setBounds(x, hy, 30, hh); x += 30;
    meterBox.setBounds(x, hy, 48, hh); x += 48 + hg;
    meterTransformBox.setBounds(x, hy, uiDensity == 0 ? 58 : 68, hh);

    const int tabsW = 44 + hg + 46;
    int px = getWidth() - margin - tabsW;
    auto placeTab = [&](juce::Component& c, int w)
    {
        c.setBounds(px, hy, w, hh);
        px += w + hg;
    };
    placeTab(pageT1Button, 44);
    placeTab(pageSongButton, 46);
#if GROOVELAB_LABS_ENSEMBLE
    // No EDIT/PERFORM toggle: SEQ is the single create/record workspace.
    seqEditButton.setVisible(false);
    seqPerformButton.setVisible(false);
#endif

    // Context inspector: only contextual actions, never another full editor.
    const bool showInspector = ! focusMode && ! inspectorPanel.isEmpty();
    for (auto* c : { static_cast<juce::Component*>(&contextTitle), static_cast<juce::Component*>(&contextDetail),
                     static_cast<juce::Component*>(&contextHint), static_cast<juce::Component*>(&contextPrimary),
                     static_cast<juce::Component*>(&contextSecondary) })
        c->setVisible(showInspector);
    if (showInspector)
    {
        auto ci = inspectorPanel.reduced(9, 8);
        contextTitle.setBounds(ci.removeFromTop(20));
        contextDetail.setBounds(ci.removeFromTop(36));
        ci.removeFromTop(3);
        contextHint.setBounds(ci.removeFromTop(42));
        auto buttons = ci.removeFromBottom(28);
        contextPrimary.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2).reduced(1));
        contextSecondary.setBounds(buttons.reduced(1));
        contextTitle.toFront(false); contextDetail.toFront(false); contextHint.toFront(false);
        contextPrimary.toFront(false); contextSecondary.toFront(false);
    }

    // Workspace visibility is selection-driven.
    const bool seqEdit = currentPage == 1;
    torsoPage.setVisible(seqEdit);
    songPage.setVisible(currentPage == 2);
#if GROOVELAB_LABS_ENSEMBLE
    ensembleView.setVisible(false);
#endif

    const bool melodic = ! groove::unifiedTrackIsDrum(engine.state().selectedTarget);
    if (seqEdit)
    {
        const int lane = melodic ? groove::unifiedTrackMidiLane(engine.state().selectedTarget) : 0;
        pianoRoll.setLane(lane);
        pianoRoll.setVisible(true);
        // One visual language for drums and instruments: note/key view above,
        // rhythm/generator controls below. Drum rows map to their MIDI notes.
        torsoPage.setCompactMelodicMode(true);
        auto split = pageBounds;
        // Drums need a taller rhythm/step editor; melodic needs more piano-roll.
        const float noteFrac = focusMode ? 0.62f : (melodic ? 0.56f : 0.42f);
        const int noteH = juce::jmax(melodic ? 190 : 150, (int) (split.getHeight() * noteFrac));
        auto notes = split.removeFromTop(noteH);
        split.removeFromTop(5);
        pianoRoll.setBounds(notes);
        torsoPage.setBounds(split);
        pianoRoll.toFront(false);
        torsoPage.toFront(false);
        pianoRoll.refresh();
    }
    else
    {
        pianoRoll.setVisible(false);
        torsoPage.setCompactMelodicMode(false);
        torsoPage.setBounds(pageBounds);
        songPage.setBounds(pageBounds);
#if GROOVELAB_LABS_ENSEMBLE
        ensembleView.setBounds(pageBounds);
#endif
    }
#if GROOVELAB_LABS_ENSEMBLE
    ensembleView.setVisible(false);
#endif

    // Persistent global track row + optional keyboard consume no space in focus mode.
    synthKeyboard.setVisible(! focusMode && keyboardVisible);
    instrumentDock.setVisible(! focusMode);
    if (! focusMode && keyboardVisible)
    {
        const int keyboardY = getHeight() - foot - kbH;
        synthKeyboard.setBounds(margin, keyboardY, getWidth() - 2 * margin, kbH);
    }
    if (! focusMode)
    {
        if (! instrumentDock.isFloating())
        {
            const int dockY = getHeight() - foot - kbH - dockH - dockGap;
            instrumentDockedBounds = { margin, dockY, getWidth() - 2 * margin, dockH };
            instrumentDock.setBounds(instrumentDockedBounds);
        }
        else if (instrumentDock.getWidth() < 100)
            instrumentDock.setBounds(juce::jmax(12, getWidth() - 960), header + 6,
                                     juce::jmin(930, getWidth() - 24), instrumentDockHeight());
        instrumentDock.toFront(false);
        if (keyboardVisible) synthKeyboard.toFront(false);
    }

    footer.setBounds(10, getHeight() - foot + 1, getWidth() - 20, foot - 2);
    refreshContextInspector();

    // Legacy GRID lab controls are unused in the SEQ/SONG workspace — keep them
    // invisible and out of hit-testing so they cannot stack over TorsoPage.
    setLabPageVisible(false);
    for (auto* c : std::initializer_list<juce::Component*>{
            &auditionButton, &clearLocks, &deleteNote,
            &soundScope, &soundScopeLabel, &velocity, &midiNote, &probability, &ratchet, &role,
            &trackSteps, &trackPulses, &trackRotate, &trackDivision, &selectedLabel})
        c->setBounds({});
    for (auto& b : lockChips) b.setBounds({});
    for (auto& sl : soundSliders) sl.setBounds({});
    for (auto& lab : soundLabels) lab.setBounds({});
}

void MainComponent::layoutEvolutionLab()
{
    if (evolutionWindow == nullptr)
        return;
    auto& lab = evolutionWindow->lab;
    auto ep = lab.evolutionPanel.reduced(14);
    ep.removeFromTop(46);
    auto a = ep.removeFromTop(31);
    similarity.setBounds(a.withTrimmedLeft(95));
    auto b = ep.removeFromTop(35);
    surprise.setBounds(b.withTrimmedLeft(95).removeFromLeft(70));
    auto c = ep.removeFromTop(35);
    lockResistance.setBounds(c.withTrimmedLeft(95));
    auto d = ep.removeFromTop(35);
    evolveAmount.setBounds(d.withTrimmedLeft(95));
    auto buttons = ep.removeFromBottom(70);
    const int bw = juce::jmax(58, buttons.getWidth() / 5);
    sparse.setBounds(buttons.removeFromLeft(bw).reduced(2));
    syncopate.setBounds(buttons.removeFromLeft(bw).reduced(2));
    human.setBounds(buttons.removeFromLeft(bw).reduced(2));
    dense.setBounds(buttons.removeFromLeft(bw).reduced(2));
    soundEvolve.setBounds(buttons.removeFromLeft(bw).reduced(2));
    evolutionStatus.setBounds(lab.evolutionPanel.getX() + 14,
                              lab.evolutionPanel.getBottom() - 30,
                              lab.evolutionPanel.getWidth() - 28, 20);

    auto rp = lab.rulesPanel.reduced(12);
    rp.removeFromTop(40);
    policyLabel.setBounds(rp.removeFromTop(16));
    evolvePolicy.setBounds(rp.removeFromTop(28));
    rp.removeFromTop(10);
    trackProbLabel.setBounds(rp.removeFromTop(16));
    trackProbability.setBounds(rp.removeFromTop(26));
    rp.removeFromTop(10);
    trackVelLabel.setBounds(rp.removeFromTop(16));
    trackVelocity.setBounds(rp.removeFromTop(26));
}

void MainComponent::showProphetBrowser()
{
    if (! polymaxHost.isLoaded())
    {
        tryLoadPolymax();
        if (! polymaxHost.isLoaded())
            return;
    }
    if (prophetBrowser == nullptr)
    {
        prophetBrowser = std::make_unique<PatchBrowserWindow>(polymaxHost, look, "MAXPOLY  ·  Patches");
        prophetBrowser->browser.onPatchChosen = [this]
        {
            storeCurrentPolyPatch();
            refreshPolyKitUi(true);
            evolutionStatus.setText("MAXPOLY · " + polymaxHost.getCurrentPatchName(),
                                    juce::dontSendNotification);
            engine.saveAutosave();
        };
    }
    prophetBrowser->showAndRefresh();
}

void MainComponent::toggleEvolutionWindow()
{
    if (evolutionWindow == nullptr)
        return;
    evolutionWindow->setVisible(! evolutionWindow->isVisible());
    if (evolutionWindow->isVisible())
    {
        evolutionWindow->toFront(true);
        layoutEvolutionLab();
        evolutionWindow->lab.repaint();
    }
}

void MainComponent::refreshFromSelection()
{
    refreshing=true; const auto& st=engine.state();int t=st.selectedTrack,s=st.selectedStep;auto p=st.effectiveParams(t,s);const auto& ss=st.tracks[t].steps[s];const auto& tr=st.tracks[t];
    selectedLabel.setText("SELECTED: " + groove::voiceName(t) + " · STEP " + juce::String(s+1)
                          + (ss.hasAnyLock() ? " · STEP SOUND LOCKED" : " · follows VOICE SOUND"),
                               juce::dontSendNotification);
    soundSliders[(int)groove::Param::pitch].setValue(p.pitchHz,juce::dontSendNotification);soundSliders[(int)groove::Param::decay].setValue(p.decayMs,juce::dontSendNotification);soundSliders[(int)groove::Param::transient].setValue(p.transient,juce::dontSendNotification);soundSliders[(int)groove::Param::noise].setValue(p.noise,juce::dontSendNotification);soundSliders[(int)groove::Param::filter].setValue(p.filter,juce::dontSendNotification);soundSliders[(int)groove::Param::drive].setValue(p.drive,juce::dontSendNotification);soundSliders[(int)groove::Param::space].setValue(p.space,juce::dontSendNotification);soundSliders[(int)groove::Param::blend].setValue(p.blend,juce::dontSendNotification);
    velocity.setValue(ss.velocity,juce::dontSendNotification);midiNote.setValue((double)st.effectiveMidiNote(t,s),juce::dontSendNotification);probability.setValue(ss.probability,juce::dontSendNotification);ratchet.setSelectedId(ss.ratchet,juce::dontSendNotification);role.setSelectedId((int)ss.role+1,juce::dontSendNotification);
    trackSteps.setSelectedId(tr.generatorSteps,juce::dontSendNotification);trackPulses.setSelectedId(tr.pulses+1,juce::dontSendNotification);int rr=((tr.rotate%tr.generatorSteps)+tr.generatorSteps)%tr.generatorSteps;trackRotate.setSelectedId(rr+1,juce::dontSendNotification);int did=tr.division<0.375f?1:tr.division<0.75f?2:tr.division<1.5f?3:tr.division<3.0f?4:5;trackDivision.setSelectedId(did,juce::dontSendNotification);trackProbability.setValue(tr.probability,juce::dontSendNotification);trackVelocity.setValue(tr.velocity,juce::dontSendNotification);
    evolvePolicy.setSelectedId((int)tr.evolutionPolicy+1,juce::dontSendNotification);evolveAmount.setValue(tr.evolveAmount,juce::dontSendNotification);similarity.setValue(st.similarity,juce::dontSendNotification);lockResistance.setValue(st.lockResistance,juce::dontSendNotification);surprise.setSelectedId(st.surpriseBudget,juce::dontSendNotification);bpm.setValue(st.bpm,juce::dontSendNotification);
    meterBox.setSelectedId((int) st.meter + 1, juce::dontSendNotification);
    meterTransformBox.setSelectedId((int) st.meterTransform + 1, juce::dontSendNotification);
    if (! projectName.hasKeyboardFocus(true))
        projectName.setText(st.name, false);
    mixStrip.setSelectedStep(s);
    for (int i = 0; i < groove::paramCount; ++i)
    {
        auto p = parameterOrder[(size_t) i];
        const bool on = ss.locks[(size_t) p].has_value();
        auto& b = lockChips[(size_t) i];
        b.setColour(juce::TextButton::buttonColourId,
                    on ? juce::Colour(0xff8a6a22) : juce::Colour(0xff152430));
        b.setColour(juce::TextButton::textColourOffId,
                    on ? juce::Colours::white : juce::Colour(0xff8aa0ae));
    }
    refreshing=false;
}

void MainComponent::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (currentPage != 0) return;
    if (! sequencerPanel.contains(e.getPosition())) return;

    auto area = sequencerPanel.reduced(10);
    area.removeFromTop(gridTopPad);
    area.removeFromBottom(gridBottomPad);
    auto labels = area.removeFromLeft(gridLabelWidth);
    const float rh = (float) area.getHeight() / groove::kTracks;

    if (labels.contains(e.getPosition()))
    {
        const int track = juce::jlimit(0, groove::kTracks - 1,
                                       (int) ((e.y - labels.getY()) / rh));
        const int rowY = labels.getY() + (int) (track * rh);
        const auto muteHit = juce::Rectangle<int>(labels.getX(), rowY, 28, (int) rh);
        if (muteHit.contains(e.getPosition()) || e.mods.isCommandDown())
            engine.toggleMute(track);
        else if (e.mods.isAltDown())
            engine.toggleSolo(track);
        else
            engine.selectStep(track, engine.state().selectedStep);
        refreshFromSelection();
        repaint();
        return;
    }

    if (! area.contains(e.getPosition())) return;
    const float sw = (float) area.getWidth() / groove::kSteps;
    const int step = juce::jlimit(0, groove::kSteps - 1, (int) ((e.x - area.getX()) / sw));
    const int track = juce::jlimit(0, groove::kTracks - 1, (int) ((e.y - area.getY()) / rh));
    const bool pureEuclid = engine.state().tracks[(size_t) track].rhythmMode == groove::RhythmMode::euclid;
    engine.selectStep(track, step);

    if (e.mods.isPopupMenu() || e.mods.isRightButtonDown())
    {
        engine.deleteNote(track, step);
    }
    else if (pureEuclid)
    {
        // A normal click in EUCLID selects the generated step so velocity,
        // probability and Param Locks remain fully editable. Double-click is
        // an explicit placement edit and promotes the track to HYBRID.
        if (e.getNumberOfClicks() >= 2)
            engine.toggleStep(track, step);
    }
    else if (! e.mods.isShiftDown())
    {
        engine.toggleStep(track, step);
    }

    refreshFromSelection();
    repaint();
}
void MainComponent::timerCallback()
{
    static int midiPoll = 0;
    playButton.setButtonText(engine.isPlaying()?"STOP":"PLAY");
    recordButton.setToggleState(engine.isRecording(), juce::dontSendNotification);
    meterBox.setSelectedId((int) engine.state().meter + 1, juce::dontSendNotification);
    if (! meterTransformBox.isPopupActive())
        meterTransformBox.setSelectedId((int) engine.state().meterTransform + 1, juce::dontSendNotification);
    if (pluginHost.isLoaded() && ! mixStrip.isDrumPopupActive()) refreshVstKitUi(false);
    if (synthHost.isLoaded() && ! mixStrip.isSynthPopupActive()) refreshSynthKitUi(false);
    if (keysHost.isLoaded() && ! mixStrip.isKeysPopupActive()) refreshKeysKitUi(false);
    if (polymaxHost.isLoaded() && ! mixStrip.isPolyPopupActive()) refreshPolyKitUi(false);

    // Small always-current status strip instead of instructional paragraphs.
    juce::String mode = "STEP";
    const int target = engine.state().selectedTarget;
    if (groove::unifiedTrackIsDrum(target))
    {
        const auto m = engine.state().tracks[(size_t) juce::jlimit(0, groove::kTracks - 1, target)].rhythmMode;
        mode = m == groove::RhythmMode::euclid ? "EUCLID" : (m == groove::RhythmMode::hybrid ? "HYBRID" : "STEP");
    }
    else
    {
        const auto m = engine.state().midiLanes[(size_t) groove::unifiedTrackMidiLane(target)].rhythmMode;
        mode = groove::rhythmModeName(m);
    }
    juce::String q = engine.isRecordQuantize()
        ? juce::String(groove::kQuantizeNoteNames[engine.getRecordQuantizeNote()]) : "FREE";
    juce::String page = currentPage == 1 ? "SEQ" : "SONG";
    juce::String status = selectedTargetName() + "  ·  " + page + "  ·  " + mode
        + "  ·  " + q + "  ·  " + (midiInput != nullptr ? "MIDI IN" : "NO MIDI")
        + (engine.isRecording() ? "  ·  REC" : "") + (focusMode ? "  ·  FOCUS" : "");
    footer.setText(status, juce::dontSendNotification);
    refreshContextInspector();

    if (midiInAuto && (++midiPoll % 60) == 0)
    {
        refreshMidiInputs();
        if (midiInput == nullptr) autoSelectMidiInput();
    }
    if (currentPage == 1)
    {
        repaint();
        if (pianoRoll.isVisible()) pianoRoll.refresh();
        torsoPage.repaint();
    }
    if (evolutionWindow != nullptr && evolutionWindow->isVisible()) evolutionWindow->lab.repaint();
}

void MainComponent::setPage(int page)
{
    currentPage = juce::jlimit(1, 2, page);
    pageT1Button.setToggleState(currentPage == 1, juce::dontSendNotification);
    pageSongButton.setToggleState(currentPage == 2, juce::dontSendNotification);

#if GROOVELAB_LABS_ENSEMBLE
    seqMode = 0;
    seqEditButton.setVisible(false);
    seqPerformButton.setVisible(false);
    ensembleView.setVisible(false);
#endif

    if (currentPage == 1)
    {
        torsoPage.setVisible(true);
        torsoPage.refreshFromEngine();
#if GROOVELAB_LABS_ENSEMBLE
        ensembleView.setVisible(false);
#endif
        songPage.setVisible(false);
    }
    else
    {
        // SONG is a playable workspace too; keep the keyboard available at
        // the bottom when entering it.
        if (! focusMode && ! keyboardVisible)
            setKeyboardVisible(true);
        torsoPage.setVisible(false);
#if GROOVELAB_LABS_ENSEMBLE
        ensembleView.setVisible(false);
#endif
        songPage.setVisible(true);
        songPage.refreshFromEngine();
    }

    setLabPageVisible(false);
    refreshContextInspector();
    resized();
    repaint();
}

void MainComponent::setSeqMode(int mode)
{
    // Legacy entry point retained so older code/projects compile. The former
    // PERFORM/BEAT workspace has been folded into SEQ; recording, drum step
    // entry, Euclidean editing and melodic generation all happen there.
    juce::ignoreUnused(mode);
    seqMode = 0;
    if (currentPage != 1)
        setPage(1);
    torsoPage.setVisible(true);
#if GROOVELAB_LABS_ENSEMBLE
    ensembleView.setVisible(false);
#endif
    torsoPage.refreshFromEngine();
    if (! groove::unifiedTrackIsDrum(engine.state().selectedTarget))
    {
        const int lane = groove::unifiedTrackMidiLane(engine.state().selectedTarget);
        pianoRoll.setLane(lane);
        pianoRoll.refresh();
    }
    refreshContextInspector();
    resized();
    repaint();
}

void MainComponent::setLabPageVisible(bool on)
{
    for (auto* c : std::initializer_list<juce::Component*>{
            &auditionButton, &clearLocks, &deleteNote,
            &soundScope, &soundScopeLabel, &velocity, &midiNote, &probability, &ratchet, &role,
            &trackSteps, &trackPulses, &trackRotate, &trackDivision,
            &selectedLabel, &mixStrip})
        c->setVisible(on);
    for (auto& b : lockChips)
        b.setVisible(on);
}

void MainComponent::applyStoredPluginKit()
{
    if (! pluginHost.isLoaded())
        return;
    const auto& patch = engine.state().lastPluginPatch;
    if (patch.isNotEmpty() && pluginHost.setKitByName(patch))
        return;
}

void MainComponent::refreshVstKitUi(bool rebuildList)
{
    const bool loaded = pluginHost.isLoaded();
    mixStrip.drumSound.setEnabled(loaded);
    mixStrip.drumPrev.setEnabled(loaded);
    mixStrip.drumNext.setEnabled(loaded);

    if (! loaded)
    {
        if (rebuildList || vstKitListCount != 0)
        {
            const juce::ScopedValueSetter<bool> sv(refreshing, true);
            mixStrip.drumSound.clear(juce::dontSendNotification);
            mixStrip.drumSound.addItem("Load VST to browse sounds", 1);
            mixStrip.drumSound.setSelectedId(1, juce::dontSendNotification);
            vstKitListCount = 0;
        }
        return;
    }

    const int n = pluginHost.getKitCount();
    if (rebuildList || n != vstKitListCount)
    {
        const juce::ScopedValueSetter<bool> sv(refreshing, true);
        mixStrip.drumSound.clear(juce::dontSendNotification);
        for (int i = 0; i < n; ++i)
        {
            auto name = pluginHost.getKitName(i);
            if (name.isEmpty())
                name = juce::String(i + 1).paddedLeft('0', 2);
            mixStrip.drumSound.addItem(name, i + 1);
        }
        vstKitListCount = n;
    }

    const int cur = juce::jlimit(0, juce::jmax(0, n - 1), pluginHost.getKitIndex());
    if (mixStrip.drumSound.getSelectedId() != cur + 1)
    {
        const juce::ScopedValueSetter<bool> sv(refreshing, true);
        mixStrip.drumSound.setSelectedId(cur + 1, juce::dontSendNotification);
    }
}

void MainComponent::refreshSynthKitUi(bool rebuildList)
{
    const bool loaded = synthHost.isLoaded();
    mixStrip.synthSound.setEnabled(loaded);
    mixStrip.synthPrev.setEnabled(loaded);
    mixStrip.synthNext.setEnabled(loaded);

    if (! loaded)
    {
        if (rebuildList || synthKitListCount != 0)
        {
            const juce::ScopedValueSetter<bool> sv(refreshing, true);
            mixStrip.synthSound.clear(juce::dontSendNotification);
            mixStrip.synthSound.addItem("Load Mini-Moog to browse sounds", 1);
            mixStrip.synthSound.setSelectedId(1, juce::dontSendNotification);
            synthKitListCount = 0;
        }
        return;
    }

    const int n = synthHost.getKitCount();
    if (rebuildList || n != synthKitListCount)
    {
        const juce::ScopedValueSetter<bool> sv(refreshing, true);
        mixStrip.synthSound.clear(juce::dontSendNotification);
        for (int i = 0; i < n; ++i)
        {
            auto name = synthHost.getKitName(i);
            if (name.isEmpty())
                name = juce::String(i + 1).paddedLeft('0', 2);
            mixStrip.synthSound.addItem(name, i + 1);
        }
        synthKitListCount = n;
    }

    const int cur = juce::jlimit(0, juce::jmax(0, n - 1), synthHost.getKitIndex());
    if (mixStrip.synthSound.getSelectedId() != cur + 1)
    {
        const juce::ScopedValueSetter<bool> sv(refreshing, true);
        mixStrip.synthSound.setSelectedId(cur + 1, juce::dontSendNotification);
    }
}

void MainComponent::applyStoredSynthPatch()
{
    if (! synthHost.isLoaded())
        return;
    const auto& patch = engine.state().lastSynthPatch;
    if (patch.isNotEmpty() && synthHost.setKitByName(patch))
        return;
    if (! synthHost.setKitByName("Default"))
        synthHost.setKitIndex(0);
    engine.state().lastSynthPatch = synthHost.getCurrentPatchName();
    if (engine.state().lastSynthPatch.isEmpty())
        engine.state().lastSynthPatch = synthHost.getKitName(synthHost.getKitIndex());
}

void MainComponent::storeCurrentKeysPatch()
{
    if (! keysHost.isLoaded())
        return;
    auto name = keysHost.getCurrentPatchName();
    if (name.isEmpty())
        name = keysHost.getKitName(keysHost.getKitIndex());
    engine.state().lastElectraPatch = name;
}

void MainComponent::applyStoredKeysPatch()
{
    if (! keysHost.isLoaded())
        return;
    const auto& patch = engine.state().lastElectraPatch;
    if (patch.isNotEmpty() && keysHost.setKitByName(patch))
        return;
    if (! keysHost.setKitByName("Default"))
        keysHost.setKitIndex(0);
    storeCurrentKeysPatch();
}

void MainComponent::storeCurrentPolyPatch()
{
    if (! polymaxHost.isLoaded())
        return;
    auto name = polymaxHost.getCurrentPatchName();
    if (name.isEmpty())
        name = polymaxHost.getKitName(polymaxHost.getKitIndex());
    engine.state().lastPolymaxPatch = name;
}

void MainComponent::applyStoredPolyPatch()
{
    if (! polymaxHost.isLoaded())
        return;
    const auto& patch = engine.state().lastPolymaxPatch;
    if (patch.isNotEmpty() && polymaxHost.setKitByName(patch))
        return;
    if (! polymaxHost.setKitByName("Init") && ! polymaxHost.setKitByName("Default"))
        polymaxHost.setKitIndex(0);
    storeCurrentPolyPatch();
}

void MainComponent::refreshKeysKitUi(bool rebuildList)
{
    const bool loaded = keysHost.isLoaded();
    mixStrip.keysSound.setEnabled(loaded);
    mixStrip.keysPrev.setEnabled(loaded);
    mixStrip.keysNext.setEnabled(loaded);

    if (! loaded)
    {
        if (rebuildList || keysKitListCount != 0)
        {
            const juce::ScopedValueSetter<bool> sv(refreshing, true);
            mixStrip.keysSound.clear(juce::dontSendNotification);
            mixStrip.keysSound.addItem("Load Electra 88", 1);
            mixStrip.keysSound.setSelectedId(1, juce::dontSendNotification);
            keysKitListCount = 0;
        }
        return;
    }

    const int n = keysHost.getKitCount();
    if (rebuildList || n != keysKitListCount)
    {
        const juce::ScopedValueSetter<bool> sv(refreshing, true);
        mixStrip.keysSound.clear(juce::dontSendNotification);
        for (int i = 0; i < n; ++i)
        {
            auto name = keysHost.getKitName(i);
            if (name.isEmpty())
                name = juce::String(i + 1).paddedLeft('0', 2);
            mixStrip.keysSound.addItem(name, i + 1);
        }
        keysKitListCount = n;
    }

    const int cur = juce::jlimit(0, juce::jmax(0, n - 1), keysHost.getKitIndex());
    if (mixStrip.keysSound.getSelectedId() != cur + 1)
    {
        const juce::ScopedValueSetter<bool> sv(refreshing, true);
        mixStrip.keysSound.setSelectedId(cur + 1, juce::dontSendNotification);
    }
}

void MainComponent::refreshPolyKitUi(bool rebuildList)
{
    const bool loaded = polymaxHost.isLoaded();
    mixStrip.polySound.setEnabled(loaded);
    mixStrip.polyPrev.setEnabled(loaded);
    mixStrip.polyNext.setEnabled(loaded);
    mixStrip.polyBrowse.setEnabled(loaded);

    if (! loaded)
    {
        if (rebuildList || polyKitListCount != 0)
        {
            const juce::ScopedValueSetter<bool> sv(refreshing, true);
            mixStrip.polySound.setButtonText("Load MAXPOLY");
            polyKitListCount = 0;
        }
        return;
    }

    const int n = polymaxHost.getKitCount();
    auto name = polymaxHost.getCurrentPatchName();
    if (name.isEmpty())
        name = polymaxHost.getKitName(polymaxHost.getKitIndex());
    if (name.isEmpty())
        name = "MAXPOLY SOUND";
    if (rebuildList || n != polyKitListCount
        || mixStrip.polySound.getButtonText() != name)
    {
        mixStrip.polySound.setButtonText(name);
        polyKitListCount = n;
    }
}
