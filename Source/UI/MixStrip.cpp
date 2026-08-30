#include "MixStrip.h"
#include "../Audio/MixBus.h"

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
    delayL.setText("WET", juce::dontSendNotification);
    delayFbL.setText("FB", juce::dontSendNotification);
    delayInfo.setText("1/8  ·  ping-pong  ·  off", juce::dontSendNotification);
    delayInfo.setMinimumHorizontalScale(0.65f);
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
                     &masterL, &compL, &delayL, &delayFbL, &delayInfo, &eqTitle })
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
    configure(busDelay, 0.0, 1.0, 0.01);
    busDelay.textFromValueFunction = [](double v)
    {
        return juce::String(juce::roundToInt(v * 100.0)) + "%";
    };
    busDelay.valueFromTextFunction = [](const juce::String& t)
    {
        return juce::jlimit(0.0, 1.0, t.upToFirstOccurrenceOf("%", false, false).getDoubleValue() / 100.0);
    };
    delayFeedback.setRange(0.0, 0.85, 0.01);
    delayFeedback.setSliderStyle(juce::Slider::LinearHorizontal);
    delayFeedback.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 14);
    delayFeedback.setColour(juce::Slider::trackColourId, juce::Colour(0xff2a4a5c));
    delayFeedback.setColour(juce::Slider::thumbColourId, juce::Colour(0xff7ac8ff));
    delayFeedback.textFromValueFunction = [](double v)
    {
        return juce::String(juce::roundToInt(v * 100.0)) + "%";
    };
    delayFeedback.valueFromTextFunction = [](const juce::String& t)
    {
        return juce::jlimit(0.0, 0.85, t.upToFirstOccurrenceOf("%", false, false).getDoubleValue() / 100.0);
    };
    addAndMakeVisible(delayFeedback);

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
    busDelay.setValue(0.0, juce::dontSendNotification);
    delayFeedback.setValue(0.38, juce::dontSendNotification);

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
                     &masterVol, &busComp, &busDelay, &delayFeedback })
        bind(*s);
    auto notifyDelay = [this]
    {
        refreshDelayInfo();
        if (refreshing)
            return;
        if (onChanged)
            onChanged();
    };
    busDelay.onValueChange = notifyDelay;
    delayFeedback.onValueChange = notifyDelay;
    for (int i = 0; i < groove::MixBus::kDelayNoteCount; ++i)
    {
        auto& b = delayNotes[(size_t) i];
        b.setButtonText(groove::MixBus::kDelayNoteNames[i]);
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a3a4e));
        b.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        b.onClick = [this, i] { selectDelayNote(i); };
        addAndMakeVisible(b);
    }
    delayInfo.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    delayInfo.setColour(juce::Label::textColourId, juce::Colour(0xff8aa0ae));
    delayInfo.setJustificationType(juce::Justification::centred);
    refreshDelayNotes();
    refreshDelayInfo();

    stylePicker(drumSound, drumPrev, drumNext);
    stylePicker(synthSound, synthPrev, synthNext);
    stylePicker(polySound, polyPrev, polyNext);
    stylePicker(keysSound, keysPrev, keysNext);
    drumSound.setTextWhenNothingSelected("DRUM SOUND");
    synthSound.setTextWhenNothingSelected("MOOG SOUND");
    keysSound.setTextWhenNothingSelected("KEYS SOUND");
    polySound.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0c1822));
    polySound.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffc5d8e4));

    for (auto* b : { &drumUi, &synthUi, &polyUi, &keysUi, &polyBrowse })
    {
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a3a4e));
        b->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        addAndMakeVisible(*b);
    }
}

void MixStrip::stylePicker(juce::ComboBox& box, juce::TextButton& prev, juce::TextButton& next)
{
    box.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(box);
    prev.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a3a4e));
    next.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a3a4e));
    prev.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    next.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    addAndMakeVisible(prev);
    addAndMakeVisible(next);
}

void MixStrip::stylePicker(juce::TextButton& box, juce::TextButton& prev, juce::TextButton& next)
{
    addAndMakeVisible(box);
    prev.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a3a4e));
    next.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a3a4e));
    prev.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    next.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    addAndMakeVisible(prev);
    addAndMakeVisible(next);
}

void MixStrip::configure(juce::Slider& s, double min, double max, double step)
{
    s.setRange(min, max, step);
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 14);
    addAndMakeVisible(s);
}

void MixStrip::configureEq(juce::Slider& s)
{
    s.setRange(-12.0, 12.0, 0.1);
    s.setSliderStyle(juce::Slider::LinearVertical);
    s.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    s.setValue(0.0, juce::dontSendNotification);
    s.setColour(juce::Slider::trackColourId, juce::Colour(0xff2a4a5c));
    s.setColour(juce::Slider::thumbColourId, juce::Colour(0xff7ac8ff));
    addAndMakeVisible(s);
}

void MixStrip::bind(juce::Slider& s)
{
    s.onValueChange = [this]
    {
        if (refreshing)
            return;
        if (onChanged)
            onChanged();
    };
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
    busDelay.setValue(m.busDelay, juce::dontSendNotification);
    delayFeedback.setValue(m.delayFeedback, juce::dontSendNotification);
    delayNote = juce::jlimit(0, groove::MixBus::kDelayNoteCount - 1, m.delayNote);
    refreshDelayNotes();
    refreshDelayInfo();
    for (int i = 0; i < groove::kEqBands; ++i)
        eqSliders[(size_t) i].setValue(m.eqGainDb[(size_t) i], juce::dontSendNotification);
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
    m.busDelay = (float) busDelay.getValue();
    m.delayFeedback = (float) delayFeedback.getValue();
    m.delayNote = delayNote;
    for (int i = 0; i < groove::kEqBands; ++i)
        m.eqGainDb[(size_t) i] = (float) eqSliders[(size_t) i].getValue();
}

void MixStrip::setActiveMidiChannel(int channel)
{
    activeMidiChannel = (channel >= 1 && channel <= 4) ? channel : 0;
    refreshChannelHighlight();
    repaint();
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
    busTitle.setColour(juce::Label::textColourId, dim);
}

void MixStrip::setBpm(double bpm)
{
    currentBpm = juce::jlimit(40.0, 260.0, bpm);
    refreshDelayInfo();
}

void MixStrip::refreshDelayNotes()
{
    for (int i = 0; i < groove::MixBus::kDelayNoteCount; ++i)
    {
        const bool on = delayNote == i;
        delayNotes[(size_t) i].setColour(juce::TextButton::buttonColourId,
                                         on ? juce::Colour(0xff2a6a88) : juce::Colour(0xff1a3a4e));
        delayNotes[(size_t) i].setColour(juce::TextButton::textColourOffId,
                                         on ? juce::Colour(0xffe8f6ff) : juce::Colours::white);
    }
}

void MixStrip::selectDelayNote(int index)
{
    delayNote = juce::jlimit(0, groove::MixBus::kDelayNoteCount - 1, index);
    refreshDelayNotes();
    refreshDelayInfo();
    if (refreshing)
        return;
    if (onChanged)
        onChanged();
}

void MixStrip::refreshDelayInfo()
{
    const int note = juce::jlimit(0, groove::MixBus::kDelayNoteCount - 1, delayNote);
    const auto name = juce::String(groove::MixBus::kDelayNoteNames[note]);
    const double beats = groove::MixBus::delayBeatsForNote(note);
    const int ms = juce::roundToInt((60000.0 / juce::jmax(40.0, currentBpm)) * beats);
    const int wet = juce::roundToInt(busDelay.getValue() * 100.0);
    const int fb = juce::roundToInt(delayFeedback.getValue() * 100.0);
    juce::String text = name + "  ·  ping-pong  ·  " + juce::String(ms) + " ms";
    if (wet <= 0)
        text += "\noff  ·  FB " + juce::String(fb) + "%";
    else
        text += "\nFB " + juce::String(fb) + "%  ·  " + juce::String(wet) + "% wet";
    delayInfo.setText(text, juce::dontSendNotification);
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
    auto placeCol = [&](juce::Label& title, juce::Label& aL, juce::Slider& a,
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

    placeCol(drumsTitle, drumVolL, drumVol, drumLL, drumLeft, drumRL, drumRight,
             &drumSound, &drumPrev, &drumNext, &drumUi);
    placeCol(synthTitle, synthVolL, synthVol, synthLL, synthLeft, synthRL, synthRight,
             &synthSound, &synthPrev, &synthNext, &synthUi);
    placeCol(polyTitle, polyVolL, polyVol, polyLL, polyLeft, polyRL, polyRight,
             &polySound, &polyPrev, &polyNext, &polyUi);
    placeCol(keysTitle, keysVolL, keysVol, keysLL, keysLeft, keysRL, keysRight,
             &keysSound, &keysPrev, &keysNext, &keysUi);

    auto bus = r.removeFromLeft(colW).reduced(4, 2);
    busTitle.setBounds(bus.removeFromTop(16));

    auto masterCol = bus.removeFromLeft(juce::jmax(56, bus.getWidth() / 4));
    masterL.setBounds(masterCol.removeFromTop(14));
    masterVol.setBounds(masterCol);

    auto fx = bus.removeFromTop(juce::jmax(52, bus.getHeight() / 3));
    const int knobW = juce::jmax(36, fx.getWidth() / 2);
    auto placeBusKnob = [&](juce::Label& lab, juce::Slider& sl)
    {
        auto cell = fx.removeFromLeft(knobW);
        lab.setBounds(cell.removeFromTop(14));
        sl.setBounds(cell);
    };
    placeBusKnob(compL, busComp);
    placeBusKnob(delayL, busDelay);

    auto noteRow = bus.removeFromTop(20);
    const int noteW = juce::jmax(22, noteRow.getWidth() / groove::MixBus::kDelayNoteCount);
    for (int i = 0; i < groove::MixBus::kDelayNoteCount; ++i)
        delayNotes[(size_t) i].setBounds(noteRow.removeFromLeft(noteW).reduced(1, 1));

    auto fbRow = bus.removeFromTop(18);
    delayFbL.setBounds(fbRow.removeFromLeft(18));
    delayFeedback.setBounds(fbRow.reduced(0, 1));
    delayInfo.setBounds(bus.removeFromTop(28));

    eqTitle.setBounds(bus.removeFromTop(12));
    auto labs = bus.removeFromBottom(12);
    const int bandW = juce::jmax(12, bus.getWidth() / groove::kEqBands);
    for (int i = 0; i < groove::kEqBands; ++i)
    {
        eqSliders[(size_t) i].setBounds(bus.removeFromLeft(bandW).reduced(1, 0));
        eqLabels[(size_t) i].setBounds(labs.removeFromLeft(bandW));
    }
}
