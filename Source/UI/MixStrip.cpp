#include "MixStrip.h"

MixStrip::MixStrip()
{
    setOpaque(false);
    drumsTitle.setText("DRUMS  ·  CH1", juce::dontSendNotification);
    synthTitle.setText("MOOG  ·  CH2", juce::dontSendNotification);
    polyTitle.setText("PROPHET  ·  CH3", juce::dontSendNotification);
    keysTitle.setText("KEYS  ·  CH4", juce::dontSendNotification);
    busTitle.setText("BUS  ·  MASTER", juce::dontSendNotification);
    drumVolL.setText("VOL", juce::dontSendNotification);
    drumLL.setText("L", juce::dontSendNotification);
    drumRL.setText("R", juce::dontSendNotification);
    synthVolL.setText("VOL", juce::dontSendNotification);
    synthLL.setText("L", juce::dontSendNotification);
    synthRL.setText("R", juce::dontSendNotification);
    polyVolL.setText("VOL", juce::dontSendNotification);
    polyLL.setText("L", juce::dontSendNotification);
    polyRL.setText("R", juce::dontSendNotification);
    keysVolL.setText("VOL", juce::dontSendNotification);
    keysLL.setText("L", juce::dontSendNotification);
    keysRL.setText("R", juce::dontSendNotification);
    masterL.setText("MASTER", juce::dontSendNotification);
    compL.setText("CMP", juce::dontSendNotification);
    eqTitle.setText("EQ", juce::dontSendNotification);

    for (auto* l : { &drumsTitle, &synthTitle, &polyTitle, &keysTitle, &busTitle })
    {
        l->setFont(juce::FontOptions(10.0f, juce::Font::bold));
        l->setColour(juce::Label::textColourId, juce::Colour(0xffd8e8f1));
        l->setJustificationType(juce::Justification::centred);
        l->setMouseCursor(juce::MouseCursor::PointingHandCursor);
        addAndMakeVisible(*l);
    }
    drumsTitle.addMouseListener(this, false);
    synthTitle.addMouseListener(this, false);
    polyTitle.addMouseListener(this, false);
    keysTitle.addMouseListener(this, false);
    refreshChannelHighlight();
    for (auto* l : { &drumVolL, &drumLL, &drumRL, &synthVolL, &synthLL, &synthRL,
                     &polyVolL, &polyLL, &polyRL, &keysVolL, &keysLL, &keysRL,
                     &masterL, &compL, &eqTitle })
    {
        l->setFont(juce::FontOptions(10.0f, juce::Font::bold));
        l->setColour(juce::Label::textColourId, juce::Colour(0xffe6f0f5));
        l->setJustificationType(juce::Justification::centred);
        l->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*l);
    }

    configure(drumVol, 0.0, 1.5, 0.01);
    configure(drumLeft, 0.0, 1.5, 0.01);
    configure(drumRight, 0.0, 1.5, 0.01);
    configure(synthVol, 0.0, 1.5, 0.01);
    configure(synthLeft, 0.0, 1.5, 0.01);
    configure(synthRight, 0.0, 1.5, 0.01);
    configure(polyVol, 0.0, 1.5, 0.01);
    configure(polyLeft, 0.0, 1.5, 0.01);
    configure(polyRight, 0.0, 1.5, 0.01);
    configure(keysVol, 0.0, 1.5, 0.01);
    configure(keysLeft, 0.0, 1.5, 0.01);
    configure(keysRight, 0.0, 1.5, 0.01);
    configure(masterVol, 0.0, 1.5, 0.01);
    masterVol.setSliderStyle(juce::Slider::LinearVertical);
    masterVol.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 16);
    masterVol.setColour(juce::Slider::trackColourId, juce::Colour(0xff2a4a5c));
    masterVol.setColour(juce::Slider::thumbColourId, juce::Colour(0xff7ac8ff));
    masterVol.textFromValueFunction = [](double v)
    {
        if (v <= 0.001)
            return juce::String("-inf");
        return juce::String(juce::Decibels::gainToDecibels((float) v), 1) + " dB";
    };
    masterVol.valueFromTextFunction = [](const juce::String& t)
    {
        auto s = t.upToFirstOccurrenceOf("dB", false, true).trim();
        if (s.containsIgnoreCase("inf"))
            return 0.0;
        return (double) juce::Decibels::decibelsToGain(s.getFloatValue());
    };
    configure(busComp, 0.0, 1.0, 0.01);

    drumVol.setValue(1.0, juce::dontSendNotification);
    drumLeft.setValue(1.0, juce::dontSendNotification);
    drumRight.setValue(1.0, juce::dontSendNotification);
    synthVol.setValue(1.0, juce::dontSendNotification);
    synthLeft.setValue(1.0, juce::dontSendNotification);
    synthRight.setValue(1.0, juce::dontSendNotification);
    polyVol.setValue(1.0, juce::dontSendNotification);
    polyLeft.setValue(1.0, juce::dontSendNotification);
    polyRight.setValue(1.0, juce::dontSendNotification);
    keysVol.setValue(1.0, juce::dontSendNotification);
    keysLeft.setValue(1.0, juce::dontSendNotification);
    keysRight.setValue(1.0, juce::dontSendNotification);
    masterVol.setValue(1.0, juce::dontSendNotification);
    busComp.setValue(0.22, juce::dontSendNotification);

    for (int i = 0; i < groove::kEqBands; ++i)
    {
        configureEq(eqSliders[(size_t) i]);
        eqLabels[(size_t) i].setText(groove::kEqLabels[i], juce::dontSendNotification);
        eqLabels[(size_t) i].setFont(juce::FontOptions(9.0f, juce::Font::bold));
        eqLabels[(size_t) i].setColour(juce::Label::textColourId, juce::Colour(0xff8aa0ae));
        eqLabels[(size_t) i].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(eqLabels[(size_t) i]);
        bind(eqSliders[(size_t) i]);
    }

    for (auto* s : { &drumVol, &drumLeft, &drumRight, &synthVol, &synthLeft, &synthRight,
                     &polyVol, &polyLeft, &polyRight, &keysVol, &keysLeft, &keysRight,
                     &masterVol, &busComp })
        bind(*s);

    stylePicker(drumSound, drumPrev, drumNext);
    stylePicker(synthSound, synthPrev, synthNext);
    stylePicker(keysSound, keysPrev, keysNext);
    stylePicker(polySound, polyPrev, polyNext);
    addAndMakeVisible(drumUi);
    addAndMakeVisible(synthUi);
    addAndMakeVisible(keysUi);
    addAndMakeVisible(polyUi);
    addAndMakeVisible(polyBrowse);
    for (int c = 0; c < groove::kMixChannels; ++c)
        setupInserts(c);
    refreshInserts();
}

void MixStrip::setupInserts(int channel)
{
    auto& ins = inserts[(size_t) channel];
    auto chip = [](juce::TextButton& b)
    {
        b.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd8e8f1));
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff152430));
    };
    chip(ins.lpBtn);
    chip(ins.hpBtn);
    chip(ins.delayBtn);
    chip(ins.lockBtn);
    chip(ins.delayNote);
    configure(ins.lp, 0.0, 1.0, 0.01);
    configure(ins.hp, 0.0, 1.0, 0.01);
    configure(ins.delayWet, 0.0, 1.0, 0.01);
    configure(ins.delayFeedback, 0.0, 0.85, 0.01);
    ins.lp.setValue(1.0, juce::dontSendNotification);
    ins.hp.setValue(0.0, juce::dontSendNotification);
    ins.delayWet.setValue(0.22, juce::dontSendNotification);
    ins.delayFeedback.setValue(0.32, juce::dontSendNotification);
    ins.lpL.setText("LP", juce::dontSendNotification);
    ins.hpL.setText("HP", juce::dontSendNotification);
    ins.delayL.setText("WET", juce::dontSendNotification);
    ins.delayFeedbackL.setText("FDBK", juce::dontSendNotification);
    for (auto* l : { &ins.lpL, &ins.hpL, &ins.delayL, &ins.delayFeedbackL })
    {
        l->setFont(juce::FontOptions(9.0f, juce::Font::bold));
        l->setColour(juce::Label::textColourId, juce::Colour(0xff8aa0ae));
        l->setJustificationType(juce::Justification::centred);
        l->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*l);
    }
    bindFx(ins.lp, channel, 0);
    bindFx(ins.hp, channel, 1);
    bindFx(ins.delayWet, channel, 2);
    bindFx(ins.delayFeedback, channel, 3);
    addAndMakeVisible(ins.lpBtn);
    addAndMakeVisible(ins.hpBtn);
    addAndMakeVisible(ins.delayBtn);
    addAndMakeVisible(ins.lockBtn);
    addAndMakeVisible(ins.delayNote);
    ins.lpBtn.onClick = [this, channel] { toggleInsert(channel, 0); };
    ins.hpBtn.onClick = [this, channel] { toggleInsert(channel, 1); };
    ins.delayBtn.onClick = [this, channel] { toggleInsert(channel, 2); };
    ins.lockBtn.onClick = [this, channel] { toggleInsert(channel, 3); };
    ins.delayNote.onClick = [this, channel] { cycleDelayNote(channel); };

    ins.fxSelect.addItem("DRIVE / SAT", 1);
    ins.fxSelect.addItem("RING MOD", 2);
    ins.fxSelect.addItem("CAPITOL CHAMBERS", 3);
    ins.fxSelect.addItem("PARADISE GUITAR STUDIO", 4);
    ins.fxSelect.setSelectedId(1, juce::dontSendNotification);
    ins.fxSelect.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff12202a));
    ins.fxSelect.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff2a4452));
    addAndMakeVisible(ins.fxSelect);
    chip(ins.fxEnable);
    addAndMakeVisible(ins.fxEnable);
    configure(ins.fxA, 0.0, 1.0, 0.01);
    configure(ins.fxB, 0.0, 1.0, 0.01);
    configure(ins.fxC, 0.0, 1.0, 0.01);
    configure(ins.fxD, 0.0, 1.0, 0.01);
    configure(ins.fxE, 0.0, 1.0, 0.01);
    configure(ins.fxF, 0.0, 1.0, 0.01);
    configure(ins.fxG, 0.0, 1.0, 0.01);
    configure(ins.fxH, 0.0, 1.0, 0.01);
    for (auto* l : { &ins.fxAL, &ins.fxBL, &ins.fxCL, &ins.fxDL, &ins.fxEL, &ins.fxFL, &ins.fxGL, &ins.fxHL })
    {
        l->setFont(juce::FontOptions(9.0f, juce::Font::bold));
        l->setColour(juce::Label::textColourId, juce::Colour(0xff8aa0ae));
        l->setJustificationType(juce::Justification::centred);
        l->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*l);
    }
    bindCreativeFx(ins.fxA, channel, 0);
    bindCreativeFx(ins.fxB, channel, 1);
    bindCreativeFx(ins.fxC, channel, 2);
    bindCreativeFx(ins.fxD, channel, 3);
    bindCreativeFx(ins.fxE, channel, 4);
    bindCreativeFx(ins.fxF, channel, 5);
    bindCreativeFx(ins.fxG, channel, 6);
    bindCreativeFx(ins.fxH, channel, 7);
    addAndMakeVisible(ins.fxD);
    addAndMakeVisible(ins.fxE);
    addAndMakeVisible(ins.fxF);
    addAndMakeVisible(ins.fxG);
    addAndMakeVisible(ins.fxH);
    ins.fxSelect.onChange = [this, channel]
    {
        if (refreshing) return;
        inserts[(size_t)channel].selectedFx = juce::jlimit(0, 3, inserts[(size_t)channel].fxSelect.getSelectedId() - 1);
        refreshCreativeEditor(channel);
        resized();
    };
    ins.fxEnable.onClick = [this, channel]
    {
        auto& x = inserts[(size_t)channel];
        if (x.selectedFx == 0) x.driveOn = !x.driveOn;
        else if (x.selectedFx == 1) x.ringOn = !x.ringOn;
        else if (x.selectedFx == 2) x.reverbOn = !x.reverbOn;
        else x.paradiseOn = !x.paradiseOn;
        refreshCreativeEditor(channel);
        notify();
    };
    refreshCreativeEditor(channel);
}

void MixStrip::styleChip(juce::TextButton& b, bool on, juce::Colour onColour)
{
    b.setColour(juce::TextButton::buttonColourId, on ? onColour : juce::Colour(0xff152430));
    b.setColour(juce::TextButton::textColourOffId, on ? juce::Colours::white : juce::Colour(0xff8aa0ae));
}

void MixStrip::toggleInsert(int channel, int kind)
{
    auto& ins = inserts[(size_t) channel];
    if (kind == 0)
    {
        ins.lpOn = ! ins.lpOn;
        ins.lpBtn.setButtonText(ins.lpOn ? "LP ×" : "+ LP");
    }
    else if (kind == 1)
    {
        ins.hpOn = ! ins.hpOn;
        ins.hpBtn.setButtonText(ins.hpOn ? "HP ×" : "+ HP");
    }
    else if (kind == 2)
    {
        ins.delayOn = ! ins.delayOn;
        ins.delayBtn.setButtonText(ins.delayOn ? "DLY ×" : "+ DLY");
    }
    else if (kind == 3)
    {
        auto& lock = fxLocks[(size_t) channel][(size_t) selectedStep];
        if (! lock.empty())
            lock = {};
        else
        {
            if (ins.lpOn) lock.lp = (float) ins.lp.getValue();
            if (ins.hpOn) lock.hp = (float) ins.hp.getValue();
            if (ins.delayOn)
            {
                lock.delayWet = (float) ins.delayWet.getValue();
                lock.delayFeedback = (float) ins.delayFeedback.getValue();
                lock.delayNote = ins.delayNoteIndex;
            }
            if (ins.reverbOn) { lock.reverbSize = ins.baseReverbSize; lock.reverbDecay = ins.baseReverbDecay; lock.reverbWet = ins.baseReverbWet; }
            if (ins.driveOn) { lock.driveAmount = ins.baseDriveAmount; lock.driveTone = ins.baseDriveTone; lock.driveMix = ins.baseDriveMix; }
            if (ins.ringOn)  { lock.ringFreq = ins.baseRingFreq; lock.ringDepth = ins.baseRingDepth; lock.ringMix = ins.baseRingMix; }
            if (ins.combOn)  { lock.combFreq = ins.baseCombFreq; lock.combFeedback = ins.baseCombFeedback; lock.combMix = ins.baseCombMix; }
        }
    }
    showFxForSelectedStep(channel);
    refreshInserts();
    resized();
    notify();
}

void MixStrip::cycleDelayNote(int channel)
{
    auto& ins = inserts[(size_t) channel];
    const int next = (ins.delayNoteIndex + 1) % groove::kDelayNoteCount;
    auto& lock = fxLocks[(size_t) channel][(size_t) selectedStep];
    if (! lock.empty())
        lock.delayNote = next;
    else
        ins.baseDelayNote = next;
    ins.delayNoteIndex = next;
    ins.delayNote.setButtonText(groove::kDelayNoteNames[next]);
    notify();
}

void MixStrip::refreshInserts()
{
    for (int c = 0; c < groove::kMixChannels; ++c)
    {
        auto& ins = inserts[(size_t) c];
        const auto& lock = fxLocks[(size_t) c][(size_t) selectedStep];
        styleChip(ins.lpBtn, ins.lpOn, juce::Colour(0xff2e6a8a));
        styleChip(ins.hpBtn, ins.hpOn, juce::Colour(0xff2e6a8a));
        styleChip(ins.delayBtn, ins.delayOn, juce::Colour(0xff2e6a8a));
        styleChip(ins.lockBtn, ! lock.empty(), juce::Colour(0xff8a6a22));
        ins.lpBtn.setButtonText(ins.lpOn ? "LP ×" : "+ LP");
        ins.hpBtn.setButtonText(ins.hpOn ? "HP ×" : "+ HP");
        ins.delayBtn.setButtonText(ins.delayOn ? "DLY ×" : "+ DLY");
        ins.lockBtn.setButtonText(lock.empty() ? "+ LOCK" : "LOCK ×");
        ins.delayNote.setButtonText(groove::kDelayNoteNames[juce::jlimit(0, groove::kDelayNoteCount - 1, ins.delayNoteIndex)]);
        ins.lp.setVisible(ins.lpOn);
        ins.lpL.setVisible(ins.lpOn);
        ins.hp.setVisible(ins.hpOn);
        ins.hpL.setVisible(ins.hpOn);
        ins.delayWet.setVisible(ins.delayOn);
        ins.delayFeedback.setVisible(ins.delayOn);
        ins.delayL.setVisible(ins.delayOn);
        ins.delayFeedbackL.setVisible(ins.delayOn);
        ins.delayNote.setVisible(ins.delayOn);
        refreshCreativeEditor(c);
    }
}

void MixStrip::notify()
{
    if (refreshing)
        return;
    if (onChanged)
        onChanged();
}

void MixStrip::configure(juce::Slider& s, double min, double max, double step)
{
    s.setRange(min, max, step);
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 44, 14);
    s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff3d8ec4));
    s.setColour(juce::Slider::thumbColourId, juce::Colour(0xff7ac8ff));
    addAndMakeVisible(s);
}

void MixStrip::configureEq(juce::Slider& s)
{
    s.setRange(-12.0, 12.0, 0.1);
    s.setSliderStyle(juce::Slider::LinearVertical);
    s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    s.setColour(juce::Slider::trackColourId, juce::Colour(0xff2a4a5c));
    s.setColour(juce::Slider::thumbColourId, juce::Colour(0xff7ac8ff));
    s.setValue(0.0, juce::dontSendNotification);
    addAndMakeVisible(s);
}

void MixStrip::bind(juce::Slider& s)
{
    s.onValueChange = [this]
    {
        notify();
    };
}

void MixStrip::bindFx(juce::Slider& s, int channel, int kind)
{
    s.onValueChange = [this, channel, kind, &s]
    {
        if (refreshing)
            return;

        auto& ins = inserts[(size_t) channel];
        auto& lock = fxLocks[(size_t) channel][(size_t) selectedStep];
        const float value = (float) s.getValue();

        // A locked step is the edit target. Moving an FX knob records that
        // movement into the selected step instead of overwriting the channel base.
        if (! lock.empty())
        {
            if (kind == 0) lock.lp = value;
            else if (kind == 1) lock.hp = value;
            else if (kind == 2) lock.delayWet = value;
            else if (kind == 3) lock.delayFeedback = value;
        }
        else
        {
            if (kind == 0) ins.baseLp = value;
            else if (kind == 1) ins.baseHp = value;
            else if (kind == 2) ins.baseDelayWet = value;
            else if (kind == 3) ins.baseDelayFeedback = value;
        }
        notify();
        refreshInserts();
    };
}

void MixStrip::bindCreativeFx(juce::Slider& slider, int channel, int paramIndex)
{
    slider.onValueChange = [this, channel, paramIndex, &slider]
    {
        if (refreshing) return;
        auto& ins = inserts[(size_t)channel];
        auto& lock = fxLocks[(size_t)channel][(size_t)selectedStep];
        const float v = (float)slider.getValue();
        const bool stepTarget = !lock.empty();
        if (ins.selectedFx == 0)
        {
            if (stepTarget) { if (paramIndex==0) lock.driveAmount=v; else if(paramIndex==1) lock.driveTone=v; else lock.driveMix=v; }
            else { if(paramIndex==0) ins.baseDriveAmount=v; else if(paramIndex==1) ins.baseDriveTone=v; else ins.baseDriveMix=v; }
        }
        else if (ins.selectedFx == 1)
        {
            if (stepTarget) { if(paramIndex==0) lock.ringFreq=v; else if(paramIndex==1) lock.ringDepth=v; else lock.ringMix=v; }
            else { if(paramIndex==0) ins.baseRingFreq=v; else if(paramIndex==1) ins.baseRingDepth=v; else ins.baseRingMix=v; }
        }
        else if (ins.selectedFx == 2)
        {
            // Capitol Chambers: SIZE/DECAY/WET can step-lock; the rest are channel macros.
            if (paramIndex == 0) { if (stepTarget) lock.reverbSize = v; else ins.baseReverbSize = v; }
            else if (paramIndex == 1) { if (stepTarget) lock.reverbDecay = v; else ins.baseReverbDecay = v; }
            else if (paramIndex == 2) { if (stepTarget) lock.reverbWet = v; else ins.baseReverbWet = v; }
            else if (paramIndex == 3) ins.baseReverbPreDelay = v;
            else if (paramIndex == 4) ins.baseReverbWidth = v;
            else if (paramIndex == 5) ins.baseReverbBass = v;
            else if (paramIndex == 6) ins.baseReverbTreble = v;
            else if (paramIndex == 7) ins.baseReverbVolume = v;
        }
        else
        {
            // Paradise Guitar Studio macros including volume (OUT).
            if (paramIndex == 0) ins.baseParadiseInput = v;
            else if (paramIndex == 1) ins.baseParadiseGate = v;
            else if (paramIndex == 2) ins.baseParadisePre = v;
            else if (paramIndex == 3) ins.baseParadiseAmp = v;
            else if (paramIndex == 4) ins.baseParadiseCab = v;
            else if (paramIndex == 5) ins.baseParadiseRoom = v;
            else if (paramIndex == 6) ins.baseParadiseOutput = v;
            else if (paramIndex == 7) ins.baseParadiseLimit = v;
        }
        notify();
    };
}

void MixStrip::refreshCreativeEditor(int channel)
{
    auto& ins = inserts[(size_t)channel];
    const auto& lock = fxLocks[(size_t)channel][(size_t)selectedStep];
    const juce::ScopedValueSetter<bool> sv(refreshing, true);
    bool enabled = false;
    auto hideExtra = [&]
    {
        juce::Slider* sliders[] = { &ins.fxD, &ins.fxE, &ins.fxF, &ins.fxG, &ins.fxH };
        juce::Label* labels[] = { &ins.fxDL, &ins.fxEL, &ins.fxFL, &ins.fxGL, &ins.fxHL };
        for (auto* s : sliders) s->setVisible(false);
        for (auto* l : labels) l->setVisible(false);
    };
    auto showExtra = [&](int count)
    {
        juce::Slider* sliders[] = { &ins.fxD, &ins.fxE, &ins.fxF, &ins.fxG, &ins.fxH };
        juce::Label* labels[] = { &ins.fxDL, &ins.fxEL, &ins.fxFL, &ins.fxGL, &ins.fxHL };
        for (int i = 0; i < 5; ++i)
        {
            const bool on = i < count;
            sliders[i]->setVisible(on);
            labels[i]->setVisible(on);
        }
    };
    hideExtra();
    if (ins.selectedFx == 0)
    {
        enabled = ins.driveOn;
        ins.fxAL.setText("DRIVE", juce::dontSendNotification); ins.fxBL.setText("TONE", juce::dontSendNotification); ins.fxCL.setText("MIX", juce::dontSendNotification);
        ins.fxA.setValue(lock.driveAmount.value_or(ins.baseDriveAmount), juce::dontSendNotification);
        ins.fxB.setValue(lock.driveTone.value_or(ins.baseDriveTone), juce::dontSendNotification);
        ins.fxC.setValue(lock.driveMix.value_or(ins.baseDriveMix), juce::dontSendNotification);
    }
    else if (ins.selectedFx == 1)
    {
        enabled = ins.ringOn;
        ins.fxAL.setText("FREQ", juce::dontSendNotification); ins.fxBL.setText("DEPTH", juce::dontSendNotification); ins.fxCL.setText("MIX", juce::dontSendNotification);
        ins.fxA.setValue(lock.ringFreq.value_or(ins.baseRingFreq), juce::dontSendNotification);
        ins.fxB.setValue(lock.ringDepth.value_or(ins.baseRingDepth), juce::dontSendNotification);
        ins.fxC.setValue(lock.ringMix.value_or(ins.baseRingMix), juce::dontSendNotification);
    }
    else if (ins.selectedFx == 2)
    {
        enabled = ins.reverbOn;
        ins.fxAL.setText("SIZE", juce::dontSendNotification);
        ins.fxBL.setText("DECAY", juce::dontSendNotification);
        ins.fxCL.setText("WET", juce::dontSendNotification);
        ins.fxDL.setText("PRE", juce::dontSendNotification);
        ins.fxEL.setText("WIDTH", juce::dontSendNotification);
        ins.fxFL.setText("BASS", juce::dontSendNotification);
        ins.fxGL.setText("TREBLE", juce::dontSendNotification);
        ins.fxHL.setText("VOL", juce::dontSendNotification);
        ins.fxA.setValue(lock.reverbSize.value_or(ins.baseReverbSize), juce::dontSendNotification);
        ins.fxB.setValue(lock.reverbDecay.value_or(ins.baseReverbDecay), juce::dontSendNotification);
        ins.fxC.setValue(lock.reverbWet.value_or(ins.baseReverbWet), juce::dontSendNotification);
        ins.fxD.setValue(ins.baseReverbPreDelay, juce::dontSendNotification);
        ins.fxE.setValue(ins.baseReverbWidth, juce::dontSendNotification);
        ins.fxF.setValue(ins.baseReverbBass, juce::dontSendNotification);
        ins.fxG.setValue(ins.baseReverbTreble, juce::dontSendNotification);
        ins.fxH.setValue(ins.baseReverbVolume, juce::dontSendNotification);
        showExtra(5);
    }
    else
    {
        enabled = ins.paradiseOn;
        ins.fxAL.setText("IN", juce::dontSendNotification);
        ins.fxBL.setText("GATE", juce::dontSendNotification);
        ins.fxCL.setText("PRE", juce::dontSendNotification);
        ins.fxDL.setText("AMP", juce::dontSendNotification);
        ins.fxEL.setText("CAB", juce::dontSendNotification);
        ins.fxFL.setText("WET", juce::dontSendNotification);
        ins.fxGL.setText("VOL", juce::dontSendNotification);
        ins.fxHL.setText("LIMIT", juce::dontSendNotification);
        ins.fxA.setValue(ins.baseParadiseInput, juce::dontSendNotification);
        ins.fxB.setValue(ins.baseParadiseGate, juce::dontSendNotification);
        ins.fxC.setValue(ins.baseParadisePre, juce::dontSendNotification);
        ins.fxD.setValue(ins.baseParadiseAmp, juce::dontSendNotification);
        ins.fxE.setValue(ins.baseParadiseCab, juce::dontSendNotification);
        ins.fxF.setValue(ins.baseParadiseRoom, juce::dontSendNotification);
        ins.fxG.setValue(ins.baseParadiseOutput, juce::dontSendNotification);
        ins.fxH.setValue(ins.baseParadiseLimit, juce::dontSendNotification);
        showExtra(5);
    }
    ins.fxEnable.setButtonText(enabled ? "FX ON" : "FX OFF");
    styleChip(ins.fxEnable, enabled, juce::Colour(0xff6b3f8a));
}

void MixStrip::showFxForSelectedStep(int channel)
{
    auto& ins = inserts[(size_t) channel];
    const auto& lock = fxLocks[(size_t) channel][(size_t) selectedStep];
    const juce::ScopedValueSetter<bool> sv(refreshing, true);
    ins.lp.setValue(lock.lp.value_or(ins.baseLp), juce::dontSendNotification);
    ins.hp.setValue(lock.hp.value_or(ins.baseHp), juce::dontSendNotification);
    ins.delayWet.setValue(lock.delayWet.value_or(ins.baseDelayWet), juce::dontSendNotification);
    ins.delayFeedback.setValue(lock.delayFeedback.value_or(ins.baseDelayFeedback), juce::dontSendNotification);
    ins.delayNoteIndex = juce::jlimit(0, groove::kDelayNoteCount - 1,
                                     lock.delayNote.value_or(ins.baseDelayNote));
    ins.delayNote.setButtonText(groove::kDelayNoteNames[ins.delayNoteIndex]);
    refreshCreativeEditor(channel);
}

void MixStrip::stylePicker(juce::ComboBox& box, juce::TextButton& prev, juce::TextButton& next)
{
    box.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff12202a));
    box.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff2a4452));
    addAndMakeVisible(box);
    addAndMakeVisible(prev);
    addAndMakeVisible(next);
}

void MixStrip::stylePicker(juce::TextButton& box, juce::TextButton& prev, juce::TextButton& next)
{
    box.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff12202a));
    addAndMakeVisible(box);
    addAndMakeVisible(prev);
    addAndMakeVisible(next);
}

void MixStrip::loadFrom(const groove::MixSettings& m)
{
    const juce::ScopedValueSetter<bool> sv(refreshing, true);
    drumVol.setValue(m.drumVol, juce::dontSendNotification);
    drumLeft.setValue(m.drumLeft, juce::dontSendNotification);
    drumRight.setValue(m.drumRight, juce::dontSendNotification);
    synthVol.setValue(m.synthVol, juce::dontSendNotification);
    synthLeft.setValue(m.synthLeft, juce::dontSendNotification);
    synthRight.setValue(m.synthRight, juce::dontSendNotification);
    polyVol.setValue(m.polyVol, juce::dontSendNotification);
    polyLeft.setValue(m.polyLeft, juce::dontSendNotification);
    polyRight.setValue(m.polyRight, juce::dontSendNotification);
    keysVol.setValue(m.keysVol, juce::dontSendNotification);
    keysLeft.setValue(m.keysLeft, juce::dontSendNotification);
    keysRight.setValue(m.keysRight, juce::dontSendNotification);
    masterVol.setValue(m.masterVol, juce::dontSendNotification);
    busComp.setValue(m.busComp, juce::dontSendNotification);
    fxLocks = m.fxLocks;
    for (int c = 0; c < groove::kMixChannels; ++c)
    {
        const auto& fx = m.channelFx[(size_t) c];
        auto& ins = inserts[(size_t) c];
        ins.lpOn = fx.lpOn;
        ins.hpOn = fx.hpOn;
        ins.delayOn = fx.delayOn;
        ins.baseLp = fx.lp;
        ins.baseHp = fx.hp;
        ins.baseDelayWet = fx.delayWet;
        ins.baseDelayFeedback = fx.delayFb;
        ins.baseDelayNote = juce::jlimit(0, groove::kDelayNoteCount - 1, fx.delayNote);
        ins.driveOn = fx.driveOn; ins.baseDriveAmount = fx.driveAmount; ins.baseDriveTone = fx.driveTone; ins.baseDriveMix = fx.driveMix;
        ins.ringOn = fx.ringOn; ins.baseRingFreq = fx.ringFreq; ins.baseRingDepth = fx.ringDepth; ins.baseRingMix = fx.ringMix;
        ins.combOn = false; // legacy projects may contain comb data; Comb Filter UI has been removed.
        ins.reverbOn = fx.reverbOn; ins.baseReverbSize = fx.reverbSize; ins.baseReverbDecay = fx.reverbDecay; ins.baseReverbWet = fx.reverbWet;
        ins.baseReverbPreDelay = fx.reverbPreDelay; ins.baseReverbWidth = fx.reverbWidth;
        ins.baseReverbBass = fx.reverbBass; ins.baseReverbMid = fx.reverbMid;
        ins.baseReverbTreble = fx.reverbTreble; ins.baseReverbVolume = fx.reverbVolume;
        ins.paradiseOn = fx.paradiseOn;
        ins.baseParadiseInput = fx.paradiseInput; ins.baseParadiseGate = fx.paradiseGate;
        ins.baseParadisePre = fx.paradisePre; ins.baseParadiseAmp = fx.paradiseAmp;
        ins.baseParadiseCab = fx.paradiseCab; ins.baseParadiseRoom = fx.paradiseRoom;
        ins.baseParadiseOutput = fx.paradiseOutput; ins.baseParadiseLimit = fx.paradiseLimit;
        ins.lp.setValue(ins.baseLp, juce::dontSendNotification);
        ins.hp.setValue(ins.baseHp, juce::dontSendNotification);
        ins.delayWet.setValue(ins.baseDelayWet, juce::dontSendNotification);
        ins.delayFeedback.setValue(ins.baseDelayFeedback, juce::dontSendNotification);
        ins.delayNoteIndex = ins.baseDelayNote;
    }
    for (int i = 0; i < groove::kEqBands; ++i)
        eqSliders[(size_t) i].setValue(m.eqGainDb[(size_t) i], juce::dontSendNotification);
    for (int c = 0; c < groove::kMixChannels; ++c)
        showFxForSelectedStep(c);
    refreshInserts();
    resized();
}

void MixStrip::saveTo(groove::MixSettings& m) const
{
    m.drumVol = (float) drumVol.getValue();
    m.drumLeft = (float) drumLeft.getValue();
    m.drumRight = (float) drumRight.getValue();
    m.synthVol = (float) synthVol.getValue();
    m.synthLeft = (float) synthLeft.getValue();
    m.synthRight = (float) synthRight.getValue();
    m.polyVol = (float) polyVol.getValue();
    m.polyLeft = (float) polyLeft.getValue();
    m.polyRight = (float) polyRight.getValue();
    m.keysVol = (float) keysVol.getValue();
    m.keysLeft = (float) keysLeft.getValue();
    m.keysRight = (float) keysRight.getValue();
    m.masterVol = (float) masterVol.getValue();
    m.busComp = (float) busComp.getValue();
    m.fxLocks = fxLocks;
    for (int c = 0; c < groove::kMixChannels; ++c)
    {
        const auto& ins = inserts[(size_t) c];
        auto& fx = m.channelFx[(size_t) c];
        fx.lpOn = ins.lpOn;
        fx.lp = ins.baseLp;
        fx.hpOn = ins.hpOn;
        fx.hp = ins.baseHp;
        fx.delayOn = ins.delayOn;
        fx.delayWet = ins.baseDelayWet;
        fx.delayFb = ins.baseDelayFeedback;
        fx.delayNote = ins.baseDelayNote;
        fx.reverbOn = ins.reverbOn; fx.reverbSize = ins.baseReverbSize; fx.reverbDecay = ins.baseReverbDecay; fx.reverbWet = ins.baseReverbWet;
        fx.reverbPreDelay = ins.baseReverbPreDelay; fx.reverbWidth = ins.baseReverbWidth;
        fx.reverbBass = ins.baseReverbBass; fx.reverbMid = ins.baseReverbMid;
        fx.reverbTreble = ins.baseReverbTreble; fx.reverbVolume = ins.baseReverbVolume;
        fx.driveOn = ins.driveOn; fx.driveAmount = ins.baseDriveAmount; fx.driveTone = ins.baseDriveTone; fx.driveMix = ins.baseDriveMix;
        fx.ringOn = ins.ringOn; fx.ringFreq = ins.baseRingFreq; fx.ringDepth = ins.baseRingDepth; fx.ringMix = ins.baseRingMix;
        fx.combOn = false; // Comb Filter removed from the product UI.
        fx.paradiseOn = ins.paradiseOn;
        fx.paradiseInput = ins.baseParadiseInput; fx.paradiseGate = ins.baseParadiseGate;
        fx.paradisePre = ins.baseParadisePre; fx.paradiseAmp = ins.baseParadiseAmp;
        fx.paradiseCab = ins.baseParadiseCab; fx.paradiseRoom = ins.baseParadiseRoom;
        fx.paradiseOutput = ins.baseParadiseOutput; fx.paradiseLimit = ins.baseParadiseLimit;
    }
    for (int i = 0; i < groove::kEqBands; ++i)
        m.eqGainDb[(size_t) i] = (float) eqSliders[(size_t) i].getValue();
}

void MixStrip::setActiveMidiChannel(int channel)
{
    activeMidiChannel = (channel >= 1 && channel <= 4) ? channel : 0;
    refreshChannelHighlight();
    repaint();
}

void MixStrip::setSelectedStep(int step)
{
    selectedStep = juce::jlimit(0, groove::kSteps - 1, step);
    for (int c = 0; c < groove::kMixChannels; ++c)
        showFxForSelectedStep(c);
    refreshInserts();
}

void MixStrip::setBpm(double bpm)
{
    currentBpm = bpm;
}

void MixStrip::refreshChannelHighlight()
{
    const auto dim = juce::Colour(0xff8aa0ae);
    const auto hot = juce::Colour(0xffe8f6ff);
    auto style = [&](juce::Label& label, int channel, const juce::String& base)
    {
        const bool on = activeMidiChannel == channel;
        label.setText(on ? ("▶  " + base) : base, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, on ? hot : dim);
    };
    style(drumsTitle, 1, "DRUMS  ·  CH1");
    style(synthTitle, 2, "MOOG  ·  CH2");
    style(polyTitle, 3, "PROPHET  ·  CH3");
    style(keysTitle, 4, "KEYS  ·  CH4");
    busTitle.setText("BUS  ·  MASTER", juce::dontSendNotification);
}

void MixStrip::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().reduced(4, 2);
    const int colW = r.getWidth() / 5;
    for (int i = 0; i < 5; ++i)
    {
        auto col = r.removeFromLeft(colW).reduced(2, 0).toFloat();
        const int ch = (i < 4) ? (i + 1) : 0;
        const bool on = ch > 0 && ch == activeMidiChannel;
        g.setColour(on ? juce::Colour(0xff1a4a62) : juce::Colour(0xff0d1c26));
        g.fillRoundedRectangle(col, 6.0f);
        if (on)
        {
            g.setColour(juce::Colour(0xff7ac8ff));
            g.drawRoundedRectangle(col.reduced(0.5f), 6.0f, 2.0f);
            g.fillRoundedRectangle({ col.getX() + 10.0f, col.getY() + 3.0f,
                                    col.getWidth() - 20.0f, 3.0f }, 1.5f);
        }
    }
}

void MixStrip::mouseDown(const juce::MouseEvent& e)
{
    if (onChannelClicked == nullptr)
        return;
    auto* src = e.eventComponent;
    if (src == &drumsTitle) { onChannelClicked(1); return; }
    if (src == &synthTitle) { onChannelClicked(2); return; }
    if (src == &polyTitle)  { onChannelClicked(3); return; }
    if (src == &keysTitle)  { onChannelClicked(4); return; }
    if (dynamic_cast<juce::Slider*>(src) != nullptr
        || dynamic_cast<juce::ComboBox*>(src) != nullptr
        || dynamic_cast<juce::Button*>(src) != nullptr)
        return;

    const auto p = e.getEventRelativeTo(this).getPosition();
    auto r = getLocalBounds().reduced(4, 2);
    const int colW = juce::jmax(1, r.getWidth() / 5);
    const int col = (p.x - r.getX()) / colW;
    if (col >= 0 && col < 4)
        onChannelClicked(col + 1);
}

void MixStrip::resized()
{
    auto r = getLocalBounds().reduced(4, 2);
    const int colW = r.getWidth() / 5;
    auto placeCol = [&](int channel, juce::Label& title, juce::Label& aL, juce::Slider& a,
                        juce::Label& bL, juce::Slider& b,
                        juce::Label& cL, juce::Slider& c,
                        juce::Component* box, juce::TextButton* prev, juce::TextButton* next,
                        juce::TextButton* ui)
    {
        auto col = r.removeFromLeft(colW).reduced(4, 2);
        auto titleRow = col.removeFromTop(16);
        if (ui != nullptr)
            ui->setBounds(titleRow.removeFromRight(52).reduced(1));
        if (&title == &polyTitle)
            polyBrowse.setBounds(titleRow.removeFromRight(40).reduced(1));
        title.setBounds(titleRow);
        if (box != nullptr && prev != nullptr && next != nullptr)
        {
            auto picker = col.removeFromBottom(26);
            prev->setBounds(picker.removeFromLeft(24).reduced(1));
            next->setBounds(picker.removeFromRight(24).reduced(1));
            picker.removeFromLeft(2);
            picker.removeFromRight(2);
            box->setBounds(picker.reduced(0, 1));
            col.removeFromBottom(4);
        }

        auto& ins = inserts[(size_t) channel];
        auto insertArea = col.removeFromBottom(248);
        auto chips = insertArea.removeFromTop(22);
        const int chipW = juce::jmax(28, chips.getWidth() / 4);
        ins.lpBtn.setBounds(chips.removeFromLeft(chipW).reduced(1));
        ins.hpBtn.setBounds(chips.removeFromLeft(chipW).reduced(1));
        ins.delayBtn.setBounds(chips.removeFromLeft(chipW).reduced(1));
        ins.lockBtn.setBounds(chips.reduced(1));
        auto knobs = insertArea.removeFromTop(52);
        auto placeInsertKnob = [&](juce::Label& lab, juce::Slider& sl, bool on)
        {
            if (! on)
            {
                lab.setBounds({});
                sl.setBounds({});
                return;
            }
            const int w = juce::jmax(36, knobs.getWidth() / 3);
            auto cell = knobs.removeFromLeft(w);
            lab.setBounds(cell.removeFromTop(12));
            sl.setBounds(cell);
        };
        placeInsertKnob(ins.lpL, ins.lp, ins.lpOn);
        placeInsertKnob(ins.hpL, ins.hp, ins.hpOn);
        if (ins.delayOn)
        {
            const int w = juce::jmax(36, knobs.getWidth() / 2);
            auto wetCell = knobs.removeFromLeft(w);
            ins.delayL.setBounds(wetCell.removeFromTop(12));
            ins.delayWet.setBounds(wetCell);
            auto fbCell = knobs;
            ins.delayFeedbackL.setBounds(fbCell.removeFromTop(12));
            ins.delayFeedback.setBounds(fbCell);
        }
        else
        {
            ins.delayL.setBounds({});
            ins.delayWet.setBounds({});
            ins.delayFeedbackL.setBounds({});
            ins.delayFeedback.setBounds({});
        }
        ins.delayNote.setBounds(ins.delayOn ? insertArea.removeFromTop(18).reduced(2, 1) : juce::Rectangle<int>{});
        if (!ins.delayOn) insertArea.removeFromTop(18);
        auto fxPick = insertArea.removeFromTop(22);
        ins.fxEnable.setBounds(fxPick.removeFromRight(48).reduced(1));
        ins.fxSelect.setBounds(fxPick.reduced(1));
        auto fxKnobs = insertArea.removeFromTop(54);
        const int fxW = juce::jmax(34, fxKnobs.getWidth()/3);
        auto fxCellA=fxKnobs.removeFromLeft(fxW); ins.fxAL.setBounds(fxCellA.removeFromTop(12)); ins.fxA.setBounds(fxCellA);
        auto fxCellB=fxKnobs.removeFromLeft(fxW); ins.fxBL.setBounds(fxCellB.removeFromTop(12)); ins.fxB.setBounds(fxCellB);
        auto fxCellC=fxKnobs; ins.fxCL.setBounds(fxCellC.removeFromTop(12)); ins.fxC.setBounds(fxCellC);

        if (ins.selectedFx == 2 || ins.selectedFx == 3)
        {
            auto fxMore = insertArea.removeFromTop(46);
            const int moreW = juce::jmax(36, fxMore.getWidth()/5);
            auto placeMore = [&](juce::Label& lab, juce::Slider& sl)
            {
                auto cell = fxMore.removeFromLeft(moreW);
                lab.setBounds(cell.removeFromTop(12));
                sl.setBounds(cell);
            };
            placeMore(ins.fxDL, ins.fxD);
            placeMore(ins.fxEL, ins.fxE);
            placeMore(ins.fxFL, ins.fxF);
            placeMore(ins.fxGL, ins.fxG);
            placeMore(ins.fxHL, ins.fxH);
        }
        else
        {
            juce::Slider* sliders[] = { &ins.fxD, &ins.fxE, &ins.fxF, &ins.fxG, &ins.fxH };
            juce::Label* labels[] = { &ins.fxDL, &ins.fxEL, &ins.fxFL, &ins.fxGL, &ins.fxHL };
            for (auto* s : sliders) s->setBounds({});
            for (auto* l : labels) l->setBounds({});
        }

        const int slot = juce::jmax(36, col.getWidth() / 3);
        auto placeKnob = [&](juce::Label& lab, juce::Slider& sl)
        {
            auto cell = col.removeFromLeft(slot);
            lab.setBounds(cell.removeFromTop(14));
            sl.setBounds(cell);
        };
        placeKnob(aL, a);
        placeKnob(bL, b);
        placeKnob(cL, c);
    };

    placeCol(0, drumsTitle, drumVolL, drumVol, drumLL, drumLeft, drumRL, drumRight,
             &drumSound, &drumPrev, &drumNext, &drumUi);
    placeCol(1, synthTitle, synthVolL, synthVol, synthLL, synthLeft, synthRL, synthRight,
             &synthSound, &synthPrev, &synthNext, &synthUi);
    placeCol(2, polyTitle, polyVolL, polyVol, polyLL, polyLeft, polyRL, polyRight,
             &polySound, &polyPrev, &polyNext, &polyUi);
    placeCol(3, keysTitle, keysVolL, keysVol, keysLL, keysLeft, keysRL, keysRight,
             &keysSound, &keysPrev, &keysNext, &keysUi);

    auto bus = r.removeFromLeft(colW).reduced(4, 2);
    busTitle.setBounds(bus.removeFromTop(16));

    auto masterCol = bus.removeFromLeft(juce::jmax(56, bus.getWidth() / 4));
    masterL.setBounds(masterCol.removeFromTop(14));
    masterVol.setBounds(masterCol);

    auto fx = bus.removeFromTop(juce::jmax(52, bus.getHeight() / 4));
    compL.setBounds(fx.removeFromTop(14));
    busComp.setBounds(fx.removeFromLeft(juce::jmax(48, fx.getWidth() / 2)));

    eqTitle.setBounds(bus.removeFromTop(12));
    auto labs = bus.removeFromBottom(12);
    const int bandW = juce::jmax(12, bus.getWidth() / groove::kEqBands);
    for (int i = 0; i < groove::kEqBands; ++i)
    {
        eqSliders[(size_t) i].setBounds(bus.removeFromLeft(bandW).reduced(1, 0));
        eqLabels[(size_t) i].setBounds(labs.removeFromLeft(bandW));
    }
}
