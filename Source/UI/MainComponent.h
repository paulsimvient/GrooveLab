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
    void refreshMidiInputs();
    void autoSelectMidiInput();
    static bool isVirtualMidiName(const juce::String&);
    static bool looksLikeKeyboardMidi(const juce::String&);
    void toggleTransport();
    void setPage(int page); // 0 GRID, 1 EUCLIDEAN, 2 SONG
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

    groove::GrooveEngine engine;
    groove::ExternalPluginHost pluginHost;
    groove::ExternalPluginHost synthHost;
    groove::ExternalPluginHost keysHost;
    groove::ExternalPluginHost polymaxHost;
    groove::MixBus mixBus;
    juce::AudioBuffer<float> drumStem, synthStem, keysStem, polyStem;
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
    SynthKeyboard synthKeyboard;
    MixStrip mixStrip;
    std::unique_ptr<EvolutionWindow> evolutionWindow;

    juce::TextButton playButton { "PLAY" };
    juce::TextButton resetButton { "RESET" };
    juce::TextButton captureButton { "CAPTURE" };
    juce::TextButton backButton { "BACK" };
    juce::TextButton auditionButton { "AUDITION" };
    juce::TextButton performButton { "PERFORM" };
    juce::TextButton commitPerformButton { "CAPTURE TEMP" };
    juce::TextButton pageGridButton { "GRID" };
    juce::TextButton pageT1Button { "EUCLIDEAN" };
    juce::TextButton pageSongButton { "SONG" };
    juce::TextButton saveButton { "SAVE" };
    juce::TextButton saveAsButton { "SAVE AS" };
    juce::TextButton loadButton { "LOAD" };
    juce::TextButton evolveButton { "EVOLVE" };
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

    std::array<juce::Slider, groove::paramCount> soundSliders;
    std::array<juce::Label, groove::paramCount> soundLabels;
    int vstKitListCount = -1;
    int synthKitListCount = -1;
    int keysKitListCount = -1;
    int polyKitListCount = -1;
    // Explicit sound edit scope. STEP is the default: knob movement writes
    // per-step parameter locks. VOICE edits the selected track's base sound.
    juce::ComboBox soundScope;
    juce::TextButton clearLocks { "CLEAR STEP SOUND" };
    juce::Label soundScopeLabel;
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
    int currentPage = 0; // 0 GRID, 1 EUCLIDEAN, 2 SONG
    bool midiDragOver = false;
    juce::MidiMessageCollector midiCollector;

    juce::Rectangle<int> sequencerPanel, inspectorPanel, soundPanel;
    static constexpr int gridLabelWidth = 170;
    static constexpr int gridTopPad = 52;
    static constexpr int gridBottomPad = 42;
    static constexpr int synthKeyboardH = 96;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
