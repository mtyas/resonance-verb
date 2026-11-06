// SPDX-License-Identifier: AGPL-3.0-or-later
// Resonance Verb — preset management: factory presets (read-only, also exposed as host
// programs) and user presets stored as .rvpreset XML files, with categories, authors,
// descriptions, favourites, rename/delete (to Trash), import and "modified" tracking.
// Message thread only.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>
#include "FactoryPresets.h"

namespace rv
{
struct PresetInfo
{
    juce::String name, category, author, description;
    bool isFactory = false;
    int factoryIndex = -1;
    juce::File file;

    juce::String getKey() const { return isFactory ? "factory:" + name : file.getFullPathName(); }
    bool isValid() const { return isFactory ? factoryIndex >= 0 : file.existsAsFile(); }
};

class PresetManager
{
public:
    static constexpr const char* fileExtension = ".rvpreset";

    /** apply(actionName, normalisedValues) must set every parameter (and record undo). */
    using ApplyFn = std::function<void (const juce::String&, const std::vector<float>&)>;

    PresetManager (juce::AudioProcessor&, ApplyFn);

    // ---- library
    void rescan();
    const juce::Array<PresetInfo>& getAll() const noexcept { return presets; }
    juce::StringArray getCategories() const;
    juce::File getUserDirectory() const;
    void setUserDirectory (const juce::File& dir);   // tests / custom locations

    // ---- current preset
    bool load (const PresetInfo&);
    bool loadFactory (int index);
    void loadNext (int direction);
    const PresetInfo& getCurrent() const noexcept { return current; }
    bool isModified() const;
    /** Restores "current" after a session reload without touching parameter values. */
    void restoreCurrent (const juce::String& key);

    // ---- editing the library
    juce::Result save (const juce::String& name, const juce::String& category, const juce::String& author,
                       const juce::String& description, bool overwrite);
    juce::Result rename (const PresetInfo&, const juce::String& newName);
    juce::Result remove (const PresetInfo&);              // moves the file to the Trash / Recycle Bin
    juce::Result import (const juce::File& source);
    bool userPresetExists (const juce::String& name) const;

    // ---- favourites & settings (persisted in the user settings file)
    bool isFavourite (const PresetInfo&) const;
    void setFavourite (const PresetInfo&, bool);
    juce::PropertiesFile& getSettings() { return *settings; }
    void setSettingsFile (const juce::File&);        // tests: keep the user's real settings untouched

    std::vector<float> valuesFor (const PresetInfo&) const;
    std::vector<float> currentValues() const;

    static juce::String sanitiseFileName (const juce::String&);

private:
    static std::optional<PresetInfo> readHeader (const juce::File&);
    juce::Result writeFile (const juce::File&, const juce::String& name, const juce::String& category,
                            const juce::String& author, const juce::String& description) const;
    bool excludedFromModified (int paramIndex) const;

    juce::AudioProcessor& processor;
    ApplyFn apply;
    juce::Array<PresetInfo> presets;
    PresetInfo current;
    std::vector<float> loadedValues;
    juce::File userDirOverride;
    std::unique_ptr<juce::PropertiesFile> settings;
};
} // namespace rv
