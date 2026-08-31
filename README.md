# Lil God Projector v1.8

- TAP button + T-key tap tempo.
- Visible instrument-assignment/browser references removed.
- Channel reverb uses installed UAD Capitol Chambers when found (AU/VST3), one hosted instance per channel.
- Existing project reverb on/off state remains stored per channel.

# Lil God Projector v1.6.1

JUCE 8.0.4 compile fix for PianoRoll.cpp: juce::Colour is not constexpr, so UI colour constants now use const juce::Colour.

The build script uses a fresh ~/LilGodProjector-v1.6.1-build directory.

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

## v0.7 per-sound Euclidean + Param Lock update

Each drum sound now owns its rhythm mode and Euclidean shape independently. Select KICK, SNARE, CLAP, CHH, OHH, PERC1, PERC2, or FX on the EUCLIDEAN page and choose STEP, EUCLID, or HYBRID. The selected sound keeps its own Steps, Pulses, Rotate, and Division values.

- STEP: manual hits only.
- EUCLID: pure Euclidean generator for that sound.
- HYBRID: Euclidean base pattern plus manual force-on/force-off edits. Editing a step while in EUCLID automatically promotes that sound to HYBRID so the edit is retained.
- Step sound locks remain independent of rhythm generation. LP/HP/delay and sound parameter edits on locked steps are stored per step and recalled when that step plays.

## v0.8 song-timeline MIDI fix
MIDI lane recording now follows the actual current song-section timeline (bars x meter), uses incoming MIDI sample offsets for placement, and no longer wraps at the 32-step drum sequencer length. MIDI lane display/editing scales to the selected section length. Drum/Euclidean tracks remain independent.


## v1.2 Channel FX
Each instrument channel now has independent Delay and Reverb. Delay Time/Feedback/Wet and Reverb Size/Decay/Wet are saved with the project; these parameters also support per-step Param Locks.

## v1.3 Arturia / Live MIDI Fix

Live MIDI now preserves the controller's note number and velocity and routes it only to the currently selected instrument. Selecting a mixer channel no longer injects a test note (including MIDI note 36/kick), and live controller input is not echoed back to hardware MIDI OUT. The status line shows note name, MIDI note number, velocity, and destination for live diagnosis.

## v1.5 Euclidean interaction fix

In pure EUCLID mode, generated hits are now full-opacity and remain editable. A single click selects a Euclidean step for velocity/probability/ratchet/note/Param Lock editing without changing the generated rhythm. Double-clicking a step is treated as an explicit placement edit: the track switches to HYBRID and the clicked hit toggles immediately. STEP and HYBRID retain their normal single-click placement editing.


## v1.5 Euclidean mode fix
- Editing Steps, Pulses, or Rotate on the Euclidean page automatically promotes STEP to EUCLID.
- HYBRID is preserved when editing generator controls.
- STEP mode no longer draws misleading Euclidean ghost hits.
- EUCLID/HYBRID generated hits remain selectable for step editing and parameter locks.


## v1.8.3 UADx fix
Capitol Chambers auto-loading now explicitly prefers/requires the native UADx build and rejects identifiable legacy UAD/UAD-2 DSP variants. VST3 is preferred over AU when both native formats are present.
