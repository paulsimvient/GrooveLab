#include "RelationshipScorer.h"
#include <algorithm>
#include <cmath>

namespace groove::ensemble
{
float scoreComplementHats(const HatCandidate& candidate, const SeedFeatures& features)
{
    const int hits = countHits(candidate.hits);
    if (hits == 0)
        return -100.0f;

    float score = 0.0f;
    int holes = 0, kickHits = 0, snareHits = 0, even = 0;
    for (int s = 0; s < kHatSteps; ++s)
    {
        if (! candidate.hits[(size_t) s])
            continue;
        if (! features.occupied[(size_t) s])
            ++holes;
        if (features.kickMask[(size_t) s])
            ++kickHits;
        if (features.snareMask[(size_t) s])
            ++snareHits;
        if ((s % 2) == 0)
            ++even;
    }

    score += (float) holes * 3.2f;
    score -= (float) kickHits * 4.5f;
    score -= (float) snareHits * 1.8f;
    score += (float) even * 0.55f;

    const float seedBusy = juce::jlimit(0.08f, 0.55f, features.density);
    const float target = juce::jmap(seedBusy, 0.08f, 0.55f, 12.0f, 7.0f);
    score -= std::abs((float) hits - target) * 1.4f;

    if (hits >= 15)
        score -= 8.0f;
    if (features.offbeatRatio > 0.45f && (even == hits))
        score += 1.2f;
    return score;
}

HatCandidate pickBestHats(std::vector<HatCandidate> candidates, const SeedFeatures& features, int variation)
{
    if (candidates.empty())
        return {};

    for (auto& c : candidates)
        c.score = scoreComplementHats(c, features);

    std::sort(candidates.begin(), candidates.end(),
              [](const HatCandidate& a, const HatCandidate& b) { return a.score > b.score; });

    const int pick = juce::jlimit(0, juce::jmin(4, (int) candidates.size() - 1), variation % 5);
    return candidates[(size_t) pick];
}
}