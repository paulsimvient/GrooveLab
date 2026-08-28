#pragma once
#include "GrooveTypes.h"

namespace groove
{
class GrooveState
{
public:
    GrooveState();

    std::array<Track, kTracks> tracks {};
    double bpm = 124.0;
    int selectedTrack = 0;
    int selectedStep = 0;

    float similarity = 0.82f;       // 0 = radical, 1 = nearly identical
    int surpriseBudget = 3;         // max meaningful changes per evolve
    float lockResistance = 1.0f;    // 1 = never mutate locked traits

    VoiceParams effectiveParams(int track, int step) const;
    int effectiveMidiNote(int track, int step) const;
    bool trackIsAudible(int track) const;
    void setLock(int track, int step, Param param, float value);
    void clearLock(int track, int step, Param param);
    void clearAllLocks(int track, int step);

    juce::var toVar() const;
    bool fromVar(const juce::var&);
};
}