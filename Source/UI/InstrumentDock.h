#pragma once
#include <JuceHeader.h>
#include <array>
#include <functional>
#include "../Core/GrooveTypes.h"
#include "../Audio/DrumMidi.h"

// Persistent global track selector.  It is deliberately a single compact row:
// the same selected target is used by SEQ, SONG, BEAT and the mixer.
class InstrumentDock : public juce::Component
{
public:
    InstrumentDock();
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

    void setSelectedChannel(int channel); // compatibility helper
    void setSelectedTarget(int target);
    int getSelectedTarget() const noexcept { return selectedTarget; }
    void setFloating(bool shouldFloat);
    bool isFloating() const noexcept { return floating; }

    std::function<void(int target)> onTargetSelected;
    std::function<void(int channel)> onOpenInstrumentUi;
    std::function<void(bool)> onFloatingChanged;

private:
    std::array<juce::TextButton, groove::kUnifiedTracks> targetButtons;
    juce::TextButton floatButton { "FLOAT" };
    int selectedTarget = groove::unifiedTrackForMidiLane(1);
    bool floating = false;
    juce::ComponentDragger dragger;
    juce::ComponentBoundsConstrainer constrainer;
    std::array<double, groove::kUnifiedTracks> lastClickMs {};

    static int channelForTarget(int target);
    static const char* nameForTarget(int target);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InstrumentDock)
};
