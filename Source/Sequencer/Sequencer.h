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
        int midiNote = 36;
        float division = 1.0f;
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

        if (tr.rhythmMode == RhythmMode::step)
            return st.overrideMode == StepOverrideMode::forceOn || st.active;

        const bool generated = euclideanHit(local, steps, tr.pulses, tr.rotate);
        if (tr.rhythmMode == RhythmMode::euclid)
            return generated;

        // HYBRID: generator underneath, explicit step edits on top.
        if (st.overrideMode == StepOverrideMode::forceOn) return true;
        if (st.overrideMode == StepOverrideMode::forceOff) return false;
        return generated;
    }


    static VoiceParams effectiveParams(const Track& tr, int step)
    {
        auto p = tr.base;
        const auto& L = tr.steps[(size_t) step].locks;
        if (L[(int) Param::pitch])     p.pitchHz   = *L[(int) Param::pitch];
        if (L[(int) Param::decay])     p.decayMs   = *L[(int) Param::decay];
        if (L[(int) Param::transient]) p.transient = *L[(int) Param::transient];
        if (L[(int) Param::noise])     p.noise     = *L[(int) Param::noise];
        if (L[(int) Param::filter])    p.filter    = *L[(int) Param::filter];
        if (L[(int) Param::drive])     p.drive     = *L[(int) Param::drive];
        if (L[(int) Param::space])     p.space     = *L[(int) Param::space];
        if (L[(int) Param::blend])     p.blend     = *L[(int) Param::blend];
        return p;
    }

    static int effectiveMidiNote(const Track& tr, int step, int trackIndex)
    {
        const auto& st = tr.steps[(size_t) step];
        if (st.midiNote.has_value() && *st.midiNote >= 0 && *st.midiNote <= 127)
            return *st.midiNote;
        if (tr.midiNote >= 0 && tr.midiNote <= 127)
            return tr.midiNote;
        static constexpr int defaults[kTracks] = { 36, 38, 39, 42, 46, 45, 47, 49 };
        return defaults[juce::jlimit(0, kTracks - 1, trackIndex)];
    }

    static bool trackIsAudible(const std::array<Track, kTracks>& tracks, int track)
    {
        bool anySolo = false;
        for (const auto& tr : tracks)
            anySolo = anySolo || tr.soloed;
        return anySolo ? tracks[(size_t) track].soloed : ! tracks[(size_t) track].muted;
    }

    template <typename Fn>
    void processBlock(const std::array<Track, kTracks>& tracks, int numSamples, Fn&& onTrigger)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            for (int t = 0; t < kTracks; ++t)
            {
                auto& clock = trackClocks[(size_t) t];
                if (--clock.samplesUntilNextStep <= 0.0)
                {
                    const auto& tr = tracks[(size_t) t];
                    const int length = juce::jlimit(1, kSteps, tr.generatorSteps);
                    const int step = clock.nextStep % length;
                    const auto& st = tr.steps[(size_t) step];
                    clock.lastStep = step;

                    const float probability = juce::jlimit(0.0f, 1.0f,
                        st.probability * tr.probability);
                    if (trackIsAudible(tracks, t) && resolvedStepActive(tr, step)
                        && random.nextFloat() <= probability)
                    {
                        const int reps = juce::jlimit(1, 4, st.ratchet);
                        const float vel = juce::jlimit(0.0f, 1.2f, st.velocity * tr.velocity);
                        onTrigger(Trigger { t, step, vel, effectiveParams(tr, step), reps, i,
                                            effectiveMidiNote(tr, step, t), tr.division });
                    }

                    clock.nextStep = (clock.nextStep + 1) % length;
                    const double division = juce::jlimit(0.25, 4.0, (double) tr.division);
                    clock.samplesUntilNextStep += samplesPerStep / division;
                }
            }
            ++globalSampleCounter;
        }
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
