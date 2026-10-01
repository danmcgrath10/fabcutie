#include "ValueEntry.h"

#include <optional>
#include "Theme.h"

namespace fabcutie::ui::ValueEntry
{
    using BandParam = params::BandParam;

    void show (juce::Component& parent, EqModel& model, int band)
    {
        if (band < 0 || band >= dsp::maxBands)
            return;

        const auto settings = model.getBand (band);
        const auto text = [&] (BandParam p) { return model.parameter (band, p).getCurrentValueAsText(); };

        auto* window = new juce::AlertWindow ("Band " + juce::String (band + 1), {}, juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("freq", text (BandParam::frequency), "Frequency");

        if (EqModel::usesGain (settings.type))
            window->addTextEditor ("gain", juce::String (settings.gainDb, 2), "Gain (dB)");

        if (EqModel::usesQ (settings))
            window->addTextEditor ("q", text (BandParam::q), "Q");

        window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        // Inside the plugin window rather than a separate desktop window, which
        // some hosts put behind their own.
        parent.addAndMakeVisible (window);
        window->setCentrePosition (parent.getLocalBounds().getCentre());

        if (auto* editor = window->getTextEditor ("freq"))
        {
            editor->selectAll();
            editor->grabKeyboardFocus();
        }

        juce::Component::SafePointer<juce::AlertWindow> safeWindow (window);

        window->enterModalState (true, juce::ModalCallbackFunction::create ([safeWindow, &model, band] (int result)
        {
            if (result != 1 || safeWindow == nullptr)
                return;

            const auto read = [&] (const juce::String& name) -> std::optional<juce::String>
            {
                if (auto* editor = safeWindow->getTextEditor (name))
                    if (const auto value = editor->getText().trim(); value.isNotEmpty())
                        return value;

                return std::nullopt;
            };

            if (const auto freq = read ("freq"))
            {
                const auto hz = params::parseFrequency (*freq);
                if (hz > 0.0f)
                    model.set (band, BandParam::frequency, juce::jlimit (params::range::freqMinHz, params::range::freqMaxHz, hz));
            }

            if (const auto gain = read ("gain"))
                model.set (band, BandParam::gain, juce::jlimit (-params::range::bandGainMaxDb, params::range::bandGainMaxDb,
                                                                 gain->getFloatValue()));

            if (const auto q = read ("q"))
                if (const auto value = q->getFloatValue(); value > 0.0f)
                    model.set (band, BandParam::q, juce::jlimit (params::range::qMin, params::range::qMax, value));
        }), true);
    }
}
