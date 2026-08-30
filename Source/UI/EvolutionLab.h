#pragma once
#include <JuceHeader.h>
#include <functional>
#include "../Audio/GrooveEngine.h"

class EvolutionLab : public juce::Component
{
public:
    explicit EvolutionLab(groove::GrooveEngine&);

    void paint(juce::Graphics&) override;
    void resized() override;

    std::function<void()> onLayout;
    juce::Rectangle<int> evolutionPanel, rulesPanel, ancestryPanel;

private:
    void drawPanel(juce::Graphics&, juce::Rectangle<int>, const juce::String&);
    void drawAncestry(juce::Graphics&, juce::Rectangle<int>);

    groove::GrooveEngine& engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EvolutionLab)
};

class EvolutionWindow : public juce::DocumentWindow
{
public:
    EvolutionWindow(groove::GrooveEngine&, juce::LookAndFeel&);
    ~EvolutionWindow() override;
    void closeButtonPressed() override { setVisible(false); }

    EvolutionLab lab;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EvolutionWindow)
};
