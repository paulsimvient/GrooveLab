#include "ListenMonitor.h"
#include <cmath>

namespace
{
juce::String pcLabel(int pc)
{
    static const char* n[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return n[juce::jlimit(0, 11, pc)];
}

float dbFromAmp(float a)
{
    return 20.0f * std::log10(juce::jmax(1.0e-6f, a));
}
}

float ListenMonitorPanel::dbToPeak(float db)
{
    return juce::Decibels::decibelsToGain(db);
}

float ListenMonitorPanel::peakToDb(float peak)
{
    return juce::Decibels::gainToDecibels(juce::jmax(1.0e-6f, peak), -100.0f);
}

ListenMonitorPanel::ListenMonitorPanel(std::function<ListenMonitorSnapshot()> fetchState)
    : fetch(std::move(fetchState))
{
    title.setText("AUDIO INPUT MONITOR", juce::dontSendNotification);
    title.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, juce::Colour(0xff9ef0ff));
    addAndMakeVisible(title);

    prefsHint.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1aa0b8));
    prefsHint.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    prefsHint.onClick = [this]
    {
        if (onOpenPreferences) onOpenPreferences();
    };
    addAndMakeVisible(prefsHint);

    privacyButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff5a2a40));
    privacyButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    privacyButton.setTooltip("Open macOS Privacy settings for Microphone");
    privacyButton.onClick = [this]
    {
        if (onOpenMicPrivacy) onOpenMicPrivacy();
    };
    addAndMakeVisible(privacyButton);

    builtInMicButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff8a3a18));
    builtInMicButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    builtInMicButton.setTooltip("Keep current speakers/headphones, switch only the INPUT to the MacBook / Built-in microphone");
    builtInMicButton.onClick = [this]
    {
        if (onUseBuiltInMic) onUseBuiltInMic();
    };
    addAndMakeVisible(builtInMicButton);

    inputDeviceBox.setTextWhenNothingSelected("Select input device...");
    inputDeviceBox.onChange = [this]
    {
        if (refreshingBox) return;
        const auto name = inputDeviceBox.getText();
        if (name.isNotEmpty() && onSelectInput)
            onSelectInput(name);
    };
    addAndMakeVisible(inputDeviceBox);

    threshLabel.setText("START THRESH", juce::dontSendNotification);
    threshLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    threshLabel.setColour(juce::Label::textColourId, juce::Colour(0xffffb84a));
    threshLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(threshLabel);

    threshSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    threshSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 22);
    threshSlider.setRange(-50.0, -3.0, 0.5);
    threshSlider.setValue(-28.0, juce::dontSendNotification);
    threshSlider.setTextValueSuffix(" dB");
    threshSlider.setTooltip("Peak level that must be exceeded to leave WAIT FOR NOTE and start capturing. Raise to ignore room noise.");
    threshSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xffffb84a));
    threshSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xff3a2a10));
    threshSlider.onValueChange = [this]
    {
        if (syncingThresh) return;
        const float peak = dbToPeak((float) threshSlider.getValue());
        if (onStartThreshold) onStartThreshold(peak);
    };
    addAndMakeVisible(threshSlider);

    history.fill(0.0f);
    triggerMarks.fill(0);
    startTimerHz(30);
}

ListenMonitorPanel::~ListenMonitorPanel()
{
    stopTimer();
}

void ListenMonitorPanel::timerCallback()
{
    if (fetch)
        snap = fetch();

    const bool newTrig = snap.triggerSerial != 0 && snap.triggerSerial != lastTriggerSerial;
    if (newTrig)
    {
        lastTriggerSerial = snap.triggerSerial;
        flashKind = snap.triggerKind;
        flashUntilMs = juce::Time::getMillisecondCounterHiRes() + (flashKind == 1 ? 650.0 : 280.0);
        ++triggerFlashCount;
    }

    // Live gate preview — fires when peak crosses START THRESH even if LISTEN is idle.
    if (snap.aboveThreshold && ! wasAboveThreshold)
    {
        flashKind = 3; // crossed threshold
        flashUntilMs = juce::Time::getMillisecondCounterHiRes() + 320.0;
    }
    wasAboveThreshold = snap.aboveThreshold;

    pushHistory(snap.peak, newTrig || (snap.aboveThreshold && flashKind == 3
                                       && juce::Time::getMillisecondCounterHiRes() < flashUntilMs
                                       && juce::Time::getMillisecondCounterHiRes() > flashUntilMs - 50.0));
    syncThresholdSliderFromSnap();
    refreshInputList();
    repaint();
}

void ListenMonitorPanel::syncThresholdSliderFromSnap()
{
    if (threshSlider.isMouseButtonDown())
        return;
    const double wantDb = (double) peakToDb(snap.startThreshold);
    if (std::abs(threshSlider.getValue() - wantDb) > 0.4)
    {
        syncingThresh = true;
        threshSlider.setValue(wantDb, juce::dontSendNotification);
        syncingThresh = false;
    }
}

void ListenMonitorPanel::refreshInputList()
{
    refreshingBox = true;
    const auto current = inputDeviceBox.getText();
    inputDeviceBox.clear(juce::dontSendNotification);
    int selected = 0;
    for (int i = 0; i < snap.availableInputs.size(); ++i)
    {
        inputDeviceBox.addItem(snap.availableInputs[i], i + 1);
        if (snap.availableInputs[i] == snap.inputDevice
            || (current.isNotEmpty() && snap.availableInputs[i] == current))
            selected = i + 1;
    }
    if (selected > 0)
        inputDeviceBox.setSelectedId(selected, juce::dontSendNotification);
    refreshingBox = false;
}

void ListenMonitorPanel::pushHistory(float peak, bool markTrigger)
{
    history[(size_t) historyWrite] = juce::jlimit(0.0f, 1.0f, peak);
    triggerMarks[(size_t) historyWrite] = markTrigger ? 1 : 0;
    historyWrite = (historyWrite + 1) % (int) history.size();
}

void ListenMonitorPanel::resized()
{
    auto r = getLocalBounds().reduced(12, 10);
    auto top = r.removeFromTop(28);
    title.setBounds(top.removeFromLeft(180));
    prefsHint.setBounds(top.removeFromRight(100));
    top.removeFromRight(4);
    privacyButton.setBounds(top.removeFromRight(100));
    top.removeFromRight(4);
    builtInMicButton.setBounds(top.removeFromRight(140));

    r.removeFromTop(6);
    inputDeviceBox.setBounds(r.removeFromTop(28));
    r.removeFromTop(6);
    auto threshRow = r.removeFromTop(28);
    threshLabel.setBounds(threshRow.removeFromLeft(100));
    threshSlider.setBounds(threshRow);
}

void ListenMonitorPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff071018));

    auto r = getLocalBounds().reduced(12, 10);
    r.removeFromTop(34);
    r.removeFromTop(34); // combo
    r.removeFromTop(6);
    r.removeFromTop(28); // start thresh slider
    r.removeFromTop(6);
    auto info = r.removeFromTop(70);
    g.setColour(juce::Colour(0xff0d2430));
    g.fillRoundedRectangle(info.toFloat(), 8.0f);
    g.setColour(juce::Colour(0xff1aa0b8));
    g.drawRoundedRectangle(info.toFloat(), 8.0f, 1.2f);

    auto infoText = info.reduced(12, 8);
    g.setColour(juce::Colour(0xff9ef0ff));
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText("INPUT   " + snap.inputDevice, infoText.removeFromTop(18), juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xffc8dbe3));
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("OUTPUT  " + snap.outputDevice, infoText.removeFromTop(16), juce::Justification::centredLeft);
    g.drawText("Channels: " + snap.inputChannels
               + " (" + juce::String(snap.activeInputCount) + " active)"
               + "   |   " + juce::String(snap.sampleRate, 0) + " Hz"
               + "   |   buffer " + juce::String(snap.bufferSize),
               infoText, juce::Justification::centredLeft);

    r.removeFromTop(10);

    const bool flashing = juce::Time::getMillisecondCounterHiRes() < flashUntilMs;
    const float flashAlpha = flashing
        ? (float) juce::jlimit(0.0, 1.0, (flashUntilMs - juce::Time::getMillisecondCounterHiRes()) / 280.0)
        : 0.0f;

    auto meters = r.removeFromTop(78);
    g.setColour(juce::Colour(0xff0b1b25));
    g.fillRoundedRectangle(meters.toFloat(), 8.0f);
    if (flashing)
    {
        g.setColour((flashKind == 1 ? juce::Colour(0xffffb84a) : juce::Colour(0xff57d98f)).withAlpha(0.25f + 0.45f * flashAlpha));
        g.drawRoundedRectangle(meters.toFloat(), 8.0f, 2.5f);
    }

    auto meterInner = meters.reduced(12, 10);
    const float peakDb = dbFromAmp(snap.peak);
    const float rmsDb = dbFromAmp(snap.rms);
    const bool signal = snap.peak > 0.004f;

    g.setColour(juce::Colour(0xff8ab4c4));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    auto levelTitle = meterInner.removeFromTop(16);
    g.drawText("INPUT LEVEL", levelTitle.removeFromLeft(90), juce::Justification::centredLeft);

    // Live gate vs START THRESH — always visible so the slider is obviously working.
    {
        auto gate = levelTitle.removeFromLeft(118).toFloat().reduced(2, 0);
        g.setColour(snap.aboveThreshold ? juce::Colour(0xff2a4a18) : juce::Colour(0xff3a1814));
        g.fillRoundedRectangle(gate, 3.0f);
        g.setColour(snap.aboveThreshold ? juce::Colour(0xff57d98f) : juce::Colour(0xffff6a4a));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(snap.aboveThreshold ? "ABOVE THRESH" : "BELOW THRESH",
                   gate.toNearestInt(), juce::Justification::centred);
    }

    if (flashing)
    {
        g.setColour(flashKind == 1 ? juce::Colour(0xffffb84a)
                    : flashKind == 3 ? juce::Colour(0xffffb84a)
                    : juce::Colour(0xff57d98f));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        const char* msg = flashKind == 1 ? "TRIGGERED  START"
                        : flashKind == 3 ? "CROSSED THRESH"
                        : "TRIGGERED  NOTE";
        g.drawText(msg, levelTitle, juce::Justification::centredRight);
    }
    else if (triggerFlashCount > 0)
    {
        g.setColour(juce::Colour(0xff6a8490));
        g.setFont(juce::FontOptions(10.0f));
        g.drawText("triggers " + juce::String(triggerFlashCount),
                   levelTitle, juce::Justification::centredRight);
    }

    auto peakRow = meterInner.removeFromTop(20);
    g.setColour(juce::Colour(0xff6a8490));
    g.drawText("PEAK", peakRow.removeFromLeft(40), juce::Justification::centredLeft);
    auto peakBar = peakRow.removeFromLeft(peakRow.getWidth() - 70).toFloat().reduced(0, 3);
    g.setColour(juce::Colour(0xff132430));
    g.fillRoundedRectangle(peakBar, 3.0f);
    const float peakNorm = juce::jlimit(0.0f, 1.0f, (peakDb + 60.0f) / 60.0f);
    g.setColour(flashing ? (flashKind == 1 ? juce::Colour(0xffffb84a) : juce::Colour(0xff57d98f))
                         : (signal ? (snap.peak > 0.9f ? juce::Colour(0xffff5a4a) : juce::Colour(0xff3fd0e8))
                                   : juce::Colour(0xff2a3a44)));
    g.fillRoundedRectangle(peakBar.withWidth(juce::jmax(2.0f, peakBar.getWidth() * peakNorm)), 3.0f);
    // Threshold marker on the peak meter.
    {
        const float threshDb = peakToDb(snap.startThreshold);
        const float threshNorm = juce::jlimit(0.0f, 1.0f, (threshDb + 60.0f) / 60.0f);
        const float x = peakBar.getX() + peakBar.getWidth() * threshNorm;
        g.setColour(juce::Colour(0xffffb84a));
        g.drawLine(x, peakBar.getY() - 2.0f, x, peakBar.getBottom() + 2.0f, 2.0f);
    }
    g.setColour(juce::Colours::white);
    g.drawText(juce::String(peakDb, 1) + " dB", peakRow, juce::Justification::centredRight);

    meterInner.removeFromTop(4);
    auto rmsRow = meterInner.removeFromTop(20);
    g.setColour(juce::Colour(0xff6a8490));
    g.drawText("RMS", rmsRow.removeFromLeft(40), juce::Justification::centredLeft);
    auto rmsBar = rmsRow.removeFromLeft(rmsRow.getWidth() - 70).toFloat().reduced(0, 3);
    g.setColour(juce::Colour(0xff132430));
    g.fillRoundedRectangle(rmsBar, 3.0f);
    const float rmsNorm = juce::jlimit(0.0f, 1.0f, (rmsDb + 60.0f) / 60.0f);
    g.setColour(signal ? juce::Colour(0xff57d98f) : juce::Colour(0xff2a3a44));
    g.fillRoundedRectangle(rmsBar.withWidth(juce::jmax(2.0f, rmsBar.getWidth() * rmsNorm)), 3.0f);
    g.setColour(juce::Colours::white);
    g.drawText(juce::String(rmsDb, 1) + " dB", rmsRow, juce::Justification::centredRight);

    r.removeFromTop(10);

    auto hist = r.removeFromTop(72);
    g.setColour(juce::Colour(0xff0b1b25));
    g.fillRoundedRectangle(hist.toFloat(), 8.0f);
    if (flashing)
    {
        g.setColour((flashKind == 1 ? juce::Colour(0xffffb84a) : juce::Colour(0xff57d98f)).withAlpha(0.35f * flashAlpha));
        g.fillRoundedRectangle(hist.toFloat(), 8.0f);
    }
    auto histInner = hist.reduced(12, 8);
    g.setColour(juce::Colour(0xff8ab4c4));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    juce::String liveMsg;
    if (flashing)
        liveMsg = flashKind == 1 ? "LIVE INPUT  -  CAPTURE STARTED"
                                 : "LIVE INPUT  -  NOTE TRIGGER";
    else if (snap.activeInputCount <= 0)
        liveMsg = "LIVE INPUT  -  no input channels enabled (click USE MACBOOK MIC or Preferences)";
    else if (signal)
        liveMsg = "LIVE INPUT  -  signal detected";
    else
        liveMsg = "LIVE INPUT  -  silence (macOS may be blocking the mic — click MIC PRIVACY)";
    g.setColour(flashing ? (flashKind == 1 ? juce::Colour(0xffffb84a) : juce::Colour(0xff57d98f))
                         : juce::Colour(0xff8ab4c4));
    g.drawText(liveMsg, histInner.removeFromTop(16), juce::Justification::centredLeft);

    auto plot = histInner.toFloat();
    g.setColour(juce::Colour(0xff132430));
    g.fillRoundedRectangle(plot, 4.0f);
    juce::Path path;
    const int n = (int) history.size();
    for (int i = 0; i < n; ++i)
    {
        const int idx = (historyWrite + i) % n;
        const float x = plot.getX() + plot.getWidth() * (float) i / (float) juce::jmax(1, n - 1);
        const float y = plot.getBottom() - 2.0f - (plot.getHeight() - 4.0f) * history[(size_t) idx];
        if (i == 0) path.startNewSubPath(x, y);
        else path.lineTo(x, y);

        if (triggerMarks[(size_t) idx] != 0)
        {
            g.setColour(juce::Colour(0xffffb84a).withAlpha(0.85f));
            g.drawLine(x, plot.getY() + 1.0f, x, plot.getBottom() - 1.0f, 1.4f);
        }
    }
    g.setColour(juce::Colour(0xff3fd0e8));
    g.strokePath(path, juce::PathStrokeType(1.6f));

    r.removeFromTop(10);

    auto listenBox = r.removeFromTop(86);
    const bool waiting = snap.listenArmed && snap.listenWaiting;
    g.setColour(waiting ? juce::Colour(0xff3a2a10)
               : snap.listenArmed ? juce::Colour(0xff3a1814)
               : juce::Colour(0xff0d2430));
    g.fillRoundedRectangle(listenBox.toFloat(), 8.0f);
    g.setColour(waiting ? juce::Colour(0xffffb84a)
               : snap.listenArmed ? juce::Colour(0xffff6a4a)
               : juce::Colour(0xff1aa0b8));
    g.drawRoundedRectangle(listenBox.toFloat(), 8.0f, 1.4f);

    auto lb = listenBox.reduced(12, 10);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    juce::String listenTitle = "LISTEN";
    if (waiting)
        listenTitle += "  WAIT FOR NOTE";
    else if (snap.listenArmed)
        listenTitle += "  CAPTURING  " + juce::String((int) std::round(snap.listenProgress * 100.0f)) + "%";
    else if (snap.hasObservation)
        listenTitle += "  LAST CAPTURE";
    else
        listenTitle += "  IDLE";
    g.drawText(listenTitle, lb.removeFromTop(18), juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xffc8dbe3));
    g.setFont(juce::FontOptions(11.0f));
    if (waiting)
        g.drawText(snap.aboveThreshold
                       ? "Above START THRESH — play a clear attack to begin capture"
                       : "Below START THRESH — raise level or lower the slider",
                   lb.removeFromTop(18), juce::Justification::centredLeft);
    else
        g.drawText("Samples  " + juce::String(snap.samplesCaptured) + " / " + juce::String(snap.samplesNeeded)
                   + "     Onsets  " + juce::String(snap.onsetCount),
                   lb.removeFromTop(18), juce::Justification::centredLeft);

    if (waiting)
    {
        auto bar = lb.removeFromTop(14).toFloat();
        const float pulse = 0.35f + 0.35f * std::sin((float) juce::Time::getMillisecondCounter() * 0.008f);
        g.setColour(juce::Colour(0xff403010));
        g.fillRoundedRectangle(bar, 3.0f);
        g.setColour(juce::Colour(0xffffb84a).withAlpha(0.55f + 0.35f * pulse));
        g.fillRoundedRectangle(bar.withWidth(juce::jmax(3.0f, bar.getWidth() * pulse)), 3.0f);
    }
    else if (snap.listenArmed)
    {
        auto bar = lb.removeFromTop(14).toFloat();
        g.setColour(juce::Colour(0xff401810));
        g.fillRoundedRectangle(bar, 3.0f);
        g.setColour(juce::Colour(0xffff8a5a));
        g.fillRoundedRectangle(bar.withWidth(juce::jmax(3.0f, bar.getWidth() * snap.listenProgress)), 3.0f);
    }
    else if (snap.hasObservation)
    {
        g.setColour(juce::Colour(0xff9ef0ff));
        g.drawFittedText(snap.observation.summary, lb, juce::Justification::centredLeft, 2);
    }
    else
    {
        g.setColour(juce::Colour(0xff8a9aa4));
        g.drawText("Get the meter moving, then press LISTEN on the piano roll.",
                   lb, juce::Justification::centredLeft);
    }

    r.removeFromTop(10);

    auto detail = r;
    g.setColour(juce::Colour(0xff0b1b25));
    g.fillRoundedRectangle(detail.toFloat(), 8.0f);
    auto d = detail.reduced(12, 10);
    g.setColour(juce::Colour(0xff8ab4c4));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("WHAT IT HEARD", d.removeFromTop(18), juce::Justification::centredLeft);

    if (! snap.hasObservation || ! snap.observation.valid())
    {
        g.setColour(juce::Colour(0xff6a8490));
        g.setFont(juce::FontOptions(11.0f));
        g.drawFittedText("System Sound prefs can see the mic while this app is blocked.\n"
                         "Allow Lil God Projector under Privacy & Security > Microphone, then speak again.\n"
                         "Click MIC PRIVACY to open that settings pane.",
                         d, juce::Justification::topLeft, 4);
        return;
    }

    std::array<float, 12> chroma {};
    for (const auto& bar : snap.observation.barsObserved)
        for (int i = 0; i < 12; ++i)
            chroma[(size_t) i] += bar.chroma[(size_t) i];
    float maxC = 0.0f;
    for (float v : chroma) maxC = juce::jmax(maxC, v);
    if (maxC < 1.0e-6f) maxC = 1.0f;

    auto chromaArea = d.removeFromTop(78);
    const float barW = chromaArea.getWidth() / 12.0f;
    for (int i = 0; i < 12; ++i)
    {
        auto col = chromaArea.withX((int) (chromaArea.getX() + i * barW)).withWidth((int) barW).reduced(2, 0);
        const float h = (col.getHeight() - 16) * (chroma[(size_t) i] / maxC);
        auto fill = juce::Rectangle<float>((float) col.getX(), (float) col.getBottom() - 14.0f - h,
                                           (float) col.getWidth(), h);
        g.setColour(juce::Colour(0xff3fd0e8).withAlpha(0.85f));
        g.fillRoundedRectangle(fill, 2.0f);
        g.setColour(juce::Colour(0xff8ab4c4));
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText(pcLabel(i), col.removeFromBottom(14), juce::Justification::centred);
    }

    d.removeFromTop(6);
    g.setColour(juce::Colour(0xffc8dbe3));
    g.setFont(juce::FontOptions(11.0f));
    juce::String barsLine;
    for (int i = 0; i < (int) snap.observation.barsObserved.size(); ++i)
    {
        const auto& b = snap.observation.barsObserved[(size_t) i];
        if (i) barsLine += "   ";
        barsLine += "Bar " + juce::String(i + 1) + ": " + b.harmonyLabel
                 + " (" + juce::String((int) b.accentBeats.size()) + " accents)";
    }
    g.drawFittedText(barsLine, d, juce::Justification::topLeft, 4);
}

ListenMonitorWindow::ListenMonitorWindow(std::function<ListenMonitorSnapshot()> fetchState,
                                         std::function<void()> onOpenPreferences,
                                         std::function<void(juce::String)> onSelectInput,
                                         std::function<void()> onUseBuiltInMic,
                                         std::function<void()> onOpenMicPrivacy,
                                         std::function<void(float)> onStartThreshold,
                                         std::function<void()> onCloseFn)
    : juce::DocumentWindow("INPUT MONITOR", juce::Colour(0xff071018),
                           juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      panel(std::move(fetchState)),
      onClose(std::move(onCloseFn))
{
    panel.setOpenPreferences(std::move(onOpenPreferences));
    panel.setSelectInputDevice(std::move(onSelectInput));
    panel.setUseBuiltInMic(std::move(onUseBuiltInMic));
    panel.setOpenMicPrivacy(std::move(onOpenMicPrivacy));
    panel.setStartThresholdChanged(std::move(onStartThreshold));
    setUsingNativeTitleBar(true);
    setResizable(true, false);
    setResizeLimits(460, 560, 960, 1000);
    setContentNonOwned(&panel, false);
    setAlwaysOnTop(true);
    centreWithSize(560, 660);
}

void ListenMonitorWindow::closeButtonPressed()
{
    setVisible(false);
    if (onClose) onClose();
}
