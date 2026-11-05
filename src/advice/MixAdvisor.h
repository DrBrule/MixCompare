#pragma once

#include "analysis/AnalysisCommon.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace mixcompare
{

// What the advisor knows about one source. Deliberately plain values, so that the rules can
// be developed and tested without any audio running.
struct SourceProfile
{
    bool hasData = false;
    float integratedLufs = silenceDb;
    float crestDb = 0.0f;
    float correlation = 0.0f;
    float sideRatio = 0.0f;
    BandLevels bands {};
};

struct AdviceInput
{
    SourceProfile mix, comparison;
    bool comparisonIsNoise = false;
    float comparisonTrimDb = 0.0f; // Already part of the comparison's measured level.
};

struct Advice
{
    enum class Severity { note, minor, major };
    enum class Topic { status, tone, loudness, dynamics, stereo };

    Severity severity = Severity::note;
    Topic topic = Topic::status;
    juce::String text;
};

// First-pass rules comparing the two sources. Returns the most important differences first.
std::vector<Advice> getMixAdvice (const AdviceInput& input);

} // namespace mixcompare
