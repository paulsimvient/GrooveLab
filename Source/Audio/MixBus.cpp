#include "MixBus.h"
#include <cmath>

namespace groove
{
void MixBus::prepare(double sr, int blockSize)
{
    sampleRate = sr > 0.0 ? sr : 44100.0;
    const auto spec = juce::dsp::ProcessSpec {
        sampleRate,
        (juce::uint32) juce::jmax(1, blockSize),
        2
    };
    compressor.prepare(spec);
    compressor.setAttack(6.0f);
    compressor.setRelease(90.0f);

    auto delaySpec = spec;
    delaySpec.numChannels = 1;
    const int maxDelay = juce::jmax(4096, (int) std::ceil(sampleRate * 2.0));
    delayL.setMaximumDelayInSamples(maxDelay);
    delayR.setMaximumDelayInSamples(maxDelay);
    delayL.prepare(delaySpec);
    delayR.prepare(delaySpec);
    delayL.reset();
    delayR.reset();

    lastEqDb.fill(1.0e9f);
    for (int i = 0; i < kEqBands; ++i)
    {
        eq[(size_t) i].state = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
            sampleRate, kEqFreqs[i], 0.85f, 1.0f);
        eq[(size_t) i].prepare(spec);
    }
    updateEqIfNeeded();
}

void MixBus::reset()
{
    compressor.reset();
    delayL.reset();
    delayR.reset();
    for (auto& band : eq)
        band.reset();
}

void MixBus::updateEqIfNeeded()
{
    bool dirty = false;
    std::array<float, kEqBands> db {};
    for (int i = 0; i < kEqBands; ++i)
    {
        db[(size_t) i] = juce::jlimit(-12.0f, 12.0f, eqGainDb[(size_t) i].load());
        if (std::abs(db[(size_t) i] - lastEqDb[(size_t) i]) > 0.05f)
            dirty = true;
    }
    if (! dirty)
        return;

    lastEqDb = db;
    for (int i = 0; i < kEqBands; ++i)
    {
        const float freq = juce::jmin(kEqFreqs[i], (float) sampleRate * 0.45f);
        *eq[(size_t) i].state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
            sampleRate, freq, 0.85f, juce::Decibels::decibelsToGain(db[(size_t) i]));
    }
}

void MixBus::applyStemGain(juce::AudioBuffer<float>& buf, float vol, float left, float right)
{
    const int n = buf.getNumSamples();
    if (n <= 0)
        return;
    const float gL = juce::jlimit(0.0f, 2.0f, vol * left);
    const float gR = juce::jlimit(0.0f, 2.0f, vol * right);
    if (buf.getNumChannels() > 0)
        buf.applyGain(0, 0, n, gL);
    if (buf.getNumChannels() > 1)
        buf.applyGain(1, 0, n, gR);
    for (int ch = 2; ch < buf.getNumChannels(); ++ch)
        buf.applyGain(ch, 0, n, 0.5f * (gL + gR));
}

void MixBus::process(juce::AudioBuffer<float>& io)
{
    const int n = io.getNumSamples();
    const int ch = io.getNumChannels();
    if (n <= 0 || ch <= 0)
        return;

    const float c = juce::jlimit(0.0f, 1.0f, busComp.load());
    const float d = juce::jlimit(0.0f, 1.0f, busDelay.load());

    if (c > 0.01f)
    {
        compressor.setThreshold(juce::jmap(c, 0.0f, 1.0f, 0.0f, -26.0f));
        compressor.setRatio(juce::jmap(c, 0.0f, 1.0f, 1.0f, 5.5f));
        juce::dsp::AudioBlock<float> block(io);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        compressor.process(ctx);
        io.applyGain(juce::Decibels::decibelsToGain(juce::jmap(c, 0.0f, 1.0f, 0.0f, 7.0f)));
    }

    if (d > 0.005f)
    {
        const float delaySamp = (float) juce::jlimit(
            64.0,
            (double) delayL.getMaximumDelayInSamples() - 4.0,
            (60.0 / juce::jmax(40.0, bpm.load())) * kDelayBeats * sampleRate);
        delayL.setDelay(delaySamp);
        delayR.setDelay(delaySamp);
        const float fb = kDelayFeedback;
        float* L = io.getWritePointer(0);
        float* R = ch > 1 ? io.getWritePointer(1) : nullptr;
        for (int i = 0; i < n; ++i)
        {
            const float inL = L[i];
            const float inR = R != nullptr ? R[i] : inL;
            const float outL = delayL.popSample(0);
            const float outR = delayR.popSample(0);
            delayL.pushSample(0, inL + outR * fb);
            delayR.pushSample(0, inR + outL * fb);
            L[i] = inL + outL * d;
            if (R != nullptr)
                R[i] = inR + outR * d;
        }
    }

    updateEqIfNeeded();
    bool eqActive = false;
    for (int i = 0; i < kEqBands; ++i)
        if (std::abs(lastEqDb[(size_t) i]) > 0.15f)
            eqActive = true;
    if (eqActive)
    {
        juce::dsp::AudioBlock<float> block(io);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        for (auto& band : eq)
            band.process(ctx);
    }

    const float master = juce::jlimit(0.0f, 2.0f, masterVol.load());
    if (std::abs(master - 1.0f) > 0.001f)
        io.applyGain(master);
}
