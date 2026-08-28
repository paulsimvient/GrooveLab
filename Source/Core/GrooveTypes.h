#pragma once
#include <JuceHeader.h>
#include <array>
#include <optional>
#include <vector>

namespace groove
{
constexpr int kTracks = 8;
constexpr int kSteps  = 32;

enum class DrumVoice : int
{
    kick = 0, snare, clap, closedHat, openHat, perc1, perc2, fx
};

enum class Param : int
{
    pitch = 0,
    decay,
    transient,
    noise,
    filter,
    drive,
    space,
    blend,
    count
};

enum class StepRole : int { normal = 0, anchor, ghost, fill };
enum class EvolutionPolicy : int { protect = 0, anchorsOnly, free };

// Manual edits live on top of the algorithmic generator.
// inherit = use Euclidean result, forceOn = always trigger, forceOff = suppress.
enum class StepOverrideMode : int { inherit = 0, forceOn, forceOff };

inline constexpr int paramCount = static_cast<int>(Param::count);

inline juce::String voiceName(int i)
{
    static const char* names[] = {"KICK","SNARE","CLAP","CHH","OHH","PERC1","PERC2","FX"};
    return names[juce::jlimit(0, kTracks - 1, i)];
}

inline juce::String paramName(Param p)
{
    switch (p)
    {
        case Param::pitch:     return "pitch";
        case Param::decay:     return "decay";
        case Param::transient: return "transient";
        case Param::noise:     return "noise";
        case Param::filter:    return "filter";
        case Param::drive:     return "drive";
        case Param::space:     return "space";
        case Param::blend:     return "blend";
        default:               return "unknown";
    }
}

struct VoiceParams
{
    float pitchHz   = 100.0f;
    float decayMs   = 200.0f;
    float transient = 0.35f;
    float noise     = 0.0f;
    float filter    = 0.7f;
    float drive     = 0.0f;
    float space     = 0.08f;
    float blend     = 0.5f;
};

struct Step
{
    // Kept for backwards compatibility with v0.5 files. In v0.6 the sequencer
    // uses overrideMode, not active, as the primary manual edit representation.
    bool active = false;
    StepOverrideMode overrideMode = StepOverrideMode::inherit;
    float velocity = 1.0f;
    float probability = 1.0f;
    int ratchet = 1;
    StepRole role = StepRole::normal;
    std::optional<int> midiNote; // nullopt = track default UJAM kit note

    std::array<std::optional<float>, paramCount> locks {};

    bool hasAnyLock() const
    {
        if (midiNote.has_value()) return true;
        for (const auto& x : locks)
            if (x.has_value()) return true;
        return false;
    }
};

struct Track
{
    std::array<Step, kSteps> steps {};
    VoiceParams base {};

    // Algorithmic rhythm generator.
    int generatorSteps = 16;
    int pulses = 4;
    int rotate = 0;
    float division = 1.0f; // 0.25x, 0.5x, 1x, 2x, 4x

    // Track-level groove transforms.
    float probability = 0.96f;
    float velocity = 1.0f;
    float swing = 0.0f; // reserved for scheduling pass; serialized now

    EvolutionPolicy evolutionPolicy = EvolutionPolicy::anchorsOnly;
    float evolveAmount = 0.5f;
    bool muted = false;
    bool soloed = false;
    int midiNote = 36; // assigned UJAM kit key (C1–D#2)
};

enum class Meter : int
{
    fourFour = 0,
    threeFour,
    twoFour,
    fiveFour,
    sixEight,
    sevenEight,
    nineEight,
    twelveEight
};

inline constexpr int kMeterCount = 8;

inline const char* meterName(Meter meter)
{
    switch (meter)
    {
        case Meter::fourFour:    return "4/4";
        case Meter::threeFour:   return "3/4";
        case Meter::twoFour:     return "2/4";
        case Meter::fiveFour:    return "5/4";
        case Meter::sixEight:    return "6/8";
        case Meter::sevenEight:  return "7/8";
        case Meter::nineEight:   return "9/8";
        case Meter::twelveEight: return "12/8";
        default:                 return "4/4";
    }
}

inline int meterStepsPerBar(Meter meter)
{
    switch (meter)
    {
        case Meter::twoFour:     return 8;
        case Meter::threeFour:   return 12;
        case Meter::fourFour:    return 16;
        case Meter::fiveFour:    return 20;
        case Meter::sixEight:    return 12;
        case Meter::sevenEight:  return 14;
        case Meter::nineEight:   return 18;
        case Meter::twelveEight: return 24;
        default:                 return 16;
    }
}

inline double meterQuarterNotesPerBar(Meter meter)
{
    return (double) meterStepsPerBar(meter) / 4.0;
}

inline bool meterIsBarLine(Meter meter, int step)
{
    const int spb = meterStepsPerBar(meter);
    return spb > 0 && step >= 0 && (step % spb) == 0;
}

inline bool meterIsBeatLine(Meter meter, int step)
{
    if (step < 0)
        return false;
    if (meter == Meter::sevenEight)
    {
        const int local = step % 14;
        return local == 0 || local == 4 || local == 8;
    }
    if (meter == Meter::sixEight || meter == Meter::nineEight || meter == Meter::twelveEight)
        return (step % 6) == 0;
    return (step % 4) == 0;
}

enum class SongPart : int
{
    intro = 0, verse, prechorus, chorus, bridge, breakdown, fill, outro
};

inline const char* songPartName(SongPart part)
{
    switch (part)
    {
        case SongPart::intro:      return "INTRO";
        case SongPart::verse:      return "VERSE";
        case SongPart::prechorus:  return "PRE";
        case SongPart::chorus:     return "CHORUS";
        case SongPart::bridge:     return "BRIDGE";
        case SongPart::breakdown:  return "BREAK";
        case SongPart::fill:       return "FILL";
        case SongPart::outro:      return "OUTRO";
        default:                   return "PART";
    }
}

struct TrackShape
{
    int generatorSteps = 16;
    int pulses = 4;
    int rotate = 0;
    float division = 1.0f;
    float probability = 0.96f;
    float velocity = 1.0f;

    static TrackShape fromTrack(const Track& tr)
    {
        return { tr.generatorSteps, tr.pulses, tr.rotate, tr.division, tr.probability, tr.velocity };
    }
};

struct SongSection
{
    SongPart part = SongPart::verse;
    Meter meter = Meter::fourFour;
    int bars = 4;
    std::array<TrackShape, kTracks> shapes {};
};

struct Song
{
    bool follow = true;
    int current = 0;
    std::vector<SongSection> sections;
};
}
