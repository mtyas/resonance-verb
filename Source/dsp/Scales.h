// SPDX-License-Identifier: AGPL-3.0-or-later
// Resonance Verb — scale / chord tables for MIDI-free "Scale" mode.
#pragma once

#include <array>
#include <cstdint>

namespace rv
{
struct ScaleDef
{
    const char* name;
    uint16_t mask;   // bit i set = pitch class (root + i) is open
};

constexpr uint16_t pcs (std::initializer_list<int> degrees)
{
    uint16_t m = 0;
    for (int d : degrees) m = (uint16_t) (m | (1u << d));
    return m;
}

inline const std::array<ScaleDef, 22>& scaleTable()
{
    static const std::array<ScaleDef, 22> t { {
        { "Chromatic (all strings)", 0x0FFF },
        { "Major",              pcs ({ 0, 2, 4, 5, 7, 9, 11 }) },
        { "Natural Minor",      pcs ({ 0, 2, 3, 5, 7, 8, 10 }) },
        { "Harmonic Minor",     pcs ({ 0, 2, 3, 5, 7, 8, 11 }) },
        { "Melodic Minor",      pcs ({ 0, 2, 3, 5, 7, 9, 11 }) },
        { "Dorian",             pcs ({ 0, 2, 3, 5, 7, 9, 10 }) },
        { "Phrygian",           pcs ({ 0, 1, 3, 5, 7, 8, 10 }) },
        { "Lydian",             pcs ({ 0, 2, 4, 6, 7, 9, 11 }) },
        { "Mixolydian",         pcs ({ 0, 2, 4, 5, 7, 9, 10 }) },
        { "Major Pentatonic",   pcs ({ 0, 2, 4, 7, 9 }) },
        { "Minor Pentatonic",   pcs ({ 0, 3, 5, 7, 10 }) },
        { "Blues",              pcs ({ 0, 3, 5, 6, 7, 10 }) },
        { "Whole Tone",         pcs ({ 0, 2, 4, 6, 8, 10 }) },
        { "Major Triad",        pcs ({ 0, 4, 7 }) },
        { "Minor Triad",        pcs ({ 0, 3, 7 }) },
        { "Sus4",               pcs ({ 0, 5, 7 }) },
        { "Major 7",            pcs ({ 0, 4, 7, 11 }) },
        { "Minor 7",            pcs ({ 0, 3, 7, 10 }) },
        { "Dominant 7",         pcs ({ 0, 4, 7, 10 }) },
        { "Major 9",            pcs ({ 0, 2, 4, 7, 11 }) },
        { "Minor 9",            pcs ({ 0, 2, 3, 7, 10 }) },
        { "Open Fifths",        pcs ({ 0, 7 }) },
    } };
    return t;
}
} // namespace rv
