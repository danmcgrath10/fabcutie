#pragma once

#include <juce_dsp/juce_dsp.h>

namespace fabcutie::dsp
{
    // Order matters: stored in saved sessions through the character parameter.
    enum class CharacterMode
    {
        clean,
        gentle,
        warm
    };

    inline constexpr int numCharacterModes = 3;

    // The two saturation curves. Both have a slope of exactly 1 at zero, so
    // quiet material passes at unity gain and the drawn EQ curve stays true;
    // the colour only shows up as levels rise.
    namespace shaper
    {
        // Gentle: symmetric soft saturation, odd harmonics only.
        inline constexpr float gentleDrive = 0.7f;

        inline float gentle (float x) noexcept
        {
            return std::tanh (gentleDrive * x) / gentleDrive;
        }

        // Warm: the same curve pushed off-centre by a bias, which adds even
        // (mostly second) harmonics and a little more compression. The bias
        // offset is subtracted and the slope renormalised to 1 at zero.
        inline constexpr float warmDrive = 0.8f;
        inline constexpr float warmBias  = 0.2f;

        inline float warm (float x) noexcept
        {
            static const float offset = std::tanh (warmDrive * warmBias);
            static const float slope  = warmDrive * (1.0f - offset * offset);
            return (std::tanh (warmDrive * (x + warmBias)) - offset) / slope;
        }
    }

    // Global analog-style colour after the EQ bands. Clean leaves the signal
    // untouched (no oversampling, no latency). Gentle and Warm run the shaper
    // at 4x the sample rate behind half-band IIR filters, so the harmonics it
    // creates above the original Nyquist are filtered out instead of folding
    // back down as aliasing. Mode changes crossfade over a short ramp.
    class CharacterStage
    {
    public:
        static constexpr int maxChannels = 16; // up to 9.1.6
        static constexpr int oversamplingStages = 2; // 2^2 = 4x

        void prepare (double sampleRate, int maxBlockSize, int numChannels)
        {
            channels = juce::jlimit (1, maxChannels, numChannels);

            oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
                (size_t) channels, (size_t) oversamplingStages,
                juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                true, false);
            oversampler->initProcessing ((size_t) maxBlockSize);

            dry.setSize (channels, maxBlockSize);

            // Remove the DC the Warm bias adds to the harmonics, without
            // touching the signal itself (only the added colour is filtered).
            const auto osRate = sampleRate * (double) (1 << oversamplingStages);
            dcCoefficient = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * dcBlockHz / osRate);

            const auto rampSamples = juce::roundToInt (sampleRate * rampSeconds);
            for (auto* s : { &active, &gentleAmount, &warmAmount })
                s->reset (rampSamples);

            snapToTarget();
            reset();
        }

        void reset() noexcept
        {
            if (oversampler != nullptr)
                oversampler->reset();

            dcIn.fill (0.0f);
            dcOut.fill (0.0f);
        }

        void setMode (CharacterMode newMode) noexcept
        {
            mode = newMode;
            active.setTargetValue (mode == CharacterMode::clean ? 0.0f : 1.0f);

            // While fading to Clean, keep the last colour so it fades out
            // as it was instead of switching curves on the way.
            if (mode == CharacterMode::gentle) { gentleAmount.setTargetValue (1.0f); warmAmount.setTargetValue (0.0f); }
            if (mode == CharacterMode::warm)   { gentleAmount.setTargetValue (0.0f); warmAmount.setTargetValue (1.0f); }
        }

        CharacterMode getMode() const noexcept { return mode; }

        // Jump straight to the current mode (used when playback starts).
        void snapToTarget() noexcept
        {
            for (auto* s : { &active, &gentleAmount, &warmAmount })
                s->setCurrentAndTargetValue (s->getTargetValue());
        }

        void process (juce::AudioBuffer<float>& buffer) noexcept
        {
            const auto numSamples = buffer.getNumSamples();
            const auto numChannels = std::min (buffer.getNumChannels(), channels);

            if (numSamples == 0 || numChannels == 0 || oversampler == nullptr)
                return;

            const auto fading = active.isSmoothing();

            if (! fading && active.getTargetValue() < 0.5f)
            {
                // Clean: bit-exact passthrough. Clear filter state so the
                // next switch away from Clean starts from silence.
                if (! idle) { reset(); idle = true; }
                return;
            }

            idle = false;

            // Hosts may send blocks larger than announced: work in chunks.
            const auto maxChunk = dry.getNumSamples();

            for (int start = 0; start < numSamples; start += maxChunk)
            {
                float* ptrs[maxChannels] {};
                for (int ch = 0; ch < numChannels; ++ch)
                    ptrs[ch] = buffer.getWritePointer (ch, start);

                processChunk (ptrs, numChannels, std::min (maxChunk, numSamples - start));
            }
        }

        // Latency of the oversampling filters at the base rate. The IIR
        // filters are minimum-phase style, so this is a few samples of
        // low-frequency group delay rather than a pure delay.
        float getOversamplingLatency() const noexcept
        {
            return oversampler != nullptr ? oversampler->getLatencyInSamples() : 0.0f;
        }

    private:
        static constexpr double rampSeconds = 0.03;
        static constexpr double dcBlockHz = 8.0;

        void processChunk (float* const* channelPtrs, int numChannels, int numSamples) noexcept
        {
            const auto fading = active.isSmoothing();

            if (fading)
                for (int ch = 0; ch < numChannels; ++ch)
                    dry.copyFrom (ch, 0, channelPtrs[ch], numSamples);

            juce::dsp::AudioBlock<float> block (channelPtrs, (size_t) numChannels, (size_t) numSamples);
            auto up = oversampler->processSamplesUp (block);
            shape (up);
            oversampler->processSamplesDown (block);

            if (! fading)
                return;

            // Crossfade between the untouched input and the coloured signal.
            for (int i = 0; i < numSamples; ++i)
            {
                const auto a = active.getNextValue();

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const auto d = dry.getSample (ch, i);
                    channelPtrs[ch][i] = d + a * (channelPtrs[ch][i] - d);
                }
            }
        }

        void shape (juce::dsp::AudioBlock<float>& up) noexcept
        {
            const auto factor = (int) (1 << oversamplingStages);
            const auto osSamples = (int) up.getNumSamples();
            const auto baseSamples = osSamples / factor;

            // Advance the colour ramps once per base-rate sample.
            float g0 = gentleAmount.getCurrentValue(), w0 = warmAmount.getCurrentValue();
            gentleAmount.skip (baseSamples);
            warmAmount.skip (baseSamples);
            const float g1 = gentleAmount.getCurrentValue(), w1 = warmAmount.getCurrentValue();

            const auto useGentle = g0 > 0.0f || g1 > 0.0f;
            const auto useWarm   = w0 > 0.0f || w1 > 0.0f;
            const auto step = osSamples > 0 ? 1.0f / (float) osSamples : 0.0f;

            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
            {
                auto* x = up.getChannelPointer (ch);
                auto xPrev = dcIn[ch], yPrev = dcOut[ch];

                for (int i = 0; i < osSamples; ++i)
                {
                    const auto t = (float) i * step;
                    const auto in = x[i];
                    auto colour = 0.0f;

                    if (useGentle) colour += (g0 + (g1 - g0) * t) * (shaper::gentle (in) - in);
                    if (useWarm)   colour += (w0 + (w1 - w0) * t) * (shaper::warm (in) - in);

                    // One-pole DC blocker on the added colour only.
                    const auto y = colour - xPrev + dcCoefficient * yPrev;
                    xPrev = colour;
                    yPrev = y;

                    x[i] = in + y;
                }

                dcIn[ch] = xPrev;
                dcOut[ch] = yPrev;
            }
        }

        std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
        juce::AudioBuffer<float> dry;
        int channels = maxChannels;

        CharacterMode mode = CharacterMode::clean;
        juce::LinearSmoothedValue<float> active { 0.0f }, gentleAmount { 0.0f }, warmAmount { 0.0f };
        bool idle = true;

        float dcCoefficient = 0.999f;
        std::array<float, maxChannels> dcIn {}, dcOut {};
    };
}
