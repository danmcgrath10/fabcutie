#include "Presets.h"
#include "Parameters.h"

namespace fabcutie::workflow
{
    namespace
    {
        using params::BandParam;
        using dsp::FilterType;

        const juce::Identifier presetFileType { "FabCutiePreset" };

        // Plain parameter values switching band `b` on with the given settings.
        std::vector<std::pair<juce::String, float>> band (int b, FilterType type, float hz, float gainDb = 0.0f,
                                                          float q = 1.0f, int slopeIndex = 1)
        {
            return { { params::bandParamId (b, BandParam::enabled), 1.0f },
                     { params::bandParamId (b, BandParam::type), (float) type },
                     { params::bandParamId (b, BandParam::frequency), hz },
                     { params::bandParamId (b, BandParam::gain), gainDb },
                     { params::bandParamId (b, BandParam::q), q },
                     { params::bandParamId (b, BandParam::slope), (float) slopeIndex } };
        }

        std::vector<std::pair<juce::String, float>> join (std::initializer_list<std::vector<std::pair<juce::String, float>>> parts)
        {
            std::vector<std::pair<juce::String, float>> all;
            for (const auto& part : parts)
                all.insert (all.end(), part.begin(), part.end());
            return all;
        }

        bool isValidName (const juce::String& name)
        {
            return name.trim().isNotEmpty() && ! name.containsAnyOf ("/\\:*?\"<>|");
        }
    }

    std::vector<Presets::Factory> Presets::makeFactory()
    {
        // Slope indices: 1 = 12 dB/oct, 3 = 24 dB/oct.
        return {
            { "Default", {} },
            { "Low Cut 80 Hz", band (0, FilterType::lowCut, 80.0f, 0.0f, 1.0f, 3) },
            { "Vocal Presence", join ({ band (0, FilterType::lowCut, 90.0f, 0.0f, 1.0f, 3),
                                        band (1, FilterType::bell, 300.0f, -2.5f, 1.2f),
                                        band (2, FilterType::bell, 3200.0f, 2.5f, 0.9f),
                                        band (3, FilterType::highShelf, 11000.0f, 2.0f, 0.7f) }) },
            { "Kick Punch", join ({ band (0, FilterType::lowCut, 28.0f, 0.0f, 1.0f, 3),
                                    band (1, FilterType::bell, 60.0f, 3.0f, 1.4f),
                                    band (2, FilterType::bell, 380.0f, -4.0f, 1.6f),
                                    band (3, FilterType::bell, 4000.0f, 2.5f, 1.2f) }) },
            { "Mud Cut", band (0, FilterType::bell, 250.0f, -4.0f, 1.0f) },
            { "Air", band (0, FilterType::highShelf, 12000.0f, 3.0f, 0.6f) },
            { "Warm Tilt", band (0, FilterType::tiltShelf, 1000.0f, -2.0f, 0.5f) },
            { "Telephone", join ({ band (0, FilterType::lowCut, 400.0f, 0.0f, 1.0f, 3),
                                   band (1, FilterType::bell, 1500.0f, 4.0f, 0.8f),
                                   band (2, FilterType::highCut, 3400.0f, 0.0f, 1.0f, 3) }) },
        };
    }

    Presets::Presets (const ParameterSet& p, History& h)
        : parameters (p), history (h), factory (makeFactory())
    {
        rescan();
    }

    juce::File Presets::getUserFolder() const
    {
       #if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Presets/FabCutie");
       #else
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("FabCutie/Presets");
       #endif
    }

    void Presets::rescan()
    {
        entries.clear();

        for (const auto& f : factory)
            entries.push_back ({ f.name, true, {} });

        auto files = getUserFolder().findChildFiles (juce::File::findFiles, false, "*" + fileExtension);
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });

        for (const auto& file : files)
            entries.push_back ({ file.getFileNameWithoutExtension(), false, file });

        // Keep pointing at the same preset if it is still there.
        current = -1;
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].name == currentName)
                current = (int) i;
    }

    Snapshot Presets::snapshotFor (const Factory& f) const
    {
        juce::ValueTree tree (presetFileType);
        for (const auto& [id, value] : f.values)
            tree.setProperty (id, value, nullptr);

        return parameters.fromTree (tree, parameters.defaults());
    }

    bool Presets::load (int index)
    {
        if (index < 0 || index >= (int) entries.size())
            return false;

        const auto& entry = entries[(size_t) index];
        Snapshot snapshot;

        if (entry.factory)
        {
            snapshot = snapshotFor (factory[(size_t) index]);
        }
        else
        {
            const auto xml = juce::parseXML (entry.file);

            if (xml == nullptr || ! xml->hasTagName (presetFileType.toString()))
                return false;

            // Parameters the file does not mention (newer than the preset) take their defaults.
            snapshot = parameters.fromTree (juce::ValueTree::fromXml (*xml), parameters.defaults());
        }

        history.applyAsStep (snapshot);
        current = index;
        currentName = entry.name;
        return true;
    }

    void Presets::loadNext (int direction)
    {
        const auto count = (int) entries.size();

        if (count == 0)
            return;

        const auto start = current < 0 ? (direction > 0 ? -1 : 0) : current;
        load (((start + direction) % count + count) % count);
    }

    bool Presets::save (const juce::String& rawName)
    {
        const auto name = rawName.trim();

        if (! isValidName (name))
            return false;

        const auto folder = getUserFolder();

        if (! folder.createDirectory())
            return false;

        auto tree = parameters.toTree (parameters.capture(), presetFileType);
       #ifdef JucePlugin_VersionString
        tree.setProperty ("pluginVersion", JucePlugin_VersionString, nullptr);
       #endif

        const auto xml = tree.createXml();

        if (xml == nullptr || ! xml->writeTo (folder.getChildFile (name + fileExtension)))
            return false;

        currentName = name;
        rescan();
        return true;
    }

    bool Presets::remove (int index)
    {
        if (index < 0 || index >= (int) entries.size() || entries[(size_t) index].factory)
            return false;

        if (! entries[(size_t) index].file.deleteFile())
            return false;

        if (index == current)
            currentName = {};

        rescan();
        return true;
    }

    juce::ValueTree Presets::toTree() const
    {
        juce::ValueTree tree (treeType);
        tree.setProperty ("name", currentName, nullptr);
        return tree;
    }

    void Presets::fromTree (const juce::ValueTree& tree)
    {
        currentName = tree.hasType (treeType) ? tree.getProperty ("name").toString() : juce::String();
        rescan();
    }
}
