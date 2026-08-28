#include "DrumSynth.h"
#include <cmath>

namespace groove
{
void DrumSynth::prepare(double sampleRate, int)
{
    sr = sampleRate;

    for (auto& v : voices)
        v = {};

    pending.clear();

    // Small stereo feedback room. Deliberately simple and portable.
    const int roomSamples = juce::jmax(1024, (int) (sr * 0.23));
    roomL.assign((size_t) roomSamples, 0.0f);
    roomR.assign((size_t) roomSamples, 0.0f);
    roomIndex = 0;
}

float DrumSynth::whiteNoise()
{
    return random.nextFloat() * 2.0f - 1.0f;
}

float DrumSynth::onePoleLowpass(ActiveVoice& v, float x, float coeff)
{
    coeff = juce::jlimit(0.002f, 0.995f, coeff);
    v.lpState += coeff * (x - v.lpState);
    return v.lpState;
}

float DrumSynth::onePoleHighpass(ActiveVoice& v, float x, float coeff)
{
    coeff = juce::jlimit(0.002f, 0.995f, coeff);
    float y = coeff * (v.hpState + x - v.hpInputLast);
    v.hpState = y;
    v.hpInputLast = x;
    return y;
}

float DrumSynth::saturate(float x, float amount) const
{
    amount = juce::jlimit(0.0f, 1.0f, amount);
    const float gain = 1.0f + 14.0f * amount;
    return std::tanh(x * gain) / std::tanh(gain);
}

void DrumSynth::trigger(int type, const VoiceParams& p, float velocity)
{
    auto* slot = &voices[0];

    for (auto& v : voices)
    {
        if (! v.active)
        {
            slot = &v;
            break;
        }
    }

    *slot = {};
    slot->active = true;
    slot->type = juce::jlimit(0, kTracks - 1, type);
    slot->params = p;
    slot->velocity = juce::jlimit(0.0f, 1.2f, velocity);
    slot->phaseInc = juce::MathConstants<float>::twoPi
                   * juce::jmax(20.0f, p.pitchHz) / (float) sr;

    const float seconds = juce::jmax(0.018f, p.decayMs * 0.001f);
    slot->env = 1.0f;
    slot->envMul = std::pow(0.001f, 1.0f / (seconds * (float) sr));

    slot->transientEnv = juce::jlimit(0.0f, 1.0f, p.transient);
    float transientSeconds = 0.0025f + 0.020f * (1.0f - p.transient);
    slot->transientMul = std::pow(0.001f, 1.0f / (transientSeconds * (float) sr));

    slot->clapBurst = 0;
    slot->nextClapBurstSample = 0;
}

void DrumSynth::triggerRatchet(int voice,
                              const VoiceParams& params,
                              float velocity,
                              int repeats,
                              int stepSamples)
{
    repeats = juce::jlimit(1, 4, repeats);
    trigger(voice, params, velocity);

    if (repeats <= 1)
        return;

    const int spacing = juce::jmax(1, stepSamples / repeats);
    for (int r = 1; r < repeats; ++r)
        pending.push_back(PendingTrigger {
            spacing * r,
            voice,
            params,
            velocity * (1.0f - 0.08f * (float) r)
        });
}

float DrumSynth::renderVoiceSample(ActiveVoice& v)
{
    const auto& p = v.params;
    const float noise = whiteNoise();
    float raw = 0.0f;

    switch (v.type)
    {
        case 0: // KICK: sine body + pitch envelope + click
        {
            const float bend = 1.0f + (2.6f + p.transient * 2.0f) * v.env * v.env;
            const float body = std::sin(v.phase);
            const float sub  = std::sin(v.phase * 0.5f) * 0.22f;
            const float click = noise * v.transientEnv * (0.35f + 0.65f * p.transient);

            raw = body + sub + click;
            v.phase += v.phaseInc * bend;
            break;
        }

        case 1: // SNARE: pitched shell + filtered noise + snap
        {
            const float shell = std::sin(v.phase) * 0.68f
                              + std::sin(v.phase * 1.47f) * 0.20f;
            const float noisy = onePoleHighpass(v, noise, 0.89f);
            const float snap = noisy * (0.45f + p.transient * 0.9f) * v.transientEnv;

            raw = (shell * (1.0f - p.blend * 0.75f)
                + noisy * (0.25f + p.noise * 0.85f)
                + snap) * 1.55f;
            v.phase += v.phaseInc;
            break;
        }

        case 2: // CLAP: multiple short noise bursts + diffuse tail
        {
            const int burstSpacing = juce::jmax(1, (int) (sr * 0.011));
            float burst = 0.0f;

            if (v.ageSamples >= v.nextClapBurstSample && v.clapBurst < 4)
            {
                v.transientEnv = 1.0f;
                v.clapBurst++;
                v.nextClapBurstSample += burstSpacing;
            }

            burst = onePoleHighpass(v, noise, 0.93f)
                  * v.transientEnv * (0.7f + 0.5f * p.transient);

            float tail = onePoleLowpass(v, noise, 0.30f + p.filter * 0.55f)
                       * v.env * p.noise * 0.55f;

            raw = burst + tail;
            break;
        }

        case 3: // CLOSED HAT: six metallic partials + noise
        case 4: // OPEN HAT: same source, longer envelope comes from patch
        {
            float m = 0.0f;
            const float ratios[] = {1.0f, 1.342f, 1.781f, 2.319f, 2.956f, 3.667f};

            for (int i = 0; i < 6; ++i)
                m += std::sin(v.phase * ratios[i]);

            m *= 1.0f / 6.0f;
            m = (m >= 0.0f ? 1.0f : -1.0f) * std::sqrt(std::abs(m));

            float metallic = m * (0.55f + p.noise * 0.35f)
                           + noise * (0.25f + p.noise * 0.55f);

            raw = onePoleHighpass(v, metallic, 0.94f - p.filter * 0.12f);
            raw += noise * v.transientEnv * p.transient * 0.35f;
            v.phase += v.phaseInc;
            break;
        }

        case 5: // PERC1: FM / membrane
        {
            const float mod = std::sin(v.phase2) * (0.7f + 5.0f * p.noise);
            const float fm = std::sin(v.phase + mod);
            const float body = std::sin(v.phase * 0.51f) * 0.25f;

            raw = fm * (0.55f + 0.45f * (1.0f - p.blend)) + body;
            raw += noise * v.transientEnv * p.transient * 0.22f;

            v.phase += v.phaseInc;
            v.phase2 += v.phaseInc * (1.38f + p.blend * 2.2f);
            break;
        }

        case 6: // PERC2: ring/FM metallic voice
        {
            float a = std::sin(v.phase);
            float b = std::sin(v.phase2);
            float c = std::sin(v.phase3);

            raw = (a * b) * (0.65f + p.noise * 0.25f)
                + c * (0.25f + p.blend * 0.25f);
            raw += noise * v.transientEnv * 0.16f;

            v.phase += v.phaseInc;
            v.phase2 += v.phaseInc * 1.713f;
            v.phase3 += v.phaseInc * 2.381f;
            break;
        }

        default: // FX: downward chirp + noise texture
        {
            const float sweep = 0.45f + 4.2f * v.env * v.env;
            const float tone = std::sin(v.phase)
                             + std::sin(v.phase * 1.91f) * 0.32f;

            raw = tone * (1.0f - p.blend * 0.55f)
                + noise * (0.12f + p.noise * 0.72f);
            raw += noise * v.transientEnv * p.transient * 0.45f;

            v.phase += v.phaseInc * sweep;
            break;
        }
    }

    while (v.phase > juce::MathConstants<float>::twoPi)
        v.phase -= juce::MathConstants<float>::twoPi;
    while (v.phase2 > juce::MathConstants<float>::twoPi)
        v.phase2 -= juce::MathConstants<float>::twoPi;
    while (v.phase3 > juce::MathConstants<float>::twoPi)
        v.phase3 -= juce::MathConstants<float>::twoPi;

    // Voice-dependent filtering. High hats have already been high-passed.
    if (v.type != 3 && v.type != 4)
        raw = onePoleLowpass(v, raw, 0.04f + p.filter * 0.88f);

    raw = saturate(raw, p.drive);

    float output = raw * v.env * v.velocity;
    v.env *= v.envMul;
    v.transientEnv *= v.transientMul;
    v.ageSamples++;

    if (v.env < 0.0007f && v.transientEnv < 0.0007f)
        v.active = false;

    return output;
}

void DrumSynth::render(juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();

    if (roomL.empty() || roomR.empty())
        return;

    for (int i = 0; i < n; ++i)
    {
        for (auto it = pending.begin(); it != pending.end(); )
        {
            if (--it->samplesRemaining <= 0)
            {
                trigger(it->voice, it->params, it->velocity);
                it = pending.erase(it);
            }
            else
            {
                ++it;
            }
        }

        float dry = 0.0f;
        float send = 0.0f;

        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            float sample = renderVoiceSample(v);
            dry += sample * 0.20f;
            send += sample * juce::jlimit(0.0f, 1.0f, v.params.space) * 0.15f;
        }

        int tapR = (roomIndex + (int) (roomL.size() * 0.79)) % (int) roomL.size();
        float wetL = roomL[(size_t) roomIndex];
        float wetR = roomR[(size_t) tapR];

        roomL[(size_t) roomIndex] = send + wetR * 0.34f;
        roomR[(size_t) roomIndex] = send * 0.83f + wetL * 0.29f;

        roomIndex++;
        if (roomIndex >= (int) roomL.size())
            roomIndex = 0;

        float left  = juce::jlimit(-1.0f, 1.0f, dry + wetL);
        float right = juce::jlimit(-1.0f, 1.0f, dry + wetR);

        if (buffer.getNumChannels() > 0) buffer.addSample(0, i, left);
        if (buffer.getNumChannels() > 1) buffer.addSample(1, i, right);

        for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
            buffer.addSample(ch, i, 0.5f * (left + right));
    }
}
}