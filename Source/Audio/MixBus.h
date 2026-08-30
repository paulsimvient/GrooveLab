#pragma once
#include <JuceHeader.h>
#include "../Core/GrooveTypes.h"
#include <atomic>
#include <array>

namespace groove
{
class MixBus
{
public:
    void prepare(double sampleRate, int blockSize);
    void reset();
    void setBpm(double bpm) noexcept { this->bpm.store(bpm); }
    void process(juce::AudioBuffer<float>& io);

    std::atomic<float> drumVol { 1.0f };
    std::atomic<float> drumLeft { 1.0f };
    std::atomic<float> drumRight { 1.0f };
    std::atomic<float> synthVol { 1.0f };
    std::atomic<float> synthLeft { 1.0f };
    std::atomic<float> synthRight { 1.0f };
    std::atomic<float> keysVol { 1.0f };
    std::atomic<float> keysLeft { 1.0f };
    std::atomic<float> keysRight { 1.0f };
    std::atomic<float> polyVol { 1.0f };
    std::atomic<float> polyLeft { 1.0f };
    std::atomic<float> polyRight { 1.0f };
    std::atomic<float> busComp { 0.22f };
    std::atomic<float> busDelay { 0.0f };
    std::atomic<float> masterVol { 1.0f };
    std::atomic<float> delayFeedback { 0.38f };
    std::atomic<int> delayNote { 2 };
    static constexpr int kDelayNoteCount = 5;
    static constexpr double kDelayNoteBeats[kDelayNoteCount] = { 1.0, 0.75, 0.5, 1.0 / 3.0, 0.25 };
    static constexpr const char* kDelayNoteNames[kDelayNoteCount] = { "1/4", "1/8D", "1/8", "1/8T", "1/16" };
    static double delayBeatsForNote(int note) noexcept;
    std::array<std::atomic<float>, kEqBands> eqGainDb {};

    static void applyStemGain(juce::AudioBuffer<float>&, float vol, float left, float right);

private:
    void updateEqIfNeeded();

    double sampleRate = 44100.0;
    std::atomic<double> bpm { 124.0 };
    juce::dsp::Compressor<float> compressor;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayL { 192000 };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayR { 192000 };
    using Peak = juce::dsp::IIR::Filter<float>;
    using PeakStereo = juce::dsp::ProcessorDuplicator<Peak, juce::dsp::IIR::Coefficients<float>>;
    std::array<PeakStereo, kEqBands> eq;
    std::array<float, kEqBands> lastEqDb {};
};
}
