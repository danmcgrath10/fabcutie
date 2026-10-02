#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "EqModel.h"
#include "dsp/Assist.h"
#include "dsp/EditorLink.h"
#include "dsp/SpectrumMatch.h"

namespace fabcutie::ui
{
    // Assist: two listening tools that suggest dynamic bands.
    //
    // Resonances listens to the input and finds narrow peaks that ring out
    // of the spectrum around them, then adds a narrow dynamic cut on each.
    //
    // Unmask listens to the input and to its key, the track that should
    // cut through this one: another FabCutie instance in the session (no
    // routing needed) or the host's sidechain. It finds where the two
    // compete and adds dynamic bands that duck this track there only while
    // the key plays (External bands, fed by the key).
    class AssistPanel final : public juce::Component,
                              private juce::Timer
    {
    public:
        // The instance's key: the other instances it can choose from, and
        // the one chosen (0 = the host's sidechain).
        struct KeySource
        {
            struct Choice { std::uint64_t id = 0; juce::String name; };

            std::function<std::vector<Choice>()> choices;
            std::function<std::uint64_t()> get;
            std::function<void (std::uint64_t)> set;
        };

        AssistPanel (EqModel&, dsp::EditorLink&, KeySource, std::function<double()> sampleRateSource);
        ~AssistPanel() override;

        static constexpr int preferredWidth  = 330;
        static constexpr int preferredHeight = 282;

        // Call when instances come and go or are renamed.
        void refreshKeys();

        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override;

    private:
        enum class Mode { resonances, unmask };

        void timerCallback() override;
        void setMode (Mode);
        void setLearning (bool);
        void analyse();
        void apply();
        void reset();
        double getSampleRate() const;
        void chooseKey();

        EqModel& model;
        dsp::EditorLink& link;
        KeySource keys;
        std::function<double()> sampleRateSource;

        const std::vector<double> fineHz, coarseHz;
        dsp::LongTermSpectrum source, key;
        std::vector<float> scratch;

        Mode mode = Mode::resonances;
        bool learning = false;
        int timerTicks = 0;

        std::vector<double> sourceDb, keyDb; // for the preview, at fineHz (resonances) or coarseHz (unmask)
        std::vector<dsp::assist::Suggestion> suggestions;

        juce::TextButton resonancesTab { "Resonances" }, unmaskTab { "Unmask" };
        juce::ComboBox keyBox;
        std::vector<std::uint64_t> keyIds; // per keyBox item, from index 0
        juce::TextButton learnButton { "Learn" }, resetButton { "Reset" }, applyButton { "Apply" };
        juce::Slider amount { juce::Slider::LinearBar, juce::Slider::TextBoxAbove };
        juce::Slider bandCount { juce::Slider::LinearBar, juce::Slider::TextBoxAbove };

        juce::Rectangle<int> previewArea, statusArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AssistPanel)
    };
}
