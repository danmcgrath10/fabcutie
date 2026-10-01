#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace fabcutie::ui
{
    // How the spectrum analyzer looks. Saved with the session as properties
    // of the plugin state (not parameters: none of this affects the sound).
    struct AnalyzerSettings
    {
        bool showPre = true;
        bool showPost = true;
        bool showExternal = true; // only drawn while a sidechain is connected
        bool freeze = false;      // not saved
        bool collisions = true;
        bool spectrumGrab = true;

        int rangeIndex = 1;
        int speedIndex = 2;
        int tiltIndex = 3;
        int resolutionIndex = 2;

        // Display range: how many dB below the top of the graph are shown.
        static constexpr std::array<float, 3> rangesDb { 60.0f, 90.0f, 120.0f };

        // Release time of the smoothing, in seconds.
        static constexpr std::array<float, 5> releaseSeconds { 2.0f, 0.9f, 0.4f, 0.16f, 0.06f };
        static inline const juce::StringArray speedNames { "Very slow", "Slow", "Medium", "Fast", "Very fast" };

        // Tilt around 1 kHz, so pink noise (and most music) looks level.
        static constexpr std::array<float, 5> tiltsDbPerOct { 0.0f, 1.5f, 3.0f, 4.5f, 6.0f };

        // FFT order: 2048 to 16384 points.
        static constexpr std::array<int, 4> fftOrders { 11, 12, 13, 14 };
        static inline const juce::StringArray resolutionNames { "Low", "Medium", "High", "Maximum" };

        float getRangeDb() const noexcept      { return rangesDb[clampIndex (rangeIndex, rangesDb.size())]; }
        float getReleaseSeconds() const noexcept { return releaseSeconds[clampIndex (speedIndex, releaseSeconds.size())]; }
        float getTiltDbPerOct() const noexcept { return tiltsDbPerOct[clampIndex (tiltIndex, tiltsDbPerOct.size())]; }
        int getFftOrder() const noexcept       { return fftOrders[clampIndex (resolutionIndex, fftOrders.size())]; }

        bool anySpectrum() const noexcept { return showPre || showPost || showExternal; }

        void load (const juce::ValueTree& tree)
        {
            const AnalyzerSettings defaults;
            showPre         = tree.getProperty (ids::pre, defaults.showPre);
            showPost        = tree.getProperty (ids::post, defaults.showPost);
            showExternal    = tree.getProperty (ids::external, defaults.showExternal);
            collisions      = tree.getProperty (ids::collisions, defaults.collisions);
            spectrumGrab    = tree.getProperty (ids::grab, defaults.spectrumGrab);
            rangeIndex      = tree.getProperty (ids::range, defaults.rangeIndex);
            speedIndex      = tree.getProperty (ids::speed, defaults.speedIndex);
            tiltIndex       = tree.getProperty (ids::tilt, defaults.tiltIndex);
            resolutionIndex = tree.getProperty (ids::resolution, defaults.resolutionIndex);
        }

        void save (juce::ValueTree& tree) const
        {
            tree.setProperty (ids::pre, showPre, nullptr);
            tree.setProperty (ids::post, showPost, nullptr);
            tree.setProperty (ids::external, showExternal, nullptr);
            tree.setProperty (ids::collisions, collisions, nullptr);
            tree.setProperty (ids::grab, spectrumGrab, nullptr);
            tree.setProperty (ids::range, rangeIndex, nullptr);
            tree.setProperty (ids::speed, speedIndex, nullptr);
            tree.setProperty (ids::tilt, tiltIndex, nullptr);
            tree.setProperty (ids::resolution, resolutionIndex, nullptr);
        }

    private:
        static size_t clampIndex (int index, size_t size) noexcept
        {
            return (size_t) juce::jlimit (0, (int) size - 1, index);
        }

        struct ids
        {
            static inline const juce::Identifier pre        { "analyzerPre" };
            static inline const juce::Identifier post       { "analyzerPost" };
            static inline const juce::Identifier external   { "analyzerExternal" };
            static inline const juce::Identifier collisions { "analyzerCollisions" };
            static inline const juce::Identifier grab       { "analyzerGrab" };
            static inline const juce::Identifier range      { "analyzerRange" };
            static inline const juce::Identifier speed      { "analyzerSpeed" };
            static inline const juce::Identifier tilt       { "analyzerTilt" };
            static inline const juce::Identifier resolution { "analyzerResolution" };
        };
    };
}
