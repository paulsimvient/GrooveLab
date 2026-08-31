#pragma once
#include "../Core/GrooveTypes.h"

namespace groove
{
class DrumSynth
{
public:
    void prepare(double sampleRate, int maxBlockSize);
    void trigger(int voice, const VoiceParams& params, float velocity);
    void triggerRatchet(int voice, const VoiceParams& params, float velocity, int repeats, int stepSamples);
    void render(juce::AudioBuffer<float>& buffer);

private:
    struct ActiveVoice
    {
        bool active = false;
        int type = 0;

        VoiceParams params {};
        float velocity = 1.0f;

        float phase = 0.0f;
        float phase2 = 0.0f;
        float phase3 = 0.0f;
        float phaseInc = 0.0f;

        float env = 0.0f;
        float envMul = 0.999f;
        float transientEnv = 0.0f;
        float transientMul = 0.95f;

        float lpState = 0.0f;
        float hpState = 0.0f;
        float hpInputLast = 0.0f;

        int ageSamples = 0;
        int clapBurst = 0;
        int nextClapBurstSample = 0;
    };

    struct PendingTrigger
    {
        int samplesRemaining = 0;
        int voice = 0;
        VoiceParams params {};
        float velocity = 1.0f;
    };

    float renderVoiceSample(ActiveVoice&);
    float whiteNoise();
    float onePoleLowpass(ActiveVoice&, float x, float coeff);
    float onePoleHighpass(ActiveVoice&, float x, float coeff);
    float saturate(float x, float amount) const;

    static constexpr int maxVoices = 48;
    std::array<ActiveVoice, maxVoices> voices {};
    static constexpr int maxPendingTriggers = 64;
    std::array<PendingTrigger, maxPendingTriggers> pending {};
    int pendingCount = 0;

    std::vector<float> roomL, roomR;
    int roomIndex = 0;

    double sr = 44100.0;
    juce::Random random;
};
}