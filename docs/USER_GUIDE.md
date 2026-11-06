# Resonance Verb: User Guide

- [The idea](#the-idea)
- [Opening strings: MIDI, Latch and Scale](#opening-strings-midi-latch-and-scale)
- [Pedal, Freeze and the sidechain](#pedal-freeze-and-the-sidechain)
- [Control reference](#control-reference)
- [Presets](#presets)
- [Undo and redo](#undo-and-redo)
- [MIDI learn](#midi-learn)
- [Sending MIDI to the plugin in your DAW](#sending-midi-to-the-plugin-in-your-daw)
- [Tips](#tips)
- [Troubleshooting](#troubleshooting)

## The idea

Resonance Verb is a set of virtual piano strings with the dampers resting on them. Whatever you play through
it hits the soundboard. Strings whose damper is **lifted** ring in sympathy with any part of the sound that
matches their pitch or overtones. Damped strings stay silent. You decide which dampers are up.

The 88-key strip at the top shows this live: a **brass bar** under a key means that string is open, and the
amber glow shows how strongly it is ringing. You can also click the keys to open strings with the mouse.

## Opening strings: MIDI, Latch and Scale

**Mode** (Performance panel):

| Mode | Strings are open while… | Use it for |
|---|---|---|
| **MIDI** | a key is held | playing changes in real time |
| **MIDI Latch** | the last chord you played is latched. Releasing keys keeps it open; the next chord, started after all keys are up, replaces it, and notes added while keys are still down are added to it. | hands-free chords |
| **Scale** | the note is in the chosen **Scale** and **Root**, between **Low** and **High**. MIDI notes still add strings on top. | no MIDI at all; tuned drums; quick keys |

Scale mode includes 22 scales and chords, from *Chromatic (all strings)* to *Open Fifths*. At most 56 strings
stay open in Scale mode, which leaves room for MIDI notes and ringing tails.

## Pedal, Freeze and the sidechain

- **Pedal** (and MIDI **CC64**) is continuous, like a real sustain pedal. Below about 20 % the dampers grip
  normally; above about 80 % they are fully lifted and released strings keep ringing; in between you get
  **half-pedalling**, a partial damping. The plugin uses whichever is higher: the Pedal control or CC64.
- **Freeze** holds everything that's currently ringing (about a 5-minute decay) and stops new input from feeding
  the strings, so you can play over a frozen chord.
- **Sidechain**: set **Input › Source** to *Sidechain* (or *Main + Sidechain*) and route another track to the
  plugin's sidechain input. That track plays the strings while the main input passes through dry. If no
  sidechain is connected, the main input is used.

## Control reference

### Performance
| Control | Range (default) | What it does |
|---|---|---|
| Mode | MIDI / MIDI Latch / Scale (MIDI) | How strings are opened (see above) |
| Scale, Root | 22 scales, C–B (Major, C) | Scale mode's pitch set |
| Low, High | A0–C8 (C2, C6) | Scale mode's range |
| Pedal | 0–100 % (0) | Continuous sustain pedal / half-pedal |
| Freeze (header) | Off / On | Hold everything that's ringing |

### Input
| Control | Range (default) | What it does |
|---|---|---|
| High-Pass | 20–1000 Hz (40) | Removes rumble before it reaches the strings |
| Low-Pass | 1–20 kHz (14 k) | Tames sizzle before it reaches the strings |
| Drive | 0–24 dB (0) | Asymmetric saturation adds harmonics, so more strings get excited |
| Excite | −24…+24 dB (0) | How hard the input drives the strings |
| Source | Main / Sidechain / Both (Main) | What drives the strings |
| Pre-Delay (Space & Tone) | 0–250 ms (0) | Delays the strings' response; the dry signal is unaffected |

### Strings
| Control | Range (default) | What it does |
|---|---|---|
| Decay | 0.5–30 s (8) | Ring time (T60) of an open string around middle C; lower strings ring longer |
| Damper | 0.03–2 s (0.2) | How quickly a string dies when its damper falls |
| HF Damp | 0–100 % (35) | How much faster the upper partials decay |
| Bright | 0–100 % (50) | Balance of upper vs. lower partials |
| Detune | 0–10 ct (1.2) | Spread between unison strings, which sets how fast they beat (chorus) |
| Inharm. | 0–3× (1) | String stiffness. Above 1 sounds bell-like and metallic; 0 is perfectly harmonic |
| Velocity | 0–100 % (50) | How much MIDI velocity changes each string's level |
| Spread | 0–100 % (70) | Pans strings across the keyboard: bass left, treble right |
| Unison | 1 / 2 / 3 (3) | Maximum strings per note. Real pianos use 1 in the bass, 2 in the tenor and 3 above |

### Character
| Control | Range (default) | What it does |
|---|---|---|
| Halo | 0–100 % (30) | Bridge coupling: open strings excite each other (e.g. C4 sets C5 ringing). Always stable |
| Dbl Decay | 0–100 % (50) | Piano's two-stage decay: a quick initial drop, then a long, quiet aftersound |
| Body | 0–100 % (30) | Soundboard colouration of the strings |
| Body Size | 0–100 % (50) | Upright (small, higher modes) ↔ concert grand (large, lower modes) |
| Bleed | 0–100 % (0) | The input rings the soundboard directly, as if every damper were down |

### Tuning
| Control | Range (default) | What it does |
|---|---|---|
| A4 | 415–466 Hz (440) | Reference pitch |
| Stretch | 0–100 % (50) | Piano-style octave stretch (up to ±30 ct at the extremes) |
| Bend | 0–24 st (2) | Pitch-bend range; all strings bend together, glitch-free |
| Temperament, Temp. Root | Equal, Just, Pythagorean, ¼-comma Meantone, Werckmeister III | Historical tunings, anchored at A4 |

### Blur
| Control | Range (default) | What it does |
|---|---|---|
| Amount | 0–100 % (35) | Blend of plain strings ↔ diffused wash. 0 is an exact bypass |
| Size | 0–100 % (50) | Diffusion length. Changes glide smoothly |
| Tail | 0–10 s (1.5) | A short diffuse tail after the smear (0 = smear only) |
| Motion | 0–100 % (30) | Slow movement in the wash (under 1 cent of pitch drift at the default) |

### Space & Tone, Dynamics, Output
| Control | Range (default) | What it does |
|---|---|---|
| Low Cut | 20–1000 Hz (20 = off) | 12 dB/oct high-pass on the wet signal only |
| Tilt | −6…+6 dB (0) | Darker ↔ brighter wet signal (pivots at 800 Hz) |
| Duck | 0–100 % (0) | Turns the wet down by up to 24 dB while the dry signal is loud |
| Duck Rel. | 20–1000 ms (250) | How quickly the wet swells back in |
| Threshold, Ratio, Attack, Release | −24 dB, 3:1, 10 ms, 150 ms | Wet-only compressor (sympathetic resonance can be quiet and very dynamic) |
| Makeup | 0–36 dB (12) | Wet gain after compression |
| Width | 0–200 % (100) | Stereo width of the wet signal |
| Dry/Wet | 0–100 % (50) | Mix |
| Output | −24…+12 dB (0) | Final level |

Double-click any knob to reset it to its default.

## Presets

Click the preset name in the header to open the **browser**. **◀ / ▶** step through presets, and an asterisk (*)
means you've changed something since loading. Moving *Freeze* or *Pedal* doesn't count as a change.

- **Categories** on the left: All, Favourites, Factory, User, then each category.
- **Search** matches name, category, author and description.
- Click **☆** to favourite a preset. Click a column header to sort.
- **Load** (or double-click / Enter); **Save As…** with name, category, author and description; **Rename…**;
  **Delete** moves the file to the Trash or Recycle Bin, so you can restore it; **Import…** copies `.rvpreset`
  files into your folder; **Show Folder** opens it.
- The header's **Save** button overwrites the current *user* preset, or opens Save As for a factory preset.

User presets live in `Documents/Resonance Verb/Presets` as `.rvpreset` files (small XML files, easy to share).
Factory presets are also offered to your host as programs.

## Undo and redo

**Undo / Redo** in the header, or **⌘Z / ⇧⌘Z** (macOS) and **Ctrl+Z / Ctrl+Y** (Windows/Linux). Each knob move,
menu change and preset load is one step; the history keeps the last 200. Host automation and MIDI-controller
movements are deliberately *not* recorded, so playing doesn't flood the history.

## MIDI learn

1. Click **MIDI** › **MIDI Learn mode**. Every control gets an overlay.
2. Click a control (it glows), then move a knob or fader on your controller. Done: the overlay shows its CC.
3. Right-click an overlay to re-learn or clear it. Press **Esc** or choose the menu item again to finish.

Mappings are saved with your session. In the **MIDI** menu you can see every mapping (click one to clear it),
clear them all, **Save mappings as default** (new instances start with them), or recall the default. Any CC from
0 to 119 can be learned; a mapped CC64 drives its control instead of the pedal. MIDI learn listens on all channels.

## Sending MIDI to the plugin in your DAW

| Host | How |
|---|---|
| **Ableton Live** | Put Resonance Verb on the audio track. On a MIDI track, set *MIDI To* → that track → *Resonance Verb*. |
| **Logic Pro** | Insert it as an Audio FX on the audio track. Create a Software Instrument track, click its instrument slot and choose *MIDI-controlled Effects* › that Resonance Verb instance. |
| **Reaper** | Send MIDI from a MIDI track to the audio track (*Routing › MIDI send*), or record MIDI items directly on the audio track. |
| **Bitwig** | Use a *Note Receiver* device or set the MIDI track's output to the plugin's track. |
| **Cubase / Nuendo** | Set a MIDI track's output to *Resonance Verb* (VST3 MIDI input). |
| **FL Studio** | In the plugin wrapper's settings give Resonance Verb a MIDI input port, then send notes from a *MIDI Out* channel set to the same port. |

No MIDI at all? Use **Scale** mode, or click the on-screen keyboard.

## Tips

- **Tuned drums**: *Drum Halo* or *Snare Strings*: Scale mode in the song's key, Duck around 50 %, Low Cut 120–180 Hz.
- **Vocals**: *MIDI Latch* with the song's chords, Pre-Delay 20–40 ms, Duck 30–40 %.
- **Pads from anything**: a long Decay, Blur Amount 80 %+, Tail 5 s+, then **Freeze**.
- **Real piano realism**: Unison 3, Detune 1–2 ct, Halo 30–50 %, Double Decay 50–70 %, Body 30 %.
- More open strings means more energy. If big clusters get loud, lower **Excite** or raise the compressor ratio.

## Troubleshooting

- **No sound from the strings**: no strings are open. Hold MIDI notes, use Latch or Scale mode, or check the
  keyboard strip for brass bars.
- **MIDI isn't reaching the plugin**: see the host table above. Clicking the on-screen keys should always work.
- **macOS says the plugin is damaged**: clear the quarantine flag (see the README's Install section).
- **The sidechain does nothing**: enable the sidechain bus in your host and route a track to it. Source must be
  *Sidechain* or *Main + Sidechain*.
