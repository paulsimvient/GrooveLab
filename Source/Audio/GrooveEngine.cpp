#include "GrooveEngine.h"
#include "DrumMidi.h"
#include <cmath>

namespace groove
{
GrooveEngine::GrooveEngine()
{
    loadAutosave();
}

void GrooveEngine::prepare(double sampleRate, int maxBlockSize)
{
    currentSampleRate = sampleRate;
    sequencer.prepare(sampleRate);
    sequencer.setBpm(grooveState.bpm);
    synth.prepare(sampleRate, maxBlockSize);
}

void GrooveEngine::scheduleMidi(juce::MidiBuffer& midiOut, const juce::MidiMessage& message,
                               int sample, int blockSamples)
{
    if (sample < blockSamples)
        midiOut.addEvent(message, juce::jlimit(0, blockSamples - 1, sample));
    else
        pendingMidi.push_back({ sample - blockSamples, message });
}

void GrooveEngine::emitAllControllers(juce::MidiBuffer& midiOut, int track, int step,
                                      int sample, int blockSamples)
{
    const auto& tr = grooveState.tracks[juce::jlimit(0, kTracks - 1, track)];
    const auto& st = tr.steps[juce::jlimit(0, kSteps - 1, step)];
    const auto params = grooveState.effectiveParams(track, step);
    const int channel = 1;

    scheduleMidi(midiOut, juce::MidiMessage::controllerEvent(channel, kCcVolume, toMidi7(tr.velocity)),
                 sample, blockSamples);
    scheduleMidi(midiOut, juce::MidiMessage::controllerEvent(channel, kCcExpression, toMidi7(st.velocity)),
                 sample, blockSamples);

    for (int i = 0; i < paramCount; ++i)
    {
        const auto p = (Param) i;
        scheduleMidi(midiOut,
                     juce::MidiMessage::controllerEvent(channel, ccForParam(p),
                                                        paramToCcValue(p, paramValue(params, p))),
                     sample, blockSamples);
    }
}

void GrooveEngine::emitTriggerMidi(juce::MidiBuffer& midiOut, const Sequencer::Trigger& tr, int blockSamples)
{
    const int note = grooveState.effectiveMidiNote(tr.track, tr.step);
    const bool snare = (tr.track == 1 || note == 38 || note == 40);
    float hitVel = tr.velocity;
    if (snare)
    {
        // UJAM snares read quiet + they gate on short notes. Lift body, keep ghosts quieter.
        if (hitVel >= 0.45f)
            hitVel = juce::jlimit(0.85f, 1.2f, std::sqrt(hitVel) * 1.2f);
        else
            hitVel = juce::jmax(0.4f, std::sqrt(hitVel));
    }
    const int vel = juce::jlimit(1, 127, (int) std::round(hitVel * 127.0f));
    const int channel = 1;
    const int reps = juce::jmax(1, tr.ratchet);
    const auto division = juce::jlimit(0.25f, 4.0f, grooveState.tracks[tr.track].division);
    const int stepSamples = juce::jmax(1,
        (int) (currentSampleRate * 60.0 / juce::jmax(1.0, grooveState.bpm) / 4.0 / division));
    const int spacing = juce::jmax(1, stepSamples / reps);
    const float decayMs = juce::jmax(snare ? 650.0f : 450.0f,
                                     grooveState.effectiveParams(tr.track, tr.step).decayMs);
    // Closed hat can stay short; snare/kick/clap need to ring or UJAM chokes the tail.
    const int noteLen = (tr.track == 3)
        ? juce::jmax(64, spacing / 2)
        : juce::jmax((int) (currentSampleRate * decayMs / 1000.0), spacing * 4);

    for (int r = 0; r < reps; ++r)
    {
        const int onAt = tr.sampleOffset + r * spacing;
        emitAllControllers(midiOut, tr.track, tr.step, onAt, blockSamples);
        if (snare)
        {
            scheduleMidi(midiOut, juce::MidiMessage::controllerEvent(channel, kCcVolume, vel),
                         onAt, blockSamples);
            scheduleMidi(midiOut, juce::MidiMessage::controllerEvent(channel, kCcExpression, vel),
                         onAt, blockSamples);
        }
        scheduleMidi(midiOut, juce::MidiMessage::noteOff(channel, note),
                     juce::jmax(0, onAt - 1), blockSamples);
        scheduleMidi(midiOut, juce::MidiMessage::noteOn(channel, note, (juce::uint8) vel), onAt, blockSamples);
        scheduleMidi(midiOut, juce::MidiMessage::noteOff(channel, note), onAt + noteLen, blockSamples);
    }
}

void GrooveEngine::process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiOut)
{
    buffer.clear();
    midiOut.clear();

    const int n = buffer.getNumSamples();
    const juce::ScopedLock sl(stateLock);

    const bool panic = pendingAllNotesOff.exchange(false);
    if (panic)
        pendingMidi.clear();

    std::vector<PendingMidi> leftover;
    leftover.reserve(pendingMidi.size());
    for (auto& event : pendingMidi)
    {
        if (event.samplesUntil < n)
            midiOut.addEvent(event.message, juce::jmax(0, event.samplesUntil));
        else
            leftover.push_back({ event.samplesUntil - n, event.message });
    }
    pendingMidi.swap(leftover);

    if (panic)
    {
        for (int note = 0; note < 128; ++note)
            midiOut.addEvent(juce::MidiMessage::noteOff(1, note), 0);
        midiOut.addEvent(juce::MidiMessage::allNotesOff(1), 0);
    }

    if (queuedAudition.has_value())
    {
        auto tr = *queuedAudition;
        queuedAudition.reset();
        tr.sampleOffset = 0;
        emitTriggerMidi(midiOut, tr, n);
        if (internalSynthEnabled.load())
            synth.trigger(tr.track, tr.params, tr.velocity);
    }

    if (playing.load())
    {
        sequencer.setBpm(grooveState.bpm);

        sequencer.processBlock(grooveState, n,
            [this, &midiOut, n](const Sequencer::Trigger& tr)
            {
                if (! grooveState.trackIsAudible(tr.track))
                    return;

                emitTriggerMidi(midiOut, tr, n);

                if (internalSynthEnabled.load())
                {
                    const auto division = juce::jlimit(0.25f, 4.0f, grooveState.tracks[tr.track].division);
                    const auto stepSamples = (int) (currentSampleRate * 60.0 / grooveState.bpm / 4.0 / division);
                    synth.triggerRatchet(tr.track, tr.params, tr.velocity, tr.ratchet, stepSamples);
                }
            });
    }

    // Voices continue to decay naturally after transport is stopped.
    synth.render(buffer);
}

void GrooveEngine::setInternalSynthEnabled(bool shouldPlay)
{
    internalSynthEnabled.store(shouldPlay);
}

void GrooveEngine::setPlaying(bool shouldPlay)
{
    playing.store(shouldPlay);
    if (! shouldPlay)
        pendingAllNotesOff.store(true);
    journal.append("transport", shouldPlay ? "play" : "stop");
}

void GrooveEngine::resetTransport()
{
    const juce::ScopedLock sl(stateLock);
    pendingMidi.clear();
    pendingAllNotesOff.store(true);
    sequencer.reset();
    journal.append("transport", "reset");
}

void GrooveEngine::auditionSelected()
{
    const juce::ScopedLock sl(stateLock);
    const int t = grooveState.selectedTrack;
    const int st = grooveState.selectedStep;
    const auto params = grooveState.effectiveParams(t, st);
    const auto velocity = grooveState.tracks[t].steps[st].velocity;
    queuedAudition = Sequencer::Trigger { t, st, velocity, params, 1, 0 };
    journal.append("audition", voiceName(t) + " step " + juce::String(st + 1));
}

void GrooveEngine::toggleStep(int track, int step)
{
    const juce::ScopedLock sl(stateLock);
    auto& st = grooveState.tracks[track].steps[step];
    switch (st.overrideMode)
    {
        case StepOverrideMode::inherit:  st.overrideMode = StepOverrideMode::forceOn; break;
        case StepOverrideMode::forceOn:  st.overrideMode = StepOverrideMode::forceOff; break;
        case StepOverrideMode::forceOff: st.overrideMode = StepOverrideMode::inherit; break;
    }
    st.active = (st.overrideMode == StepOverrideMode::forceOn);
    journal.append("step.override", voiceName(track) + " step " + juce::String(step + 1)
                   + " mode=" + juce::String((int)st.overrideMode));
    saveAutosave();
}

void GrooveEngine::selectStep(int track, int step)
{
    grooveState.selectedTrack = track;
    grooveState.selectedStep = step;
}

void GrooveEngine::setBaseParam(int track, Param p, float value)
{
    const juce::ScopedLock sl(stateLock);
    auto& v = grooveState.tracks[track].base;

    switch (p)
    {
        case Param::pitch:     v.pitchHz = value; break;
        case Param::decay:     v.decayMs = value; break;
        case Param::transient: v.transient = value; break;
        case Param::noise:     v.noise = value; break;
        case Param::filter:    v.filter = value; break;
        case Param::drive:     v.drive = value; break;
        case Param::space:     v.space = value; break;
        case Param::blend:     v.blend = value; break;
        default: break;
    }

    journal.append("param.base", voiceName(track) + "." + paramName(p) + "=" + juce::String(value));
    saveAutosave();
}


void GrooveEngine::setStepParam(int track, int step, Param p, float value, bool createLock)
{
    const juce::ScopedLock sl(stateLock);

    if (createLock)
    {
        grooveState.setLock(track, step, p, value);
        journal.append("param.lock.edit",
                       voiceName(track) + " step " + juce::String(step + 1) + " "
                       + paramName(p) + "=" + juce::String(value));
    }
    else
    {
        auto& v = grooveState.tracks[track].base;
        switch (p)
        {
            case Param::pitch:     v.pitchHz = value; break;
            case Param::decay:     v.decayMs = value; break;
            case Param::transient: v.transient = value; break;
            case Param::noise:     v.noise = value; break;
            case Param::filter:    v.filter = value; break;
            case Param::drive:     v.drive = value; break;
            case Param::space:     v.space = value; break;
            case Param::blend:     v.blend = value; break;
            default: break;
        }

        journal.append("param.base.edit",
                       voiceName(track) + " " + paramName(p) + "=" + juce::String(value));
    }

    pendingMidi.push_back({ 0, juce::MidiMessage::controllerEvent(1, ccForParam(p),
                                                                   paramToCcValue(p, value)) });

    saveAutosave();
}

void GrooveEngine::setVelocity(int track, int step, float value)
{
    const juce::ScopedLock sl(stateLock);
    grooveState.tracks[track].steps[step].velocity = juce::jlimit(0.0f, 1.2f, value);
    pendingMidi.push_back({ 0, juce::MidiMessage::controllerEvent(1, kCcExpression,
                                                                   toMidi7(grooveState.tracks[track].steps[step].velocity)) });
    pendingMidi.push_back({ 0, juce::MidiMessage::controllerEvent(1, kCcVolume,
                                                                   toMidi7(grooveState.tracks[track].velocity)) });
    journal.append("step.velocity", voiceName(track) + " step " + juce::String(step + 1)
                   + "=" + juce::String(value));
    saveAutosave();
}

void GrooveEngine::setProbability(int track, int step, float value)
{
    const juce::ScopedLock sl(stateLock);
    grooveState.tracks[track].steps[step].probability = juce::jlimit(0.0f, 1.0f, value);
    journal.append("step.probability", voiceName(track) + " step " + juce::String(step + 1)
                   + "=" + juce::String(value));
    saveAutosave();
}

void GrooveEngine::setStepMidiNote(int track, int step, int note)
{
    const juce::ScopedLock sl(stateLock);
    note = juce::jlimit(kUjamKitLow, kUjamKitHigh, note);
    grooveState.tracks[track].steps[step].midiNote = note;
    journal.append("step.midiNote", voiceName(track) + " step " + juce::String(step + 1)
                   + "=" + ujamKitName(note));
    saveAutosave();
}

void GrooveEngine::setTrackMidiNote(int track, int note)
{
    const juce::ScopedLock sl(stateLock);
    track = juce::jlimit(0, kTracks - 1, track);
    note = juce::jlimit(kUjamKitLow, kUjamKitHigh, note);
    grooveState.tracks[track].midiNote = note;
    journal.append("track.midiNote", voiceName(track) + "=" + ujamKitName(note));
    saveAutosave();
}

void GrooveEngine::toggleMute(int track)
{
    const juce::ScopedLock sl(stateLock);
    auto& tr = grooveState.tracks[juce::jlimit(0, kTracks - 1, track)];
    tr.muted = ! tr.muted;
    journal.append("track.mute", voiceName(track) + (tr.muted ? " mute" : " unmute"));
    saveAutosave();
}

void GrooveEngine::toggleSolo(int track)
{
    const juce::ScopedLock sl(stateLock);
    auto& tr = grooveState.tracks[juce::jlimit(0, kTracks - 1, track)];
    tr.soloed = ! tr.soloed;
    journal.append("track.solo", voiceName(track) + (tr.soloed ? " solo" : " unsolo"));
    saveAutosave();
}

int GrooveEngine::effectiveMidiNote(int track, int step) const
{
    const juce::ScopedLock sl(stateLock);
    return grooveState.effectiveMidiNote(track, step);
}

bool GrooveEngine::trackIsAudible(int track) const
{
    const juce::ScopedLock sl(stateLock);
    return grooveState.trackIsAudible(track);
}

void GrooveEngine::setRatchet(int track, int step, int repeats)
{
    const juce::ScopedLock sl(stateLock);
    grooveState.tracks[track].steps[step].ratchet = juce::jlimit(1, 4, repeats);
    journal.append("step.ratchet", voiceName(track) + " step " + juce::String(step + 1)
                   + "=" + juce::String(repeats));
    saveAutosave();
}

void GrooveEngine::setTrackSteps(int track, int steps)
{
    const juce::ScopedLock sl(stateLock);
    auto& tr = grooveState.tracks[track];
    tr.generatorSteps = juce::jlimit(1, kSteps, steps);
    tr.pulses = juce::jlimit(0, tr.generatorSteps, tr.pulses);
    for (auto& st : tr.steps)
    {
        st.overrideMode = StepOverrideMode::inherit;
        st.active = false;
    }
    journal.append("track.steps", voiceName(track) + "=" + juce::String(tr.generatorSteps));
    saveAutosave();
}

void GrooveEngine::setTrackPulses(int track, int pulses)
{
    const juce::ScopedLock sl(stateLock);
    auto& tr = grooveState.tracks[track];
    tr.pulses = juce::jlimit(0, tr.generatorSteps, pulses);
    for (auto& st : tr.steps)
    {
        st.overrideMode = StepOverrideMode::inherit;
        st.active = false;
    }
    journal.append("track.pulses", voiceName(track) + "=" + juce::String(tr.pulses));
    saveAutosave();
}

void GrooveEngine::setTrackRotate(int track, int rotate)
{
    const juce::ScopedLock sl(stateLock);
    auto& tr = grooveState.tracks[track];
    tr.rotate = rotate;
    for (auto& st : tr.steps)
    {
        st.overrideMode = StepOverrideMode::inherit;
        st.active = false;
    }
    journal.append("track.rotate", voiceName(track) + "=" + juce::String(rotate));
    saveAutosave();
}

void GrooveEngine::setPulseEnabled(int track, int step, bool shouldPlay)
{
    const juce::ScopedLock sl(stateLock);
    auto& tr = grooveState.tracks[juce::jlimit(0, kTracks - 1, track)];
    step = juce::jlimit(0, kSteps - 1, step);
    auto& st = tr.steps[step];

    if (step >= tr.generatorSteps)
    {
        const int old = juce::jlimit(1, kSteps, tr.generatorSteps);
        for (int s = 0; s < old; ++s)
        {
            auto& baked = tr.steps[s];
            if (baked.overrideMode != StepOverrideMode::inherit)
                continue;
            const bool hit = Sequencer::euclideanHit(s, old, tr.pulses, tr.rotate);
            baked.overrideMode = hit ? StepOverrideMode::forceOn : StepOverrideMode::forceOff;
            baked.active = hit;
        }
        for (int s = old; s <= step; ++s)
        {
            tr.steps[s].overrideMode = StepOverrideMode::forceOff;
            tr.steps[s].active = false;
        }
        tr.generatorSteps = step + 1;
    }

    if (shouldPlay)
    {
        st.overrideMode = StepOverrideMode::forceOn;
        st.active = true;
    }
    else
    {
        st.overrideMode = StepOverrideMode::forceOff;
        st.active = false;
    }

    journal.append("step.pulse", voiceName(track) + " step " + juce::String(step + 1)
                   + (shouldPlay ? " on" : " off"));
    saveAutosave();
}

void GrooveEngine::setTrackDivision(int track, float division)
{
    const juce::ScopedLock sl(stateLock);
    const float choices[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
    float best = choices[0], dist = std::abs(division - choices[0]);
    for (float c : choices)
        if (std::abs(division - c) < dist) { best = c; dist = std::abs(division - c); }
    grooveState.tracks[track].division = best;
    journal.append("track.division", voiceName(track) + "=" + juce::String(best));
    saveAutosave();
}

void GrooveEngine::setTrackProbability(int track, float value)
{
    const juce::ScopedLock sl(stateLock);
    grooveState.tracks[track].probability = juce::jlimit(0.0f, 1.0f, value);
    journal.append("track.probability", voiceName(track) + "=" + juce::String(value));
    saveAutosave();
}

void GrooveEngine::setTrackVelocity(int track, float value)
{
    const juce::ScopedLock sl(stateLock);
    grooveState.tracks[track].velocity = juce::jlimit(0.0f, 1.2f, value);
    pendingMidi.push_back({ 0, juce::MidiMessage::controllerEvent(1, kCcVolume,
                                                                   toMidi7(grooveState.tracks[track].velocity)) });
    journal.append("track.velocity", voiceName(track) + "=" + juce::String(value));
    saveAutosave();
}

void GrooveEngine::setLockFromBase(int track, int step, Param p)
{
    const juce::ScopedLock sl(stateLock);
    const auto b = grooveState.tracks[track].base;
    float value = 0.0f;

    switch (p)
    {
        case Param::pitch:     value = b.pitchHz; break;
        case Param::decay:     value = b.decayMs; break;
        case Param::transient: value = b.transient; break;
        case Param::noise:     value = b.noise; break;
        case Param::filter:    value = b.filter; break;
        case Param::drive:     value = b.drive; break;
        case Param::space:     value = b.space; break;
        case Param::blend:     value = b.blend; break;
        default: break;
    }

    grooveState.setLock(track, step, p, value);
    journal.append("param.lock", voiceName(track) + " step " + juce::String(step + 1) + " " + paramName(p) + "=" + juce::String(value));
    saveAutosave();
}

void GrooveEngine::clearLock(int track, int step, Param p)
{
    const juce::ScopedLock sl(stateLock);
    grooveState.clearLock(track, step, p);
    journal.append("param.unlock", voiceName(track) + " step " + juce::String(step + 1) + " " + paramName(p));
    saveAutosave();
}

void GrooveEngine::clearAllLocks(int track, int step)
{
    const juce::ScopedLock sl(stateLock);
    grooveState.clearAllLocks(track, step);
    journal.append("param.unlockAll", voiceName(track) + " step " + juce::String(step + 1));
    saveAutosave();
}


void GrooveEngine::setStepRole(int track, int step, StepRole role)
{
    const juce::ScopedLock sl(stateLock);
    grooveState.tracks[track].steps[step].role = role;
    journal.append("step.role", voiceName(track) + " step " + juce::String(step + 1)
                   + "=" + juce::String((int) role));
    saveAutosave();
}

void GrooveEngine::setTrackEvolutionPolicy(int track, EvolutionPolicy policy)
{
    const juce::ScopedLock sl(stateLock);
    grooveState.tracks[track].evolutionPolicy = policy;
    journal.append("track.evolutionPolicy", voiceName(track) + "=" + juce::String((int) policy));
    saveAutosave();
}

void GrooveEngine::setTrackEvolveAmount(int track, float amount)
{
    const juce::ScopedLock sl(stateLock);
    grooveState.tracks[track].evolveAmount = juce::jlimit(0.0f, 1.0f, amount);
    journal.append("track.evolveAmount", voiceName(track) + "=" + juce::String(amount));
    saveAutosave();
}

bool GrooveEngine::isGeneratedHit(int track, int step) const
{
    const auto& tr = grooveState.tracks[juce::jlimit(0, kTracks - 1, track)];
    return Sequencer::euclideanHit(step, tr.generatorSteps, tr.pulses, tr.rotate);
}

bool GrooveEngine::isResolvedHit(int track, int step) const
{
    const auto& tr = grooveState.tracks[juce::jlimit(0, kTracks - 1, track)];
    return Sequencer::resolvedStepActive(tr, step);
}

void GrooveEngine::beginPerform()
{
    const juce::ScopedLock sl(stateLock);
    if (performBase.has_value()) return;
    performBase = grooveState;
    journal.append("perform.begin", "temporary layer active");
}

void GrooveEngine::endPerform(bool commit)
{
    const juce::ScopedLock sl(stateLock);
    if (!performBase.has_value()) return;

    if (commit)
    {
        ancestryGraph.capture(*performBase, "perform-parent");
        ancestryGraph.capture(grooveState, "perform-child");
        performBase.reset();
        journal.append("perform.commit", "captured temporary performance");
        saveAutosave();
    }
    else
    {
        grooveState = *performBase;
        performBase.reset();
        journal.append("perform.revert", "returned to base state");
        saveAutosave();
    }
}

EvolutionEngine::Result GrooveEngine::evolve(EvolutionEngine::Mode mode)
{
    const juce::ScopedLock sl(stateLock);

    // Capture the current state before mutation so Back is deterministic.
    ancestryGraph.capture(grooveState, "pre-evolve");

    auto result = evolution.evolve(grooveState, mode);

    juce::String detail = "changes=" + juce::String(result.appliedChanges);
    for (const auto& c : result.changes)
        detail += " | " + c.description;

    journal.append("evolve", detail);

    // Capture child so ancestry reflects actual branch endpoints.
    ancestryGraph.capture(grooveState, "child");
    saveAutosave();
    return result;
}

int GrooveEngine::capture(const juce::String& label)
{
    const juce::ScopedLock sl(stateLock);
    auto id = ancestryGraph.capture(grooveState, label);
    journal.append("capture", "node=" + juce::String(id) + " " + label);
    saveAutosave();
    return id;
}

bool GrooveEngine::back()
{
    const juce::ScopedLock sl(stateLock);
    if (! ancestryGraph.restoreLast(grooveState))
        return false;

    journal.append("ancestry.back", "node=" + juce::String(ancestryGraph.currentNodeId()));
    saveAutosave();
    return true;
}

bool GrooveEngine::importMidiFile(const juce::File& file, juce::String& error)
{
    juce::FileInputStream stream(file);
    if (! stream.openedOk())
    {
        error = "Could not open " + file.getFileName();
        return false;
    }

    juce::MidiFile midi;
    if (! midi.readFrom(stream, true))
    {
        error = "Not a MIDI file: " + file.getFileName();
        return false;
    }

    const int ppq = midi.getTimeFormat();
    if (ppq <= 0)
    {
        error = "Unsupported MIDI time format";
        return false;
    }

    struct Cell
    {
        bool on = false;
        float velocity = 1.0f;
        int note = 0;
        int ratchet = 0;
    };
    std::array<std::array<Cell, kSteps>, kTracks> grid {};
    int lastStep = -1;

    for (int t = 0; t < midi.getNumTracks(); ++t)
    {
        const auto* seq = midi.getTrack(t);
        if (seq == nullptr)
            continue;

        for (int i = 0; i < seq->getNumEvents(); ++i)
        {
            const auto* ev = seq->getEventPointer(i);
            if (ev == nullptr)
                continue;
            const auto& msg = ev->message;
            if (! msg.isNoteOn() || msg.getVelocity() <= 0)
                continue;

            const int note = msg.getNoteNumber();
            const int track = trackIndexForUjamNote(note);
            if (track < 0)
                continue;

            const double ticks = seq->getEventTime(i);
            const int step = (int) std::round(ticks / (double) ppq * 4.0);
            if (step < 0 || step >= kSteps)
                continue;

            auto& cell = grid[(size_t) track][(size_t) step];
            const float vel = juce::jlimit(0.05f, 1.2f, msg.getVelocity() / 127.0f);
            if (! cell.on)
            {
                cell.on = true;
                cell.velocity = vel;
                cell.note = note;
                cell.ratchet = 1;
            }
            else
            {
                cell.ratchet = juce::jmin(4, cell.ratchet + 1);
                cell.velocity = juce::jmax(cell.velocity, vel);
            }
            lastStep = juce::jmax(lastStep, step);
        }
    }

    if (lastStep < 0)
    {
        error = "No UJAM kit hits in that MIDI (need notes C1–D#2)";
        return false;
    }

    const int length = juce::jlimit(16, kSteps, ((lastStep / 16) + 1) * 16);
    int totalHits = 0;

    {
        const juce::ScopedLock sl(stateLock);
        for (int trk = 0; trk < kTracks; ++trk)
        {
            auto& tr = grooveState.tracks[trk];
            tr.generatorSteps = length;
            tr.rotate = 0;
            tr.division = 1.0f;
            int pulses = 0;
            int commonNote = midiNoteForTrack(trk);
            int commonCount = 0;

            for (int s = 0; s < kSteps; ++s)
            {
                auto& st = tr.steps[s];
                const auto& cell = grid[(size_t) trk][(size_t) s];
                if (s < length && cell.on)
                {
                    st.overrideMode = StepOverrideMode::forceOn;
                    st.active = true;
                    st.velocity = cell.velocity;
                    st.ratchet = juce::jmax(1, cell.ratchet);
                    st.midiNote = cell.note;
                    ++pulses;
                    ++totalHits;
                    if (cell.note == commonNote)
                        ++commonCount;
                    else if (commonCount == 0)
                    {
                        commonNote = cell.note;
                        commonCount = 1;
                    }
                }
                else
                {
                    st.overrideMode = StepOverrideMode::forceOff;
                    st.active = false;
                    st.ratchet = 1;
                    if (s < length)
                        st.midiNote.reset();
                }
            }
            tr.pulses = pulses;
            if (pulses > 0)
                tr.midiNote = commonNote;
        }
        sequencer.reset();
        pendingMidi.clear();
        pendingAllNotesOff.store(true);
    }

    journal.append("midi.import", file.getFileName() + " · " + juce::String(totalHits)
                   + " hits · " + juce::String(length) + " steps");
    saveAutosave();
    error.clear();
    return true;
}

void GrooveEngine::saveAutosave()
{
    if (performBase.has_value()) return;
    const auto json = juce::JSON::toString(grooveState.toVar(), true);
    journal.getStateFile().replaceWithText(json);
}

bool GrooveEngine::loadAutosave()
{
    auto f = journal.getStateFile();
    if (! f.existsAsFile())
        return false;

    auto parsed = juce::JSON::parse(f);
    if (parsed.isVoid())
        return false;

    return grooveState.fromVar(parsed);
}
}