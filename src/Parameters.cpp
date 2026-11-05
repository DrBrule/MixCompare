#include "Parameters.h"

#include "analysis/SpectrumAnalyser.h"

namespace mixcompare
{

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    constexpr int version = 1;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { params::monitor, version },
                                                              "Monitor",
                                                              juce::StringArray { "Mix", "Comparison" },
                                                              0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { params::comparisonSource, version },
                                                              "Comparison Source",
                                                              juce::StringArray { "File", "Noise", "Sidechain" },
                                                              0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { params::noiseColour, version },
                                                              "Noise Colour",
                                                              juce::StringArray { "White", "Pink", "Brown" },
                                                              1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { params::comparisonTrim, version },
                                                             "Comparison Trim",
                                                             juce::NormalisableRange<float> (-36.0f, 12.0f, 0.1f),
                                                             0.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { params::followHost, version },
                                                            "Follow Host Transport",
                                                            true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { params::stereoMode, version },
                                                              "Stereo Display",
                                                              juce::StringArray { "Goniometer", "Lissajous", "Stereometer" },
                                                              0));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { params::fftOrder, 2 },
                                                           "FFT Resolution",
                                                           SpectrumAnalyser::minOrder,
                                                           SpectrumAnalyser::maxOrder,
                                                           SpectrumAnalyser::defaultOrder,
                                                           juce::AudioParameterIntAttributes().withStringFromValueFunction (
                                                               [] (int order, int) { return juce::String (1 << order) + " points"; })));
    return layout;
}

} // namespace mixcompare
