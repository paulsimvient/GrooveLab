#pragma once
#include "../Core/GrooveTypes.h"
#include <cmath>

namespace groove
{
// UJAM Beatmaker / Virtual Drummer kit keys. Style/song-part keys start at C3 (60)
// and must not be used for sequencer hits.
constexpr int kUjamKitLow  = 36; // C1 BD 1
constexpr int kUjamKitHigh = 51; // D#2 HH 4
constexpr int kFirstParamCc = 20; // pitch, decay, transient, noise, filter, drive, space, blend
constexpr int kCcVolume     = 7;
constexpr int kCcExpression = 11;

inline int toMidi7(float unit)
{
    return juce::jlimit(0, 127, (int) std::round(unit * 127.0f));
}

inline int midiNoteForTrack(int track)
{
    static constexpr int notes[kTracks] = { 36, 38, 39, 42, 46, 41, 45, 47 };
    return notes[juce::jlimit(0, kTracks - 1, track)];
}

inline const char* midiNoteNameForTrack(int track)
{
    static constexpr const char* names[kTracks] = {
        "C1", "D1", "D#1", "F#1", "A#1", "F1", "A1", "B1"
    };
    return names[juce::jlimit(0, kTracks - 1, track)];
}

inline juce::String midiNoteName(int note)
{
    static constexpr const char* names[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    note = juce::jlimit(0, 127, note);
    return juce::String(names[note % 12]) + juce::String(note / 12 - 1);
}

inline juce::String ujamKitName(int note)
{
    static constexpr const char* names[] = {
        "BD1", "BD2", "SN1", "CLAP", "SN2", "TOM1", "HH1", "TOM2",
        "HH2", "TOM3", "HH3", "FX1", "FX2", "CYM1", "CYM2", "HH4"
    };
    if (note < kUjamKitLow || note > kUjamKitHigh)
        return midiNoteName(note);
    return names[note - kUjamKitLow];
}

inline bool isUjamKitNote(int note)
{
    return note >= kUjamKitLow && note <= kUjamKitHigh;
}

// Map a UJAM kit key onto Groove Lab's 8 tracks.
inline int trackIndexForUjamNote(int note)
{
    if (! isUjamKitNote(note))
        return -1;
    switch (note)
    {
        case 36: case 37: return 0; // BD1 / BD2 → KICK
        case 38: case 40: return 1; // SN1 / SN2 → SNARE
        case 39:          return 2; // CLAP
        case 42: case 44: return 3; // HH1 / HH2 → CHH
        case 46: case 51: return 4; // HH3 / HH4 → OHH
        case 41: case 43: return 5; // TOM1 / TOM2 → PERC1
        case 45:          return 6; // TOM3 → PERC2
        default:          return 7; // FX / cymbals
    }
}

inline bool looksLikeMidiFile(const juce::File& file)
{
    const auto ext = file.getFileExtension().toLowerCase();
    if (ext == ".mid" || ext == ".midi" || ext == ".smf")
        return true;
    if (! file.existsAsFile())
        return false;
    if (file.getFileName().containsIgnoreCase("midi"))
        return true;
    return file.getSize() > 4 && file.getSize() < 512 * 1024 && ext.isEmpty();
}

inline float paramValue(const VoiceParams& p, Param param)
{
    switch (param)
    {
        case Param::pitch:     return p.pitchHz;
        case Param::decay:     return p.decayMs;
        case Param::transient: return p.transient;
        case Param::noise:     return p.noise;
        case Param::filter:    return p.filter;
        case Param::drive:     return p.drive;
        case Param::space:     return p.space;
        case Param::blend:     return p.blend;
        default:               return 0.0f;
    }
}

inline int paramToCcValue(Param p, float value)
{
    switch (p)
    {
        case Param::pitch: return juce::jlimit(0, 127, (int) std::round((value - 30.0f) / 1570.0f * 127.0f));
        case Param::decay: return juce::jlimit(0, 127, (int) std::round((value - 20.0f) / 1780.0f * 127.0f));
        default:           return juce::jlimit(0, 127, (int) std::round(value * 127.0f));
    }
}

inline int ccForParam(Param p) { return kFirstParamCc + (int) p; }

inline void configureMidiNoteSlider(juce::Slider& s)
{
    s.setRange((double) kUjamKitLow, (double) kUjamKitHigh, 1.0);
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 52, 16);
    s.textFromValueFunction = [](double v) { return ujamKitName((int) std::round(v)); };
    s.valueFromTextFunction = [](const juce::String&) { return (double) kUjamKitLow; };
}

inline void fillUjamKitCombo(juce::ComboBox& box)
{
    box.clear(juce::dontSendNotification);
    for (int n = kUjamKitLow; n <= kUjamKitHigh; ++n)
        box.addItem(ujamKitName(n) + "  " + midiNoteName(n), n);
}
}
