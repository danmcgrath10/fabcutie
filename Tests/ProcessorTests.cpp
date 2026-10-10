// Checks on the whole plug-in, as a host drives it: bad input (NaN,
// infinity), blocks bigger than announced, switching the spectral stage
// during playback, restoring older or off-thread state, the host bypass,
// and opening and closing the editor.

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <thread>

#include "PluginProcessor.h"
#include "Parameters.h"

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr double pi = 3.14159265358979323846;

    int failures = 0;

    void check (bool ok, const std::string& what)
    {
        std::printf ("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (! ok)
            ++failures;
    }

    using fabcutie::params::BandParam;

    void set (FabCutieAudioProcessor& p, const juce::String& id, float plain)
    {
        auto* parameter = p.getState().getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
    }

    void setBand (FabCutieAudioProcessor& p, BandParam param, float plain)
    {
        set (p, fabcutie::params::bandParamId (0, param), plain);
    }

    float get (FabCutieAudioProcessor& p, const juce::String& id)
    {
        return p.getState().getRawParameterValue (id)->load();
    }

    // A stereo processor with a sidechain, prepared, with one +6 dB bell at 1 kHz.
    struct Rig
    {
        explicit Rig (int phaseMode = 0)
        {
            processor.setPlayConfigDetails (4, 2, sampleRate, blockSize);
            set (processor, fabcutie::params::id::phaseMode, (float) phaseMode);
            setBand (processor, BandParam::enabled, 1.0f);
            setBand (processor, BandParam::frequency, 1000.0f);
            setBand (processor, BandParam::gain, 6.0f);
            setBand (processor, BandParam::q, 1.0f);
            processor.prepareToPlay (sampleRate, blockSize);
        }

        // A 0.25 sine at 1 kHz in, processed; the buffer holds the output.
        juce::AudioBuffer<float>& run (int numSamples = blockSize)
        {
            buffer.setSize (4, numSamples, false, false, true);

            for (int i = 0; i < numSamples; ++i, phase += 2.0 * pi * 1000.0 / sampleRate)
                for (int c = 0; c < 4; ++c)
                    buffer.setSample (c, i, c < 2 ? 0.25f * (float) std::sin (phase) : 0.0f);

            processor.processBlock (buffer, midi);
            return buffer;
        }

        FabCutieAudioProcessor processor;
        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midi;
        double phase = 0.0;
    };

    bool allFinite (const juce::AudioBuffer<float>& b)
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! fabcutie::dsp::isFiniteSample (b.getSample (c, i)))
                    return false;

        return true;
    }

    float peak (const juce::AudioBuffer<float>& b)
    {
        auto p = 0.0f;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                p = std::max (p, std::abs (b.getSample (c, i)));
        return p;
    }

    void testBadInput()
    {
        const char* modes[] = { "zero latency", "natural", "linear" };

        for (int mode = 0; mode < 3; ++mode)
        {
            for (const auto bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
            {
                Rig rig (mode);

                for (int k = 0; k < 20; ++k)
                    rig.run();

                rig.buffer.setSample (0, 100, bad);
                rig.processor.processBlock (rig.buffer, rig.midi);
                const auto sameBlock = allFinite (rig.buffer);

                for (int k = 0; k < 40; ++k)
                    rig.run();

                const auto name = std::string (modes[mode]) + (std::isnan (bad) ? ", NaN" : ", inf");
                check (sameBlock, name + ": the bad block's output is finite");
                check (allFinite (rig.buffer) && peak (rig.buffer) > 0.45f && peak (rig.buffer) < 0.55f,
                       name + ": the EQ keeps working afterwards");
            }
        }
    }

    void testBadSidechain()
    {
        Rig rig;
        setBand (rig.processor, BandParam::dynamic, 1.0f);
        setBand (rig.processor, BandParam::detectorSource, 1.0f); // external
        rig.processor.getBus (true, 1)->enable (true);

        rig.run();
        rig.buffer.setSample (2, 10, std::numeric_limits<float>::quiet_NaN());
        rig.processor.processBlock (rig.buffer, rig.midi);

        for (int k = 0; k < 20; ++k)
            rig.run();

        check (allFinite (rig.buffer) && peak (rig.buffer) > 0.2f, "NaN in the sidechain doesn't stick in a dynamic band");
    }

    void testOversizedBlock()
    {
        Rig rig;
        rig.run (8192);
        check (allFinite (rig.buffer) && peak (rig.buffer) > 0.45f, "a block 16x bigger than prepared is processed");
    }

    // Largest change between neighbouring samples; a clean 0.5 sine at
    // 1 kHz moves at most about 0.065.
    float biggestStep (Rig& rig, int blocks, float& last)
    {
        auto step = 0.0f;

        for (int k = 0; k < blocks; ++k)
        {
            rig.run();

            for (int i = 0; i < blockSize; ++i)
            {
                const auto x = rig.buffer.getSample (0, i);
                step = std::max (step, std::abs (x - last));
                last = x;
            }
        }

        return step;
    }

    void testSpectralSwitch()
    {
        Rig rig;
        setBand (rig.processor, BandParam::dynamic, 1.0f);
        setBand (rig.processor, BandParam::threshold, 0.0f); // never triggers: the stage passes audio unchanged

        auto last = 0.0f;
        biggestStep (rig, 20, last);
        const auto tailOff = rig.processor.getTailLengthSeconds();

        setBand (rig.processor, BandParam::spectral, 1.0f);
        check (biggestStep (rig, 20, last) < 0.1f, "switching the spectral stage in mid-playback doesn't click");
        check (rig.processor.getTailLengthSeconds() > tailOff, "the spectral stage runs once a band is spectral");

        // With the switch set, the stage keeps running whatever the band does.
        setBand (rig.processor, BandParam::enabled, 0.0f);
        const auto stepOff = biggestStep (rig, 20, last);
        setBand (rig.processor, BandParam::enabled, 1.0f);
        const auto stepOn = biggestStep (rig, 20, last);
        check (stepOff < 0.1f && stepOn < 0.1f, "turning a spectral band off and on doesn't switch the stage or click");
        check (rig.processor.getTailLengthSeconds() > tailOff, "the stage is still running after the band went off and on");

        setBand (rig.processor, BandParam::spectral, 0.0f);
        check (biggestStep (rig, 20, last) < 0.1f, "switching the spectral stage out mid-playback doesn't click");
    }

    juce::MemoryBlock saveState (FabCutieAudioProcessor& p)
    {
        juce::MemoryBlock data;
        p.getStateInformation (data);
        return data;
    }

    void testOldStateTakesDefaults()
    {
        // A session saved before the character parameter existed.
        FabCutieAudioProcessor fresh;
        auto xml = juce::AudioProcessor::getXmlFromBinary (saveState (fresh).getData(), (int) saveState (fresh).getSize());
        auto tree = juce::ValueTree::fromXml (*xml);
        tree.removeChild (tree.getChildWithProperty ("id", fabcutie::params::id::character), nullptr);

        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), old);

        FabCutieAudioProcessor p;
        set (p, fabcutie::params::id::character, 2.0f);
        p.setStateInformation (old.getData(), (int) old.getSize());

        check (juce::approximatelyEqual (get (p, fabcutie::params::id::character), 0.0f),
               "a parameter missing from older state takes its default");
    }

    void testRestoreOffMessageThread()
    {
        FabCutieAudioProcessor source;
        set (source, fabcutie::params::id::outputGain, -3.0f);
        auto xml = juce::AudioProcessor::getXmlFromBinary (saveState (source).getData(), (int) saveState (source).getSize());
        auto tree = juce::ValueTree::fromXml (*xml);
        auto presets = tree.getChildWithName ("Preset");
        presets.setProperty ("name", "Kick Tight", nullptr);

        juce::MemoryBlock data;
        juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), data);

        FabCutieAudioProcessor p;
        std::thread loader ([&] { p.setStateInformation (data.getData(), (int) data.getSize()); });
        loader.join();

        check (juce::approximatelyEqual (get (p, fabcutie::params::id::outputGain), -3.0f),
               "state restored off the message thread sets the parameters");

        // Saved again before the message thread caught up: nothing is lost.
        auto again = juce::AudioProcessor::getXmlFromBinary (saveState (p).getData(), (int) saveState (p).getSize());
        const auto name = juce::ValueTree::fromXml (*again).getChildWithName ("Preset").getProperty ("name").toString();
        check (name == "Kick Tight", "a pending off-thread restore is still what gets saved");
    }

    void testHostBypass()
    {
        FabCutieAudioProcessor p;
        auto* bypass = p.getBypassParameter();
        check (bypass != nullptr && bypass == p.getState().getParameter (fabcutie::params::id::bypass),
               "the host's bypass is the Bypass button");
    }

    void testEditor()
    {
        Rig rig;
        rig.run();

        for (int k = 0; k < 3; ++k)
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (rig.processor.createEditor());
            editor->setSize (1200, 700);
            rig.run();
        }

        check (true, "the editor opens and closes");
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    testBadInput();
    testBadSidechain();
    testOversizedBlock();
    testSpectralSwitch();
    testOldStateTakesDefaults();
    testRestoreOffMessageThread();
    testHostBypass();
    testEditor();

    std::printf ("\n%s\n", failures == 0 ? "All processor tests passed." : "Some processor tests FAILED.");
    return failures == 0 ? 0 : 1;
}
