#include "MixStrip.h"

namespace
{
constexpr const char* kTitles[] = { "DRUMS  |  CH1", "MOOG  |  CH2", "MAXPOLY  |  CH3", "KEYS  |  CH4" };
}

MixStrip::MixStrip()
{
    setOpaque(false);
    auto initTitle = [this](juce::Label& l, const juce::String& text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setFont(juce::FontOptions(7.5f, juce::Font::bold));
        l.setColour(juce::Label::textColourId, juce::Colour(0xffd8e8f1));
        l.setJustificationType(juce::Justification::centred);
        l.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        addAndMakeVisible(l);
        l.addMouseListener(this, false);
    };
    initTitle(drumsTitle, kTitles[0]); initTitle(synthTitle, kTitles[1]);
    initTitle(polyTitle, kTitles[2]); initTitle(keysTitle, kTitles[3]);
    initTitle(busTitle, "FX BUS");

    static constexpr const char* fxNames[kFxCount] = { "PARADISE", "CENTURY", "COMP", "GALAXY", "CAPITOL" };
    for (int c = 0; c < groove::kMixChannels; ++c)
    {
        configure(vols[(size_t)c], 0.0, 1.5, 0.01);
        configure(pans[(size_t)c], -1.0, 1.0, 0.01);
        vols[(size_t)c].setValue(1.0, juce::dontSendNotification);
        pans[(size_t)c].setValue(0.0, juce::dontSendNotification);
        volLabels[(size_t)c].setText("VOL", juce::dontSendNotification);
        panLabels[(size_t)c].setText("PAN", juce::dontSendNotification);
        for (auto* l : { &volLabels[(size_t)c], &panLabels[(size_t)c] })
        {
            l->setFont(juce::FontOptions(7.0f, juce::Font::bold));
            l->setColour(juce::Label::textColourId, juce::Colour(0xff8aa0ae));
            l->setJustificationType(juce::Justification::centred);
            addAndMakeVisible(*l);
        }

        for (int f = 0; f < kFxCount; ++f)
        {
            configure(fxSends[(size_t)c][(size_t)f], 0.0, 1.0, 0.01);
            fxSends[(size_t)c][(size_t)f].setSliderStyle(juce::Slider::LinearHorizontal);
            fxSends[(size_t)c][(size_t)f].setTextBoxStyle(juce::Slider::TextBoxRight, false, 36, 16);
            fxSendLabels[(size_t)c][(size_t)f].setText(fxNames[f], juce::dontSendNotification);
            fxSendLabels[(size_t)c][(size_t)f].setFont(juce::FontOptions(7.0f, juce::Font::bold));
            fxSendLabels[(size_t)c][(size_t)f].setColour(juce::Label::textColourId, juce::Colour(0xff8aa0ae));
            fxSendLabels[(size_t)c][(size_t)f].setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(fxSendLabels[(size_t)c][(size_t)f]);
            fxSends[(size_t)c][(size_t)f].addMouseListener(this, false);
            fxSendLabels[(size_t)c][(size_t)f].addMouseListener(this, false);

            auto& b = fxSendButtons[(size_t)c][(size_t)f];
            b.setButtonText("OFF"); addAndMakeVisible(b); styleChip(b, false);
            b.onClick = [this, c, f]
            {
                bool* on = nullptr;
                switch (f)
                {
                    case 0: on = &sharedFx.paradiseSendOn[(size_t)c]; break;
                    case 1: on = &sharedFx.centurySendOn[(size_t)c]; break;
                    case 2: on = &sharedFx.compressorSendOn[(size_t)c]; break;
                    case 3: on = &sharedFx.galaxySendOn[(size_t)c]; break;
                    default:on = &sharedFx.capitolSendOn[(size_t)c]; break;
                }
                *on = !*on;
                auto& button = fxSendButtons[(size_t)c][(size_t)f];
                button.setButtonText(*on ? "ON" : "OFF"); styleChip(button, *on); notify();
            };
            fxSends[(size_t)c][(size_t)f].onValueChange = [this, c, f]
            {
                if (refreshing) return;
                const float v = (float)fxSends[(size_t)c][(size_t)f].getValue();
                switch (f)
                {
                    case 0: sharedFx.paradiseSend[(size_t)c] = v; break;
                    case 1: sharedFx.centurySend[(size_t)c] = v; break;
                    case 2: sharedFx.compressorSend[(size_t)c] = v; break;
                    case 3: sharedFx.galaxySend[(size_t)c] = v; break;
                    default:sharedFx.capitolSend[(size_t)c] = v; break;
                }
                notify();
            };
        }
        vols[(size_t)c].onValueChange = [this] { notify(); };
        pans[(size_t)c].onValueChange = [this] { notify(); };
    }

    stylePicker(drumSound, drumPrev, drumNext); stylePicker(synthSound, synthPrev, synthNext);
    stylePicker(keysSound, keysPrev, keysNext); stylePicker(polySound, polyPrev, polyNext);
    for (auto* b : { &drumUi, &synthUi, &keysUi, &polyUi, &polyBrowse }) addAndMakeVisible(*b);

    fxSelect.addItem("PARADISE GUITAR STUDIO", 1);
    fxSelect.addItem("CENTURY TUBE CHANNEL STRIP", 2);
    fxSelect.addItem("COMPRESSOR", 3);
    fxSelect.addItem("GALAXY TAPE ECHO", 4);
    fxSelect.addItem("CAPITOL CHAMBERS", 5);
    fxSelect.setSelectedId(1, juce::dontSendNotification);
    fxSelect.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff12202a));
    fxSelect.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff2a4452));
    addAndMakeVisible(fxSelect);
    addAndMakeVisible(fxEnable);
    addAndMakeVisible(compactButton);
    compactButton.setTooltip("Compact mixer: channels + sends only");
    compactButton.onClick = [this] { setCompactMode(! compactMode); };
    fxSelect.onChange = [this]
    {
        selectedFx = juce::jlimit(0, 4, fxSelect.getSelectedId() - 1);
        refreshFxEditor(); resized();
    };
    fxEnable.onClick = [this]
    {
        bool* p = nullptr;
        if (selectedFx == 0) p = &sharedFx.paradiseOn;
        else if (selectedFx == 1) p = &sharedFx.centuryOn;
        else if (selectedFx == 2) p = &sharedFx.compressorOn;
        else if (selectedFx == 3) p = &sharedFx.galaxyOn;
        else p = &sharedFx.capitolOn;
        *p = !*p; refreshFxEditor(); notify();
    };

    for (int i = 0; i < 8; ++i)
    {
        configure(fxKnobs[(size_t)i], 0.0, 1.0, 0.01);
        fxLabels[(size_t)i].setFont(juce::FontOptions(7.0f, juce::Font::bold));
        fxLabels[(size_t)i].setColour(juce::Label::textColourId, juce::Colour(0xff8aa0ae));
        fxLabels[(size_t)i].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(fxLabels[(size_t)i]);
        bindFxKnob(fxKnobs[(size_t)i], i);
    }
    configure(fxReturn, 0.0, 1.5, 0.01); fxReturn.setValue(sharedFx.returnLevel, juce::dontSendNotification);
    configure(masterVol, 0.0, 1.5, 0.01); masterVol.setValue(1.0, juce::dontSendNotification);
    returnL.setText("FX RETURN", juce::dontSendNotification); masterL.setText("MASTER", juce::dontSendNotification);
    for (auto* l : { &returnL, &masterL }) { l->setFont(juce::FontOptions(7.0f, juce::Font::bold)); l->setColour(juce::Label::textColourId, juce::Colour(0xffe6f0f5)); l->setJustificationType(juce::Justification::centred); addAndMakeVisible(*l); }
    fxReturn.onValueChange = [this] { if (!refreshing) { sharedFx.returnLevel = (float)fxReturn.getValue(); notify(); } };
    masterVol.onValueChange = [this] { notify(); };
    refreshFxEditor(); refreshChannelHighlight();
    // Open in full-detail mode by default so effect controls are immediately available.
    setCompactMode(false);
}

void MixStrip::configure(juce::Slider& s, double min, double max, double step)
{
    s.setRange(min, max, step); s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 40, 12);
    s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff3d8ec4));
    s.setColour(juce::Slider::thumbColourId, juce::Colour(0xff7ac8ff)); addAndMakeVisible(s);
}

void MixStrip::stylePicker(juce::ComboBox& box, juce::TextButton& prev, juce::TextButton& next)
{
    box.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff12202a)); box.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff2a4452));
    addAndMakeVisible(box); addAndMakeVisible(prev); addAndMakeVisible(next);
}
void MixStrip::stylePicker(juce::TextButton& box, juce::TextButton& prev, juce::TextButton& next)
{ addAndMakeVisible(box); addAndMakeVisible(prev); addAndMakeVisible(next); }
void MixStrip::styleChip(juce::TextButton& b, bool on)
{
    b.setColour(juce::TextButton::buttonColourId, on ? juce::Colour(0xff236d50) : juce::Colour(0xff152430));
    b.setColour(juce::TextButton::textColourOffId, on ? juce::Colours::white : juce::Colour(0xff8aa0ae));
}
void MixStrip::notify() { if (!refreshing && onChanged) onChanged(); }

void MixStrip::setCompactMode(bool compact)
{
    if (compactMode == compact) return;
    compactMode = compact;
    compactButton.setButtonText(compactMode ? "DETAIL" : "COMPACT");
    fxSelect.setVisible(! compactMode);
    fxEnable.setVisible(! compactMode);
    busTitle.setVisible(! compactMode);
    for (auto& k : fxKnobs) k.setVisible(! compactMode);
    for (auto& l : fxLabels) l.setVisible(! compactMode);
    resized();
    repaint();
    if (onCompactModeChanged) onCompactModeChanged(compactMode);
}

void MixStrip::bindFxKnob(juce::Slider& s, int i)
{
    s.onValueChange = [this, i, &s]
    {
        if (refreshing) return; const float v=(float)s.getValue();
        if (selectedFx==0) { float* a[]={&sharedFx.paradiseInput,&sharedFx.paradiseGate,&sharedFx.paradiseAmp,&sharedFx.paradiseRoom,&sharedFx.paradiseOutput}; if(i<5)*a[i]=v; }
        else if(selectedFx==1) { float* a[]={&sharedFx.centuryPreamp,&sharedFx.centuryLow,&sharedFx.centuryMid,&sharedFx.centuryMidFreq,&sharedFx.centuryHigh,&sharedFx.centuryComp,&sharedFx.centuryOutput}; if(i<7)*a[i]=v; }
        else if(selectedFx==2) { float* a[]={&sharedFx.compThreshold,&sharedFx.compRatio,&sharedFx.compAttack,&sharedFx.compRelease,&sharedFx.compMakeup}; if(i<5)*a[i]=v; }
        else if(selectedFx==3) { float* a[]={&sharedFx.galaxyDelay,&sharedFx.galaxyFeedback,&sharedFx.galaxyEcho,&sharedFx.galaxyReverb,&sharedFx.galaxyTreble,&sharedFx.galaxyBass,&sharedFx.galaxyInput}; if(i<7)*a[i]=v; }
        else { float* a[]={&sharedFx.capitolSize,&sharedFx.capitolDecay,&sharedFx.capitolPreDelay,&sharedFx.capitolWidth,&sharedFx.capitolBass,&sharedFx.capitolMid,&sharedFx.capitolTreble,&sharedFx.capitolVolume}; if(i<8)*a[i]=v; }
        notify();
    };
}

void MixStrip::refreshFxEditor()
{
    const char* labels[8] = {"","","","","","","",""}; int count=0; bool on=false; float vals[8]{};
    if(selectedFx==0){ const char* x[]={"IN","GATE","AMP","ROOM","OUT"}; count=5; on=sharedFx.paradiseOn; float v[]={sharedFx.paradiseInput,sharedFx.paradiseGate,sharedFx.paradiseAmp,sharedFx.paradiseRoom,sharedFx.paradiseOutput}; for(int i=0;i<count;++i){labels[i]=x[i];vals[i]=v[i];}}
    else if(selectedFx==1){ const char* x[]={"PREAMP","LOW","MID","MID F","HIGH","COMP","OUT"}; count=7; on=sharedFx.centuryOn; float v[]={sharedFx.centuryPreamp,sharedFx.centuryLow,sharedFx.centuryMid,sharedFx.centuryMidFreq,sharedFx.centuryHigh,sharedFx.centuryComp,sharedFx.centuryOutput}; for(int i=0;i<count;++i){labels[i]=x[i];vals[i]=v[i];}}
    else if(selectedFx==2){ const char* x[]={"THRESH","RATIO","ATTACK","RELEASE","MAKEUP"}; count=5; on=sharedFx.compressorOn; float v[]={sharedFx.compThreshold,sharedFx.compRatio,sharedFx.compAttack,sharedFx.compRelease,sharedFx.compMakeup}; for(int i=0;i<count;++i){labels[i]=x[i];vals[i]=v[i];}}
    else if(selectedFx==3){ const char* x[]={"DELAY","FEEDBACK","ECHO","REVERB","TREBLE","BASS","INPUT"}; count=7; on=sharedFx.galaxyOn; float v[]={sharedFx.galaxyDelay,sharedFx.galaxyFeedback,sharedFx.galaxyEcho,sharedFx.galaxyReverb,sharedFx.galaxyTreble,sharedFx.galaxyBass,sharedFx.galaxyInput}; for(int i=0;i<count;++i){labels[i]=x[i];vals[i]=v[i];}}
    else { const char* x[]={"SIZE","DECAY","PRE-DLY","WIDTH","BASS","MID","TREBLE","VOL"}; count=8; on=sharedFx.capitolOn; float v[]={sharedFx.capitolSize,sharedFx.capitolDecay,sharedFx.capitolPreDelay,sharedFx.capitolWidth,sharedFx.capitolBass,sharedFx.capitolMid,sharedFx.capitolTreble,sharedFx.capitolVolume}; for(int i=0;i<count;++i){labels[i]=x[i];vals[i]=v[i];}}
    const juce::ScopedValueSetter<bool> sv(refreshing,true);
    fxEnable.setButtonText(on?"ON":"OFF"); styleChip(fxEnable,on);
    for(int i=0;i<8;++i){ const bool vis=!compactMode && i<count; fxLabels[(size_t)i].setVisible(vis); fxKnobs[(size_t)i].setVisible(vis); if(i<count){fxLabels[(size_t)i].setText(labels[i],juce::dontSendNotification);fxKnobs[(size_t)i].setValue(vals[i],juce::dontSendNotification);} }
}

void MixStrip::loadFrom(const groove::MixSettings& m)
{
    const juce::ScopedValueSetter<bool> sv(refreshing,true);
    const float v[]={m.drumVol,m.synthVol,m.polyVol,m.keysVol};
    const float l[]={m.drumLeft,m.synthLeft,m.polyLeft,m.keysLeft}; const float r[]={m.drumRight,m.synthRight,m.polyRight,m.keysRight};
    for(int c=0;c<4;++c){vols[(size_t)c].setValue(v[c],juce::dontSendNotification); const float denom=juce::jmax(0.001f,l[c]+r[c]); pans[(size_t)c].setValue(juce::jlimit(-1.0f,1.0f,(r[c]-l[c])/denom),juce::dontSendNotification);}
    masterVol.setValue(m.masterVol,juce::dontSendNotification); sharedFx=m.sharedFx; fxReturn.setValue(sharedFx.returnLevel,juce::dontSendNotification);
    for(int c=0;c<4;++c)
    {
        const bool ons[kFxCount] = { sharedFx.paradiseSendOn[(size_t)c], sharedFx.centurySendOn[(size_t)c], sharedFx.compressorSendOn[(size_t)c], sharedFx.galaxySendOn[(size_t)c], sharedFx.capitolSendOn[(size_t)c] };
        const float vals[kFxCount] = { sharedFx.paradiseSend[(size_t)c], sharedFx.centurySend[(size_t)c], sharedFx.compressorSend[(size_t)c], sharedFx.galaxySend[(size_t)c], sharedFx.capitolSend[(size_t)c] };
        for(int f=0;f<kFxCount;++f)
        {
            fxSends[(size_t)c][(size_t)f].setValue(vals[f],juce::dontSendNotification);
            fxSendButtons[(size_t)c][(size_t)f].setButtonText(ons[f]?"ON":"OFF");
            styleChip(fxSendButtons[(size_t)c][(size_t)f],ons[f]);
        }
    }
    refreshFxEditor(); resized();
}

void MixStrip::saveTo(groove::MixSettings& m) const
{
    const float v[]={ (float)vols[0].getValue(),(float)vols[1].getValue(),(float)vols[2].getValue(),(float)vols[3].getValue() };
    float left[4],right[4]; for(int c=0;c<4;++c){ const float p=(float)pans[(size_t)c].getValue(); left[c]=p>0?1.0f-p:1.0f; right[c]=p<0?1.0f+p:1.0f; }
    m.drumVol=v[0];m.drumLeft=left[0];m.drumRight=right[0]; m.synthVol=v[1];m.synthLeft=left[1];m.synthRight=right[1];
    m.polyVol=v[2];m.polyLeft=left[2];m.polyRight=right[2]; m.keysVol=v[3];m.keysLeft=left[3];m.keysRight=right[3]; m.masterVol=(float)masterVol.getValue();
    m.sharedFx=sharedFx; m.sharedFx.returnLevel=(float)fxReturn.getValue();
    for(int c=0;c<4;++c)
    {
        m.sharedFx.paradiseSend[(size_t)c]=(float)fxSends[(size_t)c][0].getValue();
        m.sharedFx.centurySend[(size_t)c]=(float)fxSends[(size_t)c][1].getValue();
        m.sharedFx.compressorSend[(size_t)c]=(float)fxSends[(size_t)c][2].getValue();
        m.sharedFx.galaxySend[(size_t)c]=(float)fxSends[(size_t)c][3].getValue();
        m.sharedFx.capitolSend[(size_t)c]=(float)fxSends[(size_t)c][4].getValue();
    }
    // Old per-track creative effects remain serialized for compatibility, but are disabled and never processed.
    for(auto& ch:m.channelFx){ch.driveOn=false;ch.ringOn=false;ch.combOn=false;ch.paradiseOn=false;ch.reverbOn=false;ch.delayOn=false;ch.lpOn=false;ch.hpOn=false;}
}

void MixStrip::setActiveMidiChannel(int channel){activeMidiChannel=(channel>=1&&channel<=4)?channel:0;refreshChannelHighlight();repaint();}
void MixStrip::refreshChannelHighlight()
{
    juce::Label* ls[]={&drumsTitle,&synthTitle,&polyTitle,&keysTitle}; for(int i=0;i<4;++i){const bool on=activeMidiChannel==i+1;ls[i]->setText(on ? (juce::String("▶  ") + kTitles[i]) : juce::String(kTitles[i]), juce::dontSendNotification);ls[i]->setColour(juce::Label::textColourId,on?juce::Colour(0xffe8f6ff):juce::Colour(0xff8aa0ae));}
    busTitle.setText("FX BUS  |  UADx AUX RETURNS",juce::dontSendNotification);
}

void MixStrip::paint(juce::Graphics& g)
{
    auto r=getLocalBounds().reduced(3,2); const int colW=r.getWidth()/5;for(int i=0;i<5;++i){auto col=r.removeFromLeft(i==4?r.getWidth():colW).reduced(2,0).toFloat();const bool on=i<4&&activeMidiChannel==i+1;g.setColour(on?juce::Colour(0xff1a4a62):juce::Colour(0xff0d1c26));g.fillRoundedRectangle(col,6.0f);if(on){g.setColour(juce::Colour(0xff7ac8ff));g.drawRoundedRectangle(col.reduced(.5f),6.0f,2.0f);}}
}
void MixStrip::mouseDown(const juce::MouseEvent& e)
{
    if(!onChannelClicked)return;auto* s=e.eventComponent;if(s==&drumsTitle)onChannelClicked(1);else if(s==&synthTitle)onChannelClicked(2);else if(s==&polyTitle)onChannelClicked(3);else if(s==&keysTitle)onChannelClicked(4);
}

void MixStrip::mouseDoubleClick(const juce::MouseEvent& e)
{
    auto* c = e.eventComponent;
    if (onOpenInstrumentUi)
    {
        if(c==&drumsTitle){onOpenInstrumentUi(1);return;}
        if(c==&synthTitle){onOpenInstrumentUi(2);return;}
        if(c==&polyTitle){onOpenInstrumentUi(3);return;}
        if(c==&keysTitle){onOpenInstrumentUi(4);return;}
    }
    for(int ch=0; ch<groove::kMixChannels; ++ch)
        for(int f=0; f<kFxCount; ++f)
            if(c==&fxSends[(size_t)ch][(size_t)f] || c==&fxSendLabels[(size_t)ch][(size_t)f])
            {
                selectedFx=f;
                fxSelect.setSelectedId(f+1, juce::sendNotificationSync);
                if(compactMode) setCompactMode(false);
                refreshFxEditor();
                return;
            }
}

void MixStrip::resized()
{
    auto r=getLocalBounds().reduced(3,2);
    // Compact mode is deliberately dense: four complete channels + master bus must
    // fit comfortably beside the main application on a laptop-sized display.
    const int busW = compactMode ? juce::jlimit(96, 112, r.getWidth() / 8)
                                 : juce::jmax(210, r.getWidth()/5);
    const int channelAreaW = juce::jmax(1, r.getWidth() - busW);
    const int colW = channelAreaW / 4;
    juce::Label* titles[]={&drumsTitle,&synthTitle,&polyTitle,&keysTitle};
    juce::Component* sounds[]={&drumSound,&synthSound,&polySound,&keysSound};
    juce::TextButton* prev[]={&drumPrev,&synthPrev,&polyPrev,&keysPrev};
    juce::TextButton* next[]={&drumNext,&synthNext,&polyNext,&keysNext};
    juce::TextButton* ui[]={&drumUi,&synthUi,&polyUi,&keysUi};
    for(int c=0;c<4;++c)
    {
        auto col=r.removeFromLeft(colW).reduced(3,2);
        auto tr=col.removeFromTop(compactMode ? 13 : 15); ui[c]->setBounds(tr.removeFromRight(compactMode ? 38 : 43)); if(c==2)polyBrowse.setBounds(tr.removeFromRight(compactMode ? 29 : 34)); titles[c]->setBounds(tr);
        auto picker=col.removeFromBottom(compactMode ? 18 : 20); prev[c]->setBounds(picker.removeFromLeft(compactMode ? 15 : 18).reduced(1)); next[c]->setBounds(picker.removeFromRight(compactMode ? 15 : 18).reduced(1)); sounds[c]->setBounds(picker.reduced(1));
        auto knobs=col.removeFromTop(compactMode ? 48 : 64); const int w=knobs.getWidth()/2; auto a=knobs.removeFromLeft(w); volLabels[(size_t)c].setBounds(a.removeFromTop(9)); vols[(size_t)c].setBounds(a); panLabels[(size_t)c].setBounds(knobs.removeFromTop(9)); pans[(size_t)c].setBounds(knobs);
        col.removeFromTop(2);
        for(int f=0;f<kFxCount;++f)
        {
            auto row=col.removeFromTop(compactMode ? 18 : 23);
            fxSendButtons[(size_t)c][(size_t)f].setBounds(row.removeFromRight(compactMode ? 24 : 27).reduced(1,1));
            fxSendLabels[(size_t)c][(size_t)f].setBounds(row.removeFromLeft(compactMode ? 36 : 43));
            // Keep a small numeric readout while giving the send slider most of the width.
            fxSends[(size_t)c][(size_t)f].setTextBoxStyle(juce::Slider::TextBoxRight, false, compactMode ? 30 : 36, compactMode ? 14 : 16);
            fxSends[(size_t)c][(size_t)f].setBounds(row.reduced(1));
        }
    }

    auto bus=r.reduced(3,2);
    if (compactMode)
    {
        compactButton.setBounds(bus.removeFromTop(18).reduced(1));
        bus.removeFromTop(2);
        auto ret=bus.removeFromTop(58); returnL.setBounds(ret.removeFromTop(9)); fxReturn.setBounds(ret);
        bus.removeFromTop(2);
        auto mas=bus.removeFromTop(58); masterL.setBounds(mas.removeFromTop(9)); masterVol.setBounds(mas);
        return;
    }

    auto header=bus.removeFromTop(18); compactButton.setBounds(header.removeFromRight(62).reduced(1)); busTitle.setBounds(header);
    auto top=bus.removeFromTop(22); fxEnable.setBounds(top.removeFromRight(46).reduced(1)); fxSelect.setBounds(top.reduced(1)); bus.removeFromTop(4);
    auto karea=bus.removeFromTop(100); const int kw=juce::jmax(34,karea.getWidth()/4); for(int i=0;i<8;++i){if(!fxKnobs[(size_t)i].isVisible())continue;auto cell=karea.removeFromLeft(kw);if(cell.isEmpty()){karea=bus.removeFromTop(56);cell=karea.removeFromLeft(kw);}fxLabels[(size_t)i].setBounds(cell.removeFromTop(11));fxKnobs[(size_t)i].setBounds(cell);}
    auto bottom=bus.removeFromBottom(66); auto ret=bottom.removeFromLeft(bottom.getWidth()/2); returnL.setBounds(ret.removeFromTop(10)); fxReturn.setBounds(ret); masterL.setBounds(bottom.removeFromTop(10)); masterVol.setBounds(bottom);
}


