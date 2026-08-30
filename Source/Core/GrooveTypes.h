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
    int midiNote = 36; // C1 default; full MIDI 0–127 is legal
};

enum class MeterTransform : int { reflow = 0, crop, squeeze };

inline constexpr int kMeterTransformCount = 3;

inline const char* meterTransformName(MeterTransform transform)
{
    switch (transform)
    {
        case MeterTransform::crop:    return "CROP";
        case MeterTransform::reflow:  return "REFLOW";
        case MeterTransform::squeeze: return "SQUEEZE";
        default:                      return "REFLOW";
    }
}

inline const char* meterTransformHint(MeterTransform transform)
{
    switch (transform)
    {
        case MeterTransform::crop:    return "Drop extra beats in each bar";
        case MeterTransform::reflow:  return "Keep the groove, move the bar lines";
        case MeterTransform::squeeze: return "Fit the old bar into the new bar";
        default:                      return "";
    }
}

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

inline int fitStepsToMeter(int currentSteps, Meter fromMeter, Meter toMeter)
{
    juce::ignoreUnused(fromMeter);
    const int newSpb = juce::jmax(1, meterStepsPerBar(toMeter));
    currentSteps = juce::jmax(1, currentSteps);
    // Already a whole number of bars in the new meter (12, 24, …) — leave it.
    if (currentSteps % newSpb == 0)
        return juce::jlimit(1, kSteps, currentSteps);
    // Otherwise snap to one bar so 4/4 16 → 3/4 12, never 24.
    return juce::jlimit(1, kSteps, newSpb);
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

inline constexpr int kMidiLanes = 4;

struct MidiLaneNote
{
    int step = 0;
    int note = 60;
    float velocity = 1.0f;
    int lengthSteps = 1;
};

struct MidiLanePatch
{
    int step = 0;
    juce::String name;
    int kitIndex = 0;
};

struct MidiLaneCc
{
    int step = 0;
    int number = 1;
    int value = 0;
};

struct MidiLaneExtra
{
    int step = 0;
    int type = 0; // 1 pitch, 2 pressure, 3 poly aftertouch, 4 program
    int data1 = 0;
    int data2 = 0;
};

struct MidiLane
{
    int channel = 1;
    juce::String name;
    std::vector<MidiLaneNote> notes;
    std::vector<MidiLanePatch> patches;
    std::vector<MidiLaneCc> ccs;
    std::vector<MidiLaneExtra> extras;
};

inline const char* midiLaneName(int lane)
{
    static constexpr const char* names[] = { "DRUMS", "MOOG", "PROPHET", "KEYS" };
    return names[juce::jlimit(0, kMidiLanes - 1, lane)];
}

inline int midiLaneChannel(int lane)
{
    static constexpr int ch[] = { 1, 2, 3, 4 };
    return ch[juce::jlimit(0, kMidiLanes - 1, lane)];
}

inline int midiLaneIndexForChannel(int channel)
{
    switch (channel)
    {
        case 1: return 0;
        case 2: return 1;
        case 3: return 2;
        case 4: return 3;
        default: return -1;
    }
}

inline std::array<MidiLane, kMidiLanes> makeDefaultMidiLanes()
{
    std::array<MidiLane, kMidiLanes> lanes {};
    for (int i = 0; i < kMidiLanes; ++i)
    {
        lanes[(size_t) i].channel = midiLaneChannel(i);
        lanes[(size_t) i].name = midiLaneName(i);
    }
    return lanes;
}

struct PatternTake
{
    juce::String label;
    std::array<TrackShape, kTracks> shapes {};
    std::array<std::array<Step, kSteps>, kTracks> steps {};
    std::array<MidiLane, kMidiLanes> midiLanes = makeDefaultMidiLanes();
};

struct SongSection
{
    SongPart part = SongPart::verse;
    Meter meter = Meter::fourFour;
    int bars = 4;
    std::array<TrackShape, kTracks> shapes {};
    std::array<std::array<Step, kSteps>, kTracks> steps {};
    std::array<MidiLane, kMidiLanes> midiLanes = makeDefaultMidiLanes();
    std::vector<PatternTake> takes;
    int currentTake = -1;
};

inline void copyPatternFromTracks(SongSection& section, const std::array<Track, kTracks>& tracks)
{
    for (int t = 0; t < kTracks; ++t)
    {
        section.shapes[(size_t) t] = TrackShape::fromTrack(tracks[t]);
        section.steps[(size_t) t] = tracks[t].steps;
    }
}

inline void applyPatternToTracks(const SongSection& section, std::array<Track, kTracks>& tracks)
{
    for (int t = 0; t < kTracks; ++t)
    {
        auto& tr = tracks[t];
        const auto& sh = section.shapes[(size_t) t];
        tr.generatorSteps = juce::jlimit(1, kSteps, sh.generatorSteps);
        tr.pulses = juce::jlimit(0, tr.generatorSteps, sh.pulses);
        tr.rotate = sh.rotate;
        tr.division = sh.division;
        tr.probability = juce::jlimit(0.0f, 1.0f, sh.probability);
        tr.velocity = juce::jlimit(0.0f, 1.2f, sh.velocity);
        tr.steps = section.steps[(size_t) t];
    }
}

inline constexpr int kEqBands = 7;
inline constexpr float kEqFreqs[kEqBands] = { 80.0f, 200.0f, 500.0f, 1000.0f, 2500.0f, 6000.0f, 12000.0f };
inline constexpr const char* kEqLabels[kEqBands] = { "80", "200", "500", "1k", "2k5", "6k", "12k" };

struct MixSettings
{
    float drumVol = 1.0f;
    float drumLeft = 1.0f;
    float drumRight = 1.0f;
    float synthVol = 1.0f;
    float synthLeft = 1.0f;
    float synthRight = 1.0f;
    float keysVol = 1.0f;
    float keysLeft = 1.0f;
    float keysRight = 1.0f;
    float polyVol = 1.0f;
    float polyLeft = 1.0f;
    float polyRight = 1.0f;
    float busComp = 0.22f;
    float busDelay = 0.0f;
    float masterVol = 1.0f;
    float delayFeedback = 0.38f;
    int delayNote = 2; // 1/8 — see MixBus::kDelayNoteNames
    std::array<float, kEqBands> eqGainDb {};
};

inline constexpr int kMaxTakes = 12;

inline constexpr int kQuantizeNoteCount = 5;
inline constexpr int kDefaultQuantizeNote = 2; // 1/8
inline constexpr const char* kQuantizeNoteNames[kQuantizeNoteCount] = {
    "1/2", "1/4", "1/8", "1/16", "1/32"
};

// Sequencer step is a 16th at 1x. 1/32 is the finest available (same as 1/16).
inline int quantizeGridSteps(int quantizeNote)
{
    static constexpr int grids[] = { 8, 4, 2, 1, 1 };
    return grids[juce::jlimit(0, kQuantizeNoteCount - 1, quantizeNote)];
}

inline int snapStepToGrid(int step, int grid, int length)
{
    length = juce::jmax(1, length);
    grid = juce::jmax(1, grid);
    step = ((step % length) + length) % length;
    if (grid <= 1)
        return step;
    const int nearest = ((step + grid / 2) / grid) * grid;
    return nearest >= length ? 0 : nearest;
}

struct Song
{
    bool follow = false;
    int current = 0;
    std::vector<SongSection> sections;
};
}
