// Offline checks for the workflow features: typed frequencies, auto gain,
// polarity, undo / redo, A/B compare, factory presets and MIDI learn.

#include <cmath>
#include <cstdio>
#include <string>

#include "Parameters.h"
#include "dsp/AutoGain.h"
#include "dsp/OutputStage.h"
#include "workflow/ABCompare.h"
#include "workflow/History.h"
#include "workflow/MidiLearn.h"
#include "workflow/Presets.h"

using namespace fabcutie;

namespace
{
    int failures = 0;

    void check (bool ok, const std::string& what)
    {
        if (! ok)
        {
            ++failures;
            std::printf ("FAIL: %s\n", what.c_str());
        }
    }

    bool near (double value, double expected, double tolerance)
    {
        return std::abs (value - expected) <= tolerance;
    }

    std::string fmt (double v)
    {
        char text[32];
        std::snprintf (text, sizeof (text), "%.3f", v);
        return text;
    }

    // Just enough of a plugin to own the real parameter layout.
    class TestProcessor final : public juce::AudioProcessor
    {
    public:
        TestProcessor() : state (*this, nullptr, "FabCutieState", params::createLayout()) {}

        juce::AudioProcessorValueTreeState state;

        const juce::String getName() const override { return "Test"; }
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}
    };

    float plain (TestProcessor& p, const juce::String& id)
    {
        return p.state.getRawParameterValue (id)->load();
    }

    // A UI-style edit: one gesture.
    void edit (TestProcessor& p, const juce::String& id, float value)
    {
        auto* param = p.state.getParameter (id);
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (value));
        param->endChangeGesture();
    }

    void testParseFrequency()
    {
        check (near (params::parseFrequency ("1000"), 1000.0, 1.0e-3), "plain Hz");
        check (near (params::parseFrequency ("2.5k"), 2500.0, 1.0e-3), "2.5k");
        check (near (params::parseFrequency ("1.20 kHz"), 1200.0, 1.0e-3), "1.20 kHz");
        check (near (params::parseFrequency ("A4"), 440.0, 1.0e-3), "A4 = 440 Hz");
        check (near (params::parseFrequency ("c4"), 261.626, 1.0e-2), "c4 = middle C");
        check (near (params::parseFrequency ("Bb2"), 116.541, 1.0e-2), "Bb2");
        check (near (params::parseFrequency ("F#-1"), 11.562, 1.0e-2), "F#-1");
    }

    void testAutoGain()
    {
        constexpr double sr = 48000.0;
        dsp::AutoGain::Bands bands {};

        check (near (dsp::AutoGain::compute (bands, sr), 0.0, 1.0e-6), "no bands, no offset");

        bands[0].enabled = true;
        bands[0].type = dsp::FilterType::lowCut;
        bands[0].frequency = 80.0f;
        check (near (dsp::AutoGain::compute (bands, sr), 0.0, 1.0e-6), "cuts are not compensated");

        bands[0].type = dsp::FilterType::highShelf;
        bands[0].frequency = 1000.0f;
        bands[0].gainDb = 6.0f;
        const auto boost = dsp::AutoGain::compute (bands, sr);
        check (boost < -2.0f && boost > -6.0f, "a +6 dB shelf over half the range is offset by 2-6 dB, got " + fmt (boost));

        bands[0].gainDb = -6.0f;
        const auto cut = dsp::AutoGain::compute (bands, sr);
        check (cut > 1.0f && cut < 6.0f, "a -6 dB shelf is made up, got " + fmt (cut));

        bands[0].gainDb = 6.0f;
        bands[0].placement = dsp::Placement::side;
        const auto side = dsp::AutoGain::compute (bands, sr);
        check (side < 0.0f && side > boost, "a side-only shelf counts less, got " + fmt (side));

        dsp::AutoGain cached;
        bands[0].placement = dsp::Placement::stereo;
        const auto first = cached.update (bands, sr);
        check (near (first, boost, 1.0e-6), "update matches compute");
        bands[0].gainDb = 0.0f;
        check (near (cached.update (bands, sr), 0.0, 0.01), "update follows a change");
    }

    void testPolarity()
    {
        dsp::OutputStage stage;
        stage.setGainDecibels (0.0f, false, true);
        stage.prepare ({ 48000.0, 512, 1 });

        juce::AudioBuffer<float> buffer (1, 512);
        buffer.clear();
        for (int i = 0; i < 512; ++i)
            buffer.setSample (0, i, 0.5f);

        stage.process (buffer);
        check (near (buffer.getSample (0, 511), -0.5, 1.0e-6), "inverted output");

        // Flipping back ramps through zero instead of jumping.
        stage.setGainDecibels (0.0f, false, false);
        for (int i = 0; i < 512; ++i)
            buffer.setSample (0, i, 0.5f);

        stage.process (buffer);
        check (near (buffer.getSample (0, 0), -0.5, 0.01), "polarity flip starts where it was");
        check (buffer.getSample (0, 511) > -0.5f && buffer.getSample (0, 511) < 0.5f, "polarity flip ramps");
    }

    void testHistory()
    {
        TestProcessor p;
        workflow::ParameterSet set (p);
        workflow::History history (set);

        const auto gain = params::bandParamId (0, params::BandParam::gain);

        check (! history.canUndo(), "nothing to undo at first");

        edit (p, gain, 4.0f);
        history.flush();
        edit (p, gain, 8.0f);
        history.flush();

        check (history.canUndo(), "edits can be undone");
        history.undo();
        check (near (plain (p, gain), 4.0, 0.01), "undo goes back one step");
        history.undo();
        check (near (plain (p, gain), 0.0, 0.01), "undo goes back to the start");
        check (! history.canUndo(), "nothing more to undo");

        history.redo();
        check (near (plain (p, gain), 4.0, 0.01), "redo");

        // A new edit drops what could be redone.
        edit (p, gain, -3.0f);
        history.flush();
        check (! history.canRedo(), "new edit clears redo");

        // Automation (no gesture) is not a step.
        p.state.getParameter (gain)->setValueNotifyingHost (p.state.getParameter (gain)->convertTo0to1 (10.0f));
        history.flush();
        history.undo();
        check (near (plain (p, gain), 4.0, 0.01), "automation is not recorded, got " + fmt (plain (p, gain)));

        // Bypass is outside the history.
        check (set.indexOf (params::id::bypass) < 0, "bypass is not part of the settings");
    }

    void testABCompare()
    {
        TestProcessor p;
        workflow::ParameterSet set (p);
        workflow::History history (set);
        workflow::ABCompare ab (set, history);

        const auto freq = params::bandParamId (2, params::BandParam::frequency);

        edit (p, freq, 500.0f);
        ab.select (1);
        check (near (plain (p, freq), 500.0, 0.5), "B starts as a copy of A");

        edit (p, freq, 2000.0f);
        ab.select (0);
        check (near (plain (p, freq), 500.0, 0.5), "back to A");
        ab.select (1);
        check (near (plain (p, freq), 2000.0, 1.0), "back to B");

        history.undo();
        check (near (plain (p, freq), 500.0, 0.5), "switching is undoable");
        history.redo();

        // The inactive slot survives a save and load.
        ab.select (0);
        const auto tree = ab.toTree();
        workflow::ABCompare restored (set, history);
        restored.fromTree (tree);
        check (restored.getActive() == 0, "active slot restored");
        restored.select (1);
        check (near (plain (p, freq), 2000.0, 1.0), "inactive slot restored, got " + fmt (plain (p, freq)));

        ab.select (0);
        edit (p, freq, 500.0f);
        ab.copyToOther();
        ab.select (1);
        check (near (plain (p, freq), 500.0, 0.5), "copy to other slot");
    }

    void testFactoryPresets()
    {
        TestProcessor p;
        workflow::ParameterSet set (p);
        workflow::History history (set);
        workflow::Presets presets (set, history);

        int lowCut = -1;
        for (int i = 0; i < presets.getNumFactory(); ++i)
            if (presets.getEntries()[(size_t) i].name == "Low Cut 80 Hz")
                lowCut = i;

        check (lowCut >= 0, "factory preset exists");
        check (presets.load (lowCut), "factory preset loads");
        check (plain (p, params::bandParamId (0, params::BandParam::enabled)) > 0.5f, "preset band on");
        check (near (plain (p, params::bandParamId (0, params::BandParam::frequency)), 80.0, 0.1), "preset frequency");
        check (near (plain (p, params::bandParamId (0, params::BandParam::type)), (float) dsp::FilterType::lowCut, 0.01), "preset type");
        check (presets.getCurrentName() == "Low Cut 80 Hz", "current preset name");

        history.undo();
        check (plain (p, params::bandParamId (0, params::BandParam::enabled)) < 0.5f, "preset load is undoable");

        // Every factory preset loads and only uses parameters that exist.
        for (int i = 0; i < presets.getNumFactory(); ++i)
            check (presets.load (i), "factory preset " + presets.getEntries()[(size_t) i].name.toStdString());

        presets.load (0);
        check (plain (p, params::bandParamId (0, params::BandParam::enabled)) < 0.5f, "Default switches bands off");
    }

    void testMidiLearn()
    {
        TestProcessor p;
        workflow::ParameterSet set (p);
        workflow::MidiLearn midi (set);

        const auto gainIndex = set.indexOf (params::id::outputGain);
        midi.learn (gainIndex);

        juce::MidiBuffer buffer;
        buffer.addEvent (juce::MidiMessage::controllerEvent (1, 21, 127), 0);
        midi.process (buffer);

        check (midi.getLearning() < 0, "learning stops after a controller moves");
        check (midi.controllerFor (gainIndex) == 21, "CC 21 learned");
        check (near (plain (p, params::id::outputGain), params::range::gainMaxDb, 0.01), "CC sets the value");

        buffer.clear();
        buffer.addEvent (juce::MidiMessage::controllerEvent (5, 21, 0), 0);
        midi.process (buffer);
        check (near (plain (p, params::id::outputGain), params::range::gainMinDb, 0.01), "any channel");

        // Learning another CC for the same parameter replaces the old one.
        midi.learn (gainIndex);
        buffer.clear();
        buffer.addEvent (juce::MidiMessage::controllerEvent (1, 7, 64), 0);
        midi.process (buffer);
        check (midi.controllerFor (gainIndex) == 7, "relearn");

        const auto tree = midi.toTree();
        workflow::MidiLearn restored (set);
        restored.fromTree (tree);
        check (restored.controllerFor (gainIndex) == 7, "MIDI map restored");

        restored.forget (gainIndex);
        check (restored.controllerFor (gainIndex) < 0 && ! restored.hasAssignments(), "forget");
    }
}

int main()
{
    const juce::ScopedJuceInitialiser_GUI juce;

    testParseFrequency();
    testAutoGain();
    testPolarity();
    testHistory();
    testABCompare();
    testFactoryPresets();
    testMidiLearn();

    if (failures == 0)
        std::printf ("All workflow tests passed.\n");

    return failures == 0 ? 0 : 1;
}
