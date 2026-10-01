#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "BandDynamics.h"
#include "FilterDesign.h"

namespace fabcutie::dsp
{
    // Spectral dynamics: dynamic bands that act bin by bin instead of moving
    // the whole band's gain.
    //
    // The signal runs through a short-time Fourier transform (2048 points,
    // 75% overlap, square-root Hann windows on both sides, so it rebuilds
    // the input exactly when nothing is applied). Every bin inside a
    // spectral band is compared with the band's threshold on its own and
    // gets its own gain offset, using the same soft-knee gain computer and
    // attack / release times as a regular dynamic band, weighted by the
    // band's shape. A loud resonance is pulled down without touching the
    // quieter frequencies right next to it.
    //
    // Levels are measured like the analyzer shows them: a full-scale sine
    // reads 0 dB in its bin, so the threshold can be read off the display.
    //
    // The bands' static gain stays in the regular EQ; only the dynamic
    // offset happens here. The stage delays the signal by fftSize samples,
    // so it only runs (and the plugin only reports latency) while at least
    // one band uses it.
    class SpectralDynamics
    {
    public:
        static constexpr int fftOrder = 11;
        static constexpr int fftSize = 1 << fftOrder;
        static constexpr int hopSize = fftSize / 4;
        static constexpr int numBins = fftSize / 2 + 1;
        static constexpr int maxChannels = 2;
        static constexpr int latencySamples = fftSize;

        SpectralDynamics() : fft (fftOrder)
        {
            // Periodic Hann, square-rooted for analysis and synthesis. At a
            // quarter-frame hop the product (a Hann) sums to 2.
            const auto pi = 3.14159265358979323846;
            auto sum = 0.0;

            for (int i = 0; i < fftSize; ++i)
            {
                const auto hann = 0.5 - 0.5 * std::cos (2.0 * pi * i / fftSize);
                window[(size_t) i] = (float) std::sqrt (hann);
                sum += window[(size_t) i];
            }

            levelScale = (float) (2.0 / sum);

            for (auto& b : bands)
            {
                b.weight.assign ((size_t) numBins, 0.0f);
                b.envelope.assign ((size_t) numBins, 0.0f);
            }
        }

        void prepare (double newSampleRate)
        {
            sampleRate = newSampleRate;

            for (auto& b : bands)
                b.shapeValid = false;

            reset();
        }

        void reset() noexcept
        {
            for (auto& r : input) std::fill (r.begin(), r.end(), 0.0f);
            for (auto& r : output) std::fill (r.begin(), r.end(), 0.0f);
            for (auto& r : sidechainInput) std::fill (r.begin(), r.end(), 0.0f);

            for (auto& b : bands)
            {
                std::fill (b.envelope.begin(), b.envelope.end(), 0.0f);
                b.gainDb = 0.0f;
            }

            position = 0;
            hopCount = 0;
        }

        void setBand (int index, const BandSettings& s) noexcept
        {
            auto& b = bands[(size_t) index];
            const auto on = s.enabled && s.dynamics.enabled && s.dynamics.spectral && supportsDynamics (s.type);

            if (on && b.shapeValid
                && (s.type != b.settings.type || ! juce::exactlyEqual (s.frequency, b.settings.frequency)
                    || ! juce::exactlyEqual (s.q, b.settings.q)))
                b.shapeValid = false;

            if (on != b.on)
            {
                std::fill (b.envelope.begin(), b.envelope.end(), 0.0f);
                b.gainDb = 0.0f;
            }

            b.on = on;
            b.settings = s;
        }

        bool isBandOn (int index) const noexcept { return bands[(size_t) index].on; }

        // While bypassed the stage still runs (so its latency stays the
        // same) but leaves the signal alone.
        void setBypassed (bool shouldBypass) noexcept
        {
            if (shouldBypass && ! bypassed)
            {
                for (auto& b : bands)
                {
                    std::fill (b.envelope.begin(), b.envelope.end(), 0.0f);
                    b.gainDb = 0.0f;
                }
            }

            bypassed = shouldBypass;
        }

        // True when any band works spectrally, so the stage has to run.
        bool isActive() const noexcept
        {
            return std::any_of (bands.begin(), bands.end(), [] (const Band& b) { return b.on; });
        }

        // The band's strongest current gain offset, in dB, for the display.
        float getGainDb (int index) const noexcept { return bands[(size_t) index].gainDb; }

        // Processes the buffer in place (mono or stereo), delayed by
        // latencySamples. Bands listening to an external source hear the
        // sidechain, or silence when there is none.
        void process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>* sidechain = nullptr) noexcept
        {
            const auto numChannels = std::min (buffer.getNumChannels(), maxChannels);
            const auto numSamples = buffer.getNumSamples();
            auto* const* data = buffer.getArrayOfWritePointers();

            const auto numSidechain = sidechain != nullptr && sidechain->getNumSamples() >= numSamples
                                          ? std::min (sidechain->getNumChannels(), maxChannels) : 0;

            channels = numChannels;
            sidechainChannels = numSidechain;

            for (int i = 0; i < numSamples; ++i)
            {
                for (int c = 0; c < numChannels; ++c)
                {
                    input[(size_t) c][(size_t) position] = data[c][i];
                    data[c][i] = output[(size_t) c][(size_t) position];
                    output[(size_t) c][(size_t) position] = 0.0f;
                }

                for (int c = 0; c < maxChannels; ++c)
                    sidechainInput[(size_t) c][(size_t) position] = c < numSidechain ? sidechain->getSample (c, i) : 0.0f;

                position = (position + 1) % fftSize;

                if (++hopCount == hopSize)
                {
                    hopCount = 0;
                    processFrame();
                }
            }
        }

    private:
        using Spectrum = std::array<float, (size_t) fftSize * 2>; // interleaved re / im
        using Bins = std::array<float, (size_t) numBins>;

        struct Band
        {
            bool on = false;
            BandSettings settings;
            bool shapeValid = false;
            std::vector<float> weight;   // per bin, signed: the band's shape at a +1 dB offset
            std::vector<float> envelope; // per bin gain offset, dB
            int firstBin = 0, lastBin = -1;
            float gainDb = 0.0f;
        };

        // The band's shape: how much of a gain offset each bin gets. A bell
        // gets the full offset at its centre, a shelf on its shelf side, a
        // tilt shelf half up and half down. Bins that barely move are skipped.
        void updateShape (Band& b) noexcept
        {
            constexpr float referenceDb = 12.0f;

            auto s = b.settings;
            s.gainDb = referenceDb;
            s.dynamics = {};
            const auto design = designBand (s, sampleRate);

            b.firstBin = numBins;
            b.lastBin = -1;

            for (int k = 0; k < numBins; ++k)
            {
                const auto hz = std::max (1.0, (double) k * sampleRate / fftSize);
                const auto mag = std::abs (designResponse (design, hz, sampleRate));
                const auto w = (float) (20.0 * std::log10 (std::max (mag, 1.0e-9)) / referenceDb);
                b.weight[(size_t) k] = w;

                if (std::abs (w) >= 0.02f)
                {
                    b.firstBin = std::min (b.firstBin, k);
                    b.lastBin = k;
                }
            }

            b.shapeValid = true;
        }

        void analyse (const std::array<float, (size_t) fftSize>& ring, Spectrum& spectrum) noexcept
        {
            // Oldest sample first: the ring's write position is the oldest.
            for (int i = 0; i < fftSize; ++i)
                spectrum[(size_t) i] = ring[(size_t) ((position + i) % fftSize)] * window[(size_t) i];

            std::fill (spectrum.begin() + fftSize, spectrum.end(), 0.0f);
            fft.performRealOnlyForwardTransform (spectrum.data(), true);
        }

        void magnitudes (const Spectrum& spectrum, Bins& out) const noexcept
        {
            for (int k = 0; k < numBins; ++k)
            {
                const auto re = spectrum[(size_t) (2 * k)], im = spectrum[(size_t) (2 * k + 1)];
                out[(size_t) k] = std::sqrt (re * re + im * im) * levelScale;
            }
        }

        void processFrame() noexcept
        {
            const auto anyOn = isActive();

            for (int c = 0; c < channels; ++c)
                analyse (input[(size_t) c], spectra[(size_t) c]);

            if (anyOn && ! bypassed)
                applyDynamics();

            for (int c = 0; c < channels; ++c)
            {
                auto& spectrum = spectra[(size_t) c];
                fft.performRealOnlyInverseTransform (spectrum.data());

                auto& out = output[(size_t) c];
                for (int i = 0; i < fftSize; ++i)
                    out[(size_t) ((position + i) % fftSize)] += spectrum[(size_t) i] * window[(size_t) i] * 0.5f;
            }
        }

        void applyDynamics() noexcept
        {
            const auto stereo = channels == 2;

            // Detector levels, linear, computed on demand.
            bool haveLeft = false, haveRight = false, haveMid = false, haveSide = false, haveExternal = false;

            auto level = [&] (Bins& bins, bool& have, auto&& fill) -> const Bins&
            {
                if (! have)
                {
                    fill (bins);
                    have = true;
                }

                return bins;
            };

            auto fillLeft  = [&] (Bins& out) { magnitudes (spectra[0], out); };
            auto fillRight = [&] (Bins& out) { magnitudes (spectra[1], out); };
            auto fillMidSide = [&] (Bins& out, float sign)
            {
                for (int k = 0; k < numBins; ++k)
                {
                    const auto re = 0.5f * (spectra[0][(size_t) (2 * k)] + sign * spectra[1][(size_t) (2 * k)]);
                    const auto im = 0.5f * (spectra[0][(size_t) (2 * k + 1)] + sign * spectra[1][(size_t) (2 * k + 1)]);
                    out[(size_t) k] = std::sqrt (re * re + im * im) * levelScale;
                }
            };
            auto fillExternal = [&] (Bins& out)
            {
                std::fill (out.begin(), out.end(), 0.0f);

                for (int c = 0; c < sidechainChannels; ++c)
                {
                    analyse (sidechainInput[(size_t) c], scratch);
                    magnitudes (scratch, other);

                    for (int k = 0; k < numBins; ++k)
                        out[(size_t) k] = std::max (out[(size_t) k], other[(size_t) k]);
                }
            };

            // Gain offsets in dB, for left / right (or mono) and mid / side.
            for (auto* g : { &gainA, &gainB, &gainMid, &gainSide })
                std::fill (g->begin(), g->end(), 0.0f);

            auto usedLeftRight = false, usedMidSide = false;

            const auto frameSeconds = (double) hopSize / sampleRate;

            for (auto& b : bands)
            {
                if (! b.on)
                    continue;

                if (! b.shapeValid)
                    updateShape (b);

                const auto& d = b.settings.dynamics;
                const auto placement = b.settings.placement;

                // Which bins this band listens to, and which it changes.
                const Bins* detector = nullptr;
                Bins* target1 = nullptr;
                Bins* target2 = nullptr;

                if (d.source == DetectorSource::external)
                {
                    detector = &level (external, haveExternal, fillExternal);
                }
                else if (! stereo || placement == Placement::left)
                {
                    detector = &level (left, haveLeft, fillLeft);
                }
                else if (placement == Placement::right)
                {
                    detector = &level (right, haveRight, fillRight);
                }
                else if (placement == Placement::mid)
                {
                    detector = &level (mid, haveMid, [&] (Bins& out) { fillMidSide (out, 1.0f); });
                }
                else if (placement == Placement::side)
                {
                    detector = &level (side, haveSide, [&] (Bins& out) { fillMidSide (out, -1.0f); });
                }
                else
                {
                    // Stereo: linked, the louder channel decides.
                    const auto& l = level (left, haveLeft, fillLeft);
                    const auto& r = level (right, haveRight, fillRight);
                    for (int k = 0; k < numBins; ++k)
                        linked[(size_t) k] = std::max (l[(size_t) k], r[(size_t) k]);
                    detector = &linked;
                }

                if (! stereo)
                {
                    // A mono signal is all mid and no side, like EqEngine.
                    if (placement != Placement::right && placement != Placement::side)
                        target1 = &gainA;
                }
                else
                {
                    switch (placement)
                    {
                        case Placement::stereo: target1 = &gainA; target2 = &gainB; break;
                        case Placement::left:   target1 = &gainA; break;
                        case Placement::right:  target1 = &gainB; break;
                        case Placement::mid:    target1 = &gainMid; break;
                        case Placement::side:   target1 = &gainSide; break;
                    }
                }

                if (target1 == nullptr)
                {
                    b.gainDb = 0.0f;
                    continue;
                }

                const auto isMidSide = stereo && (placement == Placement::mid || placement == Placement::side);
                usedMidSide = usedMidSide || isMidSide;
                usedLeftRight = usedLeftRight || ! isMidSide;

                const auto attack = (float) std::exp (-frameSeconds / std::max (1.0e-5, (double) d.attackMs * 0.001));
                const auto release = (float) std::exp (-frameSeconds / std::max (1.0e-5, (double) d.releaseMs * 0.001));

                auto strongest = 0.0f, strongestWeight = 0.0f;

                for (int k = b.firstBin; k <= b.lastBin; ++k)
                {
                    const auto w = b.weight[(size_t) k];
                    const auto levelDb = 20.0f * std::log10 (std::max ((*detector)[(size_t) k], 1.0e-10f));
                    const auto targetDb = BandDynamics::gainOffsetDb (levelDb, d.thresholdDb, d.rangeDb);

                    auto& env = b.envelope[(size_t) k];
                    const auto coeff = std::abs (targetDb) > std::abs (env) ? attack : release;
                    env = targetDb + coeff * (env - targetDb);

                    if (std::abs (env) < 1.0e-4f)
                        env = 0.0f;

                    const auto offset = env * w;
                    (*target1)[(size_t) k] += offset;
                    if (target2 != nullptr)
                        (*target2)[(size_t) k] += offset;

                    // Report the strongest move near the middle of the band.
                    if (std::abs (w) >= 0.5f && std::abs (env) > std::abs (strongest))
                        strongest = env;

                    strongestWeight = std::max (strongestWeight, std::abs (w));
                }

                b.gainDb = strongestWeight >= 0.5f ? strongest : 0.0f;
            }

            if (usedLeftRight)
            {
                applyGains (spectra[0], gainA);
                if (stereo)
                    applyGains (spectra[1], gainB);
            }

            if (usedMidSide)
            {
                // M = (L + R) / 2, S = (L - R) / 2; back with L = M + S, R = M - S.
                for (int k = 0; k < numBins; ++k)
                {
                    const auto gm = juce::Decibels::decibelsToGain (gainMid[(size_t) k], -200.0f);
                    const auto gs = juce::Decibels::decibelsToGain (gainSide[(size_t) k], -200.0f);

                    for (int part = 0; part < 2; ++part)
                    {
                        auto& l = spectra[0][(size_t) (2 * k + part)];
                        auto& r = spectra[1][(size_t) (2 * k + part)];
                        const auto m = 0.5f * (l + r) * gm;
                        const auto s = 0.5f * (l - r) * gs;
                        l = m + s;
                        r = m - s;
                    }
                }
            }
        }

        static void applyGains (Spectrum& spectrum, const Bins& gainsDb) noexcept
        {
            for (int k = 0; k < numBins; ++k)
            {
                const auto db = gainsDb[(size_t) k];
                if (juce::exactlyEqual (db, 0.0f))
                    continue;

                const auto g = juce::Decibels::decibelsToGain (db, -200.0f);
                spectrum[(size_t) (2 * k)] *= g;
                spectrum[(size_t) (2 * k + 1)] *= g;
            }
        }

        juce::dsp::FFT fft;
        double sampleRate = 48000.0;

        std::array<float, (size_t) fftSize> window {};
        float levelScale = 1.0f;

        std::array<std::array<float, (size_t) fftSize>, maxChannels> input {}, output {}, sidechainInput {};
        int position = 0, hopCount = 0;
        int channels = 0, sidechainChannels = 0;
        bool bypassed = false;

        std::array<Spectrum, maxChannels> spectra {};
        Spectrum scratch {};
        Bins left {}, right {}, mid {}, side {}, external {}, linked {}, other {};
        Bins gainA {}, gainB {}, gainMid {}, gainSide {};

        std::array<Band, maxBands> bands;
    };
}
