// SPDX-License-Identifier: AGPL-3.0-or-later
// Resonance Verb — soundboard body.
//
// A bank of 10 broad (low-Q) soundboard modes, 60 Hz – 1.7 kHz, spread across L/R.
//   Body  : the strings' output also drives the board  -> woody "inside the piano" colour
//   Bleed : the input drives the board directly, independent of which strings are open —
//           the muffled ring of a piano with all dampers down. With Bleed = 0 and no strings
//           open the plugin stays exactly silent.
// Body Size scales the mode frequencies (upright ↔ concert grand).
#pragma once

#include <algorithm>
#include <cmath>

namespace rv
{
class SoundboardBody
{
public:
    static constexpr int kModes = 10;

    void prepare (double sampleRate) noexcept
    {
        sr = (float) sampleRate;
        smooth = 1.0f - std::exp (-1.0f / (0.02f * sr));
        updateModes();
        bodyGain = bodyTarget; bleedGain = bleedTarget;
        reset();
    }

    void reset() noexcept
    {
        for (auto& m : modes) m.ic1 = m.ic2 = 0.0f;
    }

    /** body, bleed: 0..1; size: 0 (small/upright) .. 1 (large/concert grand) */
    void set (float body, float size, float bleed) noexcept
    {
        bodyTarget  = std::clamp (body, 0.0f, 1.0f);
        bleedTarget = std::clamp (bleed, 0.0f, 1.0f);
        if (size != sizeParam) { sizeParam = size; updateModes(); }
    }

    /** Adds the board's response to wetL/wetR. excitation = mono soundboard drive. */
    void process (const float* excitation, float* wetL, float* wetR, int n) noexcept
    {
        if (bodyTarget <= 0.0f && bleedTarget <= 0.0f && bodyGain < 1e-5f && bleedGain < 1e-5f)
        {
            if (! idle) { reset(); idle = true; }
            bodyGain = bleedGain = 0.0f;
            return;
        }
        idle = false;

        for (int i = 0; i < n; ++i)
        {
            bodyGain  += (bodyTarget - bodyGain) * smooth;
            bleedGain += (bleedTarget - bleedGain) * smooth;
            const float u = bodyGain * kBodyDrive * 0.5f * (wetL[i] + wetR[i])
                          + bleedGain * kBleedDrive * excitation[i];
            float l = 0.0f, r = 0.0f;
            for (int k = 0; k < kModes; ++k)
            {
                auto& m = modes[k];
                // TPT SVF, peak-normalised band-pass (k * v1)
                const float v3 = u - m.ic2;
                const float v1 = m.a1 * m.ic1 + m.a2 * v3;
                const float v2 = m.ic2 + m.a2 * m.ic1 + m.a3 * v3;
                m.ic1 = 2.0f * v1 - m.ic1;
                m.ic2 = 2.0f * v2 - m.ic2;
                const float bp = m.k * v1 * m.gain;
                l += bp * m.panL;
                r += bp * m.panR;
            }
            wetL[i] += l;
            wetR[i] += r;
        }
    }

private:
    struct Mode { float a1 = 0, a2 = 0, a3 = 0, k = 1, ic1 = 0, ic2 = 0, gain = 0, panL = 0, panR = 0; };

    void updateModes() noexcept
    {
        static constexpr float hz[kModes] = { 62, 95, 140, 205, 290, 410, 580, 820, 1150, 1650 };
        static constexpr float q[kModes]  = { 3.0f, 3.5f, 3.0f, 4.0f, 3.5f, 4.5f, 4.0f, 5.0f, 4.5f, 5.0f };
        const float scale = std::exp2 ((0.5f - sizeParam) * 1.5f);   // size 0 → ×1.68, size 1 → ×0.59
        for (int i = 0; i < kModes; ++i)
        {
            auto& m = modes[i];
            const float f = std::min (hz[i] * scale, 0.45f * sr);
            const float g = std::tan (3.14159265f * f / sr);
            m.k  = 1.0f / q[i];
            m.a1 = 1.0f / (1.0f + g * (g + m.k));
            m.a2 = g * m.a1;
            m.a3 = g * m.a2;
            m.gain = 1.0f / std::sqrt (f / 100.0f + 1.0f);           // board response falls with frequency
            const float pan = (i % 2 == 0 ? -0.5f : 0.5f) * (0.4f + 0.6f * (float) i / (kModes - 1));
            const float ang = (pan + 1.0f) * 0.785398163f;
            m.panL = std::cos (ang);
            m.panR = std::sin (ang);
        }
    }

    static constexpr float kBodyDrive  = 0.35f;
    static constexpr float kBleedDrive = 3.0f;   // Bleed 1 ≈ -10 dB re the input

    Mode modes[kModes];
    float sr = 48000.0f, smooth = 0.001f;
    float bodyTarget = 0.3f, bodyGain = 0.3f, bleedTarget = 0.0f, bleedGain = 0.0f, sizeParam = 0.5f;
    bool idle = true;
};
} // namespace rv
