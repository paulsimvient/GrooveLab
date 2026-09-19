#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <vector>
#include "../Audio/GrooveEngine.h"
#include "../Audio/DrumMidi.h"
#include "../Audio/ExternalPluginHost.h"
#include "../Audio/MixBus.h"
#include "GrooveLookAndFeel.h"
#include "TorsoPage.h"
#include "SongPage.h"
#include "SynthKeyboard.h"
#include "MixStrip.h"
#include "EvolutionLab.h"
#include "PatchBrowser.h"
#include "PianoRoll.h"
#include "InstrumentBrowser.h"
#include "InstrumentDock.h"
#if GROOVELAB_LABS_ENSEMBLE
#include "../Labs/Ensemble/EnsembleView.h"
#endif

class MixerWindow;

class MainComponent : public juce::AudioAppComponent,
                      public juce::MenuBarModel,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer,
                      private juce::KeyListener,
                      private juce::ChangeListener,
                      private juce::MidiInputCallback
{
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo&) override;
    void releaseResources() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;

private:
    void timerCallback() override;
    void refreshFromSelection();
    void runEvolution(groove::EvolutionEngine::Mode);
    void configureRotary(juce::Slider&, double min, double max, double step);
    void configureLinear(juce::Slider&, double min, double max, double step);
    void bindSoundSlider(juce::Slider&, groove::Param);
    void addSmallLabel(juce::Label&, const juce::String&);
    void drawPanel(juce::Graphics&, juce::Rectangle<int>, const juce::String&);
    void drawGrid(juce::Graphics&);
    void layoutEvolutionLab();
    void toggleEvolutionWindow();
    void showProphetBrowser();
    void refreshMidiInputs();
    void autoSelectMidiInput();
    static bool isVirtualMidiName(const juce::String&);
    static bool looksLikeKeyboardMidi(const juce::String&);
    void toggleTransport();
    void setPage(int page); // 1 SEQ, 2 SONG
    void setSeqMode(int mode); // legacy: SEQ is now the only create/record workspace
    void setLabPageVisible(bool on);
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;
    void browseForPlugin();
    void browseForSynth();
    void loadPluginFromFile(const juce::File&);
    void loadSynthFromFile(const juce::File&);
    void tryLoadUjamHot();
    void tryLoadMiniMoog();
    void tryLoadKeys();
    void tryLoadPolymax();
    static juce::File findMiniMoogFile();
    static juce::File findElectraFile();
    static juce::File findPolymaxFile();
    void splitDrumAndSynthMidi(const juce::MidiBuffer& src,
                               juce::MidiBuffer& drums, juce::MidiBuffer& synth) const;
    void routeLiveMidi(const juce::MidiBuffer& live,
                       juce::MidiBuffer& drums, juce::MidiBuffer& moog,
                       juce::MidiBuffer& keys, juce::MidiBuffer& poly) const;
    int keyboardMidiChannel() const;
    void applyLiveMidiChannel(int channel);
    void selectMidiChannelFromUi(int channel);
    void sendArturiaChannelChange(int channel);
    void ensureArturiaMidiOutput();
    void showAllInstrumentEditors();
    void showInstrumentEditor(int channel);
    void showInstrumentBrowser(int channel);
    void loadInstrumentForChannel(int channel, const juce::File&);
    void tryLoadCapitolChambers();
    void tryLoadParadiseGuitarStudio();
    void tryLoadCenturyTube();
    void tryLoadGalaxyTapeEcho();
    void saveInstrumentDefaults();
    void loadInstrumentDefaultsIntoState();
    void routeExternalMidiToSelected(const juce::MidiBuffer& live,
                                     juce::MidiBuffer& drums, juce::MidiBuffer& moog,
                                     juce::MidiBuffer& keys, juce::MidiBuffer& poly) const;
    void routeChannelMidi(const juce::MidiBuffer& src,
                          juce::MidiBuffer& drums, juce::MidiBuffer& moog,
                          juce::MidiBuffer& keys, juce::MidiBuffer& poly) const;
    void refreshMidiOutputs();
    void setSoundMode(int mode);
    void setMidiOutput(int deviceIndex);
    void setMidiInput(int deviceIndex);
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;
    void preferLowLatencyBuffer();
    void showPreferences();
    void saveAudioSettings();
    static juce::File audioSettingsFile();
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    bool importDroppedMidi(const juce::StringArray& files);
    void captureSessionIntoState();
    void applyLoadedSession();
    void saveCurrentGroove();
    void saveGrooveAs();
    void saveGrooveAsFile();
    void showLoadMenu();
    void showFileMenu();
    void showLabMenu();
    void showViewMenu();
    void tapTempo();
    static juce::File findCapitolChambersFile();
    static juce::File findParadiseGuitarStudioFile();
    static juce::File findCenturyTubeFile();
    static juce::File findGalaxyTapeEchoFile();
    static juce::PropertiesFile::Options instrumentPreferenceOptions();
    void loadGrooveFromFile(const juce::File&);
    void newGroove();
    void refreshVstKitUi(bool rebuildList);
    void refreshSynthKitUi(bool rebuildList);
    void refreshKeysKitUi(bool rebuildList);
    void refreshPolyKitUi(bool rebuildList);
    void applyStoredPluginKit();
    void applyStoredSynthPatch();
    void applyStoredKeysPatch();
    void applyStoredPolyPatch();
    void storeCurrentKeysPatch();
    void storeCurrentPolyPatch();
    void pushMixToDsp();
    void showMixerWindow(bool makeVisible = true);
    void saveMixerWindowBounds();
    void saveMixerWindowBoundsForMode(bool compact);
    void restoreMixerWindowBoundsForMode(bool compact);
    void setKeyboardVisible(bool visible);
    void cycleUiDensity();
    void applyUiDensity();
    void setFocusMode(bool on);
    void refreshContextInspector();
    juce::String selectedTargetName() const;
    int selectedTargetChannel() const;

    groove::GrooveEngine engine;
    groove::ExternalPluginHost pluginHost;
    groove::ExternalPluginHost synthHost;
    groove::ExternalPluginHost keysHost;
    groove::ExternalPluginHost polymaxHost;
    groove::ExternalPluginHost capitolReverbHost;
    groove::ExternalPluginHost paradiseGuitarHost;
    groove::ExternalPluginHost centuryTubeHost;
    groove::ExternalPluginHost galaxyTapeHost;
    juce::dsp::Compressor<float> fxBusCompressor;
    groove::MixBus mixBus;
    juce::AudioBuffer<float> drumStem, synthStem, keysStem, polyStem, fxSendStem, fxReturnStem;
    std::unique_ptr<juce::MidiOutput> midiOutput;
    std::unique_ptr<juce::MidiInput> midiInput;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::Array<juce::MidiDeviceInfo> midiDevices;
    juce::Array<juce::MidiDeviceInfo> midiInDevices;
    juce::Array<juce::File> ujamPluginFiles;
    juce::PopupMenu extraAppleMenu;
    std::atomic<int> soundMode { 2 };
    GrooveLookAndFeel look;
    TorsoPage torsoPage { engine };
    SongPage songPage { engine };
#if GROOVELAB_LABS_ENSEMBLE
    groove::ensemble::EnsembleView ensembleView { engine };
#endif
    SynthKeyboard synthKeyboard;
    SynthKeyboard mixerKeyboard;
    MixStrip mixStrip;
    std::unique_ptr<MixerWindow> mixerWindow;
    PianoRoll pianoRoll { engine };
    InstrumentDock instrumentDock;
    std::unique_ptr<EvolutionWindow> evolutionWindow;
    std::unique_ptr<PatchBrowserWindow> prophetBrowser;
    std::unique_ptr<InstrumentBrowserWindow> instrumentBrowser;

    juce::MidiBuffer engineMidiScratch, drumMidiScratch, synthMidiScratch, liveMidiScratch;
    juce::MidiBuffer keysMidiScratch, polyMidiScratch, laneMidiScratch, hardwareMidiScratch;

    juce::TextButton playButton { "PLAY" };
    juce::TextButton recordButton { "REC" };
    juce::TextButton resetButton { "RST" };
    juce::TextButton captureButton { "CAPTURE" };
    juce::TextButton backButton { "BACK" };
    juce::TextButton auditionButton { "AUDITION" };
    juce::TextButton performButton { "PERF" };
    juce::TextButton commitPerformButton { "KEEP" };
    juce::TextButton pageGridButton { "GRID" };
    juce::TextButton pageT1Button { "SEQ" };
    juce::TextButton pageSongButton { "SONG" };
#if GROOVELAB_LABS_ENSEMBLE
    juce::TextButton seqEditButton { "EDIT" };
    juce::TextButton seqPerformButton { "PERFORM" };
#endif
    juce::TextButton fileMenuButton { "FILE" };
    juce::TextButton labMenuButton { "LAB" };
    juce::TextButton saveButton { "SAVE" };
    juce::TextButton saveAsButton { "SAVE AS" };
    juce::TextButton loadButton { "LOAD" };
    juce::TextButton evolveButton { "EVOLVE" };
    juce::TextButton tapTempoButton { "TAP" };
    juce::TextButton mixWindowButton { "MIXER" };
    juce::TextButton keyboardButton { "KEYS" };
    juce::TextButton densityButton { "UI:C" };
    std::array<double, 5> tapTimesMs {};
    int tapCount = 0;
    int midiOutIndex = -1;
    int midiInIndex = -1;
    juce::String midiInIdentifier;
    bool midiInAuto = true;
    std::atomic<int> liveMidiChannel { groove::kMidiChMoog };
    std::atomic<bool> uiChannelLocked { false };
    juce::Slider bpm;
    juce::Label bpmLabel;
    juce::ComboBox meterBox;
    juce::ComboBox meterTransformBox;
    juce::Label meterLabel;
    juce::TextEditor projectName;
    juce::Label contextTitle, contextDetail, contextHint;
    juce::TextButton contextPrimary { "MUTE" };
    juce::TextButton contextSecondary { "PLUGIN" };

    std::array<juce::Slider, groove::paramCount> soundSliders;
    std::array<juce::Label, groove::paramCount> soundLabels;
    int vstKitListCount = -1;
    int synthKitListCount = -1;
    int keysKitListCount = -1;
    int polyKitListCount = -1;
    // Explicit sound edit scope. STEP is the default: knob movement writes
    // per-step parameter locks. VOICE edits the selected track's base sound.
    juce::ComboBox soundScope;
    juce::TextButton clearLocks { "UNLOCK" };
    juce::TextButton deleteNote { "DELETE NOTE" };
    juce::Label soundScopeLabel;
    std::array<juce::TextButton, groove::paramCount> lockChips;
    juce::Slider velocity, midiNote, probability;
    juce::ComboBox ratchet, role;

    // Algorithmic track generator: Euclidean first-class controls.
    juce::ComboBox trackSteps, trackPulses, trackRotate, trackDivision;
    juce::Slider trackProbability, trackVelocity;
    juce::ComboBox evolvePolicy;
    juce::Label policyLabel, trackProbLabel, trackVelLabel;
    juce::Slider evolveAmount;

    juce::Slider similarity, lockResistance;
    juce::ComboBox surprise;
    juce::TextButton sparse { "SPARSE" }, syncopate { "ROTATE" }, human { "HUMAN" }, dense { "DENSE" }, soundEvolve { "SOUND" };

    juce::Label selectedLabel, evolutionStatus, footer;
    bool refreshing = false;
    int currentPage = 1; // 1 SEQ, 2 SONG
    int seqMode = 0;      // legacy compatibility; always 0 in v3.6
    bool midiDragOver = false;
    bool keyboardVisible = true;
    bool focusMode = false;
    int uiDensity = 0; // 0 compact, 1 normal, 2 large
    juce::MidiMessageCollector midiCollector;

    juce::Rectangle<int> sequencerPanel, inspectorPanel, soundPanel;
    static constexpr int gridLabelWidth = 170;
    static constexpr int gridTopPad = 52;
    static constexpr int gridBottomPad = 42;
    int synthKeyboardHeight() const noexcept { return uiDensity == 0 ? 62 : (uiDensity == 1 ? 78 : 94); }
    int instrumentDockHeight() const noexcept { return uiDensity == 0 ? 32 : (uiDensity == 1 ? 38 : 44); }
    int headerHeight() const noexcept { return uiDensity == 0 ? 38 : (uiDensity == 1 ? 44 : 50); }
    int footerHeight() const noexcept { return uiDensity == 0 ? 20 : (uiDensity == 1 ? 24 : 28); }
    juce::Rectangle<int> instrumentDockedBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
