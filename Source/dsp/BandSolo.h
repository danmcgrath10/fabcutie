#pragma once

#include "EqBand.h"

namespace fabcutie::dsp
{
    // Intelligent solo: plays only the part of the spectrum a band works on,
    // so it is easy to hear what the band is doing (or hunt for a problem
    // frequency by sweeping it). Bells, notches, band passes and tilts are
    // auditioned through a band pass at the band's frequency and width; low
    // shelves and cuts through a low pass, high ones through a high pass.
    //
    // It reuses EqBand, so moving the soloed band glides and switching solo
    // on, off or to another band fades instead of clicking.
    class BandSolo
    {
    public:
        void prepare (double sampleRate) { filter.prepare (sampleRate); }

        // Pass a disabled band to switch the solo off.
        void setBand (const BandSettings& band) noexcept { filter.setTarget (auditionFor (band)); }

        void process (juce::AudioBuffer<float>& buffer, int numChannels) noexcept
        {
            filter.beginBlock();

            if (! filter.isActive())
                return;

            numChannels = std::min (numChannels, EqBand::maxChannels);

            float* channels[EqBand::maxChannels] {};
            const int slots[EqBand::maxChannels] { 0, 1 };

            for (int c = 0; c < numChannels; ++c)
                channels[c] = buffer.getWritePointer (c);

            filter.process (channels, slots, numChannels, buffer.getNumSamples());
        }

        // The filter used to audition a band. Public so it can be tested.
        static BandSettings auditionFor (const BandSettings& band) noexcept
        {
            BandSettings s;
            s.enabled = band.enabled;
            s.frequency = band.frequency;
            s.placement = Placement::stereo;

            switch (band.type)
            {
                case FilterType::lowShelf:
                case FilterType::lowCut:
                    s.type = FilterType::highCut; // keep what lies below the frequency
                    s.slopeIndex = 3;             // 24 dB/oct
                    s.q = 1.0f;
                    break;

                case FilterType::highShelf:
                case FilterType::highCut:
                    s.type = FilterType::lowCut;
                    s.slopeIndex = 3;
                    s.q = 1.0f;
                    break;

                case FilterType::bell:
                case FilterType::notch:
                case FilterType::bandPass:
                case FilterType::tiltShelf:
                case FilterType::allPass:
                    // Very wide bells would solo almost everything, very narrow
                    // ones almost nothing, so keep the width audible.
                    s.type = FilterType::bandPass;
                    s.q = std::clamp (band.q, 0.5f, 12.0f);
                    break;

                case FilterType::flatTilt:
                    // A flat tilt works on the whole spectrum, so solo passes
                    // everything (the default band is a 0 dB bell).
                    break;
            }

            return s;
        }

    private:
        EqBand filter;
    };
}
