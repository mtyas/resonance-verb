// SPDX-License-Identifier: AGPL-3.0-or-later
// Resonance Verb — framework-independent DSP core.
// Input stage -> pre-delay -> sympathetic string matrix (+ halo, double decay)
//   -> soundboard body/bleed -> blur/diffusion -> wet EQ, dynamics, ducking, width, mix.
// Used by the JUCE processor and by the offline test harness.
// Real-time safe after prepare(): no allocation, no locks, no latency.
//
// String "holders": a string's damper is up when any active source holds it —
//   MIDI mode   : key currently down
//   Latch mode  : latched (a new chord, started after all keys were released, replaces the old one)
//   Scale mode  : in the selected scale/chord within [low, high]  OR  key currently down
// The core tracks these per note and only tells the bank when the union changes.
#pragma once

#include "Diffuser.h"
#include "InputStage.h"
#include "OutputStage.h"
#include "ResonatorBank.h"
#include "Scales.h"
#include "SoundboardBody.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace rv
{
enum class StringsMode : int { Midi = 0, Latch = 1, Scale = 2 };

struct CoreParams
{
    // input
    float hpHz = 40.0f, lpHz = 14000.0f, driveDb = 0.0f, exciteDb = 0.0f;
    int   exciteSource = InputStage::Main;
    // resonator
    ResonatorSettings res;
    Tuning tuning;
    float bendRangeSemis = 2.0f;
    // performance
    StringsMode mode = StringsMode::Midi;
    int   scaleRoot = 0, scaleType = 1, scaleLow = 36, scaleHigh = 84;
    float pedal = 0.0f;          // automatable pedal, combined (max) with MIDI CC64
    bool  freeze = false;
    // character
    float body = 0.3f, bodySize = 0.5f, bleed = 0.0f;
    // space / mix tools
    float preDelayMs = 0.0f, lowCutHz = 20.0f, tiltDb = 0.0f, duck = 0.0f, duckReleaseMs = 250.0f;
    // blur / diffusion
    float blurAmount = 0.35f, blurSize = 0.5f, blurTailSeconds = 1.5f, blurMod = 0.3f;
    // output
    float compThreshDb = -24.0f, compRatio = 3.0f, compAttackMs = 10.0f, compReleaseMs = 150.0f;
    float makeupDb = 12.0f, width = 1.0f, mix = 0.5f, outDb = 0.0f;
};

class ResonanceVerbCore
{
public:
    static constexpr int kChunk = 256;
    static constexpr int kMaxScaleStrings = ResonatorBank::kMaxVoices - 8;  // leave room for MIDI + tails
    static constexpr float kScaleVelocity = 0.8f;

    /** Allocates the diffuser delay lines: call from prepareToPlay, not the audio thread. */
    void prepare (double sampleRate)
    {
        input.prepare (sampleRate);
        bank.prepare (sampleRate);
        body.prepare (sampleRate);
        diffuser.prepare (sampleRate);
        sr = (float) sampleRate;
        preDelay.prepare ((int) (kMaxPreDelayMs * 0.001f * sr) + 8);
        preDelaySmooth = 1.0f - std::exp (-1.0f / (0.05f * sr));
        output.prepare (sampleRate);
        clearHolders();
    }

    void reset() noexcept
    {
        input.reset(); bank.reset(); body.reset(); diffuser.reset(); output.reset();
        preDelay.reset();
        preDelaySamples = preDelayTarget;
        clearHolders();
    }

    void setParams (const CoreParams& p) noexcept
    {
        input.set (p.hpHz, p.lpHz, p.driveDb, p.exciteDb);
        exciteSource = p.exciteSource;
        bank.setSettings (p.res);
        bank.setTuning (p.tuning);
        bendRange = p.bendRangeSemis;
        bank.setPitchBendSemitones (bendNorm * bendRange);
        diffuser.set (p.blurAmount, p.blurSize, p.blurTailSeconds, p.blurMod);
        body.set (p.body, p.bodySize, p.bleed);
        preDelayTarget = std::clamp (p.preDelayMs, 0.0f, kMaxPreDelayMs) * 0.001f * sr;
        output.setTone (p.lowCutHz, p.tiltDb);
        output.setDuck (p.duck, p.duckReleaseMs);
        output.set (p.compThreshDb, p.compRatio, p.compAttackMs, p.compReleaseMs,
                    p.makeupDb, p.width, p.mix, p.outDb);

        pedalParam = p.pedal;
        bank.setPedal (std::max (pedalParam, pedalMidi));
        bank.setFreeze (p.freeze);

        const bool scaleChanged = p.mode != mode || p.scaleRoot != scaleRoot || p.scaleType != scaleType
                               || p.scaleLow != scaleLow || p.scaleHigh != scaleHigh;
        if (scaleChanged || holdersDirty)
        {
            const bool leavingLatch = mode == StringsMode::Latch && p.mode != StringsMode::Latch;
            mode = p.mode; scaleRoot = p.scaleRoot; scaleType = p.scaleType;
            scaleLow = p.scaleLow; scaleHigh = p.scaleHigh;
            if (leavingLatch) latched.fill (0);
            rebuildScale();
            holdersDirty = false;
            for (int n = 0; n < 128; ++n) refresh (n);
        }
    }

    // --- MIDI ---
    void noteOn (int note, float velocity01) noexcept
    {
        if (note < 0 || note > 127) return;
        if (mode == StringsMode::Latch && keysDown == 0)
        {
            // first key of a new gesture: the previous latched chord is released
            for (int n = 0; n < 128; ++n)
                if (latched[(size_t) n]) { latched[(size_t) n] = 0; refresh (n); }
        }
        if (! midiDown[(size_t) note]) ++keysDown;
        midiDown[(size_t) note] = 1;
        if (mode == StringsMode::Latch) latched[(size_t) note] = 1;
        velocity[(size_t) note] = velocity01;
        refresh (note, true);
    }

    void noteOff (int note) noexcept
    {
        if (note < 0 || note > 127 || ! midiDown[(size_t) note]) return;
        midiDown[(size_t) note] = 0;
        --keysDown;
        refresh (note);
    }

    /** CC64 value / 127. */
    void setPedalMidi (float pedal01) noexcept
    {
        pedalMidi = std::clamp (pedal01, 0.0f, 1.0f);
        bank.setPedal (std::max (pedalParam, pedalMidi));
    }
    void setSustain (bool on) noexcept { setPedalMidi (on ? 1.0f : 0.0f); }

    void allNotesOff() noexcept
    {
        midiDown.fill (0); latched.fill (0); keysDown = 0;
        pedalMidi = 0.0f;
        bank.setPedal (pedalParam);
        for (int n = 0; n < 128; ++n) refresh (n);
    }

    /** normalized -1..+1 */
    void pitchBend (float normalized) noexcept
    {
        bendNorm = std::clamp (normalized, -1.0f, 1.0f);
        bank.setPitchBendSemitones (bendNorm * bendRange);
    }

    /** In-place on the main bus (right may be nullptr for mono). The sidechain is read-only
        and optional (scLeft == nullptr means not connected). */
    void process (float* left, float* right, int numSamples,
                  const float* scLeft = nullptr, const float* scRight = nullptr) noexcept
    {
        for (int o = 0; o < numSamples; o += kChunk)
        {
            const int m = std::min (kChunk, numSamples - o);
            float* l = left + o;
            float* r = right != nullptr ? right + o : nullptr;
            input.process (l, r, scLeft != nullptr ? scLeft + o : nullptr,
                           scRight != nullptr ? scRight + o : nullptr, exciteSource, mono, m);
            applyPreDelay (m);
            bank.process (mono, wetL, wetR, m);
            body.process (mono, wetL, wetR, m);
            diffuser.process (wetL, wetR, m);
            output.process (wetL, wetR, l, r, m);
        }
    }

    int   getActiveVoices() const noexcept     { return bank.activeVoiceCount(); }
    float getGainReductionDb() const noexcept  { return output.getGainReductionDb(); }
    float getDuckReductionDb() const noexcept  { return output.getDuckReductionDb(); }
    float getStringEnergy (int note) const noexcept { return bank.noteEnergy (note); }
    bool  isStringOpen (int note) const noexcept { return note >= 0 && note < 128 && open[(size_t) note]; }

private:
    static constexpr float kMaxPreDelayMs = 250.0f;

    void applyPreDelay (int m) noexcept
    {
        // Always written so turning pre-delay on never replays stale audio; read glides.
        for (int i = 0; i < m; ++i)
        {
            preDelaySamples += (preDelayTarget - preDelaySamples) * preDelaySmooth;
            const float x = mono[i];
            preDelay.write (x);
            if (preDelaySamples >= 1.0f)
                mono[i] = preDelay.read (preDelaySamples + 1.0f);   // +1: read after this write
            else if (preDelaySamples > 0.001f)
                mono[i] = x + preDelaySamples * (preDelay.read (2.0f) - x);  // fade 0..1 sample
        }
    }

    void clearHolders() noexcept
    {
        midiDown.fill (0); latched.fill (0); open.fill (0); keysDown = 0;
        pedalMidi = 0.0f;
        holdersDirty = true;   // re-apply Scale mode on the next setParams
    }

    void rebuildScale() noexcept
    {
        scaleOn.fill (0);
        if (mode != StringsMode::Scale) return;
        const auto& table = scaleTable();
        const uint16_t mask = table[(size_t) std::clamp (scaleType, 0, (int) table.size() - 1)].mask;
        const int lo = std::clamp (std::min (scaleLow, scaleHigh), 0, 127);
        const int hi = std::clamp (std::max (scaleLow, scaleHigh), 0, 127);
        int count = 0;
        for (int n = lo; n <= hi && count < kMaxScaleStrings; ++n)
        {
            const int pc = ((n - scaleRoot) % 12 + 12) % 12;
            if (mask & (1u << pc)) { scaleOn[(size_t) n] = 1; ++count; }
        }
    }

    void refresh (int n, bool retrigger = false) noexcept
    {
        const size_t i = (size_t) n;
        bool want = midiDown[i] != 0;
        if (mode == StringsMode::Latch) want = want || latched[i];
        if (mode == StringsMode::Scale) want = want || scaleOn[i];

        if (want && (! open[i] || retrigger))
            bank.noteOn (n, midiDown[i] ? velocity[i] : kScaleVelocity);
        else if (! want && open[i])
            bank.noteOff (n);
        open[i] = want ? 1 : 0;
    }

    InputStage input;
    ResonatorBank bank;
    SoundboardBody body;
    Diffuser diffuser;
    FracDelay preDelay;
    float sr = 48000.0f, preDelayTarget = 0.0f, preDelaySamples = 0.0f, preDelaySmooth = 0.001f;
    OutputStage output;

    std::array<uint8_t, 128> midiDown {}, latched {}, scaleOn {}, open {};
    std::array<float, 128> velocity {};
    int keysDown = 0;
    StringsMode mode = StringsMode::Midi;
    int scaleRoot = 0, scaleType = 1, scaleLow = 36, scaleHigh = 84;
    bool holdersDirty = true;
    float pedalParam = 0.0f, pedalMidi = 0.0f;
    int exciteSource = InputStage::Main;

    float bendNorm = 0.0f, bendRange = 2.0f;
    alignas (16) float mono[kChunk] {};
    alignas (16) float wetL[kChunk] {};
    alignas (16) float wetR[kChunk] {};
};
} // namespace rv
