#pragma once
#include "../Core/GrooveState.h"

namespace groove
{
class Sequencer
{
public:
    struct Trigger
    {
        int track = 0;
        int step = 0;
        float velocity = 1.0f;
        VoiceParams params {};
        int ratchet = 1;
        int sampleOffset = 0;
    };

    void prepare(double sampleRate);
    void setBpm(double bpm);
    void reset();

    static bool euclideanHit(int step, int steps, int pulses, int rotate)
    {
        steps = juce::jlimit(1, kSteps, steps);
        pulses = juce::jlimit(0, steps, pulses);
        if (pulses == 0) return false;
        if (pulses == steps) return true;

        int r = rotate % steps;
        if (r < 0) r += steps;
        int i = (step - r) % steps;
        if (i < 0) i += steps;

        // Even-distribution / Euclidean bucket test. Deterministic and allocation-free.
        return ((i * pulses) % steps) < pulses;
    }

    static bool resolvedStepActive(const Track& tr, int step)
    {
        const int steps = juce::jlimit(1, kSteps, tr.generatorSteps);
        const int local = step % steps;
        const auto& st = tr.steps[local];
        if (st.overrideMode == StepOverrideMode::forceOn) return true;
        if (st.overrideMode == StepOverrideMode::forceOff) return false;
        return euclideanHit(local, steps, tr.pulses, tr.rotate);
    }

    template <typename Fn>
    void processBlock(GrooveState& state, int numSamples, Fn&& onTrigger)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            for (int t = 0; t < kTracks; ++t)
            {
                auto& clock = trackClocks[t];
                if (--clock.samplesUntilNextStep <= 0.0)
                {
                    const auto& tr = state.tracks[t];
                    const int length = juce::jlimit(1, kSteps, tr.generatorSteps);
                    const int s = clock.nextStep % length;
                    const auto& st = tr.steps[s];
                    clock.lastStep = s;

                    const bool eventExists = resolvedStepActive(tr, s);
                    const float combinedProbability = juce::jlimit(0.0f, 1.0f,
                        st.probability * tr.probability);

                    if (eventExists && random.nextFloat() <= combinedProbability)
                    {
                        const int reps = juce::jlimit(1, 4, st.ratchet);
                        const float vel = juce::jlimit(0.0f, 1.2f,
                            st.velocity * tr.velocity);
                        onTrigger(Trigger { t, s, vel, state.effectiveParams(t, s), reps, i });
                    }

                    clock.nextStep = (clock.nextStep + 1) % length;
                    const double division = juce::jlimit(0.25, 4.0, (double)tr.division);
                    clock.samplesUntilNextStep += samplesPerStep / division;
                }
            }
            ++globalSampleCounter;
        }
    }

    int getCurrentStep() const noexcept { return trackClocks[0].lastStep; }
    int getTrackStep(int track) const noexcept
    {
        return trackClocks[juce::jlimit(0, kTracks - 1, track)].lastStep;
    }

private:
    struct TrackClock
    {
        double samplesUntilNextStep = 1.0;
        int nextStep = 0;
        int lastStep = 0;
    };

    double sr = 44100.0;
    double bpm = 124.0;
    double samplesPerStep = 0.0;
    std::array<TrackClock, kTracks> trackClocks {};
    int64 globalSampleCounter = 0;
    juce::Random random;
};
}
