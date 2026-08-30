#pragma once
#include "GrooveTypes.h"

namespace groove
{
class GrooveState
{
public:
    GrooveState();

    std::array<Track, kTracks> tracks {};
    Song song;
    juce::String name { "Lil God Projector" };
    juce::String lastPluginPath;
    int lastPluginProgram = -1;
    juce::String lastPluginPatch;
    juce::String lastSynthPluginPath;
    juce::String lastSynthPatch;
    int lastSynthOctave = 0;
    int lastKeyboardTarget = 0; // 0 Moog ch2, 1 Keys ch4, 2 Poly ch3, 3 Drums ch1
    int keysPlugin = 0;         // unused; Electra and Prophet 5 are separate hosts
    juce::String lastKeysPluginPath;
    juce::String lastPolymaxPluginPath;
    juce::String lastElectraPatch;
    juce::String lastPolymaxPatch;
    std::array<MidiLane, kMidiLanes> midiLanes = makeDefaultMidiLanes();
    MixSettings mix;
    int soundMode = 2;
    double bpm = 124.0;
    bool recordQuantize = true;
    int recordQuantizeNote = kDefaultQuantizeNote;
    Meter meter = Meter::fourFour;
    MeterTransform meterTransform = MeterTransform::reflow;
    int selectedTrack = 0;
    int selectedStep = 0;

    void seedDefaultSong();
    void applySongSection(int index);
    void captureLiveToCurrentSection();

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