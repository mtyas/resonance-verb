// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "dsp/ResonanceVerbCore.h"
#include "PresetManager.h"

class ResonanceVerbProcessor final : public juce::AudioProcessor,
                                     private juce::AudioProcessorParameter::Listener
{
public:
    ResonanceVerbProcessor();
    ~ResonanceVerbProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Resonance Verb"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    // Factory presets as host programs
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    /** Notes played on the editor's keyboard are merged into the MIDI stream. */
    juce::MidiKeyboardState keyboardState;

    // ---- undo / redo (message thread). Records UI/host-UI gestures and preset loads;
    //      automation and MIDI-CC movements are deliberately not recorded.
    juce::UndoManager& getUndoManager() noexcept { return undoManager; }
    std::vector<float> captureValues() const;
    void applyValuesWithUndo (const juce::String& actionName, const std::vector<float>& values);

    // ---- presets (message thread)
    rv::PresetManager& getPresetManager() noexcept { return *presetManager; }

    // ---- MIDI learn
    void startMidiLearn (int paramIndex) noexcept;          // -1 cancels
    int  getMidiLearnTarget() const noexcept { return learnTarget.load(); }
    int  getCcForParameter (int paramIndex) const noexcept;
    void clearMidiMapping (int paramIndex) noexcept;
    void clearAllMidiMappings() noexcept;
    int  getMidiMapVersion() const noexcept { return midiMapVersion.load(); }
    juce::String midiMapToString() const;
    void midiMapFromString (const juce::String&);
    void saveMidiMapAsDefault();
    void recallDefaultMidiMap();

    // ---- metering, read from the message thread
    int   getActiveVoices() const noexcept      { return activeVoices.load (std::memory_order_relaxed); }
    float getGainReductionDb() const noexcept   { return core.getGainReductionDb(); }
    float getDuckReductionDb() const noexcept   { return core.getDuckReductionDb(); }
    float getStringEnergy (int note) const noexcept
    {
        return note >= 0 && note < 128 ? stringEnergy[(size_t) note].load (std::memory_order_relaxed) : 0.0f;
    }
    bool isStringOpen (int note) const noexcept
    {
        return note >= 0 && note < 128 && stringOpen[(size_t) note].load (std::memory_order_relaxed);
    }

private:
    friend struct ParamChangeAction;
    friend struct StateChangeAction;

    void pullParameters();
    void handleMidi (const juce::MidiMessage&);
    void applyValues (const std::vector<float>&);   // no undo recording

    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;

    rv::ResonanceVerbCore core;
    rv::CoreParams params;
    std::atomic<int> activeVoices { 0 };
    std::array<std::atomic<float>, 128> stringEnergy {};
    std::array<std::atomic<bool>, 128> stringOpen {};
    juce::AudioBuffer<float> sidechainCopy;   // sidechain channels may alias main outputs

    // undo
    juce::UndoManager undoManager { 0, 200 };   // keep the last 200 transactions
    std::vector<float> gestureStartValues;
    bool recordingSuppressed = false;

    // presets
    std::unique_ptr<rv::PresetManager> presetManager;

    // MIDI learn: CC number -> parameter index (-1 = unmapped). Written by the audio thread
    // when learning, read by both; all atomics, no locks.
    juce::Array<juce::AudioProcessorParameter*> parameterList;
    std::array<std::atomic<int>, 128> ccToParam;
    std::atomic<int> learnTarget { -1 };
    std::atomic<int> midiMapVersion { 0 };

    struct Raw
    {
        std::atomic<float>* hp, *lp, *drive, *excite, *exciteSrc,
            *decay, *release, *hfDamp, *inharm, *detune, *bright, *unison, *velSens, *spread,
            *halo, *doubleDecay, *body, *bodySize, *bleed,
            *bendRange, *a4, *temper, *root, *stretch,
            *mode, *scaleRoot, *scale, *scaleLow, *scaleHigh, *pedal, *freeze,
            *blur, *blurSize, *blurTail, *blurMod,
            *preDelay, *lowCut, *tilt, *duck, *duckRel,
            *thr, *ratio, *atk, *rel, *makeup, *width, *mix, *out;
    } raw {};

    JUCE_DECLARE_WEAK_REFERENCEABLE (ResonanceVerbProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResonanceVerbProcessor)
};
