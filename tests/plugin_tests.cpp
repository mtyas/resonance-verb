// SPDX-License-Identifier: AGPL-3.0-or-later
// Resonance Verb — headless tests for the plugin layer: undo/redo, presets, MIDI learn,
// session state. Uses a temporary preset folder and settings file; never touches the
// user's real presets, settings or Trash.
#include "PluginProcessor.h"

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what, const juce::String& detail = {})
{
    std::printf ("  [%s] %-58s %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8(), detail.toRawUTF8());
    if (! ok) ++failures;
}

juce::RangedAudioParameter* param (ResonanceVerbProcessor& p, const juce::String& id)
{
    return p.apvts.getParameter (id);
}

float real (ResonanceVerbProcessor& p, const juce::String& id)
{
    auto* r = param (p, id);
    return r->convertFrom0to1 (r->getValue());
}

void gestureSet (ResonanceVerbProcessor& p, const juce::String& id, float realValue)
{
    auto* r = param (p, id);
    r->beginChangeGesture();
    r->setValueNotifyingHost (r->convertTo0to1 (realValue));
    r->endChangeGesture();
}

void sendCC (ResonanceVerbProcessor& p, int cc, int value)
{
    juce::AudioBuffer<float> buffer (2, 256);
    buffer.clear();
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::controllerEvent (1, cc, value), 10);
    p.processBlock (buffer, midi);
}

void isolate (ResonanceVerbProcessor& p, const juce::File& dir)
{
    p.getPresetManager().setSettingsFile (dir.getChildFile ("test.settings"));
    p.getPresetManager().setUserDirectory (dir.getChildFile ("Presets"));
    p.clearAllMidiMappings();
    p.getUndoManager().clearUndoHistory();
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("rv-plugin-tests-" + juce::Uuid().toString());
    dir.createDirectory();

    auto proc = std::make_unique<ResonanceVerbProcessor>();
    auto& p = *proc;
    isolate (p, dir);
    p.prepareToPlay (48000.0, 512);
    auto& pm = p.getPresetManager();
    auto& um = p.getUndoManager();

    std::puts ("Factory presets");
    {
        juce::StringArray bad;
        for (const auto& preset : rv::factoryPresets())
            for (const auto& [id, value] : preset.values)
                if (param (p, id) == nullptr) bad.add (juce::String (preset.name) + ":" + id);
        check (bad.isEmpty(), "Every factory preset parameter ID exists", bad.isEmpty() ? juce::String ((int) rv::factoryPresets().size()) + " presets" : bad.joinIntoString (", "));
        p.setCurrentProgram (4);
        check (p.getCurrentProgram() == 4 && pm.getCurrent().name == rv::factoryPresets()[4].name,
               "Host program change loads factory preset", pm.getCurrent().name);
        check (! pm.isModified(), "Freshly loaded preset is not modified");
    }

    std::puts ("Undo / redo");
    {
        pm.loadFactory (0);
        um.clearUndoHistory();
        gestureSet (p, "decay", 12.0f);
        check (um.canUndo() && um.getUndoDescription() == "Decay", "Control gesture is one undo step", um.getUndoDescription());
        um.undo();
        check (std::abs (real (p, "decay") - 8.0f) < 0.01f, "Undo restores the previous value", juce::String (real (p, "decay"), 2) + " s");
        um.redo();
        check (std::abs (real (p, "decay") - 12.0f) < 0.01f, "Redo re-applies it", juce::String (real (p, "decay"), 2) + " s");

        um.clearUndoHistory();
        param (p, "mix")->setValueNotifyingHost (0.9f);            // automation-style: no gesture
        sendCC (p, 1, 64);                                         // unmapped CC
        check (! um.canUndo(), "Automation / MIDI changes are not recorded");

        um.clearUndoHistory();
        pm.loadFactory (2);                                        // Open Grand: Scale mode
        check ((int) real (p, "mode") == 2 && um.getUndoDescription().startsWith ("Load preset"),
               "Preset load is a single undo step", um.getUndoDescription());
        um.undo();
        check ((int) real (p, "mode") == 0 && std::abs (real (p, "mix") - 0.9f) < 0.01f,
               "Undoing a preset load restores every parameter");
    }

    std::puts ("Preset library");
    {
        pm.loadFactory (0);
        check (! pm.isModified(), "Init preset not modified");
        gestureSet (p, "freeze", 1.0f);
        check (! pm.isModified(), "Freeze / pedal don't mark a preset modified");
        gestureSet (p, "freeze", 0.0f);
        gestureSet (p, "decay", 12.0f);
        gestureSet (p, "scale", 7.0f);
        check (pm.isModified(), "Editing a control marks it modified");

        auto r = pm.save ("My Test Preset", "Tests", "Tester", "A test preset.", false);
        const auto file = pm.getUserDirectory().getChildFile ("My Test Preset.rvpreset");
        check (r.wasOk() && file.existsAsFile(), "Save As writes a .rvpreset file", file.getFileName());
        check (pm.getCurrent().name == "My Test Preset" && ! pm.isModified(), "Saved preset becomes current, unmodified");
        check (pm.getCategories().contains ("Tests"), "Custom category appears in the library");
        check (pm.save ("My Test Preset", "Tests", "", "", false).getErrorMessage() == "exists",
               "Saving over an existing name needs confirmation");

        pm.loadFactory (0);
        const rv::PresetInfo* mine = nullptr;
        for (const auto& info : pm.getAll()) if (info.name == "My Test Preset") mine = &info;
        check (mine != nullptr && pm.load (*mine) && std::abs (real (p, "decay") - 12.0f) < 0.01f && (int) real (p, "scale") == 7,
               "User preset round-trips its values", juce::String (real (p, "decay"), 2) + " s, scale " + juce::String ((int) real (p, "scale")));

        pm.setFavourite (pm.getCurrent(), true);
        check (pm.isFavourite (pm.getCurrent()), "Favourites persist", pm.getSettings().getFile().getFileName());

        r = pm.rename (pm.getCurrent(), "Renamed Preset");
        check (r.wasOk() && ! file.existsAsFile() && pm.getUserDirectory().getChildFile ("Renamed Preset.rvpreset").existsAsFile()
                   && pm.getCurrent().name == "Renamed Preset",
               "Rename moves the file and keeps it current");

        const auto external = dir.getChildFile ("Shared.rvpreset");
        juce::XmlElement xml ("ResonanceVerbPreset");
        xml.setAttribute ("name", "Shared From Friend");
        xml.setAttribute ("category", "Imported");
        xml.createNewChildElement ("PARAM")->setAttribute ("id", "decay");
        xml.getChildByName ("PARAM")->setAttribute ("value", 3.5);
        xml.writeTo (external);
        r = pm.import (external);
        bool found = false;
        for (const auto& info : pm.getAll()) found |= info.name == "Shared From Friend" && info.category == "Imported";
        check (r.wasOk() && found, "Import copies a preset into the library");
        check (! pm.import (dir.getChildFile ("test.settings")).wasOk(), "Import rejects non-preset files");
    }

    std::puts ("MIDI learn");
    {
        const int mixIndex = param (p, "mix")->getParameterIndex();
        const int decayIndex = param (p, "decay")->getParameterIndex();
        p.startMidiLearn (mixIndex);
        sendCC (p, 21, 64);
        check (p.getCcForParameter (mixIndex) == 21 && p.getMidiLearnTarget() == -1, "Learn maps the next CC to the control", "CC 21");
        check (std::abs (param (p, "mix")->getValue() - 64.0f / 127.0f) < 1e-4f, "Mapped CC drives the parameter");
        sendCC (p, 21, 127);
        check (param (p, "mix")->getValue() > 0.999f, "Full CC range reaches the maximum");

        p.startMidiLearn (decayIndex);
        sendCC (p, 21, 0);
        check (p.getCcForParameter (decayIndex) == 21 && p.getCcForParameter (mixIndex) == -1, "Re-learning a CC moves it to the new control");

        p.startMidiLearn (param (p, "freeze")->getParameterIndex());
        sendCC (p, 64, 127);
        check (real (p, "freeze") > 0.5f, "Learned CC64 drives Freeze instead of the pedal");
        p.clearMidiMapping (param (p, "freeze")->getParameterIndex());
        check (p.getCcForParameter (param (p, "freeze")->getParameterIndex()) == -1, "Clear mapping");
        gestureSet (p, "freeze", 0.0f);

        sendCC (p, 123, 0);   // all notes off: never learnable
        check (p.getCcForParameter (decayIndex) == 21, "Channel-mode CCs (120-127) are not learnable");
    }

    std::puts ("Session state");
    {
        juce::MemoryBlock state;
        p.getStateInformation (state);

        ResonanceVerbProcessor restored;
        isolate (restored, dir);
        restored.setStateInformation (state.getData(), (int) state.getSize());
        bool same = true;
        for (int i = 0; i < p.getParameters().size(); ++i)
            same &= std::abs (p.getParameters()[i]->getValue() - restored.getParameters()[i]->getValue()) < 1e-6f;
        check (same, "All parameter values restored");
        check (restored.getCcForParameter (param (restored, "decay")->getParameterIndex()) == 21, "MIDI mappings restored");
        check (restored.getPresetManager().getCurrent().name == "Renamed Preset", "Current preset restored",
               restored.getPresetManager().getCurrent().name);
        check (! restored.getUndoManager().canUndo(), "Restored session starts with a clean undo history");

        // CLAP hosts set values during processing without notifying parameter listeners;
        // the saved state must still contain them, and re-saving must be byte-identical.
        {
            ResonanceVerbProcessor a;
            isolate (a, dir);
            juce::Random rng (7);
            std::vector<float> set;
            for (auto* prm : a.getParameters()) { const float v = rng.nextFloat(); prm->setValue (v); set.push_back (prm->getValue()); }
            juce::MemoryBlock sa;
            a.getStateInformation (sa);
            ResonanceVerbProcessor b;
            isolate (b, dir);
            b.setStateInformation (sa.getData(), (int) sa.getSize());
            int wrong = 0;
            for (int i = 0; i < b.getParameters().size(); ++i)
                wrong += std::abs (b.getParameters()[i]->getValue() - set[(size_t) i]) > 1e-3f ? 1 : 0;
            juce::MemoryBlock sb;
            b.getStateInformation (sb);
            check (wrong == 0, "Values set without notification are saved (CLAP)", juce::String (wrong) + " parameters wrong after reload");
            check (sa == sb, "Re-saving a loaded state is byte-identical", juce::String ((int) sa.getSize()) + " vs " + juce::String ((int) sb.getSize()) + " bytes");
        }

        // version-1 sessions (bare parameter tree) still load
        ResonanceVerbProcessor legacy;
        isolate (legacy, dir);
        gestureSet (p, "decay", 5.0f);
        juce::MemoryBlock v1;
        if (auto xml = p.apvts.copyState().createXml()) juce::AudioProcessor::copyXmlToBinary (*xml, v1);
        legacy.setStateInformation (v1.getData(), (int) v1.getSize());
        check (std::abs (real (legacy, "decay") - 5.0f) < 0.01f, "Version-1 session state still loads");
    }

    proc.reset();
    dir.deleteRecursively();   // our own temp folder
    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PLUGIN CHECKS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
