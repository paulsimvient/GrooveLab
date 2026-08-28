#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class GrooveLabAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      private juce::Timer
{
public:
    explicit GrooveLabAudioProcessorEditor(GrooveLabAudioProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { repaint(); }
    GrooveLabAudioProcessor& processor;
    juce::TextButton muteInternal { "MUTE INTERNAL SYNTH" };
};
