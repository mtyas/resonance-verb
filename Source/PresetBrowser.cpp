// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PresetBrowser.h"
#include "PluginEditor.h"   // colours

namespace rvui
{
namespace
{
const juce::String allItem = "All Presets", favItem = juce::String::fromUTF8 ("\xe2\x98\x85 Favourites"),
                   factoryItem = "Factory", userItem = "User";

void styleEditor (juce::TextEditor& e)
{
    e.setColour (juce::TextEditor::backgroundColourId, colours::track);
    e.setColour (juce::TextEditor::outlineColourId, colours::panelEdge);
    e.setColour (juce::TextEditor::focusedOutlineColourId, colours::brass);
    e.setColour (juce::TextEditor::textColourId, colours::ivory);
    e.setColour (juce::CaretComponent::caretColourId, colours::brass);
}
} // namespace

// ================================================================ save / rename dialog
SavePresetDialog::SavePresetDialog()
{
    heading.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    heading.setColour (juce::Label::textColourId, colours::brass);
    errorLabel.setColour (juce::Label::textColourId, juce::Colour (0xffff7a6b));
    for (auto* l : { &nameLabel, &categoryLabel, &authorLabel, &descriptionLabel })
        l->setColour (juce::Label::textColourId, colours::dimText);
    for (auto* e : { &name, &author, &description }) styleEditor (*e);
    description.setMultiLine (true, true);
    description.setReturnKeyStartsNewLine (true);
    category.setEditableText (true);
    name.setTitle ("Preset name");
    category.setTitle ("Preset category");
    author.setTitle ("Preset author");
    description.setTitle ("Preset description");

    confirm.onClick = [this]
    {
        if (onConfirm) onConfirm ({ name.getText(), category.getText(), author.getText(), description.getText() });
    };
    cancel.onClick = [this] { if (onCancel) onCancel(); };
    name.onReturnKey = [this] { confirm.triggerClick(); };
    name.onEscapeKey = [this] { cancel.triggerClick(); };

    for (auto* c : std::initializer_list<juce::Component*> { &heading, &nameLabel, &categoryLabel, &authorLabel, &descriptionLabel,
                                                             &errorLabel, &name, &author, &description, &category, &confirm, &cancel })
        addAndMakeVisible (c);
}

void SavePresetDialog::showFor (bool renameMode, const Fields& f, const juce::StringArray& cats)
{
    renaming = renameMode;
    heading.setText (renameMode ? "Rename preset" : "Save preset", juce::dontSendNotification);
    confirm.setButtonText (renameMode ? "Rename" : "Save");
    name.setText (f.name, false);
    category.clear (juce::dontSendNotification);
    category.addItemList (cats, 1);
    category.setText (f.category.isNotEmpty() ? f.category : juce::String ("User"), juce::dontSendNotification);
    author.setText (f.author, false);
    description.setText (f.description, false);
    for (auto* c : std::initializer_list<juce::Component*> { &category, &author, &description, &categoryLabel, &authorLabel, &descriptionLabel })
        c->setVisible (! renameMode);
    setError ({});
    setVisible (true);
    toFront (true);
    name.grabKeyboardFocus();
    name.selectAll();
}

void SavePresetDialog::setError (const juce::String& e) { errorLabel.setText (e, juce::dontSendNotification); }

void SavePresetDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    const auto box = getLocalBounds().withSizeKeepingCentre (460, renaming ? 170 : 360).toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (box, 8.0f);
    g.setColour (colours::brass.withAlpha (0.6f));
    g.drawRoundedRectangle (box, 8.0f, 1.0f);
}

void SavePresetDialog::resized()
{
    auto box = getLocalBounds().withSizeKeepingCentre (460, renaming ? 170 : 360).reduced (18);
    heading.setBounds (box.removeFromTop (26));
    box.removeFromTop (6);
    auto row = [&box] (juce::Label& l, juce::Component& c, int h)
    {
        auto r = box.removeFromTop (h);
        l.setBounds (r.removeFromLeft (90));
        c.setBounds (r);
        box.removeFromTop (8);
    };
    row (nameLabel, name, 28);
    if (! renaming)
    {
        row (categoryLabel, category, 28);
        row (authorLabel, author, 28);
        row (descriptionLabel, description, 96);
    }
    auto buttons = box.removeFromBottom (30);
    cancel.setBounds (buttons.removeFromRight (100));
    buttons.removeFromRight (8);
    confirm.setBounds (buttons.removeFromRight (100));
    errorLabel.setBounds (buttons);
}

// ================================================================ browser
PresetBrowser::PresetBrowser (ResonanceVerbProcessor& p)
    : proc (p), presets (p.getPresetManager())
{
    setTitle ("Preset browser");
    setWantsKeyboardFocus (true);

    styleEditor (search);
    search.setTextToShowWhenEmpty ("Search name, category, author or description...", colours::dimText);
    search.setTitle ("Search presets");
    search.onTextChange = [this] { refilter(); };
    search.onEscapeKey = [this]
    {
        if (search.isEmpty()) { if (onClose) onClose(); return; }
        search.clear();
        refilter();
    };
    addAndMakeVisible (search);

    categories.setRowHeight (26);
    categories.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (categories);

    auto& header = table.getHeader();
    header.addColumn (juce::String::fromUTF8 ("\xe2\x98\x85"), favouriteCol, 34, 34, 34, juce::TableHeaderComponent::notResizableOrSortable);
    header.addColumn ("Name", nameCol, 300, 120, 600);
    header.addColumn ("Category", categoryCol, 130, 80, 300);
    header.addColumn ("Author", authorCol, 130, 80, 300);
    header.addColumn ("Source", sourceCol, 80, 60, 120);
    header.setColour (juce::TableHeaderComponent::backgroundColourId, colours::track);
    header.setColour (juce::TableHeaderComponent::textColourId, colours::ivory);
    header.setColour (juce::TableHeaderComponent::outlineColourId, colours::panelEdge);
    table.setRowHeight (26);
    table.setColour (juce::ListBox::backgroundColourId, colours::background.brighter (0.03f));
    table.setColour (juce::ListBox::outlineColourId, colours::panelEdge);
    table.setOutlineThickness (1);
    table.setMultipleSelectionEnabled (false);
    addAndMakeVisible (table);

    detailName.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    detailName.setColour (juce::Label::textColourId, colours::ivory);
    detailMeta.setColour (juce::Label::textColourId, colours::dimText);
    styleEditor (detailDescription);
    detailDescription.setMultiLine (true, true);
    detailDescription.setReadOnly (true);
    detailDescription.setCaretVisible (false);
    detailDescription.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    detailDescription.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    for (auto* c : std::initializer_list<juce::Component*> { &detailName, &detailMeta, &detailDescription })
        addAndMakeVisible (c);

    loadButton.onClick   = [this] { loadSelected(); };
    saveAsButton.onClick = [this] { showDialog (false); };
    renameButton.onClick = [this] { showDialog (true); };
    deleteButton.onClick = [this] { confirmDelete(); };
    importButton.onClick = [this] { importPresets(); };
    folderButton.onClick = [this]
    {
        presets.getUserDirectory().createDirectory();
        presets.getUserDirectory().startAsProcess();
    };
    closeButton.onClick = [this] { if (onClose) onClose(); };
    loadButton.setTooltip ("Load the selected preset (Enter or double-click)");
    deleteButton.setTooltip ("Move the selected user preset to the Trash");
    importButton.setTooltip ("Copy .rvpreset files into your user preset folder");
    folderButton.setTooltip (presets.getUserDirectory().getFullPathName());
    for (auto* b : { &loadButton, &saveAsButton, &renameButton, &deleteButton, &importButton, &folderButton, &closeButton })
        addAndMakeVisible (b);

    dialog.onCancel = [this] { dialog.setVisible (false); table.grabKeyboardFocus(); };
    addChildComponent (dialog);

    refresh();
}

PresetBrowser::~PresetBrowser() = default;

void PresetBrowser::refresh()
{
    presets.rescan();
    rebuildCategories();
    refilter();
}

void PresetBrowser::rebuildCategories()
{
    const auto previous = categoryItems[categories.getSelectedRow()];
    categoryItems = { allItem, favItem, factoryItem, userItem };
    categoryItems.addArray (presets.getCategories());
    categories.updateContent();
    const int idx = juce::jmax (0, categoryItems.indexOf (previous));
    categories.selectRow (idx, true, true);
}

void PresetBrowser::refilter()
{
    const auto& all = presets.getAll();
    const auto cat = categoryItems[juce::jmax (0, categories.getSelectedRow())];
    const auto needle = search.getText().trim();

    rows.clearQuick();
    for (int i = 0; i < all.size(); ++i)
    {
        const auto& p = all.getReference (i);
        if (cat == favItem && ! presets.isFavourite (p)) continue;
        if (cat == factoryItem && ! p.isFactory) continue;
        if (cat == userItem && p.isFactory) continue;
        if (cat != allItem && cat != favItem && cat != factoryItem && cat != userItem && p.category != cat) continue;
        if (needle.isNotEmpty() && ! (p.name.containsIgnoreCase (needle) || p.category.containsIgnoreCase (needle)
                                      || p.author.containsIgnoreCase (needle) || p.description.containsIgnoreCase (needle)))
            continue;
        rows.add (i);
    }

    if (sortColumn != 0)
    {
        auto key = [&] (int i) -> juce::String
        {
            const auto& p = all.getReference (i);
            switch (sortColumn)
            {
                case categoryCol: return p.category + "\x01" + p.name;
                case authorCol:   return p.author + "\x01" + p.name;
                case sourceCol:   return juce::String (p.isFactory ? "Factory" : "User") + "\x01" + p.name;
                default:          return p.name;
            }
        };
        std::stable_sort (rows.begin(), rows.end(), [&] (int a, int b)
        {
            const int c = key (a).compareNatural (key (b));
            return sortForwards ? c < 0 : c > 0;
        });
    }

    table.updateContent();
    const auto currentKey = presets.getCurrent().getKey();
    int select = -1;
    for (int r = 0; r < rows.size(); ++r)
        if (all.getReference (rows[r]).getKey() == currentKey) select = r;
    if (select >= 0) { table.selectRow (select); table.scrollToEnsureRowIsOnscreen (select); }
    else table.deselectAllRows();
    table.repaint();
    updateDetails();
}

const rv::PresetInfo* PresetBrowser::selectedPreset() const
{
    const int r = table.getSelectedRow();
    if (! juce::isPositiveAndBelow (r, rows.size())) return nullptr;
    return &presets.getAll().getReference (rows[r]);
}

void PresetBrowser::updateDetails()
{
    const auto* p = selectedPreset();
    detailName.setText (p ? p->name : juce::String ("No preset selected"), juce::dontSendNotification);
    detailMeta.setText (p ? (p->category + "  \xc2\xb7  " + (p->author.isNotEmpty() ? p->author : juce::String ("Unknown author"))
                             + "  \xc2\xb7  " + (p->isFactory ? "Factory" : "User"))
                          : juce::String(),
                        juce::dontSendNotification);
    detailDescription.setText (p ? (p->description.isNotEmpty() ? p->description : juce::String ("No description."))
                                 : juce::String ("Pick a preset on the left. Double-click or press Enter to load it."));
    loadButton.setEnabled (p != nullptr);
    renameButton.setEnabled (p != nullptr && ! p->isFactory);
    deleteButton.setEnabled (p != nullptr && ! p->isFactory);
}

void PresetBrowser::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    if (selected)                g.fillAll (colours::brass.withAlpha (0.28f));
    else if (row % 2 == 1)       g.fillAll (juce::Colours::white.withAlpha (0.025f));
}

void PresetBrowser::paintCell (juce::Graphics& g, int row, int col, int w, int h, bool)
{
    if (! juce::isPositiveAndBelow (row, rows.size())) return;
    const auto& p = presets.getAll().getReference (rows[row]);
    const bool isCurrent = p.getKey() == presets.getCurrent().getKey();
    g.setFont (juce::FontOptions (14.0f, isCurrent ? juce::Font::bold : juce::Font::plain));
    juce::String text;
    juce::Colour c = colours::ivory;
    switch (col)
    {
        case favouriteCol:
            text = presets.isFavourite (p) ? juce::String::fromUTF8 ("\xe2\x98\x85") : juce::String::fromUTF8 ("\xe2\x98\x86");
            c = presets.isFavourite (p) ? colours::glow : colours::dimText;
            break;
        case nameCol:     text = (isCurrent ? juce::String::fromUTF8 ("\xe2\x97\x8f ") : juce::String()) + p.name; break;
        case categoryCol: text = p.category; c = colours::dimText; break;
        case authorCol:   text = p.author; c = colours::dimText; break;
        case sourceCol:   text = p.isFactory ? "Factory" : "User"; c = p.isFactory ? colours::dimText : colours::brass; break;
        default: break;
    }
    g.setColour (c);
    g.drawText (text, 6, 0, w - 8, h, col == favouriteCol ? juce::Justification::centred : juce::Justification::centredLeft, true);
}

void PresetBrowser::cellClicked (int row, int col, const juce::MouseEvent&)
{
    if (col != favouriteCol || ! juce::isPositiveAndBelow (row, rows.size())) return;
    const auto& p = presets.getAll().getReference (rows[row]);
    presets.setFavourite (p, ! presets.isFavourite (p));
    if (categoryItems[categories.getSelectedRow()] == favItem) refilter();
    else table.repaintRow (row);
}

void PresetBrowser::cellDoubleClicked (int, int col, const juce::MouseEvent&)
{
    if (col != favouriteCol) loadSelected();
}

void PresetBrowser::selectedRowsChanged (int) { updateDetails(); }
void PresetBrowser::returnKeyPressed (int)    { loadSelected(); }

void PresetBrowser::sortOrderChanged (int col, bool forwards)
{
    sortColumn = col;
    sortForwards = forwards;
    refilter();
}

void PresetBrowser::CategoryModel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (selected) g.fillAll (colours::brass.withAlpha (0.28f));
    const auto item = owner.categoryItems[row];
    const bool builtIn = row < 4;
    g.setColour (builtIn ? colours::ivory : colours::dimText);
    g.setFont (juce::FontOptions (14.0f, builtIn ? juce::Font::bold : juce::Font::plain));
    g.drawText ((builtIn ? juce::String() : juce::String ("    ")) + item, 10, 0, w - 12, h, juce::Justification::centredLeft, true);
}

void PresetBrowser::loadSelected()
{
    if (const auto* p = selectedPreset())
    {
        const auto copy = *p;               // load() may rescan
        presets.load (copy);
        table.repaint();
    }
}

void PresetBrowser::openSaveAs() { showDialog (false); }

void PresetBrowser::showDialog (bool rename)
{
    SavePresetDialog::Fields f;
    if (rename)
    {
        const auto* p = selectedPreset();
        if (p == nullptr || p->isFactory) return;
        f.name = p->name;
        const auto target = *p;
        dialog.onConfirm = [this, target] (const SavePresetDialog::Fields& fields)
        {
            if (const auto r = presets.rename (target, fields.name); r.failed()) { dialog.setError (r.getErrorMessage()); return; }
            dialog.setVisible (false);
            refresh();
        };
    }
    else
    {
        const auto& cur = presets.getCurrent();
        f.name = cur.isFactory ? cur.name + " (copy)" : cur.name;
        f.category = cur.isFactory ? juce::String ("User") : cur.category;
        f.author = presets.getSettings().getValue ("author");
        f.description = cur.description;
        dialog.onConfirm = [this] (const SavePresetDialog::Fields& fields) { saveWithFields (fields, false); };
    }
    dialog.setBounds (getLocalBounds());
    dialog.showFor (rename, f, presets.getCategories());
}

void PresetBrowser::saveWithFields (const SavePresetDialog::Fields& f, bool overwrite)
{
    const auto r = presets.save (f.name, f.category, f.author, f.description, overwrite);
    if (r.wasOk())
    {
        presets.getSettings().setValue ("author", f.author.trim());
        presets.getSettings().saveIfNeeded();
        dialog.setVisible (false);
        refresh();
        return;
    }
    if (r.getErrorMessage() != "exists") { dialog.setError (r.getErrorMessage()); return; }

    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("Replace preset?")
                       .withMessage ("A user preset called \"" + f.name.trim() + "\" already exists. Replace it?")
                       .withButton ("Replace", 1)
                       .withButton ("Cancel", 0)
                       .withAssociatedComponent (this);
    juce::AlertWindow::showAsync (options, [safe = juce::WeakReference<PresetBrowser> (this), f] (int result)
    {
        if (auto* self = safe.get(); self != nullptr && result == 1)
            self->saveWithFields (f, true);
    });
}

void PresetBrowser::confirmDelete()
{
    const auto* p = selectedPreset();
    if (p == nullptr || p->isFactory) return;
    const auto target = *p;
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::WarningIcon)
                       .withTitle ("Delete preset?")
                       .withMessage ("Move \"" + target.name + "\" to the Trash? You can restore it from there.")
                       .withButton ("Move to Trash", 1)
                       .withButton ("Cancel", 0)
                       .withAssociatedComponent (this);
    juce::AlertWindow::showAsync (options, [safe = juce::WeakReference<PresetBrowser> (this), target] (int result)
    {
        auto* self = safe.get();
        if (self == nullptr || result != 1) return;
        if (const auto r = self->presets.remove (target); r.failed()) self->showError (r.getErrorMessage());
        self->refresh();
    });
}

void PresetBrowser::importPresets()
{
    chooser = std::make_unique<juce::FileChooser> ("Import Resonance Verb presets", juce::File(),
                                                   juce::String ("*") + rv::PresetManager::fileExtension);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [safe = juce::WeakReference<PresetBrowser> (this)] (const juce::FileChooser& fc)
    {
        auto* self = safe.get();
        if (self == nullptr) return;
        juce::StringArray errors;
        for (const auto& f : fc.getResults())
            if (const auto r = self->presets.import (f); r.failed()) errors.add (r.getErrorMessage());
        self->refresh();
        if (! errors.isEmpty()) self->showError (errors.joinIntoString ("\n"));
    });
}

void PresetBrowser::showError (const juce::String& message)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle ("Preset browser")
                                      .withMessage (message)
                                      .withButton ("OK", 0)
                                      .withAssociatedComponent (this),
                                  nullptr);
}

bool PresetBrowser::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    return false;
}

void PresetBrowser::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    g.setColour (colours::brass.withAlpha (0.5f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);
    g.setColour (colours::brass);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("PRESETS", getLocalBounds().reduced (14, 10).removeFromTop (20), juce::Justification::centredLeft);
}

void PresetBrowser::resized()
{
    auto area = getLocalBounds().reduced (14, 10);
    auto top = area.removeFromTop (30);
    top.removeFromLeft (90);
    closeButton.setBounds (top.removeFromRight (80));
    top.removeFromRight (10);
    search.setBounds (top.removeFromLeft (juce::jmin (420, top.getWidth())));
    area.removeFromTop (10);

    categories.setBounds (area.removeFromLeft (180));
    area.removeFromLeft (10);
    auto details = area.removeFromRight (280);
    area.removeFromRight (10);
    table.setBounds (area);

    detailName.setBounds (details.removeFromTop (30));
    detailMeta.setBounds (details.removeFromTop (22));
    details.removeFromTop (6);
    auto buttons = details.removeFromBottom (30 * 3 + 16);
    detailDescription.setBounds (details);
    auto rowOf = [&buttons] { auto r = buttons.removeFromTop (30); buttons.removeFromTop (8); return r; };
    auto r1 = rowOf(), r2 = rowOf(), r3 = rowOf();
    loadButton.setBounds (r1.removeFromLeft (r1.getWidth() / 2 - 4)); r1.removeFromLeft (8); saveAsButton.setBounds (r1);
    renameButton.setBounds (r2.removeFromLeft (r2.getWidth() / 2 - 4)); r2.removeFromLeft (8); deleteButton.setBounds (r2);
    importButton.setBounds (r3.removeFromLeft (r3.getWidth() / 2 - 4)); r3.removeFromLeft (8); folderButton.setBounds (r3);

    dialog.setBounds (getLocalBounds());
}
} // namespace rvui
