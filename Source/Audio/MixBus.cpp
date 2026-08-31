#include "MixBus.h"
#include <cmath>

namespace groove
{
namespace
{
float logFreq(float t, float lo, float hi)
{
    t = juce::jlimit(0.0f, 1.0f, t);
    return lo * std::pow(hi / lo, t);
}
}

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

    auto mono = spec;
    mono.numChannels = 1;
    const int maxDelay = juce::jmax(4096, (int) std::ceil(sampleRate * 2.0));
    for (auto& ch : dsp)
    {
        ch.lpL.prepare(mono);
        ch.lpR.prepare(mono);
        ch.hpL.prepare(mono);
        ch.hpR.prepare(mono);
        ch.delayL.setMaximumDelayInSamples(maxDelay);
        ch.delayR.setMaximumDelayInSamples(maxDelay);
        ch.delayL.prepare(mono);
        ch.delayR.prepare(mono);
        ch.delayL.reset();
        ch.delayR.reset();
        ch.lastLp = -1.0f;
        ch.lastHp = -1.0f;
        ch.delayWasOn = false;
    }

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
    for (auto& band : eq)
        band.reset();
    for (auto& ch : dsp)
    {
        ch.lpL.reset();
        ch.lpR.reset();
        ch.hpL.reset();
        ch.hpR.reset();
        ch.delayL.reset();
        ch.delayR.reset();
        ch.lastLp = -1.0f;
        ch.lastHp = -1.0f;
        ch.delayWasOn = false;
    }
}

void MixBus::pushChannelFx(const MixSettings& mix)
{
    for (int c = 0; c < kMixChannels; ++c)
    {
        const auto& fx = mix.channelFx[(size_t) c];
        auto& dst = live[(size_t) c];
        dst.lpOn.store(fx.lpOn ? 1 : 0);
        dst.lp.store(fx.lp);
        dst.hpOn.store(fx.hpOn ? 1 : 0);
        dst.hp.store(fx.hp);
        dst.delayOn.store(fx.delayOn ? 1 : 0);
        dst.delayWet.store(fx.delayWet);
        dst.delayFb.store(fx.delayFb);
        dst.delayNote.store(fx.delayNote);
        dst.reverbOn.store(fx.reverbOn ? 1 : 0);
        for (int s = 0; s < kSteps; ++s)
        {
            const auto& lock = mix.fxLocks[(size_t) c][(size_t) s];
            int mask = 0;
            if (lock.lp.has_value())
            {
                mask |= 1;
                dst.lockLp[(size_t) s].store(*lock.lp);
            }
            if (lock.hp.has_value())
            {
                mask |= 2;
                dst.lockHp[(size_t) s].store(*lock.hp);
            }
            if (lock.delayWet.has_value())
            {
                mask |= 4;
                dst.lockDelay[(size_t) s].store(*lock.delayWet);
            }
            dst.lockMask[(size_t) s].store(mask);
        }
    }
}

void MixBus::updateFilter(juce::dsp::IIR::Filter<float>& filter, bool lowpass, float norm, float& last)
{
    if (std::abs(norm - last) < 0.004f)
        return;
    last = norm;
    const float freq = lowpass ? logFreq(norm, 80.0f, 18000.0f)
                               : logFreq(norm, 20.0f, 4000.0f);
    const float clamped = juce::jmin(freq, (float) sampleRate * 0.45f);
    *filter.coefficients = lowpass
        ? *juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, clamped, 0.707f)
        : *juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, clamped, 0.707f);
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

void MixBus::processStem(int channel, juce::AudioBuffer<float>& io)
{
    if (channel < 0 || channel >= kMixChannels)
        return;
    const int n = io.getNumSamples();
    const int chans = io.getNumChannels();
    if (n <= 0 || chans <= 0)
        return;

    auto& fx = live[(size_t) channel];
    auto& proc = dsp[(size_t) channel];
    const int step = juce::jlimit(0, kSteps - 1, playStep.load());
    const int mask = fx.lockMask[(size_t) step].load();

    const bool lpOn = fx.lpOn.load() != 0 || (mask & 1);
    const bool hpOn = fx.hpOn.load() != 0 || (mask & 2);
    const bool delayOn = fx.delayOn.load() != 0;
    float lp = fx.lp.load();
    float hp = fx.hp.load();
    float wet = fx.delayWet.load();
    if (mask & 1) lp = fx.lockLp[(size_t) step].load();
    if (mask & 2) hp = fx.lockHp[(size_t) step].load();
    if (mask & 4) wet = fx.lockDelay[(size_t) step].load();

    if (lpOn)
    {
        updateFilter(proc.lpL, true, lp, proc.lastLp);
        proc.lpR.coefficients = proc.lpL.coefficients;
        float* L = io.getWritePointer(0);
        float* R = chans > 1 ? io.getWritePointer(1) : nullptr;
        for (int i = 0; i < n; ++i)
        {
            L[i] = proc.lpL.processSample(L[i]);
            if (R != nullptr)
                R[i] = proc.lpR.processSample(R[i]);
        }
    }

    if (hpOn)
    {
        updateFilter(proc.hpL, false, hp, proc.lastHp);
        proc.hpR.coefficients = proc.hpL.coefficients;
        float* L = io.getWritePointer(0);
        float* R = chans > 1 ? io.getWritePointer(1) : nullptr;
        for (int i = 0; i < n; ++i)
        {
            L[i] = proc.hpL.processSample(L[i]);
            if (R != nullptr)
                R[i] = proc.hpR.processSample(R[i]);
        }
    }

    if (delayOn || (mask & 4))
    {
        if (! proc.delayWasOn)
        {
            proc.delayL.reset();
            proc.delayR.reset();
        }
        proc.delayWasOn = true;
        const double beats = delayBeatsForNote(fx.delayNote.load());
        const float delaySamp = (float) juce::jlimit(
            64.0,
            (double) proc.delayL.getMaximumDelayInSamples() - 4.0,
            (60.0 / juce::jmax(40.0, bpm.load())) * beats * sampleRate);
        proc.delayL.setDelay(delaySamp);
        proc.delayR.setDelay(delaySamp);
        const float d = juce::jlimit(0.0f, 1.0f, wet);
        const float fb = juce::jlimit(0.0f, 0.85f, fx.delayFb.load());
        float* L = io.getWritePointer(0);
        float* R = chans > 1 ? io.getWritePointer(1) : nullptr;
        for (int i = 0; i < n; ++i)
        {
            const float inL = L[i];
            const float inR = R != nullptr ? R[i] : inL;
            const float outL = proc.delayL.popSample(0);
            const float outR = proc.delayR.popSample(0);
            proc.delayL.pushSample(0, inL + outR * fb);
            proc.delayR.pushSample(0, inR + outL * fb);
            L[i] = inL + outL * d;
            if (R != nullptr)
                R[i] = inR + outR * d;
        }
    }
    else
    {
        proc.delayWasOn = false;
    }
}

void MixBus::process(juce::AudioBuffer<float>& io)
{
    const int n = io.getNumSamples();
    const int ch = io.getNumChannels();
    if (n <= 0 || ch <= 0)
        return;

    const float c = juce::jlimit(0.0f, 1.0f, busComp.load());
    if (c > 0.01f)
    {
        compressor.setThreshold(juce::jmap(c, 0.0f, 1.0f, 0.0f, -26.0f));
        compressor.setRatio(juce::jmap(c, 0.0f, 1.0f, 1.0f, 5.5f));
        juce::dsp::AudioBlock<float> block(io);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        compressor.process(ctx);
        io.applyGain(juce::Decibels::decibelsToGain(juce::jmap(c, 0.0f, 1.0f, 0.0f, 7.0f)));
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
}
