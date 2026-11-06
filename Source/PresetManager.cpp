// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PresetManager.h"

namespace rv
{
namespace
{
juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName     = "Resonance Verb";
    o.folderName          = "Resonance Verb";
    o.filenameSuffix      = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    return o;
}

juce::RangedAudioParameter* ranged (juce::AudioProcessorParameter* p) { return dynamic_cast<juce::RangedAudioParameter*> (p); }
} // namespace

PresetManager::PresetManager (juce::AudioProcessor& p, ApplyFn fn)
    : processor (p), apply (std::move (fn)), settings (std::make_unique<juce::PropertiesFile> (settingsOptions()))
{
    rescan();
    current = presets.isEmpty() ? PresetInfo {} : presets.getReference (0);   // "Init"
    loadedValues = valuesFor (current);
}

// ---------------------------------------------------------------- library
juce::File PresetManager::getUserDirectory() const
{
    if (userDirOverride != juce::File()) return userDirOverride;
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Resonance Verb").getChildFile ("Presets");
}

void PresetManager::setUserDirectory (const juce::File& dir)
{
    userDirOverride = dir;
    rescan();
}

void PresetManager::setSettingsFile (const juce::File& f)
{
    settings = std::make_unique<juce::PropertiesFile> (f, settingsOptions());
}

void PresetManager::rescan()
{
    presets.clearQuick();
    const auto& factory = factoryPresets();
    for (int i = 0; i < (int) factory.size(); ++i)
    {
        PresetInfo info;
        info.name = factory[(size_t) i].name;
        info.category = factory[(size_t) i].category;
        info.author = "Resonance Verb";
        info.description = factory[(size_t) i].description;
        info.isFactory = true;
        info.factoryIndex = i;
        presets.add (info);
    }

    juce::Array<PresetInfo> user;
    for (const auto& entry : juce::RangedDirectoryIterator (getUserDirectory(), true, juce::String ("*") + fileExtension))
        if (auto info = readHeader (entry.getFile()))
            user.add (*info);

    struct ByName { static int compareElements (const PresetInfo& a, const PresetInfo& b) { return a.name.compareNatural (b.name); } };
    ByName sorter;
    user.sort (sorter);
    presets.addArray (user);
}

juce::StringArray PresetManager::getCategories() const
{
    juce::StringArray cats;
    for (const auto& p : presets)
        if (p.category.isNotEmpty()) cats.addIfNotAlreadyThere (p.category);
    cats.sortNatural();
    return cats;
}

std::optional<PresetInfo> PresetManager::readHeader (const juce::File& f)
{
    const auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName ("ResonanceVerbPreset")) return std::nullopt;
    PresetInfo info;
    info.name = xml->getStringAttribute ("name", f.getFileNameWithoutExtension());
    info.category = xml->getStringAttribute ("category", "User");
    info.author = xml->getStringAttribute ("author");
    info.description = xml->getStringAttribute ("description");
    info.file = f;
    return info;
}

// ---------------------------------------------------------------- values
std::vector<float> PresetManager::valuesFor (const PresetInfo& info) const
{
    const auto& params = processor.getParameters();
    std::vector<float> values ((size_t) params.size());
    for (int i = 0; i < params.size(); ++i)
        values[(size_t) i] = params[i]->getDefaultValue();

    auto setReal = [&] (const juce::String& id, float real)
    {
        for (int i = 0; i < params.size(); ++i)
            if (auto* r = ranged (params[i]); r != nullptr && r->getParameterID() == id)
                values[(size_t) i] = r->convertTo0to1 (real);
    };

    if (info.isFactory && juce::isPositiveAndBelow (info.factoryIndex, (int) factoryPresets().size()))
    {
        for (const auto& [id, value] : factoryPresets()[(size_t) info.factoryIndex].values)
            setReal (id, value);
    }
    else if (! info.isFactory)
    {
        if (const auto xml = juce::XmlDocument::parse (info.file))
            for (auto* p : xml->getChildWithTagNameIterator ("PARAM"))
                setReal (p->getStringAttribute ("id"), (float) p->getDoubleAttribute ("value"));
    }
    return values;
}

std::vector<float> PresetManager::currentValues() const
{
    const auto& params = processor.getParameters();
    std::vector<float> values ((size_t) params.size());
    for (int i = 0; i < params.size(); ++i)
        values[(size_t) i] = params[i]->getValue();
    return values;
}

bool PresetManager::excludedFromModified (int index) const
{
    // Performance controls, not sound design: toggling them doesn't "modify" a preset.
    if (auto* r = ranged (processor.getParameters()[index]))
        return r->getParameterID() == "freeze" || r->getParameterID() == "pedal";
    return false;
}

bool PresetManager::isModified() const
{
    const auto now = currentValues();
    if (now.size() != loadedValues.size()) return true;
    for (size_t i = 0; i < now.size(); ++i)
        if (! excludedFromModified ((int) i) && std::abs (now[i] - loadedValues[i]) > 1.0e-4f)
            return true;
    return false;
}

// ---------------------------------------------------------------- current preset
bool PresetManager::load (const PresetInfo& info)
{
    if (! info.isValid()) return false;
    const auto values = valuesFor (info);
    apply ("Load preset \"" + info.name + "\"", values);
    current = info;
    loadedValues = values;
    return true;
}

bool PresetManager::loadFactory (int index)
{
    for (const auto& p : presets)
        if (p.isFactory && p.factoryIndex == index)
            return load (p);
    return false;
}

void PresetManager::loadNext (int direction)
{
    if (presets.isEmpty()) return;
    int idx = 0;
    for (int i = 0; i < presets.size(); ++i)
        if (presets.getReference (i).getKey() == current.getKey()) { idx = i; break; }
    idx = (idx + direction + presets.size()) % presets.size();
    load (presets.getReference (idx));
}

void PresetManager::restoreCurrent (const juce::String& key)
{
    for (const auto& p : presets)
        if (p.getKey() == key)
        {
            current = p;
            loadedValues = valuesFor (p);
            return;
        }
}

// ---------------------------------------------------------------- editing
juce::String PresetManager::sanitiseFileName (const juce::String& name)
{
    auto s = juce::File::createLegalFileName (name.trim());
    return s.isEmpty() ? juce::String ("Untitled") : s;
}

bool PresetManager::userPresetExists (const juce::String& name) const
{
    return getUserDirectory().getChildFile (sanitiseFileName (name) + fileExtension).existsAsFile();
}

juce::Result PresetManager::writeFile (const juce::File& f, const juce::String& name, const juce::String& category,
                                       const juce::String& author, const juce::String& description) const
{
    juce::XmlElement xml ("ResonanceVerbPreset");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("name", name);
    xml.setAttribute ("category", category);
    xml.setAttribute ("author", author);
    xml.setAttribute ("description", description);
    for (auto* p : processor.getParameters())
        if (auto* r = ranged (p))
        {
            auto* e = xml.createNewChildElement ("PARAM");
            e->setAttribute ("id", r->getParameterID());
            e->setAttribute ("value", (double) r->convertFrom0to1 (r->getValue()));
        }

    if (auto dir = f.getParentDirectory(); ! dir.isDirectory())
        if (const auto r = dir.createDirectory(); r.failed())
            return juce::Result::fail ("Couldn't create the preset folder: " + r.getErrorMessage());
    if (! xml.writeTo (f))
        return juce::Result::fail ("Couldn't write " + f.getFullPathName());
    return juce::Result::ok();
}

juce::Result PresetManager::save (const juce::String& nameIn, const juce::String& category, const juce::String& author,
                                  const juce::String& description, bool overwrite)
{
    const auto name = nameIn.trim();
    if (name.isEmpty()) return juce::Result::fail ("Please enter a preset name.");
    const auto f = getUserDirectory().getChildFile (sanitiseFileName (name) + fileExtension);
    if (f.existsAsFile() && ! overwrite)
        return juce::Result::fail ("exists");

    if (const auto r = writeFile (f, name, category.trim().isEmpty() ? juce::String ("User") : category.trim(), author.trim(), description.trim()); r.failed())
        return r;

    rescan();
    restoreCurrent (f.getFullPathName());
    loadedValues = currentValues();
    return juce::Result::ok();
}

juce::Result PresetManager::rename (const PresetInfo& info, const juce::String& newNameIn)
{
    const auto newName = newNameIn.trim();
    if (info.isFactory) return juce::Result::fail ("Factory presets can't be renamed.");
    if (newName.isEmpty()) return juce::Result::fail ("Please enter a preset name.");
    const auto target = getUserDirectory().getChildFile (sanitiseFileName (newName) + fileExtension);
    if (target.existsAsFile() && target != info.file) return juce::Result::fail ("A preset with that name already exists.");

    auto xml = juce::XmlDocument::parse (info.file);
    if (xml == nullptr) return juce::Result::fail ("Couldn't read " + info.file.getFileName());
    xml->setAttribute ("name", newName);
    if (! xml->writeTo (target)) return juce::Result::fail ("Couldn't write " + target.getFullPathName());
    if (target != info.file) info.file.deleteFile();   // replaced by the renamed copy

    const bool wasCurrent = current.getKey() == info.getKey();
    rescan();
    if (wasCurrent) { const auto keep = loadedValues; restoreCurrent (target.getFullPathName()); loadedValues = keep; }
    return juce::Result::ok();
}

juce::Result PresetManager::remove (const PresetInfo& info)
{
    if (info.isFactory) return juce::Result::fail ("Factory presets can't be deleted.");
    if (! info.file.moveToTrash()) return juce::Result::fail ("Couldn't move " + info.file.getFileName() + " to the Trash.");
    setFavourite (info, false);
    rescan();
    return juce::Result::ok();
}

juce::Result PresetManager::import (const juce::File& source)
{
    auto header = readHeader (source);
    if (! header) return juce::Result::fail (source.getFileName() + " isn't a Resonance Verb preset.");
    auto target = getUserDirectory().getChildFile (sanitiseFileName (header->name) + fileExtension);
    if (target.existsAsFile()) target = target.getNonexistentSibling (true);
    getUserDirectory().createDirectory();
    if (! source.copyFileTo (target)) return juce::Result::fail ("Couldn't copy into " + target.getFullPathName());
    rescan();
    return juce::Result::ok();
}

// ---------------------------------------------------------------- favourites
bool PresetManager::isFavourite (const PresetInfo& info) const
{
    juce::StringArray favs;
    favs.addLines (settings->getValue ("favourites"));
    return favs.contains (info.getKey());
}

void PresetManager::setFavourite (const PresetInfo& info, bool fav)
{
    juce::StringArray favs;
    favs.addLines (settings->getValue ("favourites"));
    favs.removeEmptyStrings();
    if (fav) favs.addIfNotAlreadyThere (info.getKey());
    else     favs.removeString (info.getKey());
    settings->setValue ("favourites", favs.joinIntoString ("\n"));
    settings->saveIfNeeded();
}
} // namespace rv
