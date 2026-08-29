#include "GrooveEngine.h"
#include "DrumMidi.h"
#include <algorithm>
#include <cmath>

namespace groove
{
GrooveEngine::GrooveEngine()
{
    loadAutosave();
    saveAutosave();
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

void GrooveEngine::cancelPendingNoteOff(int channel, int note)
{
    pendingMidi.erase(std::remove_if(pendingMidi.begin(), pendingMidi.end(),
        [channel, note](const PendingMidi& event)
        {
            return event.message.isNoteOff()
                && event.message.getChannel() == channel
                && event.message.getNoteNumber() == note;
        }), pendingMidi.end());
}

void GrooveEngine::dropNoteOffs(juce::MidiBuffer& midiOut, int channel, int note)
{
    juce::MidiBuffer kept;
    for (const auto metadata : midiOut)
    {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOff() && msg.getChannel() == channel && msg.getNoteNumber() == note)
            continue;
        kept.addEvent(msg, metadata.samplePosition);
    }
    midiOut.swapWith(kept);
}

void GrooveEngine::emitAllControllers(juce::MidiBuffer& midiOut, int track, int step,
                                      int sample, int blockSamples)
{
    // Sound CCs are plugin-global. Only snare may set them so hats do not choke the tail.
    if (! isSnareHit(track, grooveState.effectiveMidiNote(track, step)))
        return;

    const auto params = grooveState.effectiveParams(track, step);
    const int channel = 1;
    for (int i = 0; i < paramCount; ++i)
    {
        const auto p = (Param) i;
        float value = paramValue(params, p);
        if (p == Param::decay)
            value = juce::jmax(value, 1400.0f);
        scheduleMidi(midiOut,
                     juce::MidiMessage::controllerEvent(channel, ccForParam(p),
                                                        paramToCcValue(p, value)),
                     sample, blockSamples);
    }
}

void GrooveEngine::emitTriggerMidi(juce::MidiBuffer& midiOut, const Sequencer::Trigger& tr, int blockSamples)
{
    const int note = grooveState.effectiveMidiNote(tr.track, tr.step);
    const bool snare = isSnareHit(tr.track, note);
    float hitVel = tr.velocity;
    if (snare)
    {
        // Sit the snare on top of the kit. Ghosts stay below backbeats but still speak.
        if (hitVel >= 0.40f)
            hitVel = 1.2f;
        else
            hitVel = juce::jmax(0.72f, hitVel * 1.6f);
    }
    const int vel = juce::jlimit(1, 127, (int) std::round(hitVel * 127.0f));
    const int channel = 1;
    const int reps = juce::jmax(1, tr.ratchet);
    const auto division = juce::jlimit(0.25f, 4.0f, grooveState.tracks[tr.track].division);
    const int stepSamples = juce::jmax(1,
        (int) (currentSampleRate * 60.0 / juce::jmax(1.0, grooveState.bpm) / 4.0 / division));
    const int spacing = juce::jmax(1, stepSamples / reps);
    const float holdMs = snare ? 1800.0f
                               : juce::jmax(450.0f, grooveState.effectiveParams(tr.track, tr.step).decayMs);
    const int noteLen = (tr.track == 3)
        ? juce::jmax(64, spacing / 2)
        : juce::jmax((int) (currentSampleRate * holdMs / 1000.0), snare ? spacing * 8 : spacing * 2);

    for (int r = 0; r < reps; ++r)
    {
        const int onAt = tr.sampleOffset + r * spacing;
        if (snare)
        {
            dropNoteOffs(midiOut, channel, note);
            cancelPendingNoteOff(channel, note);
            emitAllControllers(midiOut, tr.track, tr.step, onAt, blockSamples);
            scheduleMidi(midiOut, juce::MidiMessage::controllerEvent(channel, kCcVolume, 127),
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
        advanceSong(n);
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
    songSamplesInSection = 0.0;
    grooveState.applySongSection(0);
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
    if (isSnareHit(track, grooveState.effectiveMidiNote(track, step)))
    {
        pendingMidi.push_back({ 0, juce::MidiMessage::controllerEvent(1, kCcExpression,
                                                                       toMidi7(juce::jmax(0.85f, value))) });
        pendingMidi.push_back({ 0, juce::MidiMessage::controllerEvent(1, kCcVolume, 127) });
    }
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
    syncCurrentSongSection();
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
    syncCurrentSongSection();
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
    syncCurrentSongSection();
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
    syncCurrentSongSection();
    saveAutosave();
}

void GrooveEngine::setTrackProbability(int track, float value)
{
    const juce::ScopedLock sl(stateLock);
    grooveState.tracks[track].probability = juce::jlimit(0.0f, 1.0f, value);
    journal.append("track.probability", voiceName(track) + "=" + juce::String(value));
    syncCurrentSongSection();
    saveAutosave();
}

void GrooveEngine::setTrackVelocity(int track, float value)
{
    const juce::ScopedLock sl(stateLock);
    grooveState.tracks[track].velocity = juce::jlimit(0.0f, 1.2f, value);
    if (track == 1)
        pendingMidi.push_back({ 0, juce::MidiMessage::controllerEvent(1, kCcVolume,
                                                                       toMidi7(juce::jmax(0.85f, grooveState.tracks[track].velocity))) });
    journal.append("track.velocity", voiceName(track) + "=" + juce::String(value));
    syncCurrentSongSection();
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
            float vel = juce::jlimit(0.05f, 1.2f, msg.getVelocity() / 127.0f);
            if (track == 1)
                vel = (vel >= 0.40f) ? juce::jmax(vel, 1.0f) : juce::jmax(vel, 0.65f);
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

void GrooveEngine::syncCurrentSongSection()
{
    grooveState.captureLiveToCurrentSection();
}

void GrooveEngine::advanceSong(int numSamples)
{
    auto& song = grooveState.song;
    if (! song.follow || song.sections.empty())
        return;

    song.current = juce::jlimit(0, (int) song.sections.size() - 1, song.current);
    const double samplesPerBar = currentSampleRate * 60.0 / juce::jmax(1.0, grooveState.bpm)
        * meterQuarterNotesPerBar(grooveState.meter);
    const double sectionSamples = samplesPerBar * (double) juce::jmax(1, song.sections[(size_t) song.current].bars);
    songSamplesInSection += (double) numSamples;
    if (songSamplesInSection < sectionSamples)
        return;

    songSamplesInSection = std::fmod(songSamplesInSection, sectionSamples);
    const int next = (song.current + 1) % (int) song.sections.size();
    grooveState.applySongSection(next);
}

int GrooveEngine::addSongSection(SongPart part)
{
    const juce::ScopedLock sl(stateLock);
    SongSection section;
    section.part = part;
    section.bars = (part == SongPart::chorus ? 8 : part == SongPart::fill ? 1 : 4);
    section.meter = grooveState.meter;
    for (int t = 0; t < kTracks; ++t)
        section.shapes[(size_t) t] = TrackShape::fromTrack(grooveState.tracks[t]);
    grooveState.song.sections.push_back(section);
    const int index = (int) grooveState.song.sections.size() - 1;
    grooveState.applySongSection(index);
    songSamplesInSection = 0.0;
    saveAutosave();
    return index;
}

void GrooveEngine::removeSongSection(int index)
{
    const juce::ScopedLock sl(stateLock);
    auto& sections = grooveState.song.sections;
    if (index < 0 || index >= (int) sections.size() || sections.size() <= 1)
        return;
    sections.erase(sections.begin() + index);
    grooveState.applySongSection(juce::jmin(index, (int) sections.size() - 1));
    songSamplesInSection = 0.0;
    saveAutosave();
}

void GrooveEngine::duplicateSongSection(int index)
{
    const juce::ScopedLock sl(stateLock);
    auto& sections = grooveState.song.sections;
    if (index < 0 || index >= (int) sections.size())
        return;
    sections.insert(sections.begin() + index + 1, sections[(size_t) index]);
    grooveState.applySongSection(index + 1);
    songSamplesInSection = 0.0;
    saveAutosave();
}

void GrooveEngine::selectSongSection(int index)
{
    const juce::ScopedLock sl(stateLock);
    if (grooveState.song.sections.empty())
        return;
    grooveState.applySongSection(index);
    songSamplesInSection = 0.0;
    saveAutosave();
}

void GrooveEngine::moveSongSection(int from, int to)
{
    const juce::ScopedLock sl(stateLock);
    auto& sections = grooveState.song.sections;
    const int n = (int) sections.size();
    if (from < 0 || from >= n || n <= 1)
        return;
    to = juce::jlimit(0, n, to);
    if (from == to || from + 1 == to)
        return;

    SongSection moved = std::move(sections[(size_t) from]);
    sections.erase(sections.begin() + from);
    int dest = to;
    if (to > from)
        dest -= 1;
    dest = juce::jlimit(0, (int) sections.size(), dest);
    sections.insert(sections.begin() + dest, std::move(moved));

    int current = grooveState.song.current;
    if (current == from)
        current = dest;
    else
    {
        if (from < current)
            --current;
        if (dest <= current)
            ++current;
        current = juce::jlimit(0, (int) sections.size() - 1, current);
    }
    grooveState.song.current = current;
    saveAutosave();
}

void GrooveEngine::setSongSectionBars(int index, int bars)
{
    const juce::ScopedLock sl(stateLock);
    if (index < 0 || index >= (int) grooveState.song.sections.size())
        return;
    grooveState.song.sections[(size_t) index].bars = juce::jlimit(1, 32, bars);
    saveAutosave();
}

void GrooveEngine::setMeter(Meter meter)
{
    const juce::ScopedLock sl(stateLock);
    const int oldSpb = juce::jmax(1, meterStepsPerBar(grooveState.meter));
    grooveState.meter = meter;
    const int spb = juce::jmax(1, meterStepsPerBar(meter));
    for (auto& tr : grooveState.tracks)
    {
        int bars = juce::jmax(1, (tr.generatorSteps + oldSpb / 2) / oldSpb);
        int next = bars * spb;
        if (next > kSteps)
            next = spb;
        tr.generatorSteps = juce::jlimit(1, kSteps, next);
        tr.pulses = juce::jlimit(0, tr.generatorSteps, tr.pulses);
        for (auto& st : tr.steps)
        {
            st.overrideMode = StepOverrideMode::inherit;
            st.active = false;
        }
    }
    if (! grooveState.song.sections.empty())
    {
        const int i = juce::jlimit(0, (int) grooveState.song.sections.size() - 1,
                                   grooveState.song.current);
        grooveState.song.sections[(size_t) i].meter = meter;
        grooveState.captureLiveToCurrentSection();
    }
    saveAutosave();
}

void GrooveEngine::setSectionTrackShape(int section, int track, const TrackShape& shape)
{
    const juce::ScopedLock sl(stateLock);
    auto& sections = grooveState.song.sections;
    if (section < 0 || section >= (int) sections.size()
        || track < 0 || track >= kTracks)
        return;

    auto& sh = sections[(size_t) section].shapes[(size_t) track];
    const int steps = juce::jlimit(1, kSteps, shape.generatorSteps);
    const bool rhythmChanged = sh.generatorSteps != steps
        || sh.pulses != shape.pulses
        || sh.rotate != shape.rotate;

    sh.generatorSteps = steps;
    sh.pulses = juce::jlimit(0, sh.generatorSteps, shape.pulses);
    sh.rotate = shape.rotate;
    sh.division = shape.division;
    sh.probability = juce::jlimit(0.0f, 1.0f, shape.probability);
    sh.velocity = juce::jlimit(0.0f, 1.2f, shape.velocity);

    if (grooveState.song.current == section)
    {
        auto& tr = grooveState.tracks[track];
        tr.generatorSteps = sh.generatorSteps;
        tr.pulses = sh.pulses;
        tr.rotate = sh.rotate;
        tr.division = sh.division;
        tr.probability = sh.probability;
        tr.velocity = sh.velocity;
        if (rhythmChanged)
        {
            for (auto& st : tr.steps)
            {
                st.overrideMode = StepOverrideMode::inherit;
                st.active = false;
            }
        }
    }
    saveAutosave();
}

void GrooveEngine::setSongFollow(bool shouldFollow)
{
    const juce::ScopedLock sl(stateLock);
    grooveState.song.follow = shouldFollow;
    saveAutosave();
}

int GrooveEngine::songBarInSection() const
{
    const juce::ScopedLock sl(stateLock);
    const double samplesPerBar = currentSampleRate * 60.0 / juce::jmax(1.0, grooveState.bpm)
        * meterQuarterNotesPerBar(grooveState.meter);
    return 1 + (int) (songSamplesInSection / juce::jmax(1.0, samplesPerBar));
}

double GrooveEngine::songSectionProgress() const
{
    const juce::ScopedLock sl(stateLock);
    const auto& song = grooveState.song;
    if (song.sections.empty())
        return 0.0;
    const int i = juce::jlimit(0, (int) song.sections.size() - 1, song.current);
    const double samplesPerBar = currentSampleRate * 60.0 / juce::jmax(1.0, grooveState.bpm)
        * meterQuarterNotesPerBar(grooveState.meter);
    const double total = samplesPerBar * (double) juce::jmax(1, song.sections[(size_t) i].bars);
    return juce::jlimit(0.0, 1.0, songSamplesInSection / juce::jmax(1.0, total));
}

void GrooveEngine::saveAutosave()
{
    if (performBase.has_value()) return;
    juce::String error;
    writeDocumentToFile(journal.getStateFile(), error);
}

bool GrooveEngine::loadAutosave()
{
    juce::String error;
    return readDocumentFromFile(journal.getStateFile(), error);
}

juce::String GrooveEngine::legalGrooveName(const juce::String& name)
{
    auto n = name.trim();
    if (n.isEmpty())
        n = "Lil God Projector";
    n = juce::File::createLegalFileName(n);
    if (n.isEmpty())
        n = "Lil God Projector";
    return n;
}

juce::File GrooveEngine::groovesDir() const
{
    return journal.getGroovesDir();
}

juce::var GrooveEngine::documentToVar() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty("formatVersion", 9);
    root->setProperty("groove", grooveState.toVar());
    root->setProperty("ancestry", ancestryGraph.toVar());
    return juce::var(root);
}

bool GrooveEngine::documentFromVar(const juce::var& v)
{
    auto* root = v.getDynamicObject();
    if (root == nullptr)
        return false;

    auto grooveVar = root->getProperty("groove");
    if (grooveVar.getDynamicObject() != nullptr)
    {
        if (! grooveState.fromVar(grooveVar))
            return false;
        ancestryGraph.fromVar(root->getProperty("ancestry"));
        return true;
    }

    ancestryGraph = {};
    return grooveState.fromVar(v);
}

bool GrooveEngine::writeDocumentToFile(const juce::File& file, juce::String& error)
{
    const auto doc = documentToVar();
    if (! file.getParentDirectory().createDirectory())
    {
        error = "Could not create " + file.getParentDirectory().getFullPathName();
        return false;
    }
    if (! file.replaceWithText(juce::JSON::toString(doc, true)))
    {
        error = "Could not write " + file.getFileName();
        return false;
    }
    error.clear();
    return true;
}

bool GrooveEngine::readDocumentFromFile(const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "Missing file: " + file.getFileName();
        return false;
    }
    auto parsed = juce::JSON::parse(file);
    if (parsed.isVoid())
    {
        error = "Not a Groove Lab file: " + file.getFileName();
        return false;
    }
    const juce::ScopedLock sl(stateLock);
    if (! documentFromVar(parsed))
    {
        error = "Could not parse " + file.getFileName();
        return false;
    }
    sequencer.reset();
    songSamplesInSection = 0.0;
    pendingMidi.clear();
    pendingAllNotesOff.store(true);
    error.clear();
    return true;
}

juce::Array<GrooveEngine::StoredGroove> GrooveEngine::listStoredGrooves() const
{
    juce::Array<StoredGroove> result;
    for (const auto& f : groovesDir().findChildFiles(juce::File::findFiles, false, "*.groove.json"))
    {
        StoredGroove item;
        item.file = f;
        item.name = f.getFileNameWithoutExtension().upToLastOccurrenceOf(".groove", false, false);
        if (item.name.isEmpty())
            item.name = f.getFileNameWithoutExtension();
        auto parsed = juce::JSON::parse(f);
        if (auto* root = parsed.getDynamicObject())
        {
            auto grooveVar = root->getProperty("groove");
            auto* g = grooveVar.getDynamicObject();
            if (g == nullptr) g = root;
            auto n = g->getProperty("name").toString();
            if (n.isNotEmpty())
                item.name = n;
        }
        result.add(item);
    }
    struct NameOrder
    {
        static int compareElements(const StoredGroove& a, const StoredGroove& b)
        {
            return a.name.compareIgnoreCase(b.name);
        }
    };
    NameOrder order;
    result.sort(order);
    return result;
}

bool GrooveEngine::saveStoredGroove(const juce::String& name, juce::String& error)
{
    const auto legal = legalGrooveName(name);
    {
        const juce::ScopedLock sl(stateLock);
        grooveState.name = legal;
        grooveState.captureLiveToCurrentSection();
    }
    const auto file = groovesDir().getChildFile(legal + ".groove.json");
    if (! writeDocumentToFile(file, error))
        return false;
    saveAutosave();
    journal.append("store.save", legal);
    return true;
}

bool GrooveEngine::loadStoredGroove(const juce::File& file, juce::String& error)
{
    if (! readDocumentFromFile(file, error))
        return false;
    saveAutosave();
    journal.append("store.load", file.getFileName());
    return true;
}

bool GrooveEngine::saveGrooveFile(const juce::File& file, juce::String& error)
{
    {
        const juce::ScopedLock sl(stateLock);
        auto stem = file.getFileNameWithoutExtension();
        if (stem.endsWithIgnoreCase(".groove"))
            stem = stem.upToLastOccurrenceOf(".groove", false, false);
        if (stem.isNotEmpty())
            grooveState.name = legalGrooveName(stem);
        else if (grooveState.name.isEmpty())
            grooveState.name = legalGrooveName(file.getFileNameWithoutExtension());
        grooveState.captureLiveToCurrentSection();
    }
    return writeDocumentToFile(file, error);
}

bool GrooveEngine::loadGrooveFile(const juce::File& file, juce::String& error)
{
    return loadStoredGroove(file, error);
}

void GrooveEngine::newProject()
{
    {
        const juce::ScopedLock sl(stateLock);
        const auto keepPlugin = grooveState.lastPluginPath;
        const int keepMode = grooveState.soundMode;
        const int keepProgram = grooveState.lastPluginProgram;
        const auto keepPatch = grooveState.lastPluginPatch;
        grooveState = GrooveState();
        grooveState.lastPluginPath = keepPlugin;
        grooveState.soundMode = keepMode;
        grooveState.lastPluginProgram = keepProgram;
        grooveState.lastPluginPatch = keepPatch;
        ancestryGraph = {};
        sequencer.reset();
        songSamplesInSection = 0.0;
        pendingMidi.clear();
        pendingAllNotesOff.store(true);
    }
    saveAutosave();
    journal.append("store.new", grooveState.name);
}

}