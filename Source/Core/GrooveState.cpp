#include "GrooveState.h"
#include "../Audio/DrumMidi.h"

namespace groove
{
static VoiceParams defaultsFor(int i)
{
    switch (i)
    {
        case 0: return {52.0f, 430.0f, 0.46f, 0.03f, 0.58f, 0.10f, 0.04f, 0.18f};
        case 1: return {185.0f, 720.0f, 0.78f, 0.80f, 0.72f, 0.12f, 0.16f, 0.62f};
        case 2: return {320.0f, 175.0f, 0.90f, 0.92f, 0.76f, 0.08f, 0.14f, 0.78f};
        case 3: return {690.0f, 72.0f, 0.62f, 0.96f, 0.94f, 0.04f, 0.05f, 0.90f};
        case 4: return {520.0f, 380.0f, 0.55f, 0.97f, 0.90f, 0.04f, 0.18f, 0.92f};
        case 5: return {245.0f, 220.0f, 0.42f, 0.28f, 0.74f, 0.11f, 0.08f, 0.38f};
        case 6: return {385.0f, 265.0f, 0.36f, 0.22f, 0.80f, 0.13f, 0.10f, 0.32f};
        default:return {610.0f, 520.0f, 0.75f, 0.52f, 0.68f, 0.24f, 0.35f, 0.58f};
    }
}

juce::var stepToVar(const Step& st)
{
    auto* so = new juce::DynamicObject();
    so->setProperty("active", st.active);
    so->setProperty("overrideMode", (int) st.overrideMode);
    so->setProperty("velocity", st.velocity);
    so->setProperty("probability", st.probability);
    so->setProperty("ratchet", st.ratchet);
    so->setProperty("role", (int) st.role);
    if (st.midiNote.has_value())
        so->setProperty("midiNote", *st.midiNote);
    auto* lo = new juce::DynamicObject();
    for (int pi = 0; pi < paramCount; ++pi)
        if (st.locks[(size_t) pi].has_value())
            lo->setProperty(paramName((Param) pi), *st.locks[(size_t) pi]);
    so->setProperty("locks", juce::var(lo));
    return juce::var(so);
}

juce::Array<juce::var> stepsToVar(const std::array<Step, kSteps>& steps)
{
    juce::Array<juce::var> stepArray;
    for (int s = 0; s < kSteps; ++s)
        stepArray.add(stepToVar(steps[(size_t) s]));
    return stepArray;
}

void stepFromVar(Step& st, juce::DynamicObject* so, bool migrateActive)
{
    if (so == nullptr) return;
    st.active = (bool) so->getProperty("active");
    auto overrideVar = so->getProperty("overrideMode");
    if (! overrideVar.isVoid())
        st.overrideMode = (StepOverrideMode) juce::jlimit(0, 2, (int) overrideVar);
    else if (migrateActive && st.active)
        st.overrideMode = StepOverrideMode::forceOn;
    st.velocity = (float) so->getProperty("velocity");
    st.probability = (float) so->getProperty("probability");
    st.ratchet = juce::jmax(1, (int) so->getProperty("ratchet"));
    st.role = (StepRole) juce::jlimit(0, 3, (int) so->getProperty("role"));
    auto noteVar = so->getProperty("midiNote");
    if (! noteVar.isVoid() && isValidMidiNote((int) noteVar))
        st.midiNote = (int) noteVar;
    else
        st.midiNote.reset();
    auto* lo = so->getProperty("locks").getDynamicObject();
    if (lo != nullptr)
    {
        for (int pi = 0; pi < paramCount; ++pi)
        {
            auto value = lo->getProperty(paramName((Param) pi));
            if (! value.isVoid())
                st.locks[(size_t) pi] = (float) value;
        }
    }
}

void stepsFromVar(std::array<Step, kSteps>& steps, const juce::var& stepsVar, bool migrateActive)
{
    if (! stepsVar.isArray()) return;
    const int n = juce::jmin(kSteps, stepsVar.size());
    for (int s = 0; s < n; ++s)
        stepFromVar(steps[(size_t) s], stepsVar[s].getDynamicObject(), migrateActive);
}

juce::var shapeToVar(const TrackShape& sh)
{
    auto* sho = new juce::DynamicObject();
    sho->setProperty("generatorSteps", sh.generatorSteps);
    sho->setProperty("pulses", sh.pulses);
    sho->setProperty("rotate", sh.rotate);
    sho->setProperty("division", sh.division);
    sho->setProperty("probability", sh.probability);
    sho->setProperty("velocity", sh.velocity);
    return juce::var(sho);
}

void shapeFromVar(TrackShape& sh, juce::DynamicObject* sho)
{
    if (sho == nullptr) return;
    auto readI = [sho](const char* k, int fb)
    {
        auto v = sho->getProperty(k);
        return v.isVoid() ? fb : (int) v;
    };
    auto readF = [sho](const char* k, float fb)
    {
        auto v = sho->getProperty(k);
        return v.isVoid() ? fb : (float) v;
    };
    sh.generatorSteps = juce::jlimit(1, kSteps, readI("generatorSteps", sh.generatorSteps));
    sh.pulses = juce::jlimit(0, sh.generatorSteps, readI("pulses", sh.pulses));
    sh.rotate = readI("rotate", sh.rotate);
    sh.division = readF("division", sh.division);
    sh.probability = juce::jlimit(0.0f, 1.0f, readF("probability", sh.probability));
    sh.velocity = juce::jlimit(0.0f, 1.2f, readF("velocity", sh.velocity));
}

juce::var lanesToVar(const std::array<MidiLane, kMidiLanes>& lanes)
{
    juce::Array<juce::var> arr;
    for (int i = 0; i < kMidiLanes; ++i)
    {
        const auto& lane = lanes[(size_t) i];
        auto* o = new juce::DynamicObject();
        o->setProperty("channel", lane.channel);
        o->setProperty("name", lane.name);
        juce::Array<juce::var> notes;
        for (const auto& n : lane.notes)
        {
            auto* no = new juce::DynamicObject();
            no->setProperty("step", n.step);
            no->setProperty("note", n.note);
            no->setProperty("vel", n.velocity);
            no->setProperty("len", juce::jmax(1, n.lengthSteps));
            notes.add(juce::var(no));
        }
        o->setProperty("notes", notes);
        juce::Array<juce::var> patches;
        for (const auto& p : lane.patches)
        {
            auto* po = new juce::DynamicObject();
            po->setProperty("step", p.step);
            po->setProperty("name", p.name);
            po->setProperty("kit", p.kitIndex);
            patches.add(juce::var(po));
        }
        o->setProperty("patches", patches);
        juce::Array<juce::var> ccs;
        for (const auto& c : lane.ccs)
        {
            auto* co = new juce::DynamicObject();
            co->setProperty("step", c.step);
            co->setProperty("cc", c.number);
            co->setProperty("val", c.value);
            ccs.add(juce::var(co));
        }
        o->setProperty("ccs", ccs);
        juce::Array<juce::var> extras;
        for (const auto& e : lane.extras)
        {
            auto* eo = new juce::DynamicObject();
            eo->setProperty("step", e.step);
            eo->setProperty("type", e.type);
            eo->setProperty("d1", e.data1);
            eo->setProperty("d2", e.data2);
            extras.add(juce::var(eo));
        }
        o->setProperty("extras", extras);
        arr.add(juce::var(o));
    }
    return juce::var(arr);
}

void lanesFromVar(std::array<MidiLane, kMidiLanes>& lanes, const juce::var& v)
{
    lanes = makeDefaultMidiLanes();
    if (! v.isArray())
        return;
    for (int i = 0; i < juce::jmin(kMidiLanes, v.size()); ++i)
    {
        auto* o = v[i].getDynamicObject();
        if (o == nullptr)
            continue;
        auto& lane = lanes[(size_t) i];
        if (! o->getProperty("channel").isVoid())
            lane.channel = juce::jlimit(1, 16, (int) o->getProperty("channel"));
        if (o->getProperty("name").toString().isNotEmpty())
            lane.name = o->getProperty("name").toString();
        if (auto* notes = o->getProperty("notes").getArray())
        {
            for (const auto& item : *notes)
            {
                auto* no = item.getDynamicObject();
                if (no == nullptr) continue;
                MidiLaneNote n;
                n.step = juce::jlimit(0, kSteps - 1, (int) no->getProperty("step"));
                n.note = juce::jlimit(0, 127, (int) no->getProperty("note"));
                n.velocity = juce::jlimit(0.05f, 1.2f, (float) no->getProperty("vel"));
                n.lengthSteps = juce::jmax(1, no->getProperty("len").isVoid()
                    ? 1 : (int) no->getProperty("len"));
                lane.notes.push_back(n);
            }
        }
        if (auto* patches = o->getProperty("patches").getArray())
        {
            for (const auto& item : *patches)
            {
                auto* po = item.getDynamicObject();
                if (po == nullptr) continue;
                MidiLanePatch p;
                p.step = juce::jlimit(0, kSteps - 1, (int) po->getProperty("step"));
                p.name = po->getProperty("name").toString();
                p.kitIndex = juce::jmax(0, (int) po->getProperty("kit"));
                lane.patches.push_back(p);
            }
        }
        if (auto* ccs = o->getProperty("ccs").getArray())
        {
            for (const auto& item : *ccs)
            {
                auto* co = item.getDynamicObject();
                if (co == nullptr) continue;
                MidiLaneCc c;
                c.step = juce::jlimit(0, kSteps - 1, (int) co->getProperty("step"));
                c.number = juce::jlimit(0, 127, (int) co->getProperty("cc"));
                c.value = juce::jlimit(0, 127, (int) co->getProperty("val"));
                lane.ccs.push_back(c);
            }
        }
        if (auto* extras = o->getProperty("extras").getArray())
        {
            for (const auto& item : *extras)
            {
                auto* eo = item.getDynamicObject();
                if (eo == nullptr) continue;
                MidiLaneExtra e;
                e.step = juce::jlimit(0, kSteps - 1, (int) eo->getProperty("step"));
                e.type = juce::jlimit(0, 4, (int) eo->getProperty("type"));
                e.data1 = (int) eo->getProperty("d1");
                e.data2 = (int) eo->getProperty("d2");
                if (e.type > 0)
                    lane.extras.push_back(e);
            }
        }
    }
}

juce::var takeToVar(const PatternTake& take)
{
    auto* to = new juce::DynamicObject();
    to->setProperty("label", take.label);
    juce::Array<juce::var> shapes;
    juce::Array<juce::var> tracksSteps;
    for (int t = 0; t < kTracks; ++t)
    {
        shapes.add(shapeToVar(take.shapes[(size_t) t]));
        tracksSteps.add(stepsToVar(take.steps[(size_t) t]));
    }
    to->setProperty("shapes", shapes);
    to->setProperty("steps", tracksSteps);
    to->setProperty("midiLanes", lanesToVar(take.midiLanes));
    return juce::var(to);
}

PatternTake takeFromVar(const juce::var& v)
{
    PatternTake take;
    auto* to = v.getDynamicObject();
    if (to == nullptr) return take;
    take.label = to->getProperty("label").toString();
    auto shapesVar = to->getProperty("shapes");
    auto stepsVar = to->getProperty("steps");
    if (shapesVar.isArray())
        for (int t = 0; t < juce::jmin(kTracks, shapesVar.size()); ++t)
            shapeFromVar(take.shapes[(size_t) t], shapesVar[t].getDynamicObject());
    if (stepsVar.isArray())
        for (int t = 0; t < juce::jmin(kTracks, stepsVar.size()); ++t)
            stepsFromVar(take.steps[(size_t) t], stepsVar[t], false);
    lanesFromVar(take.midiLanes, to->getProperty("midiLanes"));
    return take;
}

GrooveState::GrooveState()
{
    // Start as a blank musical canvas.  Instrument defaults stay initialized so
    // every voice is immediately playable, but no rhythm, MIDI, arrangement,
    // take, or parameter-lock data is pre-populated.
    for (int i = 0; i < kTracks; ++i)
    {
        tracks[i].base = defaultsFor(i);
        tracks[i].rhythmMode = RhythmMode::step;
        tracks[i].generatorSteps = 16;
        tracks[i].pulses = 0;
        tracks[i].rotate = 0;
        tracks[i].division = 1.0f;
        tracks[i].probability = 1.0f;
        tracks[i].velocity = 1.0f;
        tracks[i].midiNote = midiNoteForTrack(i);
    }
    clearSong();
}

VoiceParams GrooveState::effectiveParams(int t, int s) const
{
    auto p = tracks[t].base;
    const auto& L = tracks[t].steps[s].locks;

    if (L[(int) Param::pitch])     p.pitchHz   = *L[(int) Param::pitch];
    if (L[(int) Param::decay])     p.decayMs   = *L[(int) Param::decay];
    if (L[(int) Param::transient]) p.transient = *L[(int) Param::transient];
    if (L[(int) Param::noise])     p.noise     = *L[(int) Param::noise];
    if (L[(int) Param::filter])    p.filter    = *L[(int) Param::filter];
    if (L[(int) Param::drive])     p.drive     = *L[(int) Param::drive];
    if (L[(int) Param::space])     p.space     = *L[(int) Param::space];
    if (L[(int) Param::blend])     p.blend     = *L[(int) Param::blend];
    return p;
}

int GrooveState::effectiveMidiNote(int t, int s) const
{
    const auto& st = tracks[t].steps[s];
    if (st.midiNote.has_value() && isValidMidiNote(*st.midiNote))
        return *st.midiNote;
    if (isValidMidiNote(tracks[t].midiNote))
        return tracks[t].midiNote;
    return midiNoteForTrack(t);
}

bool GrooveState::trackIsAudible(int track) const
{
    bool anySolo = false;
    for (const auto& tr : tracks)
        if (tr.soloed)
        {
            anySolo = true;
            break;
        }
    if (anySolo)
        return tracks[track].soloed;
    return ! tracks[track].muted;
}

void GrooveState::clearSong()
{
    song = {};
    song.follow = false;
    song.current = 0;
    midiLanes = makeDefaultMidiLanes();

    for (auto& track : tracks)
    {
        track.rhythmMode = RhythmMode::step;
        track.generatorSteps = 16;
        track.pulses = 0;
        track.rotate = 0;
        track.division = 1.0f;
        track.probability = 1.0f;
        track.velocity = 1.0f;
        track.muted = false;
        track.soloed = false;

        for (auto& step : track.steps)
        {
            step = Step{};
        }
    }
}

void GrooveState::seedDefaultSong()
{
    song = {};
    song.follow = false;
    song.current = 0;

    auto make = [this](SongPart part, int bars)
    {
        SongSection section;
        section.part = part;
        section.bars = bars;
        section.meter = meter;
        copyPatternFromTracks(section, tracks);
        return section;
    };

    auto verse = make(SongPart::verse, 4);
    auto chorus = make(SongPart::chorus, 8);
    chorus.shapes[1].pulses = juce::jmin(chorus.shapes[1].generatorSteps, chorus.shapes[1].pulses + 2);
    chorus.shapes[3].pulses = juce::jmin(chorus.shapes[3].generatorSteps, chorus.shapes[3].pulses + 4);
    auto bridge = make(SongPart::bridge, 4);
    bridge.shapes[0].pulses = juce::jmax(1, bridge.shapes[0].pulses - 1);
    bridge.shapes[3].rotate = (bridge.shapes[3].rotate + 4) % juce::jmax(1, bridge.shapes[3].generatorSteps);
    bridge.shapes[5].pulses = juce::jmin(bridge.shapes[5].generatorSteps, bridge.shapes[5].pulses + 3);

    song.sections.push_back(verse);
    song.sections.push_back(chorus);
    song.sections.push_back(bridge);
    applySongSection(0);
}

void GrooveState::applySongSection(int index)
{
    if (song.sections.empty())
        return;
    song.current = juce::jlimit(0, (int) song.sections.size() - 1, index);
    const auto& section = song.sections[(size_t) song.current];
    applyPatternToTracks(section, tracks);
    midiLanes = section.midiLanes;
    meter = section.meter;
}

void GrooveState::captureLiveToCurrentSection()
{
    if (song.sections.empty())
        return;
    song.current = juce::jlimit(0, (int) song.sections.size() - 1, song.current);
    auto& section = song.sections[(size_t) song.current];
    section.meter = meter;
    copyPatternFromTracks(section, tracks);
    section.midiLanes = midiLanes;
}

void GrooveState::setLock(int t, int s, Param p, float v) { tracks[t].steps[s].locks[(int)p] = v; }
void GrooveState::clearLock(int t, int s, Param p) { tracks[t].steps[s].locks[(int)p].reset(); }
void GrooveState::clearAllLocks(int t, int s)
{
    for (auto& x : tracks[t].steps[s].locks)
        x.reset();
    tracks[t].steps[s].midiNote.reset();
}

juce::var GrooveState::toVar() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty("formatVersion", 13);
    root->setProperty("name", name);
    root->setProperty("lastPluginPath", lastPluginPath);
    root->setProperty("lastPluginProgram", lastPluginProgram);
    root->setProperty("lastPluginPatch", lastPluginPatch);
    root->setProperty("lastSynthPluginPath", lastSynthPluginPath);
    root->setProperty("lastSynthPatch", lastSynthPatch);
    root->setProperty("lastSynthOctave", lastSynthOctave);
    root->setProperty("lastKeyboardTarget", lastKeyboardTarget);
    root->setProperty("keysPlugin", keysPlugin);
    root->setProperty("lastKeysPluginPath", lastKeysPluginPath);
    root->setProperty("lastPolymaxPluginPath", lastPolymaxPluginPath);
    root->setProperty("lastElectraPatch", lastElectraPatch);
    root->setProperty("lastPolymaxPatch", lastPolymaxPatch);
    {
        auto* m = new juce::DynamicObject();
        m->setProperty("drumVol", mix.drumVol);
        m->setProperty("drumLeft", mix.drumLeft);
        m->setProperty("drumRight", mix.drumRight);
        m->setProperty("synthVol", mix.synthVol);
        m->setProperty("synthLeft", mix.synthLeft);
        m->setProperty("synthRight", mix.synthRight);
        m->setProperty("keysVol", mix.keysVol);
        m->setProperty("keysLeft", mix.keysLeft);
        m->setProperty("keysRight", mix.keysRight);
        m->setProperty("polyVol", mix.polyVol);
        m->setProperty("polyLeft", mix.polyLeft);
        m->setProperty("polyRight", mix.polyRight);
        m->setProperty("busComp", mix.busComp);
        m->setProperty("masterVol", mix.masterVol);
        juce::Array<juce::var> eq;
        for (int i = 0; i < kEqBands; ++i)
            eq.add(mix.eqGainDb[(size_t) i]);
        m->setProperty("eq", eq);
        juce::Array<juce::var> fxArr;
        for (int c = 0; c < kMixChannels; ++c)
        {
            auto* fx = new juce::DynamicObject();
            const auto& ch = mix.channelFx[(size_t) c];
            fx->setProperty("lpOn", ch.lpOn);
            fx->setProperty("lp", ch.lp);
            fx->setProperty("hpOn", ch.hpOn);
            fx->setProperty("hp", ch.hp);
            fx->setProperty("delayOn", ch.delayOn);
            fx->setProperty("delayWet", ch.delayWet);
            fx->setProperty("delayFb", ch.delayFb);
            fx->setProperty("delayNote", ch.delayNote);
            fx->setProperty("reverbOn", ch.reverbOn);
            fx->setProperty("reverbSize", ch.reverbSize);
            fx->setProperty("reverbDecay", ch.reverbDecay);
            fx->setProperty("reverbWet", ch.reverbWet);
            fx->setProperty("reverbPreDelay", ch.reverbPreDelay);
            fx->setProperty("reverbWidth", ch.reverbWidth);
            fx->setProperty("reverbBass", ch.reverbBass);
            fx->setProperty("reverbMid", ch.reverbMid);
            fx->setProperty("reverbTreble", ch.reverbTreble);
            fx->setProperty("reverbVolume", ch.reverbVolume);
            fx->setProperty("paradiseOn", ch.paradiseOn);
            fx->setProperty("paradiseInput", ch.paradiseInput);
            fx->setProperty("paradiseGate", ch.paradiseGate);
            fx->setProperty("paradisePre", ch.paradisePre);
            fx->setProperty("paradiseAmp", ch.paradiseAmp);
            fx->setProperty("paradiseCab", ch.paradiseCab);
            fx->setProperty("paradiseRoom", ch.paradiseRoom);
            fx->setProperty("paradiseOutput", ch.paradiseOutput);
            fx->setProperty("paradiseLimit", ch.paradiseLimit);
            fx->setProperty("driveOn", ch.driveOn); fx->setProperty("driveAmount", ch.driveAmount); fx->setProperty("driveTone", ch.driveTone); fx->setProperty("driveMix", ch.driveMix);
            fx->setProperty("ringOn", ch.ringOn); fx->setProperty("ringFreq", ch.ringFreq); fx->setProperty("ringDepth", ch.ringDepth); fx->setProperty("ringMix", ch.ringMix);
            fx->setProperty("combOn", ch.combOn); fx->setProperty("combFreq", ch.combFreq); fx->setProperty("combFeedback", ch.combFeedback); fx->setProperty("combMix", ch.combMix);
            juce::Array<juce::var> locks;
            for (int s = 0; s < kSteps; ++s)
            {
                const auto& lock = mix.fxLocks[(size_t) c][(size_t) s];
                if (lock.empty())
                    continue;
                auto* lo = new juce::DynamicObject();
                lo->setProperty("step", s);
                if (lock.lp.has_value()) lo->setProperty("lp", *lock.lp);
                if (lock.hp.has_value()) lo->setProperty("hp", *lock.hp);
                if (lock.delayWet.has_value()) lo->setProperty("delayWet", *lock.delayWet);
                if (lock.delayFeedback.has_value()) lo->setProperty("delayFeedback", *lock.delayFeedback);
                if (lock.delayNote.has_value()) lo->setProperty("delayNote", *lock.delayNote);
                if (lock.reverbSize.has_value()) lo->setProperty("reverbSize", *lock.reverbSize);
                if (lock.reverbDecay.has_value()) lo->setProperty("reverbDecay", *lock.reverbDecay);
                if (lock.reverbWet.has_value()) lo->setProperty("reverbWet", *lock.reverbWet);
                if (lock.driveAmount.has_value()) lo->setProperty("driveAmount", *lock.driveAmount);
                if (lock.driveTone.has_value()) lo->setProperty("driveTone", *lock.driveTone);
                if (lock.driveMix.has_value()) lo->setProperty("driveMix", *lock.driveMix);
                if (lock.ringFreq.has_value()) lo->setProperty("ringFreq", *lock.ringFreq);
                if (lock.ringDepth.has_value()) lo->setProperty("ringDepth", *lock.ringDepth);
                if (lock.ringMix.has_value()) lo->setProperty("ringMix", *lock.ringMix);
                if (lock.combFreq.has_value()) lo->setProperty("combFreq", *lock.combFreq);
                if (lock.combFeedback.has_value()) lo->setProperty("combFeedback", *lock.combFeedback);
                if (lock.combMix.has_value()) lo->setProperty("combMix", *lock.combMix);
                locks.add(juce::var(lo));
            }
            fx->setProperty("locks", locks);
            fxArr.add(juce::var(fx));
        }
        m->setProperty("channelFx", fxArr);
        root->setProperty("mix", juce::var(m));
    }
    root->setProperty("soundMode", soundMode);
    root->setProperty("bpm", bpm);
    root->setProperty("recordQuantize", recordQuantize);
    root->setProperty("recordQuantizeNote", recordQuantizeNote);
    root->setProperty("meter", (int) meter);
    root->setProperty("meterTransform", (int) meterTransform);
    root->setProperty("selectedTrack", selectedTrack);
    root->setProperty("selectedStep", selectedStep);
    root->setProperty("similarity", similarity);
    root->setProperty("surpriseBudget", surpriseBudget);
    root->setProperty("lockResistance", lockResistance);

    juce::Array<juce::var> trackArray;
    for (int t = 0; t < kTracks; ++t)
    {
        const auto& track = tracks[t];
        auto* tr = new juce::DynamicObject();
        const auto p = track.base;
        tr->setProperty("pitch", p.pitchHz);
        tr->setProperty("decay", p.decayMs);
        tr->setProperty("transient", p.transient);
        tr->setProperty("noise", p.noise);
        tr->setProperty("filter", p.filter);
        tr->setProperty("drive", p.drive);
        tr->setProperty("space", p.space);
        tr->setProperty("blend", p.blend);

        tr->setProperty("rhythmMode", (int) track.rhythmMode);
        tr->setProperty("generatorSteps", track.generatorSteps);
        tr->setProperty("pulses", track.pulses);
        tr->setProperty("rotate", track.rotate);
        tr->setProperty("division", track.division);
        tr->setProperty("trackProbability", track.probability);
        tr->setProperty("trackVelocity", track.velocity);
        tr->setProperty("swing", track.swing);
        tr->setProperty("evolutionPolicy", (int)track.evolutionPolicy);
        tr->setProperty("evolveAmount", track.evolveAmount);
        tr->setProperty("muted", track.muted);
        tr->setProperty("soloed", track.soloed);
        tr->setProperty("midiNote", track.midiNote);
        tr->setProperty("steps", stepsToVar(track.steps));
        trackArray.add(juce::var(tr));
    }

    root->setProperty("tracks", trackArray);

    auto* songObj = new juce::DynamicObject();
    songObj->setProperty("follow", song.follow);
    songObj->setProperty("current", song.current);
    juce::Array<juce::var> sectionArray;
    for (const auto& section : song.sections)
    {
        auto* so = new juce::DynamicObject();
        so->setProperty("part", (int) section.part);
        so->setProperty("bars", section.bars);
        so->setProperty("meter", (int) section.meter);
        so->setProperty("currentTake", section.currentTake);
        juce::Array<juce::var> shapes;
        juce::Array<juce::var> sectionSteps;
        for (int t = 0; t < kTracks; ++t)
        {
            shapes.add(shapeToVar(section.shapes[(size_t) t]));
            sectionSteps.add(stepsToVar(section.steps[(size_t) t]));
        }
        so->setProperty("shapes", shapes);
        so->setProperty("steps", sectionSteps);
        so->setProperty("midiLanes", lanesToVar(section.midiLanes));
        juce::Array<juce::var> takes;
        for (const auto& take : section.takes)
            takes.add(takeToVar(take));
        so->setProperty("takes", takes);
        sectionArray.add(juce::var(so));
    }
    songObj->setProperty("sections", sectionArray);
    root->setProperty("song", juce::var(songObj));
    return juce::var(root);
}

bool GrooveState::fromVar(const juce::var& v)
{
    auto* root = v.getDynamicObject();
    if (root == nullptr) return false;

    const int formatVersion = root->getProperty("formatVersion").isVoid()
        ? 0 : (int) root->getProperty("formatVersion");

    auto bpmVar = root->getProperty("bpm");
    if (!bpmVar.isVoid()) bpm = (double)bpmVar;
    auto quantVar = root->getProperty("recordQuantize");
    if (! quantVar.isVoid()) recordQuantize = (bool) quantVar;
    auto quantNoteVar = root->getProperty("recordQuantizeNote");
    if (! quantNoteVar.isVoid())
        recordQuantizeNote = juce::jlimit(0, kQuantizeNoteCount - 1, (int) quantNoteVar);
    auto meterVar = root->getProperty("meter");
    if (!meterVar.isVoid())
        meter = (Meter) juce::jlimit(0, kMeterCount - 1, (int) meterVar);
    auto transformVar = root->getProperty("meterTransform");
    if (! transformVar.isVoid())
        meterTransform = (MeterTransform) juce::jlimit(0, kMeterTransformCount - 1, (int) transformVar);
    auto nameVar = root->getProperty("name");
    if (!nameVar.isVoid() && nameVar.toString().isNotEmpty())
        name = nameVar.toString();
    auto pluginVar = root->getProperty("lastPluginPath");
    if (!pluginVar.isVoid()) lastPluginPath = pluginVar.toString();
    auto programVar = root->getProperty("lastPluginProgram");
    if (!programVar.isVoid()) lastPluginProgram = (int) programVar;
    auto patchVar = root->getProperty("lastPluginPatch");
    if (!patchVar.isVoid()) lastPluginPatch = patchVar.toString();
    auto synthVar = root->getProperty("lastSynthPluginPath");
    if (! synthVar.isVoid()) lastSynthPluginPath = synthVar.toString();
    auto synthPatchVar = root->getProperty("lastSynthPatch");
    if (! synthPatchVar.isVoid()) lastSynthPatch = synthPatchVar.toString();
    auto synthOctVar = root->getProperty("lastSynthOctave");
    if (! synthOctVar.isVoid())
    {
        const int oct = (int) synthOctVar;
        lastSynthOctave = (oct < -3 || oct > 3) ? 0 : oct;
    }
    auto kbTargetVar = root->getProperty("lastKeyboardTarget");
    if (! kbTargetVar.isVoid())
        lastKeyboardTarget = juce::jlimit(0, 3, (int) kbTargetVar);
    auto keysPluginVar = root->getProperty("keysPlugin");
    if (! keysPluginVar.isVoid())
        keysPlugin = juce::jlimit(0, 1, (int) keysPluginVar);
    auto keysPathVar = root->getProperty("lastKeysPluginPath");
    if (! keysPathVar.isVoid()) lastKeysPluginPath = keysPathVar.toString();
    auto polyPathVar = root->getProperty("lastPolymaxPluginPath");
    if (! polyPathVar.isVoid()) lastPolymaxPluginPath = polyPathVar.toString();
    auto electraVar = root->getProperty("lastElectraPatch");
    if (! electraVar.isVoid()) lastElectraPatch = electraVar.toString();
    auto polymaxVar = root->getProperty("lastPolymaxPatch");
    if (! polymaxVar.isVoid()) lastPolymaxPatch = polymaxVar.toString();
    if (auto* m = root->getProperty("mix").getDynamicObject())
    {
        auto take = [](const juce::var& v, float fallback, float lo, float hi)
        {
            return v.isVoid() ? fallback : juce::jlimit(lo, hi, (float) v);
        };
        mix.drumVol = take(m->getProperty("drumVol"), 1.0f, 0.0f, 1.5f);
        mix.drumLeft = take(m->getProperty("drumLeft"), 1.0f, 0.0f, 1.5f);
        mix.drumRight = take(m->getProperty("drumRight"), 1.0f, 0.0f, 1.5f);
        mix.synthVol = take(m->getProperty("synthVol"), 1.0f, 0.0f, 1.5f);
        mix.synthLeft = take(m->getProperty("synthLeft"), 1.0f, 0.0f, 1.5f);
        mix.synthRight = take(m->getProperty("synthRight"), 1.0f, 0.0f, 1.5f);
        mix.keysVol = take(m->getProperty("keysVol"), 1.0f, 0.0f, 1.5f);
        mix.keysLeft = take(m->getProperty("keysLeft"), 1.0f, 0.0f, 1.5f);
        mix.keysRight = take(m->getProperty("keysRight"), 1.0f, 0.0f, 1.5f);
        mix.polyVol = take(m->getProperty("polyVol"), 1.0f, 0.0f, 1.5f);
        mix.polyLeft = take(m->getProperty("polyLeft"), 1.0f, 0.0f, 1.5f);
        mix.polyRight = take(m->getProperty("polyRight"), 1.0f, 0.0f, 1.5f);
        mix.busComp = take(m->getProperty("busComp"), 0.22f, 0.0f, 1.0f);
        mix.masterVol = take(m->getProperty("masterVol"), 1.0f, 0.0f, 1.5f);
        if (auto* eq = m->getProperty("eq").getArray())
        {
            for (int i = 0; i < juce::jmin(kEqBands, eq->size()); ++i)
                mix.eqGainDb[(size_t) i] = juce::jlimit(-12.0f, 12.0f, (float) eq->getUnchecked(i));
        }
        mix.channelFx = {};
        mix.fxLocks = {};
        if (auto* fxArr = m->getProperty("channelFx").getArray())
        {
            for (int c = 0; c < juce::jmin(kMixChannels, fxArr->size()); ++c)
            {
                auto* fx = fxArr->getUnchecked(c).getDynamicObject();
                if (fx == nullptr)
                    continue;
                auto& ch = mix.channelFx[(size_t) c];
                ch.lpOn = (bool) fx->getProperty("lpOn");
                ch.lp = take(fx->getProperty("lp"), 1.0f, 0.0f, 1.0f);
                ch.hpOn = (bool) fx->getProperty("hpOn");
                ch.hp = take(fx->getProperty("hp"), 0.0f, 0.0f, 1.0f);
                ch.delayOn = (bool) fx->getProperty("delayOn");
                ch.delayWet = take(fx->getProperty("delayWet"), 0.22f, 0.0f, 1.0f);
                ch.delayFb = take(fx->getProperty("delayFb"), 0.32f, 0.0f, 0.85f);
                ch.delayNote = juce::jlimit(0, kDelayNoteCount - 1,
                    fx->getProperty("delayNote").isVoid() ? 2 : (int) fx->getProperty("delayNote"));
                ch.reverbOn = !fx->getProperty("reverbOn").isVoid() && (bool)fx->getProperty("reverbOn");
                ch.reverbSize = take(fx->getProperty("reverbSize"), 0.45f, 0.0f, 1.0f);
                ch.reverbDecay = take(fx->getProperty("reverbDecay"), 0.55f, 0.0f, 1.0f);
                ch.reverbWet = take(fx->getProperty("reverbWet"), 0.38f, 0.0f, 1.0f);
                ch.reverbPreDelay = take(fx->getProperty("reverbPreDelay"), 0.12f, 0.0f, 1.0f);
                ch.reverbWidth = take(fx->getProperty("reverbWidth"), 0.75f, 0.0f, 1.0f);
                ch.reverbBass = take(fx->getProperty("reverbBass"), 0.5f, 0.0f, 1.0f);
                ch.reverbMid = take(fx->getProperty("reverbMid"), 0.5f, 0.0f, 1.0f);
                ch.reverbTreble = take(fx->getProperty("reverbTreble"), 0.5f, 0.0f, 1.0f);
                ch.reverbVolume = take(fx->getProperty("reverbVolume"), 0.85f, 0.0f, 1.0f);
                ch.paradiseOn = ! fx->getProperty("paradiseOn").isVoid()
                    && (bool) fx->getProperty("paradiseOn");
                ch.paradiseInput = take(fx->getProperty("paradiseInput"), 0.70f, 0.0f, 1.0f);
                ch.paradiseGate = take(fx->getProperty("paradiseGate"), 0.00f, 0.0f, 1.0f);
                ch.paradisePre = take(fx->getProperty("paradisePre"), 0.70f, 0.0f, 1.0f);
                ch.paradiseAmp = take(fx->getProperty("paradiseAmp"), 0.78f, 0.0f, 1.0f);
                ch.paradiseCab = take(fx->getProperty("paradiseCab"), 0.80f, 0.0f, 1.0f);
                ch.paradiseRoom = take(fx->getProperty("paradiseRoom"), 0.25f, 0.0f, 1.0f);
                ch.paradiseOutput = take(fx->getProperty("paradiseOutput"), 0.90f, 0.0f, 1.0f);
                ch.paradiseLimit = take(fx->getProperty("paradiseLimit"), 0.55f, 0.0f, 1.0f);
                ch.driveOn = !fx->getProperty("driveOn").isVoid() && (bool)fx->getProperty("driveOn");
                ch.driveAmount = take(fx->getProperty("driveAmount"), 0.25f, 0.0f, 1.0f);
                ch.driveTone = take(fx->getProperty("driveTone"), 0.55f, 0.0f, 1.0f);
                ch.driveMix = take(fx->getProperty("driveMix"), 1.0f, 0.0f, 1.0f);
                ch.ringOn = !fx->getProperty("ringOn").isVoid() && (bool)fx->getProperty("ringOn");
                ch.ringFreq = take(fx->getProperty("ringFreq"), 0.35f, 0.0f, 1.0f);
                ch.ringDepth = take(fx->getProperty("ringDepth"), 1.0f, 0.0f, 1.0f);
                ch.ringMix = take(fx->getProperty("ringMix"), 0.5f, 0.0f, 1.0f);
                ch.combOn = !fx->getProperty("combOn").isVoid() && (bool)fx->getProperty("combOn");
                ch.combFreq = take(fx->getProperty("combFreq"), 0.35f, 0.0f, 1.0f);
                ch.combFeedback = take(fx->getProperty("combFeedback"), 0.45f, 0.0f, 0.96f);
                ch.combMix = take(fx->getProperty("combMix"), 0.5f, 0.0f, 1.0f);
                if (auto* locks = fx->getProperty("locks").getArray())
                {
                    for (const auto& item : *locks)
                    {
                        auto* lo = item.getDynamicObject();
                        if (lo == nullptr)
                            continue;
                        const int s = juce::jlimit(0, kSteps - 1, (int) lo->getProperty("step"));
                        auto& lock = mix.fxLocks[(size_t) c][(size_t) s];
                        if (! lo->getProperty("lp").isVoid())
                            lock.lp = take(lo->getProperty("lp"), 1.0f, 0.0f, 1.0f);
                        if (! lo->getProperty("hp").isVoid())
                            lock.hp = take(lo->getProperty("hp"), 0.0f, 0.0f, 1.0f);
                        if (! lo->getProperty("delayWet").isVoid())
                            lock.delayWet = take(lo->getProperty("delayWet"), 0.22f, 0.0f, 1.0f);
                        if (! lo->getProperty("delayFeedback").isVoid())
                            lock.delayFeedback = take(lo->getProperty("delayFeedback"), 0.32f, 0.0f, 0.85f);
                        if (! lo->getProperty("delayNote").isVoid())
                            lock.delayNote = juce::jlimit(0, kDelayNoteCount - 1, (int) lo->getProperty("delayNote"));
                        if (! lo->getProperty("reverbSize").isVoid()) lock.reverbSize = take(lo->getProperty("reverbSize"), 0.45f, 0.0f, 1.0f);
                        if (! lo->getProperty("reverbDecay").isVoid()) lock.reverbDecay = take(lo->getProperty("reverbDecay"), 0.50f, 0.0f, 1.0f);
                        if (! lo->getProperty("reverbWet").isVoid()) lock.reverbWet = take(lo->getProperty("reverbWet"), 0.18f, 0.0f, 1.0f);
                        if (!lo->getProperty("driveAmount").isVoid()) lock.driveAmount = take(lo->getProperty("driveAmount"), 0.25f, 0.0f, 1.0f);
                        if (!lo->getProperty("driveTone").isVoid()) lock.driveTone = take(lo->getProperty("driveTone"), 0.55f, 0.0f, 1.0f);
                        if (!lo->getProperty("driveMix").isVoid()) lock.driveMix = take(lo->getProperty("driveMix"), 1.0f, 0.0f, 1.0f);
                        if (!lo->getProperty("ringFreq").isVoid()) lock.ringFreq = take(lo->getProperty("ringFreq"), 0.35f, 0.0f, 1.0f);
                        if (!lo->getProperty("ringDepth").isVoid()) lock.ringDepth = take(lo->getProperty("ringDepth"), 1.0f, 0.0f, 1.0f);
                        if (!lo->getProperty("ringMix").isVoid()) lock.ringMix = take(lo->getProperty("ringMix"), 0.5f, 0.0f, 1.0f);
                        if (!lo->getProperty("combFreq").isVoid()) lock.combFreq = take(lo->getProperty("combFreq"), 0.35f, 0.0f, 1.0f);
                        if (!lo->getProperty("combFeedback").isVoid()) lock.combFeedback = take(lo->getProperty("combFeedback"), 0.45f, 0.0f, 0.96f);
                        if (!lo->getProperty("combMix").isVoid()) lock.combMix = take(lo->getProperty("combMix"), 0.5f, 0.0f, 1.0f);
                    }
                }
            }
        }
    }
    auto modeVar = root->getProperty("soundMode");
    if (!modeVar.isVoid()) soundMode = juce::jlimit(1, 3, (int) modeVar);
    selectedTrack = juce::jlimit(0, kTracks - 1, (int)root->getProperty("selectedTrack"));
    selectedStep = juce::jlimit(0, kSteps - 1, (int)root->getProperty("selectedStep"));

    auto simVar = root->getProperty("similarity");
    auto surpriseVar = root->getProperty("surpriseBudget");
    auto resistVar = root->getProperty("lockResistance");
    if (!simVar.isVoid()) similarity = juce::jlimit(0.0f,1.0f,(float)simVar);
    if (!surpriseVar.isVoid()) surpriseBudget = juce::jlimit(1,32,(int)surpriseVar);
    if (!resistVar.isVoid()) lockResistance = juce::jlimit(0.0f,1.0f,(float)resistVar);

    auto arr = root->getProperty("tracks");
    if (!arr.isArray() || arr.size() != kTracks) return false;

    for (int t = 0; t < kTracks; ++t)
    {
        auto* tr = arr[t].getDynamicObject();
        if (!tr) continue;
        auto& track = tracks[t];

        auto readFloat = [tr](const char* key, float fallback)
        {
            auto x = tr->getProperty(key);
            return x.isVoid() ? fallback : (float)x;
        };
        track.base.pitchHz = readFloat("pitch", track.base.pitchHz);
        track.base.decayMs = readFloat("decay", track.base.decayMs);
        track.base.transient = readFloat("transient", track.base.transient);
        track.base.noise = readFloat("noise", track.base.noise);
        track.base.filter = readFloat("filter", track.base.filter);
        track.base.drive = readFloat("drive", track.base.drive);
        track.base.space = readFloat("space", track.base.space);
        track.base.blend = readFloat("blend", track.base.blend);

        auto rhythmModeVar = tr->getProperty("rhythmMode");
        if (!rhythmModeVar.isVoid())
            track.rhythmMode = (RhythmMode) juce::jlimit(0, 2, (int) rhythmModeVar);
        else
            track.rhythmMode = RhythmMode::hybrid;

        // v0.6 fields; fall back to v0.5 length/rate when loading older files.
        auto gs = tr->getProperty("generatorSteps");
        auto oldLen = tr->getProperty("length");
        track.generatorSteps = juce::jlimit(1,kSteps,
            !gs.isVoid() ? (int)gs : (!oldLen.isVoid() ? (int)oldLen : track.generatorSteps));

        auto pu = tr->getProperty("pulses");
        if (!pu.isVoid()) track.pulses = juce::jlimit(0,track.generatorSteps,(int)pu);
        else track.pulses = juce::jlimit(0,track.generatorSteps,track.pulses);

        auto ro = tr->getProperty("rotate");
        if (!ro.isVoid()) track.rotate = (int)ro;

        auto div = tr->getProperty("division");
        auto oldRate = tr->getProperty("rate");
        if (!div.isVoid()) track.division = (float)div;
        else if (!oldRate.isVoid()) track.division = (float)oldRate;

        track.probability = juce::jlimit(0.0f,1.0f,readFloat("trackProbability",track.probability));
        track.velocity = juce::jlimit(0.0f,1.2f,readFloat("trackVelocity",track.velocity));
        track.swing = juce::jlimit(0.0f,0.75f,readFloat("swing",track.swing));

        auto policy = tr->getProperty("evolutionPolicy");
        auto amount = tr->getProperty("evolveAmount");
        if (!policy.isVoid()) track.evolutionPolicy = (EvolutionPolicy)juce::jlimit(0,2,(int)policy);
        if (!amount.isVoid()) track.evolveAmount = juce::jlimit(0.0f,1.0f,(float)amount);
        auto mutedVar = tr->getProperty("muted");
        auto soloVar = tr->getProperty("soloed");
        if (!mutedVar.isVoid()) track.muted = (bool) mutedVar;
        if (!soloVar.isVoid()) track.soloed = (bool) soloVar;
        auto trackNote = tr->getProperty("midiNote");
        if (!trackNote.isVoid() && isValidMidiNote((int) trackNote))
            track.midiNote = (int) trackNote;
        else
            track.midiNote = midiNoteForTrack(t);

        auto steps = tr->getProperty("steps");
        if (!steps.isArray()) continue;
        for (int s = 0; s < juce::jmin(kSteps, steps.size()); ++s)
        {
            auto* so = steps[s].getDynamicObject();
            if (!so) continue;
            auto& st = track.steps[s];

            auto activeVar = so->getProperty("active");
            st.active = !activeVar.isVoid() && (bool)activeVar;
            auto overrideVar = so->getProperty("overrideMode");
            if (!overrideVar.isVoid())
                st.overrideMode = (StepOverrideMode)juce::jlimit(0,2,(int)overrideVar);
            else if (st.active)
                st.overrideMode = StepOverrideMode::forceOn; // migrate v0.5 manual hits

            auto vel = so->getProperty("velocity");
            auto prob = so->getProperty("probability");
            auto rat = so->getProperty("ratchet");
            auto role = so->getProperty("role");
            if (!vel.isVoid()) st.velocity = juce::jlimit(0.0f,1.2f,(float)vel);
            if (!prob.isVoid()) st.probability = juce::jlimit(0.0f,1.0f,(float)prob);
            if (!rat.isVoid()) st.ratchet = juce::jlimit(1,4,(int)rat);
            if (!role.isVoid()) st.role = (StepRole)juce::jlimit(0,3,(int)role);
            auto midi = so->getProperty("midiNote");
            if (!midi.isVoid() && isValidMidiNote((int) midi))
                st.midiNote = (int) midi;
            else
                st.midiNote.reset();

            auto* lo = so->getProperty("locks").getDynamicObject();
            if (lo)
            {
                for (int pi=0; pi<paramCount; ++pi)
                {
                    auto value = lo->getProperty(paramName((Param)pi));
                    if (!value.isVoid()) st.locks[pi] = (float)value;
                }
            }
        }
    }

    auto* songObj = root->getProperty("song").getDynamicObject();
    if (songObj == nullptr)
    {
        clearSong();
        return true;
    }

    song = {};
    auto followVar = songObj->getProperty("follow");
    if (! followVar.isVoid()) song.follow = (bool) followVar;
    song.current = juce::jmax(0, (int) songObj->getProperty("current"));
    auto sectionsVar = songObj->getProperty("sections");
    if (sectionsVar.isArray())
    {
        for (const auto& item : *sectionsVar.getArray())
        {
            auto* so = item.getDynamicObject();
            if (so == nullptr) continue;
            SongSection section;
            section.part = (SongPart) juce::jlimit(0, 7, (int) so->getProperty("part"));
            section.bars = juce::jlimit(1, 32, (int) so->getProperty("bars"));
            if (section.bars < 1) section.bars = 4;
            auto meterSec = so->getProperty("meter");
            if (! meterSec.isVoid())
                section.meter = (Meter) juce::jlimit(0, kMeterCount - 1, (int) meterSec);
            else
                section.meter = meter;
            auto shapesVar = so->getProperty("shapes");
            for (int t = 0; t < kTracks; ++t)
            {
                section.shapes[(size_t) t] = TrackShape::fromTrack(tracks[t]);
                section.steps[(size_t) t] = tracks[t].steps;
            }
            if (shapesVar.isArray())
            {
                for (int t = 0; t < juce::jmin(kTracks, shapesVar.size()); ++t)
                    shapeFromVar(section.shapes[(size_t) t], shapesVar[t].getDynamicObject());
            }
            auto sectionStepsVar = so->getProperty("steps");
            if (sectionStepsVar.isArray())
            {
                for (int t = 0; t < juce::jmin(kTracks, sectionStepsVar.size()); ++t)
                    stepsFromVar(section.steps[(size_t) t], sectionStepsVar[t], formatVersion < 9);
            }
            lanesFromVar(section.midiLanes, so->getProperty("midiLanes"));
            auto takeVar = so->getProperty("currentTake");
            if (! takeVar.isVoid())
                section.currentTake = (int) takeVar;
            auto takesVar = so->getProperty("takes");
            if (takesVar.isArray())
            {
                for (const auto& takeItem : *takesVar.getArray())
                {
                    if ((int) section.takes.size() >= kMaxTakes)
                        break;
                    section.takes.push_back(takeFromVar(takeItem));
                }
            }
            if (section.currentTake >= (int) section.takes.size())
                section.currentTake = (int) section.takes.size() - 1;
            song.sections.push_back(section);
        }
    }
    if (song.sections.empty())
        clearSong();
    else
        applySongSection(song.current);

    if (formatVersion < 9)
    {
        for (auto& track : tracks)
            track.probability = 0.96f;
        for (auto& section : song.sections)
            for (auto& sh : section.shapes)
                sh.probability = 0.96f;
    }
    return true;
}
}
