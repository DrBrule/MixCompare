#include "TestSignals.h"
#include "analysis/SourceAnalyser.h"

namespace mixcompare::test
{

class AnalysisTests final : public juce::UnitTest
{
public:
    AnalysisTests() : UnitTest ("Analysis", "MixCompare") {}

    void runTest() override
    {
        testLoudnessCalibration();
        testLoudnessGating();
        testLoudnessWindows();
        testLevels();
        testSpectrum();
        testStereoField();
        testSourceAnalyser();
    }

private:
    // Stereo 1 kHz sine at a given per-channel level, which the EBU test cases define as
    // reading that same level in LUFS.
    static void feedTone (LoudnessMeter& meter, float levelDb, double seconds, double sampleRate)
    {
        const auto length = (int) (seconds * sampleRate);
        const auto sine = makeSine (1000.0, juce::Decibels::decibelsToGain (levelDb), sampleRate, length);
        const float* const channels[] { sine.data(), sine.data() };
        meter.process (channels, 2, length);
    }

    void testLoudnessCalibration()
    {
        beginTest ("Loudness reads EBU Tech 3341 case 1 and 2 levels at any sample rate");

        for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
        {
            for (const auto level : { -23.0f, -33.0f })
            {
                LoudnessMeter meter;
                meter.prepare (sampleRate);
                feedTone (meter, level, 20.0, sampleRate);

                expectWithinAbsoluteError (meter.getMomentaryLufs(), level, 0.1f);
                expectWithinAbsoluteError (meter.getShortTermLufs(), level, 0.1f);
                expectWithinAbsoluteError (meter.getIntegratedLufs(), level, 0.1f);
            }
        }
    }

    void testLoudnessGating()
    {
        beginTest ("Integrated loudness gates out quiet passages (EBU Tech 3341 cases 3 and 4)");

        constexpr auto sampleRate = 48000.0;

        LoudnessMeter relative;
        relative.prepare (sampleRate);
        feedTone (relative, -36.0f, 10.0, sampleRate);
        feedTone (relative, -23.0f, 60.0, sampleRate);
        feedTone (relative, -36.0f, 10.0, sampleRate);
        expectWithinAbsoluteError (relative.getIntegratedLufs(), -23.0f, 0.1f);

        LoudnessMeter absolute;
        absolute.prepare (sampleRate);
        feedTone (absolute, -72.0f, 10.0, sampleRate);
        feedTone (absolute, -36.0f, 10.0, sampleRate);
        feedTone (absolute, -23.0f, 60.0, sampleRate);
        feedTone (absolute, -36.0f, 10.0, sampleRate);
        feedTone (absolute, -72.0f, 10.0, sampleRate);
        expectWithinAbsoluteError (absolute.getIntegratedLufs(), -23.0f, 0.1f);
    }

    void testLoudnessWindows()
    {
        beginTest ("Loudness windows and reset behave");

        constexpr auto sampleRate = 48000.0;
        LoudnessMeter meter;
        meter.prepare (sampleRate);

        expectEquals (meter.getMomentaryLufs(), silenceDb);

        feedTone (meter, -23.0f, 0.3, sampleRate);
        expectEquals (meter.getMomentaryLufs(), silenceDb, "Not enough audio for 400 ms yet");

        feedTone (meter, -23.0f, 1.0, sampleRate);
        expectWithinAbsoluteError (meter.getMomentaryLufs(), -23.0f, 0.2f);
        expectEquals (meter.getShortTermLufs(), silenceDb, "Not enough audio for 3 s yet");

        // After a sudden drop, momentary follows quickly and integrated barely moves.
        feedTone (meter, -23.0f, 10.0, sampleRate);
        feedTone (meter, -33.0f, 0.5, sampleRate);
        expectWithinAbsoluteError (meter.getMomentaryLufs(), -33.0f, 0.2f);
        expectGreaterThan (meter.getIntegratedLufs(), -24.0f);

        meter.requestReset();
        feedTone (meter, -33.0f, 2.0, sampleRate);
        expectWithinAbsoluteError (meter.getIntegratedLufs(), -33.0f, 0.2f);

        // Left alone in a stereo pair, one channel reads 3 dB down on both together.
        LoudnessMeter single;
        single.prepare (sampleRate);
        const auto sine = makeSine (1000.0, juce::Decibels::decibelsToGain (-23.0f), sampleRate, 480000);
        const float* const channels[] { sine.data() };
        single.process (channels, 1, 480000);
        expectWithinAbsoluteError (single.getIntegratedLufs(), -26.0f, 0.1f);
    }

    void testLevels()
    {
        beginTest ("RMS and crest factor are right for known shapes");

        constexpr auto sampleRate = 48000.0;
        constexpr int length = 192000;

        {
            LevelMeter meter;
            meter.prepare (sampleRate);
            const auto sine = makeSine (1000.0, 0.5f, sampleRate, length);
            meter.process (sine.data(), sine.data(), length);

            expectWithinAbsoluteError (meter.getRmsDb (0), -9.03f, 0.05f);
            expectWithinAbsoluteError (meter.getCrestDb (0), 3.01f, 0.05f);
            expectWithinAbsoluteError (meter.getAverageCrestDb(), 3.01f, 0.05f);
        }

        {
            LevelMeter meter;
            meter.prepare (sampleRate);
            std::vector<float> square ((size_t) length), silence ((size_t) length, 0.0f);

            for (int i = 0; i < length; ++i)
                square[(size_t) i] = (i / 24) % 2 == 0 ? 0.25f : -0.25f;

            meter.process (square.data(), silence.data(), length);

            expectWithinAbsoluteError (meter.getRmsDb (0), -12.04f, 0.05f);
            expectWithinAbsoluteError (meter.getCrestDb (0), 0.0f, 0.05f);
            expectEquals (meter.getRmsDb (1), silenceDb);
            expectEquals (meter.getCrestDb (1), 0.0f, "A silent channel has no crest factor");
            expectWithinAbsoluteError (meter.getAverageCrestDb(), 0.0f, 0.05f);
        }
    }

    static float peakOf (const SpectrumAnalyser& analyser)
    {
        SpectrumAnalyser::Spectrum spectrum {};
        analyser.getAverageDb (spectrum);
        return *std::max_element (spectrum.begin(), spectrum.end());
    }

    void testSpectrum()
    {
        beginTest ("The spectrum finds a sine at the right frequency and level");

        constexpr auto sampleRate = 48000.0;
        constexpr int length = 96000;
        constexpr int bin = 171;
        const auto frequency = bin * sampleRate / SpectrumAnalyser::defaultSize;
        const auto sine = makeSine (frequency, 1.0f, sampleRate, length);
        std::vector<float> inverted ((size_t) length), silence ((size_t) length, 0.0f);

        for (int i = 0; i < length; ++i)
            inverted[(size_t) i] = -sine[(size_t) i];

        {
            SpectrumAnalyser analyser;
            analyser.prepare (sampleRate);
            expect (! analyser.hasAverage());
            analyser.push (sine.data(), sine.data(), length);
            expect (analyser.hasAverage());

            SpectrumAnalyser::Spectrum spectrum {};
            analyser.getAverageDb (spectrum);
            const auto peak = std::max_element (spectrum.begin(), spectrum.end());

            expectEquals ((int) std::distance (spectrum.begin(), peak), bin);
            expectWithinAbsoluteError (*peak, 0.0f, 0.1f);
            expectLessThan (spectrum[(size_t) bin * 4], -60.0f, "Energy stays near the sine");

            analyser.getDisplayDb (spectrum);
            expectWithinAbsoluteError (spectrum[(size_t) bin], 0.0f, 0.1f);
        }

        {
            SpectrumAnalyser analyser;
            analyser.prepare (sampleRate);
            analyser.push (sine.data(), inverted.data(), length);
            expectWithinAbsoluteError (peakOf (analyser), 0.0f, 0.1f);
        }

        {
            SpectrumAnalyser analyser;
            analyser.prepare (sampleRate);
            analyser.push (sine.data(), silence.data(), length);
            expectWithinAbsoluteError (peakOf (analyser), -3.01f, 0.1f);
        }

        beginTest ("The spectrum's average ignores silence and can be reset");
        {
            SpectrumAnalyser analyser;
            analyser.prepare (sampleRate);
            analyser.push (sine.data(), sine.data(), length);
            analyser.push (silence.data(), silence.data(), length);

            // A little below 0 dB, from the few frames with the sine only part-way through
            // the window. All the silence after them would have taken it far lower.
            expectWithinAbsoluteError (peakOf (analyser), 0.0f, 0.5f);

            analyser.requestReset();
            analyser.push (silence.data(), silence.data(), length);
            expect (! analyser.hasAverage());
            expectEquals (peakOf (analyser), silenceDb);
        }

        beginTest ("Band levels land in the right band");
        {
            SpectrumAnalyser analyser;
            analyser.prepare (sampleRate);
            const auto bass = makeSine (100.0, 1.0f, sampleRate, length);
            analyser.push (bass.data(), bass.data(), length);

            const auto bands = analyser.getAverageBands();
            const auto loudest = std::max_element (bands.begin(), bands.end());
            expectEquals (juce::String (frequencyBands[(size_t) std::distance (bands.begin(), loudest)].name), juce::String ("Bass"));
        }

        testSpectrumResolution();
    }

    void testSpectrumResolution()
    {
        constexpr auto sampleRate = 48000.0;
        constexpr int length = 200000;

        beginTest ("Every FFT size finds a sine at the right frequency and level");

        for (auto order = SpectrumAnalyser::minOrder; order <= SpectrumAnalyser::maxOrder; ++order)
        {
            constexpr double wanted = 1000.0;
            const auto binHz = sampleRate / (1 << order);
            const auto bin = juce::roundToInt (wanted / binHz);
            const auto sine = makeSine (bin * binHz, 1.0f, sampleRate, length);

            SpectrumAnalyser analyser (order);
            analyser.prepare (sampleRate);
            analyser.push (sine.data(), sine.data(), length);

            SpectrumAnalyser::Spectrum spectrum;
            expectWithinAbsoluteError (analyser.getDisplayDb (spectrum), binHz, 1.0e-9);
            expectEquals ((int) spectrum.size(), (1 << order) / 2 + 1);

            const auto peak = std::max_element (spectrum.begin(), spectrum.end());
            expectEquals ((int) std::distance (spectrum.begin(), peak), bin);
            expectWithinAbsoluteError (*peak, 0.0f, 0.1f);
        }

        beginTest ("A larger FFT separates tones that a smaller one can't");
        {
            // 8 Hz apart: closer than a 2048 point window can resolve, plain to a 65536 point one.
            const auto low = makeSine (1000.0, 0.5f, sampleRate, length);
            const auto high = makeSine (1008.0, 0.5f, sampleRate, length);
            std::vector<float> both ((size_t) length);

            for (size_t i = 0; i < both.size(); ++i)
                both[i] = low[i] + high[i];

            const auto dipBetweenTones = [&] (int order)
            {
                SpectrumAnalyser analyser (order);
                analyser.prepare (sampleRate);
                analyser.push (both.data(), both.data(), length);

                SpectrumAnalyser::Spectrum spectrum;
                const auto binHz = analyser.getAverageDb (spectrum);
                const auto at = [&] (double hz) { return spectrum[(size_t) juce::roundToInt (hz / binHz)]; };
                return at (1000.0) - at (1004.0);
            };

            expectLessThan (std::abs (dipBetweenTones (SpectrumAnalyser::minOrder)), 3.0f);
            expectGreaterThan (dipBetweenTones (SpectrumAnalyser::maxOrder), 30.0f);
        }

        beginTest ("Changing the FFT size carries straight on with the audio it already has");
        {
            const auto sine = makeSine (1000.0, 1.0f, sampleRate, length);
            SpectrumAnalyser analyser;
            analyser.prepare (sampleRate);
            analyser.push (sine.data(), sine.data(), length);

            for (const auto order : { SpectrumAnalyser::maxOrder, SpectrumAnalyser::minOrder, SpectrumAnalyser::defaultOrder })
            {
                analyser.setOrder (order);

                SpectrumAnalyser::Spectrum spectrum;
                const auto binHz = analyser.getDisplayDb (spectrum);
                expectWithinAbsoluteError (binHz, sampleRate / (1 << order), 1.0e-9);
                expectGreaterThan (*std::max_element (spectrum.begin(), spectrum.end()), -1.5f);
                expect (! analyser.hasAverage(), "The old average doesn't apply to the new bins");
            }

            analyser.setOrder (99);
            expectWithinAbsoluteError (analyser.getBinHz(), sampleRate / (1 << SpectrumAnalyser::maxOrder), 1.0e-9);
        }

        beginTest ("The display's resolution leaves the advice's averages alone");
        {
            const auto sine = makeSine (100.0, 0.5f, sampleRate, length);
            SourceAnalyser analyser;
            analyser.prepare (sampleRate);
            analyser.processAudio (sine.data(), sine.data(), 2, length);
            analyser.drain();

            const auto bands = analyser.getSpectrum().getAverageBands();
            const auto seconds = analyser.getSpectrum().getAverageSeconds();
            expectGreaterThan (seconds, 2.0);

            analyser.setDisplayOrder (SpectrumAnalyser::maxOrder);

            expect (analyser.getSpectrum().getAverageBands() == bands);
            expectEquals (analyser.getSpectrum().getAverageSeconds(), seconds);
            expectWithinAbsoluteError (analyser.getDisplaySpectrum().getBinHz(), sampleRate / (1 << SpectrumAnalyser::maxOrder), 1.0e-9);
        }
    }

    void testStereoField()
    {
        beginTest ("Correlation and side ratio are right for known stereo images");

        constexpr auto sampleRate = 48000.0;
        constexpr int length = 96000;
        const auto sine = makeSine (440.0, 0.5f, sampleRate, length);
        const auto noiseA = makeNoise (0.5f, length, 1);
        const auto noiseB = makeNoise (0.5f, length, 2);
        std::vector<float> inverted ((size_t) length);

        for (int i = 0; i < length; ++i)
            inverted[(size_t) i] = -sine[(size_t) i];

        const auto check = [&] (const float* left, const float* right, float correlation, float sideRatio)
        {
            StereoField field;
            field.prepare (sampleRate);
            field.push (left, right, length);

            expectWithinAbsoluteError (field.getCorrelation(), correlation, 0.05f);
            expectWithinAbsoluteError (field.getAverageCorrelation(), correlation, 0.05f);
            expectWithinAbsoluteError (field.getAverageSideRatio(), sideRatio, 0.05f);
        };

        check (sine.data(), sine.data(), 1.0f, 0.0f);
        check (sine.data(), inverted.data(), -1.0f, 1.0f);
        check (noiseA.data(), noiseB.data(), 0.0f, 0.5f);

        beginTest ("Balance and live width are right for known stereo images");
        {
            const std::vector<float> silence ((size_t) length, 0.0f);

            const auto checkBalance = [&] (const float* left, const float* right, float balance, float width)
            {
                StereoField field;
                field.prepare (sampleRate);
                field.push (left, right, length);

                expectWithinAbsoluteError (field.getBalance(), balance, 0.05f);
                expectWithinAbsoluteError (field.getSideRatio(), width, 0.05f);
            };

            checkBalance (sine.data(), sine.data(), 0.0f, 0.0f);
            checkBalance (sine.data(), silence.data(), -1.0f, 0.5f);
            checkBalance (silence.data(), sine.data(), 1.0f, 0.5f);
            checkBalance (sine.data(), inverted.data(), 0.0f, 1.0f);
            checkBalance (silence.data(), silence.data(), 0.0f, 0.0f);
        }

        beginTest ("The scope keeps the latest samples, oldest first");
        {
            StereoField field;
            field.prepare (sampleRate);
            field.push (sine.data(), inverted.data(), length);

            StereoField::Scope scope;
            field.copyScope (scope);
            expectEquals (scope.back().x, sine.back());
            expectEquals (scope.back().y, inverted.back());
            expectEquals (scope.front().x, sine[(size_t) length - StereoField::scopeSize]);
        }

        beginTest ("Goniometer and Lissajous place samples where they should");
        {
            constexpr auto rootHalf = 0.7071f;
            const auto expectPoint = [this] (juce::Point<float> point, float x, float y)
            {
                expectWithinAbsoluteError (point.x, x, 0.001f);
                expectWithinAbsoluteError (point.y, y, 0.001f);
            };

            // Mono is vertical, left-only leans upper left and out of phase is horizontal.
            expectPoint (StereoField::project (StereoMode::goniometer, 1.0f, 1.0f), 0.0f, 2.0f * rootHalf);
            expectPoint (StereoField::project (StereoMode::goniometer, 1.0f, 0.0f), -rootHalf, rootHalf);
            expectPoint (StereoField::project (StereoMode::goniometer, 0.0f, 1.0f), rootHalf, rootHalf);
            expectPoint (StereoField::project (StereoMode::goniometer, -1.0f, 1.0f), 2.0f * rootHalf, 0.0f);

            expectPoint (StereoField::project (StereoMode::lissajous, 1.0f, 1.0f), 1.0f, 1.0f);
            expectPoint (StereoField::project (StereoMode::lissajous, 1.0f, 0.0f), 1.0f, 0.0f);
            expectPoint (StereoField::project (StereoMode::lissajous, 0.0f, 1.0f), 0.0f, 1.0f);
        }
    }

    void testSourceAnalyser()
    {
        beginTest ("Audio reaches every analyser across the queue, in any block size");

        constexpr auto sampleRate = 48000.0;
        constexpr int length = 96000;
        const auto sine = makeSine (1000.0, 0.5f, sampleRate, length);

        for (const auto blockSize : { 32, 441, 4096 })
        {
            SourceAnalyser analyser;
            analyser.prepare (sampleRate);

            for (int start = 0; start < length; start += blockSize)
            {
                const auto size = juce::jmin (blockSize, length - start);
                analyser.processAudio (sine.data() + start, sine.data() + start, 2, size);

                if (start % (blockSize * 4) == 0)
                    analyser.drain();
            }

            analyser.drain();

            expect (analyser.getSpectrum().hasAverage());
            expectWithinAbsoluteError (analyser.getLevels().getRmsDb (0), -9.03f, 0.1f);
            expectWithinAbsoluteError (analyser.getLoudness().getIntegratedLufs(), -6.02f, 0.3f);
            expectWithinAbsoluteError (analyser.getStereo().getCorrelation(), 1.0f, 0.01f);
        }

        beginTest ("A stalled analysis thread can't overrun the queue");
        {
            SourceAnalyser analyser;
            analyser.prepare (sampleRate);

            for (int i = 0; i < 40; ++i)
                analyser.processAudio (sine.data(), sine.data(), 2, length);

            analyser.drain();
            expectWithinAbsoluteError (analyser.getStereo().getCorrelation(), 1.0f, 0.01f);
        }
    }
};

static AnalysisTests analysisTests;

} // namespace mixcompare::test
