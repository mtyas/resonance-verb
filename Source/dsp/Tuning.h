// SPDX-License-Identifier: AGPL-3.0-or-later
// Resonance Verb — tuning: reference pitch, historical temperaments, Railsback-style
// stretch, and an optional per-note frequency table (hook for Scala / MTS-ESP).
#pragma once

#include <array>
#include <cmath>

namespace rv
{
enum class Temperament : int
{
    Equal = 0,
    JustMajor,
    Pythagorean,
    MeantoneQuarterComma,
    WerckmeisterIII,
    NumTemperaments
};

class Tuning
{
public:
    float a4Hz        = 440.0f;
    Temperament temperament = Temperament::Equal;
    int   root        = 0;     // pitch class the temperament is built on (0 = C)
    float stretch     = 0.5f;  // 0..1, octave stretch typical of real pianos (±~30 cents at the extremes)

    bool  useNoteTable = false;           // when true, noteHz overrides everything above
    std::array<float, 128> noteHz {};     // filled by a Scala loader / MTS-ESP client

    bool operator== (const Tuning& o) const noexcept
    {
        return a4Hz == o.a4Hz && temperament == o.temperament && root == o.root
            && stretch == o.stretch && useNoteTable == o.useNoteTable
            && (! useNoteTable || noteHz == o.noteHz);
    }
    bool operator!= (const Tuning& o) const noexcept { return ! (*this == o); }

    /** Frequency in Hz of a (possibly fractional) MIDI note number. */
    double frequency (int note) const noexcept
    {
        if (useNoteTable && note >= 0 && note < 128 && noteHz[(size_t) note] > 0.0f)
            return noteHz[(size_t) note];

        const auto& table = offsetsCents (temperament);
        const int pc  = ((note - root) % 12 + 12) % 12;
        const int pcA = ((9 - root) % 12 + 12) % 12;
        double cents  = table[(size_t) pc] - table[(size_t) pcA]; // keep A anchored at a4Hz

        const double d = (double) (note - 69);
        const double x = d / 48.0;
        cents += 30.0 * (double) stretch * (d < 0 ? -x * x : x * x);

        return (double) a4Hz * std::pow (2.0, (d * 100.0 + cents) / 1200.0);
    }

    static const std::array<double, 12>& offsetsCents (Temperament t) noexcept
    {
        // Deviation from 12-TET in cents, per pitch class relative to the root.
        static const std::array<double, 12> tables[] = {
            { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },                                                   // Equal
            { 0, 11.73, 3.91, 15.64, -13.69, -1.96, -9.78, 1.96, 13.69, -15.64, 17.60, -11.73 },      // 5-limit just major
            { 0, -9.78, 3.91, -5.87, 7.82, -1.96, -11.73, 1.96, -7.82, 5.87, -3.91, 9.78 },           // Pythagorean
            { 0, -23.95, -6.84, 10.26, -13.69, 3.42, -20.53, -3.42, -27.37, -10.26, 6.84, -17.11 },   // 1/4-comma meantone
            { 0, -9.78, -7.82, -5.87, -9.78, -1.96, -11.73, -3.91, -7.82, -11.73, -3.91, -7.82 },     // Werckmeister III
        };
        const int i = (int) t;
        return tables[(i >= 0 && i < (int) Temperament::NumTemperaments) ? i : 0];
    }
};
} // namespace rv
