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
    std::function<void(int channel)> onOpenInstrumentUi;
    std::function<void(bool compact)> onCompactModeChanged;

    void loadFrom(const groove::MixSettings&);
    void saveTo(groove::MixSettings&) const;
    void setActiveMidiChannel(int channel);
    void setSelectedStep(int step) { selectedStep = juce::jlimit(0, groove::kSteps - 1, step); }
    void setBpm(double bpm) { currentBpm = bpm; }
    int getActiveMidiChannel() const noexcept { return activeMidiChannel; }
    bool isCompactMode() const noexcept { return compactMode; }
    void setCompactMode(bool compact);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

    juce::ComboBox drumSound, synthSound, keysSound;
    juce::TextButton polySound { "MAXPOLY SOUND" };
    juce::TextButton drumPrev { "◀" }, drumNext { "▶" };
    juce::TextButton synthPrev { "◀" }, synthNext { "▶" };
    juce::TextButton keysPrev { "◀" }, keysNext { "▶" };
    juce::TextButton polyPrev { "◀" }, polyNext { "▶" };
    juce::TextButton drumUi { "DRUM UI" }, synthUi { "MOOG UI" };
    juce::TextButton keysUi { "KEYS UI" }, polyUi { "MAXPOLY UI" };
    juce::TextButton polyBrowse { "FIND" };

    bool isDrumPopupActive() const { return drumSound.isPopupActive(); }
    bool isSynthPopupActive() const { return synthSound.isPopupActive(); }
    bool isKeysPopupActive() const { return keysSound.isPopupActive(); }
    bool isPolyPopupActive() const { return false; }

private:
    void configure(juce::Slider&, double min, double max, double step);
    void stylePicker(juce::ComboBox&, juce::TextButton& prev, juce::TextButton& next);
    void stylePicker(juce::TextButton&, juce::TextButton& prev, juce::TextButton& next);
    void styleChip(juce::TextButton&, bool on);
    void notify();
    void refreshChannelHighlight();
    void refreshFxEditor();
    void bindFxKnob(juce::Slider&, int index);

    std::array<juce::Slider, groove::kMixChannels> vols;
    std::array<juce::Slider, groove::kMixChannels> pans;
    static constexpr int kFxCount = 5;
    std::array<std::array<juce::Slider, kFxCount>, groove::kMixChannels> fxSends;
    std::array<std::array<juce::TextButton, kFxCount>, groove::kMixChannels> fxSendButtons;
    std::array<std::array<juce::Label, kFxCount>, groove::kMixChannels> fxSendLabels;
    std::array<juce::Label, groove::kMixChannels> volLabels, panLabels;

    juce::Slider masterVol, fxReturn;
    juce::Label masterL, returnL;
    juce::ComboBox fxSelect;
    juce::TextButton fxEnable { "OFF" };
    juce::TextButton compactButton { "COMPACT" };
    std::array<juce::Slider, 8> fxKnobs;
    std::array<juce::Label, 8> fxLabels;

    juce::Label drumsTitle, synthTitle, keysTitle, polyTitle, busTitle;
    groove::SharedFxSettings sharedFx;
    int selectedFx = 0; // 0 Paradise, 1 Century, 2 Compressor, 3 Galaxy, 4 Capitol
    int activeMidiChannel = 0;
    int selectedStep = 0;
    double currentBpm = 124.0;
    bool refreshing = false;
    bool compactMode = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixStrip)
};
