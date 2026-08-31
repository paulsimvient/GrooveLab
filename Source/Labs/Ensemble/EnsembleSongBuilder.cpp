#include "EnsembleSongBuilder.h"
#include "../../Sequencer/Sequencer.h"
#include <algorithm>
#include <vector>

namespace groove::ensemble
{
static int lengthOf(const TrackShape& sh)
{
    return juce::jlimit(1, kSteps, sh.generatorSteps);
}

static void bakeResolved(SongSection& section, const std::array<Track, kTracks>& seed)
{
    copyPatternFromTracks(section, seed);
    for (int t = 0; t < kTracks; ++t)
    {
        const int n = juce::jlimit(1, kSteps, seed[(size_t) t].generatorSteps);
        for (int s = 0; s < n; ++s)
        {
            auto& st = section.steps[(size_t) t][(size_t) s];
            if (Sequencer::resolvedStepActive(seed[(size_t) t], s))
            {
                st = seed[(size_t) t].steps[(size_t) s];
                st.overrideMode = StepOverrideMode::forceOn;
                st.active = true;
                if (st.velocity <= 0.0f)
                    st.velocity = 0.85f;
                if (st.probability <= 0.0f)
                    st.probability = 1.0f;
                st.ratchet = juce::jlimit(1, 4, st.ratchet);
            }
            else
            {
                st.overrideMode = StepOverrideMode::forceOff;
                st.active = false;
            }
        }
    }
}

static void scaleVelocity(SongSection& section, int track, float gain)
{
    const int n = lengthOf(section.shapes[(size_t) track]);
    for (int s = 0; s < n; ++s)
    {
        auto& st = section.steps[(size_t) track][(size_t) s];
        if (st.overrideMode == StepOverrideMode::forceOn)
            st.velocity = juce::jlimit(0.08f, 1.2f, st.velocity * gain);
    }
}

static void scaleProbability(SongSection& section, int track, float gain)
{
    const int n = lengthOf(section.shapes[(size_t) track]);
    for (int s = 0; s < n; ++s)
    {
        auto& st = section.steps[(size_t) track][(size_t) s];
        if (st.overrideMode == StepOverrideMode::forceOn)
            st.probability = juce::jlimit(0.08f, 1.0f, st.probability * gain);
    }
}

static void addHit(SongSection& section, int track, int step, float velocity,
                   float probability = 1.0f, int ratchet = 1, StepRole role = StepRole::normal)
{
    const int n = lengthOf(section.shapes[(size_t) track]);
    if (n <= 0)
        return;
    step = ((step % n) + n) % n;
    auto& st = section.steps[(size_t) track][(size_t) step];
    if (st.overrideMode == StepOverrideMode::forceOn)
    {
        st.velocity = juce::jmax(st.velocity, velocity);
        st.probability = juce::jmax(st.probability, probability);
        st.ratchet = juce::jmax(st.ratchet, ratchet);
    }
    else
    {
        st.overrideMode = StepOverrideMode::forceOn;
        st.active = true;
        st.velocity = juce::jlimit(0.08f, 1.2f, velocity);
        st.probability = juce::jlimit(0.08f, 1.0f, probability);
        st.ratchet = juce::jlimit(1, 4, ratchet);
        st.role = role;
    }
}

static void dropQuietest(SongSection& section, int track, float dropFraction)
{
    struct Hit { int step; float vel; };
    std::vector<Hit> hits;
    const int n = lengthOf(section.shapes[(size_t) track]);
    for (int s = 0; s < n; ++s)
    {
        const auto& st = section.steps[(size_t) track][(size_t) s];
        if (st.overrideMode == StepOverrideMode::forceOn)
            hits.push_back({ s, st.velocity });
    }
    if (hits.size() <= 1)
        return;
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.vel < b.vel; });
    const int drop = juce::jlimit(0, (int) hits.size() - 1, (int) std::floor((float) hits.size() * dropFraction));
    for (int i = 0; i < drop; ++i)
    {
        auto& st = section.steps[(size_t) track][(size_t) hits[(size_t) i].step];
        st.overrideMode = StepOverrideMode::forceOff;
        st.active = false;
    }
}

static void applyDynamic(SongSection& section, DynamicLevel level)
{
    const int kick = (int) DrumVoice::kick;
    const int snare = (int) DrumVoice::snare;
    const int hats = (int) DrumVoice::closedHat;
    const int nHats = lengthOf(section.shapes[(size_t) hats]);

    auto eachTrack = [&](auto fn)
    {
        for (int t = 0; t < kTracks; ++t)
            fn(t);
    };

    switch (level)
    {
        case DynamicLevel::quiet:
            eachTrack([&](int t)
            {
                dropQuietest(section, t, t == hats ? 0.25f : 0.4f);
                scaleVelocity(section, t, t == hats ? 0.5f : 0.7f);
                scaleProbability(section, t, t == hats ? 0.55f : 0.74f);
            });
            break;

        case DynamicLevel::asPlayed:
            break;

        case DynamicLevel::lifted:
            eachTrack([&](int t) { scaleVelocity(section, t, 1.08f); });
            for (int s = 0; s < nHats; ++s)
                if ((s % 4) == 3)
                    addHit(section, hats, s, 0.38f, 0.7f, 1, StepRole::ghost);
            break;

        case DynamicLevel::peak:
            eachTrack([&](int t) { scaleVelocity(section, t, t == hats ? 1.08f : 1.16f); });
            for (int s = 0; s < nHats; ++s)
                if ((s % 2) == 1)
                    addHit(section, hats, s, 0.42f, 0.85f, 1, StepRole::ghost);
            break;

        case DynamicLevel::dropped:
            eachTrack([&](int t)
            {
                if (t != kick)
                    dropQuietest(section, t, t == hats ? 0.5f : 0.3f);
                scaleVelocity(section, t, t == hats ? 0.7f : 0.84f);
            });
            scaleProbability(section, hats, 0.58f);
            scaleProbability(section, snare, 0.85f);
            break;
    }
}

static SongSection suggestPart(const std::array<Track, kTracks>& seed, SongPart part,
                               int bars, Meter meter, DynamicLevel level)
{
    SongSection section;
    section.part = part;
    section.bars = bars;
    section.meter = meter;
    bakeResolved(section, seed);
    applyDynamic(section, level);
    return section;
}

static void writeHatStep(Step& st, bool on, float velocity)
{
    st.overrideMode = on ? StepOverrideMode::forceOn : StepOverrideMode::forceOff;
    st.active = on;
    if (on)
    {
        st.velocity = velocity;
        if (st.probability <= 0.0f)
            st.probability = 1.0f;
        st.ratchet = juce::jmax(1, st.ratchet);
    }
}

void applyHatPattern(SongSection& section, HatRate rate)
{
    const int period = hatStepPeriod(rate);
    const int chh = (int) DrumVoice::closedHat;
    const int ohh = (int) DrumVoice::openHat;
    const int n = juce::jlimit(1, kSteps, section.shapes[(size_t) chh].generatorSteps);
    section.shapes[(size_t) ohh].generatorSteps = n;
    int closedHits = 0, openHits = 0;
    for (int s = 0; s < n; ++s)
    {
        const bool open = isBeatFour(s);
        const bool closed = ! open && (s % period) == 0;
        writeHatStep(section.steps[(size_t) chh][(size_t) s], closed, 0.62f);
        writeHatStep(section.steps[(size_t) ohh][(size_t) s], open, 0.88f);
        if (closed) ++closedHits;
        if (open) ++openHits;
    }
    section.shapes[(size_t) chh].pulses = closedHits;
    section.shapes[(size_t) ohh].pulses = openHits;
    section.shapes[(size_t) chh].rotate = 0;
    section.shapes[(size_t) ohh].rotate = 0;
}

void applyHatPatternToTracks(std::array<Track, kTracks>& tracks, HatRate rate)
{
    const int period = hatStepPeriod(rate);
    const int chh = (int) DrumVoice::closedHat;
    const int ohh = (int) DrumVoice::openHat;
    const int n = juce::jlimit(1, kSteps, tracks[(size_t) chh].generatorSteps);
    tracks[(size_t) ohh].generatorSteps = n;
    int closedHits = 0, openHits = 0;
    for (int s = 0; s < n; ++s)
    {
        const bool open = isBeatFour(s);
        const bool closed = ! open && (s % period) == 0;
        writeHatStep(tracks[(size_t) chh].steps[(size_t) s], closed, 0.62f);
        writeHatStep(tracks[(size_t) ohh].steps[(size_t) s], open, 0.88f);
        if (closed) ++closedHits;
        if (open) ++openHits;
    }
    tracks[(size_t) chh].pulses = closedHits;
    tracks[(size_t) ohh].pulses = openHits;
    tracks[(size_t) chh].rotate = 0;
    tracks[(size_t) ohh].rotate = 0;
}

SongSection workshopSection(Meter meter, int steps, HatRate hatRate)
{
    steps = juce::jlimit(8, kSteps, steps);
    SongSection section;
    section.part = SongPart::verse;
    section.bars = juce::jmax(1, steps / 16);
    section.meter = meter;
    for (int t = 0; t < kTracks; ++t)
    {
        section.shapes[(size_t) t].generatorSteps = steps;
        section.shapes[(size_t) t].pulses = 0;
        for (auto& st : section.steps[(size_t) t])
        {
            st.overrideMode = StepOverrideMode::forceOff;
            st.active = false;
        }
    }
    applyHatPattern(section, hatRate);
    return section;
}

static void setKitLoopLength(SongSection& section, int newLen, bool tile)
{
    newLen = juce::jlimit(1, kSteps, newLen);
    for (int t = 0; t < kTracks; ++t)
    {
        Track tr;
        const auto& sh = section.shapes[(size_t) t];
        tr.generatorSteps = juce::jlimit(1, kSteps, sh.generatorSteps);
        tr.pulses = juce::jlimit(0, tr.generatorSteps, sh.pulses);
        tr.rotate = sh.rotate;
        tr.steps = section.steps[(size_t) t];
        const int old = juce::jmax(1, tr.generatorSteps);

        std::array<Step, kSteps> out {};
        int hits = 0;
        for (int i = 0; i < newLen; ++i)
        {
            const bool have = tile || i < old;
            const int src = have ? (i % old) : 0;
            const bool on = have && Sequencer::resolvedStepActive(tr, src);
            auto st = tr.steps[(size_t) src];
            st.overrideMode = on ? StepOverrideMode::forceOn : StepOverrideMode::forceOff;
            st.active = on;
            if (on)
            {
                if (st.velocity <= 0.0f)
                    st.velocity = 0.85f;
                ++hits;
            }
            out[(size_t) i] = st;
        }
        section.steps[(size_t) t] = out;
        section.shapes[(size_t) t].generatorSteps = newLen;
        section.shapes[(size_t) t].pulses = hits;
        section.shapes[(size_t) t].rotate = 0;
    }
}

void setKitLoopLength(Song& song, int newLen, bool tile)
{
    for (auto& section : song.sections)
        setKitLoopLength(section, newLen, tile);
}

void applySeedToArrangement(Song& song, const std::array<Track, kTracks>& seed)
{
    for (auto& section : song.sections)
    {
        if (section.part == SongPart::verse)
            continue;
        const auto part = section.part;
        const int bars = section.bars;
        const auto meter = section.meter;
        section = suggestPart(seed, part, bars, meter, dynamicLevelForPart(part));
        section.part = part;
        section.bars = bars;
        section.meter = meter;
    }
}

Song buildSongFromBeat(const std::array<Track, kTracks>& seed, Meter meter)
{
    Song song;
    song.follow = false;
    song.current = 1; // verse = the take
    song.sections.push_back(suggestPart(seed, SongPart::intro, 4, meter, DynamicLevel::quiet));
    song.sections.push_back(suggestPart(seed, SongPart::verse, 4, meter, DynamicLevel::asPlayed));
    song.sections.push_back(suggestPart(seed, SongPart::prechorus, 4, meter, DynamicLevel::lifted));
    song.sections.push_back(suggestPart(seed, SongPart::chorus, 8, meter, DynamicLevel::peak));
    song.sections.push_back(suggestPart(seed, SongPart::bridge, 4, meter, DynamicLevel::dropped));
    song.sections.push_back(suggestPart(seed, SongPart::outro, 4, meter, DynamicLevel::quiet));
    return song;
}
}