#pragma once
#include <JuceHeader.h>
#include <bitset>
#include <functional>

class SynthKeyboard : public juce::Component
{
public:
    SynthKeyboard();
    ~SynthKeyboard() override;

    std::function<void(int note, float velocity)> onNoteOn;
    std::function<void(int note)> onNoteOff;

    void setExternalHeld(int note, bool down);
    void allNotesOff();
    void setOctaveOffset(int offset);
    int getOctaveOffset() const noexcept { return octaveOffset; }
    void setTarget(int target); // 0 Moog ch2, 1 Keys ch4, 2 Poly ch3, 3 Drums ch1
    int getTarget() const noexcept { return keyboardTarget; }
    std::function<void()> onOctaveChanged;
    std::function<void()> onTargetChanged;
    std::function<void()> onUserPickedTarget;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool isKeyDown) override;
    void focusLost(FocusChangeType) override;

private:
    static constexpr int kOctaves = 3;
    static constexpr int kNumNotes = kOctaves * 12 + 1; // C to C

    static bool isBlackKey(int note) noexcept;
    static constexpr int kMinOctave = -3; // C-1
    static constexpr int kMaxOctave = 3;  // C5
    int lowestNote() const noexcept { return 36 + octaveOffset * 12; } // C2 + octaves
    int highestNote() const noexcept { return lowestNote() + kNumNotes - 1; }
    int noteAt(juce::Point<int>) const;
    void noteOn(int note, float velocity);
    void noteOff(int note);
    int computerKeyToNote(int keyCode) const;
    void shiftOctave(int delta);
    void releaseMouseNote();

    int octaveOffset = 0;
    int keyboardTarget = 0;
    int mouseNote = -1;
    std::bitset<128> held {};
    std::bitset<128> computerHeld {};
    juce::TextButton octaveDown { "<" };
    juce::TextButton octaveUp { ">" };
    juce::TextButton drumsTarget { "CH1" };
    juce::TextButton moogTarget { "CH2" };
    juce::TextButton keysTarget { "CH4" };
    juce::TextButton polyTarget { "CH3" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthKeyboard)
};
