#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <vector>

namespace groove
{
enum class ScaleMode
{
    autoDetect = 0,
    major,
    naturalMinor,
    dorian,
    phrygian,
    lydian,
    mixolydian,
    locrian,
    harmonicMinor,
    melodicMinor,
    pentMajor,
    pentMinor,
    blues
};

inline const char* scaleModeName(ScaleMode m)
{
    switch (m)
    {
        case ScaleMode::autoDetect:    return "AUTO";
        case ScaleMode::major:         return "MAJOR";
        case ScaleMode::naturalMinor:  return "MINOR";
        case ScaleMode::dorian:        return "DORIAN";
        case ScaleMode::phrygian:      return "PHRYGIAN";
        case ScaleMode::lydian:        return "LYDIAN";
        case ScaleMode::mixolydian:    return "MIXOLYDIAN";
        case ScaleMode::locrian:       return "LOCRIAN";
        case ScaleMode::harmonicMinor: return "HARM MIN";
        case ScaleMode::melodicMinor:  return "MEL MIN";
        case ScaleMode::pentMajor:     return "PENT MAJ";
        case ScaleMode::pentMinor:     return "PENT MIN";
        case ScaleMode::blues:         return "BLUES";
        default:                       return "AUTO";
    }
}

inline const char* pitchClassName(int pc)
{
    static const char* n[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return n[juce::jlimit(0, 11, pc)];
}

inline std::vector<int> scaleDegrees(ScaleMode mode)
{
    switch (mode)
    {
        case ScaleMode::major:         return { 0, 2, 4, 5, 7, 9, 11 };
        case ScaleMode::naturalMinor:  return { 0, 2, 3, 5, 7, 8, 10 };
        case ScaleMode::dorian:        return { 0, 2, 3, 5, 7, 9, 10 };
        case ScaleMode::phrygian:      return { 0, 1, 3, 5, 7, 8, 10 };
        case ScaleMode::lydian:        return { 0, 2, 4, 6, 7, 9, 11 };
        case ScaleMode::mixolydian:    return { 0, 2, 4, 5, 7, 9, 10 };
        case ScaleMode::locrian:       return { 0, 1, 3, 5, 6, 8, 10 };
        case ScaleMode::harmonicMinor: return { 0, 2, 3, 5, 7, 8, 11 };
        case ScaleMode::melodicMinor:  return { 0, 2, 3, 5, 7, 9, 11 };
        case ScaleMode::pentMajor:     return { 0, 2, 4, 7, 9 };
        case ScaleMode::pentMinor:     return { 0, 3, 5, 7, 10 };
        case ScaleMode::blues:         return { 0, 3, 5, 6, 7, 10 };
        case ScaleMode::autoDetect:
        default:                       return { 0, 2, 4, 5, 7, 9, 11 }; // major fallback
    }
}

inline int snapPcToScale(int pc, int keyRoot, ScaleMode mode)
{
    pc = ((pc % 12) + 12) % 12;
    keyRoot = ((keyRoot % 12) + 12) % 12;
    if (mode == ScaleMode::autoDetect)
        return pc;

    const auto degrees = scaleDegrees(mode);
    int best = degrees.front();
    int bestDist = 99;
    for (int d : degrees)
    {
        const int absPc = (keyRoot + d) % 12;
        int dist = std::abs(absPc - pc);
        dist = juce::jmin(dist, 12 - dist);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = absPc;
        }
    }
    return best;
}

struct HeardNote
{
    int step = 0;           // absolute timeline step within the listen take
    int midi = 36;          // exact MIDI note heard
    float velocity = 0.85f;
    int lengthSteps = 1;
    float confidence = 0.0f;
};

struct BarObservation
{
    int barIndex = 0;
    std::array<float, 12> chroma {}; // pitch-class energy, C..B
    int rootPc = 0;                  // 0=C .. 11=B
    bool minor = false;
    juce::String harmonyLabel;       // e.g. "Am", "F"
    std::vector<float> accentBeats;  // beat positions within bar (0..quarters)
    std::vector<int> heardPcs;       // strongest pitch classes heard (0..11)
    float density = 0.0f;            // onsets per beat, normalized-ish
    float confidence = 0.0f;         // 0..1 pitch detection confidence
};

struct MusicalObservation
{
    double bpm = 124.0;
    int bars = 4;
    int stepsPerBar = 16;
    float rhythmicActivity = 0.5f; // 0 sparse .. 1 busy
    float capturePeak = 0.0f;
    int detectedKeyRoot = 0;       // most-heard pitch class
    bool detectedMinor = false;
    bool hasKeyDetect = false;
    std::vector<BarObservation> barsObserved;
    std::vector<HeardNote> transcribed; // note-for-note pitch track
    juce::String summary;

    bool valid() const noexcept { return ! barsObserved.empty() || ! transcribed.empty(); }
};

struct BassGenParams
{
    float rootBias = 0.85f;   // prefer tonic / chord root
    float movement = 0.40f;
    float followRhythm = 0.80f;
    float simplify = 0.15f;
    int seed = 1;
    int keyRoot = 0; // C
    ScaleMode scaleMode = ScaleMode::autoDetect;
    bool lockKey = false; // when true (manual mode), force key/mode
    // If false, LISTEN never invents notes beyond what was transcribed/onset-detected.
    bool allowInvent = false;
};

struct GeneratedBassNote
{
    int step = 0;
    int note = 36; // MIDI
    float velocity = 0.85f;
    int lengthSteps = 2;
    juce::String reason;
};
}
