// SPDX-License-Identifier: AGPL-3.0-or-later
// Resonance Verb — factory presets. Values are in real-world units; any parameter not
// listed is set to its default.
#pragma once

#include <utility>
#include <vector>

namespace rv
{
struct FactoryPreset
{
    const char* name;
    const char* category;
    const char* description;
    std::vector<std::pair<const char*, float>> values;
};

// Choice indices used below — scale: 0 Chromatic, 1 Major, 2 Natural Minor, 7 Lydian, 10 Minor Pentatonic,
// 11 Blues, 12 Whole Tone, 16 Major 7, 19 Major 9, 20 Minor 9, 21 Open Fifths.
// mode: 0 MIDI, 1 Latch, 2 Scale.  root/scaleRoot: 0 = C … 11 = B.  temper: 2 = Pythagorean.
inline const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> list {
        { "Init", "Basics", "Default settings: MIDI mode, moderate decay, light blur. A clean starting point.", {} },
        { "Tight Sympathetic (MIDI)", "Basics",
          "Short, focused resonance that follows your MIDI chords closely. Ducks under the dry signal.",
          { { "decay", 3 }, { "release", 0.1f }, { "blur", 0.15f }, { "duck", 0.3f }, { "mix", 0.35f } } },

        { "Open Grand (C Major)", "Keys", "All C-major strings from C2 to C6 open: no MIDI needed.",
          { { "mode", 2 }, { "scaleRoot", 0 }, { "scale", 1 }, { "scaleLow", 36 }, { "scaleHigh", 84 },
            { "decay", 10 }, { "halo", 0.4f }, { "body", 0.35f }, { "blur", 0.3f }, { "mix", 0.45f } } },
        { "Concert Hall Grand", "Keys", "Big, long-sustaining grand with strong bridge coupling and a large body.",
          { { "decay", 14 }, { "halo", 0.5f }, { "body", 0.4f }, { "bodySize", 0.85f }, { "doubleDecay", 0.7f },
            { "stretch", 0.7f }, { "blur", 0.4f }, { "blurSize", 0.7f }, { "blurTail", 2.5f }, { "mix", 0.5f } } },
        { "Upright Felt", "Keys", "Small, dark, woody upright with a felt-damped top end.",
          { { "body", 0.6f }, { "bodySize", 0.15f }, { "bright", 0.25f }, { "hfDamp", 0.6f }, { "decay", 4 },
            { "detune", 3 }, { "bleed", 0.25f }, { "tilt", -3 }, { "mix", 0.4f } } },
        { "Half-Pedal Wash", "Keys", "Latch mode with the pedal half down: released chords bloom, then fade.",
          { { "mode", 1 }, { "pedal", 0.5f }, { "decay", 8 }, { "release", 0.4f }, { "blur", 0.45f }, { "mix", 0.45f } } },

        { "Drum Halo (A Minor Pent.)", "Percussion", "Turns a drum loop into a tuned A-minor-pentatonic halo.",
          { { "mode", 2 }, { "scaleRoot", 9 }, { "scale", 10 }, { "scaleLow", 33 }, { "scaleHigh", 81 },
            { "decay", 6 }, { "halo", 0.5f }, { "body", 0.4f }, { "bleed", 0.15f }, { "duck", 0.5f },
            { "lowCut", 120 }, { "blur", 0.5f }, { "compThr", -28 }, { "compRatio", 4 }, { "mix", 0.5f } } },
        { "Snare Strings (E Minor)", "Percussion", "Bright, short E-minor ring for snares and hats.",
          { { "mode", 2 }, { "scaleRoot", 4 }, { "scale", 2 }, { "scaleLow", 40 }, { "scaleHigh", 76 },
            { "decay", 3 }, { "release", 0.15f }, { "bright", 0.75f }, { "halo", 0.4f }, { "duck", 0.6f },
            { "lowCut", 180 }, { "bleed", 0.25f }, { "compThr", -30 }, { "compRatio", 5 }, { "mix", 0.4f } } },
        { "Kick Tuned Low (Open Fifths)", "Percussion", "Low C/G strings and a large body give kicks a tuned bloom.",
          { { "mode", 2 }, { "scaleRoot", 0 }, { "scale", 21 }, { "scaleLow", 24 }, { "scaleHigh", 48 },
            { "decay", 5 }, { "bright", 0.2f }, { "body", 0.6f }, { "bodySize", 0.9f }, { "tilt", -3 }, { "mix", 0.4f } } },

        { "Vocal Shimmer (Latch)", "Vocal", "Play a chord once; the voice rings through it with a bright shimmer.",
          { { "mode", 1 }, { "bright", 0.8f }, { "hfDamp", 0.15f }, { "blur", 0.6f }, { "blurSize", 0.7f },
            { "blurTail", 3 }, { "tilt", 3 }, { "preDelay", 30 }, { "duck", 0.4f }, { "mix", 0.4f } } },
        { "Choir Halo (F Lydian)", "Vocal", "Wide F-Lydian halo behind vocals; ducks to keep words clear.",
          { { "mode", 2 }, { "scaleRoot", 5 }, { "scale", 7 }, { "scaleLow", 41 }, { "scaleHigh", 89 },
            { "decay", 12 }, { "halo", 0.6f }, { "blur", 0.7f }, { "blurSize", 0.8f }, { "blurTail", 4 },
            { "bright", 0.6f }, { "preDelay", 40 }, { "duck", 0.35f }, { "mix", 0.4f } } },

        { "Frozen Cathedral (D Maj9)", "Ambient", "Huge D-major-9 wash. Try the Freeze button.",
          { { "mode", 2 }, { "scaleRoot", 2 }, { "scale", 19 }, { "scaleLow", 38 }, { "scaleHigh", 86 },
            { "decay", 20 }, { "halo", 0.6f }, { "blur", 0.8f }, { "blurSize", 1 }, { "blurTail", 6 },
            { "blurMod", 0.5f }, { "mix", 0.7f } } },
        { "Dream Pad (Eb Major 7)", "Ambient", "Slow, drifting E-flat-major-7 pad from any source.",
          { { "mode", 2 }, { "scaleRoot", 3 }, { "scale", 16 }, { "scaleLow", 39 }, { "scaleHigh", 87 },
            { "decay", 25 }, { "release", 1.5f }, { "halo", 0.7f }, { "blur", 0.9f }, { "blurSize", 0.9f },
            { "blurTail", 8 }, { "blurMod", 0.6f }, { "tilt", -2 }, { "mix", 0.75f } } },

        { "Prepared Gamelan (Whole Tone)", "Experimental", "Stiff, stretched, Pythagorean whole-tone metal.",
          { { "mode", 2 }, { "scale", 12 }, { "scaleLow", 48 }, { "scaleHigh", 96 }, { "inharm", 3 },
            { "stretch", 1 }, { "detune", 6 }, { "temper", 2 }, { "bright", 0.8f }, { "halo", 0.5f },
            { "decay", 9 }, { "mix", 0.5f } } },
        { "Bell Tree (A Blues, Stretched)", "Experimental", "Glassy, inharmonic A-blues bells.",
          { { "mode", 2 }, { "scaleRoot", 9 }, { "scale", 11 }, { "scaleLow", 57 }, { "scaleHigh", 96 },
            { "inharm", 2.5f }, { "stretch", 1 }, { "detune", 4 }, { "bright", 0.9f }, { "hfDamp", 0.1f },
            { "decay", 7 }, { "halo", 0.5f }, { "mix", 0.5f } } },
        { "Chromatic Haze", "Experimental", "Every string open: a dense, pitch-neutral haze.",
          { { "mode", 2 }, { "scale", 0 }, { "scaleLow", 48 }, { "scaleHigh", 84 }, { "decay", 4 },
            { "release", 0.3f }, { "blur", 0.8f }, { "blurTail", 5 }, { "bright", 0.3f }, { "makeup", 6 }, { "mix", 0.5f } } },
        { "Sidechain Strings (C Min9)", "Experimental",
          "Strings are played by the sidechain input: route a drum bus there to pulse a C-minor-9 pad.",
          { { "exciteSrc", 1 }, { "mode", 2 }, { "scaleRoot", 0 }, { "scale", 20 }, { "scaleLow", 36 },
            { "scaleHigh", 84 }, { "decay", 9 }, { "halo", 0.4f }, { "mix", 0.6f } } },
    };
    return list;
}
} // namespace rv
