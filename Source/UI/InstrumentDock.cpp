#include "InstrumentDock.h"

namespace
{
const juce::Colour bg(0xff09131a);
const juce::Colour border(0xff28404f);
const juce::Colour accent(0xff58d993);
}

InstrumentDock::InstrumentDock()
{
    setOpaque(true);
    constrainer.setMinimumOnscreenAmounts(24, 24, 24, 24);
    for (int i = 0; i < groove::kUnifiedTracks; ++i)
    {
        auto& b = targetButtons[(size_t) i];
        b.setButtonText(nameForTarget(i));
        b.setTooltip("Select globally · double-click opens instrument UI");
        b.onClick = [this, i]
        {
            setSelectedTarget(i);
            if (onTargetSelected) onTargetSelected(i);
            const auto now = juce::Time::getMillisecondCounterHiRes();
            if (now - lastClickMs[(size_t) i] < 360.0)
                if (onOpenInstrumentUi) onOpenInstrumentUi(channelForTarget(i));
            lastClickMs[(size_t) i] = now;
        };
        addAndMakeVisible(b);
    }
    floatButton.setTooltip("Dock / float this track bar inside Lil God Projector");
    floatButton.onClick = [this]
    {
        setFloating(! floating);
        if (onFloatingChanged) onFloatingChanged(floating);
    };
    addAndMakeVisible(floatButton);
}

int InstrumentDock::channelForTarget(int target)
{
    if (groove::unifiedTrackIsDrum(target)) return groove::kMidiChDrums;
    return groove::midiLaneChannel(groove::unifiedTrackMidiLane(target));
}

const char* InstrumentDock::nameForTarget(int i)
{
    static const char* n[] { "KICK", "SNARE", "CLAP", "CHH", "OHH", "P1", "P2", "FX", "MOOG", "MAXPOLY", "KEYS" };
    return n[juce::jlimit(0, groove::kUnifiedTracks - 1, i)];
}

void InstrumentDock::setSelectedChannel(int channel)
{
    const int lane = groove::midiLaneIndexForChannel(channel);
    if (lane > 0) setSelectedTarget(groove::unifiedTrackForMidiLane(lane));
    else if (channel == groove::kMidiChDrums && ! groove::unifiedTrackIsDrum(selectedTarget)) setSelectedTarget(0);
}

void InstrumentDock::setSelectedTarget(int target)
{
    selectedTarget = juce::jlimit(0, groove::kUnifiedTracks - 1, target);
    for (int i = 0; i < groove::kUnifiedTracks; ++i)
    {
        const bool on = i == selectedTarget;
        targetButtons[(size_t) i].setColour(juce::TextButton::buttonColourId,
                                             on ? accent.withAlpha(0.34f) : juce::Colour(0xff13232d));
        targetButtons[(size_t) i].setColour(juce::TextButton::textColourOffId,
                                             on ? juce::Colours::white : juce::Colour(0xff9db0bc));
    }
    repaint();
}

void InstrumentDock::setFloating(bool shouldFloat)
{
    floating = shouldFloat;
    floatButton.setButtonText(floating ? "DOCK" : "FLOAT");
    repaint();
}

void InstrumentDock::paint(juce::Graphics& g)
{
    g.fillAll(bg);
    g.setColour(border);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 5.0f, 1.0f);
    if (floating)
    {
        g.setColour(juce::Colour(0xff6f8290));
        g.setFont(juce::FontOptions(8.0f));
        g.drawText("TRACKS", 5, 1, 42, 11, juce::Justification::centredLeft);
    }
}

void InstrumentDock::resized()
{
    auto r = getLocalBounds().reduced(5, 4);
    auto floatArea = r.removeFromRight(48);
    floatButton.setBounds(floatArea.reduced(1));
    r.removeFromRight(4);
    const int gap = 3;
    const int w = juce::jmax(38, (r.getWidth() - (groove::kUnifiedTracks - 1) * gap) / groove::kUnifiedTracks);
    for (int i = 0; i < groove::kUnifiedTracks; ++i)
    {
        auto cell = r.removeFromLeft(i == groove::kUnifiedTracks - 1 ? r.getWidth() : w);
        targetButtons[(size_t) i].setBounds(cell);
        if (i < groove::kUnifiedTracks - 1) r.removeFromLeft(gap);
    }
}

void InstrumentDock::mouseDown(const juce::MouseEvent& e)
{
    if (! floating) return;
    if (e.eventComponent != this) return;
    dragger.startDraggingComponent(this, e);
}

void InstrumentDock::mouseDrag(const juce::MouseEvent& e)
{
    if (! floating || e.eventComponent != this) return;
    dragger.dragComponent(this, e, &constrainer);
}
