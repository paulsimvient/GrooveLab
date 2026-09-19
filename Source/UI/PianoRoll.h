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
    void fitToNotes() { fitNotes(); }

    std::function<void()> onListenClicked;
    std::function<void()> onRegenClicked;
    std::function<void()> onSimplifyClicked;
    std::function<void()> onMoreMoveClicked;
    std::function<void()> onFollowRhythmClicked;
    std::function<void()> onClearListenClicked;
    std::function<void()> onMonitorClicked;

    void setListenArmed(bool armed, float progress01 = 0.0f, bool waitingForNote = false);
    void setListenResult(bool hasResult, int noteCount = 0, const juce::String& summary = {});
    int getListenBars() const;
    int getListenKeyRoot() const;   // 0=C .. 11=B
    int getListenScaleMode() const; // ScaleMode as int
    void setListenKeyRoot(int rootPc); // updates KEY box without notifying
    bool hasListenResult() const noexcept { return listenHasResult; }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    groove::GrooveEngine& engine;
    int lane = 1; // 0=drum MIDI-key view, 1..3=melodic lanes
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
    juce::TextButton clearListenButton { "CLEAR BASS" };
    juce::TextButton resetButton { "RESET" };
    juce::TextButton newTakeButton { "NEW TAKE" };
    juce::TextButton keepButton { "KEEP" };
    juce::TextButton fitButton { "FIT NOTES" };
    juce::TextButton muteButton { "MUTE" };
    juce::TextButton recordButton { "REC" };
    juce::TextButton listenButton { "LISTEN" };
    juce::TextButton monitorButton { "INPUT" };
    juce::ComboBox listenBarsBox;
    juce::ComboBox listenKeyBox;
    juce::ComboBox listenModeBox;
    juce::TextButton regenButton { "REGEN" };
    juce::TextButton simplifyButton { "SIMPLE" };
    juce::TextButton moveButton { "MOVE" };
    juce::TextButton followButton { "RHYTHM" };
    juce::Label title;
    juce::Label listenStatus;
    bool listenArmed = false;
    bool listenWaiting = false;
    float listenProgress = 0.0f;
    bool listenHasResult = false;
    int listenNoteCount = 0;
    juce::String listenSummary;

    juce::Rectangle<int> headerArea, listenArea, pianoArea, gridArea, velocityArea, inspectorArea;

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
    void fitNotes();
    void selectNote(int index);
    void updateListenChrome();
    bool isDrumMode() const noexcept { return lane == 0; }
    int drumTrackFromY(int y) const;
    int drumStepFromX(int x) const;
    juce::String laneTitle() const;
    static bool isBlackKey(int note);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRoll)
};
