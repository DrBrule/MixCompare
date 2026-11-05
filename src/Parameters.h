#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace mixcompare
{

enum class MonitorSource { mix, comparison };
enum class ComparisonSource { file, noise, sidechain };
enum class NoiseColour { white, pink, brown };
enum class StereoMode { goniometer, lissajous, stereometer };

namespace params
{
inline constexpr auto monitor = "monitor";
inline constexpr auto comparisonSource = "comparisonSource";
inline constexpr auto noiseColour = "noiseColour";
inline constexpr auto comparisonTrim = "comparisonTrim";
inline constexpr auto followHost = "followHost";
inline constexpr auto stereoMode = "stereoMode";
inline constexpr auto fftOrder = "fftOrder";
} // namespace params

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace mixcompare
