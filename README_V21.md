# Lil God Projector v2.1 — Shared FX Bus

The mixer now uses a single post-fader shared effects return instead of per-track creative plug-in instances.

Each instrument channel has **FX ON/OFF** and **FX SEND**. The shared rack contains:

1. Paradise Guitar Studio
2. Century Tube Channel Strip
3. Compressor
4. Galaxy Tape Echo
5. Capitol Chambers

Drive/Sat and Ring Mod were removed from the visible UI and processing path. Legacy serialized fields remain only so older projects can still open safely.

The UAD effects are loaded as one shared instance each. Native/UADx builds are preferred and identifiable UAD-2/DSP paths are rejected. The compressor is a built-in JUCE DSP compressor on the shared return.

Build on macOS with:

```bash
chmod +x build_mac.command
./build_mac.command
```
