#pragma once

#include "analysis/AnalysisCommon.h"

#include <juce_dsp/juce_dsp.h>

#include <memory>
#include <vector>

namespace mixcompare
{

// The FFT. Keeps a fast-moving spectrum to display and a slow average to compare sources by.
// Left and right are transformed separately and their power averaged, because summing to mono
// first would hide anything out of phase between them. Levels are scaled so that a full-scale
// sine in both channels peaks at 0 dB, whatever the size, which means that noise reads about
// 3 dB lower each time the size doubles. Fed from the analysis thread, never the audio thread.
class SpectrumAnalyser
{
public:
    static constexpr int minOrder = 11, maxOrder = 16, defaultOrder = 13;
    static constexpr int defaultSize = 1 << defaultOrder;
    static constexpr int overlapFactor = 8;

    using Spectrum = std::vector<float>;

    explicit SpectrumAnalyser (int order = defaultOrder);

    void prepare (double sampleRate) noexcept;
    void push (const float* left, const float* right, int numSamples) noexcept;

    // Analysis thread only, as this allocates. Keeps the recent audio, so the display carries
    // on straight away, but starts the average again as its bins no longer mean the same thing.
    void setOrder (int newOrder);

    // Safe from any thread. Takes effect at the next push. Clears the average only.
    void requestReset() noexcept { resetRequested.store (true); }

    // Both return the width of a bin in Hz at the moment of the copy, so it always matches.
    double getDisplayDb (Spectrum& out) const;
    double getAverageDb (Spectrum& out) const;
    BandLevels getAverageBands() const noexcept;

    // Whether enough audio has gone by for the average to mean anything.
    bool hasAverage() const noexcept;

    // How much audio has gone into the average since it last started.
    double getAverageSeconds() const noexcept;

    double getBinHz() const noexcept;

private:
    void configure (int newOrder);
    void updateTiming() noexcept;
    void analyseFrame() noexcept;

    static constexpr int historySize = 1 << maxOrder;

    double rate = 44100.0;
    int order = 0, size = 0, numBins = 0, hopSize = 0;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, workspace;
    std::vector<double> framePower;
    float amplitudeScale = 1.0f;

    // Always the largest size, so that a change of size has the audio it needs already.
    std::array<std::vector<float>, 2> history;
    int writeIndex = 0;
    int samplesSinceFrame = 0;
    int samplesSeen = 0;

    double displayAlpha = 0.3, averageAlpha = 0.001;
    long long framesAveraged = 0;
    std::atomic<bool> resetRequested { false };

    mutable juce::SpinLock resultLock;
    std::vector<double> displayPower, averagePower;
    double binHz = 1.0, hopSeconds = 0.0;
    long long publishedFrames = 0;
};

} // namespace mixcompare
