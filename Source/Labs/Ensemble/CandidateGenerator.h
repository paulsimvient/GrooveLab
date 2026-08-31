#pragma once
#include "EnsembleTypes.h"
#include <vector>

namespace groove::ensemble
{
std::vector<HatCandidate> generateHatCandidates(const SeedFeatures& features, int variation);
}