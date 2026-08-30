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
    explicit AudioPreferencesPanel(juce::AudioDeviceManager& dm)
        : selector(dm, 0, 0, 2, 8, false, false, true, true)
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
        setSize(560, 440);
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
    juce::Label heading, hint;
    juce::AudioDeviceSelectorComponent selector;
};
}

MainComponent::MainComponent()
{
    setLookAndFeel(&look);
    setOpaque(true);
    setWantsKeyboardFocus(true);
    setFocusContainerType(juce::Component::FocusContainerType::focusContainer);

    projectName.setText(engine.state().name);
    projectName.setFont(juce::FontOptions(14.0f, juce::Font::bold));
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

    addSmallLabel(bpmLabel, "BPM");
    bpm.setRange(40.0, 260.0, 1.0);
    bpm.setSliderStyle(juce::Slider::LinearHorizontal);
    bpm.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 44, 24);
    bpm.setSliderSnapsToMousePosition(true);
    bpm.setMouseDragSensitivity(180);
    bpm.onValueChange=[this]{ if(!refreshing){ engine.state().bpm=bpm.getValue(); mixStrip.setBpm(engine.state().bpm); engine.saveAutosave(); }};
    addAndMakeVisible(bpm);
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

    for (auto* b : {&playButton,&resetButton,&captureButton,&backButton,&auditionButton,&performButton,&commitPerformButton,
                    &clearLocks,&sparse,&syncopate,&human,&dense,&soundEvolve,&pageGridButton,&pageT1Button,&pageSongButton,&saveButton,&saveAsButton,&loadButton,&evolveButton}) addAndMakeVisible(*b);

    pageGridButton.setClickingTogglesState(true);
    pageT1Button.setClickingTogglesState(true);
    pageSongButton.setClickingTogglesState(true);
    pageGridButton.setRadioGroupId(42);
    pageT1Button.setRadioGroupId(42);
    pageSongButton.setRadioGroupId(42);
    pageGridButton.setToggleState(true, juce::dontSendNotification);
    pageGridButton.onClick = [this] { setPage(0); };
    pageT1Button.onClick = [this] { setPage(1); };
    pageSongButton.onClick = [this] { setPage(2); };
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
    polymaxHost.setEditorIdentity("PROPHET  ·  Prophet 5", { 190, 190 });
    polymaxHost.setPluginMidiChannel(1);

    addAndMakeVisible(torsoPage);
    torsoPage.setVisible(false);
    torsoPage.onPatternChanged = [this]
    {
        refreshFromSelection();
        songPage.refreshFromEngine();
    };
    torsoPage.onStatusMessage = [this](const juce::String& text)
    {
        evolutionStatus.setText(text, juce::dontSendNotification);
    };

    addAndMakeVisible(songPage);
    songPage.setVisible(false);
    songPage.onChannelClicked = [this](int ch) { selectMidiChannelFromUi(ch); };
    songPage.onInstrumentUiClicked = [this](int ch) { showInstrumentEditor(ch); };
    songPage.onSongChanged = [this]
    {
        refreshFromSelection();
        torsoPage.refreshFromEngine();
    };

    synthKeyboard.onNoteOn = [this](int note, float velocity)
    {
        auto msg = juce::MidiMessage::noteOn(keyboardMidiChannel(), note, velocity);
        if (engine.isRecording())
            engine.pushIncomingMidi(msg);
        midiCollector.addMessageToQueue(msg);
    };
    synthKeyboard.onNoteOff = [this](int note)
    {
        auto msg = juce::MidiMessage::noteOff(keyboardMidiChannel(), note);
        if (engine.isRecording())
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
    applyLiveMidiChannel(keyboardMidiChannel());

    mixStrip.onChannelClicked = [this](int ch) { selectMidiChannelFromUi(ch); };
    mixStrip.onChanged = [this]
    {
        mixStrip.saveTo(engine.state().mix);
        pushMixToDsp();
        engine.saveAutosave();
    };
    addAndMakeVisible(mixStrip);

    playButton.onClick=[this]{ toggleTransport(); };
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
    mixStrip.polySound.onChange = [this]
    {
        if (refreshing) return;
        const int id = mixStrip.polySound.getSelectedId();
        if (id <= 0) return;
        polymaxHost.setKitIndex(id - 1);
        storeCurrentPolyPatch();
        evolutionStatus.setText("PROPHET · " + polymaxHost.getKitName(id - 1), juce::dontSendNotification);
        engine.saveAutosave();
    };
    mixStrip.polyPrev.onClick = [this]
    {
        polymaxHost.stepKit(-1);
        storeCurrentPolyPatch();
        refreshPolyKitUi(false);
        evolutionStatus.setText("PROPHET · " + polymaxHost.getKitName(polymaxHost.getKitIndex()),
                                juce::dontSendNotification);
        engine.saveAutosave();
    };
    mixStrip.polyNext.onClick = [this]
    {
        polymaxHost.stepKit(1);
        storeCurrentPolyPatch();
        refreshPolyKitUi(false);
        evolutionStatus.setText("PROPHET · " + polymaxHost.getKitName(polymaxHost.getKitIndex()),
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

    clearLocks.onClick=[this]
    {
        engine.clearAllLocks(engine.state().selectedTrack,engine.state().selectedStep);
        evolutionStatus.setText("Selected step sound reset to voice", juce::dontSendNotification);
        refreshFromSelection();
        repaint();
    };
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
    applyLoadedSession();
    mixStrip.addKeyListener(this);
    mixStrip.loadFrom(engine.state().mix);
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

    setSize(1680, 996); refreshFromSelection(); refreshVstKitUi(true); refreshSynthKitUi(true); refreshKeysKitUi(true); refreshPolyKitUi(true); startTimerHz(30);
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
    mixBus.prepare(sampleRate, samplesPerBlockExpected);
    const int n = juce::jmax(samplesPerBlockExpected, 512);
    drumStem.setSize(2, n, false, false, true);
    synthStem.setSize(2, n, false, false, true);
    keysStem.setSize(2, n, false, false, true);
    polyStem.setSize(2, n, false, false, true);
}

void MainComponent::releaseResources()
{
    pluginHost.release();
    synthHost.release();
    keysHost.release();
    polymaxHost.release();
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
    mixBus.busComp.store(m.busComp);
    mixBus.busDelay.store(m.busDelay);
    mixBus.masterVol.store(m.masterVol);
    mixStrip.setBpm(engine.state().bpm);
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
    if (drumStem.getNumSamples() < n || drumStem.getNumChannels() < 2)
        drumStem.setSize(2, juce::jmax(n, 512), false, false, true);
    if (synthStem.getNumSamples() < n || synthStem.getNumChannels() < 2)
        synthStem.setSize(2, juce::jmax(n, 512), false, false, true);
    if (keysStem.getNumSamples() < n || keysStem.getNumChannels() < 2)
        keysStem.setSize(2, juce::jmax(n, 512), false, false, true);
    if (polyStem.getNumSamples() < n || polyStem.getNumChannels() < 2)
        polyStem.setSize(2, juce::jmax(n, 512), false, false, true);

    juce::AudioBuffer<float> drums(drumStem.getArrayOfWritePointers(), 2, n);
    juce::AudioBuffer<float> synth(synthStem.getArrayOfWritePointers(), 2, n);
    juce::AudioBuffer<float> keys(keysStem.getArrayOfWritePointers(), 2, n);
    juce::AudioBuffer<float> poly(polyStem.getArrayOfWritePointers(), 2, n);
    drums.clear();
    synth.clear();
    keys.clear();
    poly.clear();

    juce::MidiBuffer midi;
    engine.process(drums, midi);

    juce::MidiBuffer drumMidi, synthMidi;
    splitDrumAndSynthMidi(midi, drumMidi, synthMidi);
    juce::MidiBuffer liveMidi, keysMidi, polyMidi, laneMidi;
    midiCollector.removeNextBlockOfMessages(liveMidi, n);
    engine.takeLaneMidi(laneMidi);
    routeLiveMidi(liveMidi, drumMidi, synthMidi, keysMidi, polyMidi);
    routeLiveMidi(laneMidi, drumMidi, synthMidi, keysMidi, polyMidi);

    if (midiOutput != nullptr)
    {
        juce::MidiBuffer hardwareOut;
        hardwareOut.addEvents(midi, 0, n, 0);
        hardwareOut.addEvents(liveMidi, 0, n, 0);
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

    groove::MixBus::applyStemGain(drums, mixBus.drumVol.load(), mixBus.drumLeft.load(), mixBus.drumRight.load());
    groove::MixBus::applyStemGain(synth, mixBus.synthVol.load(), mixBus.synthLeft.load(), mixBus.synthRight.load());
    groove::MixBus::applyStemGain(keys, mixBus.keysVol.load(), mixBus.keysLeft.load(), mixBus.keysRight.load());
    groove::MixBus::applyStemGain(poly, mixBus.polyVol.load(), mixBus.polyLeft.load(), mixBus.polyRight.load());

    view.clear();
    const int outCh = view.getNumChannels();
    for (int ch = 0; ch < outCh; ++ch)
    {
        view.addFrom(ch, 0, drums, juce::jmin(ch, 1), 0, n);
        view.addFrom(ch, 0, synth, juce::jmin(ch, 1), 0, n);
        view.addFrom(ch, 0, keys, juce::jmin(ch, 1), 0, n);
        view.addFrom(ch, 0, poly, juce::jmin(ch, 1), 0, n);
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
                                + "  ·  CH1 drums  ·  CH2 moog  ·  CH3 prophet  ·  CH4 keys",
                            juce::dontSendNotification);
}

void MainComponent::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
{
    juce::MidiMessage routed = message;
    if (uiChannelLocked.load() && routed.getChannel() > 0)
        routed = groove::withMidiChannel(routed, liveMidiChannel.load());

    if (engine.isRecording())
        engine.pushIncomingMidi(routed);
    midiCollector.addMessageToQueue(routed);

    const int ch = routed.getChannel();
    const bool noteMsg = routed.isNoteOnOrOff();
    const int n = noteMsg ? routed.getNoteNumber() : -1;
    const bool down = noteMsg && routed.isNoteOn() && routed.getVelocity() > 0;
    const bool followHardware = ! uiChannelLocked.load();
    const bool highlight = followHardware
        && ((routed.isNoteOn() && routed.getVelocity() > 0)
            || routed.isController() || routed.isProgramChange());
    juce::Component::SafePointer<SynthKeyboard> kb(&synthKeyboard);
    juce::Component::SafePointer<MainComponent> self(this);
    juce::MessageManager::callAsync([kb, self, n, down, ch, noteMsg, highlight]
    {
        if (kb != nullptr && noteMsg)
            kb->setExternalHeld(n, down);
        if (self == nullptr || ! highlight)
            return;
        self->applyLiveMidiChannel(ch);
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
        plugins.addItem(1000, "Browse for plugin...");
        menu.addSubMenu("Load UJAM / VST", plugins);

        const bool loaded = pluginHost.isLoaded();
        menu.addItem(220, loaded ? ("Show " + pluginHost.getName() + " UI") : "Show Plugin UI",
                     loaded, pluginHost.isEditorOpen());
        menu.addSeparator();
        const bool synthLoaded = synthHost.isLoaded();
        menu.addItem(221, synthLoaded ? ("Show " + synthHost.getName() + " UI") : "Show Mini-Moog UI",
                     synthLoaded, synthHost.isEditorOpen());
        menu.addItem(222, "Load Mini-Moog");
        menu.addItem(1100, "Browse for synth...");
        menu.addSeparator();
        const bool keysLoaded = keysHost.isLoaded();
        menu.addItem(223, keysLoaded ? ("Show " + keysHost.getName() + " UI") : "Show Keys UI",
                     keysLoaded, keysHost.isEditorOpen());
        menu.addItem(224, "Load Electra 88", true, keysLoaded);
        const bool polyLoaded = polymaxHost.isLoaded();
        menu.addItem(225, polyLoaded ? ("Show " + polymaxHost.getName() + " UI") : "Show Prophet 5 UI",
                     polyLoaded, polymaxHost.isEditorOpen());
        menu.addItem(227, "Load Prophet 5", true, polyLoaded);
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
    if (menuItemID == 1100) { browseForSynth(); return; }
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

juce::File MainComponent::findMiniMoogFile()
{
    const juce::File candidates[] = {
        juce::File("/Library/Audio/Plug-Ins/VST3/uaudio_minimoog.vst3"),
        juce::File("/Library/Audio/Plug-Ins/Components/uaudio_minimoog.component"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Library/Audio/Plug-Ins/VST3/uaudio_minimoog.vst3"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Library/Audio/Plug-Ins/Components/uaudio_minimoog.component")
    };
    for (const auto& file : candidates)
        if (file.exists())
            return file;
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
        juce::File("/Library/Audio/Plug-Ins/VST3/Prophet 5.vst3"),
        juce::File("/Library/Audio/Plug-Ins/Components/Prophet 5.component"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Library/Audio/Plug-Ins/VST3/Prophet 5.vst3"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Library/Audio/Plug-Ins/Components/Prophet 5.component")
    };
    for (const auto& file : candidates)
        if (file.exists())
            return file;
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
    if (! file.exists() || file.getFileName().containsIgnoreCase("polymax"))
        file = findPolymaxFile();
    if (! file.exists())
    {
        evolutionStatus.setText("Prophet 5 not found", juce::dontSendNotification);
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
        evolutionStatus.setText("PROPHET · " + polymaxHost.getName() + " · CH"
                                    + juce::String(groove::kMidiChPoly),
                                juce::dontSendNotification);
        polymaxHost.showEditor();
        applyStoredPolyPatch();
        refreshPolyKitUi(true);
    }
    else
    {
        evolutionStatus.setText("Prophet 5 failed: " + error, juce::dontSendNotification);
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
    mixStrip.setActiveMidiChannel(ch);
    songPage.setActiveMidiChannel(ch);

    int target = -1;
    juce::String dest = "CH" + juce::String(ch);
    if (ch == groove::kMidiChDrums) { target = 3; dest = "DRUMS"; }
    else if (ch == groove::kMidiChMoog) { target = 0; dest = "MOOG"; }
    else if (ch == groove::kMidiChPoly) { target = 2; dest = "PROPHET"; }
    else if (ch == groove::kMidiChKeys) { target = 1; dest = "KEYS"; }

    if (target >= 0)
        synthKeyboard.setTarget(target);

    evolutionStatus.setText("MIDI · CH" + juce::String(ch) + "  →  " + dest,
                            juce::dontSendNotification);
}

void MainComponent::selectMidiChannelFromUi(int channel)
{
    const int ch = juce::jlimit(1, 16, channel > 0 ? channel : groove::kMidiChMoog);
    if (ch != liveMidiChannel.load() && engine.isRecording())
        engine.setRecording(false);
    uiChannelLocked.store(true);
    applyLiveMidiChannel(ch);
    sendArturiaChannelChange(liveMidiChannel.load());
    if (currentPage == 2)
        songPage.refreshFromEngine();
}

void MainComponent::ensureArturiaMidiOutput()
{
    if (midiOutput != nullptr)
        return;
    refreshMidiOutputs();
    int arturia = -1;
    for (int i = 0; i < midiDevices.size(); ++i)
    {
        const auto& name = midiDevices[i].name;
        if (isVirtualMidiName(name))
            continue;
        if (looksLikeKeyboardMidi(name))
        {
            arturia = i;
            break;
        }
    }
    if (arturia >= 0)
        setMidiOutput(arturia);
}

void MainComponent::sendArturiaChannelChange(int channel)
{
    const int ch = juce::jlimit(1, 16, channel > 0 ? channel : 1);
    const int note = (ch == groove::kMidiChDrums) ? 36 : 60;
    auto on = juce::MidiMessage::noteOn(ch, note, (juce::uint8) 100);
    auto off = juce::MidiMessage::noteOff(ch, note);
    on.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    off.setTimeStamp(on.getTimeStamp() + 0.12);
    midiCollector.addMessageToQueue(on);
    midiCollector.addMessageToQueue(off);

    ensureArturiaMidiOutput();
    if (midiOutput == nullptr)
    {
        evolutionStatus.setText("MIDI · CH" + juce::String(ch)
                                    + " selected  ·  no Arturia MIDI out",
                                juce::dontSendNotification);
        return;
    }
    midiOutput->sendMessageNow(juce::MidiMessage::noteOn(ch, note, (juce::uint8) 100));
    juce::Timer::callAfterDelay(120, [this, ch, note]
    {
        if (midiOutput != nullptr)
            midiOutput->sendMessageNow(juce::MidiMessage::noteOff(ch, note));
    });
    juce::String dest = "CH" + juce::String(ch);
    if (ch == groove::kMidiChDrums) dest = "DRUMS";
    else if (ch == groove::kMidiChMoog) dest = "MOOG";
    else if (ch == groove::kMidiChPoly) dest = "PROPHET";
    else if (ch == groove::kMidiChKeys) dest = "KEYS";
    const juce::String outName = (midiOutIndex >= 0 && midiOutIndex < midiDevices.size())
        ? midiDevices[midiOutIndex].name : "MIDI out";
    evolutionStatus.setText("MIDI · CH" + juce::String(ch) + "  →  " + dest
                                + "  ·  locked  ·  sent to " + outName,
                            juce::dontSendNotification);
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

void MainComponent::routeLiveMidi(const juce::MidiBuffer& live,
                                 juce::MidiBuffer& drums, juce::MidiBuffer& moog,
                                 juce::MidiBuffer& keys, juce::MidiBuffer& poly) const
{
    for (const auto metadata : live)
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
    tryLoadKeys();
    tryLoadPolymax();
    applyLiveMidiChannel(keyboardMidiChannel());
    showAllInstrumentEditors();
    repaint();
}

void MainComponent::showAllInstrumentEditors()
{
    if (pluginHost.isLoaded())
        pluginHost.showEditor();
    if (synthHost.isLoaded())
        synthHost.showEditor();
    if (keysHost.isLoaded())
        keysHost.showEditor();
    if (polymaxHost.isLoaded())
        polymaxHost.showEditor();
}

void MainComponent::showInstrumentEditor(int channel)
{
    if (channel == groove::kMidiChDrums)
    {
        if (pluginHost.isLoaded())
            pluginHost.showEditor();
        else
            evolutionStatus.setText("DRUMS · load a kit first (Sound menu)", juce::dontSendNotification);
        return;
    }
    if (channel == groove::kMidiChMoog)
    {
        if (synthHost.isLoaded())
            synthHost.showEditor();
        else
            tryLoadMiniMoog();
        return;
    }
    if (channel == groove::kMidiChPoly)
    {
        if (polymaxHost.isLoaded())
            polymaxHost.showEditor();
        else
            tryLoadPolymax();
        return;
    }
    if (channel == groove::kMidiChKeys)
    {
        if (keysHost.isLoaded())
            keysHost.showEditor();
        else
            tryLoadKeys();
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
    applyLoadedSession();
    evolutionStatus.setText("NEW GROOVE", juce::dontSendNotification);
}

void MainComponent::showLoadMenu()
{
    juce::PopupMenu menu;
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

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&loadButton),
        [this, files](int result)
        {
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

void MainComponent::showPreferences()
{
    auto* panel = new AudioPreferencesPanel(deviceManager);
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

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        toggleTransport();
        return true;
    }
    if (key.getKeyCode() == 'm' || key.getKeyCode() == 'M')
    {
        engine.toggleMute(engine.state().selectedTrack);
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
            bool outside=s>=tr.generatorSteps, gen=!outside&&engine.isGeneratedHit(t,s), resolved=!outside&&engine.isResolvedHit(t,s), selected=t==state.selectedTrack&&s==state.selectedStep, play=s==engine.currentStepForTrack(t);
            if(outside) g.setColour(juce::Colour(0xff091118));
            else if(silent) g.setColour(resolved ? juce::Colour(0xff2a333a) : juce::Colour(0xff12181c));
            else if(resolved) g.setColour(c.withAlpha(st.overrideMode==groove::StepOverrideMode::forceOn?1.0f:0.72f));
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
    g.fillAll(juce::Colour(0xff050c12)); g.setColour(juce::Colour(0xff08131c)); g.fillRect(0,0,getWidth(),72);
    {
        auto icon = juce::ImageCache::getFromMemory(BinaryData::GrooveLabIcon_png,
                                                    BinaryData::GrooveLabIcon_pngSize);
        g.drawImageWithin(icon, 12, 12, 48, 48, juce::RectanglePlacement::centred);
    }
    g.setColour(juce::Colour(0xffe6f0f5));g.setFont(juce::FontOptions(16.0f,juce::Font::bold));g.drawText("Lil God Projector",66,16,236,34,juce::Justification::centredLeft);

    if (currentPage == 0)
    {
        drawPanel(g,sequencerPanel,"SEQUENCER · " + juce::String(groove::meterName(engine.state().meter))
                  + " · " + juce::String(groove::meterTransformName(engine.state().meterTransform))
                  + "  ·  DOT MUTES ROW · ⌥ SOLO");
        drawPanel(g,inspectorPanel,"TRACK GENERATOR / STEP EDIT");
        drawPanel(g,soundPanel,"MIX");
        drawGrid(g);
        g.setColour(juce::Colour(0xff829bab)); g.setFont(juce::FontOptions(10.0f));
        auto ip = inspectorPanel.reduced(14); ip.removeFromTop(68);
        g.drawText("STEPS", ip.getX(), ip.getY(), 68, 24, juce::Justification::centredLeft); ip.removeFromTop(28);
        g.drawText("PULSES", ip.getX(), ip.getY(), 68, 24, juce::Justification::centredLeft); ip.removeFromTop(28);
        g.drawText("ROTATE", ip.getX(), ip.getY(), 68, 24, juce::Justification::centredLeft); ip.removeFromTop(28);
        g.drawText("DIVISION", ip.getX(), ip.getY(), 68, 24, juce::Justification::centredLeft);
    }

    g.setColour(juce::Colour(0xff112631));g.fillRect(0,getHeight()-34,getWidth(),34);

    if (midiDragOver)
    {
        g.setColour(juce::Colour(0xffff8a22).withAlpha(0.18f));
        g.fillRect(getLocalBounds().withTrimmedTop(72).withTrimmedBottom(34 + synthKeyboardH));
        g.setColour(juce::Colour(0xffff8a22));
        g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
        g.drawText("DROP UJAM PHRASE TO LOAD GRID",
                   getLocalBounds(), juce::Justification::centred);
    }
}

void MainComponent::resized()
{
    int margin=12,header=72,foot=34,rightW=330,gap=10,lowerH=240; auto content=getLocalBounds().withTrimmedTop(header).withTrimmedBottom(foot + synthKeyboardH).reduced(margin,8); auto right=content.removeFromRight(rightW);content.removeFromRight(gap);auto bottom=content.removeFromBottom(lowerH);content.removeFromBottom(gap);sequencerPanel=content;inspectorPanel=right;soundPanel=bottom;

    const int hy = 19, hh = 32, hg = 6;
    int x = 308;
    auto placeLeft = [&](juce::Component& c, int w)
    {
        c.setBounds(x, hy, w, hh);
        x += w + hg;
    };
    projectName.setBounds(x, hy, 128, hh); x += 128 + hg;
    placeLeft(saveButton, 48);
    placeLeft(saveAsButton, 70);
    placeLeft(loadButton, 48);
    placeLeft(captureButton, 64);
    placeLeft(backButton, 50);
    placeLeft(evolveButton, 64);
    placeLeft(resetButton, 52);
    placeLeft(playButton, 60);
    placeLeft(performButton, 72);
    placeLeft(commitPerformButton, 80);
    bpmLabel.setBounds(x, hy, 28, hh);
    x += 28 + 2;
    int bpmW = 118;

    const int pageW = 50 + hg + 88 + hg + 50;
    int rightNeed = pageW;
    int meterW = 58;
    int transformW = 82;
    int leftEnd = x + bpmW + 4 + 40 + 4 + meterW + 4 + transformW;
    int startPages = getWidth() - 12 - rightNeed;
    if (startPages < leftEnd + 8)
    {
        bpmW = juce::jmax(72, bpmW - (leftEnd + 8 - startPages));
        leftEnd = x + bpmW + 4 + 40 + 4 + meterW + 4 + transformW;
        startPages = juce::jmax(leftEnd + 8, getWidth() - 12 - rightNeed);
    }
    bpm.setBounds(x, hy, bpmW, hh);
    x += bpmW + 4;
    meterLabel.setBounds(x, hy, 40, hh);
    x += 40 + 4;
    meterBox.setBounds(x, hy, meterW, hh);
    x += meterW + 4;
    meterTransformBox.setBounds(x, hy, transformW, hh);

    int px = startPages;
    auto place = [&](juce::Component& c, int w)
    {
        c.setBounds(px, hy, w, hh);
        px += w + hg;
    };
    place(pageGridButton, 50);
    place(pageT1Button, 88);
    place(pageSongButton, 50);

    torsoPage.setBounds(getLocalBounds().withTrimmedTop(72).withTrimmedBottom(34 + synthKeyboardH));
    songPage.setBounds(getLocalBounds().withTrimmedTop(72).withTrimmedBottom(34 + synthKeyboardH));
    if (currentPage == 2)
        songPage.toFront(false);
    synthKeyboard.setBounds(8, getHeight() - 34 - synthKeyboardH, getWidth() - 16, synthKeyboardH);
    synthKeyboard.toFront(false);
    footer.setBounds(20,getHeight()-31,getWidth()-40,26);
    if (currentPage != 0)
        return;

    auto ins=inspectorPanel.reduced(14);ins.removeFromTop(42);selectedLabel.setBounds(ins.removeFromTop(26));
    auto gen=ins.removeFromTop(128);
    auto r1=gen.removeFromTop(28);trackSteps.setBounds(r1.withTrimmedLeft(75)); auto r2=gen.removeFromTop(28);trackPulses.setBounds(r2.withTrimmedLeft(75)); auto r3=gen.removeFromTop(28);trackRotate.setBounds(r3.withTrimmedLeft(75)); auto r4=gen.removeFromTop(28);trackDivision.setBounds(r4.withTrimmedLeft(75));
    auto lp = ins.removeFromTop(35);
    soundScopeLabel.setBounds(lp.removeFromLeft(78));
    soundScope.setBounds(lp.removeFromLeft(82));
    lp.removeFromLeft(6);
    clearLocks.setBounds(lp.removeFromLeft(138));
    auditionButton.setBounds(inspectorPanel.getX()+14,inspectorPanel.getBottom()-44,100,28);

    auto sf=sequencerPanel.reduced(12).removeFromBottom(36);velocity.setBounds(sf.removeFromLeft(78));sf.removeFromLeft(4);midiNote.setBounds(sf.removeFromLeft(78));sf.removeFromLeft(4);probability.setBounds(sf.removeFromLeft(82));sf.removeFromLeft(8);ratchet.setBounds(sf.removeFromLeft(68));sf.removeFromLeft(8);role.setBounds(sf.removeFromLeft(94));

    auto sp=soundPanel.reduced(8, 4);
    sp.removeFromTop(22); // painted MIX title
    mixStrip.setBounds(sp);
    mixStrip.toFront(false);

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
    engine.selectStep(track, step);
    if (! e.mods.isShiftDown())
        engine.toggleStep(track, step);
    refreshFromSelection();
    repaint();
}
void MainComponent::timerCallback()
{
    static int midiPoll = 0;
    playButton.setButtonText(engine.isPlaying()?"STOP":"PLAY");
    meterBox.setSelectedId((int) engine.state().meter + 1, juce::dontSendNotification);
    if (! meterTransformBox.isPopupActive())
        meterTransformBox.setSelectedId((int) engine.state().meterTransform + 1, juce::dontSendNotification);
    if (pluginHost.isLoaded() && ! mixStrip.isDrumPopupActive())
        refreshVstKitUi(false);
    if (synthHost.isLoaded() && ! mixStrip.isSynthPopupActive())
        refreshSynthKitUi(false);
    if (keysHost.isLoaded() && ! mixStrip.isKeysPopupActive())
        refreshKeysKitUi(false);
    if (polymaxHost.isLoaded() && ! mixStrip.isPolyPopupActive())
        refreshPolyKitUi(false);
    if (engine.isRecording())
    {
        juce::String part = "PART";
        const auto& song = engine.state().song;
        if (! song.sections.empty())
        {
            const int i = juce::jlimit(0, (int) song.sections.size() - 1, song.current);
            part = groove::songPartName(song.sections[(size_t) i].part);
        }
        footer.setText("REC · " + part + "  ·  play pads  ·  REC off commits  ·  KEEP TAKE stores a version",
                       juce::dontSendNotification);
    }
    else if (footer.getText().startsWith("REC"))
    {
        footer.setText("SAVE stores this groove  ·  SAVE AS makes a named copy  ·  ⌘S save  ·  ⌘⇧S save as  ·  ⌘O load",
                       juce::dontSendNotification);
    }
    if (midiInAuto && (++midiPoll % 60) == 0)
    {
        refreshMidiInputs();
        if (midiInput == nullptr)
            autoSelectMidiInput();
    }
    if (currentPage == 0)
        repaint();
    if (evolutionWindow != nullptr && evolutionWindow->isVisible())
        evolutionWindow->lab.repaint();
}

void MainComponent::setPage(int page)
{
    currentPage = juce::jlimit(0, 2, page);
    pageGridButton.setToggleState(currentPage == 0, juce::dontSendNotification);
    pageT1Button.setToggleState(currentPage == 1, juce::dontSendNotification);
    pageSongButton.setToggleState(currentPage == 2, juce::dontSendNotification);
    torsoPage.setVisible(currentPage == 1);
    songPage.setVisible(currentPage == 2);
    if (currentPage == 1)
        torsoPage.refreshFromEngine();
    else if (currentPage == 2)
        songPage.refreshFromEngine();
    else
        refreshFromSelection();
    setLabPageVisible(currentPage == 0);
    resized();
    repaint();
}

void MainComponent::setLabPageVisible(bool on)
{
    for (auto* c : std::initializer_list<juce::Component*>{
            &auditionButton, &clearLocks,
            &soundScope, &soundScopeLabel, &velocity, &midiNote, &probability, &ratchet, &role,
            &trackSteps, &trackPulses, &trackRotate, &trackDivision,
            &selectedLabel, &mixStrip})
        c->setVisible(on);
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
    if (! polymaxHost.setKitByName("Default"))
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

    if (! loaded)
    {
        if (rebuildList || polyKitListCount != 0)
        {
            const juce::ScopedValueSetter<bool> sv(refreshing, true);
            mixStrip.polySound.clear(juce::dontSendNotification);
            mixStrip.polySound.addItem("Load Prophet 5", 1);
            mixStrip.polySound.setSelectedId(1, juce::dontSendNotification);
            polyKitListCount = 0;
        }
        return;
    }

    const int n = polymaxHost.getKitCount();
    if (rebuildList || n != polyKitListCount)
    {
        const juce::ScopedValueSetter<bool> sv(refreshing, true);
        mixStrip.polySound.clear(juce::dontSendNotification);
        for (int i = 0; i < n; ++i)
        {
            auto name = polymaxHost.getKitName(i);
            if (name.isEmpty())
                name = juce::String(i + 1).paddedLeft('0', 2);
            mixStrip.polySound.addItem(name, i + 1);
        }
        polyKitListCount = n;
    }

    const int cur = juce::jlimit(0, juce::jmax(0, n - 1), polymaxHost.getKitIndex());
    if (mixStrip.polySound.getSelectedId() != cur + 1)
    {
        const juce::ScopedValueSetter<bool> sv(refreshing, true);
        mixStrip.polySound.setSelectedId(cur + 1, juce::dontSendNotification);
    }
}
