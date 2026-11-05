#include "advice/MixAdvisor.h"

#include <algorithm>

namespace mixcompare
{

namespace
{
constexpr float toneMinorDb = 2.0f, toneMajorDb = 4.0f;
constexpr float loudnessMinorLu = 1.0f, loudnessMajorLu = 3.0f;
constexpr float crestMinorDb = 2.0f, crestMajorDb = 4.0f;
constexpr float sideRatioMinor = 0.06f, sideRatioMajor = 0.15f;
constexpr float phaseProblemCorrelation = 0.0f;

constexpr std::array<const char*, frequencyBands.size()> tooMuchHints {
    "Check the sub and any low-end enhancers.",
    "Check the kick and bass levels.",
    "This range can make a mix sound muddy or boxy.",
    "This range can make a mix sound honky or crowded.",
    "This range can make a mix sound harsh.",
    "Check for sibilance and bright cymbals.",
    "Check that the top end isn't sounding brittle.",
};

constexpr std::array<const char*, frequencyBands.size()> tooLittleHints {
    "The mix may lack weight on large systems.",
    "The mix may lack punch and warmth.",
    "The mix may sound thin.",
    "Vocals and lead parts may lack body.",
    "Lead parts may not cut through.",
    "The mix may lack clarity and definition.",
    "The mix may lack openness.",
};

juce::String db (float value)
{
    return juce::String (std::abs (value), 1);
}

Advice::Severity severityOf (float difference, float minor, float major)
{
    const auto size = std::abs (difference);
    return size >= major ? Advice::Severity::major : size >= minor ? Advice::Severity::minor : Advice::Severity::note;
}

void addTone (const AdviceInput& input, std::vector<Advice>& advice)
{
    // Compared after removing the overall level difference, so this is about balance alone.
    auto offset = 0.0f;

    for (size_t band = 0; band < frequencyBands.size(); ++band)
        offset += input.mix.bands[band] - input.comparison.bands[band];

    offset /= (float) frequencyBands.size();

    auto anyDifference = false;

    for (size_t band = 0; band < frequencyBands.size(); ++band)
    {
        const auto difference = input.mix.bands[band] - input.comparison.bands[band] - offset;
        const auto severity = severityOf (difference, toneMinorDb, toneMajorDb);

        if (severity == Advice::Severity::note)
            continue;

        anyDifference = true;
        const auto& info = frequencyBands[band];
        const auto range = juce::String (info.lowHz, 0) + "-" + juce::String (info.highHz, 0) + " Hz";

        advice.push_back ({ severity,
                            Advice::Topic::tone,
                            juce::String (info.name) + " (" + range + ") is " + db (difference) + " dB "
                                + (difference > 0.0f ? "stronger" : "weaker") + " than the comparison, relative to the overall balance. "
                                + (difference > 0.0f ? tooMuchHints[band] : tooLittleHints[band]) });
    }

    if (! anyDifference)
        advice.push_back ({ Advice::Severity::note, Advice::Topic::tone, "The tonal balance is within 2 dB of the comparison in every band." });
}

void addLoudness (const AdviceInput& input, std::vector<Advice>& advice)
{
    if (input.mix.integratedLufs <= silenceDb || input.comparison.integratedLufs <= silenceDb)
        return;

    // The trim only exists for listening, so it's taken back out to compare the real levels.
    const auto difference = input.mix.integratedLufs - (input.comparison.integratedLufs - input.comparisonTrimDb);

    if (const auto severity = severityOf (difference, loudnessMinorLu, loudnessMajorLu); severity != Advice::Severity::note)
        advice.push_back ({ severity,
                            Advice::Topic::loudness,
                            "The mix is " + db (difference) + " LU " + (difference > 0.0f ? "louder" : "quieter") + " than the comparison ("
                                + juce::String (input.mix.integratedLufs, 1) + " vs "
                                + juce::String (input.comparison.integratedLufs - input.comparisonTrimDb, 1) + " LUFS)." });
}

void addDynamics (const AdviceInput& input, std::vector<Advice>& advice)
{
    const auto difference = input.mix.crestDb - input.comparison.crestDb;

    if (const auto severity = severityOf (difference, crestMinorDb, crestMajorDb); severity != Advice::Severity::note)
        advice.push_back ({ severity,
                            Advice::Topic::dynamics,
                            "The mix's crest factor is " + db (difference) + " dB " + (difference > 0.0f ? "higher" : "lower") + " than the comparison, so it's "
                                + (difference > 0.0f ? "more dynamic. There may be room for more bus compression or limiting."
                                                     : "more compressed. Easing off the limiting may recover punch.") });
}

void addStereo (const AdviceInput& input, std::vector<Advice>& advice)
{
    const auto difference = input.mix.sideRatio - input.comparison.sideRatio;

    if (const auto severity = severityOf (difference, sideRatioMinor, sideRatioMajor); severity != Advice::Severity::note)
        advice.push_back ({ severity,
                            Advice::Topic::stereo,
                            juce::String ("The mix is ") + (difference > 0.0f ? "wider" : "narrower") + " than the comparison: "
                                + juce::String (juce::roundToInt (input.mix.sideRatio * 100.0f)) + "% of its energy is in the sides, against "
                                + juce::String (juce::roundToInt (input.comparison.sideRatio * 100.0f)) + "%." });
}
} // namespace

std::vector<Advice> getMixAdvice (const AdviceInput& input)
{
    std::vector<Advice> advice;

    if (! input.mix.hasData)
        advice.push_back ({ Advice::Severity::note, Advice::Topic::status, "Play the mix to start comparing." });
    else if (input.mix.correlation < phaseProblemCorrelation) // A problem whatever it's compared with.
        advice.push_back ({ Advice::Severity::major,
                            Advice::Topic::stereo,
                            "The mix's stereo correlation is negative (" + juce::String (input.mix.correlation, 2)
                                + "), so parts of it will cancel in mono. Check for phase problems." });

    if (! input.comparison.hasData)
        advice.push_back ({ Advice::Severity::note, Advice::Topic::status, "Play the comparison to start comparing." });

    if (input.mix.hasData && input.comparison.hasData)
    {
        addTone (input, advice);

        // Noise has a tone to match, but it's got no meaningful loudness, dynamics or width.
        if (! input.comparisonIsNoise)
        {
            addLoudness (input, advice);
            addDynamics (input, advice);
            addStereo (input, advice);
        }
    }

    std::stable_sort (advice.begin(), advice.end(), [] (const Advice& a, const Advice& b) { return a.severity > b.severity; });
    return advice;
}

} // namespace mixcompare
