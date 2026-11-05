#pragma once

#include "Parameters.h"

#include <array>

namespace mixcompare
{

// Decorrelated stereo white, pink and brown noise, each calibrated to the same RMS level so
// that switching colour changes the tone and not the loudness.
class NoiseGenerator
{
public:
    static constexpr float targetRmsDb = -18.0f;

    void prepare (double sampleRate);
    void process (float* const* channels, int numSamples, NoiseColour colour) noexcept;

private:
    struct ChannelState
    {
        std::array<float, 7> pink {};
        float brown = 0.0f;
    };

    float next (ChannelState& state, juce::Random& random, NoiseColour colour) const noexcept;

    std::array<ChannelState, 2> states;
    std::array<juce::Random, 2> randoms { juce::Random (0x5eed01), juce::Random (0x5eed02) };
    std::array<float, 3> gains { 1.0f, 1.0f, 1.0f };
    float brownLeak = 0.999f;
};

} // namespace mixcompare
