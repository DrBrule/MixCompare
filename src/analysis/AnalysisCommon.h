#pragma once

#include <array>
#include <cmath>

namespace mixcompare
{

// What every reading reports when there's nothing to measure.
inline constexpr float silenceDb = -100.0f;

inline float powerToDb (double power) noexcept
{
    return power > 1.0e-10 ? (float) (10.0 * std::log10 (power)) : silenceDb;
}

struct FrequencyBand
{
    const char* name;
    double lowHz, highHz;
};

inline constexpr std::array<FrequencyBand, 7> frequencyBands { {
    { "Sub", 20.0, 60.0 },
    { "Bass", 60.0, 200.0 },
    { "Low mids", 200.0, 500.0 },
    { "Mids", 500.0, 2000.0 },
    { "Upper mids", 2000.0, 5000.0 },
    { "Presence", 5000.0, 10000.0 },
    { "Air", 10000.0, 20000.0 },
} };

using BandLevels = std::array<float, frequencyBands.size()>;

} // namespace mixcompare
