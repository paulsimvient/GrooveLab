#pragma once
#include "EnsembleTypes.h"
#include <vector>

namespace groove::ensemble
{
float scoreComplementHats(const HatCandidate& candidate, const SeedFeatures& features);
HatCandidate pickBestHats(std::vector<HatCandidate> candidates, const SeedFeatures& features, int variation);
}