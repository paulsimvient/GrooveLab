# Lil God Projector v2.5 — Unified Track Model

This build consolidates track selection and Euclidean editing.

- One persistent selected target across GRID, EUC, and NOTES.
- Shared target list: KICK, SNARE, CLAP, CHH, OHH, PERC1, PERC2, FX, MOOG, PROPHET, KEYS.
- The EUC page is the only Euclidean editor. Piano Roll Euclidean controls were removed.
- Every target has independent Steps, Pulses, and Rotate settings.
- Drums support STEP / EUCLID / HYBRID.
- Melodic tracks now support STEP / EUCLID / HYBRID gating in the same EUC window.
- In melodic HYBRID mode, clicking a Euclidean pad toggles a gate override; double-clicking a pure-EUCLID pad promotes it to HYBRID.
- Selecting a drum or melodic track in EUC carries that target into the other views.
- Mute keyboard shortcut follows the unified selected target.

Build on macOS with `./build_mac.command`.
