#pragma once
#include "ListenTypes.h"
#include <vector>

namespace groove
{
class BasslineGenerator
{
public:
    static std::vector<GeneratedBassNote> generate(const MusicalObservation& obs,
                                                   const BassGenParams& params,
                                                   int bassOctaveMidi = 36); // C2
};
}
