// SPDX-License-Identifier: AGPL-3.0-or-later
// Resonance Verb — offline verification + benchmark + demo render (no JUCE needed).
//   rv_render                 run all checks, benchmark, write demo.wav / demo_dry.wav
//   rv_render --dump f.raw    render a deterministic scenario to raw floats
//   rv_render --compare a b   compare two dumps (e.g. SIMD vs RV_FORCE_SCALAR build)
//   rv_render --export-plots dir   write the CSV data behind the documentation figures
#define _USE_MATH_DEFINES   // M_PI on MSVC
#include "dsp/ResonanceVerbCore.h"

#include <chrono>
#include <filesystem>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if defined(__SSE2__) || defined(_M_X64)
  #include <pmmintrin.h>
#endif

using namespace rv;

static void enableFlushToZero()
{
#if defined(__SSE2__) || defined(_M_X64)
    _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE (_MM_DENORMALS_ZERO_ON);
#elif defined(__aarch64__) && ! defined(_MSC_VER)
    uint64_t fpcr;
    asm volatile ("mrs %0, fpcr" : "=r"(fpcr));
    asm volatile ("msr fpcr, %0" ::"r"(fpcr | (1ull << 24)));
#endif
}

static const char* simdName()
{
#if defined(RV_SIMD_SSE)
    return "SSE2";
#elif defined(RV_SIMD_NEON)
    return "NEON";
#else
    return "scalar";
#endif
}

struct Rng
{
    uint32_t s = 0x12345678u;
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) s / 2147483648.0f - 1.0f; }
};

static constexpr double kSr = 48000.0;
static int failures = 0;

static void check (bool ok, const char* what, const std::string& detail)
{
    std::printf ("  [%s] %-44s %s\n", ok ? "PASS" : "FAIL", what, detail.c_str());
    if (! ok) ++failures;
}

static double goertzelDb (const float* x, int n, double f)
{
    const double w = 2.0 * M_PI * f / kSr, c = 2.0 * std::cos (w);
    double s1 = 0, s2 = 0;
    for (int i = 0; i < n; ++i)
    {
        // Hann window to limit leakage
        const double win = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (n - 1));
        const double s0 = x[i] * win + c * s1 - s2;
        s2 = s1; s1 = s0;
    }
    const double p = s1 * s1 + s2 * s2 - c * s1 * s2;
    return 10.0 * std::log10 (p + 1e-30);
}

static double peakFreq (const float* x, int n, double lo, double hi, double step)
{
    double best = lo, bestDb = -1e9;
    for (double f = lo; f <= hi; f += step)
        if (const double d = goertzelDb (x, n, f); d > bestDb) { bestDb = d; best = f; }
    return best;
}

// Render through a bare ResonatorBank (wet only, mono sum of L+R).
template <typename Script>
static std::vector<float> renderBank (const ResonatorSettings& s, const Tuning& t, int numSamples,
                                      Script&& script, bool noise)
{
    static ResonatorBank bank; // large object: keep off the stack
    bank.prepare (kSr);
    bank.setSettings (s);
    bank.setTuning (t);
    Rng rng;
    std::vector<float> in ((size_t) numSamples, 0.0f), l ((size_t) numSamples), r ((size_t) numSamples), out ((size_t) numSamples);
    if (noise) for (auto& v : in) v = 0.1f * rng.next();
    else in[0] = 1.0f; // impulse

    constexpr int block = 64;
    for (int pos = 0; pos < numSamples; pos += block)
    {
        script (bank, pos);
        const int n = std::min (block, numSamples - pos);
        bank.process (in.data() + pos, l.data() + pos, r.data() + pos, n);
    }
    for (int i = 0; i < numSamples; ++i) out[(size_t) i] = l[(size_t) i] + r[(size_t) i];
    return out;
}

static std::string fmt (const char* f, double a, double b = 0, double c = 0)
{
    char buf[256];
    std::snprintf (buf, sizeof buf, f, a, b, c);
    return buf;
}

// ------------------------------------------------------------------ tests
static void testTuningMath()
{
    std::puts ("Tuning");
    Tuning t; t.stretch = 0;
    check (std::abs (t.frequency (69) - 440.0) < 1e-6, "A4 = 440 Hz (equal)", fmt ("%.4f Hz", t.frequency (69)));
    check (std::abs (t.frequency (60) - 261.6256) < 1e-3, "C4 = 261.626 Hz (equal)", fmt ("%.4f Hz", t.frequency (60)));
    t.temperament = Temperament::JustMajor;
    check (std::abs (t.frequency (64) - 330.0) < 0.01, "Just C major: E4 = 3/4 * A4", fmt ("%.4f Hz", t.frequency (64)));
    check (std::abs (t.frequency (60) - 264.0) < 0.01, "Just C major: C4 = 3/5 * A4", fmt ("%.4f Hz", t.frequency (60)));
    t.temperament = Temperament::Equal; t.stretch = 1;
    const double c8 = 1200.0 * std::log2 (t.frequency (108) / (440.0 * std::pow (2.0, 39 / 12.0)));
    check (c8 > 15 && c8 < 25, "Stretch: C8 sharp by ~20 cents", fmt ("%+.1f ct", c8));
}

static void testSelectivity()
{
    std::puts ("Sympathetic selectivity (white noise in, A4 held)");
    ResonatorSettings s; s.maxUnison = 1; s.inharmonicity = 0; s.hfDamping = 0;
    Tuning t; t.stretch = 0;
    const int N = (int) (kSr * 3);
    auto y = renderBank (s, t, N, [] (ResonatorBank& b, int pos) { if (pos == 0) b.noteOn (69, 1); }, true);
    const float* seg = y.data() + N - 48000;
    // Each harmonic vs. the bins a semitone either side of it (same spectral region).
    double worst = 1e9;
    for (double h : { 440.0, 880.0, 1320.0 })
    {
        const double on  = goertzelDb (seg, 48000, h);
        const double off = std::max (goertzelDb (seg, 48000, h * 1.059463), goertzelDb (seg, 48000, h / 1.059463));
        worst = std::min (worst, on - off);
    }
    check (worst > 30, "Harmonics of A vs. +-1 semitone", fmt ("worst-case %.1f dB separation", worst));

    auto silent = renderBank (s, t, 48000, [] (ResonatorBank&, int) {}, true);
    double e = 0; for (float v : silent) e += (double) v * v;
    check (e == 0.0, "No keys held -> no resonance", fmt ("energy %.3g", e));
}

static void testInharmonicity()
{
    std::puts ("Inharmonicity (A1 held, 10th partial)");
    ResonatorSettings s; s.inharmonicity = 1; s.hfDamping = 0; s.decaySeconds = 20;
    Tuning t; t.stretch = 0;
    const int N = (int) (kSr * 6);
    auto y = renderBank (s, t, N, [] (ResonatorBank& b, int pos) { if (pos == 0) b.noteOn (33, 1); }, true);
    const double B = ResonatorBank::inharmonicityB (33);
    const double predicted = 10 * 55.0 * std::sqrt (1 + B * 100);
    const double found = peakFreq (y.data() + N - 4 * 48000, 4 * 48000, 545, 575, 0.05);
    check (std::abs (found - predicted) < 0.3, "f10 = 10 f0 sqrt(1 + 100B)",
           fmt ("B=%.2e predicted %.2f Hz, measured %.2f Hz (harmonic would be 550)", B, predicted, found));
}

static void testDecayAndDamper()
{
    std::puts ("Decay & damper (impulse in, C4)");
    ResonatorSettings s; s.maxUnison = 1; s.hfDamping = 0; s.decaySeconds = 4; s.releaseSeconds = 0.2f; s.inharmonicity = 0;
    Tuning t; t.stretch = 0;
    const double f0 = t.frequency (60);
    const int N = (int) (kSr * 3);
    auto y = renderBank (s, t, N, [] (ResonatorBank& b, int pos) { if (pos == 0) b.noteOn (60, 1); }, false);
    const int W = 9600;
    const double d = goertzelDb (y.data() + 24000, W, f0) - goertzelDb (y.data() + 72000, W, f0);
    // power dB over 1 s; expected 60 dB / T60 = 15 dB
    check (std::abs (d - 15.0) < 1.0, "Held T60 = 4 s  (expect -15 dB/s)", fmt ("%.2f dB/s", d));

    int voicesAtEnd = -1;
    auto z = renderBank (s, t, N, [&] (ResonatorBank& b, int pos) {
        if (pos == 0) b.noteOn (60, 1);
        if (pos == 24000) b.noteOff (60);
        voicesAtEnd = b.activeVoiceCount(); }, false);
    const double drop = goertzelDb (z.data() + 24000 - W, W, f0) - goertzelDb (z.data() + 24000 + 9600, W, f0);
    check (drop > 40, "Damper falls on note-off", fmt (drop > 100 ? ">100 dB down 200 ms later (%.0f)" : "%.1f dB down 200 ms later", drop));
    check (voicesAtEnd == 0, "Voice freed after tail", fmt ("%.0f active voices", voicesAtEnd));

    // HF damping: partial 8 must decay faster than partial 1
    s.hfDamping = 0.5f;
    auto h = renderBank (s, t, N, [] (ResonatorBank& b, int pos) { if (pos == 0) b.noteOn (60, 1); }, false);
    const double d1 = goertzelDb (h.data() + 4800, W, f0) - goertzelDb (h.data() + 52800, W, f0);
    const double d8 = goertzelDb (h.data() + 4800, W, 8 * f0) - goertzelDb (h.data() + 52800, W, 8 * f0);
    check (d8 > d1 + 5, "Upper partials decay faster", fmt ("partial1 %.1f dB/s, partial8 %.1f dB/s", d1, d8));
}

static void testPitchBend()
{
    std::puts ("Pitch bend (A4 held, +2 semitones)");
    ResonatorSettings s; s.maxUnison = 1; s.inharmonicity = 0;
    Tuning t; t.stretch = 0;
    const int N = (int) (kSr * 4);
    auto y = renderBank (s, t, N, [] (ResonatorBank& b, int pos) {
        if (pos == 0) { b.noteOn (69, 1); b.setPitchBendSemitones (2.0f); } }, false);
    const double f = peakFreq (y.data() + N - 3 * 48000, 3 * 48000, 470, 520, 0.01);
    check (std::abs (f - 493.88) < 0.3, "Resonance follows bend to B4", fmt ("%.2f Hz (expect 493.88)", f));
}

static void testUnisonBeating()
{
    std::puts ("Unison strings");
    ResonatorSettings s; s.unisonDetune = 3.0f; s.inharmonicity = 0; s.hfDamping = 0;
    Tuning t; t.stretch = 0;
    const int N = (int) (kSr * 6);
    auto y = renderBank (s, t, N, [] (ResonatorBank& b, int pos) { if (pos == 0) b.noteOn (69, 1); }, true);
    // the trichord should show 3 distinct peaks within ±4 cents of 440
    const float* seg = y.data() + N - 4 * 48000;
    int peaks = 0; double prev2 = -1e9, prev1 = -1e9;
    for (double f = 438.5; f <= 441.5; f += 0.05)
    {
        const double d = goertzelDb (seg, 4 * 48000, f);
        if (prev1 > prev2 && prev1 > d) ++peaks;
        prev2 = prev1; prev1 = d;
    }
    check (ResonatorBank::stringsForNote (69, 3) == 3, "A4 modelled as trichord", "3 strings");
    check (peaks >= 2, "Detuned unisons resolved as separate modes", fmt ("%.0f peaks near 440 Hz", peaks));
}

static void testBlur()
{
    std::puts ("Blur / diffusion");
    static Diffuser d;
    auto run = [] (float amt, float size, float tail, float mod, int N, std::vector<float>& L, std::vector<float>& R, int input) {
        d.set (amt, size, tail, mod);
        d.prepare (kSr);
        L.assign ((size_t) N, 0.0f); R.assign ((size_t) N, 0.0f);
        Rng rng;
        for (int i = 0; i < N; ++i)
        {
            if (input == 0)      { L[(size_t) i] = 0.3f * rng.next(); R[(size_t) i] = 0.3f * rng.next(); }       // noise
            else if (input == 2) { L[(size_t) i] = R[(size_t) i] = 0.5f * (float) std::sin (2 * M_PI * 440 * i / kSr); } // sine
        }
        if (input == 1) L[0] = R[0] = 1.0f;                                                                      // impulse
        for (int pos = 0; pos < N; pos += 128) d.process (L.data() + pos, R.data() + pos, std::min (128, N - pos));
    };
    std::vector<float> L, R;

    // exact bypass
    run (0, 0.5f, 1.5f, 0.3f, 48000, L, R, 0);
    Rng rng; bool same = true;
    for (int i = 0; i < 48000; ++i) { same &= L[(size_t) i] == 0.3f * rng.next(); same &= R[(size_t) i] == 0.3f * rng.next(); }
    check (same, "Blur 0 = bit-exact bypass", same ? "identical" : "differs");

    // allpass chain only: energy preserved, attack smeared
    run (1, 0.5f, 0, 0, 48000, L, R, 1);
    double e = 0, cum = 0; for (float v : L) e += (double) v * v;
    int t90 = 0; for (int i = 0; i < 48000; ++i) { cum += (double) L[(size_t) i] * L[(size_t) i]; if (cum < 0.9 * e) t90 = i; }
    check (std::abs (10 * std::log10 (e)) < 0.5, "Diffuser is energy-preserving (allpass)", fmt ("impulse energy %+.2f dB", 10 * std::log10 (e)));
    check (t90 > 0.02 * kSr, "Impulse smeared in time", fmt ("90%% of energy spread over %.0f ms (dry: 0 ms)", 1000.0 * t90 / kSr));

    // with tail: decorrelation + decay
    run (1, 0.5f, 2.0f, 0.3f, (int) kSr * 2, L, R, 1);
    double lr = 0, ll = 0, rr = 0;
    for (size_t i = 0; i < L.size(); ++i) { lr += (double) L[i] * R[i]; ll += (double) L[i] * L[i]; rr += (double) R[i] * R[i]; }
    const double corr = lr / std::sqrt (ll * rr);
    check (std::abs (corr) < 0.3, "L/R decorrelated (wider image)", fmt ("correlation %.2f (dry mono: 1.00)", corr));
    auto win = [&] (double t0) { double s = 0; for (int i = (int) (t0 * kSr); i < (int) ((t0 + 0.1) * kSr); ++i) s += (double) L[(size_t) i] * L[(size_t) i] + (double) R[(size_t) i] * R[(size_t) i]; return 10 * std::log10 (s + 1e-30); };
    const double drop = win (0.4) - win (0.9);
    check (drop > 11 && drop < 22, "Blur tail T60 = 2 s (expect ~15 dB / 0.5 s)", fmt ("%.1f dB / 0.5 s", drop));

    // tuning untouched: a sine passes at unity (static), and Motion barely moves its pitch
    std::vector<float> ref ((size_t) kSr);
    for (int i = 0; i < (int) kSr; ++i) ref[(size_t) i] = 0.5f * (float) std::sin (2 * M_PI * 440 * (i + kSr) / kSr);
    const double refDb = goertzelDb (ref.data(), (int) kSr, 440);
    run (1, 0.5f, 0, 0, (int) kSr * 2, L, R, 2);
    const double g0 = goertzelDb (L.data() + (int) kSr, (int) kSr, 440) - refDb;
    run (1, 0.5f, 0, 0.3f, (int) kSr * 2, L, R, 2);
    const double g3 = goertzelDb (L.data() + (int) kSr, (int) kSr, 440) - refDb;
    check (std::abs (g0) < 0.2, "Pitched content passes at unity", fmt ("440 Hz gain %+.2f dB", g0));
    check (std::abs (g3) < 0.5, "Default Motion keeps 440 Hz focused", fmt ("440 Hz gain %+.2f dB at Motion 0.3", g3));

    // stability at extremes
    run (1, 1, 10, 1, (int) kSr * 10, L, R, 0);
    bool finite = true; float peak = 0;
    for (size_t i = 0; i < L.size(); ++i) { finite &= std::isfinite (L[i]) && std::isfinite (R[i]); peak = std::max ({ peak, std::abs (L[i]), std::abs (R[i]) }); }
    check (finite && peak < 4, "Stable at max size/tail/motion (10 s noise)", fmt ("peak %.2f", peak));
}

static void testPerformance()
{
    std::puts ("Strings modes / pedal / freeze / sidechain");
    static ResonanceVerbCore core;
    CoreParams p; p.mix = 1.0f; p.blurAmount = 0.0f;
    auto openCount = [&] { int c = 0; for (int n = 0; n < 128; ++n) c += core.isStringOpen (n); return c; };

    // ---- Latch
    p.mode = StringsMode::Latch;
    core.prepare (kSr); core.setParams (p);
    core.noteOn (60, 1); core.noteOn (64, 1); core.noteOff (60); core.noteOff (64);
    const bool held = core.isStringOpen (60) && core.isStringOpen (64);
    core.noteOn (67, 1);
    const bool replaced = ! core.isStringOpen (60) && ! core.isStringOpen (64) && core.isStringOpen (67);
    core.noteOn (71, 1);
    const bool added = core.isStringOpen (67) && core.isStringOpen (71);
    check (held, "Latch: chord stays open after release", held ? "C+E open" : "closed");
    check (replaced && added, "Latch: next gesture replaces, overlap adds", replaced && added ? "G+B replaced C+E" : "wrong");
    core.noteOff (67); core.noteOff (71);

    // ---- Scale
    p.mode = StringsMode::Scale; p.scaleRoot = 0; p.scaleType = 1; p.scaleLow = 48; p.scaleHigh = 72;
    core.setParams (p);
    const int cMajor = openCount();
    check (cMajor == 15 && core.isStringOpen (62) && ! core.isStringOpen (61), "Scale: C major C3..C5 opens 15 strings",
           fmt ("%.0f open, D open, C# closed", cMajor));
    p.scaleRoot = 2; core.setParams (p);
    check (core.isStringOpen (61) && ! core.isStringOpen (60), "Scale: root change re-tunes the open set", "D major: C# open, C closed");
    core.noteOn (60, 1);
    const bool midiOnTop = core.isStringOpen (60);
    core.noteOff (60);
    check (midiOnTop && ! core.isStringOpen (60), "Scale: MIDI adds strings on top", "C opened by key, closed on release");
    p.scaleType = 0; p.scaleLow = 21; p.scaleHigh = 108; core.setParams (p);
    check (openCount() == ResonanceVerbCore::kMaxScaleStrings, "Scale: chromatic capped to voice budget", fmt ("%.0f strings", openCount()));
    p.mode = StringsMode::Midi; core.setParams (p);
    check (openCount() == 0, "Back to MIDI mode closes scale strings", fmt ("%.0f open", openCount()));

    // ---- Scale mode makes sound with no MIDI; sidechain routing
    auto lastHalfSecond = [&] (CoreParams cp, bool mainNoise, bool scNoise, bool connectSc, std::vector<int> notes) {
        core.prepare (kSr); core.setParams (cp);
        for (int n : notes) core.noteOn (n, 1);
        const int N = (int) kSr * 2;
        std::vector<float> L ((size_t) N), R ((size_t) N), SL ((size_t) N), SR ((size_t) N);
        Rng rng;
        for (int i = 0; i < N; ++i)
        {
            const float a = rng.next(), b = rng.next();
            L[(size_t) i] = R[(size_t) i] = mainNoise ? 0.2f * a : 0.0f;
            SL[(size_t) i] = SR[(size_t) i] = scNoise ? 0.2f * b : 0.0f;
        }
        for (int pos = 0; pos < N; pos += 128)
            core.process (L.data() + pos, R.data() + pos, std::min (128, N - pos),
                          connectSc ? SL.data() + pos : nullptr, connectSc ? SR.data() + pos : nullptr);
        double e = 0;
        for (int i = N - (int) kSr / 2; i < N; ++i) e += (double) L[(size_t) i] * L[(size_t) i];
        return e; };

    CoreParams sp = p; sp.mode = StringsMode::Scale; sp.scaleType = 1; sp.scaleLow = 48; sp.scaleHigh = 72;
    const double eScale = lastHalfSecond (sp, true, false, false, {});
    const double eNone  = lastHalfSecond (p, true, false, false, {});
    check (eScale > 1e-3 && eNone == 0.0, "Scale mode resonates without any MIDI", fmt ("energy %.3g vs %.3g in MIDI mode, no keys", eScale, eNone));

    CoreParams sc = p; sc.exciteSource = InputStage::Sidechain;
    const double eMainOnly = lastHalfSecond (sc, true, false, true, { 60, 64, 67 });
    const double eScOnly   = lastHalfSecond (sc, false, true, true, { 60, 64, 67 });
    const double eFallback = lastHalfSecond (sc, true, false, false, { 60, 64, 67 });
    check (eMainOnly == 0.0 && eScOnly > 1e-3, "Sidechain drives the strings, main doesn't", fmt ("main-only %.3g, sidechain-only %.3g", eMainOnly, eScOnly));
    check (eFallback > 1e-3, "Unconnected sidechain falls back to main", fmt ("energy %.3g", eFallback));

    // ---- Half pedal (bank level, impulse, C4, key up)
    ResonatorSettings s; s.maxUnison = 1; s.hfDamping = 0; s.decaySeconds = 4; s.releaseSeconds = 0.2f; s.inharmonicity = 0;
    Tuning t; t.stretch = 0;
    const double f0 = t.frequency (60);
    int voices = 0;
    auto y = renderBank (s, t, (int) kSr, [&] (ResonatorBank& b, int pos) {
        if (pos == 0) { b.setPedal (0.5f); b.noteOn (60, 1); b.noteOff (60); }
        voices = b.activeVoiceCount(); }, false);
    const double rate = (goertzelDb (y.data() + 4800, 4800, f0) - goertzelDb (y.data() + 14400, 4800, f0)) / 0.2;
    const double sigH = 6.9078 / 4.0, sigD = sigH + 6.9078 / 0.2 * (1 + 0.5 * f0 / 1000);
    const double expect = std::sqrt (sigH * sigD) * 8.6859;
    check (std::abs (rate - expect) < 0.15 * expect && voices == 1, "Half-pedal: decay between lifted and damped",
           fmt ("%.0f dB/s (lifted %.0f, half expected %.0f)", rate, sigH * 8.6859, expect));

    // ---- Freeze: hold level, ignore new input (input jumps +20 dB after freezing)
    static ResonatorBank bank;
    bank.prepare (kSr); bank.setSettings (s); bank.setTuning (t);
    const int N = (int) kSr * 5;
    std::vector<float> in ((size_t) N), l ((size_t) N), r ((size_t) N);
    Rng rng;
    for (int i = 0; i < N; ++i) in[(size_t) i] = (i < (int) kSr ? 0.1f : 1.0f) * rng.next();
    bank.noteOn (60, 1);
    for (int pos = 0; pos < N; pos += 64)
    {
        if (pos == (int) kSr) bank.setFreeze (true);
        if (pos == (int) (kSr * 1.5)) bank.noteOff (60);
        bank.process (in.data() + pos, l.data() + pos, r.data() + pos, 64);
    }
    for (int i = 0; i < N; ++i) l[(size_t) i] += r[(size_t) i];
    const double change = goertzelDb (l.data() + 4 * (int) kSr, (int) kSr, f0) - goertzelDb (l.data() + 2 * (int) kSr, (int) kSr, f0);
    check (change < 0.0 && change > -1.0, "Freeze holds (and ignores +20 dB new input)", fmt ("%+.2f dB over 2 s", change));
    bank.setFreeze (false);
    for (int pos = 0; pos < (int) kSr; pos += 64) bank.process (in.data() + pos, l.data() + pos, r.data() + pos, 64);
    check (bank.activeVoiceCount() == 0, "Unfreeze releases (key already up)", fmt ("%.0f voices after 1 s", bank.activeVoiceCount()));

    // ---- Released notes must free even while audio keeps playing
    bank.prepare (kSr);
    for (int n = 48; n < 72; ++n) bank.noteOn (n, 1);
    for (int pos = 0; pos < (int) kSr; pos += 64)
    {
        if (pos == 192 * 64) for (int n = 48; n < 72; ++n) bank.noteOff (n);   // 0.256 s
        bank.process (in.data() + (int) kSr + pos, l.data(), r.data(), 64);   // loud, continuous noise
    }
    check (bank.activeVoiceCount() == 0, "Released voices free under continuous input", fmt ("%.0f of 24 voices left 0.74 s after release", bank.activeVoiceCount()));
}

static void testCharacter()
{
    std::puts ("Character: double decay / halo / body");
    Tuning t; t.stretch = 0;

    // ---- Double decay: A4 trichord, unisons in tune so beating doesn't confuse the envelope
    auto decayRates = [&] (float dd) {
        ResonatorSettings s; s.unisonDetune = 0; s.inharmonicity = 0; s.hfDamping = 0; s.decaySeconds = 6; s.doubleDecay = dd; s.halo = 0;
        auto y = renderBank (s, t, (int) kSr * 4, [] (ResonatorBank& b, int pos) { if (pos == 0) b.noteOn (69, 1); }, false);
        const int W = 4800;
        const double early = (goertzelDb (y.data() + 2400, W, 440) - goertzelDb (y.data() + 2400 + 14400, W, 440)) / 0.3;
        const double late  = (goertzelDb (y.data() + 2 * 48000, W, 440) - goertzelDb (y.data() + 3 * 48000, W, 440)) / 1.0;
        return std::make_pair (early, late); };
    const auto [e0, l0] = decayRates (0.0f);
    const auto [e1, l1] = decayRates (1.0f);
    check (std::abs (e0 / l0 - 1.0) < 0.15, "Double decay off: single-slope decay", fmt ("early %.1f dB/s, late %.1f dB/s", e0, l0));
    check (e1 > 2.0 * l1, "Double decay on: fast prompt, slow aftersound", fmt ("early %.1f dB/s, late %.1f dB/s", e1, l1));

    // ---- Halo: C5 opened in silence picks up energy from a ringing C4 (its 2nd partial)
    auto pickup = [&] (float halo) {
        static ResonatorBank b;
        ResonatorSettings s; s.halo = halo;
        b.prepare (kSr); b.setSettings (s); b.setTuning (t);
        const int N = (int) kSr * 2;
        std::vector<float> in ((size_t) N, 0.0f), l ((size_t) N), r ((size_t) N);
        Rng rng; for (int i = 0; i < (int) kSr; ++i) in[(size_t) i] = 0.2f * rng.next();
        b.noteOn (60, 1);
        for (int pos = 0; pos < N; pos += 64)
        {
            if (pos == 50 * 64 * 16) b.noteOn (72, 1);   // ~1.07 s: input already silent
            b.process (in.data() + pos, l.data() + pos, r.data() + pos, 64);
        }
        return b.noteEnergy (72); };
    const double off = pickup (0.0f), on = pickup (1.0f);
    check (off == 0.0 && on > 1e-8, "Halo: open C5 rings from C4 through the bridge", fmt ("C5 energy %.3g (halo off: %.3g)", on, off));

    // ---- Halo stability, worst cases
    auto stress = [&] (std::vector<int> notes, float halo) {
        static ResonatorBank b;
        // Worst case: inharmonicity, detune and stretch at 0 so octave partials coincide exactly.
        ResonatorSettings s; s.halo = halo; s.decaySeconds = 30; s.doubleDecay = 0; s.hfDamping = 0;
        s.inharmonicity = 0; s.unisonDetune = 0; s.brightness = 1;
        b.prepare (kSr); b.setSettings (s); b.setTuning (t);
        for (int n : notes) b.noteOn (n, 1);
        const int N = (int) kSr * 20;
        std::vector<float> in ((size_t) N, 0.0f), l ((size_t) N), r ((size_t) N);
        Rng rng; for (int i = 0; i < (int) kSr; ++i) in[(size_t) i] = 0.3f * rng.next();
        for (int pos = 0; pos < N; pos += 64) b.process (in.data() + pos, l.data() + pos, r.data() + pos, 64);
        auto e = [&] (int s0) { double a = 0; for (int i = s0; i < s0 + 24000; ++i) a += (double) l[(size_t) i] * l[(size_t) i]; return 10 * std::log10 (a + 1e-30); };
        bool finite = true; for (float v : l) finite &= std::isfinite (v);
        double peak = -1e9; for (int sec = 1; sec < 20; ++sec) peak = std::max (peak, e (sec * (int) kSr));
        return std::make_tuple (e ((int) kSr), peak, e (N - 24000), finite); };
    std::vector<int> chromatic; for (int n = 36; n < 92; ++n) chromatic.push_back (n);
    std::vector<int> octaves { 24, 36, 48, 60, 72, 84, 96 };
    for (auto& [name, notes] : { std::pair<const char*, std::vector<int>> { "56 chromatic strings", chromatic }, { "7-octave stack", octaves } })
    {
        const auto [startOn, peakOn, endOn, finOn] = stress (notes, 1.0f);
        const double swell = peakOn - startOn, decayOn = startOn - endOn;
        check (finOn && swell < 3.0 && decayOn > 10.0, (std::string ("Halo 1, exact partials: ") + name).c_str(),
               fmt ("bounded (max swell %+.1f dB), decays %.1f dB by 19.5 s", swell, decayOn));
    }
}

static void testMixTools()
{
    std::puts ("Mix tools: bleed / pre-delay / wet EQ / ducking");
    static ResonanceVerbCore core;
    auto render = [&] (CoreParams p, std::vector<int> notes, int N, bool impulse, float level, std::vector<float>& L) {
        core.prepare (kSr); core.setParams (p); core.reset(); core.setParams (p);
        for (int n : notes) core.noteOn (n, 1);
        L.assign ((size_t) N, 0.0f); std::vector<float> R ((size_t) N, 0.0f);
        Rng rng;
        if (impulse) L[0] = R[0] = 1.0f;
        else for (int i = 0; i < N; ++i) L[(size_t) i] = R[(size_t) i] = level * rng.next();
        for (int pos = 0; pos < N; pos += 128) core.process (L.data() + pos, R.data() + pos, std::min (128, N - pos));
    };
    auto energyDb = [] (const std::vector<float>& x, int from) { double e = 0; for (size_t i = (size_t) from; i < x.size(); ++i) e += (double) x[i] * x[i]; return 10 * std::log10 (e + 1e-30); };
    CoreParams base; base.mix = 1; base.blurAmount = 0; base.compRatio = 1; base.makeupDb = 0; base.body = 0;
    std::vector<float> L;

    // Bleed: soundboard rings with no strings open; nothing at all when Bleed = 0
    CoreParams b0 = base; b0.body = 1; render (b0, {}, (int) kSr, false, 0.2f, L);
    const double silent = energyDb (L, 24000);
    std::vector<float> ref ((size_t) kSr); Rng rr; for (auto& v : ref) v = 0.2f * rr.next();
    const double inDb = energyDb (ref, 24000);
    CoreParams b1 = b0; b1.bleed = 1; render (b1, {}, (int) kSr, false, 0.2f, L);
    const double bled = energyDb (L, 24000) - inDb;
    check (silent < -250 && bled > -16 && bled < -4, "Bleed: board rings with every damper down", fmt ("%.1f dB re input (Bleed 0: silent)", bled));

    // Pre-delay
    auto onset = [&] (float ms) {
        CoreParams p = base; p.preDelayMs = ms; render (p, { 69 }, (int) kSr / 2, true, 0, L);
        for (size_t i = 0; i < L.size(); ++i) if (std::abs (L[i]) > 1e-5f) return (double) i / kSr * 1000.0;
        return -1.0; };
    const double d0 = onset (0), d100 = onset (100);
    check (d0 < 1.0 && std::abs (d100 - 100.0) < 1.5, "Pre-delay shifts the wet onset", fmt ("onset %.1f ms (0 ms setting: %.1f ms)", d100, d0));

    // Low cut + tilt on A1 + A5 strings
    auto bands = [&] (float lowCut, float tiltDb) {
        CoreParams p = base; p.lowCutHz = lowCut; p.tiltDb = tiltDb; p.res.maxUnison = 1; p.res.inharmonicity = 0; p.tuning.stretch = 0; p.res.halo = 0;
        render (p, { 33, 81 }, (int) kSr * 2, false, 0.1f, L);
        return std::make_pair (goertzelDb (L.data() + 48000, 48000, 55), goertzelDb (L.data() + 48000, 48000, 880)); };
    const auto [lo0, hi0] = bands (20, 0);
    const auto [loC, hiC] = bands (300, 0);
    const auto [loT, hiT] = bands (20, 6);
    check (std::abs ((lo0 - loC) - 29.4) < 2.0 && std::abs (hi0 - hiC) < 1.0, "Wet low cut 300 Hz (12 dB/oct): expect -29.4 dB",
           fmt ("55 Hz %+.1f dB, 880 Hz %+.1f dB", loC - lo0, hiC - hi0));
    const double tiltSpread = (hiT - hi0) - (loT - lo0);
    check (std::abs (tiltSpread - 5.0) < 1.5, "Tilt +6 dB brightens the wet", fmt ("880 Hz vs 55 Hz moved %+.1f dB", tiltSpread));

    // Ducking: loud dry input pulls the wet down by up to 24 dB
    CoreParams dk = base; render (dk, { 60, 64, 67 }, (int) kSr * 2, false, 0.5f, L);
    const double wetNoDuck = energyDb (L, 48000);
    dk.duck = 1; render (dk, { 60, 64, 67 }, (int) kSr * 2, false, 0.5f, L);
    const double wetDuck = energyDb (L, 48000);
    check (std::abs ((wetNoDuck - wetDuck) - 24.0) < 2.0, "Ducking: wet drops under loud dry signal", fmt ("%.1f dB reduction (expected 24)", wetNoDuck - wetDuck));
}

// ------------------------------------------------------------------ full chain
static CoreParams defaultParams()
{
    CoreParams p;
    p.mix = 1.0f;
    return p;
}

static void scenario (ResonanceVerbCore& core, std::vector<float>& L, std::vector<float>& R, int numSamples, int numNotes)
{
    Rng rng;
    L.assign ((size_t) numSamples, 0); R.assign ((size_t) numSamples, 0);
    for (int i = 0; i < numSamples; ++i) { L[(size_t) i] = 0.25f * rng.next(); R[(size_t) i] = 0.25f * rng.next(); }
    for (int k = 0; k < numNotes; ++k) core.noteOn (36 + k, 0.8f);
    constexpr int block = 128;
    for (int pos = 0; pos < numSamples; pos += block)
    {
        if (pos == numSamples / 2) core.pitchBend (0.5f);
        core.process (L.data() + pos, R.data() + pos, std::min (block, numSamples - pos));
    }
}

static void benchmark()
{
    std::printf ("Performance (%s, 48 kHz, block 128, full chain)\n", simdName());
    for (int notes : { 1, 8, 24, 48 })
    {
        static ResonanceVerbCore core;
        core.prepare (kSr);
        core.setParams (defaultParams());
        std::vector<float> L, R;
        const int N = (int) (kSr * 20);
        const auto t0 = std::chrono::steady_clock::now();
        scenario (core, L, R, N, notes);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        bool finite = true; float peak = 0;
        for (size_t i = 0; i < L.size(); ++i) { finite &= std::isfinite (L[i]) && std::isfinite (R[i]); peak = std::max (peak, std::abs (L[i])); }
        std::printf ("  %2d notes held: %6.1fx realtime  (%5.2f%% of one core)  peak %.2f  %s\n",
                     notes, 20.0 / secs, 100.0 * secs / 20.0, peak, finite ? "finite" : "NaN/Inf!");
        if (! finite || peak > 1.01f) ++failures;
    }
}

// ------------------------------------------------------------------ demo render
static void writeWav (const char* path, const std::vector<float>& L, const std::vector<float>& R)
{
    FILE* f = std::fopen (path, "wb");
    if (! f) return;
    const uint32_t n = (uint32_t) L.size(), dataBytes = n * 4, sr = (uint32_t) kSr, byteRate = sr * 4;
    const uint16_t fmtTag = 1, ch = 2, align = 4, bits = 16;
    const uint32_t riffSize = 36 + dataBytes, fmtSize = 16;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riffSize, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f);
    std::fwrite (&fmtSize, 4, 1, f); std::fwrite (&fmtTag, 2, 1, f); std::fwrite (&ch, 2, 1, f);
    std::fwrite (&sr, 4, 1, f); std::fwrite (&byteRate, 4, 1, f); std::fwrite (&align, 2, 1, f); std::fwrite (&bits, 2, 1, f);
    std::fwrite ("data", 1, 4, f); std::fwrite (&dataBytes, 4, 1, f);
    for (uint32_t i = 0; i < n; ++i)
        for (float v : { L[i], R[i] })
        {
            const int16_t s = (int16_t) std::lround (std::max (-1.0f, std::min (1.0f, v)) * 32767.0f);
            std::fwrite (&s, 2, 1, f);
        }
    std::fclose (f);
}

static void renderDemo (bool blur)
{
    // Dry "drum loop" (kick + snare-ish noise + hats) at 96 bpm through four held chords.
    const int N = (int) (kSr * 18);
    std::vector<float> L ((size_t) N, 0), R ((size_t) N, 0);
    Rng rng;
    const double beat = 60.0 / 96.0;
    for (int i = 0; i < N; ++i)
    {
        const double t = i / kSr;
        const double inBeat = std::fmod (t, beat), inHalf = std::fmod (t, beat / 2);
        const int beatIdx = (int) (t / beat);
        float x = 0;
        if (beatIdx % 2 == 0) x += 0.7f * (float) (std::exp (-inBeat * 25) * std::sin (2 * M_PI * (50 + 120 * std::exp (-inBeat * 40)) * inBeat));
        else                  x += 0.35f * rng.next() * (float) std::exp (-inBeat * 18);
        x += 0.08f * rng.next() * (float) std::exp (-inHalf * 90);
        if (t > 16.0) x = 0; // let the strings ring out
        L[(size_t) i] = R[(size_t) i] = x;
    }
    writeWav ("demo_dry.wav", L, R);

    static ResonanceVerbCore core;
    core.prepare (kSr);
    CoreParams p; p.mix = 0.65f; p.res.decaySeconds = 10; p.makeupDb = 14;
    if (blur) { p.blurAmount = 0.6f; p.blurSize = 0.6f; p.blurTailSeconds = 2.5f; p.blurMod = 0.4f; }
    else p.blurAmount = 0.0f;
    core.setParams (p);

    const std::vector<std::vector<int>> chords = {
        { 36, 48, 55, 59, 64, 67 },   // Cmaj7
        { 33, 45, 52, 59, 60, 64 },   // Am9
        { 29, 41, 48, 57, 64, 71 },   // Fmaj7#11
        { 31, 43, 50, 57, 59, 64 } }; // G6/9
    const int chordLen = (int) (kSr * 4);
    std::vector<int> held;
    constexpr int block = 128;
    for (int pos = 0; pos < N; pos += block)
    {
        if (pos % chordLen < block && pos / chordLen < 4)
        {
            for (int n : held) core.noteOff (n);
            held = chords[(size_t) (pos / chordLen)];
            for (int n : held) core.noteOn (n, 0.85f);
        }
        if (pos >= 16 * (int) kSr && ! held.empty()) { for (int n : held) core.noteOff (n); held.clear(); }
        const double t = pos / kSr;
        if (t > 12.0 && t < 16.0) core.pitchBend ((float) (0.15 * std::sin (2 * M_PI * 0.5 * (t - 12.0))));
        core.process (L.data() + pos, R.data() + pos, std::min (block, N - pos));
    }
    writeWav (blur ? "demo.wav" : "demo_noblur.wav", L, R);
    float peak = 0; size_t hot = 0; double e = 0;
    for (size_t i = 0; i < L.size(); ++i)
    {
        const float a = std::max (std::abs (L[i]), std::abs (R[i]));
        peak = std::max (peak, a); hot += a > 0.8f; e += 0.5 * ((double) L[i] * L[i] + (double) R[i] * R[i]);
    }
    std::printf ("%s: peak %.2f, RMS %.1f dBFS, %.3f%% of samples in soft-clip region\n",
                 blur ? "demo.wav (blur)   " : "demo_noblur.wav   ", peak, 10 * std::log10 (e / L.size()), 100.0 * hot / L.size());
}

// ------------------------------------------------------------------ SIMD vs scalar
static int dump (const char* path)
{
    static ResonanceVerbCore core;
    core.prepare (kSr);
    core.setParams (defaultParams());
    std::vector<float> L, R;
    scenario (core, L, R, (int) kSr * 2, 24);
    FILE* f = std::fopen (path, "wb");
    std::fwrite (L.data(), 4, L.size(), f);
    std::fwrite (R.data(), 4, R.size(), f);
    std::fclose (f);
    std::printf ("dumped %s (%s)\n", path, simdName());
    return 0;
}

static int compare (const char* a, const char* b)
{
    auto load = [] (const char* p) {
        std::vector<float> v; FILE* f = std::fopen (p, "rb"); float x;
        while (f && std::fread (&x, 4, 1, f) == 1) v.push_back (x);
        if (f) std::fclose (f);
        return v; };
    auto x = load (a), y = load (b);
    if (x.size() != y.size() || x.empty()) { std::puts ("size mismatch"); return 1; }
    double maxDiff = 0, peak = 0;
    for (size_t i = 0; i < x.size(); ++i) { maxDiff = std::max (maxDiff, (double) std::abs (x[i] - y[i])); peak = std::max (peak, (double) std::abs (x[i])); }
    const double db = 20 * std::log10 (maxDiff / peak + 1e-30);
    std::printf ("max |diff| = %.3g  (%.1f dB re peak) -> %s\n", maxDiff, db, db < -80 ? "MATCH" : "MISMATCH");
    return db < -80 ? 0 : 1;
}

// ------------------------------------------------------------------ data for the documentation plots
static void exportPlots (const std::string& dir)
{
    std::filesystem::create_directories (dir);
    auto open = [&] (const char* name) { return std::fopen ((dir + "/" + name).c_str(), "w"); };
    Tuning t; t.stretch = 0;

    // 1. Spectrum: C-major triad (C3 E3 G3) held, white noise in. Welch average of 6 one-second windows.
    {
        ResonatorSettings s;
        const int N = (int) kSr * 8;
        auto y = renderBank (s, t, N, [] (ResonatorBank& b, int pos) { if (pos == 0) { b.noteOn (48, 1); b.noteOn (52, 1); b.noteOn (55, 1); } }, true);
        FILE* f = open ("spectrum.csv");
        std::fprintf (f, "hz,wet_db\n");
        for (int k = 0; k <= 900; ++k)
        {
            const double hz = 80.0 * std::pow (1100.0 / 80.0, k / 900.0);
            double p = 0;
            for (int w = 0; w < 6; ++w) p += std::pow (10.0, goertzelDb (y.data() + (2 + w) * 48000, 48000, hz) / 10.0);
            std::fprintf (f, "%.3f,%.2f\n", hz, 10.0 * std::log10 (p / 6.0));
        }
        std::fclose (f);
    }

    // 2. Envelopes (20 ms RMS) for an impulse: double decay off/on (A4), and C4 after note-off at 3 pedal depths
    auto envelope = [] (const std::vector<float>& y, int i0) { double e = 0; for (int i = i0; i < i0 + 960; ++i) e += (double) y[(size_t) i] * y[(size_t) i]; return 10 * std::log10 (e / 960 + 1e-20); };
    {
        std::vector<std::vector<float>> runs;
        for (float dd : { 0.0f, 1.0f })
        {
            ResonatorSettings s; s.unisonDetune = 0; s.inharmonicity = 0; s.hfDamping = 0; s.decaySeconds = 6; s.doubleDecay = dd; s.halo = 0;
            runs.push_back (renderBank (s, t, (int) kSr * 4, [] (ResonatorBank& b, int pos) { if (pos == 0) b.noteOn (69, 1); }, false));
        }
        FILE* f = open ("double_decay.csv");
        std::fprintf (f, "t,off_db,on_db\n");
        for (int w = 0; w < 4 * 50 - 1; ++w)
            std::fprintf (f, "%.3f,%.2f,%.2f\n", w * 0.02, envelope (runs[0], w * 960), envelope (runs[1], w * 960));
        std::fclose (f);
    }
    {
        std::vector<std::vector<float>> runs;
        for (float pedal : { 0.0f, 0.5f, 1.0f })
        {
            ResonatorSettings s; s.maxUnison = 1; s.hfDamping = 0.35f; s.decaySeconds = 6; s.releaseSeconds = 0.2f;
            runs.push_back (renderBank (s, t, (int) kSr * 3, [pedal] (ResonatorBank& b, int pos)
            {
                if (pos == 0) { b.setPedal (pedal); b.noteOn (60, 1); }
                if (pos == 32 * 64) b.noteOff (60);   // key up at ~43 ms
            }, false));
        }
        FILE* f = open ("pedal.csv");
        std::fprintf (f, "t,down_db,half_db,lifted_db\n");
        for (int w = 0; w < 3 * 50 - 1; ++w)
            std::fprintf (f, "%.3f,%.2f,%.2f,%.2f\n", w * 0.02, envelope (runs[0], w * 960), envelope (runs[1], w * 960), envelope (runs[2], w * 960));
        std::fclose (f);
    }
    std::printf ("wrote spectrum.csv, double_decay.csv, pedal.csv to %s\n", dir.c_str());
}

int main (int argc, char** argv)
{
    enableFlushToZero();
    if (argc == 3 && std::strcmp (argv[1], "--dump") == 0) return dump (argv[2]);
    if (argc == 4 && std::strcmp (argv[1], "--compare") == 0) return compare (argv[2], argv[3]);
    if (argc == 3 && std::strcmp (argv[1], "--export-plots") == 0) { exportPlots (argv[2]); return 0; }

    std::printf ("Resonance Verb DSP verification — SIMD path: %s\n\n", simdName());
    testTuningMath();
    testSelectivity();
    testInharmonicity();
    testDecayAndDamper();
    testPitchBend();
    testUnisonBeating();
    testBlur();
    testPerformance();
    testCharacter();
    testMixTools();
    std::puts ("");
    benchmark();
    std::puts ("");
    renderDemo (false);
    renderDemo (true);
    std::puts ("Wrote demo_dry.wav, demo_noblur.wav, demo.wav (18 s, drums through Cmaj7 / Am9 / Fmaj7#11 / G6/9)");
    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL CHECKS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
