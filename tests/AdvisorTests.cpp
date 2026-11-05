#include "advice/MixAdvisor.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace mixcompare::test
{

class AdvisorTests final : public juce::UnitTest
{
public:
    AdvisorTests() : UnitTest ("Mix Advisor", "MixCompare") {}

    void runTest() override
    {
        testMissingData();
        testTone();
        testLoudness();
        testDynamics();
        testStereo();
        testNoiseComparison();
    }

private:
    static SourceProfile profile()
    {
        SourceProfile p;
        p.hasData = true;
        p.integratedLufs = -14.0f;
        p.crestDb = 10.0f;
        p.correlation = 0.5f;
        p.sideRatio = 0.25f;
        p.bands.fill (-40.0f);
        return p;
    }

    static AdviceInput matched()
    {
        AdviceInput input;
        input.mix = profile();
        input.comparison = profile();
        return input;
    }

    static int count (const std::vector<Advice>& advice, Advice::Topic topic)
    {
        return (int) std::count_if (advice.begin(), advice.end(), [=] (const Advice& a) { return a.topic == topic; });
    }

    void testMissingData()
    {
        beginTest ("Without both sources, it asks for what's missing");

        AdviceInput input;
        auto advice = getMixAdvice (input);
        expectEquals (count (advice, Advice::Topic::status), 2);
        expectEquals (count (advice, Advice::Topic::tone), 0);

        input.mix = profile();
        advice = getMixAdvice (input);
        expectEquals (count (advice, Advice::Topic::status), 1);
        expect (advice[0].text.contains ("comparison"));
    }

    void testTone()
    {
        beginTest ("Matching sources get a clean bill of health");
        {
            const auto advice = getMixAdvice (matched());
            expectEquals ((int) advice.size(), 1);
            expect (advice[0].severity == Advice::Severity::note);
            expect (advice[0].topic == Advice::Topic::tone);
        }

        beginTest ("A band that stands out is named, with the right direction and severity");
        {
            auto input = matched();
            input.mix.bands[1] += 7.0f; // Bass

            const auto advice = getMixAdvice (input);
            expect (advice[0].severity == Advice::Severity::major);
            expect (advice[0].text.startsWith ("Bass"));
            expect (advice[0].text.contains ("6.0 dB stronger"));
        }

        {
            auto input = matched();
            input.mix.bands[6] -= 3.5f; // Air

            const auto advice = getMixAdvice (input);
            expect (advice[0].severity == Advice::Severity::minor);
            expect (advice[0].text.startsWith ("Air"));
            expect (advice[0].text.contains ("weaker"));
        }

        beginTest ("An overall level difference is not mistaken for a tonal one");
        {
            auto input = matched();

            for (auto& band : input.mix.bands)
                band -= 10.0f;

            expectEquals (count (getMixAdvice (input), Advice::Topic::tone), 1);
            expect (getMixAdvice (input)[0].severity == Advice::Severity::note);
        }
    }

    void testLoudness()
    {
        beginTest ("Loudness differences are reported");

        auto input = matched();
        input.mix.integratedLufs = -18.0f;
        auto advice = getMixAdvice (input);
        expect (advice[0].topic == Advice::Topic::loudness);
        expect (advice[0].severity == Advice::Severity::major);
        expect (advice[0].text.contains ("4.0 LU quieter"));

        beginTest ("The listening trim doesn't change the real loudness difference");

        // Turned down by 4 dB to match by ear: they read the same, but aren't.
        input = matched();
        input.mix.integratedLufs = -18.0f;
        input.comparison.integratedLufs = -18.0f;
        input.comparisonTrimDb = -4.0f;
        advice = getMixAdvice (input);
        expect (advice[0].topic == Advice::Topic::loudness);
        expect (advice[0].text.contains ("4.0 LU quieter"));
        expect (advice[0].text.contains ("-14.0 LUFS"));

        // A true match, trimmed or not, says nothing.
        input.mix.integratedLufs = -14.0f;
        expectEquals (count (getMixAdvice (input), Advice::Topic::loudness), 0);
    }

    void testDynamics()
    {
        beginTest ("Crest factor differences are reported");

        auto input = matched();
        input.mix.crestDb = 6.0f;
        auto advice = getMixAdvice (input);
        expect (advice[0].topic == Advice::Topic::dynamics);
        expect (advice[0].text.contains ("more compressed"));

        input.mix.crestDb = 12.5f;
        advice = getMixAdvice (input);
        expect (advice[0].severity == Advice::Severity::minor);
        expect (advice[0].text.contains ("more dynamic"));
    }

    void testStereo()
    {
        beginTest ("Stereo width differences are reported");

        auto input = matched();
        input.mix.sideRatio = 0.05f;
        auto advice = getMixAdvice (input);
        expect (advice[0].topic == Advice::Topic::stereo);
        expect (advice[0].text.contains ("narrower"));

        beginTest ("Negative correlation is flagged on its own");

        input = matched();
        input.mix.correlation = -0.3f;
        advice = getMixAdvice (input);
        expect (advice[0].severity == Advice::Severity::major);
        expect (advice[0].text.contains ("cancel in mono"));

        input.comparison.hasData = false;
        expect (getMixAdvice (input)[0].text.contains ("cancel in mono"), "Needs nothing to compare with");
    }

    void testNoiseComparison()
    {
        beginTest ("Noise is only compared for tone");

        auto input = matched();
        input.comparisonIsNoise = true;
        input.mix.integratedLufs = -8.0f;
        input.mix.crestDb = 4.0f;
        input.mix.sideRatio = 0.02f;
        input.mix.bands[0] += 8.0f;

        const auto advice = getMixAdvice (input);
        expectEquals (count (advice, Advice::Topic::loudness), 0);
        expectEquals (count (advice, Advice::Topic::dynamics), 0);
        expectEquals (count (advice, Advice::Topic::stereo), 0);
        expect (count (advice, Advice::Topic::tone) > 0);
    }
};

static AdvisorTests advisorTests;

} // namespace mixcompare::test
