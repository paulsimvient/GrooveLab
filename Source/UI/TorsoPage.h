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
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;

    std::function<void()> onPatternChanged;
    std::function<void(juce::String)> onStatusMessage;

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
    std::array<juce::Slider, groove::paramCount> soundSliders;
    juce::TextButton playStep { "PLAY" };
    juce::TextButton clearStep { "CLEAR STEP" };
    std::array<juce::TextButton, groove::kTracks> trackPlay;

    juce::Rectangle<int> shapePanel, pulsePanel, stepPanel, trackPanel;
    bool refreshing = false;
    bool midiDragOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TorsoPage)
};
