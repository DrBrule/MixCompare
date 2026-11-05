#include "analysis/LoudnessMeter.h"

#include <juce_core/juce_core.h>

namespace mixcompare
{

namespace
{
constexpr double loudnessOffset = -0.691;
constexpr double absoluteGate = -70.0;
constexpr double relativeGate = -10.0;

double energyToLufs (double energy) noexcept
{
    return energy > 0.0 ? loudnessOffset + 10.0 * std::log10 (energy) : -std::numeric_limits<double>::infinity();
}

double lufsToEnergy (double lufs) noexcept
{
    return std::pow (10.0, (lufs - loudnessOffset) / 10.0);
}

float toReading (double energy) noexcept
{
    return juce::jmax (silenceDb, (float) energyToLufs (energy));
}
} // namespace

void LoudnessMeter::prepare (double sampleRate) noexcept
{
    // The K-weighting filters. BS.1770 only lists coefficients for 48 kHz, so these are the
    // analogue prototypes behind them, which give the same result at every other rate.
    {
        const auto k = std::tan (juce::MathConstants<double>::pi * 1681.974450955533 / sampleRate);
        const auto q = 0.7071752369554196;
        const auto vh = std::pow (10.0, 3.999843853973347 / 20.0);
        const auto vb = std::pow (vh, 0.4996667741545416);
        const auto a0 = 1.0 + k / q + k * k;

        shelf.b0 = (vh + vb * k / q + k * k) / a0;
        shelf.b1 = 2.0 * (k * k - vh) / a0;
        shelf.b2 = (vh - vb * k / q + k * k) / a0;
        shelf.a1 = 2.0 * (k * k - 1.0) / a0;
        shelf.a2 = (1.0 - k / q + k * k) / a0;
    }

    {
        const auto k = std::tan (juce::MathConstants<double>::pi * 38.13547087602444 / sampleRate);
        const auto q = 0.5003270373238773;
        const auto a0 = 1.0 + k / q + k * k;

        highPass.b0 = 1.0;
        highPass.b1 = -2.0;
        highPass.b2 = 1.0;
        highPass.a1 = 2.0 * (k * k - 1.0) / a0;
        highPass.a2 = (1.0 - k / q + k * k) / a0;
    }

    samplesPerStep = juce::roundToInt (sampleRate * 0.1);
    clear();
}

void LoudnessMeter::clear() noexcept
{
    filterStates = {};
    samplesInStep = 0;
    stepEnergy = 0.0;
    steps = {};
    stepIndex = 0;
    stepsSeen = 0;
    binEnergy = {};
    binCount = {};
    momentary.store (silenceDb);
    shortTerm.store (silenceDb);
    integrated.store (silenceDb);
}

void LoudnessMeter::process (const float* const* channels, int numChannels, int numSamples) noexcept
{
    if (resetRequested.exchange (false))
        clear();

    numChannels = juce::jmin (numChannels, maxChannels);

    const auto run = [] (const Biquad& f, FilterState& s, double in) noexcept
    {
        const auto out = f.b0 * in + s.z1;
        s.z1 = f.b1 * in - f.a1 * out + s.z2;
        s.z2 = f.b2 * in - f.a2 * out;
        return out;
    };

    for (int i = 0; i < numSamples; ++i)
    {
        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto& states = filterStates[(size_t) channel];
            const auto weighted = run (highPass, states[1], run (shelf, states[0], (double) channels[channel][i]));
            stepEnergy += weighted * weighted;
        }

        if (++samplesInStep == samplesPerStep)
            finishStep();
    }
}

double LoudnessMeter::meanOfLastSteps (int count) const noexcept
{
    auto sum = 0.0;

    for (int i = 1; i <= count; ++i)
        sum += steps[(size_t) ((stepIndex - i + stepsPerShortTerm) % stepsPerShortTerm)];

    return sum / count;
}

void LoudnessMeter::finishStep() noexcept
{
    steps[(size_t) stepIndex] = stepEnergy / samplesPerStep;
    stepIndex = (stepIndex + 1) % stepsPerShortTerm;
    stepsSeen = juce::jmin (stepsSeen + 1, stepsPerShortTerm);
    samplesInStep = 0;
    stepEnergy = 0.0;

    if (stepsSeen >= stepsPerMomentary)
    {
        const auto block = meanOfLastSteps (stepsPerMomentary);
        momentary.store (toReading (block));
        addGatingBlock (block);
    }

    if (stepsSeen >= stepsPerShortTerm)
        shortTerm.store (toReading (meanOfLastSteps (stepsPerShortTerm)));
}

void LoudnessMeter::addGatingBlock (double energy) noexcept
{
    const auto lufs = energyToLufs (energy);

    if (lufs <= absoluteGate)
        return;

    const auto bin = juce::jlimit (0, numHistogramBins - 1, (int) ((lufs - absoluteGate) * 10.0));
    binEnergy[(size_t) bin] += energy;
    ++binCount[(size_t) bin];

    integrated.store (computeIntegrated());
}

float LoudnessMeter::computeIntegrated() const noexcept
{
    auto energySum = 0.0;
    juce::int64 countSum = 0;

    for (size_t bin = 0; bin < numHistogramBins; ++bin)
    {
        energySum += binEnergy[bin];
        countSum += binCount[bin];
    }

    if (countSum == 0)
        return silenceDb;

    const auto threshold = lufsToEnergy (energyToLufs (energySum / (double) countSum) + relativeGate);
    energySum = 0.0;
    countSum = 0;

    for (size_t bin = 0; bin < numHistogramBins; ++bin)
    {
        // Each bin's own average decides which side of the gate it falls on.
        if (binCount[bin] > 0 && binEnergy[bin] / binCount[bin] >= threshold)
        {
            energySum += binEnergy[bin];
            countSum += binCount[bin];
        }
    }

    return countSum > 0 ? toReading (energySum / (double) countSum) : silenceDb;
}

} // namespace mixcompare
