#include "LevelMeter.h"
#include "Theme.h"

namespace fabcutie::ui
{
    namespace
    {
        constexpr double holdSeconds = 1.5;
        constexpr float releaseDbPerSecond = 24.0f;
        constexpr float kneeDb = -20.0f;
        constexpr float kneeProportion = 0.3f;
    }

    LevelMeter::LevelMeter (dsp::PeakMeter& m) : meter (m)
    {
        setTooltip ("Output level. Click the number to reset the peak.");
        startTimerHz (30);
    }

    float LevelMeter::proportionForDb (float db) noexcept
    {
        db = juce::jlimit (minDb, maxDb, db);

        if (db >= kneeDb)
            return kneeProportion + (1.0f - kneeProportion) * (db - kneeDb) / (maxDb - kneeDb);

        return kneeProportion * (db - minDb) / (kneeDb - minDb);
    }

    void LevelMeter::timerCallback()
    {
        const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const auto dt = (float) juce::jlimit (0.0, 0.2, now - lastTick);
        lastTick = now;

        numChannels = juce::jlimit (1, dsp::PeakMeter::maxChannels, meter.getNumChannels());

        for (int c = 0; c < dsp::PeakMeter::maxChannels; ++c)
        {
            auto& ch = channels[(size_t) c];
            const auto peakDb = juce::Decibels::gainToDecibels (meter.takePeak (c), -100.0f);

            if (c >= numChannels)
                continue;

            // Instant attack, steady fall.
            ch.levelDb = std::max (peakDb, ch.levelDb - releaseDbPerSecond * dt);

            if (peakDb >= ch.holdDb)
            {
                ch.holdDb = peakDb;
                ch.holdUntil = now + holdSeconds;
            }
            else if (now > ch.holdUntil)
            {
                ch.holdDb = std::max (ch.levelDb, ch.holdDb - releaseDbPerSecond * dt);
            }

            maxPeakDb = std::max (maxPeakDb, peakDb);
        }

        repaint();
    }

    juce::Rectangle<float> LevelMeter::readoutArea() const
    {
        return getLocalBounds().toFloat().removeFromTop (20.0f).reduced (2.0f, 0.0f);
    }

    juce::Rectangle<float> LevelMeter::barArea() const
    {
        return getLocalBounds().toFloat().withTrimmedTop (28.0f).withTrimmedBottom (22.0f).withTrimmedLeft (20.0f).withTrimmedRight (4.0f);
    }

    void LevelMeter::mouseDown (const juce::MouseEvent& e)
    {
        if (readoutArea().contains (e.position))
        {
            maxPeakDb = -std::numeric_limits<float>::infinity();
            repaint();
        }
    }

    void LevelMeter::paint (juce::Graphics& g)
    {
        // Peak readout.
        const auto readout = readoutArea();
        const auto clipped = maxPeakDb > 0.0f;

        g.setColour (clipped ? colours::meterHigh.withAlpha (0.85f) : colours::control);
        g.fillRoundedRectangle (readout, 3.0f);
        g.setColour (clipped ? juce::Colours::white : colours::text);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (maxPeakDb > -100.0f ? juce::String (maxPeakDb, 1) : juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e")),
                    readout, juce::Justification::centred);

        const auto bars = barArea();

        // Scale.
        g.setFont (juce::FontOptions (9.5f));
        for (auto db : { 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f })
        {
            const auto y = bars.getBottom() - bars.getHeight() * proportionForDb (db);
            g.setColour (colours::gridMinor.withMultipliedAlpha (2.0f));
            g.drawHorizontalLine (juce::roundToInt (y), bars.getX(), bars.getRight());
            g.setColour (colours::gridText);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (0.0f, y - 6.0f, bars.getX() - 3.0f, 12.0f),
                        juce::Justification::centredRight);
        }

        // Bars, coloured by level: green, then yellow from -12 dB, red above 0.
        const auto gap = 2.0f;
        const auto barWidth = (bars.getWidth() - gap * (float) (numChannels - 1)) / (float) numChannels;

        const auto yFor = [&] (float db) { return bars.getBottom() - bars.getHeight() * proportionForDb (db); };

        juce::ColourGradient gradient (colours::meterLow, 0.0f, bars.getBottom(), colours::meterHigh, 0.0f, bars.getY(), false);
        gradient.addColour (proportionForDb (-12.0f), colours::meterLow);
        gradient.addColour (proportionForDb (-3.0f), colours::meterMid);
        gradient.addColour (proportionForDb (0.0f), colours::meterHigh);

        for (int c = 0; c < numChannels; ++c)
        {
            const auto& ch = channels[(size_t) c];
            const auto column = juce::Rectangle<float> (bars.getX() + (float) c * (barWidth + gap), bars.getY(), barWidth, bars.getHeight());

            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (column);

            const auto top = yFor (ch.levelDb);
            if (ch.levelDb > minDb)
            {
                g.setGradientFill (gradient);
                g.fillRect (column.withTop (top));
            }

            if (ch.holdDb > minDb)
            {
                g.setColour (ch.holdDb > 0.0f ? colours::meterHigh : colours::text.withAlpha (0.8f));
                g.fillRect (column.withY (yFor (ch.holdDb) - 1.0f).withHeight (2.0f));
            }
        }

        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText ("OUT", getLocalBounds().toFloat().removeFromBottom (18.0f).withTrimmedLeft (16.0f), juce::Justification::centred);
    }
}
