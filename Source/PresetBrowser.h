// SPDX-License-Identifier: AGPL-3.0-or-later
// Resonance Verb — preset browser: categories, search, favourites, sortable table,
// details pane, Save As / Rename / Delete (to Trash) / Import / Show Folder.
#pragma once

#include "PluginProcessor.h"

namespace rvui
{
/** Inline dialog for "Save As" and "Rename". */
class SavePresetDialog final : public juce::Component
{
public:
    struct Fields { juce::String name, category, author, description; };

    SavePresetDialog();
    void showFor (bool renameMode, const Fields& initial, const juce::StringArray& categories);
    void setError (const juce::String&);

    std::function<void (const Fields&)> onConfirm;
    std::function<void()> onCancel;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    bool renaming = false;
    juce::Label heading, nameLabel { {}, "Name" }, categoryLabel { {}, "Category" },
                authorLabel { {}, "Author" }, descriptionLabel { {}, "Description" }, errorLabel;
    juce::TextEditor name, author, description;
    juce::ComboBox category;
    juce::TextButton confirm { "Save" }, cancel { "Cancel" };
};

class PresetBrowser final : public juce::Component,
                            private juce::TableListBoxModel
{
public:
    explicit PresetBrowser (ResonanceVerbProcessor&);
    ~PresetBrowser() override;

    /** Rescans the library and selects the current preset. */
    void refresh();
    void openSaveAs();

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum Columns { favouriteCol = 1, nameCol, categoryCol, authorCol, sourceCol };

    // table model
    int getNumRows() override { return rows.size(); }
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (juce::Graphics&, int row, int col, int w, int h, bool selected) override;
    void cellClicked (int row, int col, const juce::MouseEvent&) override;
    void cellDoubleClicked (int row, int col, const juce::MouseEvent&) override;
    void selectedRowsChanged (int) override;
    void sortOrderChanged (int col, bool forwards) override;
    void returnKeyPressed (int) override;

    struct CategoryModel final : juce::ListBoxModel
    {
        explicit CategoryModel (PresetBrowser& o) : owner (o) {}
        int getNumRows() override { return owner.categoryItems.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        void selectedRowsChanged (int) override { owner.refilter(); }
        PresetBrowser& owner;
    };

    void rebuildCategories();
    void refilter();
    void updateDetails();
    const rv::PresetInfo* selectedPreset() const;
    void loadSelected();
    void showDialog (bool rename);
    void saveWithFields (const SavePresetDialog::Fields&, bool overwrite);
    void confirmDelete();
    void importPresets();
    void showError (const juce::String&);

    ResonanceVerbProcessor& proc;
    rv::PresetManager& presets;

    juce::TextEditor search;
    CategoryModel categoryModel { *this };
    juce::ListBox categories { "Categories", &categoryModel };
    juce::TableListBox table { "Presets", this };
    juce::Label detailName, detailMeta;
    juce::TextEditor detailDescription;
    juce::TextButton loadButton { "Load" }, saveAsButton { "Save As..." }, renameButton { "Rename..." },
                     deleteButton { "Delete" }, importButton { "Import..." }, folderButton { "Show Folder" },
                     closeButton { "Close" };
    SavePresetDialog dialog;

    juce::StringArray categoryItems;
    juce::Array<int> rows;                    // indices into presets.getAll()
    int sortColumn = 0;                       // 0 = library order (factory first)
    bool sortForwards = true;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_WEAK_REFERENCEABLE (PresetBrowser)
};
} // namespace rvui
