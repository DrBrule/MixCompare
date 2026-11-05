#pragma once

#include "analysis/LevelMeter.h"
#include "analysis/LoudnessMeter.h"
#include "analysis/SpectrumAnalyser.h"
#include "analysis/StereoField.h"

namespace mixcompare
{

// All the measurements of one audio source. There are two of these, one for the mix and one
// for the comparison, both always running: that's what lets the display and the advice compare
// them whichever is being listened to. Loudness and levels are cheap enough to measure right
// on the audio thread. The rest reaches the analysis thread through a lock-free queue.
class SourceAnalyser
{
public:
    SourceAnalyser();

    void prepare (double sampleRate) noexcept;
    void processAudio (const float* left, const float* right, int numChannels, int numSamples) noexcept;

    // Analysis thread.
    void drain() noexcept;

    // Analysis thread. Changes the display's resolution, and leaves the averages alone.
    void setDisplayOrder (int order) { displaySpectrum.setOrder (order); }

    // Starts the averages again, e.g. when the comparison changes to a different source.
    void requestReset() noexcept;

    const LoudnessMeter& getLoudness() const noexcept { return loudness; }
    const LevelMeter& getLevels() const noexcept { return levels; }
    const SpectrumAnalyser& getSpectrum() const noexcept { return spectrum; }
    const SpectrumAnalyser& getDisplaySpectrum() const noexcept { return displaySpectrum; }
    const StereoField& getStereo() const noexcept { return stereo; }

private:
    LoudnessMeter loudness;
    LevelMeter levels;
    StereoField stereo;

    // Two of these, so that moving the display's resolution can't disturb the averages that the
    // advice is based on. Larger sizes run less often, so the second one stays cheap.
    SpectrumAnalyser spectrum, displaySpectrum;

    static constexpr int queueSize = 1 << 17;
    juce::AbstractFifo queue { queueSize };
    juce::AudioBuffer<float> queueData { 2, queueSize };
};

} // namespace mixcompare
