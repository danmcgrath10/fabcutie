#include "BandClipboard.h"

namespace fabcutie::ui::BandClipboard
{
    namespace
    {
        const juce::Identifier rootType { "FabCutieBands" };
        const juce::Identifier bandType { "Band" };

        using BandParam = params::BandParam;

        // "b07_freq" -> "freq": the clipboard is not tied to a band number.
        juce::String key (BandParam p)
        {
            return params::bandParamId (0, p).fromFirstOccurrenceOf ("_", false, false);
        }

        juce::ValueTree read()
        {
            const auto xml = juce::parseXML (juce::SystemClipboard::getTextFromClipboard());

            if (xml == nullptr || ! xml->hasTagName (rootType.toString()))
                return {};

            return juce::ValueTree::fromXml (*xml);
        }

        void apply (EqModel& model, int band, const juce::ValueTree& source)
        {
            // Everything but "on" first, so the band appears with its final settings.
            for (int i = 0; i < params::numBandParams; ++i)
            {
                const auto p = (BandParam) i;

                if (p == BandParam::enabled)
                    continue;

                if (const auto* value = source.getPropertyPointer (key (p)))
                {
                    const auto& range = model.parameter (band, p).getNormalisableRange();
                    model.set (band, p, range.snapToLegalValue ((float) static_cast<double> (*value)));
                }
            }

            model.set (band, BandParam::enabled, 1.0f);
        }
    }

    void copy (const EqModel& model, const juce::Array<int>& bands)
    {
        juce::ValueTree root (rootType);

        for (auto b : bands)
        {
            juce::ValueTree tree (bandType);

            for (int i = 0; i < params::numBandParams; ++i)
            {
                const auto p = (BandParam) i;
                const auto& param = model.parameter (b, p);
                tree.setProperty (key (p), param.convertFrom0to1 (param.getValue()), nullptr);
            }

            root.appendChild (tree, nullptr);
        }

        if (auto xml = root.createXml())
            juce::SystemClipboard::copyTextToClipboard (xml->toString());
    }

    int count()
    {
        return read().getNumChildren();
    }

    juce::Array<int> pasteAsNew (EqModel& model)
    {
        juce::Array<int> filled;

        for (const auto& source : read())
        {
            const auto band = model.findFreeBand();

            if (band < 0)
                break;

            apply (model, band, source);
            filled.add (band);
        }

        return filled;
    }

    bool pasteOnto (EqModel& model, int band)
    {
        const auto root = read();

        if (root.getNumChildren() == 0)
            return false;

        apply (model, band, root.getChild (0));
        return true;
    }
}
