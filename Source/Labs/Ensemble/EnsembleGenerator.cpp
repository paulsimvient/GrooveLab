#include "EnsembleGenerator.h"
#include "CandidateGenerator.h"
#include "PerformanceAnalyzer.h"
#include "RelationshipScorer.h"

namespace groove::ensemble
{
HatCandidate generateComplementHats(const SeedGrid& seed, int variation)
{
    const auto features = analyzeSeed(seed);
    auto candidates = generateHatCandidates(features, variation);
    return pickBestHats(std::move(candidates), features, variation);
}
}