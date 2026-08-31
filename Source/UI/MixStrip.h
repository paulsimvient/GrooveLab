#pragma once
#include <JuceHeader.h>
#include "../Core/GrooveTypes.h"
#include <array>

class MixStrip : public juce::Component
{
public:
    MixStrip();

    std::function<void()> onChanged;
    std::function<void(int channel)> onChannelClicked;

    void loadFrom(const groove::MixSettings&);
    void saveTo(groove::MixSettings&) const;
    void setActiveMidiChannel(int channel);
    void setSelectedStep(int step);
    void setBpm(double bpm);
    int getActiveMidiChannel() const noexcept { return activeMidiChannel; }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

    juce::ComboBox drumSound, synthSound, keysSound;
    juce::TextButton polySound { "PROPHET SOUND" };
    juce::TextButton drumPrev { "◀" }, drumNext { "▶" };
    juce::TextButton synthPrev { "◀" }, synthNext { "▶" };
    juce::TextButton keysPrev { "◀" }, keysNext { "▶" };
    juce::TextButton polyPrev { "◀" }, polyNext { "▶" };
    juce::TextButton drumUi { "DRUM UI" }, synthUi { "MOOG UI" };
    juce::TextButton keysUi { "KEYS UI" }, polyUi { "P5 UI" };
    juce::TextButton polyBrowse { "FIND" };

    bool isDrumPopupActive() const { return drumSound.isPopupActive(); }
    bool isSynthPopupActive() const { return synthSound.isPopupActive(); }
    bool isKeysPopupActive() const { return keysSound.isPopupActive(); }
    bool isPolyPopupActive() const { return false; }

private:
    struct Inserts
    {
        juce::TextButton lpBtn { "+ LP" };
        juce::TextButton hpBtn { "+ HP" };
        juce::TextButton delayBtn { "+ DLY" };
        juce::TextButton lockBtn { "+ LOCK" };
        juce::Slider lp, hp, delayWet, delayFeedback;
        juce::TextButton delayNote { "1/8" };
        juce::Label lpL, hpL, delayL, delayFeedbackL;

        juce::ComboBox fxSelect;
        juce::TextButton fxEnable { "FX OFF" };
        juce::Slider fxA, fxB, fxC, fxD, fxE, fxF, fxG, fxH;
        juce::Label fxAL, fxBL, fxCL, fxDL, fxEL, fxFL, fxGL, fxHL;
        int selectedFx = 0; // 0 drive, 1 ring, 2 Capitol Chambers, 3 Paradise Guitar Studio
        bool driveOn = false, ringOn = false, combOn = false, reverbOn = false, paradiseOn = false;
        float baseDriveAmount = 0.25f, baseDriveTone = 0.55f, baseDriveMix = 1.0f;
        float baseRingFreq = 0.35f, baseRingDepth = 1.0f, baseRingMix = 0.5f;
        float baseCombFreq = 0.35f, baseCombFeedback = 0.45f, baseCombMix = 0.5f;
        float baseReverbSize = 0.45f, baseReverbDecay = 0.55f, baseReverbWet = 0.38f;
        float baseReverbPreDelay = 0.12f, baseReverbWidth = 0.75f;
        float baseReverbBass = 0.5f, baseReverbMid = 0.5f, baseReverbTreble = 0.5f, baseReverbVolume = 0.85f;
        float baseParadiseInput = 0.70f, baseParadiseGate = 0.00f, baseParadisePre = 0.70f;
        float baseParadiseAmp = 0.78f, baseParadiseCab = 0.80f, baseParadiseRoom = 0.25f;
        float baseParadiseOutput = 0.90f, baseParadiseLimit = 0.55f;
        bool lpOn = false;
        bool hpOn = false;
        bool delayOn = false;
        int delayNoteIndex = 2;
        float baseLp = 1.0f;
        float baseHp = 0.0f;
        float baseDelayWet = 0.22f;
        float baseDelayFeedback = 0.32f;
        int baseDelayNote = 2;
    };

    void configure(juce::Slider&, double min, double max, double step);
    void configureEq(juce::Slider&);
    void bind(juce::Slider&);
    void bindFx(juce::Slider&, int channel, int kind); // 0 lp, 1 hp, 2 delay wet, 3 delay feedback
    void bindCreativeFx(juce::Slider&, int channel, int paramIndex);
    void refreshCreativeEditor(int channel);
    void showFxForSelectedStep(int channel);
    void stylePicker(juce::ComboBox&, juce::TextButton& prev, juce::TextButton& next);
    void stylePicker(juce::TextButton&, juce::TextButton& prev, juce::TextButton& next);
    void styleChip(juce::TextButton&, bool on, juce::Colour onColour);
    void setupInserts(int channel);
    void refreshInserts();
    void refreshChannelHighlight();
    void toggleInsert(int channel, int kind); // 0 lp, 1 hp, 2 delay, 3 lock
    void cycleDelayNote(int channel);
    void notify();

    juce::Slider drumVol, drumLeft, drumRight;
    juce::Slider synthVol, synthLeft, synthRight;
    juce::Slider keysVol, keysLeft, keysRight;
    juce::Slider polyVol, polyLeft, polyRight;
    juce::Slider masterVol, busComp;
    std::array<juce::Slider, groove::kEqBands> eqSliders;
    std::array<juce::Label, groove::kEqBands> eqLabels;
    std::array<Inserts, groove::kMixChannels> inserts;
    std::array<std::array<groove::ChannelFxLock, groove::kSteps>, groove::kMixChannels> fxLocks {};

    juce::Label drumsTitle, synthTitle, keysTitle, polyTitle, busTitle;
    int activeMidiChannel = 0;
    int selectedStep = 0;
    juce::Label drumVolL, drumLL, drumRL;
    juce::Label synthVolL, synthLL, synthRL;
    juce::Label keysVolL, keysLL, keysRL;
    juce::Label polyVolL, polyLL, polyRL;
    juce::Label masterL, compL, eqTitle;
    double currentBpm = 124.0;
    bool refreshing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixStrip)
};
