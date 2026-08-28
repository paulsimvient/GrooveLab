#include "Sequencer.h"

namespace groove
{
void Sequencer::prepare(double sampleRate)
{
    sr = sampleRate;
    setBpm(bpm);
    reset();
}

void Sequencer::setBpm(double b)
{
    bpm = juce::jlimit(40.0, 260.0, b);
    samplesPerStep = sr * 60.0 / bpm / 4.0; // 16th-note base clock
}

void Sequencer::reset()
{
    globalSampleCounter = 0;
    for (auto& c : trackClocks)
    {
        c.nextStep = 0;
        c.lastStep = 0;
        c.samplesUntilNextStep = 1.0;
    }
}
}
