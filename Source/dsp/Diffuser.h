// SPDX-License-Identifier: AGPL-3.0-or-later
// Resonance Verb — "Blur": diffuses the string resonance into a soft wash.
//
//   wet ──► 6 modulated Schroeder allpasses / channel ──► diffused ──┬──► blend with
//           (different prime-ish lengths L vs R →                    │    un-blurred wet
//            decorrelation; slow LFOs → chorus-like smear)           │    by Amount
//                                                                    ▼
//                                      4-line FDN (Hadamard, HF-damped loop) "blur tail"
//
// Allpasses are energy-preserving: they smear attacks and phase without colouring
// the steady-state spectrum, so the sympathetic tuning is untouched. The FDN adds a
// short diffuse tail (T60 = Blur Tail). No look-ahead: zero latency.
// Amount = 0 is an exact bypass.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace rv
{
class FracDelay
{
public:
    void prepare (int maxDelaySamples)
    {
        int sz = 1;
        while (sz < maxDelaySamples + 4) sz <<= 1;
        buf.assign ((size_t) sz, 0.0f);
        mask = sz - 1;
        w = 0;
    }
    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); }

    /** delay >= 1 samples, linear interpolation. */
    inline float read (float delay) const noexcept
    {
        const float rp = (float) (w + mask + 1) - delay;
        const int i0 = (int) rp;
        const float fr = rp - (float) i0;
        const float a = buf[(size_t) (i0 & mask)], b = buf[(size_t) ((i0 + 1) & mask)];
        return a + fr * (b - a);
    }
    /** delay >= 1.5 samples, first-order Thiran allpass interpolation: flat magnitude, so
        no HF loss when used inside allpass/feedback structures. z = interpolator state. */
    inline float readAllpass (float delay, float& z) const noexcept
    {
        const int M   = (int) (delay - 0.5f);              // fractional part kept in [0.5, 1.5)
        const float f = delay - (float) M;
        const float eta = (1.0f - f) / (1.0f + f);
        const float x0 = buf[(size_t) ((w - M) & mask)];
        const float x1 = buf[(size_t) ((w - M - 1) & mask)];
        z = eta * x0 + x1 - eta * z;
        return z;
    }

    inline void write (float x) noexcept
    {
        buf[(size_t) w] = x;
        w = (w + 1) & mask;
    }

private:
    std::vector<float> buf;
    int mask = 0, w = 0;
};

struct QuadLfo
{
    float c = 1.0f, s = 0.0f, kc = 1.0f, ks = 0.0f;
    void set (float rateHz, float sr, float phase) noexcept
    {
        const float w = 6.283185307f * rateHz / sr;
        kc = std::cos (w); ks = std::sin (w);
        c = std::cos (phase); s = std::sin (phase);
    }
    inline float tick() noexcept
    {
        const float nc = c * kc - s * ks;
        s = s * kc + c * ks;
        c = nc;
        return s;
    }
    void renormalise() noexcept
    {
        const float m = 1.0f / std::sqrt (c * c + s * s);
        c *= m; s *= m;
    }
};

class Diffuser
{
public:
    static constexpr int kStages = 6;
    static constexpr int kLines  = 4;

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        const float msToSamp = sr * 0.001f;
        for (int s = 0; s < kStages; ++s)
        {
            baseL[s] = kApMsL[s] * msToSamp;
            baseR[s] = kApMsR[s] * msToSamp;
            apL[s].prepare ((int) (baseL[s] * kMaxScale + kMaxModMs * msToSamp) + 4);
            apR[s].prepare ((int) (baseR[s] * kMaxScale + kMaxModMs * msToSamp) + 4);
            lfo[s].set (kLfoHz[s], sr, 1.1f * (float) s);
        }
        for (int i = 0; i < kLines; ++i)
        {
            baseF[i] = kFdnMs[i] * msToSamp;
            line[i].prepare ((int) (baseF[i] * kMaxScale * 1.05f) + 4);
        }
        smooth      = 1.0f - std::exp (-1.0f / (0.02f * sr));
        scaleSmooth = 1.0f - std::exp (-1.0f / (0.15f * sr));   // slow: size changes glide, no clicks
        amount = amountTarget;
        scale  = scaleTarget;
        reset();
    }

    void reset() noexcept
    {
        for (auto& a : apL) a.reset();
        for (auto& a : apR) a.reset();
        for (auto& z : zL) z = 0.0f;
        for (auto& z : zR) z = 0.0f;
        resetTail();
    }

    /** amount 0..1, size 0..1, tailSeconds 0..10 (0 = no tail), mod 0..1 */
    void set (float amountIn, float sizeIn, float tailSecondsIn, float modIn) noexcept
    {
        amountTarget = std::clamp (amountIn, 0.0f, 1.0f);
        scaleTarget  = kMinScale + (kMaxScale - kMinScale) * std::clamp (sizeIn, 0.0f, 1.0f);
        tailSeconds  = std::clamp (tailSecondsIn, 0.0f, 10.0f);
        mod          = std::clamp (modIn, 0.0f, 1.0f);
    }

    /** In place, stereo. */
    void process (float* L, float* R, int n) noexcept
    {
        if (amountTarget <= 0.0f && amount < 1e-5f)
        {
            amount = 0.0f;
            bypassed = true;     // exact bypass
            return;
        }
        if (bypassed)            // re-entering: don't replay stale buffer contents
        {
            reset();
            bypassed = false;
        }

        const bool useTail = tailSeconds > 0.01f;
        if (useTail && ! tailActive) resetTail();
        tailActive = useTail;

        // FDN feedback gains for the current (smoothed) size, per block.
        float g[kLines] {};
        if (useTail)
            for (int i = 0; i < kLines; ++i)
                g[i] = std::pow (10.0f, -3.0f * baseF[i] * scale / (tailSeconds * sr));

        const float modDepth = kMaxModMs * 0.001f * sr * mod;   // samples

        for (int i = 0; i < n; ++i)
        {
            amount += (amountTarget - amount) * smooth;
            scale  += (scaleTarget - scale) * scaleSmooth;
            const float apG = 0.5f + 0.2f * amount;     // diffusion density

            const float xl = L[i], xr = R[i];
            float dl = xl, dr = xr;
            float lfoVal[kStages];
            for (int s = 0; s < kStages; ++s)
            {
                lfoVal[s] = lfo[s].tick();
                const float m = modDepth * lfoVal[s];
                dl = allpass (apL[s], zL[s], dl, baseL[s] * scale + m, apG);
                dr = allpass (apR[s], zR[s], dr, baseR[s] * scale - m, apG); // opposite phase on R
            }

            float bl = dl, br = dr;
            if (useTail)
            {
                float y[kLines];
                for (int k = 0; k < kLines; ++k)
                {
                    const float wob = (k < 2) ? 1.0f + 0.004f * mod * lfoVal[k + 2] : 1.0f;
                    y[k] = line[k].read (baseF[k] * scale * wob);
                    lp[k] = y[k] + kDamp * (lp[k] - y[k]);  // HF damping in the loop
                }
                // 4x4 Hadamard (orthogonal, x0.5)
                const float a = lp[0] + lp[1], b = lp[0] - lp[1], c = lp[2] + lp[3], d = lp[2] - lp[3];
                const float h[kLines] = { 0.5f * (a + c), 0.5f * (b + d), 0.5f * (a - c), 0.5f * (b - d) };
                const float in[kLines] = { dl, dr, dl, -dr };
                for (int k = 0; k < kLines; ++k)
                    line[k].write (kTailIn * in[k] + g[k] * h[k]);

                bl += kTailOut * (y[0] + y[2]);
                br += kTailOut * (y[1] - y[3]);
            }

            L[i] = xl + amount * (bl - xl);
            R[i] = xr + amount * (br - xr);
        }

        for (auto& l : lfo) l.renormalise();
    }

private:
    static inline float allpass (FracDelay& d, float& interpState, float x, float delay, float g) noexcept
    {
        const float z = d.readAllpass (std::max (2.0f, delay), interpState);
        const float v = x + g * z;
        d.write (v);
        return z - g * v;
    }

    void resetTail() noexcept
    {
        for (auto& l : line) l.reset();
        for (auto& v : lp) v = 0.0f;
    }

    static constexpr float kApMsL[kStages] = { 4.31f, 6.73f, 9.97f, 13.61f, 17.93f, 23.11f };
    static constexpr float kApMsR[kStages] = { 4.79f, 7.19f, 10.63f, 14.27f, 18.71f, 22.39f };
    // Slow, incommensurate rates; with a fixed ±0.3 ms depth the pitch wobble stays
    // ≲1 cent at Motion 0.3 (≲4 cents at 1) so the sympathetic tuning isn't smeared.
    static constexpr float kLfoHz[kStages] = { 0.07f, 0.11f, 0.17f, 0.23f, 0.31f, 0.41f };
    static constexpr float kFdnMs[kLines]  = { 31.7f, 37.9f, 43.3f, 53.1f };
    static constexpr float kMinScale = 0.3f, kMaxScale = 2.0f;
    static constexpr float kMaxModMs = 0.3f;      // ± excursion at Motion = 1
    static constexpr float kDamp = 0.35f;
    static constexpr float kTailIn = 0.5f, kTailOut = 0.5f;

    FracDelay apL[kStages], apR[kStages], line[kLines];
    QuadLfo lfo[kStages];
    float baseL[kStages] {}, baseR[kStages] {}, baseF[kLines] {}, lp[kLines] {};
    float zL[kStages] {}, zR[kStages] {};   // Thiran interpolator states

    float sr = 48000.0f, smooth = 0.001f, scaleSmooth = 0.0001f;
    float amountTarget = 0.0f, amount = 0.0f;
    float scaleTarget = 1.0f, scale = 1.0f;
    float tailSeconds = 1.5f, mod = 0.3f;
    bool bypassed = true, tailActive = false;
};
} // namespace rv
