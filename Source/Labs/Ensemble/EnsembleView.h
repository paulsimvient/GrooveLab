#pragma once
#include <JuceHeader.h>
#include <array>
#include "../../Audio/GrooveEngine.h"
#include "EnsembleSession.h"

namespace groove::ensemble
{
class EnsembleView : public juce::Component,
                     private juce::Timer
{
public:
    explicit EnsembleView(GrooveEngine&);
    ~EnsembleView() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void refreshFromEngine();

private:
    void timerCallback() override;
    void visibilityChanged() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void snapshotHost();
    void restoreHost();
    void beginRecord();
    void finishRecord();
    void keepArrangement();
    void startOver();
    void setBeatBars(int bars);
    void applyHatRate(HatRate rate);
    int beatSteps() const;
    bool currentIsVerse() const;
    void propagateFromVerse();
    int trackForPlayedNote(int note, int mappedTrack) const;
    void writeLiveHit(int track, int step, float velocity);
    void toggleStep(int track, int step);
    void selectBeatStep(int track, int step, bool turnOnIfEmpty);
    bool hitTestGrid(juce::Point<int> pos, int& track, int& step) const;
    int stepAtPoint(juce::Rectangle<int> row, juce::Point<int> pos, int track) const;
    juce::Rectangle<float> cellRect(juce::Rectangle<int> row, int step, int count) const;
    void refreshSeedFromTracks();
    void applySeedToEngine();
    void captureLiveHits();
    void bindInspector();
    void syncInspector();
    void commitSelectedToSection();
    bool beatHasHits() const;
    int loopSteps(int track) const;
    void updateChrome();
    juce::Rectangle<int> sectionTile(int index) const;
    void drawHitRow(juce::Graphics&, juce::Rectangle<int> row,
                    const juce::String& label, int count, int track,
                    juce::Colour hitColour) const;

    GrooveEngine& engine;
    EnsembleSession session;
    juce::TextButton recordButton { "RECORD" };
    juce::TextButton keepButton { "USE THIS SONG" };
    juce::TextButton againButton { "RECORD AGAIN" };
    juce::TextButton oneBarButton { "1 BAR" };
    juce::TextButton twoBarButton { "2 BAR" };
    juce::TextButton hatQuarterButton { "1/4" };
    juce::TextButton hatHalfButton { "1/2" };
    juce::TextButton hatSixteenthButton { "16th" };
    juce::Slider velocitySlider;
    juce::Slider probabilitySlider;
    juce::ComboBox ratchetBox;
    juce::ComboBox roleBox;
    juce::Rectangle<int> partsArea, gridArea, inspectorArea;
    std::array<juce::Rectangle<int>, kTracks> trackRows {};
    bool refreshing = false;
    int dragTrack = -1;
    int dragStep = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnsembleView)
};
}
