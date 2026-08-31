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
    void setPlayStep(int step) noexcept { playStep.store(juce::jlimit(0, kSteps - 1, step)); }
    void processStem(int channel, juce::AudioBuffer<float>& io);
    void process(juce::AudioBuffer<float>& io);
    void pushChannelFx(const MixSettings&);
    bool isReverbEnabled(int channel) const noexcept
    {
        return channel >= 0 && channel < kMixChannels && live[(size_t) channel].reverbOn.load() != 0;
    }

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
    std::atomic<float> masterVol { 1.0f };
    std::array<std::atomic<float>, kEqBands> eqGainDb {};

    static void applyStemGain(juce::AudioBuffer<float>&, float vol, float left, float right);

private:
    struct ChannelFxLive
    {
        std::atomic<int> lpOn { 0 };
        std::atomic<float> lp { 1.0f };
        std::atomic<int> hpOn { 0 };
        std::atomic<float> hp { 0.0f };
        std::atomic<int> delayOn { 0 };
        std::atomic<float> delayWet { 0.0f };
        std::atomic<float> delayFb { 0.32f };
        std::atomic<int> delayNote { 2 };
        std::atomic<int> reverbOn { 0 };
        std::array<std::atomic<int>, kSteps> lockMask {};
        std::array<std::atomic<float>, kSteps> lockLp {};
        std::array<std::atomic<float>, kSteps> lockHp {};
        std::array<std::atomic<float>, kSteps> lockDelay {};
    };

    struct ChannelFxDsp
    {
        juce::dsp::IIR::Filter<float> lpL, lpR, hpL, hpR;
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayL { 192000 };
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayR { 192000 };
        float lastLp = -1.0f;
        float lastHp = -1.0f;
        bool delayWasOn = false;
    };

    void updateEqIfNeeded();
    void updateFilter(juce::dsp::IIR::Filter<float>&, bool lowpass, float norm, float& last);

    double sampleRate = 44100.0;
    std::atomic<double> bpm { 124.0 };
    std::atomic<int> playStep { 0 };
    juce::dsp::Compressor<float> compressor;
    using Peak = juce::dsp::IIR::Filter<float>;
    using PeakStereo = juce::dsp::ProcessorDuplicator<Peak, juce::dsp::IIR::Coefficients<float>>;
    std::array<PeakStereo, kEqBands> eq;
    std::array<float, kEqBands> lastEqDb {};
    std::array<ChannelFxLive, kMixChannels> live {};
    std::array<ChannelFxDsp, kMixChannels> dsp {};
};
}
