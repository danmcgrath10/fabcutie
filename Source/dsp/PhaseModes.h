#pragma once

#include <array>
#include <cmath>
#include <complex>
#include <vector>

#include <juce_dsp/juce_dsp.h>

#include "FilterDesign.h"

namespace fabcutie::dsp
{
    // How the bands are realised. Stored in sessions: only append.
    //
    // - Zero latency: the bands run as minimum-phase IIR filters (EqBand).
    //   Their response is bilinear-transformed, so curves "cramp" (narrow
    //   and lose gain) as they near Nyquist.
    // - Natural phase: an FIR filter built from the complex response of the
    //   analog prototypes, so magnitude and phase match an analog EQ right
    //   up to Nyquist. Costs a few milliseconds of latency.
    // - Linear phase: an FIR filter with the analog magnitude and no phase
    //   shift at all. The latency is half the kernel length; a longer kernel
    //   (higher resolution) is more accurate in the low end.
    enum class PhaseMode
    {
        zeroLatency,
        natural,
        linear
    };

    inline constexpr int numPhaseModes = 3;

    // Linear phase kernel lengths at 44.1 / 48 kHz, one per resolution
    // setting. They double at 88.2 / 96 kHz and again above, so the
    // resolution in Hz (and the latency in ms) stays the same.
    inline constexpr std::array<int, 5> linearKernelLengths { 2048, 4096, 8192, 16384, 32768 };
    inline constexpr int numLinearResolutions = (int) linearKernelLengths.size();
    inline constexpr int defaultLinearResolution = 2;

    inline constexpr int naturalKernelLength = 8192;
    inline constexpr int naturalPreDelay = 64;    // room for the band-limited onset
    inline constexpr int basePartitionLength = 256;

    // Dynamic bands always run as IIR filters, after the FIR, since their
    // gain moves faster than a kernel could be redesigned. Spectral bands
    // are static in the EQ (SpectralDynamics moves their gain), so they
    // stay in the FIR.
    inline bool runsAsIir (const BandSettings& s, PhaseMode mode) noexcept
    {
        return mode == PhaseMode::zeroLatency
            || (s.dynamics.enabled && ! s.dynamics.spectral && supportsDynamics (s.type));
    }

    inline int phaseRateFactor (double sampleRate) noexcept
    {
        return sampleRate > 100000.0 ? 4 : sampleRate > 50000.0 ? 2 : 1;
    }

    struct PhaseLayout
    {
        PhaseMode mode = PhaseMode::zeroLatency;
        int kernelLength = 0;
        int kernelDelay = 0; // where the kernel's "now" sits
        int partition = 0;   // block size of the convolution, adds to the latency

        int latency() const noexcept { return mode == PhaseMode::zeroLatency ? 0 : kernelDelay + partition; }

        bool operator== (const PhaseLayout& o) const noexcept
        {
            return mode == o.mode && kernelLength == o.kernelLength
                && kernelDelay == o.kernelDelay && partition == o.partition;
        }

        bool operator!= (const PhaseLayout& o) const noexcept { return ! operator== (o); }
    };

    inline PhaseLayout makePhaseLayout (PhaseMode mode, int resolution, double sampleRate) noexcept
    {
        const auto factor = phaseRateFactor (sampleRate);
        PhaseLayout l;
        l.mode = mode;

        if (mode == PhaseMode::zeroLatency)
            return l;

        l.partition = basePartitionLength * factor;

        if (mode == PhaseMode::natural)
        {
            l.kernelLength = naturalKernelLength * factor;
            l.kernelDelay = naturalPreDelay * factor;
        }
        else
        {
            l.kernelLength = linearKernelLengths[(size_t) std::clamp (resolution, 0, numLinearResolutions - 1)] * factor;
            l.kernelDelay = l.kernelLength / 2;
        }

        return l;
    }

    // The bands are evaluated as analog filters by designing them for a
    // sample rate far above the real one, where the bilinear warp is
    // negligible (well under 0.001 dB in the audio band).
    inline constexpr double analogRateMultiple = 256.0;

    inline std::complex<double> analogBandResponse (const BandDesign& analogDesign, double frequency, double sampleRate) noexcept
    {
        return designResponse (analogDesign, frequency, sampleRate * analogRateMultiple);
    }

    inline BandDesign designAnalogBand (const BandSettings& s, double sampleRate) noexcept
    {
        return designBand (s, sampleRate * analogRateMultiple);
    }

    //==========================================================================
    // An FIR kernel for the whole EQ. With mid/side bands the left and right
    // outputs each depend on both inputs, so a stereo kernel is a 2 x 2
    // matrix of filters; path (out, in) is taps[out * 2 + in]. Mono uses
    // path 0 only.
    struct PhaseKernel
    {
        std::array<std::vector<float>, 4> taps;
        int length = 0;
        int numChannels = 0;
        bool crossTerms = false; // left feeds right and vice versa
    };

    // Builds the kernel for the given bands (disabled ones are skipped) in
    // natural or linear phase. The response is sampled on a grid four times
    // the kernel length, turned into an impulse response and windowed.
    inline void designPhaseKernel (const std::array<BandSettings, maxBands>& bands, const PhaseLayout& layout,
                                   double sampleRate, int numChannels, PhaseKernel& kernel)
    {
        using C = std::complex<double>;
        const auto pi = 3.14159265358979323846;

        const auto length = layout.kernelLength;
        const auto gridOrder = juce::roundToInt (std::log2 ((double) length)) + 2;
        const auto grid = 1 << gridOrder;
        const auto linear = layout.mode == PhaseMode::linear;
        const auto stereo = numChannels > 1;

        struct Active { BandDesign design; Placement placement; };
        std::vector<Active> active;

        for (const auto& s : bands)
            if (s.enabled)
                active.push_back ({ designAnalogBand (s, sampleRate), s.placement });

        kernel.length = length;
        kernel.numChannels = stereo ? 2 : 1;
        kernel.crossTerms = false;

        if (stereo)
            for (const auto& a : active)
                kernel.crossTerms = kernel.crossTerms || a.placement == Placement::mid || a.placement == Placement::side;

        const auto numPaths = stereo ? 4 : 1;
        std::array<std::vector<float>, 4> spectra;

        for (int p = 0; p < numPaths; ++p)
            spectra[(size_t) p].assign ((size_t) grid * 2, 0.0f);

        for (int k = 0; k <= grid / 2; ++k)
        {
            const auto f = (double) k * sampleRate / (double) grid;

            // m = [[a, b], [c, d]] maps (left, right) in to (left, right) out.
            C a { 1.0 }, b { 0.0 }, c { 0.0 }, d { 1.0 };

            for (const auto& band : active)
            {
                auto h = analogBandResponse (band.design, f, sampleRate);

                if (linear)
                    h = std::abs (h);

                if (! stereo)
                {
                    if (band.placement != Placement::side)
                        a *= h;
                    continue;
                }

                // The band's own matrix, applied after what came before.
                C ba, bb, bc, bd;

                switch (band.placement)
                {
                    case Placement::stereo: ba = h;   bb = 0.0; bc = 0.0; bd = h;   break;
                    case Placement::left:   ba = h;   bb = 0.0; bc = 0.0; bd = 1.0; break;
                    case Placement::right:  ba = 1.0; bb = 0.0; bc = 0.0; bd = h;   break;
                    case Placement::mid:    ba = bd = 0.5 * (h + 1.0); bb = bc = 0.5 * (h - 1.0); break;
                    case Placement::side:   ba = bd = 0.5 * (1.0 + h); bb = bc = 0.5 * (1.0 - h); break;
                }

                const auto na = ba * a + bb * c, nb = ba * b + bb * d;
                const auto nc = bc * a + bd * c, nd = bc * b + bd * d;
                a = na; b = nb; c = nc; d = nd;
            }

            // Natural phase keeps the analog phase, shifted by the pre-delay.
            // Linear phase is real (zero phase) here and centred later.
            const auto shift = linear ? C { 1.0 } : std::polar (1.0, -2.0 * pi * (double) k * layout.kernelDelay / (double) grid);
            const std::array<C, 4> m { a * shift, b * shift, c * shift, d * shift };

            for (int p = 0; p < numPaths; ++p)
            {
                auto v = m[(size_t) p];

                if (k == 0 || k == grid / 2)
                    v = v.real(); // a real impulse response needs real DC and Nyquist bins

                spectra[(size_t) p][(size_t) (2 * k)]     = (float) v.real();
                spectra[(size_t) p][(size_t) (2 * k + 1)] = (float) v.imag();
            }
        }

        juce::dsp::FFT fft (gridOrder);

        for (int p = 0; p < 4; ++p)
        {
            auto& taps = kernel.taps[(size_t) p];
            taps.assign ((size_t) length, 0.0f);

            if (p >= numPaths)
                continue;

            auto& ir = spectra[(size_t) p];
            fft.performRealOnlyInverseTransform (ir.data());

            if (linear)
            {
                // Centre the zero-phase response and taper it to the ends.
                const auto half = length / 2;

                for (int i = 0; i < length; ++i)
                {
                    const auto n = i - half;
                    const auto edge = std::abs ((double) n) / (double) half;
                    const auto taper = 0.5; // outer fraction that is tapered (Tukey window)
                    const auto w = edge <= 1.0 - taper ? 1.0 : 0.5 + 0.5 * std::cos (pi * (edge - (1.0 - taper)) / taper);
                    taps[(size_t) i] = (float) (ir[(size_t) ((n + grid) % grid)] * w);
                }
            }
            else
            {
                // Causal: fade out the last quarter so a long decay doesn't stop dead.
                const auto fadeStart = length * 3 / 4;

                for (int i = 0; i < length; ++i)
                {
                    const auto w = i < fadeStart ? 1.0
                                                 : 0.5 + 0.5 * std::cos (pi * (double) (i - fadeStart) / (double) (length - fadeStart));
                    taps[(size_t) i] = (float) (ir[(size_t) i] * w);
                }
            }
        }
    }

    //==========================================================================
    // Kernel spectra, cut into partitions for uniformly partitioned
    // convolution.
    struct PartitionedKernel
    {
        std::array<std::vector<std::complex<float>>, 4> spectra; // per path, partition after partition
        std::array<std::vector<char>, 4> used;                   // per path and partition: not all zero
        int numPartitions = 0;
        int numChannels = 0;
        bool crossTerms = false;
        juce::uint32 layoutId = 0;

        void build (const PhaseKernel& k, int partition, juce::dsp::FFT& fft, std::vector<float>& scratch)
        {
            const auto bins = partition + 1;
            numPartitions = (k.length + partition - 1) / partition;
            numChannels = k.numChannels;
            crossTerms = k.crossTerms;
            scratch.resize ((size_t) partition * 4);

            for (int p = 0; p < 4; ++p)
            {
                auto& spec = spectra[(size_t) p];
                auto& flags = used[(size_t) p];
                spec.resize ((size_t) (numPartitions * bins));
                flags.assign ((size_t) numPartitions, 0);

                const auto& taps = k.taps[(size_t) p];

                for (int part = 0; part < numPartitions; ++part)
                {
                    std::fill (scratch.begin(), scratch.end(), 0.0f);
                    bool any = false;

                    for (int i = 0; i < partition; ++i)
                    {
                        const auto t = (size_t) (part * partition + i);
                        const auto v = t < taps.size() ? taps[t] : 0.0f;
                        scratch[(size_t) i] = v;
                        any = any || ! juce::exactlyEqual (v, 0.0f);
                    }

                    flags[(size_t) part] = any ? 1 : 0;

                    if (any)
                    {
                        fft.performRealOnlyForwardTransform (scratch.data(), true);
                        std::copy (scratch.data(), scratch.data() + 2 * bins, reinterpret_cast<float*> (spec.data() + part * bins));
                    }
                }
            }
        }
    };

    //==========================================================================
    // Uniformly partitioned overlap-save convolution of a stereo (or mono)
    // signal with a PartitionedKernel, with one partition of latency.
    // Swapping kernels crossfades over one partition.
    class PartitionedConvolver
    {
    public:
        static constexpr int maxChannels = 2;

        void prepare (int partitionLength, int maxPartitions, int channels)
        {
            partition = partitionLength;
            bins = partition + 1;
            numPartitions = maxPartitions;
            numChannels = std::clamp (channels, 1, maxChannels);
            fft = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 ((double) partition)) + 1);

            for (int ch = 0; ch < maxChannels; ++ch)
            {
                frame[(size_t) ch].assign ((size_t) partition * 2, 0.0f);
                outBlock[(size_t) ch].assign ((size_t) partition, 0.0f);
                history[(size_t) ch].assign ((size_t) (numPartitions * bins), {});
            }

            work.assign ((size_t) partition * 4, 0.0f);
            older.assign ((size_t) partition, 0.0f);
            acc.assign ((size_t) bins, {});
            reset();
        }

        void reset() noexcept
        {
            for (int ch = 0; ch < maxChannels; ++ch)
            {
                std::fill (frame[(size_t) ch].begin(), frame[(size_t) ch].end(), 0.0f);
                std::fill (outBlock[(size_t) ch].begin(), outBlock[(size_t) ch].end(), 0.0f);
                std::fill (history[(size_t) ch].begin(), history[(size_t) ch].end(), std::complex<float> {});
            }

            position = 0;
            head = 0;
        }

        // The kernel to use from the next partition on; nullptr outputs
        // silence. The previous kernel must stay untouched until
        // isCrossfading() turns false (it does after the next partition).
        void setKernel (const PartitionedKernel* k) noexcept
        {
            if (k == current)
                return;

            previous = current;
            current = k;
            crossfading = true;
        }

        // Drops both kernels at once, without a crossfade.
        void clearKernel() noexcept
        {
            current = previous = nullptr;
            crossfading = false;
        }

        bool isCrossfading() const noexcept { return crossfading; }

        void process (float* const* data, int channels, int numSamples) noexcept
        {
            channels = std::min (channels, numChannels);
            int done = 0;

            while (done < numSamples)
            {
                const auto chunk = std::min (partition - position, numSamples - done);

                for (int ch = 0; ch < channels; ++ch)
                {
                    auto* io = data[ch] + done;
                    std::copy (io, io + chunk, frame[(size_t) ch].data() + partition + position);
                    std::copy (outBlock[(size_t) ch].data() + position, outBlock[(size_t) ch].data() + position + chunk, io);
                }

                position += chunk;
                done += chunk;

                if (position == partition)
                {
                    position = 0;
                    processPartition (channels);
                }
            }
        }

    private:
        void processPartition (int channels) noexcept
        {
            head = (head + 1) % numPartitions;

            for (int ch = 0; ch < channels; ++ch)
            {
                auto& f = frame[(size_t) ch];
                std::copy (f.begin(), f.end(), work.begin());
                std::fill (work.begin() + partition * 2, work.end(), 0.0f);
                fft->performRealOnlyForwardTransform (work.data(), true);
                std::copy (work.data(), work.data() + 2 * bins, reinterpret_cast<float*> (history[(size_t) ch].data() + head * bins));
                std::copy (f.begin() + partition, f.end(), f.begin()); // this block is next time's overlap
            }

            for (int out = 0; out < channels; ++out)
            {
                auto* dest = outBlock[(size_t) out].data();
                convolve (current, out, channels, dest);

                if (crossfading)
                {
                    convolve (previous, out, channels, older.data());

                    for (int i = 0; i < partition; ++i)
                    {
                        const auto t = ((float) i + 0.5f) / (float) partition;
                        dest[i] = older[(size_t) i] + t * (dest[i] - older[(size_t) i]);
                    }
                }
            }

            crossfading = false;
            previous = nullptr;
        }

        void convolve (const PartitionedKernel* k, int out, int channels, float* dest) noexcept
        {
            if (k == nullptr || k->numPartitions > numPartitions)
            {
                std::fill (dest, dest + partition, 0.0f);
                return;
            }

            std::fill (acc.begin(), acc.end(), std::complex<float> {});

            for (int in = 0; in < channels; ++in)
            {
                if (in != out && ! k->crossTerms)
                    continue;

                const auto path = k->numChannels > 1 ? out * 2 + in : 0;
                const auto& spec = k->spectra[(size_t) path];
                const auto& used = k->used[(size_t) path];
                const auto* hist = history[(size_t) in].data();

                for (int p = 0; p < k->numPartitions; ++p)
                {
                    if (! used[(size_t) p])
                        continue;

                    const auto* x = hist + ((head - p + numPartitions) % numPartitions) * bins;
                    const auto* h = spec.data() + p * bins;

                    for (int i = 0; i < bins; ++i)
                        acc[(size_t) i] += x[i] * h[i];
                }
            }

            std::copy (reinterpret_cast<const float*> (acc.data()), reinterpret_cast<const float*> (acc.data()) + 2 * bins, work.data());
            fft->performRealOnlyInverseTransform (work.data());
            std::copy (work.begin() + partition, work.begin() + partition * 2, dest);
        }

        int partition = 0, bins = 0, numPartitions = 1, numChannels = 1;
        int position = 0, head = 0;
        std::unique_ptr<juce::dsp::FFT> fft;
        std::array<std::vector<float>, maxChannels> frame, outBlock;
        std::array<std::vector<std::complex<float>>, maxChannels> history;
        std::vector<float> work, older;
        std::vector<std::complex<float>> acc;
        const PartitionedKernel* current = nullptr;
        const PartitionedKernel* previous = nullptr;
        bool crossfading = false;
    };

    //==========================================================================
    // A delay line that keeps the sidechain in step with the delayed main signal.
    class SidechainDelay
    {
    public:
        void prepare (int maxDelay)
        {
            for (auto& line : lines)
                line.assign ((size_t) maxDelay + 1, 0.0f);
            write = 0;
        }

        void reset() noexcept
        {
            for (auto& line : lines)
                std::fill (line.begin(), line.end(), 0.0f);
        }

        void process (float* const* data, int channels, int numSamples, int delay) noexcept
        {
            const auto size = (int) lines[0].size();

            if (delay <= 0 || delay >= size)
                return;

            for (int i = 0; i < numSamples; ++i)
            {
                const auto read = (write - delay + size) % size;

                for (int ch = 0; ch < std::min (channels, 2); ++ch)
                {
                    auto& line = lines[(size_t) ch];
                    line[(size_t) write] = data[ch][i];
                    data[ch][i] = line[(size_t) read];
                }

                write = (write + 1) % size;
            }
        }

    private:
        std::array<std::vector<float>, 2> lines;
        int write = 0;
    };

    //==========================================================================
    // Runs the natural or linear phase FIR for the bands that are not
    // dynamic. Kernels are designed on a background thread (or inline when
    // rendering offline) and crossfaded in. Changing mode or resolution
    // fades the output out, switches, and fades back in once the first
    // kernel for the new layout is ready.
    class PhaseStage : private juce::Thread
    {
    public:
        PhaseStage() : juce::Thread ("FabCutie phase designer") {}
        ~PhaseStage() override { stopThread (2000); }

        // Rendering offline: design kernels inline so every change lands on
        // exactly the block it was made in.
        void setNonRealtime (bool shouldDesignInline) noexcept { inlineDesign.store (shouldDesignInline); }

        void prepare (double newSampleRate, int channels, PhaseMode mode, int resolution,
                      const std::array<BandSettings, maxBands>& bands)
        {
            stopThread (2000);

            sampleRate = newSampleRate;
            numChannels = std::clamp (channels, 1, PartitionedConvolver::maxChannels);

            const auto longest = makePhaseLayout (PhaseMode::linear, numLinearResolutions - 1, sampleRate);
            const auto partition = longest.partition;
            convolver.prepare (partition, longest.kernelLength / partition, numChannels);
            sidechainDelay.prepare (longest.latency());

            convolver.clearKernel();

            for (auto& slot : slots)
                slot = PartitionedKernel {};

            activeSlot.store (-1);
            previousSlot.store (-1);
            pendingSlot.store (-1);
            lastPublished = -1;

            targetLayout = layout = makePhaseLayout (mode, resolution, sampleRate);
            ++layoutId;
            fadeIn = false;
            gain = 1.0f;

            bandsSent = bands;
            request.bands = bands;
            request.layout = layout;
            request.layoutId = layoutId;
            ++requestSerial;
            designedSerial.store (requestSerial.load());

            if (layout.mode != PhaseMode::zeroLatency)
            {
                produce (request);
                adoptPendingKernel();
            }

            startThread();
        }

        void setTarget (PhaseMode mode, int resolution) noexcept
        {
            targetLayout = makePhaseLayout (mode, resolution, sampleRate);
        }

        // The mode the stage runs this block, i.e. which bands EqEngine must run.
        PhaseMode getMode() const noexcept { return layout.mode; }
        int getLatency() const noexcept { return layout.latency(); }

        static int latencyFor (PhaseMode mode, int resolution, double sampleRate) noexcept
        {
            return makePhaseLayout (mode, resolution, sampleRate).latency();
        }

        // The bands the FIR should run (the others disabled). Call each block.
        void setBands (const std::array<BandSettings, maxBands>& bands) noexcept
        {
            if (layout.mode == PhaseMode::zeroLatency)
                return;

            if (! requestDirty && sameBands (bands, bandsSent))
                return;

            // Offline the designer thread stays away from the request, so
            // waiting is fine; live, a busy lock just means next block.
            if (inlineDesign.load())
            {
                const juce::SpinLock::ScopedLockType lock (requestLock);
                writeRequest (bands);
                return;
            }

            const juce::SpinLock::ScopedTryLockType lock (requestLock);

            if (! lock.isLocked())
            {
                requestDirty = true;
                return;
            }

            writeRequest (bands);
        }

        void process (juce::AudioBuffer<float>& buffer) noexcept
        {
            const auto channels = std::min (buffer.getNumChannels(), numChannels);
            const auto numSamples = buffer.getNumSamples();

            // After a switch the signal fades in on the way into the FIR
            // (whose history starts silent), so it doesn't start abruptly.
            if (fadeIn)
            {
                const auto end = std::min (1.0f, gain + (float) numSamples / (float) fadeSamples());
                buffer.applyGainRamp (0, numSamples, gain, end);
                gain = end;
                fadeIn = gain < 1.0f;
            }

            if (layout.mode != PhaseMode::zeroLatency)
            {
                if (inlineDesign.load() && requestSerial.load() != designedSerial.load())
                {
                    designedSerial.store (requestSerial.load());
                    produce (request);
                }

                if (! convolver.isCrossfading())
                {
                    previousSlot.store (-1); // the last crossfade is over
                    adoptPendingKernel();
                }

                convolver.process (buffer.getArrayOfWritePointers(), channels, numSamples);
            }

            if (targetLayout != layout)
            {
                // Fade out on the old layout, then switch for the next block.
                buffer.applyGainRamp (0, numSamples, 1.0f, 0.0f);
                switchLayout();
            }
        }

        // Delays the sidechain by the stage's latency.
        void processSidechain (juce::AudioBuffer<float>& sidechain) noexcept
        {
            sidechainDelay.process (sidechain.getArrayOfWritePointers(), sidechain.getNumChannels(),
                                    sidechain.getNumSamples(), layout.latency());
        }

    private:
        struct Request
        {
            std::array<BandSettings, maxBands> bands {};
            PhaseLayout layout;
            juce::uint32 layoutId = 0;
        };

        void writeRequest (const std::array<BandSettings, maxBands>& bands) noexcept
        {
            request.bands = bands;
            request.layout = layout;
            request.layoutId = layoutId;
            requestSerial.fetch_add (1);
            bandsSent = bands;
            requestDirty = false;
        }

        static bool sameBands (const std::array<BandSettings, maxBands>& x, const std::array<BandSettings, maxBands>& y) noexcept
        {
            for (size_t i = 0; i < x.size(); ++i)
            {
                const auto& a = x[i];
                const auto& b = y[i];

                if (a.enabled != b.enabled)
                    return false;

                if (a.enabled && (a.type != b.type || a.slopeIndex != b.slopeIndex || a.placement != b.placement
                                  || ! juce::exactlyEqual (a.frequency, b.frequency)
                                  || ! juce::exactlyEqual (a.gainDb, b.gainDb)
                                  || ! juce::exactlyEqual (a.q, b.q)))
                    return false;
            }

            return true;
        }

        int fadeSamples() const noexcept { return std::max (1, (int) (0.01 * sampleRate)); }

        void switchLayout() noexcept
        {
            layout = targetLayout;
            ++layoutId;
            convolver.reset();
            convolver.clearKernel();
            activeSlot.store (-1);
            previousSlot.store (-1);
            sidechainDelay.reset();
            gain = 0.0f;
            fadeIn = true;
            requestDirty = layout.mode != PhaseMode::zeroLatency; // republish the bands for the new layout
        }

        // Audio thread: switch to a finished kernel if there is one for the
        // current layout.
        void adoptPendingKernel() noexcept
        {
            const auto slot = pendingSlot.exchange (-1);

            if (slot < 0)
                return;

            if (slots[(size_t) slot].layoutId != layoutId)
                return; // designed for a layout we've left

            const auto old = activeSlot.load();
            previousSlot.store (old);
            activeSlot.store (slot);
            convolver.setKernel (&slots[(size_t) slot]);
        }

        // Designer thread, or the audio thread when offline: build a kernel
        // into a slot the audio thread isn't using, then publish it.
        void produce (const Request& r)
        {
            const juce::ScopedLock lock (producerLock);

            int slot = -1;
            const auto active = activeSlot.load();
            const auto previous = previousSlot.load();

            for (int s = 0; s < (int) slots.size() && slot < 0; ++s)
                if (s != active && s != previous && s != lastPublished)
                    slot = s;

            if (slot < 0)
                return;

            PhaseKernel kernel;
            designPhaseKernel (r.bands, r.layout, sampleRate, numChannels, kernel);

            if (designFft == nullptr || designFftPartition != r.layout.partition)
            {
                designFft = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 ((double) r.layout.partition)) + 1);
                designFftPartition = r.layout.partition;
            }

            auto& target = slots[(size_t) slot];
            target.build (kernel, r.layout.partition, *designFft, designScratch);
            target.layoutId = r.layoutId;

            lastPublished = slot;
            pendingSlot.store (slot);
        }

        void run() override
        {
            while (! threadShouldExit())
            {
                wait (3);

                if (inlineDesign.load() || requestSerial.load() == designedSerial.load())
                    continue;

                Request r;

                {
                    const juce::SpinLock::ScopedLockType lock (requestLock);
                    r = request;
                    designedSerial.store (requestSerial.load());
                }

                produce (r);
            }
        }

        double sampleRate = 48000.0;
        int numChannels = 2;

        PhaseLayout layout, targetLayout;
        juce::uint32 layoutId = 0;
        float gain = 1.0f;
        bool fadeIn = false;

        PartitionedConvolver convolver;
        SidechainDelay sidechainDelay;

        // Four slots: the active kernel, the one being faded out, the one
        // waiting to be picked up, and one to design into.
        std::array<PartitionedKernel, 4> slots;
        std::atomic<int> activeSlot { -1 }, previousSlot { -1 }, pendingSlot { -1 };
        int lastPublished = -1;

        juce::SpinLock requestLock;
        Request request;
        std::array<BandSettings, maxBands> bandsSent {};
        bool requestDirty = false;
        std::atomic<juce::uint32> requestSerial { 0 }, designedSerial { 0 };
        std::atomic<bool> inlineDesign { false };

        juce::CriticalSection producerLock;
        std::unique_ptr<juce::dsp::FFT> designFft;
        int designFftPartition = 0;
        std::vector<float> designScratch;
    };
}
