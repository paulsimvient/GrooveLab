#include "PluginEditor.h"
#include "../Audio/DrumMidi.h"

GrooveLabAudioProcessorEditor::GrooveLabAudioProcessorEditor(GrooveLabAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    muteInternal.setClickingTogglesState(true);
    muteInternal.onClick = [this]
    {
        processor.engine().setInternalSynthEnabled(! muteInternal.getToggleState());
    };
    addAndMakeVisible(muteInternal);
    setSize(1120, 460);
    startTimerHz(24);
}

void GrooveLabAudioProcessorEditor::resized()
{
    muteInternal.setBounds(getWidth() - 200, 18, 178, 28);
}

void GrooveLabAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff050c12));

    g.setColour(juce::Colour(0xffe5f0f6));
    g.setFont(juce::FontOptions(21.0f, juce::Font::bold));
    g.drawText("Lil God Projector", 22, 16, 360, 30,
               juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xff7c9bad));
    g.setFont(juce::FontOptions(10.0f));
    g.drawText("MIDI out uses UJAM Beatmaker kit notes  ·  route this track to Beatmaker / Virtual Drummer",
               24, 44, 700, 18, juce::Justification::centredLeft);

    const auto& st = processor.engine().state();
    const int left = 90;
    const int top = 84;
    const int rowH = 35;
    const int usableW = getWidth() - left - 18;
    const float stepW = (float)usableW / (float)groove::kSteps;

    for (int t = 0; t < groove::kTracks; ++t)
    {
        g.setColour(juce::Colour(0xffa5bdcb));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(groove::voiceName(t) + " " + groove::midiNoteNameForTrack(t),
                   10, top + t * rowH, 78, rowH,
                   juce::Justification::centredLeft);

        for (int step = 0; step < groove::kSteps; ++step)
        {
            juce::Rectangle<float> r(left + step * stepW + 1.0f,
                                     top + t * rowH + 3.0f,
                                     stepW - 2.0f,
                                     rowH - 7.0f);

            auto active = processor.engine().isResolvedHit(t, step)
                       && step < st.tracks[t].generatorSteps;
            g.setColour(active ? juce::Colour(0xff208ee0)
                               : juce::Colour(0xff17252f));
            g.fillRoundedRectangle(r, 2.0f);

            if (step == processor.engine().currentStepForTrack(t))
            {
                g.setColour(juce::Colour(0xffb9e5ff));
                g.drawRoundedRectangle(r, 2.0f, 1.5f);
            }
        }
    }
}
