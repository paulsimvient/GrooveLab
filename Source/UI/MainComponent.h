#pragma once
#include <JuceHeader.h>
#include <atomic>
#include "../Audio/GrooveEngine.h"
#include "../Audio/ExternalPluginHost.h"
#include "GrooveLookAndFeel.h"
#include "TorsoPage.h"

class MainComponent : public juce::AudioAppComponent,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer,
                      private juce::KeyListener,
                      private juce::ChangeListener
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
    void drawAncestry(juce::Graphics&, juce::Rectangle<int>);
    void toggleTransport();
    void setT1View(bool on);
    void setLabPageVisible(bool on);
    void choosePluginFile();
    void browseForPlugin();
    void loadPluginFromFile(const juce::File&);
    void refreshMidiOutputs();
    void showPreferences();
    void saveAudioSettings();
    static juce::File audioSettingsFile();
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    bool importDroppedMidi(const juce::StringArray& files);

    groove::GrooveEngine engine;
    groove::ExternalPluginHost pluginHost;
    std::unique_ptr<juce::MidiOutput> midiOutput;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::Array<juce::MidiDeviceInfo> midiDevices;
    std::atomic<int> soundMode { 1 };
    GrooveLookAndFeel look;
    TorsoPage torsoPage { engine };

    juce::TextButton playButton { "PLAY" };
    juce::TextButton resetButton { "RESET" };
    juce::TextButton captureButton { "CAPTURE" };
    juce::TextButton backButton { "BACK" };
    juce::TextButton auditionButton { "AUDITION" };
    juce::TextButton performButton { "PERFORM" };
    juce::TextButton commitPerformButton { "CAPTURE TEMP" };
    juce::TextButton pageGridButton { "GRID" };
    juce::TextButton pageT1Button { "EUCLIDEAN" };
    juce::TextButton prefsButton { "PREFS" };
    juce::ComboBox soundSource;
    juce::TextButton loadPluginButton { "LOAD VST" };
    juce::TextButton showPluginButton { "UJAM UI" };
    juce::ComboBox midiOutBox;
    juce::Slider bpm;
    juce::Label bpmLabel, projectLabel;

    std::array<juce::Slider, groove::paramCount> soundSliders;
    std::array<juce::Label, groove::paramCount> soundLabels;
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
    juce::Slider evolveAmount;

    juce::Slider similarity, lockResistance;
    juce::ComboBox surprise;
    juce::TextButton sparse { "SPARSE" }, syncopate { "ROTATE" }, human { "HUMAN" }, dense { "DENSE" }, soundEvolve { "SOUND" };

    juce::Label selectedLabel, evolutionStatus, footer;
    bool refreshing = false;
    bool t1View = false;
    bool midiDragOver = false;

    juce::Rectangle<int> sequencerPanel, inspectorPanel, soundPanel, evolutionPanel, rulesPanel, ancestryPanel;
    static constexpr int gridLabelWidth = 170;
    static constexpr int gridTopPad = 52;
    static constexpr int gridBottomPad = 42;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
