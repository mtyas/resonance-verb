// SPDX-License-Identifier: AGPL-3.0-or-later
// Renders the Resonance Verb editor to a PNG after feeding it some audio, so the
// keyboard shows live string energy. Usage: rv_ui_snapshot out.png [presetIndex]
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PresetBrowser.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "ui.png"));

    ResonanceVerbProcessor proc;
    proc.setCurrentProgram (argc > 2 ? juce::String (argv[2]).getIntValue() : 1);
    proc.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    juce::Random rng (1);
    for (int block = 0; block < 150; ++block)                // ~1.6 s of noise bursts
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buffer.setSample (ch, i, (block % 40 < 8 ? 0.3f : 0.0f) * (rng.nextFloat() * 2.0f - 1.0f));
        midi.clear();
        proc.processBlock (buffer, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    const juce::String view = argc > 3 ? argv[3] : "";
    if (auto* rv = dynamic_cast<ResonanceVerbEditor*> (editor.get()))
    {
        if (view == "browser") rv->setBrowserVisible (true);
        if (view == "learn")
        {
            proc.midiMapFromString ("21=decay;22=halo;23=blur;24=mix;64=freeze");
            proc.startMidiLearn (proc.apvts.getParameter ("bright")->getParameterIndex());
            rv->setLearnMode (true);
        }
    }
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    juce::FileOutputStream stream (out);
    if (! stream.openedOk()) return 1;
    stream.setPosition (0); stream.truncate();
    juce::PNGImageFormat().writeImageToStream (image, stream);
    std::printf ("wrote %s (%d x %d), %d strings active\n", out.getFullPathName().toRawUTF8(),
                 image.getWidth(), image.getHeight(), proc.getActiveVoices());
    return 0;
}
