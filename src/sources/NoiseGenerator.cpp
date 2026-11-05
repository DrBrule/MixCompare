#include "sources/NoiseGenerator.h"

#include <cmath>

namespace mixcompare
{

void NoiseGenerator::prepare (double sampleRate)
{
    // The leak keeps brown noise free of DC and stays out of the way above about 10 Hz.
    brownLeak = (float) std::exp (-juce::MathConstants<double>::twoPi * 10.0 / sampleRate);

    // The filters' output levels depend on the sample rate, so they're measured rather than
    // derived. This runs from prepare, off the audio thread.
    const auto target = juce::Decibels::decibelsToGain (targetRmsDb);

    for (const auto colour : { NoiseColour::white, NoiseColour::pink, NoiseColour::brown })
    {
        ChannelState state;
        juce::Random random (0xca11b);
        double energy = 0.0;
        constexpr int length = 1 << 19;

        for (int i = 0; i < length; ++i)
        {
            const auto sample = (double) next (state, random, colour);
            energy += sample * sample;
        }

        gains[(size_t) colour] = target / (float) std::sqrt (energy / length);
    }

    states = {};
}

float NoiseGenerator::next (ChannelState& state, juce::Random& random, NoiseColour colour) const noexcept
{
    const auto white = random.nextFloat() * 2.0f - 1.0f;

    if (colour == NoiseColour::white)
        return white;

    if (colour == NoiseColour::brown)
    {
        state.brown = state.brown * brownLeak + white;
        return state.brown;
    }

    // Paul Kellet's pink noise filter.
    auto& b = state.pink;
    b[0] = 0.99886f * b[0] + white * 0.0555179f;
    b[1] = 0.99332f * b[1] + white * 0.0750759f;
    b[2] = 0.96900f * b[2] + white * 0.1538520f;
    b[3] = 0.86650f * b[3] + white * 0.3104856f;
    b[4] = 0.55000f * b[4] + white * 0.5329522f;
    b[5] = -0.7616f * b[5] - white * 0.0168980f;
    const auto pink = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + white * 0.5362f;
    b[6] = white * 0.115926f;
    return pink;
}

void NoiseGenerator::process (float* const* channels, int numSamples, NoiseColour colour) noexcept
{
    const auto gain = gains[(size_t) colour];

    for (size_t channel = 0; channel < 2; ++channel)
        for (int i = 0; i < numSamples; ++i)
            channels[channel][i] = gain * next (states[channel], randoms[channel], colour);
}

} // namespace mixcompare
