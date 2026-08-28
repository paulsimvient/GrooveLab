#pragma once
#include <JuceHeader.h>
#include <array>
#include <optional>

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
    float probability = 1.0f;
    float velocity = 1.0f;
    float swing = 0.0f; // reserved for scheduling pass; serialized now

    EvolutionPolicy evolutionPolicy = EvolutionPolicy::anchorsOnly;
    float evolveAmount = 0.5f;
    bool muted = false;
    bool soloed = false;
    int midiNote = 36; // assigned UJAM kit key (C1–D#2)
};
}
