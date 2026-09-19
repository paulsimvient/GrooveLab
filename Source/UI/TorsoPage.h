#pragma once
#include <JuceHeader.h>
#include <functional>
#include "../Audio/GrooveEngine.h"

// Euclidean page: 32-step grid you can click, per-step sound, assigned kit note.
class TorsoPage : public juce::Component,
                  public juce::FileDragAndDropTarget,
                  private juce::Timer
{
public:
    explicit TorsoPage(groove::GrooveEngine&);
    ~TorsoPage() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void refreshFromEngine();
    void setMidiLane(int laneIndex); // -1 = drum Euclid, 1..3 = melodic instrument lane
    int getMidiLane() const noexcept { return midiLane; }
    void setCompactMelodicMode(bool compact);
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;

    std::function<void()> onPatternChanged;
    std::function<void(juce::String)> onStatusMessage;
    // Called when MOOG / MAXPOLY / KEYS is selected directly in the shared EUC window.
    std::function<void(int)> onMidiChannelSelected;

private:
    void timerCallback() override;
    void bindGeneratorKnobs();
    void bindStepKnobs();
    void commitGenerator();
    void playTrack(int track);
    juce::Rectangle<int> pulsePad(int index) const;
    juce::Rectangle<int> trackPad(int index) const;

    groove::GrooveEngine& engine;

    juce::Slider steps;
    juce::Slider pulses;
    juce::Slider rotate;
    juce::ComboBox division;
    juce::ComboBox rhythmMode;

    juce::Slider velocity;
    juce::ComboBox kitNote;
    juce::Slider probability;
    juce::Slider repeats;
    // Contextual melodic controls shown in the same lower panel when MOOG / MAXPOLY / KEYS is selected.
    juce::Slider melodicVelocity;
    juce::Slider melodicProbability;
    juce::Slider melodicRepeats;
    juce::Slider melodicOctave;
    juce::Slider melodicGate;
    std::array<juce::Slider, groove::paramCount> soundSliders;
    juce::TextButton playStep { "PLAY" };
    juce::TextButton clearStep { "CLEAR STEP" };
    std::array<juce::TextButton, groove::kTracks> trackPlay;

    juce::Rectangle<int> shapePanel, pulsePanel, stepPanel, trackPanel;
    bool refreshing = false;
    int midiLane = -1;
    bool editingMidiLane() const noexcept { return midiLane > 0 && midiLane < groove::kMidiLanes; }
    bool midiDragOver = false;
    bool compactMelodicMode = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TorsoPage)
};
