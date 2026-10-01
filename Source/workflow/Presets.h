#pragma once

#include "History.h"

namespace fabcutie::workflow
{
    // Factory presets built into the plugin plus user presets saved as files
    // (macOS: ~/Library/Audio/Presets/FabCutie, elsewhere the user app data
    // folder). Loading a preset is one undo step.
    class Presets
    {
    public:
        Presets (const ParameterSet&, History&);

        struct Entry
        {
            juce::String name;
            bool factory = true;
            juce::File file; // user presets only
        };

        // Factory presets first, then user presets sorted by name.
        const std::vector<Entry>& getEntries() const noexcept { return entries; }
        void rescan();

        int getNumFactory() const noexcept { return (int) factory.size(); }

        // The preset last loaded or saved, -1 when none (or after the list changed under it).
        int getCurrent() const noexcept { return current; }
        juce::String getCurrentName() const { return currentName; }

        bool load (int index);
        void loadNext (int direction); // +1 / -1, wrapping round

        // Saves the live settings as a user preset, replacing one of the same name.
        bool save (const juce::String& name);
        bool remove (int index);

        juce::File getUserFolder() const;
        static inline const juce::String fileExtension { ".fcpreset" };

        // The name of the preset in use, kept with the session.
        juce::ValueTree toTree() const;
        void fromTree (const juce::ValueTree&);
        static inline const juce::Identifier treeType { "Preset" };

    private:
        struct Factory
        {
            juce::String name;
            std::vector<std::pair<juce::String, float>> values; // plain values over the defaults
        };

        static std::vector<Factory> makeFactory();
        Snapshot snapshotFor (const Factory&) const;

        const ParameterSet& parameters;
        History& history;
        std::vector<Factory> factory;
        std::vector<Entry> entries;
        int current = -1;
        juce::String currentName;
    };
}
