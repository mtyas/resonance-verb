// SPDX-License-Identifier: AGPL-3.0-or-later
// Resonance Verb — input stage: source select (main / sidechain / both),
// stereo->mono soundboard drive, HP/LP pre-filter
// (TPT state-variable filters, modulation-safe), optional asymmetric tanh drive
// for added even/odd harmonics, and excitation gain.
#pragma once

#include <algorithm>
#include <cmath>

namespace rv
{
class TptSvf
{
public:
    void reset() noexcept { ic1 = ic2 = 0.0f; }

    void setup (float freqHz, float q, float sampleRate) noexcept
    {
        const float f = std::clamp (freqHz, 5.0f, 0.49f * sampleRate);
        g  = std::tan (3.14159265f * f / sampleRate);
        k  = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    inline void tick (float v0, float& lp, float& hp) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        hp = v0 - k * v1 - v2;
    }

private:
    float g = 0, k = 1.414f, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
};

class InputStage
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = (float) sampleRate;
        smoothCoef = 1.0f - std::exp (-1.0f / (0.02f * sr));
        reset();
        set (hpHz, lpHz, driveDb, exciteDb);
        exciteGain = exciteTarget;
    }

    void reset() noexcept
    {
        hp1.reset(); hp2.reset(); lp1.reset(); lp2.reset();
    }

    void set (float hpHzIn, float lpHzIn, float driveDbIn, float exciteDbIn) noexcept
    {
        hpHz = hpHzIn; lpHz = lpHzIn; driveDb = driveDbIn; exciteDb = exciteDbIn;
        // 4th-order Butterworth each side: two cascaded 2-pole sections.
        hp1.setup (hpHz, 0.5412f, sr); hp2.setup (hpHz, 1.3066f, sr);
        lp1.setup (lpHz, 0.5412f, sr); lp2.setup (lpHz, 1.3066f, sr);
        drive = std::pow (10.0f, driveDb / 20.0f);
        tb = std::tanh (drive * kBias);
        driveNorm = 1.0f / (drive * (1.0f - tb * tb));   // unity small-signal gain
        exciteTarget = std::pow (10.0f, exciteDb / 20.0f);
    }

    enum Source { Main = 0, Sidechain = 1, Both = 2 };

    /** right / scLeft / scRight may be nullptr. If the sidechain is requested but not connected
        (scLeft == nullptr), the main input is used. Writes the mono excitation signal. */
    void process (const float* left, const float* right, const float* scLeft, const float* scRight,
                  int source, float* mono, int n) noexcept
    {
        const bool useDrive = drive > 1.0012f; // > ~0.01 dB
        const bool haveSc = scLeft != nullptr;
        const bool useMain = source == Main || source == Both || ! haveSc;
        const bool useSc   = haveSc && (source == Sidechain || source == Both);
        for (int i = 0; i < n; ++i)
        {
            float x = 0.0f;
            if (useMain) x += right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
            if (useSc)   x += scRight != nullptr ? 0.5f * (scLeft[i] + scRight[i]) : scLeft[i];
            float lp, hp;
            hp1.tick (x, lp, hp); x = hp;
            hp2.tick (x, lp, hp); x = hp;
            lp1.tick (x, lp, hp); x = lp;
            lp2.tick (x, lp, hp); x = lp;

            if (useDrive)
                x = (std::tanh (drive * (x + kBias)) - tb) * driveNorm;

            exciteGain += (exciteTarget - exciteGain) * smoothCoef;
            mono[i] = x * exciteGain;
        }
    }

private:
    static constexpr float kBias = 0.2f;
    TptSvf hp1, hp2, lp1, lp2;
    float sr = 48000.0f, smoothCoef = 0.001f;
    float hpHz = 40.0f, lpHz = 14000.0f, driveDb = 0.0f, exciteDb = 0.0f;
    float drive = 1.0f, tb = 0.0f, driveNorm = 1.0f;
    float exciteTarget = 1.0f, exciteGain = 1.0f;
};
} // namespace rv
