#pragma once
#include <JuceHeader.h>
#include <functional>
#include "../Audio/GrooveEngine.h"

class SongPage : public juce::Component,
                 private juce::Timer
{
public:
    explicit SongPage(groove::GrooveEngine&);
    ~SongPage() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void refreshFromEngine();

    std::function<void()> onSongChanged;

private:
    struct TrackRow
    {
        juce::Label name;
        juce::ComboBox steps, pulses, rotate, division;
        juce::Slider probability, velocity;
    };

    void timerCallback() override;
    juce::Rectangle<int> sectionTile(int index) const;
    int insertIndexForX(int x) const;
    void addPart(groove::SongPart);
    void fillRhythmBoxes(int track, const groove::TrackShape&);
    void commitTrackRow(int track);
    void paintSectionTile(juce::Graphics&, int index, juce::Rectangle<float> tile,
                          bool selected, bool playing, float alpha);

    groove::GrooveEngine& engine;
    juce::TextButton followButton { "FOLLOW SONG" };
    juce::TextButton introButton { "+ INTRO" };
    juce::TextButton verseButton { "+ VERSE" };
    juce::TextButton preButton { "+ PRE" };
    juce::TextButton chorusButton { "+ CHORUS" };
    juce::TextButton bridgeButton { "+ BRIDGE" };
    juce::TextButton fillButton { "+ FILL" };
    juce::TextButton outroButton { "+ OUTRO" };
    juce::ComboBox barsBox;
    juce::ComboBox meterBox;
    juce::TextButton duplicateButton { "DUPLICATE" };
    juce::TextButton deleteButton { "DELETE" };
    std::array<TrackRow, groove::kTracks> rows;
    juce::Rectangle<int> arrangePanel, editPanel;
    bool refreshing = false;
    int dragFrom = -1;
    int dragInsert = -1;
    bool dragging = false;
    juce::Point<float> dragGhostPos;
    juce::Point<int> dragOffset;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SongPage)
};
