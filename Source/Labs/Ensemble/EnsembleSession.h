#pragma once
#include "EnsembleTypes.h"

namespace groove::ensemble
{
struct EnsembleSession
{
    Phase phase = Phase::idle;
    SeedGrid seed;
    Song hostSong;
    bool hasHost = false;
    bool committed = false;
    int captureIndex = 0;
    int lastSeqStep = -1;
    int bars = 2;
    HatRate hatRate = HatRate::eighth;

    int recordSteps() const { return juce::jlimit(8, kSeedSteps, bars * 16); }

    void clear() { *this = {}; }
};
}