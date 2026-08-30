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
    static constexpr float kDelayFeedback = 0.38f;
    static constexpr double kDelayBeats = 0.5; // 1/8 note at current BPM
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
