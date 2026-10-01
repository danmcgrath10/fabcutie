#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "FilterDesign.h"

namespace fabcutie::dsp
{
    // One EQ band running on up to two channels (left/right or mid/side,
    // chosen by EqEngine). Frequency, gain and Q glide to new values;
    // changes to type, slope, placement or on/off fade the band's effect out,
    // swap the filter and fade it back in, so nothing clicks.
    class EqBand
    {
    public:
        static constexpr int maxChannels = 2;

        void prepare (double newSampleRate)
        {
            sampleRate = newSampleRate;

            frequency.reset (sampleRate, glideSeconds);
            gainDb.reset (sampleRate, glideSeconds);
            q.reset (sampleRate, glideSeconds);
            fadeStep = 1.0f / (float) (fadeSeconds * sampleRate);

            current = target;
            fade = current.enabled ? 1.0f : 0.0f;
            snapToTarget();
        }

        void setTarget (const BandSettings& settings) noexcept
        {
            target = settings;

            if (current.sameStructure (target))
            {
                frequency.setTargetValue (target.frequency);
                gainDb.setTargetValue (target.gainDb);
                q.setTargetValue (target.q);
            }
        }

        // Call once per block before process(). Swaps in a pending structure
        // once the band has faded out. Returns the placement to process with.
        Placement beginBlock() noexcept
        {
            if (! current.sameStructure (target) && fade <= 0.0f)
            {
                current = target;
                snapToTarget();
            }

            return current.placement;
        }

        // True when the band currently changes the signal at all.
        bool isActive() const noexcept
        {
            return fade > 0.0f || (current.enabled && current.sameStructure (target));
        }

        // Processes the given channels in place. stateSlots says which
        // filter memory each channel uses (0 = left/mid, 1 = right/side), so
        // a band switched between e.g. left and right never mixes them up.
        void process (float* const* channels, const int* stateSlots, int numChannels, int numSamples) noexcept
        {
            const auto fadeTarget = (current.enabled && current.sameStructure (target)) ? 1.0f : 0.0f;

            for (int start = 0; start < numSamples; start += controlBlockSize)
            {
                const auto n = std::min (controlBlockSize, numSamples - start);

                if (frequency.isSmoothing() || gainDb.isSmoothing() || q.isSmoothing())
                {
                    frequency.skip (n);
                    gainDb.skip (n);
                    q.skip (n);
                    updateDesign();
                }

                std::array<float, controlBlockSize> mix;
                for (int i = 0; i < n; ++i)
                {
                    if (fade < fadeTarget)      fade = std::min (fadeTarget, fade + fadeStep);
                    else if (fade > fadeTarget) fade = std::max (fadeTarget, fade - fadeStep);
                    mix[(size_t) i] = fade;
                }

                for (int c = 0; c < numChannels; ++c)
                {
                    auto* data = channels[c] + start;
                    auto& st = state[(size_t) stateSlots[c]];

                    for (int i = 0; i < n; ++i)
                    {
                        const auto x = (double) data[i];
                        const auto y = tick (st, x);
                        data[i] = (float) (x + (double) mix[(size_t) i] * (y - x));
                    }
                }
            }
        }

        const BandSettings& getCurrentSettings() const noexcept { return current; }

    private:
        static constexpr int controlBlockSize = 16;
        static constexpr double glideSeconds = 0.03;
        static constexpr double fadeSeconds = 0.01;

        struct SectionState { double ic1 = 0.0, ic2 = 0.0; };

        struct ChannelState
        {
            std::array<SectionState, BandDesign::maxSections> sections {};
            double onePole = 0.0;
        };

        // Per-section coefficients precomputed from the design.
        struct Runtime
        {
            double a1 = 1.0, a2 = 0.0, a3 = 0.0;
            double m0 = 1.0, m1 = 0.0, m2 = 0.0;
        };

        double tick (ChannelState& st, double x) const noexcept
        {
            if (design.hasOnePole)
            {
                const auto v = (x - st.onePole) * onePoleG;
                const auto low = v + st.onePole;
                st.onePole = low + v;
                x = design.onePole.highpass ? x - low : low;
            }

            for (int i = 0; i < design.numSections; ++i)
            {
                const auto& r = runtime[(size_t) i];
                auto& s = st.sections[(size_t) i];

                const auto v3 = x - s.ic2;
                const auto v1 = r.a1 * s.ic1 + r.a2 * v3;
                const auto v2 = s.ic2 + r.a2 * s.ic1 + r.a3 * v3;
                s.ic1 = 2.0 * v1 - s.ic1;
                s.ic2 = 2.0 * v2 - s.ic2;

                x = r.m0 * x + r.m1 * v1 + r.m2 * v2;
            }

            return x;
        }

        void snapToTarget() noexcept
        {
            frequency.setCurrentAndTargetValue (target.frequency);
            gainDb.setCurrentAndTargetValue (target.gainDb);
            q.setCurrentAndTargetValue (target.q);
            state = {};
            updateDesign();
        }

        void updateDesign() noexcept
        {
            auto s = current;
            s.frequency = frequency.getCurrentValue();
            s.gainDb = gainDb.getCurrentValue();
            s.q = q.getCurrentValue();

            design = designBand (s, sampleRate);

            for (int i = 0; i < design.numSections; ++i)
            {
                const auto& sec = design.sections[(size_t) i];
                auto& r = runtime[(size_t) i];
                r.a1 = 1.0 / (1.0 + sec.g * (sec.g + sec.k));
                r.a2 = sec.g * r.a1;
                r.a3 = sec.g * r.a2;
                r.m0 = sec.m0;
                r.m1 = sec.m1;
                r.m2 = sec.m2;
            }

            onePoleG = design.onePole.g / (1.0 + design.onePole.g);
        }

        double sampleRate = 44100.0;

        BandSettings target, current;

        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> frequency { 1000.0f }, q { 1.0f };
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gainDb { 0.0f };

        float fade = 0.0f, fadeStep = 0.01f;

        BandDesign design;
        std::array<Runtime, BandDesign::maxSections> runtime {};
        double onePoleG = 0.0;

        std::array<ChannelState, maxChannels> state {};
    };
}
