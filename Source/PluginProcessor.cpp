// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using APF = juce::AudioParameterFloat;
using APC = juce::AudioParameterChoice;
using API = juce::AudioParameterInt;
using NR  = juce::NormalisableRange<float>;

NR skewed (float lo, float hi, float centre)
{
    NR r (lo, hi);
    r.setSkewForCentre (centre);
    return r;
}

/** Float parameter whose text includes its unit with sensible precision; "%" shows 0..1 as percent. */
std::unique_ptr<APF> fp (const char* id, const char* name, NR range, float def, const char* unitIn = "")
{
    const juce::String unit (unitIn);
    auto toText = [unit] (float v, int maxLen)
    {
        juce::String t;
        if (unit == "%")
            t = juce::String (juce::roundToInt (v * 100.0f)) + " %";
        else
        {
            const float a = std::abs (v);
            const int decimals = unit == "st" ? 0
                               : unit == "Hz" ? (a >= 1000.0f ? 0 : 1)
                               : (a >= 100.0f ? 0 : (a >= 10.0f ? 1 : 2));
            t = juce::String (v, decimals) + (unit.isEmpty() ? juce::String() : " " + unit);
        }
        return maxLen > 0 ? t.substring (0, maxLen) : t;
    };
    auto fromText = [unit] (const juce::String& t)
    {
        const float v = t.retainCharacters ("-0123456789.").getFloatValue();
        return unit == "%" ? v / 100.0f : v;
    };
    return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, range, def,
                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction (toText)
                                                                       .withValueFromStringFunction (fromText));
}
std::unique_ptr<API> noteParam (const char* id, const char* name, int def)
{
    auto toText = [] (int v, int) { return juce::MidiMessage::getMidiNoteName (v, true, true, 4); };
    auto fromText = [] (const juce::String& t)
    {
        for (int n = 21; n <= 108; ++n)
            if (juce::MidiMessage::getMidiNoteName (n, true, true, 4).equalsIgnoreCase (t.trim()))
                return n;
        return t.getIntValue();
    };
    return std::make_unique<API> (juce::ParameterID { id, 1 }, name, 21, 108, def,
                                  juce::AudioParameterIntAttributes().withStringFromValueFunction (toText)
                                                                     .withValueFromStringFunction (fromText));
}

juce::StringArray noteNames() { return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; }

juce::StringArray scaleNames()
{
    juce::StringArray a;
    for (const auto& s : rv::scaleTable()) a.add (s.name);
    return a;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout ResonanceVerbProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    auto input = std::make_unique<juce::AudioProcessorParameterGroup> ("input", "Input", "|");
    input->addChild (fp ("inHP",   "Input High-Pass", skewed (20.0f, 1000.0f, 120.0f), 40.0f, "Hz"),
                     fp ("inLP",   "Input Low-Pass",  skewed (1000.0f, 20000.0f, 6000.0f), 14000.0f, "Hz"),
                     fp ("drive",  "Drive",           NR (0.0f, 24.0f), 0.0f, "dB"),
                     fp ("excite", "Excitation",      NR (-24.0f, 24.0f), 0.0f, "dB"),
                     std::make_unique<APC> (juce::ParameterID { "exciteSrc", 1 }, "Excitation Source",
                                            juce::StringArray { "Main Input", "Sidechain", "Main + Sidechain" }, 0));

    auto perf = std::make_unique<juce::AudioProcessorParameterGroup> ("perform", "Performance", "|");
    perf->addChild (std::make_unique<APC> (juce::ParameterID { "mode", 1 }, "Strings Mode",
                                           juce::StringArray { "MIDI", "MIDI Latch", "Scale" }, 0),
                    std::make_unique<APC> (juce::ParameterID { "scaleRoot", 1 }, "Scale Root", noteNames(), 0),
                    std::make_unique<APC> (juce::ParameterID { "scale", 1 }, "Scale", scaleNames(), 1),
                    noteParam ("scaleLow",  "Scale Low",  36),
                    noteParam ("scaleHigh", "Scale High", 84),
                    fp ("pedal", "Pedal", NR (0.0f, 1.0f), 0.0f, "%"),
                    // A two-state choice rather than AudioParameterBool: bools keep raw fractional values
                    // that APVTS state restore can miss (caught by pluginval's state-restoration test).
                    std::make_unique<APC> (juce::ParameterID { "freeze", 1 }, "Freeze", juce::StringArray { "Off", "On" }, 0));

    auto res = std::make_unique<juce::AudioProcessorParameterGroup> ("strings", "Strings", "|");
    res->addChild (fp ("decay",   "Decay",           skewed (0.5f, 30.0f, 6.0f), 8.0f, "s"),
                   fp ("release", "Damper Release",  skewed (0.03f, 2.0f, 0.3f), 0.2f, "s"),
                   fp ("hfDamp",  "HF Damping",      NR (0.0f, 1.0f), 0.35f, "%"),
                   fp ("inharm",  "Inharmonicity",   NR (0.0f, 3.0f), 1.0f, "x"),
                   fp ("detune",  "Unison Detune",   NR (0.0f, 10.0f), 1.2f, "ct"),
                   fp ("bright",  "Brightness",      NR (0.0f, 1.0f), 0.5f, "%"),
                   std::make_unique<APC> (juce::ParameterID { "unison", 1 }, "Unison Strings",
                                          juce::StringArray { "1", "2", "3" }, 2),
                   fp ("velSens", "Velocity Sens",   NR (0.0f, 1.0f), 0.5f, "%"),
                   fp ("spread",  "Keyboard Spread", NR (0.0f, 1.0f), 0.7f, "%"));

    auto character = std::make_unique<juce::AudioProcessorParameterGroup> ("character", "Character", "|");
    character->addChild (fp ("halo",        "Halo",         NR (0.0f, 1.0f), 0.3f, "%"),
                         fp ("doubleDecay", "Double Decay", NR (0.0f, 1.0f), 0.5f, "%"),
                         fp ("body",        "Body",         NR (0.0f, 1.0f), 0.3f, "%"),
                         fp ("bodySize",    "Body Size",    NR (0.0f, 1.0f), 0.5f, "%"),
                         fp ("bleed",       "Bleed",        NR (0.0f, 1.0f), 0.0f, "%"));

    auto tune = std::make_unique<juce::AudioProcessorParameterGroup> ("tuning", "Tuning", "|");
    tune->addChild (fp ("bendRange", "Bend Range", NR (0.0f, 24.0f, 1.0f), 2.0f, "st"),
                    fp ("a4",        "A4 Reference", NR (415.0f, 466.0f, 0.01f), 440.0f, "Hz"),
                    std::make_unique<APC> (juce::ParameterID { "temper", 1 }, "Temperament",
                                           juce::StringArray { "Equal", "Just (Major)", "Pythagorean",
                                                               "1/4-Comma Meantone", "Werckmeister III" }, 0),
                    std::make_unique<APC> (juce::ParameterID { "root", 1 }, "Temperament Root", noteNames(), 0),
                    fp ("stretch", "Octave Stretch", NR (0.0f, 1.0f), 0.5f, "%"));

    auto blur = std::make_unique<juce::AudioProcessorParameterGroup> ("blur", "Blur", "|");
    blur->addChild (fp ("blur",     "Blur Amount", NR (0.0f, 1.0f), 0.35f, "%"),
                    fp ("blurSize", "Blur Size",   NR (0.0f, 1.0f), 0.5f, "%"),
                    fp ("blurTail", "Blur Tail",   skewed (0.0f, 10.0f, 2.0f), 1.5f, "s"),
                    fp ("blurMod",  "Blur Motion", NR (0.0f, 1.0f), 0.3f, "%"));

    auto space = std::make_unique<juce::AudioProcessorParameterGroup> ("space", "Space & Tone", "|");
    space->addChild (fp ("preDelay", "Pre-Delay",    NR (0.0f, 250.0f), 0.0f, "ms"),
                     fp ("lowCut",   "Wet Low Cut",  skewed (20.0f, 1000.0f, 150.0f), 20.0f, "Hz"),
                     fp ("tilt",     "Wet Tilt",     NR (-6.0f, 6.0f), 0.0f, "dB"),
                     fp ("duck",     "Duck",         NR (0.0f, 1.0f), 0.0f, "%"),
                     fp ("duckRel",  "Duck Release", skewed (20.0f, 1000.0f, 250.0f), 250.0f, "ms"));

    auto out = std::make_unique<juce::AudioProcessorParameterGroup> ("output", "Output", "|");
    out->addChild (fp ("compThr",   "Comp Threshold", NR (-60.0f, 0.0f), -24.0f, "dB"),
                   fp ("compRatio", "Comp Ratio",     skewed (1.0f, 20.0f, 4.0f), 3.0f, ":1"),
                   fp ("compAtk",   "Comp Attack",    skewed (0.1f, 100.0f, 10.0f), 10.0f, "ms"),
                   fp ("compRel",   "Comp Release",   skewed (10.0f, 1000.0f, 150.0f), 150.0f, "ms"),
                   fp ("makeup",    "Makeup Gain",    NR (0.0f, 36.0f), 12.0f, "dB"),
                   fp ("width",     "Stereo Width",   NR (0.0f, 2.0f), 1.0f, "%"),
                   fp ("mix",       "Dry/Wet",        NR (0.0f, 1.0f), 0.5f, "%"),
                   fp ("out",       "Output Gain",    NR (-24.0f, 12.0f), 0.0f, "dB"));

    l.add (std::move (input), std::move (perf), std::move (res), std::move (character), std::move (tune),
           std::move (blur), std::move (space), std::move (out));
    return l;
}

ResonanceVerbProcessor::ResonanceVerbProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "ResonanceVerb", createLayout())
{
    auto p = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    raw = { p ("inHP"), p ("inLP"), p ("drive"), p ("excite"), p ("exciteSrc"),
            p ("decay"), p ("release"), p ("hfDamp"), p ("inharm"), p ("detune"), p ("bright"),
            p ("unison"), p ("velSens"), p ("spread"),
            p ("halo"), p ("doubleDecay"), p ("body"), p ("bodySize"), p ("bleed"),
            p ("bendRange"), p ("a4"), p ("temper"), p ("root"), p ("stretch"),
            p ("mode"), p ("scaleRoot"), p ("scale"), p ("scaleLow"), p ("scaleHigh"), p ("pedal"), p ("freeze"),
            p ("blur"), p ("blurSize"), p ("blurTail"), p ("blurMod"),
            p ("preDelay"), p ("lowCut"), p ("tilt"), p ("duck"), p ("duckRel"),
            p ("compThr"), p ("compRatio"), p ("compAtk"), p ("compRel"), p ("makeup"),
            p ("width"), p ("mix"), p ("out") };

    parameterList = getParameters();
    gestureStartValues.resize ((size_t) parameterList.size());
    for (auto* param : parameterList)
        param->addListener (this);
    for (auto& c : ccToParam) c.store (-1);

    presetManager = std::make_unique<rv::PresetManager> (*this, [this] (const juce::String& name, const std::vector<float>& v)
    {
        applyValuesWithUndo (name, v);
    });
    recallDefaultMidiMap();
    savedPresetKey = presetManager->getCurrent().getKey();
}

ResonanceVerbProcessor::~ResonanceVerbProcessor()
{
    cancelPendingUpdate();
    for (auto* param : parameterList)
        param->removeListener (this);
}

// ---------------------------------------------------------------- undo / redo
struct ParamChangeAction final : juce::UndoableAction
{
    ParamChangeAction (ResonanceVerbProcessor& p, int i, float a, float b) : proc (p), index (i), from (a), to (b) {}
    bool perform() override { set (to); return true; }
    bool undo() override    { set (from); return true; }
    int getSizeInUnits() override { return 1; }
    void set (float v)
    {
        const juce::ScopedValueSetter<bool> svs (proc.recordingSuppressed, true);
        auto* param = proc.parameterList[index];
        if (std::abs (param->getValue() - v) < 1.0e-7f) return;
        param->beginChangeGesture();
        param->setValueNotifyingHost (v);
        param->endChangeGesture();
    }
    ResonanceVerbProcessor& proc;
    int index;
    float from, to;
};

struct StateChangeAction final : juce::UndoableAction
{
    StateChangeAction (ResonanceVerbProcessor& p, std::vector<float> a, std::vector<float> b)
        : proc (p), from (std::move (a)), to (std::move (b)) {}
    bool perform() override { proc.applyValues (to); return true; }
    bool undo() override    { proc.applyValues (from); return true; }
    int getSizeInUnits() override { return (int) to.size(); }
    ResonanceVerbProcessor& proc;
    std::vector<float> from, to;
};

std::vector<float> ResonanceVerbProcessor::captureValues() const
{
    std::vector<float> v ((size_t) parameterList.size());
    for (int i = 0; i < parameterList.size(); ++i) v[(size_t) i] = parameterList[i]->getValue();
    return v;
}

void ResonanceVerbProcessor::applyValues (const std::vector<float>& values)
{
    const juce::ScopedValueSetter<bool> svs (recordingSuppressed, true);
    for (int i = 0; i < parameterList.size() && i < (int) values.size(); ++i)
    {
        auto* param = parameterList[i];
        if (std::abs (param->getValue() - values[(size_t) i]) < 1.0e-7f) continue;
        param->beginChangeGesture();
        param->setValueNotifyingHost (values[(size_t) i]);
        param->endChangeGesture();
    }
}

void ResonanceVerbProcessor::applyValuesWithUndo (const juce::String& actionName, const std::vector<float>& values)
{
    undoManager.beginNewTransaction (actionName);
    undoManager.perform (new StateChangeAction (*this, captureValues(), values));
}

void ResonanceVerbProcessor::parameterGestureChanged (int index, bool starting)
{
    if (recordingSuppressed || ! juce::isPositiveAndBelow (index, parameterList.size())) return;

    auto record = [this, index, starting]
    {
        auto* param = parameterList[index];
        if (starting)
        {
            gestureStartValues[(size_t) index] = param->getValue();
            return;
        }
        const float from = gestureStartValues[(size_t) index], to = param->getValue();
        if (std::abs (from - to) < 1.0e-7f) return;
        undoManager.beginNewTransaction (param->getName (64));
        undoManager.perform (new ParamChangeAction (*this, index, from, to));
    };

    // Only gestures on the message thread come from an editor (ours or the host's generic UI);
    // those reported from other threads are host automation / control surfaces and aren't undo steps.
    if (juce::MessageManager::existsAndIsCurrentThread())
        record();
}

void ResonanceVerbProcessor::handleAsyncUpdate()
{
    bool restore;
    juce::String key;
    int program;
    {
        const juce::SpinLock::ScopedLockType lock (pendingLock);
        restore = std::exchange (pendingRestore, false);
        key = std::exchange (pendingPresetKey, {});
        program = std::exchange (pendingProgram, -1);
    }
    if (restore)
    {
        undoManager.clearUndoHistory();              // a restored session starts a fresh history
        if (key.isNotEmpty()) presetManager->restoreCurrent (key);
    }
    if (program >= 0)
        presetManager->loadFactory (program);
}

// ---------------------------------------------------------------- MIDI learn
void ResonanceVerbProcessor::startMidiLearn (int paramIndex) noexcept
{
    learnTarget.store (juce::isPositiveAndBelow (paramIndex, parameterList.size()) ? paramIndex : -1);
    ++midiMapVersion;
}

int ResonanceVerbProcessor::getCcForParameter (int paramIndex) const noexcept
{
    for (int cc = 0; cc < 128; ++cc)
        if (ccToParam[(size_t) cc].load() == paramIndex) return cc;
    return -1;
}

void ResonanceVerbProcessor::clearMidiMapping (int paramIndex) noexcept
{
    for (auto& c : ccToParam)
        if (c.load() == paramIndex) c.store (-1);
    ++midiMapVersion;
}

void ResonanceVerbProcessor::clearAllMidiMappings() noexcept
{
    for (auto& c : ccToParam) c.store (-1);
    learnTarget.store (-1);
    ++midiMapVersion;
}

juce::String ResonanceVerbProcessor::midiMapToString() const
{
    juce::StringArray items;
    for (int cc = 0; cc < 128; ++cc)
        if (const int idx = ccToParam[(size_t) cc].load(); idx >= 0)
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (parameterList[idx]))
                items.add (juce::String (cc) + "=" + r->getParameterID());
    return items.joinIntoString (";");
}

void ResonanceVerbProcessor::midiMapFromString (const juce::String& text)
{
    for (auto& c : ccToParam) c.store (-1);
    for (const auto& item : juce::StringArray::fromTokens (text, ";", ""))
    {
        const int cc = item.upToFirstOccurrenceOf ("=", false, false).getIntValue();
        const auto id = item.fromFirstOccurrenceOf ("=", false, false);
        if (! juce::isPositiveAndBelow (cc, 120)) continue;
        for (int i = 0; i < parameterList.size(); ++i)
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (parameterList[i]); r != nullptr && r->getParameterID() == id)
                ccToParam[(size_t) cc].store (i);
    }
    ++midiMapVersion;
}

void ResonanceVerbProcessor::saveMidiMapAsDefault()
{
    presetManager->getSettings().setValue ("defaultMidiMap", midiMapToString());
    presetManager->getSettings().saveIfNeeded();
}

void ResonanceVerbProcessor::recallDefaultMidiMap()
{
    midiMapFromString (presetManager->getSettings().getValue ("defaultMidiMap"));
}

bool ResonanceVerbProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out == juce::AudioChannelSet::stereo())
        return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
    return out == juce::AudioChannelSet::mono() && in == juce::AudioChannelSet::mono();
}

void ResonanceVerbProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sidechainCopy.setSize (2, juce::jmax (1, samplesPerBlock));
    core.prepare (sampleRate);
    pullParameters();
    core.reset();
}

double ResonanceVerbProcessor::getTailLengthSeconds() const
{
    // Held strings ring "forever" while keys are down; this covers the decay after release/automation.
    return (double) raw.decay->load() * 1.5 + (double) raw.blurTail->load();
}

void ResonanceVerbProcessor::pullParameters()
{
    auto& p = params;
    p.hpHz     = raw.hp->load();
    p.lpHz     = raw.lp->load();
    p.driveDb  = raw.drive->load();
    p.exciteDb = raw.excite->load();
    p.exciteSource = (int) raw.exciteSrc->load();

    p.mode      = (rv::StringsMode) (int) raw.mode->load();
    p.scaleRoot = (int) raw.scaleRoot->load();
    p.scaleType = (int) raw.scale->load();
    p.scaleLow  = (int) raw.scaleLow->load();
    p.scaleHigh = (int) raw.scaleHigh->load();
    p.pedal     = raw.pedal->load();
    p.freeze    = raw.freeze->load() > 0.5f;

    p.res.decaySeconds   = raw.decay->load();
    p.res.releaseSeconds = raw.release->load();
    p.res.hfDamping      = raw.hfDamp->load();
    p.res.inharmonicity  = raw.inharm->load();
    p.res.unisonDetune   = raw.detune->load();
    p.res.brightness     = raw.bright->load();
    p.res.maxUnison      = (int) raw.unison->load() + 1;
    p.res.velocitySens   = raw.velSens->load();
    p.res.spread         = raw.spread->load();
    p.res.halo           = raw.halo->load();
    p.res.doubleDecay    = raw.doubleDecay->load();
    p.body               = raw.body->load();
    p.bodySize           = raw.bodySize->load();
    p.bleed              = raw.bleed->load();

    p.bendRangeSemis      = raw.bendRange->load();
    p.tuning.a4Hz         = raw.a4->load();
    p.tuning.temperament  = (rv::Temperament) (int) raw.temper->load();
    p.tuning.root         = (int) raw.root->load();
    p.tuning.stretch      = raw.stretch->load();

    p.blurAmount      = raw.blur->load();
    p.blurSize        = raw.blurSize->load();
    p.blurTailSeconds = raw.blurTail->load();
    p.blurMod         = raw.blurMod->load();

    p.preDelayMs    = raw.preDelay->load();
    p.lowCutHz      = raw.lowCut->load();
    p.tiltDb        = raw.tilt->load();
    p.duck          = raw.duck->load();
    p.duckReleaseMs = raw.duckRel->load();

    p.compThreshDb  = raw.thr->load();
    p.compRatio     = raw.ratio->load();
    p.compAttackMs  = raw.atk->load();
    p.compReleaseMs = raw.rel->load();
    p.makeupDb      = raw.makeup->load();
    p.width         = raw.width->load();
    p.mix           = raw.mix->load();
    p.outDb         = raw.out->load();

    core.setParams (p); // cheap when nothing changed; per-voice rebuild only on change
}

void ResonanceVerbProcessor::handleMidi (const juce::MidiMessage& m)
{
    // Learnable CCs (0-119; 120-127 are channel-mode messages). A mapped CC drives its
    // parameter instead of any built-in meaning (e.g. CC64 pedal).
    if (m.isController() && m.getControllerNumber() < 120)
    {
        const int cc = m.getControllerNumber();
        int target = learnTarget.load();
        if (target >= 0 && learnTarget.compare_exchange_strong (target, -1))
        {
            for (auto& c : ccToParam)
                if (c.load() == target) c.store (-1);
            ccToParam[(size_t) cc].store (target);
            ++midiMapVersion;
        }
        if (const int idx = ccToParam[(size_t) cc].load(); idx >= 0)
        {
            parameterList[idx]->setValueNotifyingHost ((float) m.getControllerValue() / 127.0f);
            return;
        }
    }

    if (m.isNoteOn())
        core.noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        core.noteOff (m.getNoteNumber());
    else if (m.isPitchWheel())
        core.pitchBend ((float) (m.getPitchWheelValue() - 8192) / 8192.0f);
    else if (m.isControllerOfType (64))                       // continuous: half-pedal aware
        core.setPedalMidi ((float) m.getControllerValue() / 127.0f);
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        core.allNotesOff();
}

void ResonanceVerbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numIn  = getMainBusNumInputChannels();
    const int numOut = getMainBusNumOutputChannels();

    // Sidechain first: its channels can share buffer channels with the main outputs.
    const float* scL = nullptr;
    const float* scR = nullptr;
    if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        const int scCh = juce::jmin (2, sc.getNumChannels());
        if (scCh > 0)
        {
            if (sidechainCopy.getNumSamples() < n)
                sidechainCopy.setSize (2, n, false, false, true);   // only if the host exceeds its promised block size
            for (int ch = 0; ch < scCh; ++ch)
                sidechainCopy.copyFrom (ch, 0, sc, ch, 0, n);
            scL = sidechainCopy.getReadPointer (0);
            scR = scCh > 1 ? sidechainCopy.getReadPointer (1) : nullptr;
        }
    }

    if (numIn == 1 && numOut >= 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);   // mono in -> keep dry on both sides
    else
        for (int ch = numIn; ch < numOut; ++ch)
            buffer.clear (ch, 0, n);

    pullParameters();
    keyboardState.processNextMidiBuffer (midi, 0, n, true);   // merge on-screen keyboard

    float* L = buffer.getWritePointer (0);
    float* R = numOut > 1 ? buffer.getWritePointer (1) : nullptr;

    // Sample-accurate MIDI: render up to each event, then apply it.
    int pos = 0;
    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, n, meta.samplePosition);
        if (t > pos)
        {
            core.process (L + pos, R != nullptr ? R + pos : nullptr, t - pos,
                          scL != nullptr ? scL + pos : nullptr, scR != nullptr ? scR + pos : nullptr);
            pos = t;
        }
        handleMidi (meta.getMessage());
    }
    if (pos < n)
        core.process (L + pos, R != nullptr ? R + pos : nullptr, n - pos,
                      scL != nullptr ? scL + pos : nullptr, scR != nullptr ? scR + pos : nullptr);

    activeVoices.store (core.getActiveVoices(), std::memory_order_relaxed);
    for (int note = 0; note < 128; ++note)
    {
        stringEnergy[(size_t) note].store (core.getStringEnergy (note), std::memory_order_relaxed);
        stringOpen[(size_t) note].store (core.isStringOpen (note), std::memory_order_relaxed);
    }
}

juce::AudioProcessorEditor* ResonanceVerbProcessor::createEditor()
{
    return new ResonanceVerbEditor (*this);
}

void ResonanceVerbProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement root ("ResonanceVerbState");
    root.setAttribute ("version", 2);

    // Some wrappers (e.g. CLAP during processing) set parameter values without notifying
    // listeners, which leaves the APVTS tree stale. Write the live value wherever the tree
    // disagrees with it; where they agree, keep the tree's exact value so re-saving a loaded
    // state is byte-identical.
    auto state = apvts.copyState();
    for (auto* p : parameterList)
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            auto child = state.getChildWithProperty ("id", r->getParameterID());
            if (! child.isValid()) continue;
            const float stored = (float) child.getProperty ("value", r->convertFrom0to1 (r->getDefaultValue()));
            if (std::abs (r->convertTo0to1 (stored) - r->getValue()) > 1.0e-5f)
                child.setProperty ("value", r->convertFrom0to1 (r->getValue()), nullptr);
        }
    if (auto params = state.createXml())
        root.addChildElement (params.release());

    auto* midiMap = root.createNewChildElement ("MIDIMAP");
    midiMap->setAttribute ("map", midiMapToString());

    // Always written (deterministic output whichever thread the host saves from).
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        const juce::SpinLock::ScopedLockType lock (pendingLock);
        savedPresetKey = presetManager->getCurrent().getKey();
    }
    auto* preset = root.createNewChildElement ("PRESET");
    {
        const juce::SpinLock::ScopedLockType lock (pendingLock);
        preset->setAttribute ("key", savedPresetKey);
    }
    copyXmlToBinary (root, dest);
}

void ResonanceVerbProcessor::setStateInformation (const void* data, int size)
{
    const auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;

    const juce::XmlElement* paramsXml = xml.get();          // version 1: the APVTS tree itself
    if (xml->hasTagName ("ResonanceVerbState"))
        paramsXml = xml->getChildByName (apvts.state.getType());
    if (paramsXml != nullptr && paramsXml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*paramsXml));

    if (auto* m = xml->getChildByName ("MIDIMAP"))
        midiMapFromString (m->getStringAttribute ("map"));

    const auto presetKey = xml->getChildByName ("PRESET") != nullptr
                         ? xml->getChildByName ("PRESET")->getStringAttribute ("key") : juce::String();
    {
        const juce::SpinLock::ScopedLockType lock (pendingLock);
        pendingRestore = true;
        pendingPresetKey = presetKey;
        if (presetKey.isNotEmpty()) savedPresetKey = presetKey;
    }
    if (juce::MessageManager::existsAndIsCurrentThread()) handleAsyncUpdate();
    else triggerAsyncUpdate();
}

// ---------------------------------------------------------------- factory presets as host programs
int ResonanceVerbProcessor::getNumPrograms() { return (int) rv::factoryPresets().size(); }

int ResonanceVerbProcessor::getCurrentProgram()
{
    const auto& c = presetManager->getCurrent();
    return c.isFactory ? c.factoryIndex : 0;
}

const juce::String ResonanceVerbProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumPrograms()) ? juce::String (rv::factoryPresets()[(size_t) index].name) : juce::String();
}

void ResonanceVerbProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    if (juce::MessageManager::existsAndIsCurrentThread()) { presetManager->loadFactory (index); return; }
    {
        const juce::SpinLock::ScopedLockType lock (pendingLock);
        pendingProgram = index;
    }
    triggerAsyncUpdate();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ResonanceVerbProcessor();
}
