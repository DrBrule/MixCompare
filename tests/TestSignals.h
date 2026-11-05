#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <vector>

namespace mixcompare::test
{

inline std::vector<float> makeSine (double frequency, float amplitude, double sampleRate, int length)
{
    std::vector<float> samples ((size_t) length);

    for (int i = 0; i < length; ++i)
        samples[(size_t) i] = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * frequency * i / sampleRate);

    return samples;
}

inline std::vector<float> makeNoise (float amplitude, int length, juce::int64 seed)
{
    juce::Random random (seed);
    std::vector<float> samples ((size_t) length);

    for (auto& sample : samples)
        sample = amplitude * (random.nextFloat() * 2.0f - 1.0f);

    return samples;
}

inline float rmsDb (const float* samples, int length)
{
    auto energy = 0.0;

    for (int i = 0; i < length; ++i)
        energy += (double) samples[i] * (double) samples[i];

    return juce::Decibels::gainToDecibels ((float) std::sqrt (energy / length), -200.0f);
}

} // namespace mixcompare::test
