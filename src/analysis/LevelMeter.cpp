#include "analysis/LevelMeter.h"

#include <juce_core/juce_core.h>

namespace mixcompare
{

namespace
{
// Below this a channel counts as silent and has no meaningful crest factor.
constexpr float audibleDb = -80.0f;
} // namespace

void LevelMeter::prepare (double sampleRate) noexcept
{
    samplesPerSegment = juce::roundToInt (sampleRate * 0.05);
    clear();
}

void LevelMeter::clear() noexcept
{
    ring = {};
    current = {};
    samplesInSegment = 0;
    writeIndex = 0;
    segmentsSeen = 0;
    crestSum = 0.0;
    crestCount = 0;

    for (size_t channel = 0; channel < 2; ++channel)
    {
        rmsDb[channel].store (silenceDb);
        crestDb[channel].store (0.0f);
    }

    averageCrestDb.store (0.0f);
}

void LevelMeter::process (const float* left, const float* right, int numSamples) noexcept
{
    if (resetRequested.exchange (false))
        clear();

    for (int i = 0; i < numSamples; ++i)
    {
        const float samples[] { left[i], right[i] };

        for (size_t channel = 0; channel < 2; ++channel)
        {
            current.energy[channel] += (double) samples[channel] * (double) samples[channel];
            current.peak[channel] = juce::jmax (current.peak[channel], std::abs (samples[channel]));
        }

        if (++samplesInSegment == samplesPerSegment)
            finishSegment();
    }
}

void LevelMeter::finishSegment() noexcept
{
    for (auto& energy : current.energy)
        energy /= samplesPerSegment;

    ring[(size_t) writeIndex] = current;
    writeIndex = (writeIndex + 1) % crestSegments;
    segmentsSeen = juce::jmin (segmentsSeen + 1, crestSegments);
    current = {};
    samplesInSegment = 0;

    for (size_t channel = 0; channel < 2; ++channel)
    {
        auto shortEnergy = 0.0, longEnergy = 0.0;
        auto peak = 0.0f;

        for (int i = 1; i <= segmentsSeen; ++i)
        {
            const auto& segment = ring[(size_t) ((writeIndex - i + crestSegments) % crestSegments)];
            longEnergy += segment.energy[channel];
            peak = juce::jmax (peak, segment.peak[channel]);

            if (i <= rmsSegments)
                shortEnergy += segment.energy[channel];
        }

        rmsDb[channel].store (powerToDb (shortEnergy / juce::jmin (segmentsSeen, rmsSegments)));

        const auto longDb = powerToDb (longEnergy / segmentsSeen);
        const auto audible = longDb > audibleDb;
        const auto crest = audible ? 20.0f * std::log10 (peak) - longDb : 0.0f;
        crestDb[channel].store (crest);

        if (audible)
        {
            crestSum += (double) crest;
            ++crestCount;
            averageCrestDb.store ((float) (crestSum / (double) crestCount));
        }
    }
}

} // namespace mixcompare
