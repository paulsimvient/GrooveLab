#include "MainComponent.h"
#include "../Audio/DrumMidi.h"
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

    projectLabel.setText("GROOVE 01", juce::dontSendNotification);
    projectLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd8eaf4));
    projectLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    addAndMakeVisible(projectLabel);

    addSmallLabel(bpmLabel, "BPM");
    bpm.setRange(40.0, 260.0, 1.0);
    bpm.setSliderStyle(juce::Slider::LinearHorizontal);
    bpm.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 44, 24);
    bpm.setSliderSnapsToMousePosition(true);
    bpm.setMouseDragSensitivity(180);
    bpm.onValueChange=[this]{ if(!refreshing){ engine.state().bpm=bpm.getValue(); engine.saveAutosave(); }};
    addAndMakeVisible(bpm);

    for (auto* b : {&playButton,&resetButton,&captureButton,&backButton,&auditionButton,&performButton,&commitPerformButton,
                    &clearLocks,&sparse,&syncopate,&human,&dense,&soundEvolve,&pageGridButton,&pageT1Button,&prefsButton}) addAndMakeVisible(*b);

    pageGridButton.setClickingTogglesState(true);
    pageT1Button.setClickingTogglesState(true);
    pageGridButton.setRadioGroupId(42);
    pageT1Button.setRadioGroupId(42);
    pageGridButton.setToggleState(true, juce::dontSendNotification);
    pageGridButton.onClick = [this] { setT1View(false); };
    pageT1Button.onClick = [this] { setT1View(true); };
    prefsButton.onClick = [this] { showPreferences(); };

    soundSource.addItem("INTERNAL", 1);
    soundSource.addItem("UJAM / VST", 2);
    soundSource.addItem("BOTH", 3);
    soundSource.setSelectedId(1, juce::dontSendNotification);
    soundSource.onChange = [this]
    {
        const int id = soundSource.getSelectedId();
        soundMode.store(id);
        engine.setInternalSynthEnabled(id != 2);
        if (id >= 2 && ! pluginHost.isLoaded())
            evolutionStatus.setText("Click LOAD VST — UJAM is listed as Virtual Drummer / Beatmaker", juce::dontSendNotification);
    };
    loadPluginButton.onClick = [this] { choosePluginFile(); };
    showPluginButton.onClick = [this]
    {
        if (pluginHost.isLoaded())
            pluginHost.showEditor();
        else
            choosePluginFile();
    };
    addAndMakeVisible(soundSource);
    addAndMakeVisible(loadPluginButton);
    addAndMakeVisible(showPluginButton);
    addAndMakeVisible(midiOutBox);
    refreshMidiOutputs();
    midiOutBox.onChange = [this]
    {
        midiOutput.reset();
        const int id = midiOutBox.getSelectedId();
        if (id > 1 && id - 2 < midiDevices.size())
            midiOutput = juce::MidiOutput::openDevice(midiDevices[id - 2].identifier);
    };

    addAndMakeVisible(torsoPage);
    torsoPage.setVisible(false);
    torsoPage.onPatternChanged = [this]
    {
        refreshFromSelection();
    };
    torsoPage.onStatusMessage = [this](const juce::String& text)
    {
        evolutionStatus.setText(text, juce::dontSendNotification);
    };

    playButton.onClick=[this]{ toggleTransport(); };
    resetButton.onClick=[this]{ engine.resetTransport(); repaint(); };
    auditionButton.onClick=[this]{ engine.auditionSelected(); };
    captureButton.onClick=[this]{ evolutionStatus.setText("Captured node "+juce::String(engine.capture("manual")),juce::dontSendNotification); repaint(); };
    backButton.onClick=[this]{ auto ok=engine.back(); evolutionStatus.setText(ok?"Returned to parent":"No parent",juce::dontSendNotification); refreshFromSelection(); repaint(); };

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

    clearLocks.onClick=[this]
    {
        engine.clearAllLocks(engine.state().selectedTrack,engine.state().selectedStep);
        evolutionStatus.setText("Selected step sound reset to voice", juce::dontSendNotification);
        refreshFromSelection();
        repaint();
    };
    addSmallLabel(selectedLabel,"SELECTED"); selectedLabel.setFont(juce::FontOptions(13.0f,juce::Font::bold));
    addSmallLabel(evolutionStatus,"READY");
    addSmallLabel(footer,"MIDI to UJAM: velocity · CC7 volume · CC11 expression · CC20–27 sound · drop a phrase to load the grid");

    for(int i=0;i<groove::paramCount;++i)
    {
        auto p=parameterOrder[(size_t)i]; addSmallLabel(soundLabels[(size_t)i],displayName(p));
        auto& sl=soundSliders[(size_t)i];
        if(p==groove::Param::pitch) configureRotary(sl,30,1600,1);
        else if(p==groove::Param::decay) configureRotary(sl,20,1800,1);
        else configureRotary(sl,0,1,0.01);
        bindSoundSlider(sl,p); addAndMakeVisible(sl);
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

    auto settingsXml = juce::XmlDocument::parse(audioSettingsFile());
    setAudioChannels(0, 2, settingsXml.get());
    deviceManager.addChangeListener(this);
    setSize(1600,900); refreshFromSelection(); startTimerHz(30);
    playButton.setButtonText(engine.isPlaying()?"STOP":"PLAY");
}

MainComponent::~MainComponent()
{
    deviceManager.removeChangeListener(this);
    saveAudioSettings();
    shutdownAudio();
    pluginHost.hideEditor();
    setLookAndFeel(nullptr);
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    engine.prepare(sampleRate, samplesPerBlockExpected);
    pluginHost.prepare(sampleRate, samplesPerBlockExpected);
}

void MainComponent::releaseResources()
{
    pluginHost.release();
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& info)
{
    auto view = juce::AudioBuffer<float>(info.buffer->getArrayOfWritePointers(),
                                         info.buffer->getNumChannels(),
                                         info.startSample,
                                         info.numSamples);
    juce::MidiBuffer midi;
    engine.process(view, midi);

    const int mode = soundMode.load();
    if (mode >= 2)
        pluginHost.process(view, midi, mode == 2);

    if (midiOutput != nullptr)
        midiOutput->sendBlockOfMessagesNow(midi);
}

void MainComponent::choosePluginFile()
{
    juce::PopupMenu menu;
    juce::Array<juce::File> files;

    const auto vst3Dir = juce::File("/Library/Audio/Plug-Ins/VST3");
    const auto auDir = juce::File("/Library/Audio/Plug-Ins/Components");

    auto addUjam = [&](const juce::String& stem, const juce::String& label)
    {
        const auto vst3 = vst3Dir.getChildFile(stem + ".vst3");
        const auto au = auDir.getChildFile(stem + ".component");
        const auto file = vst3.exists() ? vst3 : au;
        if (! file.exists())
            return;
        files.add(file);
        menu.addItem(files.size(), label);
    };

    addUjam("VD-BRUTE",   "Virtual Drummer Brute");
    addUjam("VD-DEEP",    "Virtual Drummer Deep");
    addUjam("VD-HEAVY",   "Virtual Drummer Heavy");
    addUjam("VD-HOT",     "Virtual Drummer Hot");
    addUjam("VD-LEGEND",  "Virtual Drummer Legend");
    addUjam("VD-SOLID",   "Virtual Drummer Solid");
    addUjam("BM-CIRCUITS","Beatmaker Circuits");

    if (files.isEmpty())
        menu.addItem(999, "(No UJAM plugins found)", false, false);

    menu.addSeparator();
    menu.addItem(1000, "Browse for plugin...");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&loadPluginButton),
        [this, files](int result)
        {
            if (result == 1000)
            {
                browseForPlugin();
                return;
            }
            if (result >= 1 && result <= files.size())
                loadPluginFromFile(files[result - 1]);
        });
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
        soundSource.setSelectedId(2, juce::sendNotification);
        evolutionStatus.setText("PLUGIN · " + pluginHost.getName(), juce::dontSendNotification);
        pluginHost.showEditor();
    }
    else
    {
        evolutionStatus.setText("Plugin load failed: " + error, juce::dontSendNotification);
    }
}

void MainComponent::refreshMidiOutputs()
{
    midiOutBox.clear();
    midiDevices = juce::MidiOutput::getAvailableDevices();
    midiOutBox.addItem("MIDI OUT: Off", 1);
    for (int i = 0; i < midiDevices.size(); ++i)
        midiOutBox.addItem(midiDevices[i].name, i + 2);
    midiOutBox.setSelectedId(1, juce::dontSendNotification);
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
    if (auto xml = deviceManager.createStateXml())
        xml->writeTo(audioSettingsFile());
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    saveAudioSettings();
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

void MainComponent::filesDropped(const juce::StringArray& files, int, int)
{
    midiDragOver = false;
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
            if (t1View)
                torsoPage.refreshFromEngine();
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
    if (key.getKeyCode() == 's' || key.getKeyCode() == 'S')
    {
        if (key.getModifiers().isCommandDown())
            return false;
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
    return false;
}

bool MainComponent::keyPressed(const juce::KeyPress& key, juce::Component*)
{
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
    for(int s=0;s<groove::kSteps;++s) if(s%4==0){ g.setColour(juce::Colour(0xff728b99)); g.setFont(juce::FontOptions(9.0f)); g.drawText(juce::String(s+1),area.getX()+(int)(s*sw),area.getY()-19,30,16,juce::Justification::centredLeft); }
    for(int t=0;t<groove::kTracks;++t)
    {
        int y=area.getY()+(int)(t*rh); auto lr=juce::Rectangle<int>(labels.getX(),y,labels.getWidth()-6,(int)rh-3); auto c=trackColour(t);
        const auto& tr=state.tracks[t];
        g.setColour(t==state.selectedTrack?juce::Colour(0xff102536):juce::Colour(0xff0b1923));
        if (! state.trackIsAudible(t)) g.setColour(juce::Colour(0xff071016));
        g.fillRoundedRectangle(lr.toFloat(),4);
        g.setColour(tr.soloed?juce::Colour(0xffffd76a):c); g.fillEllipse((float)lr.getX()+10,(float)lr.getCentreY()-4,8,8);
        if (tr.muted){ g.setColour(juce::Colour(0xffff6363)); g.setFont(juce::FontOptions(10.0f,juce::Font::bold)); g.drawText("M",lr.removeFromRight(16),juce::Justification::centred); }
        else if (tr.soloed){ g.setColour(juce::Colour(0xffffd76a)); g.setFont(juce::FontOptions(10.0f,juce::Font::bold)); g.drawText("S",lr.removeFromRight(16),juce::Justification::centred); }
        g.setColour(state.trackIsAudible(t)?c:juce::Colour(0xff4a5a66));
        g.setFont(juce::FontOptions(11.0f,juce::Font::bold));
        g.drawText(juce::String(t+1)+"   "+groove::voiceName(t)+" "+groove::ujamKitName(state.effectiveMidiNote(t,state.selectedStep))+"   "+juce::String(tr.pulses)+"/"+juce::String(tr.generatorSteps),lr.withTrimmedLeft(26),juce::Justification::centredLeft);
        for(int s=0;s<groove::kSteps;++s)
        {
            juce::Rectangle<float> pad(area.getX()+s*sw+1.5f,(float)y+2,sw-3,rh-6); const auto& st=tr.steps[s];
            bool outside=s>=tr.generatorSteps, gen=!outside&&engine.isGeneratedHit(t,s), resolved=!outside&&engine.isResolvedHit(t,s), selected=t==state.selectedTrack&&s==state.selectedStep, play=s==engine.currentStepForTrack(t);
            if(outside) g.setColour(juce::Colour(0xff091118)); else if(resolved) g.setColour(c.withAlpha(st.overrideMode==groove::StepOverrideMode::forceOn?1.0f:0.72f)); else g.setColour(juce::Colour(0xff17252f)); g.fillRoundedRectangle(pad,2.5f);
            if(gen && !resolved){ g.setColour(c.withAlpha(0.22f)); g.fillRoundedRectangle(pad.reduced(2),2); }
            g.setColour(selected?juce::Colours::white:play?juce::Colour(0xff7bcaff):juce::Colour(0xff253d4b)); g.drawRoundedRectangle(pad,2.5f,selected?2.0f:play?1.5f:0.8f);
            if(st.overrideMode==groove::StepOverrideMode::forceOn){ g.setColour(juce::Colour(0xffffffff)); g.fillEllipse(pad.getX()+2,pad.getY()+2,3,3); }
            if(st.overrideMode==groove::StepOverrideMode::forceOff){ g.setColour(juce::Colour(0xffff6363)); g.drawLine(pad.getX()+3,pad.getY()+3,pad.getRight()-3,pad.getBottom()-3,1.5f); }
            if(st.hasAnyLock()){ g.setColour(juce::Colour(0xffffbf4d)); g.fillEllipse(pad.getRight()-5,pad.getY()+2,3,3); }
            if(st.role==groove::StepRole::anchor){ g.setColour(juce::Colour(0xffffd576)); g.drawLine(pad.getX()+3,pad.getBottom()-3,pad.getRight()-3,pad.getBottom()-3,1.4f); }
            if(st.ratchet>1){ g.setColour(juce::Colour(0xffe4f6ff)); g.setFont(juce::FontOptions(7.0f)); g.drawText(juce::String(st.ratchet),pad.toNearestInt().reduced(2),juce::Justification::bottomLeft); }
        }
    }
}

void MainComponent::drawAncestry(juce::Graphics& g,juce::Rectangle<int> r)
{
    const auto& nodes=engine.ancestry().nodes(); int current=engine.ancestry().currentNodeId(); r.removeFromTop(42); r=r.reduced(14);
    if(nodes.empty()){ g.setColour(juce::Colour(0xff698494)); g.drawText("Capture or evolve to create lineage.",r,juce::Justification::centred); return; }
    int n=juce::jmin(7,(int)nodes.size()), cx=r.getCentreX(), top=r.getY()+15;
    for(int i=0;i<n;++i){ const auto& node=nodes[nodes.size()-1-(size_t)i]; int y=top+i*34,x=cx+(i%2==0?-28:28); if(i>0){g.setColour(juce::Colour(0xff35566a));g.drawLine((float)cx,(float)(y-24),(float)x,(float)y,1);} auto box=juce::Rectangle<int>(x-20,y-13,40,26); g.setColour(node.id==current?juce::Colour(0xff194c6b):juce::Colour(0xff111f29));g.fillRoundedRectangle(box.toFloat(),10);g.setColour(node.id==current?juce::Colour(0xff7ac8ff):juce::Colour(0xff587486));g.drawRoundedRectangle(box.toFloat(),10,1);g.setColour(juce::Colour(0xffd8e8f1));g.drawText(juce::String(node.id),box,juce::Justification::centred); }
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff050c12)); g.setColour(juce::Colour(0xff08131c)); g.fillRect(0,0,getWidth(),72);
    g.setColour(juce::Colour(0xffff8a22));g.setFont(juce::FontOptions(26.0f,juce::Font::bold));g.drawText("G",22,17,32,34,juce::Justification::centred);
    g.setColour(juce::Colour(0xffe6f0f5));g.setFont(juce::FontOptions(20.0f,juce::Font::bold));g.drawText("GROOVE LAB",61,16,170,34,juce::Justification::centredLeft);

    if (! t1View)
    {
        drawPanel(g,sequencerPanel,"SEQUENCER · CLICK LABEL TO SELECT · ⌘ MUTE · ⌥ SOLO"); drawPanel(g,inspectorPanel,"TRACK GENERATOR / STEP EDIT"); drawPanel(g,soundPanel,"SOUND"); drawPanel(g,evolutionPanel,"EVOLUTION"); drawPanel(g,rulesPanel,"RULES"); drawPanel(g,ancestryPanel,"ANCESTRY"); drawGrid(g); drawAncestry(g,ancestryPanel);
        g.setColour(juce::Colour(0xff829bab)); g.setFont(juce::FontOptions(10.0f));
        auto ip = inspectorPanel.reduced(14); ip.removeFromTop(68);
        g.drawText("STEPS", ip.getX(), ip.getY(), 68, 24, juce::Justification::centredLeft); ip.removeFromTop(28);
        g.drawText("PULSES", ip.getX(), ip.getY(), 68, 24, juce::Justification::centredLeft); ip.removeFromTop(28);
        g.drawText("ROTATE", ip.getX(), ip.getY(), 68, 24, juce::Justification::centredLeft); ip.removeFromTop(28);
        g.drawText("DIVISION", ip.getX(), ip.getY(), 68, 24, juce::Justification::centredLeft);
        auto rp = rulesPanel.reduced(14); rp.removeFromTop(38);
        g.drawText("POLICY", rp.getX(), rp.getY(), 80, 18, juce::Justification::centredLeft); rp.removeFromTop(42);
        g.drawText("TRACK PROB", rp.getX(), rp.getY(), 90, 18, juce::Justification::centredLeft); rp.removeFromTop(34);
        g.drawText("TRACK VEL", rp.getX(), rp.getY(), 90, 18, juce::Justification::centredLeft);
    }

    g.setColour(juce::Colour(0xff112631));g.fillRect(0,getHeight()-34,getWidth(),34);

    if (midiDragOver)
    {
        g.setColour(juce::Colour(0xffff8a22).withAlpha(0.18f));
        g.fillRect(getLocalBounds().withTrimmedTop(72).withTrimmedBottom(34));
        g.setColour(juce::Colour(0xffff8a22));
        g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
        g.drawText("DROP UJAM PHRASE TO LOAD GRID",
                   getLocalBounds(), juce::Justification::centred);
    }
}

void MainComponent::resized()
{
    int margin=12,header=72,foot=34,rightW=330,gap=10,lowerH=282; auto content=getLocalBounds().withTrimmedTop(header).withTrimmedBottom(foot).reduced(margin,8); auto right=content.removeFromRight(rightW);content.removeFromRight(gap);auto bottom=content.removeFromBottom(lowerH);content.removeFromBottom(gap);sequencerPanel=content;inspectorPanel=right.removeFromTop(500);right.removeFromTop(gap);ancestryPanel=right;
    auto left=bottom.removeFromLeft((int)(bottom.getWidth()*0.46f));bottom.removeFromLeft(gap);auto mid=bottom.removeFromLeft((int)(bottom.getWidth()*0.46f));bottom.removeFromLeft(gap);soundPanel=left;evolutionPanel=mid;rulesPanel=bottom;

    const int hy = 19, hh = 32, hg = 8;
    int x = 238;
    auto placeLeft = [&](juce::Component& c, int w)
    {
        c.setBounds(x, hy, w, hh);
        x += w + hg;
    };
    projectLabel.setBounds(x, 18, 82, 34); x += 82 + hg;
    placeLeft(captureButton, 74);
    placeLeft(backButton, 56);
    placeLeft(resetButton, 56);
    placeLeft(playButton, 70);
    placeLeft(performButton, 80);
    placeLeft(commitPerformButton, 102);
    bpmLabel.setBounds(x, hy, 32, hh);
    x += 32 + 4;
    bpm.setBounds(x, hy, 168, hh);
    const int leftEnd = x + 168;

    int midiW = 150, srcW = 110;
    int rightNeed = 54 + hg + 52 + hg + 94 + hg + srcW + hg + 78 + hg + 56 + hg + midiW;
    int spare = getWidth() - 12 - leftEnd - 12 - rightNeed;
    if (spare < 0)
    {
        const int cut = juce::jmin(-spare, midiW - 108);
        midiW -= cut;
        spare += cut;
        if (spare < 0)
        {
            const int cutSrc = juce::jmin(-spare, srcW - 92);
            srcW -= cutSrc;
        }
    }

    int rx = getWidth() - 12;
    auto placeRight = [&](juce::Component& c, int w)
    {
        rx -= w;
        c.setBounds(rx, hy, w, hh);
        rx -= hg;
    };
    placeRight(midiOutBox, midiW);
    placeRight(showPluginButton, 56);
    placeRight(loadPluginButton, 78);
    placeRight(soundSource, srcW);
    placeRight(pageT1Button, 94);
    placeRight(pageGridButton, 52);
    placeRight(prefsButton, 54);

    if (prefsButton.getX() < leftEnd + 10)
        prefsButton.setBounds(leftEnd + 10, hy, 54, hh);

    torsoPage.setBounds(getLocalBounds().withTrimmedTop(72).withTrimmedBottom(34));
    footer.setBounds(20,getHeight()-31,getWidth()-40,26);
    if (t1View)
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

    auto sp=soundPanel.reduced(12);sp.removeFromTop(45);int colW=juce::jmax(58,sp.getWidth()/groove::paramCount);for(int i=0;i<groove::paramCount;++i){auto col=juce::Rectangle<int>(sp.getX()+i*colW,sp.getY(),colW,sp.getHeight());soundLabels[(size_t)i].setBounds(col.getX(),sp.getY(),colW,18);soundSliders[(size_t)i].setBounds(col.getX(),sp.getY()+22,colW,108);}

    auto ep=evolutionPanel.reduced(14);ep.removeFromTop(46); auto a=ep.removeFromTop(31);similarity.setBounds(a.withTrimmedLeft(95));auto b=ep.removeFromTop(35);surprise.setBounds(b.withTrimmedLeft(95).removeFromLeft(70));auto c=ep.removeFromTop(35);lockResistance.setBounds(c.withTrimmedLeft(95));auto d=ep.removeFromTop(35);evolveAmount.setBounds(d.withTrimmedLeft(95));auto buttons=ep.removeFromBottom(70);int bw=juce::jmax(58,buttons.getWidth()/5);sparse.setBounds(buttons.removeFromLeft(bw).reduced(2));syncopate.setBounds(buttons.removeFromLeft(bw).reduced(2));human.setBounds(buttons.removeFromLeft(bw).reduced(2));dense.setBounds(buttons.removeFromLeft(bw).reduced(2));soundEvolve.setBounds(buttons.removeFromLeft(bw).reduced(2));evolutionStatus.setBounds(evolutionPanel.getX()+14,evolutionPanel.getBottom()-30,evolutionPanel.getWidth()-28,20);

    auto rp=rulesPanel.reduced(14);rp.removeFromTop(48);evolvePolicy.setBounds(rp.removeFromTop(30));rp.removeFromTop(8);trackProbability.setBounds(rp.removeFromTop(34));trackVelocity.setBounds(rp.removeFromTop(34));
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
    evolvePolicy.setSelectedId((int)tr.evolutionPolicy+1,juce::dontSendNotification);evolveAmount.setValue(tr.evolveAmount,juce::dontSendNotification);similarity.setValue(st.similarity,juce::dontSendNotification);lockResistance.setValue(st.lockResistance,juce::dontSendNotification);surprise.setSelectedId(st.surpriseBudget,juce::dontSendNotification);bpm.setValue(st.bpm,juce::dontSendNotification);refreshing=false;
}

void MainComponent::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (t1View) return;
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
        if (e.mods.isCommandDown())
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
    playButton.setButtonText(engine.isPlaying()?"STOP":"PLAY");
    if (! t1View)
        repaint();
}

void MainComponent::setT1View(bool on)
{
    t1View = on;
    pageGridButton.setToggleState(! on, juce::dontSendNotification);
    pageT1Button.setToggleState(on, juce::dontSendNotification);
    torsoPage.setVisible(on);
    if (on)
        torsoPage.refreshFromEngine();
    else
        refreshFromSelection();
    setLabPageVisible(! on);
    resized();
    repaint();
}

void MainComponent::setLabPageVisible(bool on)
{
    for (auto* c : std::initializer_list<juce::Component*>{
            &auditionButton, &clearLocks, &sparse, &syncopate, &human, &dense, &soundEvolve,
            &soundScope, &soundScopeLabel, &velocity, &midiNote, &probability, &ratchet, &role,
            &trackSteps, &trackPulses, &trackRotate, &trackDivision, &trackProbability, &trackVelocity,
            &evolvePolicy, &evolveAmount, &similarity, &lockResistance, &surprise,
            &selectedLabel, &evolutionStatus})
        c->setVisible(on);

    for (int i = 0; i < groove::paramCount; ++i)
    {
        soundSliders[(size_t)i].setVisible(on);
        soundLabels[(size_t)i].setVisible(on);
    }
}
