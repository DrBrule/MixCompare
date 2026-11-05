#pragma once

#include "analysis/AnalysisCommon.h"

#include <array>
#include <atomic>

namespace mixcompare
{

// RMS over 300 ms, crest factor (peak over RMS) over 3 s, per channel. Crest needs the longer
// window, because over 300 ms a sustained passage looks far more dynamic than it is.
class LevelMeter
{
public:
    void prepare (double sampleRate) noexcept;
    void process (const float* left, const float* right, int numSamples) noexcept;

    // Safe from any thread. Takes effect at the next process call.
    void requestReset() noexcept { resetRequested.store (true); }

    float getRmsDb (int channel) const noexcept { return rmsDb[(size_t) channel].load(); }
    float getCrestDb (int channel) const noexcept { return crestDb[(size_t) channel].load(); }

    // The crest factor averaged over everything audible since the last reset, in dB. Steadier
    // than the live reading, so it's what two sources should be compared by.
    float getAverageCrestDb() const noexcept { return averageCrestDb.load(); }

private:
    struct Segment
    {
        std::array<double, 2> energy {};
        std::array<float, 2> peak {};
    };

    static constexpr int rmsSegments = 6;    // 300 ms in 50 ms segments
    static constexpr int crestSegments = 60; // 3 s in 50 ms segments

    void clear() noexcept;
    void finishSegment() noexcept;

    std::array<Segment, crestSegments> ring {};
    Segment current;
    int samplesPerSegment = 2400;
    int samplesInSegment = 0;
    int writeIndex = 0;
    int segmentsSeen = 0;

    double crestSum = 0.0;
    long long crestCount = 0;

    std::array<std::atomic<float>, 2> rmsDb { silenceDb, silenceDb };
    std::array<std::atomic<float>, 2> crestDb { 0.0f, 0.0f };
    std::atomic<float> averageCrestDb { 0.0f };
    std::atomic<bool> resetRequested { false };
};

} // namespace mixcompare
