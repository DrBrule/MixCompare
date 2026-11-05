#include "analysis/SpectrumAnalyser.h"

namespace mixcompare
{

namespace
{
constexpr double displaySeconds = 0.15;
constexpr double averageSeconds = 30.0;
constexpr double audibleFramePower = 1.0e-8;
constexpr long long minFramesForAverage = 40;
} // namespace

SpectrumAnalyser::SpectrumAnalyser (int initialOrder)
{
    for (auto& channel : history)
        channel.assign ((size_t) historySize, 0.0f);

    configure (juce::jlimit (minOrder, maxOrder, initialOrder));
}

void SpectrumAnalyser::prepare (double sampleRate) noexcept
{
    rate = sampleRate;
    updateTiming();
}

void SpectrumAnalyser::setOrder (int newOrder)
{
    newOrder = juce::jlimit (minOrder, maxOrder, newOrder);

    if (newOrder == order)
        return;

    configure (newOrder);

    // Without this, the display would be blank until the next frame, which can be a while.
    if (samplesSeen > 0)
    {
        analyseFrame();
        const juce::SpinLock::ScopedLockType lock (resultLock);
        displayPower = framePower;
    }
}

void SpectrumAnalyser::configure (int newOrder)
{
    order = newOrder;
    size = 1 << order;
    numBins = size / 2 + 1;
    hopSize = size / overlapFactor;
    samplesSinceFrame = 0;
    framesAveraged = 0;

    fft = std::make_unique<juce::dsp::FFT> (order);
    window.resize ((size_t) size);
    workspace.assign ((size_t) size * 2, 0.0f);
    framePower.assign ((size_t) numBins, 0.0);

    auto sum = 0.0;

    for (size_t i = 0; i < window.size(); ++i)
    {
        window[i] = (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * (double) i / (double) size));
        sum += window[i];
    }

    amplitudeScale = (float) (2.0 / sum);

    std::vector<double> display ((size_t) numBins, 0.0), average ((size_t) numBins, 0.0);

    {
        const juce::SpinLock::ScopedLockType lock (resultLock);
        displayPower.swap (display);
        averagePower.swap (average);
        publishedFrames = 0;
    }

    updateTiming();
}

void SpectrumAnalyser::updateTiming() noexcept
{
    const auto seconds = hopSize / rate;
    displayAlpha = 1.0 - std::exp (-seconds / displaySeconds);
    averageAlpha = seconds / averageSeconds;

    const juce::SpinLock::ScopedLockType lock (resultLock);
    binHz = rate / size;
    hopSeconds = seconds;
}

void SpectrumAnalyser::push (const float* left, const float* right, int numSamples) noexcept
{
    if (resetRequested.exchange (false))
    {
        framesAveraged = 0;
        const juce::SpinLock::ScopedLockType lock (resultLock);
        std::fill (averagePower.begin(), averagePower.end(), 0.0);
        publishedFrames = 0;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        history[0][(size_t) writeIndex] = left[i];
        history[1][(size_t) writeIndex] = right[i];
        writeIndex = (writeIndex + 1) & (historySize - 1);
        samplesSeen = juce::jmin (samplesSeen + 1, historySize);

        if (++samplesSinceFrame == hopSize)
        {
            samplesSinceFrame = 0;
            analyseFrame();
        }
    }
}

void SpectrumAnalyser::analyseFrame() noexcept
{
    std::fill (framePower.begin(), framePower.end(), 0.0);

    for (size_t channel = 0; channel < 2; ++channel)
    {
        const auto oldest = (size_t) (writeIndex - size);

        for (size_t i = 0; i < (size_t) size; ++i)
            workspace[i] = history[channel][(oldest + i) & (historySize - 1)] * window[i];

        std::fill (workspace.begin() + size, workspace.end(), 0.0f);
        fft->performFrequencyOnlyForwardTransform (workspace.data(), true);

        for (size_t bin = 0; bin < (size_t) numBins; ++bin)
        {
            const auto amplitude = (double) (workspace[bin] * amplitudeScale);
            framePower[bin] += 0.5 * amplitude * amplitude;
        }
    }

    // A window part-filled at the start would read low, and silence says nothing about a
    // source's balance, so neither is allowed into the average.
    auto total = 0.0;

    for (const auto power : framePower)
        total += power;

    const auto addToAverage = samplesSeen >= size && total > audibleFramePower;
    const auto alpha = addToAverage ? juce::jmax (1.0 / (double) ++framesAveraged, averageAlpha) : 0.0;

    const juce::SpinLock::ScopedLockType lock (resultLock);

    for (size_t bin = 0; bin < (size_t) numBins; ++bin)
    {
        displayPower[bin] += displayAlpha * (framePower[bin] - displayPower[bin]);
        averagePower[bin] += alpha * (framePower[bin] - averagePower[bin]);
    }

    publishedFrames = framesAveraged;
}

double SpectrumAnalyser::getDisplayDb (Spectrum& out) const
{
    const juce::SpinLock::ScopedLockType lock (resultLock);
    out.resize (displayPower.size());

    for (size_t bin = 0; bin < out.size(); ++bin)
        out[bin] = powerToDb (displayPower[bin]);

    return binHz;
}

double SpectrumAnalyser::getAverageDb (Spectrum& out) const
{
    const juce::SpinLock::ScopedLockType lock (resultLock);
    out.resize (averagePower.size());

    for (size_t bin = 0; bin < out.size(); ++bin)
        out[bin] = powerToDb (averagePower[bin]);

    return binHz;
}

BandLevels SpectrumAnalyser::getAverageBands() const noexcept
{
    const juce::SpinLock::ScopedLockType lock (resultLock);
    BandLevels levels {};

    for (size_t band = 0; band < frequencyBands.size(); ++band)
    {
        const auto first = (size_t) std::ceil (frequencyBands[band].lowHz / binHz);
        const auto last = juce::jmin ((size_t) std::floor (frequencyBands[band].highHz / binHz), averagePower.size() - 1);
        auto sum = 0.0;

        for (auto bin = first; bin <= last; ++bin)
            sum += averagePower[bin];

        levels[band] = last >= first ? powerToDb (sum / (double) (last - first + 1)) : silenceDb;
    }

    return levels;
}

bool SpectrumAnalyser::hasAverage() const noexcept
{
    const juce::SpinLock::ScopedLockType lock (resultLock);
    return publishedFrames >= minFramesForAverage;
}

double SpectrumAnalyser::getAverageSeconds() const noexcept
{
    const juce::SpinLock::ScopedLockType lock (resultLock);
    return (double) publishedFrames * hopSeconds;
}

double SpectrumAnalyser::getBinHz() const noexcept
{
    const juce::SpinLock::ScopedLockType lock (resultLock);
    return binHz;
}

} // namespace mixcompare
