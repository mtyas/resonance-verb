# Changelog

## 0.9.0 (first public release)

### Engine
- Sympathetic string matrix: 64 voices × 48 modal lanes, 1–3 detuned unisons, stiff-string inharmonicity,
  frequency-dependent loss, SSE2/NEON vectorised, zero latency.
- Double decay (Weinreich), feed-forward Halo bridge coupling (unconditionally stable), soundboard Body and Bleed.
- Continuous half-pedal (CC64 and an automatable Pedal control), Freeze, pitch bend.
- Blur: modulated Thiran-allpass diffusion with an FDN tail.
- Input HP/LP, drive, sidechain excitation source, pre-delay; wet low cut and tilt, compressor, ducking, width.
- Tuning: A4 reference, five temperaments, octave stretch.

### Playing
- Strings modes: MIDI, MIDI Latch, Scale (22 scales and chords); on-screen 88-key keyboard showing live string energy.

### Workflow
- Undo / redo (gestures and preset loads; automation and MIDI CCs are not recorded).
- Preset browser: 17 factory presets, user presets (`.rvpreset`), categories, search, favourites, sorting,
  Save As / Rename / Delete (to Trash) / Import / Show Folder, modified indicator, host programs.
- MIDI learn on every control, mappings saved per session, plus a default mapping.

### Formats
- VST3, AU (macOS), CLAP, LV2 (Linux), Standalone; macOS universal binaries.
