#include "analysis/StereoField.h"

#include <cmath>

namespace mixcompare
{

namespace
{
constexpr double frameSeconds = 0.02;
constexpr double smoothingSeconds = 0.15;
constexpr double averageSeconds = 30.0;
constexpr double minEnergy = 1.0e-9;
constexpr float audibleLevel = 1.0e-4f; // -80 dBFS
} // namespace

void StereoField::Accumulator::add (double l, double r, double decay) noexcept
{
    ll = ll * decay + l * l;
    rr = rr * decay + r * r;
    lr = lr * decay + l * r;
}

float StereoField::Accumulator::getCorrelation() const noexcept
{
    const auto energy = ll * rr;
    return energy > minEnergy * minEnergy ? (float) (lr / std::sqrt (energy)) : 0.0f;
}

float StereoField::Accumulator::getSideRatio() const noexcept
{
    const auto total = ll + rr;
    return total > minEnergy ? (float) (0.5 - lr / total) : 0.0f;
}

float StereoField::Accumulator::getBalance() const noexcept
{
    const auto total = ll + rr;
    return total > minEnergy ? (float) ((rr - ll) / total) : 0.0f;
}

void StereoField::prepare (double sampleRate) noexcept
{
    frameLength = juce::roundToInt (frameSeconds * sampleRate);
    frameAlpha = (float) (1.0 - std::exp (-frameSeconds / smoothingSeconds));
    frameSamples = 0;
    frame = {};
    averageDecay = std::exp (-1.0 / (averageSeconds * sampleRate));
}

void StereoField::finishFrame() noexcept
{
    // Silence has no stereo image, so the readings settle back to neutral.
    const auto audible = (frame.ll + frame.rr) / (2.0 * frameSamples) > (double) audibleLevel * (double) audibleLevel;

    const auto moveTowards = [this] (std::atomic<float>& reading, float target)
    {
        const auto current = reading.load();
        reading.store (current + frameAlpha * (target - current));
    };

    moveTowards (correlation, audible ? frame.getCorrelation() : 0.0f);
    moveTowards (balance, audible ? frame.getBalance() : 0.0f);
    moveTowards (sideRatio, audible ? frame.getSideRatio() : 0.0f);

    frame = {};
    frameSamples = 0;
}

void StereoField::push (const float* left, const float* right, int numSamples) noexcept
{
    if (resetRequested.exchange (false))
    {
        average = {};
        averageCorrelation.store (0.0f);
        averageSideRatio.store (0.0f);
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const auto l = (double) left[i], r = (double) right[i];
        frame.add (l, r, 1.0);

        if (++frameSamples == frameLength)
            finishFrame();

        // Quiet passages are left out, so that a pause doesn't slowly erase what's been learned.
        if (std::abs (left[i]) > audibleLevel || std::abs (right[i]) > audibleLevel)
            average.add (l, r, averageDecay);
    }

    averageCorrelation.store (average.getCorrelation());
    averageSideRatio.store (average.getSideRatio());

    const juce::SpinLock::ScopedLockType lock (scopeLock);

    for (int i = 0; i < numSamples; ++i)
    {
        scope[(size_t) scopeWrite] = { left[i], right[i] };
        scopeWrite = (scopeWrite + 1) % scopeSize;
    }
}

void StereoField::copyScope (Scope& out) const noexcept
{
    const juce::SpinLock::ScopedLockType lock (scopeLock);

    for (int i = 0; i < scopeSize; ++i)
        out[(size_t) i] = scope[(size_t) ((scopeWrite + i) % scopeSize)];
}

juce::Point<float> StereoField::project (StereoMode mode, float left, float right) noexcept
{
    if (mode == StereoMode::lissajous)
        return { left, right };

    constexpr auto rootHalf = juce::MathConstants<float>::sqrt2 * 0.5f;
    return { (right - left) * rootHalf, (right + left) * rootHalf };
}

} // namespace mixcompare
