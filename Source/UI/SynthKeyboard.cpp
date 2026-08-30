#include "SynthKeyboard.h"
#include "../Audio/DrumMidi.h"

namespace
{
int whiteCountBefore(int noteFromC0)
{
    static constexpr int whites[] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    const int n = juce::jmax(0, noteFromC0);
    return (n / 12) * 7 + whites[n % 12];
}
}

SynthKeyboard::SynthKeyboard()
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);
    octaveDown.setWantsKeyboardFocus(false);
    octaveUp.setWantsKeyboardFocus(false);
    octaveDown.setMouseClickGrabsKeyboardFocus(false);
    octaveUp.setMouseClickGrabsKeyboardFocus(false);
    octaveDown.onClick = [this] { shiftOctave(-1); };
    octaveUp.onClick = [this] { shiftOctave(1); };
    addAndMakeVisible(octaveDown);
    addAndMakeVisible(octaveUp);

    auto styleTarget = [](juce::TextButton& b)
    {
        b.setWantsKeyboardFocus(false);
        b.setMouseClickGrabsKeyboardFocus(false);
        b.setClickingTogglesState(true);
        b.setRadioGroupId(71);
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff142430));
        b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2e8ec4));
        b.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff8aa0ae));
        b.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    };
    styleTarget(drumsTarget);
    styleTarget(moogTarget);
    styleTarget(keysTarget);
    styleTarget(polyTarget);
    moogTarget.setToggleState(true, juce::dontSendNotification);
    drumsTarget.onClick = [this] { setTarget(3); if (onUserPickedTarget) onUserPickedTarget(); };
    moogTarget.onClick = [this] { setTarget(0); if (onUserPickedTarget) onUserPickedTarget(); };
    keysTarget.onClick = [this] { setTarget(1); if (onUserPickedTarget) onUserPickedTarget(); };
    polyTarget.onClick = [this] { setTarget(2); if (onUserPickedTarget) onUserPickedTarget(); };
    addAndMakeVisible(drumsTarget);
    addAndMakeVisible(moogTarget);
    addAndMakeVisible(keysTarget);
    addAndMakeVisible(polyTarget);
}

SynthKeyboard::~SynthKeyboard()
{
    allNotesOff();
}

void SynthKeyboard::setExternalHeld(int note, bool down)
{
    if (note < 0 || note > 127)
        return;
    held[(size_t) note] = down;
    repaint();
}

void SynthKeyboard::allNotesOff()
{
    for (int n = 0; n < 128; ++n)
    {
        if (held[(size_t) n] && onNoteOff)
            onNoteOff(n);
        held[(size_t) n] = false;
        computerHeld[(size_t) n] = false;
    }
    mouseNote = -1;
    repaint();
}

bool SynthKeyboard::isBlackKey(int note) noexcept
{
    const int pc = ((note % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

void SynthKeyboard::setOctaveOffset(int offset)
{
    const int next = (offset < kMinOctave || offset > kMaxOctave) ? 0 : offset;
    if (next == octaveOffset)
        return;
    for (int n = 0; n < 128; ++n)
        if (computerHeld[(size_t) n])
            noteOff(n);
    releaseMouseNote();
    octaveOffset = next;
    if (onOctaveChanged)
        onOctaveChanged();
    repaint();
}

void SynthKeyboard::shiftOctave(int delta)
{
    const int next = juce::jlimit(kMinOctave, kMaxOctave, octaveOffset + delta);
    if (next == octaveOffset)
        return;
    setOctaveOffset(next);
}

void SynthKeyboard::setTarget(int target)
{
    const int next = juce::jlimit(0, 3, target);
    moogTarget.setToggleState(next == 0, juce::dontSendNotification);
    keysTarget.setToggleState(next == 1, juce::dontSendNotification);
    polyTarget.setToggleState(next == 2, juce::dontSendNotification);
    drumsTarget.setToggleState(next == 3, juce::dontSendNotification);
    if (next == keyboardTarget)
        return;
    allNotesOff();
    keyboardTarget = next;
    if (onTargetChanged)
        onTargetChanged();
    repaint();
}

void SynthKeyboard::noteOn(int note, float velocity)
{
    note = juce::jlimit(0, 127, note);
    if (held[(size_t) note])
        return;
    held[(size_t) note] = true;
    if (onNoteOn)
        onNoteOn(note, velocity);
    repaint();
}

void SynthKeyboard::noteOff(int note)
{
    note = juce::jlimit(0, 127, note);
    if (! held[(size_t) note])
        return;
    held[(size_t) note] = false;
    computerHeld[(size_t) note] = false;
    if (onNoteOff)
        onNoteOff(note);
    repaint();
}

void SynthKeyboard::resized()
{
    auto bar = getLocalBounds().removeFromTop(22).reduced(6, 2);
    octaveUp.setBounds(bar.removeFromRight(28));
    bar.removeFromRight(4);
    octaveDown.setBounds(bar.removeFromRight(28));
    bar.removeFromRight(8);
    keysTarget.setBounds(bar.removeFromRight(46));
    bar.removeFromRight(3);
    polyTarget.setBounds(bar.removeFromRight(46));
    bar.removeFromRight(3);
    moogTarget.setBounds(bar.removeFromRight(46));
    bar.removeFromRight(3);
    drumsTarget.setBounds(bar.removeFromRight(50));
}

int SynthKeyboard::noteAt(juce::Point<int> pos) const
{
    const int lo = lowestNote();
    const int hi = highestNote();
    auto keys = getLocalBounds().withTrimmedTop(22).reduced(8, 6);
    if (! keys.contains(pos))
        return -1;

    const int whites = 7 * kOctaves + 1;
    const float ww = (float) keys.getWidth() / (float) whites;
    const float blackW = ww * 0.62f;
    const float blackH = (float) keys.getHeight() * 0.58f;

    for (int n = lo; n <= hi; ++n)
    {
        if (! isBlackKey(n))
            continue;
        const int wi = whiteCountBefore(n - lo + 1);
        const float x = (float) keys.getX() + (float) wi * ww - blackW * 0.5f;
        const auto r = juce::Rectangle<float>(x, (float) keys.getY(), blackW, blackH);
        if (r.contains(pos.toFloat()))
            return n;
    }

    const int wi = juce::jlimit(0, whites - 1, (int) ((pos.x - keys.getX()) / ww));
    int seen = 0;
    for (int n = lo; n <= hi; ++n)
    {
        if (isBlackKey(n))
            continue;
        if (seen == wi)
            return n;
        ++seen;
    }
    return -1;
}

void SynthKeyboard::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff08131c));
    g.setColour(juce::Colour(0xff1c3443));
    g.drawRect(getLocalBounds(), 1);

    const int lo = lowestNote();
    const int hi = highestNote();
    const bool focused = hasKeyboardFocus(true);

    g.setColour(focused ? juce::Colour(0xff7ac8ff) : juce::Colour(0xff8aa0ae));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    juce::String title = "MOOG  ·  CH2  ·  ";
    if (keyboardTarget == 1) title = "KEYS  ·  CH4  ·  ";
    else if (keyboardTarget == 2) title = "PROPHET  ·  CH3  ·  ";
    else if (keyboardTarget == 3) title = "DRUMS  ·  CH1  ·  ";
    title = title
        + groove::midiNoteName(lo) + "–" + groove::midiNoteName(hi)
        + (focused ? "  ·  A–; type  ·  [ ] octave" : "  ·  click to type  ·  mouse plays");
    g.drawText(title, getLocalBounds().removeFromTop(22).reduced(10, 0).withTrimmedRight(220),
               juce::Justification::centredLeft);

    auto keys = getLocalBounds().toFloat().withTrimmedTop(22).reduced(8, 6);
    const int whites = 7 * kOctaves + 1;
    const float ww = keys.getWidth() / (float) whites;
    const float blackW = ww * 0.62f;
    const float blackH = keys.getHeight() * 0.58f;

    int whiteIndex = 0;
    for (int n = lo; n <= hi; ++n)
    {
        if (isBlackKey(n))
            continue;
        auto r = juce::Rectangle<float>(keys.getX() + (float) whiteIndex * ww,
                                        keys.getY(), ww - 1.0f, keys.getHeight());
        const bool down = held[(size_t) n];
        g.setColour(down ? juce::Colour(0xffc8e8ff) : juce::Colour(0xffe8eef2));
        g.fillRoundedRectangle(r, 2.0f);
        g.setColour(juce::Colour(0xff1a2a34));
        g.drawRoundedRectangle(r, 2.0f, 1.0f);
        if (n % 12 == 0)
        {
            g.setColour(juce::Colour(0xff4a5a66));
            g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
            g.drawText(groove::midiNoteName(n), r.reduced(2, 4), juce::Justification::centredBottom);
        }
        ++whiteIndex;
    }

    for (int n = lo; n <= hi; ++n)
    {
        if (! isBlackKey(n))
            continue;
        const int wi = whiteCountBefore(n - lo + 1);
        auto r = juce::Rectangle<float>(keys.getX() + (float) wi * ww - blackW * 0.5f,
                                        keys.getY(), blackW, blackH);
        const bool down = held[(size_t) n];
        g.setColour(down ? juce::Colour(0xff7ac8ff) : juce::Colour(0xff12181c));
        g.fillRoundedRectangle(r, 2.0f);
        g.setColour(juce::Colour(0xff2a3a44));
        g.drawRoundedRectangle(r, 2.0f, 1.0f);
    }
}

void SynthKeyboard::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const int n = noteAt(e.getPosition());
    if (n < 0)
        return;
    mouseNote = n;
    const float vel = juce::jlimit(0.2f, 1.0f, 1.0f - (float) e.y / (float) juce::jmax(1, getHeight()));
    noteOn(n, vel);
}

void SynthKeyboard::mouseDrag(const juce::MouseEvent& e)
{
    const int n = noteAt(e.getPosition());
    if (n == mouseNote)
        return;
    if (mouseNote >= 0)
        noteOff(mouseNote);
    mouseNote = n;
    if (n >= 0)
        noteOn(n, 0.85f);
}

void SynthKeyboard::releaseMouseNote()
{
    if (mouseNote >= 0)
    {
        noteOff(mouseNote);
        mouseNote = -1;
    }
}

void SynthKeyboard::mouseUp(const juce::MouseEvent&)
{
    releaseMouseNote();
}

void SynthKeyboard::mouseExit(const juce::MouseEvent&)
{
    releaseMouseNote();
}

int SynthKeyboard::computerKeyToNote(int keyCode) const
{
    struct Map { int key; int offset; };
    static constexpr Map keys[] = {
        { 'A', 0 },  { 'W', 1 },  { 'S', 2 },  { 'E', 3 },  { 'D', 4 },
        { 'F', 5 },  { 'T', 6 },  { 'G', 7 },  { 'Y', 8 },  { 'H', 9 },
        { 'U', 10 }, { 'J', 11 }, { 'K', 12 }, { 'O', 13 }, { 'L', 14 },
        { 'P', 15 }, { ';', 16 }
    };
    const int start = lowestNote() + 12; // second octave of the span
    for (const auto& k : keys)
        if (k.key == keyCode)
            return juce::jlimit(0, 127, start + k.offset);
    return -1;
}

bool SynthKeyboard::keyPressed(const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown())
        return false;
    if (key == juce::KeyPress::spaceKey)
        return false;
    if (key.getTextCharacter() == '[' || key.getKeyCode() == juce::KeyPress::leftKey)
    {
        shiftOctave(-1);
        return true;
    }
    if (key.getTextCharacter() == ']' || key.getKeyCode() == juce::KeyPress::rightKey)
    {
        shiftOctave(1);
        return true;
    }

    const int note = computerKeyToNote(key.getKeyCode());
    if (note < 0)
        return false;
    if (! computerHeld[(size_t) note])
    {
        computerHeld[(size_t) note] = true;
        noteOn(note, 0.9f);
    }
    return true;
}

bool SynthKeyboard::keyStateChanged(bool)
{
    for (int n = 0; n < 128; ++n)
    {
        if (! computerHeld[(size_t) n])
            continue;
        bool stillDown = false;
        static constexpr int codes[] = {
            'A','W','S','E','D','F','T','G','Y','H','U','J','K','O','L','P',';'
        };
        for (int code : codes)
            if (computerKeyToNote(code) == n && juce::KeyPress::isKeyCurrentlyDown(code))
            {
                stillDown = true;
                break;
            }
        if (! stillDown)
            noteOff(n);
    }
    return false;
}

void SynthKeyboard::focusLost(FocusChangeType)
{
    for (int n = 0; n < 128; ++n)
        if (computerHeld[(size_t) n])
            noteOff(n);
    releaseMouseNote();
    repaint();
}
