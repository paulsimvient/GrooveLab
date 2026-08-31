#pragma once
#include "../../Core/GrooveTypes.h"
#include <array>

namespace groove::ensemble
{
constexpr int kSeedSteps = 32; // 2 bars of 16ths
constexpr int kHatSteps = 16;  // one-bar loop, matches default generatorSteps

enum class Relation { follow = 0, complement, contrast };
enum class Phase { idle = 0, recording, ready, previewing };
enum class DynamicLevel { quiet = 0, asPlayed, lifted, peak, dropped };
enum class HatRate { quarter = 0, eighth, sixteenth };

inline int hatStepPeriod(HatRate rate)
{
    switch (rate)
    {
        case HatRate::quarter:   return 4;
        case HatRate::eighth:    return 2;
        case HatRate::sixteenth: return 1;
        default:                 return 2;
    }
}

inline bool isBeatFour(int step)
{
    return (((step % 16) + 16) % 16) == 12;
}

inline const char* dynamicLevelName(DynamicLevel level)
{
    switch (level)
    {
        case DynamicLevel::quiet:    return "quiet";
        case DynamicLevel::asPlayed: return "your beat";
        case DynamicLevel::lifted:   return "lifted";
        case DynamicLevel::peak:     return "biggest";
        case DynamicLevel::dropped:  return "dropped";
        default:                     return "";
    }
}

inline DynamicLevel dynamicLevelForPart(SongPart part)
{
    switch (part)
    {
        case SongPart::intro:
        case SongPart::outro:     return DynamicLevel::quiet;
        case SongPart::verse:     return DynamicLevel::asPlayed;
        case SongPart::prechorus: return DynamicLevel::lifted;
        case SongPart::chorus:    return DynamicLevel::peak;
        case SongPart::bridge:
        case SongPart::breakdown: return DynamicLevel::dropped;
        default:                  return DynamicLevel::asPlayed;
    }
}

struct SeedHit
{
    bool on = false;
    float velocity = 0.0f;
};

struct SeedGrid
{
    std::array<SeedHit, kSeedSteps> kick {};
    std::array<SeedHit, kSeedSteps> snare {};
    std::array<SeedHit, kSeedSteps> hats {};
};

struct SeedFeatures
{
    float density = 0.0f;
    float kickDensity = 0.0f;
    float snareDensity = 0.0f;
    float offbeatRatio = 0.0f;
    int impliedPulse = 2;
    std::array<bool, kHatSteps> kickMask {};
    std::array<bool, kHatSteps> snareMask {};
    std::array<bool, kHatSteps> occupied {};
    std::array<float, kHatSteps> accent {};
};

struct HatCandidate
{
    std::array<bool, kHatSteps> hits {};
    std::array<float, kHatSteps> velocity {};
    float score = 0.0f;
};

inline const SeedHit* seedHitForTrack(const SeedGrid& seed, int track, int index)
{
    if (index < 0 || index >= kSeedSteps)
        return nullptr;
    if (track == (int) DrumVoice::kick) return &seed.kick[(size_t) index];
    if (track == (int) DrumVoice::snare) return &seed.snare[(size_t) index];
    if (track == (int) DrumVoice::closedHat) return &seed.hats[(size_t) index];
    return nullptr;
}

inline SeedHit* seedHitForTrack(SeedGrid& seed, int track, int index)
{
    return const_cast<SeedHit*>(seedHitForTrack(static_cast<const SeedGrid&>(seed), track, index));
}

inline void stampSeedHit(SeedGrid& seed, int track, int index, float velocity)
{
    if (auto* slot = seedHitForTrack(seed, track, index))
    {
        slot->on = true;
        slot->velocity = juce::jmax(slot->velocity, juce::jlimit(0.05f, 1.2f, velocity));
    }
}

inline void clearSeedHit(SeedGrid& seed, int track, int index)
{
    if (auto* slot = seedHitForTrack(seed, track, index))
        *slot = {};
}

inline int countHits(const std::array<bool, kHatSteps>& hits)
{
    int n = 0;
    for (bool h : hits)
        if (h) ++n;
    return n;
}

inline const char* beatTrackName(int track)
{
    switch ((DrumVoice) juce::jlimit(0, kTracks - 1, track))
    {
        case DrumVoice::kick:      return "KICK";
        case DrumVoice::snare:     return "SNARE";
        case DrumVoice::clap:      return "CLAP";
        case DrumVoice::closedHat: return "HATS";
        case DrumVoice::openHat:   return "OHH";
        case DrumVoice::perc1:     return "TOM";
        case DrumVoice::perc2:     return "TOM2";
        case DrumVoice::fx:        return "CRASH";
        default:                   return "";
    }
}

inline juce::Colour beatTrackColour(int track)
{
    static const juce::Colour colours[] = {
        juce::Colour(0xffff8a22), juce::Colour(0xffff4f8a), juce::Colour(0xffffc438),
        juce::Colour(0xff7ac8ff), juce::Colour(0xff46d6d8), juce::Colour(0xffb85cff),
        juce::Colour(0xff8ed044), juce::Colour(0xffd5ebf7)
    };
    return colours[juce::jlimit(0, kTracks - 1, track)];
}
}