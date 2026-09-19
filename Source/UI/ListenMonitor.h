#pragma once
#include <JuceHeader.h>
#include "../Audio/ListenTypes.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>

struct ListenMonitorSnapshot
{
    juce::String outputDevice { "No device" };
    juce::String inputDevice { "No input" };
    juce::String inputChannels { "none" };
    int activeInputCount = 0;
    double sampleRate = 0.0;
    int bufferSize = 0;
    float peak = 0.0f;
    float rms = 0.0f;
    bool listenArmed = false;
    bool listenWaiting = false;
    bool listenComplete = false;
    float listenProgress = 0.0f;
    int samplesCaptured = 0;
    int samplesNeeded = 0;
    int onsetCount = 0;
    uint32_t triggerSerial = 0;
    int triggerKind = 0; // 1=capture start, 2=onset
    float startThreshold = 0.04f; // linear peak 0..1
    bool aboveThreshold = false;
    bool hasObservation = false;
    groove::MusicalObservation observation;
    juce::StringArray availableInputs;
};

class ListenMonitorPanel : public juce::Component,
                           private juce::Timer
{
public:
    explicit ListenMonitorPanel(std::function<ListenMonitorSnapshot()> fetchState);
    ~ListenMonitorPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void setOpenPreferences(std::function<void()> fn) { onOpenPreferences = std::move(fn); }
    void setSelectInputDevice(std::function<void(juce::String)> fn) { onSelectInput = std::move(fn); }
    void setUseBuiltInMic(std::function<void()> fn) { onUseBuiltInMic = std::move(fn); }
    void setOpenMicPrivacy(std::function<void()> fn) { onOpenMicPrivacy = std::move(fn); }
    void setStartThresholdChanged(std::function<void(float)> fn) { onStartThreshold = std::move(fn); }

private:
    void timerCallback() override;
    void pushHistory(float peak, bool markTrigger);
    void refreshInputList();
    void syncThresholdSliderFromSnap();
    static float dbToPeak(float db);
    static float peakToDb(float peak);

    std::function<ListenMonitorSnapshot()> fetch;
    std::function<void()> onOpenPreferences;
    std::function<void(juce::String)> onSelectInput;
    std::function<void()> onUseBuiltInMic;
    std::function<void()> onOpenMicPrivacy;
    std::function<void(float)> onStartThreshold;
    ListenMonitorSnapshot snap;
    std::array<float, 180> history {};
    std::array<uint8_t, 180> triggerMarks {};
    int historyWrite = 0;
    uint32_t lastTriggerSerial = 0;
    int flashKind = 0;           // 1=start, 2=onset
    double flashUntilMs = 0.0;
    int triggerFlashCount = 0;
    bool wasAboveThreshold = false;
    juce::Label title;
    juce::TextButton prefsHint { "PREFERENCES" };
    juce::TextButton privacyButton { "MIC PRIVACY" };
    juce::TextButton builtInMicButton { "USE MACBOOK MIC" };
    juce::ComboBox inputDeviceBox;
    juce::Label threshLabel;
    juce::Slider threshSlider;
    bool refreshingBox = false;
    bool syncingThresh = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ListenMonitorPanel)
};

class ListenMonitorWindow : public juce::DocumentWindow
{
public:
    ListenMonitorWindow(std::function<ListenMonitorSnapshot()> fetchState,
                        std::function<void()> onOpenPreferences,
                        std::function<void(juce::String)> onSelectInput,
                        std::function<void()> onUseBuiltInMic,
                        std::function<void()> onOpenMicPrivacy,
                        std::function<void(float)> onStartThreshold,
                        std::function<void()> onCloseFn);
    void closeButtonPressed() override;

private:
    ListenMonitorPanel panel;
    std::function<void()> onClose;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ListenMonitorWindow)
};
