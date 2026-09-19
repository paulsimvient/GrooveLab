#pragma once
#include "ListenTypes.h"
#include <atomic>
#include <cstdint>
#include <vector>

namespace groove
{
// Realtime-safe capture + offline finalize into MusicalObservation.
class ListenAnalyzer
{
public:
    void prepare(double sampleRate, int maxBlock);
    void reset();

    // Call from audio thread while armed.
    void pushBlock(const float* mono, int numSamples);

    void arm(int bars, double bpm, int stepsPerBar, int meterQuartersPerBar);
    bool isArmed() const noexcept { return armed.load(); }
    bool isWaitingForNote() const noexcept { return armed.load() != 0 && waitingForNote.load() != 0; }
    bool isCapturing() const noexcept { return armed.load() != 0 && waitingForNote.load() == 0; }
    bool isComplete() const noexcept { return complete.load(); }
    float progress() const noexcept; // 0..1 once capturing; 0 while waiting
    int getSamplesCaptured() const noexcept { return samplesCaptured.load(); }
    int getSamplesNeeded() const noexcept { return samplesNeeded; }
    int getOnsetCount() const noexcept { return onsetCountAtomic.load(); }
    // Bumps whenever wait-for-note fires or an onset is detected (for INPUT monitor flash).
    uint32_t getTriggerSerial() const noexcept { return triggerSerial.load(); }
    int getTriggerKind() const noexcept { return triggerKind.load(); } // 1=start, 2=onset

    // Peak amplitude (0..1) that must be exceeded to leave WAIT FOR NOTE.
    void setStartThreshold(float peak01) noexcept;
    float getStartThreshold() const noexcept { return startThreshold.load(); }

    // Message thread: build observation after complete.
    MusicalObservation takeObservation();

private:
    void accumulateChroma(const float* x, int n);
    void detectOnsets(const float* x, int n, int globalOffset);
    bool blockLooksLikeNote(const float* mono, int numSamples) noexcept;
    void fireTrigger(int kind) noexcept;

    double sr = 44100.0;
    int maxBlock = 512;
    std::atomic<int> armed { 0 };
    std::atomic<int> waitingForNote { 0 };
    std::atomic<int> complete { 0 };
    std::atomic<int> samplesCaptured { 0 };
    std::atomic<uint32_t> triggerSerial { 0 };
    std::atomic<int> triggerKind { 0 };
    std::atomic<float> startThreshold { 0.04f }; // ~ -28 dB peak default
    float waitAmbientRms = 0.0f;
    int samplesNeeded = 0;
    int bars = 4;
    double bpm = 124.0;
    int stepsPerBar = 16;
    int quartersPerBar = 4;

    std::vector<float> capture;
    std::array<float, 12> chromaAccum {};
    std::vector<int> onsetSamples;
    std::atomic<int> onsetCountAtomic { 0 };
    float prevEnergy = 0.0f;
    float energyEnv = 0.0f;
    float waitEnergyEnv = 0.0f;
    // Note-gate latch: one physical note → one onset until level falls back below thresh.
    bool noteGateOpen = false;
    int noteGateQuietHops = 0;
    int lastOnsetSample = -1000000;

    juce::CriticalSection lock;
};
}
