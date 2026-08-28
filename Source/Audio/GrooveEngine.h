#pragma once
#include "../Core/GrooveState.h"
#include "../Core/EventJournal.h"
#include "../Core/EvolutionEngine.h"
#include "../Core/Ancestry.h"
#include "../Sequencer/Sequencer.h"
#include "DrumSynth.h"
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
    std::vector<PendingMidi> pendingMidi;
    std::optional<Sequencer::Trigger> queuedAudition;
    std::atomic<bool> internalSynthEnabled { true };
    std::atomic<bool> pendingAllNotesOff { false };

    void scheduleMidi(juce::MidiBuffer& midiOut, const juce::MidiMessage& message,
                      int sample, int blockSamples);
    void emitTriggerMidi(juce::MidiBuffer& midiOut, const Sequencer::Trigger& tr, int blockSamples);
    void emitAllControllers(juce::MidiBuffer& midiOut, int track, int step, int sample, int blockSamples);
    double currentSampleRate = 44100.0;
    std::atomic<bool> playing { true };
    std::optional<GrooveState> performBase;
};
}