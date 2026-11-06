// SPDX-License-Identifier: AGPL-3.0-or-later
// Resonance Verb — the sympathetic string matrix.
//
// Each held (or still ringing) MIDI note owns a Voice: a fixed block of 48 modal
// resonator "lanes" shared between its unison strings and their partials:
//   1 string  x 48 partials   (bass, single wound strings)
//   2 strings x 24 partials   (tenor bichords)
//   3 strings x 16 partials   (trichords)
// Partials above ~0.45*fs are dropped, so treble notes cost far fewer lanes.
//
// Every lane is a complex one-pole resonator ("rotating phasor"):
//     y[n] = r * e^{jw} * y[n-1] + b * x[n],   output = Re(y)
// which is equivalent to a 2-pole bandpass but:
//   * is unconditionally stable and click-free when w or r change (pitch bend,
//     damper falling) — the state magnitude is preserved, only the rotation changes,
//   * vectorises perfectly: 4 lanes per SSE/NEON register, no cross-lane dependencies.
//
// Physics:
//   * stiff-string inharmonicity  f_n = n f0 sqrt(1 + B n^2), B by register
//   * frequency-dependent loss    sigma_n = sigma_0 + k_hf * f_n^2   (Chaigne/Askenfelt form)
//   * damper                      extra loss when key up; continuous half-pedal blends
//                                 log-linearly between "lifted" and "down" loss
//   * freeze                      ~infinite hold of everything ringing, input decoupled
//   * unison detune with per-note deterministic jitter (beating)
//   * double decay (Weinreich): unisons split into a fast "prompt" mode strongly coupled to
//     the bridge and slow, weakly coupled "aftersound" modes
//   * halo: string-to-string coupling through the bridge, built FEED-FORWARD so it is
//     unconditionally stable. Each lane has a second "sympathetic" state (re2/im2) with the
//     same pole, driven only by the other strings' *direct* response from one sub-block
//     earlier (64 samples ≈ 1.3 ms). Sympathetic states never feed the coupling bus, so there
//     is no loop: a feedback version ran away (+50 dB) once partials coincided exactly
//     (inharmonicity/stretch/detune at 0). Only the lowest kHaloLanes lanes get a sympathetic
//     state — coincident partials live there — which bounds the extra CPU.
//
// Zero latency: everything is sample-by-sample; coefficients update per 64-sample sub-block.
#pragma once

#include "SimdVec.h"
#include "Tuning.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace rv
{
struct ResonatorSettings
{
    float decaySeconds   = 8.0f;   // T60 of the fundamental around middle C, damper lifted
    float releaseSeconds = 0.2f;   // T60 once the damper falls
    float hfDamping      = 0.35f;  // 0..1, how much faster upper partials die
    float inharmonicity  = 1.0f;   // 0..3 multiplier on the physical B coefficient
    float unisonDetune   = 1.2f;   // cents between unison strings
    float brightness     = 0.5f;   // 0..1 partial weighting tilt
    float velocitySens   = 0.5f;   // 0..1
    float spread         = 0.7f;   // 0..1 keyboard span across the stereo field
    int   maxUnison      = 3;      // 1..3 strings per note
    float doubleDecay    = 0.5f;   // 0..1 prompt/aftersound split of unison strings
    float halo           = 0.3f;   // 0..1 string-to-string (bridge) coupling

    bool operator== (const ResonatorSettings& o) const noexcept
    {
        return decaySeconds == o.decaySeconds && releaseSeconds == o.releaseSeconds
            && hfDamping == o.hfDamping && inharmonicity == o.inharmonicity
            && unisonDetune == o.unisonDetune && brightness == o.brightness
            && velocitySens == o.velocitySens && spread == o.spread && maxUnison == o.maxUnison
            && doubleDecay == o.doubleDecay && halo == o.halo;
    }
    bool operator!= (const ResonatorSettings& o) const noexcept { return ! (*this == o); }
};

class ResonatorBank
{
public:
    static constexpr int kLanes     = 48;  // per voice; divisible by 1, 2, 3 strings and by 4 (and 8 for AVX)
    static constexpr int kMaxVoices = 64;  // held notes + ringing tails (Scale mode can hold ~50)
    static constexpr int kSubBlock  = 64;  // coefficient / housekeeping granularity

    // ---------------------------------------------------------------- setup
    void prepare (double sampleRate) noexcept
    {
        sr    = (float) sampleRate;
        invSr = 1.0f / sr;
        bendSmooth = 1.0f - std::exp (-(float) kSubBlock / (0.004f * sr)); // ~4 ms glide
        updateHaloNorm();
        reset();
    }

    void reset() noexcept
    {
        for (auto& v : voices)
            v.active = false;
        noteToVoice.fill (-1);
        pedalDamp = 1.0f;
        frozen = false;
        bendCurrent = bendTarget;
        bendRatio = std::exp2 (bendCurrent / 12.0f);
        prevN = 0;
    }

    void setSettings (const ResonatorSettings& s) noexcept
    {
        if (s == settings) return;
        settings = s;
        settings.maxUnison = std::clamp (settings.maxUnison, 1, 3);
        updateHaloNorm();
        rebuildAllActive();
    }

    void setTuning (const Tuning& t) noexcept
    {
        if (t == tuning) return;
        tuning = t;
        rebuildAllActive();
    }

    // ---------------------------------------------------------------- performance events
    void setPitchBendSemitones (float semis) noexcept { bendTarget = semis; }

    void setSustain (bool on) noexcept { setPedal (on ? 1.0f : 0.0f); }

    /** Continuous pedal 0..1 (CC64 / 127). Below ~0.2 the dampers sit fully on the strings,
        above ~0.8 they're fully lifted; in between they only partly damp ("half-pedal"). */
    void setPedal (float pedal01) noexcept
    {
        const float d = std::clamp ((0.8f - pedal01) / 0.6f, 0.0f, 1.0f);
        const bool endpoint = (d == 0.0f || d == 1.0f) && d != pedalDamp;
        if (endpoint || std::abs (d - pedalDamp) > 0.002f)
        {
            pedalDamp = d;
            markAllDirty();
        }
    }

    /** Hold everything that is ringing (T60 ~ 5 min) and stop feeding the strings. */
    void setFreeze (bool on) noexcept
    {
        if (on == frozen) return;
        frozen = on;
        markAllDirty();
    }

    /** Sum of squared lane states for the voice playing this note (0 if none). For metering. */
    float noteEnergy (int note) const noexcept
    {
        if (note < 0 || note > 127) return 0.0f;
        const int i = noteToVoice[(size_t) note];
        return i >= 0 ? voices[(size_t) i].energy : 0.0f;
    }

    bool isKeyDown (int note) const noexcept
    {
        if (note < 0 || note > 127) return false;
        const int i = noteToVoice[(size_t) note];
        return i >= 0 && voices[(size_t) i].keyDown;
    }

    void noteOn (int note, float velocity01) noexcept
    {
        if (note < 0 || note > 127) return;
        velocity01 = std::clamp (velocity01, 0.0f, 1.0f);

        if (const int existing = noteToVoice[(size_t) note]; existing >= 0)
        {
            // Damper re-lifted on a still-ringing string: keep its vibration state.
            auto& v = voices[(size_t) existing];
            v.keyDown = true;
            v.velocity = std::max (v.velocity, velocity01);
            v.age = ++ageCounter;
            v.coefDirty = true;
            return;
        }

        const int idx = allocateVoice();
        auto& v = voices[(size_t) idx];
        if (v.active && v.note >= 0)
            noteToVoice[(size_t) v.note] = -1;

        v.active   = true;
        v.keyDown  = true;
        v.note     = note;
        v.velocity = velocity01;
        v.age      = ++ageCounter;
        v.energy   = 0.0f;
        v.symEnergy = 0.0f;
        std::fill (std::begin (v.re), std::end (v.re), 0.0f);
        std::fill (std::begin (v.im), std::end (v.im), 0.0f);
        std::fill (std::begin (v.ownPrev), std::end (v.ownPrev), 0.0f);
        std::fill (std::begin (v.re2), std::end (v.re2), 0.0f);
        std::fill (std::begin (v.im2), std::end (v.im2), 0.0f);
        buildLayout (v);
        noteToVoice[(size_t) note] = idx;
    }

    void noteOff (int note) noexcept
    {
        if (note < 0 || note > 127) return;
        if (const int i = noteToVoice[(size_t) note]; i >= 0)
        {
            voices[(size_t) i].keyDown = false;
            voices[(size_t) i].coefDirty = true;
        }
    }

    void allNotesOff() noexcept
    {
        pedalDamp = 1.0f;
        for (auto& v : voices)
        {
            v.keyDown = false;
            v.coefDirty = true;
        }
    }

    int activeVoiceCount() const noexcept
    {
        int n = 0;
        for (auto& v : voices) n += v.active ? 1 : 0;
        return n;
    }

    // ---------------------------------------------------------------- audio
    /** excitation: mono soundboard drive. Writes (does not add) to outL/outR. */
    void process (const float* excitation, float* outL, float* outR, int numSamples) noexcept
    {
        for (int start = 0; start < numSamples; start += kSubBlock)
        {
            const int n = std::min (kSubBlock, numSamples - start);
            updateBend();

            for (int i = 0; i < n; ++i)
                accL[i] = accR[i] = Vec4::zero();

            int open = 0;
            for (auto& v : voices) open += v.active ? 1 : 0;
            const float k = settings.halo * haloNorm / std::sqrt ((float) std::max (1, open - 1));
            const bool haloOn = settings.halo > 0.0f;
            for (int i = 0; i < n; ++i) nextTotal[i] = 0.0f;

            for (auto& v : voices)
            {
                if (! v.active) continue;
                if (v.coefDirty) updateCoefficients (v);
                // Sympathetic path while Halo is on, or while old sympathetic ringing remains
                // (so turning Halo down lets it decay instead of cutting it off).
                if (haloOn || v.symEnergy > 0.0f)
                {
                    // sympathetic drive = the other strings' direct output, one sub-block ago
                    for (int i = 0; i < n; ++i)
                    {
                        const float others = i < prevN ? prevTotal[i] - v.ownPrev[i] : 0.0f;
                        haloIn[i] = haloOn ? std::clamp (k * others, -0.5f, 0.5f) : 0.0f;
                    }
                    processVoice<true> (v, excitation + start, n);
                }
                else
                {
                    processVoice<false> (v, excitation + start, n);
                }
                housekeeping (v);
            }

            if (haloOn)
            {
                for (int i = 0; i < n; ++i) prevTotal[i] = nextTotal[i];
                prevN = n;
            }
            else
            {
                prevN = 0;
            }

            for (int i = 0; i < n; ++i)
            {
                outL[start + i] = accL[i].sum();
                outR[start + i] = accR[i].sum();
            }
        }
    }

    /** Physical inharmonicity coefficient by register (approximation of measured grand piano data). */
    static float inharmonicityB (int note) noexcept
    {
        const float log10B = note >= 36 ? -4.0f + 0.031f * (float) (note - 36)
                                        : -4.0f + 0.012f * (float) (36 - note);
        return std::pow (10.0f, log10B);
    }

    static int stringsForNote (int note, int maxUnison) noexcept
    {
        const int ns = note < 34 ? 1 : (note < 48 ? 2 : 3);
        return std::min (ns, maxUnison);
    }

private:
    struct alignas (16) Voice
    {
        // hot, SIMD-processed state (SoA)
        float re[kLanes], im[kLanes];
        float cr[kLanes], ci[kLanes];      // r*cos(w), r*sin(w)
        float b[kLanes];                   // input coupling
        float pl[kLanes], pr[kLanes];      // equal-power output pan
        // cold per-lane description
        float freq[kLanes];                // Hz before pitch bend; 0 = unused lane
        float sigHeld[kLanes], sigDamp[kLanes];
        float weight[kLanes], pan[kLanes];
        float re2[kLanes], im2[kLanes];    // sympathetic (halo-driven) states
        float ownPrev[kSubBlock];          // this voice's direct mono output, previous sub-block (halo)

        int   numChunks = 0;
        int   note = -1;
        bool  active = false, keyDown = false, coefDirty = true;
        float velocity = 1.0f;
        float energy = 0.0f, symEnergy = 0.0f;
        uint32_t age = 0;
    };

    static float hashJitter (int note, int s) noexcept
    {
        uint32_t h = (uint32_t) note * 73856093u ^ (uint32_t) (s + 1) * 19349663u;
        h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
        return (float) (h & 0xffffu) / 32767.5f - 1.0f;
    }

    int allocateVoice() noexcept
    {
        for (int i = 0; i < kMaxVoices; ++i)
            if (! voices[(size_t) i].active) return i;

        // Steal the quietest released voice, else the oldest held one.
        int best = -1; float bestEnergy = 1e30f;
        for (int i = 0; i < kMaxVoices; ++i)
        {
            const auto& v = voices[(size_t) i];
            if (! v.keyDown && v.energy < bestEnergy) { bestEnergy = v.energy; best = i; }
        }
        if (best >= 0) return best;

        uint32_t oldest = UINT32_MAX;
        for (int i = 0; i < kMaxVoices; ++i)
            if (voices[(size_t) i].age < oldest) { oldest = voices[(size_t) i].age; best = i; }
        return best;
    }

    void buildLayout (Voice& v) noexcept
    {
        const auto& s = settings;
        const int note = v.note;
        const int ns   = stringsForNote (note, s.maxUnison);
        const int pps  = kLanes / ns;
        const float f0 = (float) tuning.frequency (note);
        const float B  = inharmonicityB (note) * s.inharmonicity;

        const float t60      = std::clamp (s.decaySeconds * std::exp2 (-(float) (note - 60) / 36.0f), 0.05f, 60.0f);
        const float sigma0   = 6.9078f / t60;                                // ln(1000)/T60
        const float sigmaRel = 6.9078f / std::max (0.01f, s.releaseSeconds);
        const float tilt     = 1.6f * (1.0f - s.brightness);
        const float norm     = 1.0f / std::sqrt ((float) ns);
        const float notePan  = std::clamp (s.spread * ((float) note - 64.5f) / 43.5f, -1.0f, 1.0f);
        const float maxF     = std::min (0.45f * sr, 20000.0f);

        int lane = 0, validLanes = 0;
        for (int n = 1; n <= pps; ++n)
        {
            const float nf = (float) n;
            const float stiff = std::sqrt (1.0f + B * nf * nf);
            for (int st = 0; st < ns; ++st, ++lane)
            {
                float cents = 0.0f;
                if (ns > 1)
                {
                    const float pos = (float) st - 0.5f * (float) (ns - 1);  // -1,0,1 or -.5,.5
                    cents = s.unisonDetune * (pos + 0.25f * hashJitter (note, st));
                }
                const float fn = nf * f0 * std::exp2 (cents / 1200.0f) * stiff;

                if (fn >= maxF)
                {
                    v.freq[lane] = 0.0f;
                    continue;
                }
                validLanes = lane + 1;

                const float khz = fn * 0.001f;
                // Double decay: string 0 of a bi/trichord is the "prompt" (bridge-symmetric) mode,
                // the others the "aftersound" modes.
                float decayMul = 1.0f, couplingMul = 1.0f;
                if (ns > 1)
                {
                    const float dd = s.doubleDecay;
                    if (st == 0) { decayMul = 1.0f + 3.0f * dd; couplingMul = 1.0f + dd; }
                    else         { decayMul = 1.0f - 0.4f * dd; couplingMul = 1.0f - 0.5f * dd; }
                }
                v.freq[lane]    = fn;
                v.sigHeld[lane] = (sigma0 + s.hfDamping * 3.0f * khz * khz) * decayMul;
                v.sigDamp[lane] = v.sigHeld[lane] + sigmaRel * (1.0f + 0.5f * khz);
                v.weight[lane]  = std::pow (nf, -tilt) * norm * couplingMul;
                const float strPan = ns > 1 ? 0.12f * s.spread * ((float) st - 0.5f * (float) (ns - 1)) : 0.0f;
                v.pan[lane]     = std::clamp (notePan + strPan, -1.0f, 1.0f);
            }
        }

        v.numChunks = (validLanes + 3) / 4;
        for (int l = validLanes; l < kLanes; ++l)
            v.freq[l] = 0.0f;
        for (int l = 0; l < kLanes; ++l)
            if (v.freq[l] == 0.0f)
                v.re[l] = v.im[l] = v.re2[l] = v.im2[l] = 0.0f;

        v.coefDirty = true;
    }

    void updateCoefficients (Voice& v) noexcept
    {
        constexpr float twoPi = 6.283185307f, quarterPi = 0.785398163f;
        const float velGain = (1.0f - settings.velocitySens) + settings.velocitySens * v.velocity * v.velocity;
        const float wMax = 0.95f * 3.14159265f;

        for (int l = 0; l < v.numChunks * 4; ++l)
        {
            const float w = twoPi * v.freq[l] * bendRatio * invSr;
            if (v.freq[l] <= 0.0f || w >= wMax)
            {
                v.cr[l] = v.ci[l] = v.b[l] = v.pl[l] = v.pr[l] = 0.0f;
                continue;
            }
            float sigma = v.sigHeld[l];
            if (frozen)                     sigma = kFreezeSigma;
            else if (! v.keyDown)
            {
                if (pedalDamp >= 1.0f)      sigma = v.sigDamp[l];
                else if (pedalDamp > 0.0f)  sigma = v.sigHeld[l] * std::pow (v.sigDamp[l] / v.sigHeld[l], pedalDamp);
            }
            const float r  = std::exp (-sigma * invSr);
            const float rH = std::exp (-v.sigHeld[l] * invSr);
            v.cr[l] = r * std::cos (w);
            v.ci[l] = r * std::sin (w);
            // Coupling normalised between "unit peak gain" (1-r) and "unit noise power" sqrt(1-r^2):
            // keeps both tonal and broadband excitation in a sane range across decay settings.
            // A damper on the string also decouples it from the soundboard: key-up strings take
            // input only in proportion to how far the pedal lifts the dampers. (Otherwise a
            // released string fed continuous audio would never fall silent and never free its voice.)
            const float coupling = frozen ? 0.0f : (v.keyDown ? 1.0f : 1.0f - pedalDamp);
            v.b[l]  = coupling * kExciteScale * std::pow (1.0f - rH, 0.75f) * v.weight[l] * velGain;
            const float ang = (v.pan[l] + 1.0f) * quarterPi;
            v.pl[l] = std::cos (ang);
            v.pr[l] = std::sin (ang);
        }
        v.coefDirty = false;
    }

    template <bool TrackOwn>
    void processVoice (Voice& v, const float* x, int n) noexcept
    {
        if constexpr (TrackOwn)
            for (int i = 0; i < n; ++i) ownAcc[i] = Vec4::zero();

        for (int c = 0; c < v.numChunks; ++c)
        {
            const int o = c * 4;
            Vec4 re = Vec4::load (v.re + o), im = Vec4::load (v.im + o);
            const Vec4 cr = Vec4::load (v.cr + o), ci = Vec4::load (v.ci + o);
            const Vec4 b  = Vec4::load (v.b + o);
            const Vec4 pl = Vec4::load (v.pl + o), pr = Vec4::load (v.pr + o);

            if (TrackOwn && o < kHaloLanes)
            {
                Vec4 re2 = Vec4::load (v.re2 + o), im2 = Vec4::load (v.im2 + o);
                for (int i = 0; i < n; ++i)
                {
                    const Vec4 xin = Vec4::broadcast (x[i]);
                    const Vec4 nre = cr * re - ci * im + b * xin;
                    im = cr * im + ci * re;
                    re = nre;
                    const Vec4 hin = Vec4::broadcast (haloIn[i]);
                    const Vec4 nre2 = cr * re2 - ci * im2 + b * hin;
                    im2 = cr * im2 + ci * re2;
                    re2 = nre2;
                    const Vec4 sum = re + re2;
                    accL[i] = accL[i] + sum * pl;
                    accR[i] = accR[i] + sum * pr;
                    ownAcc[i] = ownAcc[i] + re;          // direct response only: no loop
                }
                re2.store (v.re2 + o);
                im2.store (v.im2 + o);
            }
            else
            {
                for (int i = 0; i < n; ++i)
                {
                    const Vec4 xin = Vec4::broadcast (x[i]);
                    const Vec4 nre = cr * re - ci * im + b * xin;
                    im = cr * im + ci * re;
                    re = nre;
                    if constexpr (TrackOwn) ownAcc[i] = ownAcc[i] + re;
                    accL[i] = accL[i] + re * pl;
                    accR[i] = accR[i] + re * pr;
                }
            }
            re.store (v.re + o);
            im.store (v.im + o);
        }

        if constexpr (TrackOwn)
            for (int i = 0; i < n; ++i)
            {
                const float own = ownAcc[i].sum();
                v.ownPrev[i] = own;
                nextTotal[i] += own;
            }
    }

    /** Peak resonator gain bound for the longest-ringing lane -> safe coupling scale. */
    void updateHaloNorm() noexcept
    {
        const auto& s = settings;
        const float t60 = std::clamp (s.decaySeconds * std::exp2 (39.0f / 36.0f), 0.05f, 60.0f)
                        / std::max (0.2f, 1.0f - 0.4f * s.doubleDecay);
        const float oneMinusR = 1.0f - std::exp (-6.9078f / (t60 * sr));
        const float peakGain = kExciteScale * std::pow (oneMinusR, -0.25f) * (1.0f + s.doubleDecay);
        haloNorm = kHaloMax / peakGain;
    }

    void housekeeping (Voice& v) noexcept
    {
        float e = 0.0f, e2 = 0.0f;
        for (int l = 0; l < v.numChunks * 4; ++l)
            e += v.re[l] * v.re[l] + v.im[l] * v.im[l];
        for (int l = 0; l < std::min (v.numChunks * 4, kHaloLanes); ++l)
            e2 += v.re2[l] * v.re2[l] + v.im2[l] * v.im2[l];
        if (e2 < 1e-14f && e2 > 0.0f)                // sympathetic tail finished
        {
            std::fill (std::begin (v.re2), std::end (v.re2), 0.0f);
            std::fill (std::begin (v.im2), std::end (v.im2), 0.0f);
            e2 = 0.0f;
        }
        v.symEnergy = e2;
        e += e2;
        v.energy = e;

        const bool damped = ! v.keyDown && pedalDamp >= 1.0f && ! frozen;
        if (damped && e < 1e-12f)          // ~ -120 dBFS: tail finished, free the voice
        {
            v.active = false;
            noteToVoice[(size_t) v.note] = -1;
        }
        else if (e < 1e-24f)               // held but silent: flush to avoid denormals
        {
            std::fill (std::begin (v.re), std::end (v.re), 0.0f);
            std::fill (std::begin (v.im), std::end (v.im), 0.0f);
            std::fill (std::begin (v.re2), std::end (v.re2), 0.0f);
            std::fill (std::begin (v.im2), std::end (v.im2), 0.0f);
        }
    }

    void updateBend() noexcept
    {
        if (bendCurrent == bendTarget) return;
        bendCurrent += (bendTarget - bendCurrent) * bendSmooth;
        if (std::abs (bendTarget - bendCurrent) < 1e-4f) bendCurrent = bendTarget;
        bendRatio = std::exp2 (bendCurrent / 12.0f);
        markAllDirty();
    }

    void markAllDirty() noexcept
    {
        for (auto& v : voices) v.coefDirty = true;
    }

    void rebuildAllActive() noexcept
    {
        for (auto& v : voices)
            if (v.active) buildLayout (v);
    }

    static constexpr float kExciteScale = 4.0f;
    // Coupling scale at Halo = 1. Exactly coincident partials would make this a pair loop gain
    // of 4, but partials of different strings never coincide within their ~0.1-1 Hz bandwidths
    // (inharmonicity, stretch, detune); the 56-string / 7-octave stress tests set this bound.
    static constexpr float kHaloMax = 4.0f;
    static constexpr int kHaloLanes = 24;     // lanes with a sympathetic state (multiple of 4)
    // Freeze T60 = 300 s. Deliberately not r = 1: float rounding of the pole (~6e-8/sample)
    // must stay well below the decay (~4.8e-7/sample) so a frozen chord can never grow.
    static constexpr float kFreezeSigma = 6.9078f / 300.0f;

    std::array<Voice, kMaxVoices> voices {};
    std::array<int, 128> noteToVoice {};
    Vec4 accL[kSubBlock], accR[kSubBlock], ownAcc[kSubBlock];
    float haloIn[kSubBlock] {}, prevTotal[kSubBlock] {}, nextTotal[kSubBlock] {};
    int prevN = 0;
    float haloNorm = 0.0f;

    ResonatorSettings settings;
    Tuning tuning;
    float sr = 48000.0f, invSr = 1.0f / 48000.0f;
    float bendTarget = 0.0f, bendCurrent = 0.0f, bendRatio = 1.0f, bendSmooth = 0.3f;
    float pedalDamp = 1.0f;   // 1 = dampers down (no pedal), 0 = fully lifted
    bool frozen = false;
    uint32_t ageCounter = 0;
};
} // namespace rv
