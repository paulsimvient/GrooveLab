#include "GrooveState.h"
#include "../Audio/DrumMidi.h"

namespace groove
{
static VoiceParams defaultsFor(int i)
{
    switch (i)
    {
        case 0: return {52.0f, 430.0f, 0.46f, 0.03f, 0.58f, 0.10f, 0.04f, 0.18f};
        case 1: return {185.0f, 245.0f, 0.68f, 0.74f, 0.72f, 0.10f, 0.10f, 0.62f};
        case 2: return {320.0f, 175.0f, 0.90f, 0.92f, 0.76f, 0.08f, 0.14f, 0.78f};
        case 3: return {690.0f, 72.0f, 0.62f, 0.96f, 0.94f, 0.04f, 0.05f, 0.90f};
        case 4: return {520.0f, 380.0f, 0.55f, 0.97f, 0.90f, 0.04f, 0.18f, 0.92f};
        case 5: return {245.0f, 220.0f, 0.42f, 0.28f, 0.74f, 0.11f, 0.08f, 0.38f};
        case 6: return {385.0f, 265.0f, 0.36f, 0.22f, 0.80f, 0.13f, 0.10f, 0.32f};
        default:return {610.0f, 520.0f, 0.75f, 0.52f, 0.68f, 0.24f, 0.35f, 0.58f};
    }
}

GrooveState::GrooveState()
{
    // Musical starting point is generator data, not a hard-coded step pattern.
    const int defaultPulses[kTracks] = {4, 2, 2, 11, 3, 5, 3, 1};
    const int defaultRotate[kTracks] = {0, 4, 12, 0, 3, 1, 5, 14};
    const int defaultSteps[kTracks]  = {16,16,16,16,16,16,16,16};

    for (int i = 0; i < kTracks; ++i)
    {
        tracks[i].base = defaultsFor(i);
        tracks[i].generatorSteps = defaultSteps[i];
        tracks[i].pulses = defaultPulses[i];
        tracks[i].rotate = defaultRotate[i];
        tracks[i].division = 1.0f;
        tracks[i].midiNote = midiNoteForTrack(i);
    }
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
    if (st.midiNote.has_value() && isUjamKitNote(*st.midiNote))
        return *st.midiNote;
    if (isUjamKitNote(tracks[t].midiNote))
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
    root->setProperty("formatVersion", 6);
    root->setProperty("bpm", bpm);
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

        juce::Array<juce::var> stepArray;
        for (int s = 0; s < kSteps; ++s)
        {
            const auto& st = track.steps[s];
            auto* so = new juce::DynamicObject();
            so->setProperty("active", st.active);
            so->setProperty("overrideMode", (int)st.overrideMode);
            so->setProperty("velocity", st.velocity);
            so->setProperty("probability", st.probability);
            so->setProperty("ratchet", st.ratchet);
            so->setProperty("role", (int)st.role);
            if (st.midiNote.has_value())
                so->setProperty("midiNote", *st.midiNote);

            auto* lo = new juce::DynamicObject();
            for (int pi = 0; pi < paramCount; ++pi)
                if (st.locks[pi].has_value())
                    lo->setProperty(paramName((Param)pi), *st.locks[pi]);
            so->setProperty("locks", juce::var(lo));
            stepArray.add(juce::var(so));
        }
        tr->setProperty("steps", stepArray);
        trackArray.add(juce::var(tr));
    }

    root->setProperty("tracks", trackArray);
    return juce::var(root);
}

bool GrooveState::fromVar(const juce::var& v)
{
    auto* root = v.getDynamicObject();
    if (root == nullptr) return false;

    auto bpmVar = root->getProperty("bpm");
    if (!bpmVar.isVoid()) bpm = (double)bpmVar;
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
        if (!trackNote.isVoid() && isUjamKitNote((int) trackNote))
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
            if (!midi.isVoid() && isUjamKitNote((int) midi))
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
    return true;
}
}
