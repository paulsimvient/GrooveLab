#include "BasslineGenerator.h"
#include <cmath>
#include <algorithm>

namespace groove
{
namespace
{
int toMidi(int pc, int octaveC)
{
    return octaveC + ((pc - (octaveC % 12) + 12) % 12);
}

struct Rng
{
    uint32_t s;
    explicit Rng(int seed) : s((uint32_t) juce::jmax(1, seed) * 747796405u + 2891336453u) {}
    float next()
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (s & 0xffffff) / 16777215.0f;
    }
};

void uniqueSortedSteps(std::vector<int>& steps)
{
    std::sort(steps.begin(), steps.end());
    steps.erase(std::unique(steps.begin(), steps.end()), steps.end());
}

std::vector<int> pitchPoolForBar(const BarObservation& bar, const BassGenParams& params)
{
    std::vector<int> pool;

    if (params.lockKey && params.scaleMode != ScaleMode::autoDetect)
    {
        for (int d : scaleDegrees(params.scaleMode))
            pool.push_back((params.keyRoot + d) % 12);
        return pool;
    }

    pool = bar.heardPcs;
    if (pool.empty())
        pool.push_back(bar.rootPc);

    const int fifth = (bar.rootPc + 7) % 12;
    if (std::find(pool.begin(), pool.end(), bar.rootPc) == pool.end())
        pool.insert(pool.begin(), bar.rootPc);
    if (std::find(pool.begin(), pool.end(), fifth) == pool.end())
        pool.push_back(fifth);
    if (bar.minor)
    {
        const int third = (bar.rootPc + 3) % 12;
        if (std::find(pool.begin(), pool.end(), third) == pool.end())
            pool.push_back(third);
    }

    // If a key is set even in AUTO, snap the pool toward that scale.
    if (params.scaleMode != ScaleMode::autoDetect)
    {
        for (int& pc : pool)
            pc = snapPcToScale(pc, params.keyRoot, params.scaleMode);
        uniqueSortedSteps(pool);
        // Ensure tonic is available.
        if (std::find(pool.begin(), pool.end(), params.keyRoot) == pool.end())
            pool.insert(pool.begin(), params.keyRoot);
    }

    return pool;
}
}

std::vector<GeneratedBassNote> BasslineGenerator::generate(const MusicalObservation& obs,
                                                           const BassGenParams& params,
                                                           int bassOctaveMidi)
{
    std::vector<GeneratedBassNote> out;
    if (! obs.valid()) return out;

    // Prefer exact note-for-note transcription when available — never scale-snap
    // (AUTO key is for labeling only; rewriting pitches made learned notes wrong).
    if (! obs.transcribed.empty())
    {
        for (const auto& hn : obs.transcribed)
        {
            GeneratedBassNote n;
            n.step = hn.step;
            n.note = juce::jlimit(21, 108, hn.midi);
            n.velocity = hn.velocity;
            n.lengthSteps = juce::jmax(1, hn.lengthSteps);
            n.reason = "learned";
            out.push_back(n);
        }
        return out;
    }

    // LISTEN path: do not invent a complex bassline from chroma/accents.
    if (! params.allowInvent)
        return out;

    Rng rng(params.seed);
    const int spb = juce::jmax(1, obs.stepsPerBar);
    const bool keyed = params.scaleMode != ScaleMode::autoDetect;
    int prevPc = keyed ? params.keyRoot : obs.barsObserved.front().rootPc;
    int prevMidi = toMidi(prevPc, bassOctaveMidi);

    const float activity = juce::jlimit(0.0f, 1.0f, obs.rhythmicActivity);
    const float density = juce::jlimit(0.2f, 1.0f,
        0.45f + 0.45f * activity + 0.25f * params.followRhythm - 0.45f * params.simplify);

    // Scale-tone preference order for locked keys: 1, 5, 3, 6/7, 2, 4...
    auto degreePreference = [&](int absPc) -> int
    {
        if (! keyed) return 0;
        const int deg = ((absPc - params.keyRoot) % 12 + 12) % 12;
        static constexpr int order[] = { 0, 7, 3, 4, 9, 10, 2, 5, 8, 11, 1, 6 };
        for (int i = 0; i < 12; ++i)
            if (order[i] == deg) return i;
        return 20;
    };

    for (const auto& bar : obs.barsObserved)
    {
        const int barStart = bar.barIndex * spb;
        auto pool = pitchPoolForBar(bar, params);
        if (keyed)
            std::sort(pool.begin(), pool.end(),
                      [&](int a, int b) { return degreePreference(a) < degreePreference(b); });

        std::vector<int> steps;
        steps.push_back(0);
        if (density > 0.22f) steps.push_back(spb / 2);
        if (density > 0.50f) steps.push_back(spb / 4);
        if (density > 0.72f) steps.push_back((spb * 3) / 4);

        if (params.followRhythm > 0.1f)
        {
            for (float beat : bar.accentBeats)
            {
                const float quarters = juce::jmax(1.0f, (float) spb / 4.0f);
                int st = juce::jlimit(0, spb - 1,
                    (int) std::round(beat / quarters * (float) spb));
                const int grid = juce::jmax(1, spb / 8);
                st = (st / grid) * grid;
                steps.push_back(st);
            }
        }
        uniqueSortedSteps(steps);

        const int minNotes = params.simplify > 0.85f ? 1 : (density > 0.4f ? 3 : 2);
        while ((int) steps.size() > juce::jmax(minNotes, 4) && params.simplify > 0.2f)
            steps.pop_back();
        if (steps.empty())
            steps.push_back(0);

        for (size_t i = 0; i < steps.size(); ++i)
        {
            const int local = steps[i];
            const int tonicPc = keyed ? params.keyRoot : bar.rootPc;
            int pc = tonicPc;
            juce::String reason = keyed ? (juce::String(pitchClassName(params.keyRoot)) + " " + scaleModeName(params.scaleMode))
                                        : "root";

            const float bias = juce::jlimit(0.0f, 1.0f, params.rootBias);
            const bool downbeat = (local == 0);
            const bool forceRoot = downbeat
                                || params.simplify > 0.55f
                                || rng.next() < (0.35f + 0.60f * bias);

            if (forceRoot)
            {
                pc = tonicPc;
                reason = keyed ? "tonic" : "chord root";
            }
            else if (! pool.empty())
            {
                int best = tonicPc;
                int bestScore = 999;
                for (int candidate : pool)
                {
                    int d = std::abs(candidate - prevPc);
                    d = juce::jmin(d, 12 - d);
                    // Prefer tonic heavily; then scale preference; then voice-leading.
                    const int rootPenalty = (candidate == tonicPc) ? 0 : (int) (8.0f * bias);
                    const int score = d * 2 + degreePreference(candidate) + rootPenalty
                                    + (int) (rng.next() * (1.0f - params.movement) * 3.0f);
                    if (score < bestScore)
                    {
                        bestScore = score;
                        best = candidate;
                    }
                }
                // Occasional non-root motion only when movement is high and bias is low.
                if (params.movement > 0.55f && bias < 0.7f && rng.next() > 0.65f && pool.size() > 1)
                    best = pool[(size_t) (1 + (int) (rng.next() * (pool.size() - 1))) % pool.size()];
                pc = best;
                reason = (pc == tonicPc)
                           ? (keyed ? "tonic" : "chord root")
                           : (keyed ? "scale tone" : "heard tone");
            }

            if (keyed)
                pc = snapPcToScale(pc, params.keyRoot, params.scaleMode);
            // After snapping, pull stray tones back to tonic when bias is high.
            if (bias > 0.75f && pc != tonicPc && rng.next() < (bias - 0.55f))
                pc = tonicPc;

            int midi = toMidi(pc, bassOctaveMidi);
            while (midi - prevMidi > 7) midi -= 12;
            while (prevMidi - midi > 7) midi += 12;
            midi = juce::jlimit(28, 52, midi);

            GeneratedBassNote n;
            n.step = barStart + local;
            n.note = midi;
            n.velocity = 0.66f + 0.28f * (local == 0 ? 1.0f : 0.7f);
            const int nextLocal = (i + 1 < steps.size()) ? steps[i + 1] : spb;
            n.lengthSteps = juce::jmax(1, juce::jmin(spb / 2, nextLocal - local));
            n.reason = (keyed ? juce::String(pitchClassName(params.keyRoot)) + " " + scaleModeName(params.scaleMode)
                              : bar.harmonyLabel)
                     + " | " + reason;
            out.push_back(n);

            prevPc = pc;
            prevMidi = midi;
        }
    }

    return out;
}
}
