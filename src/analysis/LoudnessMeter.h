#pragma once

#include "analysis/AnalysisCommon.h"

#include <array>
#include <atomic>

namespace mixcompare
{

// ITU-R BS.1770 / EBU R128 loudness. Momentary is the "instant" reading (400 ms), integrated
// is the gated "average" since the last reset, and short-term (3 s) sits between them.
// Everything runs on the audio thread and never allocates: gating uses a histogram of blocks
// rather than a list of them, so it costs the same however long the session runs.
class LoudnessMeter
{
public:
    static constexpr int maxChannels = 2;

    void prepare (double sampleRate) noexcept;
    void process (const float* const* channels, int numChannels, int numSamples) noexcept;

    // Safe from any thread. Takes effect at the next process call.
    void requestReset() noexcept { resetRequested.store (true); }

    float getMomentaryLufs() const noexcept { return momentary.load(); }
    float getShortTermLufs() const noexcept { return shortTerm.load(); }
    float getIntegratedLufs() const noexcept { return integrated.load(); }

private:
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    };

    struct FilterState
    {
        double z1 = 0.0, z2 = 0.0;
    };

    static constexpr int stepsPerMomentary = 4;  // 400 ms in 100 ms steps
    static constexpr int stepsPerShortTerm = 30; // 3 s in 100 ms steps
    static constexpr int numHistogramBins = 800; // 0.1 LU each, from -70 to +10 LUFS

    void clear() noexcept;
    void finishStep() noexcept;
    void addGatingBlock (double energy) noexcept;
    float computeIntegrated() const noexcept;
    double meanOfLastSteps (int count) const noexcept;

    Biquad shelf, highPass;
    std::array<std::array<FilterState, 2>, maxChannels> filterStates {};

    int samplesPerStep = 4800;
    int samplesInStep = 0;
    double stepEnergy = 0.0;

    std::array<double, stepsPerShortTerm> steps {};
    int stepIndex = 0;
    int stepsSeen = 0;

    std::array<double, numHistogramBins> binEnergy {};
    std::array<int, numHistogramBins> binCount {};

    std::atomic<float> momentary { silenceDb }, shortTerm { silenceDb }, integrated { silenceDb };
    std::atomic<bool> resetRequested { false };
};

} // namespace mixcompare
