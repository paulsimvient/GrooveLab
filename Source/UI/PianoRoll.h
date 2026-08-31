#pragma once
#include <JuceHeader.h>
#include "../Audio/GrooveEngine.h"

class PianoRoll : public juce::Component
{
public:
    explicit PianoRoll(groove::GrooveEngine& e);

    void setLane(int laneIndex);
    int getLane() const noexcept { return lane; }
    void refresh();

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    groove::GrooveEngine& engine;
    int lane = 1; // melodic lanes: 1=CH2, 2=CH3, 3=CH4
    int selectedNote = -1;
    int lowNote = 36;   // C2
    int highNote = 84;  // C6
    int snapSteps = 1;

    enum class DragMode { none, move, resize, velocity };
    DragMode dragMode = DragMode::none;
    juce::Point<int> dragStart;
    groove::MidiLaneNote dragOriginal;

    juce::ComboBox snapBox;
    juce::TextButton octaveDown { "-" }, octaveUp { "+" };
    juce::TextButton deleteButton { "DELETE" };
    juce::Label title;

    juce::Rectangle<int> headerArea, pianoArea, gridArea, velocityArea, inspectorArea;

    const groove::MidiLane* laneState() const;
    groove::MidiLaneNote noteAtIndex(int index) const;
    int noteIndexAt(juce::Point<int> p, bool includeVelocity = false) const;
    juce::Rectangle<float> noteRect(const groove::MidiLaneNote&) const;
    int stepFromX(int x) const;
    int noteFromY(int y) const;
    float velocityFromY(int y) const;
    int snap(int step) const;
    void addNoteAt(juce::Point<int> p);
    void deleteSelected();
    void selectNote(int index);
    juce::String laneTitle() const;
    static bool isBlackKey(int note);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRoll)
};
