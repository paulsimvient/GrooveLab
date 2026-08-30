#pragma once
#include "../Core/GrooveState.h"
#include "../Core/EventJournal.h"
#include "../Core/EvolutionEngine.h"
#include "../Core/Ancestry.h"
#include "../Sequencer/Sequencer.h"
#include "DrumSynth.h"
#include <array>
#include <atomic>
#include <vector>

namespace groove
{
class GrooveEngine
{
public:
    GrooveEngine();

    void prepare(double sampleRate, int maxBlockSize);
    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiOut);
    void takeLaneMidi(juce::MidiBuffer& dest);
    void setInternalSynthEnabled(bool shouldPlay);
    bool isInternalSynthEnabled() const noexcept { return internalSynthEnabled.load(); }

    void setPlaying(bool shouldPlay);
    bool isPlaying() const noexcept { return playing.load(); }
    void togglePlaying() { setPlaying(! isPlaying()); }
    void resetTransport();
    void auditionSelected();

    GrooveState& state() noexcept { return grooveState; }
    const GrooveState& state() const noexcept { return grooveState; }

    void toggleStep(int track, int step); // cycles inherit -> forceOn -> forceOff
    void selectStep(int track, int step);
    void setBaseParam(int track, Param p, float value);
    void setStepParam(int track, int step, Param p, float value, bool createLock);
    void setLockFromBase(int track, int step, Param p);
    void clearLock(int track, int step, Param p);
    void clearAllLocks(int track, int step);
    void setVelocity(int track, int step, float value);
    void setProbability(int track, int step, float value);
    void setRatchet(int track, int step, int repeats);
    void setTrackSteps(int track, int steps);
    void setTrackPulses(int track, int pulses);
    void setTrackRotate(int track, int rotate);
    void setTrackDivision(int track, float division);
    void setPulseEnabled(int track, int step, bool shouldPlay);
    void setTrackMidiNote(int track, int note);
    void setTrackProbability(int track, float value);
    void setTrackVelocity(int track, float value);
    void setStepMidiNote(int track, int step, int note);
    void toggleMute(int track);
    void toggleSolo(int track);
    int effectiveMidiNote(int track, int step) const;
    bool trackIsAudible(int track) const;
    void setStepRole(int track, int step, StepRole role);
    void setTrackEvolutionPolicy(int track, EvolutionPolicy policy);
    void setTrackEvolveAmount(int track, float amount);

    bool isGeneratedHit(int track, int step) const;
    bool isResolvedHit(int track, int step) const;

    void beginPerform();
    void endPerform(bool commit);
    bool isPerforming() const noexcept { return performBase.has_value(); }

    EvolutionEngine::Result evolve(EvolutionEngine::Mode mode);
    int capture(const juce::String& label);
    bool back();

    const Ancestry& ancestry() const noexcept { return ancestryGraph; }

    void saveAutosave();
    bool loadAutosave();
    bool importMidiFile(const juce::File& file, juce::String& error);

    struct StoredGroove
    {
        juce::String name;
        juce::File file;
    };
    static juce::String legalGrooveName(const juce::String& name);
    juce::File groovesDir() const;
    juce::Array<StoredGroove> listStoredGrooves() const;
    bool saveStoredGroove(const juce::String& name, juce::String& error);
    bool loadStoredGroove(const juce::File& file, juce::String& error);
    bool saveGrooveFile(const juce::File& file, juce::String& error);
    bool loadGrooveFile(const juce::File& file, juce::String& error);
    void newProject();

    int addSongSection(SongPart part);
    void removeSongSection(int index);
    void duplicateSongSection(int index);
    void selectSongSection(int index, bool jumpOnBeat = false);
    int queuedSongSection() const noexcept { return queuedSection.load(); }
    int pendingBeatJumpSection() const noexcept { return pendingBeatJump.load(); }
    void moveSongSection(int from, int to);
    void setSongSectionBars(int index, int bars);
    void setSongSectionPart(int index, SongPart part);
    void setSectionTrackShape(int section, int track, const TrackShape& shape);
    void setMeter(Meter meter);
    void setMeterTransform(MeterTransform transform);
    void setSongFollow(bool shouldFollow);
    int songBarInSection() const;
    double songSectionProgress() const;

    void setRecording(bool shouldRecord);
    bool isRecording() const noexcept { return recording.load(); }
    void setRecordQuantize(bool shouldQuantize);
    bool isRecordQuantize() const noexcept { return grooveState.recordQuantize; }
    void setRecordQuantizeNote(int note);
    int getRecordQuantizeNote() const noexcept { return grooveState.recordQuantizeNote; }
    int keepCurrentTake();
    void restoreTake(int index);
    void removeTake(int index);
    void deleteCurrentTake();
    void pushIncomingMidi(const juce::MidiMessage&);
    void recordLanePatch(int lane, const juce::String& name, int kitIndex);
    struct PendingPatchApply { int lane = 0; juce::String name; int kitIndex = 0; };
    void drainPendingPatches(std::vector<PendingPatchApply>& dest);

    int currentStep() const noexcept { return sequencer.getCurrentStep(); }
    int currentStepForTrack(int track) const noexcept { return sequencer.getTrackStep(track); }

private:
    GrooveState grooveState;
    EventJournal journal;
    EvolutionEngine evolution;
    Ancestry ancestryGraph;
    Sequencer sequencer;
    DrumSynth synth;
    mutable juce::CriticalSection stateLock;
    struct PendingMidi
    {
        int samplesUntil = 0;
        juce::MidiMessage message;
    };
    struct IncomingHit
    {
        int note = -1;
        float velocity = 1.0f;
        int channel = 1;
        int ccNumber = -1;
        int ccValue = 0;
        bool noteOff = false;
        int extraType = 0;
        int extra1 = 0;
        int extra2 = 0;
    };
    std::vector<PendingMidi> pendingMidi;
    std::optional<Sequencer::Trigger> queuedAudition;
    std::atomic<bool> internalSynthEnabled { false };
    std::atomic<bool> pendingAllNotesOff { false };

    void scheduleMidi(juce::MidiBuffer& midiOut, const juce::MidiMessage& message,
                      int sample, int blockSamples);
    void emitTriggerMidi(juce::MidiBuffer& midiOut, const Sequencer::Trigger& tr, int blockSamples);
    void emitAllControllers(juce::MidiBuffer& midiOut, int track, int step, int sample, int blockSamples);
    void cancelPendingNoteOff(int channel, int note);
    void dropNoteOffs(juce::MidiBuffer& midiOut, int channel, int note);
    void syncCurrentSongSection();
    void advanceSong(int numSamples);
    void setRecordingLocked(bool shouldRecord);
    int keepCurrentTakeLocked();
    void removeTakeLocked(int index);
    void wipeLiveTakeLocked();
    void panicLaneNotesLocked();
    void clearLivePatternForRecord();
    bool parseIncomingHit(const juce::MidiMessage&, IncomingHit&);
    void recordIncomingNotes(const juce::MidiBuffer& incoming, juce::MidiBuffer& midiOut, int blockSamples);
    void recordNoteLocked(int note, float velocity, int channel, juce::MidiBuffer& midiOut, int blockSamples);
    void recordLaneNoteLocked(int lane, int note, float velocity, int startStep, int lengthSteps);
    void recordLaneCcLocked(int lane, int number, int value);
    void recordLaneExtraLocked(int lane, int type, int data1, int data2);
    void recordLanePatchLocked(int lane, const juce::String& name, int kitIndex);
    void emitLaneStep(int step, int blockSamples);
    void clearMidiLanesLocked();
    void closeOpenLaneNotesLocked();
    void beginOpenLaneNoteLocked(int lane, int note, float velocity);
    void finishOpenLaneNoteLocked(int lane, int note);
    int recordLoopLength() const;
    int quantizedRecordStep(int step, int length) const;
    void quantizeLiveTakeLocked();
    void quantizeMidiLaneLocked(MidiLane& lane, int length);
    void scheduleLaneMidi(const juce::MidiMessage& message, int sample, int blockSamples);
    void cancelPendingLaneNoteOff(int channel, int note);
    int laneStepSamples() const;
    juce::var documentToVar() const;
    bool documentFromVar(const juce::var&);
    bool writeDocumentToFile(const juce::File& file, juce::String& error);
    bool readDocumentFromFile(const juce::File& file, juce::String& error);
    double currentSampleRate = 44100.0;
    double songSamplesInSection = 0.0;
    std::atomic<int> queuedSection { -1 };
    std::atomic<int> pendingBeatJump { -1 };
    std::atomic<bool> playing { false };
    std::atomic<bool> recording { false };
    bool followBeforeRecord = true;
    std::vector<IncomingHit> incomingHits;
    juce::CriticalSection incomingLock;
    juce::MidiBuffer laneMidiBlock;
    std::vector<PendingMidi> pendingLaneMidi;
    int lastLaneStep = -1;
    int laneStepCounter = 0;
    std::array<std::array<int, 128>, kMidiLanes> openNoteCount {};
    std::array<std::array<int, 128>, kMidiLanes> openNoteStep {};
    std::array<std::array<float, 128>, kMidiLanes> openNoteVel {};
    std::vector<PendingPatchApply> pendingPatches;
    juce::CriticalSection patchLock;
    std::optional<GrooveState> performBase;
};
}