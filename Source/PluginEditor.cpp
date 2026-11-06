// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"
#include "PresetBrowser.h"

namespace rvui
{
// ---------------------------------------------------------------- look and feel
LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::background);
    setColour (juce::Label::textColourId, colours::ivory);
    setColour (juce::Slider::textBoxTextColourId, colours::ivory);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, colours::track);
    setColour (juce::ComboBox::outlineColourId, colours::panelEdge);
    setColour (juce::ComboBox::textColourId, colours::ivory);
    setColour (juce::ComboBox::arrowColourId, colours::brass);
    setColour (juce::PopupMenu::backgroundColourId, colours::panel);
    setColour (juce::PopupMenu::textColourId, colours::ivory);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::brass.withAlpha (0.35f));
    setColour (juce::TextButton::buttonColourId, colours::track);
    setColour (juce::TextButton::buttonOnColourId, colours::ice);
    setColour (juce::TextButton::textColourOffId, colours::ivory);
    setColour (juce::TextButton::textColourOnId, colours::background);
    setColour (juce::ToggleButton::textColourId, colours::ivory);
    setColour (juce::ToggleButton::tickColourId, colours::brass);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float lineW = juce::jmax (2.5f, radius * 0.14f);
    const float arcR = radius - lineW * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::track);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (slider.isEnabled())
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (colours::brass);
        g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float knobR = arcR - lineW * 1.4f;
    g.setColour (colours::panelEdge.brighter (0.15f));
    g.fillEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f);
    const juce::Point<float> tip (centre.x + knobR * 0.8f * std::sin (angle), centre.y - knobR * 0.8f * std::cos (angle));
    g.setColour (colours::ivory);
    g.drawLine ({ centre, tip }, 2.0f);
}

// ---------------------------------------------------------------- keyboard
StringKeyboard::StringKeyboard (ResonanceVerbProcessor& p)
    : juce::MidiKeyboardComponent (p.keyboardState, horizontalKeyboard), proc (p)
{
    setAvailableRange (21, 108);
    setScrollButtonsVisible (false);
    setVelocity (0.8f, true);
    setTitle ("String keyboard");
    setDescription ("Open strings glow with their resonance. Click a key to open that string.");
}

float StringKeyboard::glowFor (int note) const
{
    const float db = 10.0f * std::log10 (proc.getStringEnergy (note) + 1e-20f);
    return juce::jlimit (0.0f, 1.0f, (db + 70.0f) / 50.0f);
}

void StringKeyboard::drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                    juce::Colour, juce::Colour)
{
    g.setColour (colours::ivory.darker (0.05f));
    g.fillRect (area);
    const float glow = glowFor (note);
    if (glow > 0.0f)
    {
        g.setColour (colours::glow.withAlpha (0.2f + 0.75f * glow));
        g.fillRect (area.withTrimmedTop (area.getHeight() * (1.0f - 0.35f - 0.65f * glow)));
    }
    if (proc.isStringOpen (note))
    {
        g.setColour (colours::brass);
        g.fillRect (area.removeFromBottom (5.0f).reduced (1.0f, 0.0f));
    }
    if (isDown || isOver)
    {
        g.setColour (juce::Colours::black.withAlpha (isDown ? 0.25f : 0.08f));
        g.fillRect (area);
    }
    g.setColour (colours::background.withAlpha (0.6f));
    g.drawLine (area.getRight(), area.getY(), area.getRight(), area.getBottom(), 1.0f);
    if (note % 12 == 0)
    {
        g.setColour (colours::dimText);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (juce::MidiMessage::getMidiNoteName (note, true, true, 4),
                    area.withTrimmedBottom (6.0f).toNearestInt(), juce::Justification::centredBottom);
    }
}

void StringKeyboard::drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                    juce::Colour)
{
    g.setColour (juce::Colour (0xff1a1714));
    g.fillRect (area);
    const float glow = glowFor (note);
    if (glow > 0.0f)
    {
        g.setColour (colours::glow.withAlpha (0.25f + 0.75f * glow));
        g.fillRect (area.reduced (2.0f, 0.0f).withTrimmedTop (area.getHeight() * (1.0f - 0.35f - 0.65f * glow)));
    }
    if (proc.isStringOpen (note))
    {
        g.setColour (colours::brass);
        g.fillRect (area.removeFromBottom (4.0f).reduced (2.0f, 0.0f));
    }
    if (isDown || isOver)
    {
        g.setColour (juce::Colours::white.withAlpha (isDown ? 0.2f : 0.08f));
        g.fillRect (area);
    }
}

// ---------------------------------------------------------------- MIDI learn overlay
LearnOverlay::LearnOverlay (ResonanceVerbProcessor& p, int i, juce::String displayName)
    : proc (p), index (i), name (std::move (displayName))
{
    setTitle ("MIDI learn: " + p.getParameters()[i]->getName (64));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void LearnOverlay::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool target = proc.getMidiLearnTarget() == index;
    const int cc = proc.getCcForParameter (index);
    g.setColour (colours::brass.withAlpha (target ? 0.30f : (cc >= 0 ? 0.16f : 0.06f)));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (target ? colours::glow : colours::brass.withAlpha (cc >= 0 ? 0.9f : 0.5f));
    g.drawRoundedRectangle (r, 5.0f, target ? 2.0f : 1.0f);

    const auto sep = juce::String::fromUTF8 (" \xc2\xb7 ");
    const auto text = target ? juce::String ("Move a control") : (cc >= 0 ? name + sep + "CC " + juce::String (cc) : name);
    // A solid pill over the label row keeps the control itself visible; small overlays
    // (e.g. the Freeze button) get a full solid fill so the text stays readable.
    const bool small = getHeight() < 44;
    const auto pill = small ? r.reduced (1.0f) : r.withHeight (17.0f).reduced (4.0f, 1.0f);
    g.setColour (target ? colours::glow : (cc >= 0 ? colours::brass : colours::track));
    g.fillRoundedRectangle (pill, small ? 4.0f : 7.0f);
    g.setColour (target || cc >= 0 ? colours::background : colours::ivory);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawFittedText (text, pill.toNearestInt(), juce::Justification::centred, 1);
}

void LearnOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem (1, "Learn MIDI CC");
        m.addItem (2, "Clear mapping", proc.getCcForParameter (index) >= 0);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [safe = juce::Component::SafePointer<LearnOverlay> (this)] (int r)
                         {
                             if (safe == nullptr) return;
                             if (r == 1) safe->proc.startMidiLearn (safe->index);
                             if (r == 2) safe->proc.clearMidiMapping (safe->index);
                             if (auto* parent = safe->getParentComponent()) parent->repaint();
                         });
        return;
    }
    proc.startMidiLearn (proc.getMidiLearnTarget() == index ? -1 : index);
    if (auto* parent = getParentComponent()) parent->repaint();
}

// ---------------------------------------------------------------- panel
Panel::Panel (juce::String t, ResonanceVerbProcessor& p,
              std::vector<std::pair<juce::String, juce::String>> idsAndLabels)
    : proc (p), title (std::move (t))
{
    setTitle (title);
    setFocusContainerType (FocusContainerType::focusContainer);
    auto& apvts = proc.apvts;

    for (auto& [id, text] : idsAndLabels)
    {
        auto* param = apvts.getParameter (id);
        jassert (param != nullptr);
        if (param == nullptr) continue;

        auto c = std::make_unique<Control>();
        c->parameterIndex = param->getParameterIndex();
        c->label = std::make_unique<juce::Label> (id + "_label", text);
        c->label->setJustificationType (juce::Justification::centred);
        c->label->setFont (juce::FontOptions (12.0f));
        c->label->setColour (juce::Label::textColourId, colours::dimText);
        c->label->setInterceptsMouseClicks (false, false);

        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
        {
            auto box = std::make_unique<juce::ComboBox>();
            box->addItemList (choice->choices, 1);
            box->setTitle (param->getName (64));
            c->comboAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, id, *box);
            c->widget = std::move (box);
            c->span = 2;
        }
        else if (dynamic_cast<juce::AudioParameterBool*> (param) != nullptr)
        {
            auto button = std::make_unique<juce::ToggleButton> (text);
            button->setTitle (param->getName (64));
            c->buttonAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, id, *button);
            c->widget = std::move (button);
        }
        else
        {
            auto slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 16);
            slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            slider->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            slider->setColour (juce::Slider::textBoxTextColourId, colours::ivory);
            slider->setTitle (param->getName (64));
            slider->setPopupDisplayEnabled (false, false, nullptr);
            slider->setDoubleClickReturnValue (true, (double) dynamic_cast<juce::RangedAudioParameter*> (param)
                                                         ->convertFrom0to1 (param->getDefaultValue()));
            c->sliderAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, *slider);
            c->widget = std::move (slider);
        }

        c->overlay = std::make_unique<LearnOverlay> (proc, c->parameterIndex, text);
        addAndMakeVisible (*c->label);
        addAndMakeVisible (*c->widget);
        addChildComponent (*c->overlay);
        controls.push_back (std::move (c));
    }
}

void Panel::setLearnMode (bool on)
{
    learnMode = on;
    for (auto& c : controls) c->overlay->setVisible (on);
    repaint();
}

void Panel::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    g.setColour (colours::brass);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (title.toUpperCase(), getLocalBounds().removeFromTop (24).reduced (10, 0), juce::Justification::centredLeft);
}

void Panel::paintOverChildren (juce::Graphics& g)
{
    if (learnMode) return;   // overlays show the mappings
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    for (auto& c : controls)
        if (const int cc = proc.getCcForParameter (c->parameterIndex); cc >= 0)
        {
            const auto badge = c->label->getBounds().removeFromRight (34).withHeight (14).toFloat();
            g.setColour (colours::brass.withAlpha (0.25f));
            g.fillRoundedRectangle (badge, 3.0f);
            g.setColour (colours::brass);
            g.drawText ("CC" + juce::String (cc), badge, juce::Justification::centred);
        }
}

void Panel::resized()
{
    auto area = getLocalBounds().reduced (6);
    area.removeFromTop (20);
    constexpr int cols = 5;
    int cells = 0;
    for (auto& c : controls) cells += c->span;
    const int rows = juce::jmax (1, (cells + cols - 1) / cols);
    const int cellW = area.getWidth() / cols;
    const int rowH = area.getHeight() / rows;

    int col = 0, row = 0;
    for (auto& c : controls)
    {
        if (col + c->span > cols) { col = 0; ++row; }
        auto cell = juce::Rectangle<int> (area.getX() + col * cellW, area.getY() + row * rowH, cellW * c->span, rowH).reduced (3);
        if (dynamic_cast<juce::Slider*> (c->widget.get()) != nullptr && cell.getHeight() > 92)
            cell = cell.withSizeKeepingCentre (cell.getWidth(), 92);           // consistent knob size
        c->overlay->setBounds (cell);
        c->label->setBounds (cell.removeFromTop (16));
        if (dynamic_cast<juce::Slider*> (c->widget.get()) != nullptr)
            c->widget->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), juce::jmin (cell.getHeight(), 76)));
        else
            c->widget->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), juce::jmin (26, cell.getHeight())));
        col += c->span;
    }
}
} // namespace rvui

// ---------------------------------------------------------------- editor
ResonanceVerbEditor::ResonanceVerbEditor (ResonanceVerbProcessor& p)
    : AudioProcessorEditor (p), proc (p), keyboard (p)
{
    setLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);

    undoButton.setTooltip ("Undo (Cmd/Ctrl+Z)");
    redoButton.setTooltip ("Redo (Shift+Cmd+Z / Ctrl+Y)");
    undoButton.onClick = [this] { proc.getUndoManager().undo(); updateUndoButtons(); };
    redoButton.onClick = [this] { proc.getUndoManager().redo(); updateUndoButtons(); };

    prevButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");
    prevButton.onClick = [this] { proc.getPresetManager().loadNext (-1); };
    nextButton.onClick = [this] { proc.getPresetManager().loadNext (+1); };
    presetButton.setTooltip ("Open the preset browser");
    presetButton.setTitle ("Current preset");
    presetButton.onClick = [this] { setBrowserVisible (browser == nullptr || ! browser->isVisible()); };
    saveButton.setTooltip ("Save: overwrites the current user preset, or opens Save As for factory presets");
    saveButton.onClick = [this] { quickSave(); };

    midiButton.setTooltip ("MIDI Learn and mappings");
    midiButton.setColour (juce::TextButton::buttonOnColourId, rvui::colours::brass);
    midiButton.onClick = [this] { showMidiMenu(); };

    freezeButton.setClickingTogglesState (true);
    freezeButton.setTitle ("Freeze");
    freezeButton.setTooltip ("Hold everything that is ringing; new input is ignored");
    freezeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "freeze", freezeButton);
    freezeOverlay = std::make_unique<rvui::LearnOverlay> (proc, proc.apvts.getParameter ("freeze")->getParameterIndex(), "Freeze");

    for (auto* c : std::initializer_list<juce::Component*> { &undoButton, &redoButton, &prevButton, &nextButton, &presetButton,
                                                             &saveButton, &midiButton, &freezeButton, &keyboard })
        addAndMakeVisible (c);
    addChildComponent (*freezeOverlay);

    using P = rvui::Panel;
    using L = std::vector<std::pair<juce::String, juce::String>>;
    panels.push_back (std::make_unique<P> ("Performance", proc, L {
        { "mode", "Mode" }, { "scale", "Scale" }, { "pedal", "Pedal" },
        { "scaleRoot", "Root" }, { "scaleLow", "Low" }, { "scaleHigh", "High" } }));
    panels.push_back (std::make_unique<P> ("Input", proc, L {
        { "inHP", "High-Pass" }, { "inLP", "Low-Pass" }, { "drive", "Drive" }, { "excite", "Excite" },
        { "exciteSrc", "Source" } }));
    panels.push_back (std::make_unique<P> ("Strings", proc, L {
        { "decay", "Decay" }, { "release", "Damper" }, { "hfDamp", "HF Damp" }, { "bright", "Bright" },
        { "detune", "Detune" }, { "inharm", "Inharm." }, { "velSens", "Velocity" }, { "spread", "Spread" },
        { "unison", "Unison" } }));
    panels.push_back (std::make_unique<P> ("Character", proc, L {
        { "halo", "Halo" }, { "doubleDecay", "Dbl Decay" }, { "body", "Body" }, { "bodySize", "Body Size" },
        { "bleed", "Bleed" } }));
    panels.push_back (std::make_unique<P> ("Tuning", proc, L {
        { "a4", "A4" }, { "stretch", "Stretch" }, { "bendRange", "Bend" },
        { "temper", "Temperament" }, { "root", "Temp. Root" } }));
    panels.push_back (std::make_unique<P> ("Blur", proc, L {
        { "blur", "Amount" }, { "blurSize", "Size" }, { "blurTail", "Tail" }, { "blurMod", "Motion" } }));
    panels.push_back (std::make_unique<P> ("Space & Tone", proc, L {
        { "preDelay", "Pre-Delay" }, { "lowCut", "Low Cut" }, { "tilt", "Tilt" }, { "duck", "Duck" },
        { "duckRel", "Duck Rel." } }));
    panels.push_back (std::make_unique<P> ("Dynamics", proc, L {
        { "compThr", "Threshold" }, { "compRatio", "Ratio" }, { "compAtk", "Attack" }, { "compRel", "Release" },
        { "makeup", "Makeup" } }));
    panels.push_back (std::make_unique<P> ("Output", proc, L {
        { "width", "Width" }, { "mix", "Dry/Wet" }, { "out", "Output" } }));
    for (auto& panel : panels) addAndMakeVisible (*panel);

    setSize (1240, 790);
    timerCallback();
    startTimerHz (30);
}

ResonanceVerbEditor::~ResonanceVerbEditor()
{
    stopTimer();
    proc.startMidiLearn (-1);
    setLookAndFeel (nullptr);
}

void ResonanceVerbEditor::setBrowserVisible (bool show)
{
    if (show && browser == nullptr)
    {
        browser = std::make_unique<rvui::PresetBrowser> (proc);
        browser->onClose = [this] { setBrowserVisible (false); };
        addChildComponent (*browser);
        browser->setBounds (panelArea);
    }
    if (browser == nullptr) return;
    if (show) { setLearnMode (false); browser->refresh(); }
    browser->setVisible (show);
    if (show) browser->toFront (true);
    else grabKeyboardFocus();
}

void ResonanceVerbEditor::setLearnMode (bool on)
{
    learnMode = on;
    if (on && browser != nullptr) browser->setVisible (false);
    if (! on) proc.startMidiLearn (-1);
    for (auto& panel : panels) panel->setLearnMode (on);
    freezeOverlay->setVisible (on);
    if (on) freezeOverlay->toFront (false);
    midiButton.setToggleState (on, juce::dontSendNotification);
    midiButton.setColour (juce::TextButton::buttonColourId, on ? rvui::colours::brass : rvui::colours::track);
    midiButton.setColour (juce::TextButton::textColourOffId, on ? rvui::colours::background : rvui::colours::ivory);
    repaint();
}

void ResonanceVerbEditor::showMidiMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "MIDI Learn mode", true, learnMode);
    m.addSeparator();
    bool anyMapped = false;
    juce::PopupMenu mappings;
    const auto& params = proc.getParameters();
    for (int i = 0; i < params.size(); ++i)
        if (const int cc = proc.getCcForParameter (i); cc >= 0)
        {
            anyMapped = true;
            mappings.addItem (1000 + i, "CC " + juce::String (cc) + "  ->  " + params[i]->getName (40) + "   (click to clear)");
        }
    m.addSubMenu ("Current mappings", mappings, anyMapped);
    m.addItem (2, "Clear all mappings", anyMapped);
    m.addSeparator();
    m.addItem (3, "Save mappings as default", anyMapped);
    m.addItem (4, "Recall default mappings");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&midiButton),
                     [safe = juce::Component::SafePointer<ResonanceVerbEditor> (this)] (int r)
    {
        auto* self = safe.getComponent();
        if (self == nullptr || r == 0) return;
        auto& pr = self->proc;
        if (r == 1) self->setLearnMode (! self->learnMode);
        else if (r == 2) pr.clearAllMidiMappings();
        else if (r == 3) pr.saveMidiMapAsDefault();
        else if (r == 4) pr.recallDefaultMidiMap();
        else if (r >= 1000) pr.clearMidiMapping (r - 1000);
        self->repaint();
    });
}

void ResonanceVerbEditor::quickSave()
{
    auto& pm = proc.getPresetManager();
    const auto cur = pm.getCurrent();
    if (! cur.isFactory && cur.isValid())
    {
        if (const auto r = pm.save (cur.name, cur.category, cur.author, cur.description, true); r.failed())
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                              .withTitle ("Save failed").withMessage (r.getErrorMessage()).withButton ("OK", 0), nullptr);
        if (browser != nullptr) browser->refresh();
        return;
    }
    setBrowserVisible (true);
    browser->openSaveAs();
}

bool ResonanceVerbEditor::keyPressed (const juce::KeyPress& k)
{
    const auto cmd = juce::ModifierKeys::commandModifier;
    if (k == juce::KeyPress ('z', cmd, 0))
    {
        proc.getUndoManager().undo(); updateUndoButtons(); return true;
    }
    if (k == juce::KeyPress ('z', cmd | juce::ModifierKeys::shiftModifier, 0) || k == juce::KeyPress ('y', cmd, 0))
    {
        proc.getUndoManager().redo(); updateUndoButtons(); return true;
    }
    if (k == juce::KeyPress::escapeKey && learnMode) { setLearnMode (false); return true; }
    return false;
}

void ResonanceVerbEditor::updateUndoButtons()
{
    auto& um = proc.getUndoManager();
    undoButton.setEnabled (um.canUndo());
    redoButton.setEnabled (um.canRedo());
    undoButton.setTooltip (um.canUndo() ? "Undo " + um.getUndoDescription() + "  (Cmd/Ctrl+Z)" : juce::String ("Nothing to undo"));
    redoButton.setTooltip (um.canRedo() ? "Redo " + um.getRedoDescription() + "  (Shift+Cmd+Z / Ctrl+Y)" : juce::String ("Nothing to redo"));
}

void ResonanceVerbEditor::paint (juce::Graphics& g)
{
    g.fillAll (rvui::colours::background);
    auto title = getLocalBounds().removeFromTop (52).reduced (16, 6).removeFromLeft (230);
    g.setColour (rvui::colours::ivory);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("RESONANCE VERB", title.removeFromTop (26), juce::Justification::bottomLeft);
    g.setColour (rvui::colours::dimText);
    g.setFont (juce::FontOptions (11.5f));
    g.drawText ("sympathetic string resonator", title, juce::Justification::topLeft);

    auto m = meterArea;
    g.setColour (rvui::colours::ivory);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (juce::String (proc.getActiveVoices()) + " / 64", m.removeFromLeft (64), juce::Justification::centredLeft);
    const int meterW = m.getWidth() / 2;
    drawMeter (g, m.removeFromLeft (meterW).reduced (4, 13), "COMP", proc.getGainReductionDb(), rvui::colours::brass);
    drawMeter (g, m.removeFromLeft (meterW).reduced (4, 13), "DUCK", proc.getDuckReductionDb(), rvui::colours::ice);
}

void ResonanceVerbEditor::paintOverChildren (juce::Graphics& g)
{
    if (! learnMode) return;
    const auto banner = keyboard.getBounds().removeFromBottom (26).reduced (keyboard.getWidth() / 4, 0).toFloat();
    g.setColour (rvui::colours::background.withAlpha (0.85f));
    g.fillRoundedRectangle (banner, 6.0f);
    g.setColour (rvui::colours::glow);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("MIDI LEARN: click a control, then move a knob on your controller. Right-click to clear. Esc to finish.",
                banner, juce::Justification::centred);
}

void ResonanceVerbEditor::drawMeter (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& name,
                                     float reductionDb, juce::Colour c)
{
    g.setColour (rvui::colours::dimText);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (name, r.removeFromLeft (36), juce::Justification::centredLeft);
    const auto bar = r.toFloat();
    g.setColour (rvui::colours::track);
    g.fillRoundedRectangle (bar, 3.0f);
    const float amount = juce::jlimit (0.0f, 1.0f, -reductionDb / 24.0f);
    g.setColour (c);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * amount), 3.0f);
}

void ResonanceVerbEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (52).reduced (16, 11);
    freezeButton.setBounds (header.removeFromRight (92));
    freezeOverlay->setBounds (freezeButton.getBounds().expanded (2));
    header.removeFromRight (8);
    midiButton.setBounds (header.removeFromRight (64));
    header.removeFromRight (14);
    saveButton.setBounds (header.removeFromRight (56));
    header.removeFromRight (4);
    nextButton.setBounds (header.removeFromRight (28));
    header.removeFromRight (4);
    presetButton.setBounds (header.removeFromRight (250));
    header.removeFromRight (4);
    prevButton.setBounds (header.removeFromRight (28));
    header.removeFromRight (14);
    redoButton.setBounds (header.removeFromRight (54));
    header.removeFromRight (4);
    undoButton.setBounds (header.removeFromRight (54));
    header.removeFromRight (14);
    header.removeFromLeft (236);
    meterArea = header;

    auto kb = area.removeFromTop (96).reduced (16, 4);
    keyboard.setBounds (kb);
    keyboard.setKeyWidth ((float) kb.getWidth() / 52.0f);   // 52 white keys, A0..C8

    area.reduce (12, 8);
    panelArea = area;
    const int rowH = area.getHeight() / 3;
    const int colW = area.getWidth() / 3;
    for (size_t i = 0; i < panels.size(); ++i)
    {
        const int r = (int) i / 3, c = (int) i % 3;
        panels[i]->setBounds (juce::Rectangle<int> (area.getX() + c * colW, area.getY() + r * rowH, colW, rowH).reduced (4));
    }
    if (browser != nullptr) browser->setBounds (panelArea);
}

void ResonanceVerbEditor::timerCallback()
{
    keyboard.repaint();
    repaint (meterArea);
    updateUndoButtons();

    auto& pm = proc.getPresetManager();
    const auto text = pm.getCurrent().name + (pm.isModified() ? " *" : "");
    if (text != lastPresetText)
    {
        lastPresetText = text;
        presetButton.setButtonText (text);
    }

    const int version = proc.getMidiMapVersion(), target = proc.getMidiLearnTarget();
    if (version != lastMapVersion || target != lastLearnTarget)
    {
        lastMapVersion = version;
        lastLearnTarget = target;
        for (auto& panel : panels) panel->repaint();
        freezeOverlay->repaint();
    }
}
