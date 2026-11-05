#pragma once

#include "Parameters.h"

#include <array>
#include <atomic>

namespace mixcompare
{

// Everything the stereo display needs: the recent samples to plot, plus correlation (+1 mono,
// 0 unrelated, -1 out of phase) and side ratio (side energy as a share of the total: 0 mono,
// 0.5 unrelated, 1 out of phase). Fed from the analysis thread, never the audio thread.
//
// The live readings are worked out over short frames, then smoothed. Smoothing the energy
// instead would let anything loud outweigh what follows it for seconds, so the meters would seem
// stuck through quiet passages and after the audio stops. The averages do weight by energy, as
// they're meant to describe the overall sound.
class StereoField
{
public:
    static constexpr int scopeSize = 2048;

    using Scope = std::array<juce::Point<float>, scopeSize>;

    void prepare (double sampleRate) noexcept;
    void push (const float* left, const float* right, int numSamples) noexcept;

    // Safe from any thread. Takes effect at the next push. Clears the averages only.
    void requestReset() noexcept { resetRequested.store (true); }

    float getCorrelation() const noexcept { return correlation.load(); }

    // Which side is louder: -1 left only, 0 centred, +1 right only.
    float getBalance() const noexcept { return balance.load(); }
    float getSideRatio() const noexcept { return sideRatio.load(); }

    float getAverageCorrelation() const noexcept { return averageCorrelation.load(); }
    float getAverageSideRatio() const noexcept { return averageSideRatio.load(); }

    // Oldest first, as (left, right).
    void copyScope (Scope& out) const noexcept;

    // Where a (left, right) sample lands on the display, with +x right and +y up. Goniometer
    // is the classic 45 degree view: mono is a vertical line and left-only leans upper-left.
    // Lissajous is the same thing unrotated: left on x, right on y, and mono runs diagonally.
    // The stereometer has no plot, as it's drawn from the readings above.
    static juce::Point<float> project (StereoMode mode, float left, float right) noexcept;

private:
    struct Accumulator
    {
        double ll = 0.0, rr = 0.0, lr = 0.0;

        void add (double l, double r, double decay) noexcept;
        float getCorrelation() const noexcept;
        float getSideRatio() const noexcept;
        float getBalance() const noexcept;
    };

    void finishFrame() noexcept;

    Accumulator frame, average;
    double averageDecay = 0.99999;
    int frameLength = 960, frameSamples = 0;
    float frameAlpha = 0.125f;

    mutable juce::SpinLock scopeLock;
    Scope scope;
    int scopeWrite = 0;

    std::atomic<float> correlation { 0.0f }, balance { 0.0f }, sideRatio { 0.0f };
    std::atomic<float> averageCorrelation { 0.0f }, averageSideRatio { 0.0f };
    std::atomic<bool> resetRequested { false };
};

} // namespace mixcompare
