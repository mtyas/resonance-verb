// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginProcessor.h"

namespace rvui
{
namespace colours
{
    const juce::Colour background { 0xff15120f };
    const juce::Colour panel      { 0xff201b16 };
    const juce::Colour panelEdge  { 0xff3a3128 };
    const juce::Colour brass      { 0xffc8a15a };
    const juce::Colour ivory      { 0xffefe6d4 };
    const juce::Colour dimText    { 0xff9c8f7a };
    const juce::Colour track      { 0xff3b342c };
    const juce::Colour ice        { 0xff8fd3f0 };
    const juce::Colour glow       { 0xffffb347 };
}

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
};

/** 88-key keyboard: open strings glow with their current energy; clicking plays strings. */
class StringKeyboard final : public juce::MidiKeyboardComponent
{
public:
    StringKeyboard (ResonanceVerbProcessor&);
    void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour lineColour, juce::Colour textColour) override;
    void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour noteFillColour) override;

private:
    float glowFor (int note) const;
    ResonanceVerbProcessor& proc;
};

/** Sits over a control in MIDI Learn mode: click to learn, right-click to clear. */
class LearnOverlay final : public juce::Component
{
public:
    LearnOverlay (ResonanceVerbProcessor&, int parameterIndex, juce::String displayName);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    int getParameterIndex() const noexcept { return index; }

private:
    ResonanceVerbProcessor& proc;
    int index;
    juce::String name;
};

/** A titled group of controls generated from parameter IDs. */
class Panel final : public juce::Component
{
public:
    Panel (juce::String title, ResonanceVerbProcessor&,
           std::vector<std::pair<juce::String, juce::String>> idsAndLabels);
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void setLearnMode (bool);

private:
    struct Control
    {
        std::unique_ptr<juce::Component> widget;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<LearnOverlay> overlay;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAtt;
        int span = 1;
        int parameterIndex = -1;
    };
    ResonanceVerbProcessor& proc;
    juce::String title;
    bool learnMode = false;
    std::vector<std::unique_ptr<Control>> controls;
};

class PresetBrowser;
} // namespace rvui

class ResonanceVerbEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ResonanceVerbEditor (ResonanceVerbProcessor&);
    ~ResonanceVerbEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // also used by the UI snapshot tool
    void setBrowserVisible (bool);
    void setLearnMode (bool);

private:
    void timerCallback() override;
    void drawMeter (juce::Graphics&, juce::Rectangle<int>, const juce::String& name, float reductionDb, juce::Colour);
    void showMidiMenu();
    void quickSave();
    void updateUndoButtons();

    ResonanceVerbProcessor& proc;
    rvui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };
    juce::TextButton prevButton { "<" }, nextButton { ">" }, presetButton, saveButton { "Save" };
    juce::TextButton midiButton { "MIDI" };
    juce::TextButton freezeButton { "FREEZE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAtt;
    std::unique_ptr<rvui::LearnOverlay> freezeOverlay;
    rvui::StringKeyboard keyboard;
    std::vector<std::unique_ptr<rvui::Panel>> panels;
    std::unique_ptr<rvui::PresetBrowser> browser;

    juce::Rectangle<int> meterArea, panelArea;
    bool learnMode = false;
    int lastMapVersion = -1, lastLearnTarget = -2;
    juce::String lastPresetText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResonanceVerbEditor)
};
