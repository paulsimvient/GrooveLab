#include "EnsembleView.h"
#include "EnsembleSongBuilder.h"
#include "../../Audio/DrumMidi.h"

namespace groove::ensemble
{
EnsembleView::EnsembleView(GrooveEngine& e)
    : engine(e)
{
    setOpaque(true);
    recordButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffc62828));
    recordButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    recordButton.onClick = [this] { beginRecord(); };
    addAndMakeVisible(recordButton);
    keepButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff10301c));
    keepButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff8ed044));
    keepButton.onClick = [this] { keepArrangement(); };
    addAndMakeVisible(keepButton);
    againButton.onClick = [this] { startOver(); };
    addAndMakeVisible(againButton);
    oneBarButton.setClickingTogglesState(true);
    twoBarButton.setClickingTogglesState(true);
    oneBarButton.setRadioGroupId(71);
    twoBarButton.setRadioGroupId(71);
    oneBarButton.onClick = [this] { setBeatBars(1); };
    twoBarButton.onClick = [this] { setBeatBars(2); };
    addAndMakeVisible(oneBarButton);
    addAndMakeVisible(twoBarButton);
    twoBarButton.setToggleState(true, juce::dontSendNotification);
    hatQuarterButton.setClickingTogglesState(true);
    hatHalfButton.setClickingTogglesState(true);
    hatSixteenthButton.setClickingTogglesState(true);
    hatQuarterButton.setRadioGroupId(72);
    hatHalfButton.setRadioGroupId(72);
    hatSixteenthButton.setRadioGroupId(72);
    hatQuarterButton.onClick = [this] { applyHatRate(HatRate::quarter); };
    hatHalfButton.onClick = [this] { applyHatRate(HatRate::eighth); };
    hatSixteenthButton.onClick = [this] { applyHatRate(HatRate::sixteenth); };
    addAndMakeVisible(hatQuarterButton);
    addAndMakeVisible(hatHalfButton);
    addAndMakeVisible(hatSixteenthButton);
    hatHalfButton.setToggleState(true, juce::dontSendNotification);

    auto setupRotary = [](juce::Slider& s, double min, double max, double step)
    {
        s.setRange(min, max, step);
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 14);
    };
    setupRotary(velocitySlider, 0.0, 1.2, 0.01);
    setupRotary(probabilitySlider, 0.0, 1.0, 0.01);
    velocitySlider.setName("VEL");
    probabilitySlider.setName("PROB");
    ratchetBox.addItem("1x", 1);
    ratchetBox.addItem("2x", 2);
    ratchetBox.addItem("3x", 3);
    ratchetBox.addItem("4x", 4);
    roleBox.addItem("NORMAL", 1);
    roleBox.addItem("ANCHOR", 2);
    roleBox.addItem("GHOST", 3);
    roleBox.addItem("FILL", 4);
    addAndMakeVisible(velocitySlider);
    addAndMakeVisible(probabilitySlider);
    addAndMakeVisible(ratchetBox);
    addAndMakeVisible(roleBox);
    bindInspector();
    startTimerHz(24);
}

EnsembleView::~EnsembleView()
{
    engine.setPerformanceTap(false);
    if (session.hasHost && ! session.committed)
        restoreHost();
}

void EnsembleView::visibilityChanged()
{
    if (isVisible())
    {
        if (session.phase == Phase::ready)
            engine.setPerformanceTap(true);
        refreshFromEngine();
        return;
    }

    engine.setPerformanceTap(false);
    if (session.phase == Phase::recording)
    {
        if (session.hasHost && ! session.committed)
            restoreHost();
        else
        {
            session.phase = Phase::idle;
            session.seed = {};
        }
        return;
    }
}

void EnsembleView::updateChrome()
{
    const bool recording = session.phase == Phase::recording;
    const bool suggested = session.phase == Phase::ready;
    recordButton.setVisible(! recording && ! suggested);
    recordButton.setEnabled(! recording);
    keepButton.setVisible(suggested && ! session.committed);
    againButton.setVisible(recording || suggested);
    againButton.setButtonText(recording ? "CANCEL" : "RECORD AGAIN");
    oneBarButton.setEnabled(! recording);
    twoBarButton.setEnabled(! recording);
    const bool inspect = suggested || recording;
    velocitySlider.setVisible(inspect);
    probabilitySlider.setVisible(inspect);
    ratchetBox.setVisible(inspect);
    roleBox.setVisible(inspect);
    velocitySlider.setEnabled(suggested);
    probabilitySlider.setEnabled(suggested);
    ratchetBox.setEnabled(suggested);
    roleBox.setEnabled(suggested);
    const int steps = beatSteps();
    oneBarButton.setToggleState(steps <= 16, juce::dontSendNotification);
    twoBarButton.setToggleState(steps > 16, juce::dontSendNotification);
    hatQuarterButton.setToggleState(session.hatRate == HatRate::quarter, juce::dontSendNotification);
    hatHalfButton.setToggleState(session.hatRate == HatRate::eighth, juce::dontSendNotification);
    hatSixteenthButton.setToggleState(session.hatRate == HatRate::sixteenth, juce::dontSendNotification);
}

void EnsembleView::refreshFromEngine()
{
    if (session.phase == Phase::recording)
        refreshSeedFromTracks();
    if (session.phase == Phase::ready)
    {
        int n = 0;
        for (int t = 0; t < kTracks; ++t)
            n = juce::jmax(n, loopSteps(t));
        if (n > 16)
            session.bars = 2;
    }
    updateChrome();
    syncInspector();
    repaint();
}

int EnsembleView::loopSteps(int track) const
{
    return juce::jlimit(1, kSteps, engine.state().tracks[(size_t) track].generatorSteps);
}

int EnsembleView::beatSteps() const
{
    int n = session.bars <= 1 ? 16 : 32;
    for (int t = 0; t < kTracks; ++t)
        n = juce::jmax(n, loopSteps(t));
    return juce::jlimit(8, kSteps, n);
}

bool EnsembleView::beatHasHits() const
{
    for (int t = 0; t < kTracks; ++t)
        for (int i = 0; i < loopSteps(t); ++i)
            if (engine.isResolvedHit(t, i))
                return true;
    for (const auto& h : session.seed.kick)
        if (h.on) return true;
    for (const auto& h : session.seed.snare)
        if (h.on) return true;
    for (const auto& h : session.seed.hats)
        if (h.on) return true;
    return false;
}

bool EnsembleView::currentIsVerse() const
{
    const auto& song = engine.state().song;
    if (song.sections.empty())
        return false;
    const int i = juce::jlimit(0, (int) song.sections.size() - 1, song.current);
    return song.sections[(size_t) i].part == SongPart::verse;
}

void EnsembleView::propagateFromVerse()
{
    if (session.phase != Phase::ready || ! currentIsVerse())
        return;
    engine.state().captureLiveToCurrentSection();
    applySeedToArrangement(engine.state().song, engine.state().tracks);
}

void EnsembleView::snapshotHost()
{
    if (session.hasHost)
        return;
    engine.state().captureLiveToCurrentSection();
    session.hostSong = engine.state().song;
    session.hasHost = true;
    session.committed = false;
}

void EnsembleView::restoreHost()
{
    engine.setPerformanceTap(false);
    if (! session.hasHost)
        return;
    engine.adoptSong(session.hostSong);
    session.clear();
}

void EnsembleView::beginRecord()
{
    snapshotHost();
    Song workshop;
    workshop.follow = false;
    workshop.current = 0;
    workshop.sections.push_back(workshopSection(engine.state().meter, session.recordSteps(),
                                                session.hatRate));
    engine.adoptSong(std::move(workshop));
    session.seed = {};
    session.phase = Phase::recording;
    session.captureIndex = 0;
    session.lastSeqStep = engine.currentStep();
    engine.setPerformanceTap(true);
    if (! engine.isPlaying())
        engine.setPlaying(true);
    refreshFromEngine();
}

void EnsembleView::finishRecord()
{
    captureLiveHits();
    engine.setPerformanceTap(false);
    if (! beatHasHits())
        refreshSeedFromTracks();
    if (! beatHasHits())
    {
        session.phase = Phase::idle;
        if (session.hasHost)
            restoreHost();
        refreshFromEngine();
        return;
    }
    applySeedToEngine();
    engine.state().captureLiveToCurrentSection();
    auto built = buildSongFromBeat(engine.state().tracks, engine.state().meter);
    engine.adoptSong(std::move(built));
    engine.setSongFollow(false);
    session.phase = Phase::ready;
    engine.setPerformanceTap(true);
    if (! engine.isPlaying())
        engine.setPlaying(true);
    refreshFromEngine();
}

void EnsembleView::keepArrangement()
{
    engine.state().captureLiveToCurrentSection();
    engine.saveAutosave();
    session.committed = true;
    session.hasHost = false;
    refreshFromEngine();
}

void EnsembleView::startOver()
{
    const bool cancelRecord = session.phase == Phase::recording;
    const int bars = session.bars;
    const auto hatRate = session.hatRate;
    if (session.hasHost && ! session.committed)
        restoreHost();
    else
        session.clear();
    session.bars = bars;
    session.hatRate = hatRate;
    if (cancelRecord)
        refreshFromEngine();
    else
        beginRecord();
}

void EnsembleView::setBeatBars(int bars)
{
    bars = bars <= 1 ? 1 : 2;
    session.bars = bars;
    const int steps = session.recordSteps();
    if (session.phase == Phase::ready)
    {
        engine.state().captureLiveToCurrentSection();
        auto song = engine.state().song;
        setKitLoopLength(song, steps, true);
        const int current = song.current;
        engine.adoptSong(std::move(song));
        if (current >= 0)
        {
            const bool wasPlaying = engine.isPlaying();
            if (wasPlaying)
                engine.setPlaying(false);
            engine.selectSongSection(current);
            if (wasPlaying)
                engine.setPlaying(true);
        }
        propagateFromVerse();
    }
    refreshFromEngine();
}

void EnsembleView::applyHatRate(HatRate rate)
{
    session.hatRate = rate;
    if (session.phase != Phase::recording && session.phase != Phase::ready)
    {
        updateChrome();
        return;
    }

    const int chh = (int) DrumVoice::closedHat;
    const int ohh = (int) DrumVoice::openHat;
    const int n = juce::jmax(beatSteps(), loopSteps(chh));
    const int period = hatStepPeriod(rate);
    for (int s = 0; s < n; ++s)
    {
        const bool open = isBeatFour(s);
        const bool closed = ! open && (s % period) == 0;
        engine.setPulseEnabled(chh, s, closed);
        if (closed)
        {
            engine.setVelocity(chh, s, 0.62f);
            stampSeedHit(session.seed, chh, s, 0.62f);
        }
        else
            clearSeedHit(session.seed, chh, s);
        engine.setPulseEnabled(ohh, s, open);
        if (open)
            engine.setVelocity(ohh, s, 0.88f);
    }

    if (session.phase == Phase::ready)
    {
        if (currentIsVerse())
            propagateFromVerse();
        else
            engine.state().captureLiveToCurrentSection();
    }
    updateChrome();
    repaint();
}

int EnsembleView::trackForPlayedNote(int note, int mappedTrack) const
{
    if (mappedTrack >= 0 && mappedTrack < kTracks)
        return mappedTrack;
    const int pc = ((note % 12) + 12) % 12;
    if (pc == 0 || pc == 1) return (int) DrumVoice::kick;
    if (pc == 2 || pc == 3) return (int) DrumVoice::snare;
    if (pc == 4) return (int) DrumVoice::clap;
    if (pc == 6) return (int) DrumVoice::closedHat;
    if (pc == 10) return (int) DrumVoice::openHat;
    if (pc == 5) return (int) DrumVoice::perc1;
    if (pc == 9) return (int) DrumVoice::perc2;
    if (pc == 11 || pc == 7) return (int) DrumVoice::fx;
    return (int) DrumVoice::closedHat;
}

int EnsembleView::stepAtPoint(juce::Rectangle<int> row, juce::Point<int> pos, int track) const
{
    auto cells = row.withTrimmedLeft(72);
    if (! cells.contains(pos))
        return -1;
    const int n = beatSteps();
    if (n <= 0)
        return -1;
    return juce::jlimit(0, n - 1, (pos.x - cells.getX()) * n / juce::jmax(1, cells.getWidth()));
}

void EnsembleView::writeLiveHit(int track, int step, float velocity)
{
    if (track < 0 || track >= kTracks)
        return;
    const int n = juce::jmax(loopSteps(track), beatSteps());
    step = ((step % n) + n) % n;
    engine.setPulseEnabled(track, step, true);
    engine.setVelocity(track, step, velocity);
    stampSeedHit(session.seed, track, step, velocity);
    if (session.phase == Phase::ready)
        propagateFromVerse();
}

void EnsembleView::toggleStep(int track, int step)
{
    if (track < 0 || track >= kTracks || step < 0)
        return;
    const bool on = engine.isResolvedHit(track, step);
    engine.setPulseEnabled(track, step, ! on);
    if (! on)
    {
        engine.setVelocity(track, step, track == (int) DrumVoice::closedHat ? 0.7f : 1.0f);
        stampSeedHit(session.seed, track, step, 1.0f);
    }
    else
        clearSeedHit(session.seed, track, step);
    engine.selectStep(track, step);
    if (session.phase == Phase::ready)
        propagateFromVerse();
}

void EnsembleView::selectBeatStep(int track, int step, bool turnOnIfEmpty)
{
    if (track < 0 || track >= kTracks || step < 0)
        return;
    engine.selectStep(track, step);
    if (turnOnIfEmpty && ! engine.isResolvedHit(track, step))
    {
        engine.setPulseEnabled(track, step, true);
        engine.setVelocity(track, step, track == (int) DrumVoice::closedHat ? 0.7f : 1.0f);
        stampSeedHit(session.seed, track, step, 1.0f);
        if (session.phase == Phase::ready)
            propagateFromVerse();
    }
    syncInspector();
}

bool EnsembleView::hitTestGrid(juce::Point<int> pos, int& track, int& step) const
{
    for (int t = 0; t < kTracks; ++t)
    {
        const int s = stepAtPoint(trackRows[(size_t) t], pos, t);
        if (s >= 0)
        {
            track = t;
            step = s;
            return true;
        }
    }
    return false;
}

juce::Rectangle<float> EnsembleView::cellRect(juce::Rectangle<int> row, int step, int count) const
{
    auto cells = row.withTrimmedLeft(72);
    const float w = (float) cells.getWidth() / (float) juce::jmax(1, count);
    return { (float) cells.getX() + (float) step * w + 1.5f,
             (float) cells.getY() + 2.0f,
             w - 3.0f, (float) cells.getHeight() - 4.0f };
}

void EnsembleView::bindInspector()
{
    velocitySlider.onValueChange = [this]
    {
        if (refreshing) return;
        engine.setVelocity(engine.state().selectedTrack, engine.state().selectedStep,
                           (float) velocitySlider.getValue());
        commitSelectedToSection();
        repaint();
    };
    probabilitySlider.onValueChange = [this]
    {
        if (refreshing) return;
        engine.setProbability(engine.state().selectedTrack, engine.state().selectedStep,
                              (float) probabilitySlider.getValue());
        commitSelectedToSection();
        repaint();
    };
    ratchetBox.onChange = [this]
    {
        if (refreshing) return;
        engine.setRatchet(engine.state().selectedTrack, engine.state().selectedStep,
                          ratchetBox.getSelectedId());
        commitSelectedToSection();
        repaint();
    };
    roleBox.onChange = [this]
    {
        if (refreshing) return;
        engine.setStepRole(engine.state().selectedTrack, engine.state().selectedStep,
                           (StepRole) (roleBox.getSelectedId() - 1));
        commitSelectedToSection();
        repaint();
    };
}

void EnsembleView::syncInspector()
{
    refreshing = true;
    const auto& st = engine.state();
    const int t = juce::jlimit(0, kTracks - 1, st.selectedTrack);
    const int s = juce::jlimit(0, kSteps - 1, st.selectedStep);
    const auto& step = st.tracks[(size_t) t].steps[(size_t) s];
    velocitySlider.setValue(step.velocity, juce::dontSendNotification);
    probabilitySlider.setValue(step.probability, juce::dontSendNotification);
    ratchetBox.setSelectedId(juce::jlimit(1, 4, step.ratchet), juce::dontSendNotification);
    roleBox.setSelectedId((int) step.role + 1, juce::dontSendNotification);
    refreshing = false;
}

void EnsembleView::commitSelectedToSection()
{
    if (session.phase != Phase::ready)
        return;
    engine.state().captureLiveToCurrentSection();
    propagateFromVerse();
}

void EnsembleView::refreshSeedFromTracks()
{
    for (int track = 0; track < kTracks; ++track)
        for (int i = 0; i < juce::jmin(kSeedSteps, loopSteps(track)); ++i)
            if (engine.isResolvedHit(track, i))
                stampSeedHit(session.seed, track, i,
                             engine.state().tracks[(size_t) track].steps[(size_t) i].velocity);
}

void EnsembleView::applySeedToEngine()
{
    for (int track = 0; track < kTracks; ++track)
        for (int i = 0; i < kSeedSteps; ++i)
            if (auto* slot = seedHitForTrack(session.seed, track, i); slot != nullptr && slot->on)
            {
                engine.setPulseEnabled(track, i, true);
                engine.setVelocity(track, i, slot->velocity);
            }
}

void EnsembleView::captureLiveHits()
{
    std::vector<GrooveEngine::PerformanceEvent> events;
    engine.drainPerformanceTap(events);
    for (const auto& e : events)
        writeLiveHit(trackForPlayedNote(e.note, e.track), e.step, e.velocity);
}

void EnsembleView::timerCallback()
{
    if (session.phase == Phase::recording || session.phase == Phase::ready)
        captureLiveHits();

    if (session.phase == Phase::recording)
    {

        const int step = engine.currentStep();
        if (session.lastSeqStep >= 0 && step != session.lastSeqStep)
        {
            const int loop = loopSteps((int) DrumVoice::kick);
            int delta = (step - session.lastSeqStep + loop) % loop;
            if (delta <= 0)
                delta = 1;
            session.captureIndex += delta;
            session.lastSeqStep = step;
        }
        if (session.captureIndex >= session.recordSteps())
            finishRecord();
        else
            refreshSeedFromTracks();
    }
    updateChrome();
    repaint();
}

juce::Rectangle<int> EnsembleView::sectionTile(int index) const
{
    const auto& sections = engine.state().song.sections;
    if (partsArea.isEmpty() || index < 0 || index >= (int) sections.size())
        return {};
    const int n = juce::jmax(1, (int) sections.size());
    const int w = partsArea.getWidth() / n;
    return { partsArea.getX() + index * w + 4, partsArea.getY(), w - 8, partsArea.getHeight() };
}

void EnsembleView::mouseDown(const juce::MouseEvent& e)
{
    dragTrack = -1;
    dragStep = -1;
    if (session.phase == Phase::recording || session.phase == Phase::ready)
    {
        int track = -1, step = -1;
        if (hitTestGrid(e.getPosition(), track, step))
        {
            if (session.phase == Phase::recording || e.mods.isShiftDown())
                toggleStep(track, step);
            else
                selectBeatStep(track, step, true);
            dragTrack = track;
            dragStep = step;
            refreshSeedFromTracks();
            syncInspector();
            return;
        }
    }
    if (session.phase != Phase::ready)
        return;
    const auto& sections = engine.state().song.sections;
    for (int i = 0; i < (int) sections.size(); ++i)
        if (sectionTile(i).contains(e.getPosition()))
        {
            const bool wasPlaying = engine.isPlaying();
            if (wasPlaying)
                engine.setPlaying(false);
            engine.selectSongSection(i);
            if (wasPlaying)
                engine.setPlaying(true);
            syncInspector();
            return;
        }
}

void EnsembleView::mouseDrag(const juce::MouseEvent& e)
{
    if (dragTrack < 0 || dragStep < 0)
        return;
    if (session.phase != Phase::recording && session.phase != Phase::ready)
        return;
    const auto cell = cellRect(trackRows[(size_t) dragTrack], dragStep, beatSteps());
    if (cell.getHeight() <= 1.0f)
        return;
    const float vel = juce::jlimit(0.08f, 1.2f,
        1.0f - (e.position.y - cell.getY()) / cell.getHeight());
    engine.setVelocity(dragTrack, dragStep, vel);
    stampSeedHit(session.seed, dragTrack, dragStep, vel);
    if (session.phase == Phase::ready)
        engine.state().captureLiveToCurrentSection();
    syncInspector();
    repaint();
}

void EnsembleView::mouseUp(const juce::MouseEvent&)
{
    if (dragTrack >= 0 && session.phase == Phase::ready)
        propagateFromVerse();
    dragTrack = -1;
    dragStep = -1;
}

void EnsembleView::drawHitRow(juce::Graphics& g, juce::Rectangle<int> row,
                              const juce::String& label, int count, int track,
                              juce::Colour hitColour) const
{
    auto name = row.removeFromLeft(72);
    g.setColour(juce::Colour(0xffd5ebf7));
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText(label, name, juce::Justification::centredLeft);
    const int play = engine.currentStepForTrack(track);
    const int loop = loopSteps(track);
    const int selectedTrack = engine.state().selectedTrack;
    const int selectedStep = engine.state().selectedStep;
    const bool recording = session.phase == Phase::recording;
    const float w = (float) row.getWidth() / (float) juce::jmax(1, count);
    for (int i = 0; i < count; ++i)
    {
        auto cell = juce::Rectangle<float>(row.getX() + (float) i * w + 1.5f,
                                           (float) row.getY() + 2.0f,
                                           w - 3.0f, (float) row.getHeight() - 4.0f);
        const bool outside = i >= loop;
        const auto& st = engine.state().tracks[(size_t) track].steps[(size_t) juce::jlimit(0, kSteps - 1, i)];
        bool on = false;
        float vel = st.velocity;
        if (recording)
        {
            if (auto* seed = seedHitForTrack(session.seed, track, i))
                if (seed->on)
                {
                    on = true;
                    vel = juce::jmax(vel, seed->velocity);
                }
        }
        on = on || (! outside && engine.isResolvedHit(track, i));

        g.setColour(outside ? juce::Colour(0xff081018)
                    : ((i % 4) == 0) ? juce::Colour(0xff1a3040)
                                     : juce::Colour(0xff0d1820));
        g.fillRoundedRectangle(cell, 4.0f);

        if (on)
        {
            const float height = juce::jlimit(0.12f, 1.0f, vel);
            auto fill = cell.withTrimmedTop(cell.getHeight() * (1.0f - height));
            const float alpha = juce::jlimit(0.35f, 1.0f, st.probability);
            g.setColour(hitColour.withAlpha(alpha));
            g.fillRoundedRectangle(fill, 4.0f);
            if (st.role == StepRole::ghost)
            {
                g.setColour(juce::Colours::white.withAlpha(0.25f));
                g.drawRoundedRectangle(fill, 4.0f, 1.0f);
            }
            if (st.ratchet > 1)
            {
                g.setColour(juce::Colours::white);
                g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
                g.drawText(juce::String(st.ratchet) + "x", fill.toNearestInt().reduced(2),
                           juce::Justification::bottomLeft);
            }
        }

        const bool selected = (track == selectedTrack && i == selectedStep);
        if (selected || (count > 0 && i == play))
        {
            g.setColour(selected ? juce::Colours::white : juce::Colour(0xffd5ebf7).withAlpha(0.7f));
            g.drawRoundedRectangle(cell, 4.0f, selected ? 2.0f : 1.4f);
        }
        if ((i % 16) == 0 && count > 16)
        {
            g.setColour(juce::Colour(0xff3a5a70));
            g.drawLine(cell.getX() - 1.0f, cell.getY(), cell.getX() - 1.0f, cell.getBottom(), 1.0f);
        }
    }
}

void EnsembleView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff050c12));
    const bool recording = session.phase == Phase::recording;
    const bool suggested = session.phase == Phase::ready;

    auto header = getLocalBounds().reduced(28, 18).removeFromTop(78);
    g.setColour(recording ? juce::Colour(0xffff6a6a) : juce::Colour(0xff2c98e8));
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    if (session.committed)
        g.drawText("Song kept", header.removeFromTop(32), juce::Justification::centredLeft);
    else if (suggested)
        g.drawText("Suggested beats", header.removeFromTop(32), juce::Justification::centredLeft);
    else if (recording)
        g.drawText("Recording your beat", header.removeFromTop(32), juce::Justification::centredLeft);
    else
        g.drawText("Record a beat", header.removeFromTop(32), juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xff8aa0ae));
    g.setFont(juce::FontOptions(15.0f));
    if (session.committed)
        g.drawText("Open SONG to play the parts. RECORD AGAIN makes a new one.",
                   header, juce::Justification::centredLeft);
    else if (suggested)
        g.drawText("Click any cell to add or delete. Hats: 1/4, 1/2, or 16th. Open hat sits on 4.",
                   header, juce::Justification::centredLeft);
    else if (recording)
    {
        const int bar = juce::jlimit(1, session.bars, 1 + session.captureIndex / 16);
        g.drawText("Hats are already in. Play or click any row to add or delete.  Bar " + juce::String(bar)
                   + " of " + juce::String(session.bars),
                   header, juce::Justification::centredLeft);
    }
    else
        g.drawText("Hats start on 1/2 with an open hat on 4. RECORD, then add or delete any hit.",
                   header, juce::Justification::centredLeft);

    const int steps = beatSteps();
    auto grid = gridArea;
    const int rowH = grid.getHeight() / kTracks;
    for (int t = 0; t < kTracks; ++t)
    {
        trackRows[(size_t) t] = (t == kTracks - 1) ? grid : grid.removeFromTop(rowH);
        drawHitRow(g, trackRows[(size_t) t], beatTrackName(t), steps, t, beatTrackColour(t));
    }

    if (recording || suggested)
    {
        auto labels = inspectorArea;
        auto velR = labels.removeFromLeft(88);
        auto probR = labels.removeFromLeft(88);
        auto ratR = labels.removeFromLeft(100);
        auto roleR = labels.removeFromLeft(110);
        g.setColour(juce::Colour(0xff8aa0ae));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText("VEL", velR.removeFromTop(16), juce::Justification::centred);
        g.drawText("PROB", probR.removeFromTop(16), juce::Justification::centred);
        g.drawText("RATCHET", ratR.removeFromTop(16), juce::Justification::centred);
        g.drawText("ROLE", roleR.removeFromTop(16), juce::Justification::centred);
    }

    if (suggested)
    {
        const auto& sections = engine.state().song.sections;
        const int current = engine.state().song.current;
        for (int i = 0; i < (int) sections.size(); ++i)
        {
            const auto tile = sectionTile(i);
            const bool on = (i == current);
            g.setColour(on ? juce::Colour(0xff1a4a62) : juce::Colour(0xff10202c));
            g.fillRoundedRectangle(tile.toFloat(), 6.0f);
            g.setColour(on ? juce::Colour(0xff7ac8ff) : juce::Colour(0xff2c4454));
            g.drawRoundedRectangle(tile.toFloat(), 6.0f, on ? 2.0f : 1.0f);
            auto text = tile.reduced(4, 6);
            g.setColour(on ? juce::Colours::white : juce::Colour(0xffd5ebf7));
            g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
            g.drawText(songPartName(sections[(size_t) i].part),
                       text.removeFromTop(22), juce::Justification::centred);
            g.setColour(juce::Colour(0xff8aa0ae));
            g.setFont(juce::FontOptions(11.0f));
            g.drawText(dynamicLevelName(dynamicLevelForPart(sections[(size_t) i].part)),
                       text, juce::Justification::centred);
        }
    }
}

void EnsembleView::resized()
{
    auto r = getLocalBounds().reduced(28, 18);
    r.removeFromTop(86);
    auto footer = r.removeFromBottom(44);
    r.removeFromBottom(12);
    partsArea = r.removeFromBottom(72);
    r.removeFromBottom(10);
    inspectorArea = r.removeFromBottom(78);
    r.removeFromBottom(10);
    gridArea = r;

    recordButton.setBounds(footer.removeFromLeft(130));
    keepButton.setBounds(footer.removeFromLeft(150));
    footer.removeFromLeft(12);
    againButton.setBounds(footer.removeFromLeft(140));
    footer.removeFromLeft(16);
    oneBarButton.setBounds(footer.removeFromLeft(72));
    footer.removeFromLeft(6);
    twoBarButton.setBounds(footer.removeFromLeft(72));
    footer.removeFromLeft(20);
    hatQuarterButton.setBounds(footer.removeFromLeft(52));
    footer.removeFromLeft(4);
    hatHalfButton.setBounds(footer.removeFromLeft(52));
    footer.removeFromLeft(4);
    hatSixteenthButton.setBounds(footer.removeFromLeft(56));

    auto inspect = inspectorArea;
    inspect.removeFromTop(16);
    velocitySlider.setBounds(inspect.removeFromLeft(88).reduced(4, 0));
    probabilitySlider.setBounds(inspect.removeFromLeft(88).reduced(4, 0));
    inspect.removeFromLeft(8);
    ratchetBox.setBounds(inspect.removeFromLeft(92).removeFromTop(28));
    inspect.removeFromLeft(8);
    roleBox.setBounds(inspect.removeFromLeft(100).removeFromTop(28));
}
}