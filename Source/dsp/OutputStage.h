// SPDX-License-Identifier: AGPL-3.0-or-later
// Resonance Verb — output stage (wet path):
//   wet EQ (12 dB/oct low cut + 1st-order tilt around 800 Hz)
//   -> stereo-linked feed-forward soft-knee compressor with makeup
//   -> ducking (wet pulled down while the dry signal is loud, swelling back in the gaps)
//   -> M/S width -> transparent safety soft-clip -> dry/wet mix -> output gain.
// No look-ahead: zero latency.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

namespace rv
{
class OutputStage
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = (float) sampleRate;
        smoothCoef = 1.0f - std::exp (-1.0f / (0.02f * sr));
        set (thrDb, ratio, attackMs, releaseMs, makeupDbTarget, widthTarget, mixTarget, outDbTarget);
        setTone (lowCutHz, tiltDb);
        setDuck (duckAmount, 250.0f);
        reset();
    }

    void reset() noexcept
    {
        envDb = 0.0f; duckEnvDb = -120.0f; duckGainDb = 0.0f;
        for (auto& c : eq) c = {};
        makeupDb = makeupDbTarget; width = widthTarget; mix = mixTarget; outGain = outGainTarget;
    }

    void set (float thresholdDb, float ratioIn, float attackMsIn, float releaseMsIn,
              float makeupDbIn, float widthIn, float mixIn, float outDbIn) noexcept
    {
        thrDb = thresholdDb; ratio = std::max (1.0f, ratioIn);
        attackMs = attackMsIn; releaseMs = releaseMsIn;
        atk = std::exp (-1.0f / (std::max (0.05f, attackMs)  * 0.001f * sr));
        rel = std::exp (-1.0f / (std::max (1.0f,  releaseMs) * 0.001f * sr));
        makeupDbTarget = makeupDbIn; widthTarget = widthIn;
        mixTarget = std::clamp (mixIn, 0.0f, 1.0f);
        outDbTarget = outDbIn; outGainTarget = std::pow (10.0f, outDbIn / 20.0f);
    }

    /** lowCutHz <= 20 disables the low cut; tiltDb is the total low-to-high spread (±). */
    void setTone (float lowCutHzIn, float tiltDbIn) noexcept
    {
        lowCutHz = lowCutHzIn; tiltDb = tiltDbIn;
        const float f = std::clamp (lowCutHz, 10.0f, 0.45f * sr);
        hpG = std::tan (3.14159265f * f / sr);
        hpA1 = 1.0f / (1.0f + hpG * (hpG + kHpK));
        hpA2 = hpG * hpA1; hpA3 = hpG * hpA2;
        const float gt = std::tan (3.14159265f * 800.0f / sr);
        tiltG = gt / (1.0f + gt);
        tiltLo = std::pow (10.0f, -tiltDb / 40.0f);
        tiltHi = std::pow (10.0f,  tiltDb / 40.0f);
    }

    /** amount 0..1 (max 24 dB of ducking), release in ms. */
    void setDuck (float amount, float releaseMsIn) noexcept
    {
        duckAmount = std::clamp (amount, 0.0f, 1.0f);
        duckAtk = std::exp (-1.0f / (0.005f * sr));
        duckRel = std::exp (-1.0f / (std::max (10.0f, releaseMsIn) * 0.001f * sr));
    }

    /** wet: resonator output. io: dry in / final out (right may be nullptr for mono). */
    void process (const float* wetL, const float* wetR, float* ioL, float* ioR, int n) noexcept
    {
        const float slope = 1.0f / ratio - 1.0f;
        const bool useLowCut = lowCutHz > 20.5f;
        const bool useTilt   = std::abs (tiltDb) > 0.01f;
        for (int i = 0; i < n; ++i)
        {
            float l = wetL[i], r = wetR[i];

            // --- wet EQ ---
            if (useLowCut) { l = highPass (eq[0], l); r = highPass (eq[1], r); }
            if (useTilt)   { l = tilt (eq[0], l);     r = tilt (eq[1], r); }

            // --- compressor (gain computer in dB, soft knee) ---
            const float peak  = std::max (std::abs (l), std::abs (r));
            const float lvlDb = 20.0f * std::log10 (peak + 1e-12f);
            const float over  = lvlDb - thrDb;
            float grDb = 0.0f;
            if (2.0f * over >= kKneeDb)              grDb = slope * over;
            else if (2.0f * over > -kKneeDb)         { const float t = over + 0.5f * kKneeDb; grDb = slope * t * t / (2.0f * kKneeDb); }
            envDb = grDb + (grDb < envDb ? atk : rel) * (envDb - grDb);

            // --- ducking from the dry signal ---
            const float dryPeak = std::max (std::abs (ioL[i]), ioR != nullptr ? std::abs (ioR[i]) : 0.0f);
            const float dryDb = 20.0f * std::log10 (dryPeak + 1e-9f);
            duckEnvDb = dryDb + (dryDb > duckEnvDb ? duckAtk : duckRel) * (duckEnvDb - dryDb);
            duckGainDb = -24.0f * duckAmount * std::clamp ((duckEnvDb + 40.0f) / 30.0f, 0.0f, 1.0f);

            makeupDb += (makeupDbTarget - makeupDb) * smoothCoef;
            const float g = std::exp ((envDb + makeupDb + duckGainDb) * 0.115129255f);
            l *= g; r *= g;

            // --- width ---
            width += (widthTarget - width) * smoothCoef;
            const float m = 0.5f * (l + r), s = 0.5f * (l - r) * width;
            l = softClip (m + s);
            r = softClip (m - s);

            // --- mix / output ---
            mix     += (mixTarget - mix) * smoothCoef;
            outGain += (outGainTarget - outGain) * smoothCoef;
            if (ioR != nullptr)
            {
                ioL[i] = (ioL[i] * (1.0f - mix) + l * mix) * outGain;
                ioR[i] = (ioR[i] * (1.0f - mix) + r * mix) * outGain;
            }
            else
            {
                ioL[i] = (ioL[i] * (1.0f - mix) + 0.5f * (l + r) * mix) * outGain;
            }
        }
        gainReductionDb.store (envDb, std::memory_order_relaxed);
        duckReductionDb.store (duckGainDb, std::memory_order_relaxed);
    }

    float getDuckReductionDb() const noexcept { return duckReductionDb.load (std::memory_order_relaxed); }

    float getGainReductionDb() const noexcept { return gainReductionDb.load (std::memory_order_relaxed); }

private:
    struct EqState { float h1 = 0, h2 = 0, t = 0; };

    inline float highPass (EqState& st, float x) const noexcept
    {
        const float v3 = x - st.h2;
        const float v1 = hpA1 * st.h1 + hpA2 * v3;
        const float v2 = st.h2 + hpA2 * st.h1 + hpA3 * v3;
        st.h1 = 2.0f * v1 - st.h1;
        st.h2 = 2.0f * v2 - st.h2;
        return x - kHpK * v1 - v2;
    }

    inline float tilt (EqState& st, float x) const noexcept
    {
        const float v = (x - st.t) * tiltG;   // TPT one-pole low-pass
        const float lp = v + st.t;
        st.t = lp + v;
        return lp * tiltLo + (x - lp) * tiltHi;
    }

    // Linear below 0.8, smooth tanh knee to a 1.0 ceiling above (continuous slope).
    static inline float softClip (float x) noexcept
    {
        constexpr float t = 0.8f;
        const float a = std::abs (x);
        if (a <= t) return x;
        const float y = t + (1.0f - t) * std::tanh ((a - t) / (1.0f - t));
        return x < 0.0f ? -y : y;
    }

    static constexpr float kKneeDb = 6.0f;
    float sr = 48000.0f, smoothCoef = 0.001f;
    float thrDb = -24.0f, ratio = 3.0f, attackMs = 10.0f, releaseMs = 150.0f;
    float atk = 0.99f, rel = 0.999f, envDb = 0.0f;
    float makeupDbTarget = 12.0f, makeupDb = 12.0f;
    float widthTarget = 1.0f, width = 1.0f;
    float mixTarget = 0.5f, mix = 0.5f;
    float outDbTarget = 0.0f, outGainTarget = 1.0f, outGain = 1.0f;
    std::atomic<float> gainReductionDb { 0.0f }, duckReductionDb { 0.0f };

    static constexpr float kHpK = 1.41421356f;   // Butterworth
    EqState eq[2];
    float lowCutHz = 20.0f, tiltDb = 0.0f;
    float hpG = 0, hpA1 = 0, hpA2 = 0, hpA3 = 0, tiltG = 0, tiltLo = 1, tiltHi = 1;
    float duckAmount = 0.0f, duckAtk = 0.99f, duckRel = 0.999f, duckEnvDb = -120.0f, duckGainDb = 0.0f;
};
} // namespace rv
