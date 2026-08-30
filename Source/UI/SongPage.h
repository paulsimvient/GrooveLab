#pragma once
#include <JuceHeader.h>
#include <array>
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
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void refreshFromEngine();
    void setActiveMidiChannel(int channel);
    int sectionIndexAt(juce::Point<int> pos) const;

    std::function<void()> onSongChanged;
    std::function<void(int channel)> onChannelClicked;
    std::function<void(int channel)> onInstrumentUiClicked;

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
    void paintMidiLanes(juce::Graphics&);
    void showPartMenu(int sectionIndex, juce::Point<int> screenPos);
    juce::Rectangle<int> sectionNameArea(int index) const;
    juce::Rectangle<int> sectionResizeHandle(int index) const;
    juce::Rectangle<int> sectionDeleteArea(int index) const;
    int midiLaneAt(juce::Point<int> pos) const;
    void rebuildTakeButtons();
    void expandOrContractBars(int index, bool expand);

    groove::GrooveEngine& engine;
    juce::TextButton followButton { "AUTO WALK" };
    juce::TextButton introButton { "+ INTRO" };
    juce::TextButton verseButton { "+ VERSE" };
    juce::TextButton preButton { "+ PRE" };
    juce::TextButton chorusButton { "+ CHORUS" };
    juce::TextButton bridgeButton { "+ BRIDGE" };
    juce::TextButton fillButton { "+ FILL" };
    juce::TextButton outroButton { "+ OUTRO" };
    juce::ComboBox partBox;
    juce::ComboBox barsBox;
    juce::TextButton barMinus { "−" };
    juce::TextButton barPlus { "+" };
    juce::ComboBox meterBox;
    juce::ComboBox meterTransformBox;
    juce::TextButton duplicateButton { "DUPLICATE" };
    juce::TextButton deleteButton { "DELETE" };
    juce::TextButton recButton { "REC" };
    juce::TextButton quantizeButton { "QUANTIZE" };
    juce::ComboBox quantizeBox;
    static constexpr int kLaneLabelW = 118;
    struct LaneHit : public juce::Component
    {
        std::function<void(bool openUi)> onPick;
        LaneHit()
        {
            setOpaque(false);
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        }
        void mouseDown(const juce::MouseEvent& e) override
        {
            if (onPick)
                onPick(e.x < kLaneLabelW);
        }
    };
    std::array<LaneHit, groove::kMidiLanes> laneHits;
    juce::TextButton keepTakeButton { "KEEP TAKE" };
    juce::TextButton deleteTakeButton { "DELETE TAKE" };
    juce::OwnedArray<juce::TextButton> takeButtons;
    std::array<TrackRow, groove::kTracks> rows;
    juce::Rectangle<int> arrangePanel, lanesPanel, editPanel;
    int activeMidiChannel = 2;
    bool refreshing = false;
    int dragFrom = -1;
    int dragInsert = -1;
    bool dragging = false;
    int resizeIndex = -1;
    int resizeStartBars = 4;
    int resizeTileLeft = 0;
    float resizePxPerBar = 24.0f;
    juce::Point<float> dragGhostPos;
    juce::Point<int> dragOffset;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SongPage)
};
