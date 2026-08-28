# Groove Lab Native v0.7 — T-1 Recreation + Per-Step Sound v0.7 — T-1-Style Algorithmic Recreation

This is the consolidated source tree. Build this version, not v0.1-v0.5.

## Major architectural change

The sequencer no longer treats the 32-step matrix as the primary pattern. Each track now generates rhythm from:

**Steps → Pulses → Rotate → Division → manual override → probability → velocity → ratchet → parameter locks → drum synth**

That is the core algorithmic sequencing model described in the recreation manual.

### Per-track generator
- Steps: 1-32
- Pulses: 0-Steps
- Rotate
- Division: 1/4x, 1/2x, 1x, 2x, 4x
- Track probability
- Track velocity
- Independent track cycle length

### Grid overrides
Click a step repeatedly to cycle:

1. **INHERIT** — use the generated Euclidean result
2. **FORCE ON** — manual hit overrides the generator
3. **FORCE OFF** — suppress a generated hit

Shift-click selects a step without changing the override.

### Space bar transport
**SPACE = Play / Stop**

The main component explicitly takes keyboard focus and handles `juce::KeyPress::spaceKey`.

### PERFORM / TEMP
`PERFORM` snapshots the current groove and starts a temporary editing layer. Change Steps, Pulses, Rotate, sound, probability, evolution, etc.

- click PERFORM again → discard the temporary state and return to the base groove
- click CAPTURE TEMP → keep it and capture parent/child ancestry nodes

Temporary edits are not written to autosave until committed.

### Evolution is generator-aware
- SPARSE reduces Pulses
- DENSE increases Pulses
- ROTATE changes Euclidean rotation
- HUMAN varies event dynamics/probability
- SOUND mutates synthesized drum timbre

Protected tracks / anchors / surprise budget / similarity remain available.

## Build on macOS

Prerequisites:

```bash
xcode-select --install
brew install cmake
```

Configure and build:

```bash
cd GrooveLabNative_v0.7_T1_RECREATION
cmake -S . -B build -G Xcode
cmake --build build --config Release --target GrooveLab
open build/GrooveLab.xcodeproj
```

Or double-click `build_mac.command`.

## Targets
- Standalone macOS app
- AU instrument
- VST3 instrument

## Current limitations

This pass implements the important generator architecture, manual overrides, probability/velocity, ratchets, parameter locks, synthetic drums, evolution, ancestry, TEMP/PERFORM, and space-bar transport.

Swing/microtiming, modulation lanes, pitch/scale sequencing, MIDI output, conditional triggers, parent-child morph, and host transport sync are intentionally left for the next passes.

## Validation status

Static source checks were run in the generation environment. A true JUCE/CoreAudio/Xcode compile still needs to happen on your Mac because this environment is not macOS.


## v0.7: explicit per-step sound editing

The SOUND EDIT selector now has two modes:

- STEP — default. Select any step and turn BODY, DECAY, TRANSIENT, TEXTURE, FILTER, DRIVE, SPACE, or BLEND. The changed value is stored as a parameter lock on that exact step.
- VOICE — edits the selected drum track's base synthesized sound.

A step with one or more sound locks is marked in the grid. `CLEAR STEP SOUND` removes all sound locks from the selected step so it follows the track's base voice again.

### Per-step workflow

1. Click a step.
2. Leave SOUND EDIT on STEP.
3. Turn any of the eight sound controls.
4. Press AUDITION to hear the selected step.
5. Repeat on any other step.
6. Use CLEAR STEP SOUND to remove the overrides.

SPACE remains global Play/Stop.
