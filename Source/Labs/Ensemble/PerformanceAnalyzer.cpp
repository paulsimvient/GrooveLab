#include "PerformanceAnalyzer.h"
#include <cmath>

namespace groove::ensemble
{
SeedFeatures analyzeSeed(const SeedGrid& seed)
{
    SeedFeatures f;
    int kickHits = 0, snareHits = 0, offbeats = 0, onsets = 0;
    int lastOn = -1;
    int iois[8] {};
    int ioiCount = 0;

    for (int i = 0; i < kSeedSteps; ++i)
    {
        const bool kick = seed.kick[(size_t) i].on;
        const bool snare = seed.snare[(size_t) i].on;
        const bool hat = seed.hats[(size_t) i].on;
        if (kick) ++kickHits;
        if (snare) ++snareHits;
        if (! kick && ! snare && ! hat)
            continue;
        ++onsets;
        if ((i % 4) != 0)
            ++offbeats;
        if (lastOn >= 0 && ioiCount < 8)
            iois[ioiCount++] = i - lastOn;
        lastOn = i;

        const int fold = i % kHatSteps;
        f.kickMask[(size_t) fold] = f.kickMask[(size_t) fold] || kick;
        f.snareMask[(size_t) fold] = f.snareMask[(size_t) fold] || snare;
        f.occupied[(size_t) fold] = f.occupied[(size_t) fold] || kick || snare;
        const float vel = juce::jmax(seed.kick[(size_t) i].velocity,
                                     seed.snare[(size_t) i].velocity);
        f.accent[(size_t) fold] = juce::jmax(f.accent[(size_t) fold], vel);
    }

    f.kickDensity = (float) kickHits / (float) kSeedSteps;
    f.snareDensity = (float) snareHits / (float) kSeedSteps;
    f.density = (float) onsets / (float) kSeedSteps;
    f.offbeatRatio = onsets > 0 ? (float) offbeats / (float) onsets : 0.0f;

    int pulse = 2;
    if (ioiCount > 0)
    {
        float sum = 0.0f;
        for (int i = 0; i < ioiCount; ++i)
            sum += (float) iois[i];
        const int mean = juce::jmax(1, (int) std::round(sum / (float) ioiCount));
        const int choices[] = { 1, 2, 4, 8 };
        int best = 2;
        int dist = 99;
        for (int c : choices)
        {
            const int d = std::abs(c - mean);
            if (d < dist) { dist = d; best = c; }
        }
        pulse = best;
    }
    f.impliedPulse = pulse;
    return f;
}
}