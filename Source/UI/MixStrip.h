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
    void setBpm(double bpm);
    int getActiveMidiChannel() const noexcept { return activeMidiChannel; }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

    juce::ComboBox drumSound, synthSound, keysSound, polySound;
    juce::TextButton drumPrev { "◀" }, drumNext { "▶" };
    juce::TextButton synthPrev { "◀" }, synthNext { "▶" };
    juce::TextButton keysPrev { "◀" }, keysNext { "▶" };
    juce::TextButton polyPrev { "◀" }, polyNext { "▶" };
    juce::TextButton drumUi { "DRUM UI" }, synthUi { "MOOG UI" };
    juce::TextButton keysUi { "KEYS UI" }, polyUi { "P5 UI" };

    bool isDrumPopupActive() const { return drumSound.isPopupActive(); }
    bool isSynthPopupActive() const { return synthSound.isPopupActive(); }
    bool isKeysPopupActive() const { return keysSound.isPopupActive(); }
    bool isPolyPopupActive() const { return polySound.isPopupActive(); }

private:
    void configure(juce::Slider&, double min, double max, double step);
    void configureEq(juce::Slider&);
    void bind(juce::Slider&);
    void stylePicker(juce::ComboBox&, juce::TextButton& prev, juce::TextButton& next);

    juce::Slider drumVol, drumLeft, drumRight;
    juce::Slider synthVol, synthLeft, synthRight;
    juce::Slider keysVol, keysLeft, keysRight;
    juce::Slider polyVol, polyLeft, polyRight;
    juce::Slider masterVol, busComp, busDelay;
    std::array<juce::Slider, groove::kEqBands> eqSliders;
    std::array<juce::Label, groove::kEqBands> eqLabels;
    void refreshChannelHighlight();
    void refreshDelayInfo();

    juce::Label drumsTitle, synthTitle, keysTitle, polyTitle, busTitle;
    int activeMidiChannel = 0;
    juce::Label drumVolL, drumLL, drumRL;
    juce::Label synthVolL, synthLL, synthRL;
    juce::Label keysVolL, keysLL, keysRL;
    juce::Label polyVolL, polyLL, polyRL;
    juce::Label masterL, compL, delayL, delayInfo, eqTitle;
    double currentBpm = 124.0;
    bool refreshing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixStrip)
};
