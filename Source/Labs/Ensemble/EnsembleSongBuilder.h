#pragma once
#include "EnsembleTypes.h"

namespace groove::ensemble
{
SongSection workshopSection(Meter meter, int steps = kSeedSteps, HatRate hatRate = HatRate::eighth);
void applyHatPattern(SongSection& section, HatRate rate);
void applyHatPatternToTracks(std::array<Track, kTracks>& tracks, HatRate rate);
Song buildSongFromBeat(const std::array<Track, kTracks>& seed, Meter meter);
void setKitLoopLength(Song& song, int newLen, bool tile);
void applySeedToArrangement(Song& song, const std::array<Track, kTracks>& seed);
}