#include "TestSignals.h"
#include "analysis/SpectrumAnalyser.h"
#include "sources/FilePlayer.h"
#include "sources/NoiseGenerator.h"

namespace mixcompare::test
{

namespace
{
juce::File writeWav (const juce::String& name, double sampleRate, int numChannels, const std::vector<float>& samples)
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
    file.deleteFile();

    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    const auto options = juce::AudioFormatWriterOptions {}
                             .withSampleRate (sampleRate)
                             .withNumChannels (numChannels)
                             .withBitsPerSample (24);
    auto writer = format.createWriterFor (stream, options);

    juce::AudioBuffer<float> buffer (numChannels, (int) samples.size());

    for (int channel = 0; channel < numChannels; ++channel)
        buffer.copyFrom (channel, 0, samples.data(), (int) samples.size());

    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    return file;
}

bool waitUntilLoaded (const FilePlayer& player)
{
    for (int i = 0; i < 1000 && player.getStatus() == FilePlayer::Status::loading; ++i)
        juce::Thread::sleep (5);

    return player.getStatus() != FilePlayer::Status::loading;
}
} // namespace

class SourceTests final : public juce::UnitTest
{
public:
    SourceTests() : UnitTest ("Sources", "MixCompare") {}

    void runTest() override
    {
        testNoiseLevels();
        testNoiseColours();
        testFilePlayback();
        testFileResampling();
        testFileTransport();
        testFileErrors();
        testSwappingWhilePlaying();
    }

private:
    void testNoiseLevels()
    {
        beginTest ("Every noise colour comes out at the same level, at any sample rate");

        for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
        {
            for (const auto colour : { NoiseColour::white, NoiseColour::pink, NoiseColour::brown })
            {
                NoiseGenerator noise;
                noise.prepare (sampleRate);

                juce::AudioBuffer<float> buffer (2, 1 << 19);
                noise.process (buffer.getArrayOfWritePointers(), buffer.getNumSamples(), colour);

                expectWithinAbsoluteError (rmsDb (buffer.getReadPointer (0), buffer.getNumSamples()), NoiseGenerator::targetRmsDb, 1.0f);
                expectWithinAbsoluteError (rmsDb (buffer.getReadPointer (1), buffer.getNumSamples()), NoiseGenerator::targetRmsDb, 1.0f);
            }
        }
    }

    void testNoiseColours()
    {
        beginTest ("Noise colours have the right slopes and are decorrelated between channels");

        constexpr auto sampleRate = 48000.0;
        constexpr int blockSize = 4096;
        constexpr int numBlocks = 400;

        // Density in a range around each frequency, four octaves apart.
        const auto slopeOver4Octaves = [&] (NoiseColour colour)
        {
            NoiseGenerator noise;
            noise.prepare (sampleRate);
            SpectrumAnalyser analyser;
            analyser.prepare (sampleRate);
            juce::AudioBuffer<float> buffer (2, blockSize);

            for (int i = 0; i < numBlocks; ++i)
            {
                noise.process (buffer.getArrayOfWritePointers(), blockSize, colour);
                analyser.push (buffer.getReadPointer (0), buffer.getReadPointer (1), blockSize);
            }

            SpectrumAnalyser::Spectrum spectrum {};
            analyser.getAverageDb (spectrum);

            const auto density = [&] (double hz)
            {
                const auto centre = hz / analyser.getBinHz();
                auto sum = 0.0;
                auto count = 0;

                for (auto bin = (int) (centre * 0.8); bin <= (int) (centre * 1.25); ++bin, ++count)
                    sum += std::pow (10.0, spectrum[(size_t) bin] / 10.0);

                return 10.0 * std::log10 (sum / count);
            };

            return (float) (density (6400.0) - density (400.0));
        };

        expectWithinAbsoluteError (slopeOver4Octaves (NoiseColour::white), 0.0f, 1.0f);
        expectWithinAbsoluteError (slopeOver4Octaves (NoiseColour::pink), -12.04f, 1.0f);
        expectWithinAbsoluteError (slopeOver4Octaves (NoiseColour::brown), -24.08f, 1.5f);

        for (const auto colour : { NoiseColour::white, NoiseColour::pink, NoiseColour::brown })
        {
            NoiseGenerator noise;
            noise.prepare (sampleRate);
            juce::AudioBuffer<float> buffer (2, 1 << 19);
            noise.process (buffer.getArrayOfWritePointers(), buffer.getNumSamples(), colour);

            auto ll = 0.0, rr = 0.0, lr = 0.0;

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto l = (double) buffer.getSample (0, i), r = (double) buffer.getSample (1, i);
                ll += l * l;
                rr += r * r;
                lr += l * r;
            }

            expectLessThan ((float) std::abs (lr / std::sqrt (ll * rr)), 0.1f);
        }
    }

    void testFilePlayback()
    {
        beginTest ("A file plays back unchanged at its own sample rate, and loops");

        constexpr auto sampleRate = 48000.0;
        constexpr int length = 24000;
        const auto sine = makeSine (1000.0, 0.5f, sampleRate, length);
        const auto file = writeWav ("mixcompare_test_stereo.wav", sampleRate, 2, sine);

        FilePlayer player;
        player.prepare (sampleRate);
        player.load (file);
        expect (waitUntilLoaded (player));
        expect (player.getStatus() == FilePlayer::Status::ready, player.getError());
        expectWithinAbsoluteError (player.getLengthSeconds(), 0.5, 0.001);

        // Three times the file's length, in small blocks, should stay at the same level.
        juce::AudioBuffer<float> output (2, length * 3);

        for (int start = 0; start < output.getNumSamples(); start += 256)
        {
            float* const channels[] { output.getWritePointer (0, start), output.getWritePointer (1, start) };
            player.process (channels, juce::jmin (256, output.getNumSamples() - start), true);
        }

        for (int section = 0; section < 3; ++section)
            expectWithinAbsoluteError (rmsDb (output.getReadPointer (0, section * length + 200), length - 400), -9.03f, 0.1f);

        // Nothing but the file's own 24-bit rounding, on the first pass and after looping.
        auto worst = 0.0f;

        for (int i = 0; i < output.getNumSamples(); ++i)
            worst = juce::jmax (worst, std::abs (output.getSample (0, i) - sine[(size_t) (i % length)]));

        expectLessThan (worst, 1.0e-6f);

       #if JUCE_MAC
        beginTest ("Compressed files decode, the way most reference songs arrive");
        {
            const auto aac = file.getSiblingFile ("mixcompare_test_aac.m4a");
            aac.deleteFile();

            juce::ChildProcess encoder;
            expect (encoder.start (juce::StringArray { "/usr/bin/afconvert", "-f", "m4af", "-d", "aac", file.getFullPathName(), aac.getFullPathName() }));
            encoder.waitForProcessToFinish (30000);

            FilePlayer compressed;
            compressed.prepare (sampleRate);
            compressed.load (aac);
            expect (waitUntilLoaded (compressed));
            expect (compressed.getStatus() == FilePlayer::Status::ready, compressed.getError());

            juce::AudioBuffer<float> decoded (2, 12000);
            compressed.process (decoded.getArrayOfWritePointers(), 12000, true);
            expectWithinAbsoluteError (rmsDb (decoded.getReadPointer (0, 4000), 6000), -9.03f, 0.5f);
            aac.deleteFile();
        }
       #endif

        beginTest ("A mono file plays in both channels");
        {
            const auto monoFile = writeWav ("mixcompare_test_mono.wav", sampleRate, 1, sine);
            FilePlayer mono;
            mono.prepare (sampleRate);
            mono.load (monoFile);
            expect (waitUntilLoaded (mono));

            juce::AudioBuffer<float> stereo (2, 4096);
            mono.process (stereo.getArrayOfWritePointers(), 4096, true);
            expectWithinAbsoluteError (rmsDb (stereo.getReadPointer (0, 200), 3000), -9.03f, 0.2f);
            expectWithinAbsoluteError (rmsDb (stereo.getReadPointer (1, 200), 3000), -9.03f, 0.2f);
            monoFile.deleteFile();
        }

        file.deleteFile();
    }

    void testFileResampling()
    {
        beginTest ("A file at another sample rate keeps its pitch and speed");

        constexpr auto fileRate = 44100.0;
        constexpr auto hostRate = 48000.0;
        constexpr int length = 44100 * 2;
        const auto file = writeWav ("mixcompare_test_441.wav", fileRate, 2, makeSine (1000.0, 0.5f, fileRate, length));

        FilePlayer player;
        player.prepare (hostRate);
        player.load (file);
        expect (waitUntilLoaded (player));

        constexpr int outputLength = 48000;
        juce::AudioBuffer<float> output (2, outputLength);
        player.process (output.getArrayOfWritePointers(), outputLength, true);

        expectWithinAbsoluteError (player.getPositionSeconds(), 1.0, 0.001);

        // Count rising zero crossings over one second to find the pitch.
        auto crossings = 0;

        for (int i = 2000; i < outputLength; ++i)
            if (output.getSample (0, i - 1) < 0.0f && output.getSample (0, i) >= 0.0f)
                ++crossings;

        expectWithinAbsoluteError (crossings / ((outputLength - 2000) / hostRate), 1000.0, 2.0);
        file.deleteFile();

        beginTest ("Resampling keeps the level across the audible range");

        for (const auto [from, to] : { std::pair { 44100.0, 48000.0 }, std::pair { 48000.0, 44100.0 }, std::pair { 44100.0, 96000.0 } })
        {
            for (const auto frequency : { 50.0, 1000.0, 10000.0, 18000.0 })
            {
                const auto toneFile = writeWav ("mixcompare_test_level.wav", from, 2, makeSine (frequency, 0.5f, from, (int) from * 2));

                FilePlayer tone;
                tone.prepare (to);
                tone.load (toneFile);
                expect (waitUntilLoaded (tone));

                const auto toneLength = (int) to;
                juce::AudioBuffer<float> toneOutput (2, toneLength);
                tone.process (toneOutput.getArrayOfWritePointers(), toneLength, true);

                expectWithinAbsoluteError (rmsDb (toneOutput.getReadPointer (0, 1000), toneLength - 2000), -9.03f, 0.25f);
                toneFile.deleteFile();
            }
        }
    }

    void testFileTransport()
    {
        beginTest ("The file holds still when it can't advance, and can seek");

        constexpr auto sampleRate = 48000.0;
        const auto file = writeWav ("mixcompare_test_seek.wav", sampleRate, 2, makeSine (1000.0, 0.5f, sampleRate, 96000));

        FilePlayer player;
        player.prepare (sampleRate);

        juce::AudioBuffer<float> output (2, 1024);
        output.applyGain (0.0f);
        player.process (output.getArrayOfWritePointers(), 1024, true);
        expectEquals (output.getMagnitude (0, 1024), 0.0f, "Nothing loaded yet");

        player.load (file);
        expect (waitUntilLoaded (player));

        player.process (output.getArrayOfWritePointers(), 1024, true);
        const auto position = player.getPositionSeconds();
        expectGreaterThan (position, 0.0);

        player.process (output.getArrayOfWritePointers(), 1024, false);
        expectEquals (output.getMagnitude (0, 1024), 0.0f);
        expectEquals (player.getPositionSeconds(), position);

        player.seek (1.5);
        player.process (output.getArrayOfWritePointers(), 1024, false);
        expectWithinAbsoluteError (player.getPositionSeconds(), 1.5, 0.001);

        file.deleteFile();
    }

    void testFileErrors()
    {
        beginTest ("Bad files fail with a reason, and leave what was playing alone");

        constexpr auto sampleRate = 48000.0;
        const auto good = writeWav ("mixcompare_test_good.wav", sampleRate, 2, makeSine (1000.0, 0.5f, sampleRate, 48000));
        const auto bad = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mixcompare_test_bad.wav");
        bad.replaceWithText ("This is not audio");

        FilePlayer player;
        player.prepare (sampleRate);
        player.load (good);
        expect (waitUntilLoaded (player));

        player.load (bad);
        expect (waitUntilLoaded (player));
        expect (player.getStatus() == FilePlayer::Status::failed);
        expect (player.getError().contains (bad.getFileName()));

        juce::AudioBuffer<float> output (2, 4096);
        player.process (output.getArrayOfWritePointers(), 4096, true);
        expectGreaterThan (output.getMagnitude (0, 4096), 0.4f);

        player.load (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mixcompare_does_not_exist.wav"));
        expect (waitUntilLoaded (player));
        expect (player.getStatus() == FilePlayer::Status::failed);

        good.deleteFile();
        bad.deleteFile();
    }

    void testSwappingWhilePlaying()
    {
        beginTest ("Songs can be swapped while the audio thread is running");

        constexpr auto sampleRate = 48000.0;
        const auto a = writeWav ("mixcompare_test_swap_a.wav", sampleRate, 2, makeSine (1000.0, 0.5f, sampleRate, 48000));
        const auto b = writeWav ("mixcompare_test_swap_b.wav", 44100.0, 2, makeSine (500.0, 0.25f, 44100.0, 22050));

        FilePlayer player;
        player.prepare (sampleRate);

        std::atomic<bool> stop { false };
        std::thread audio ([&]
                           {
                               juce::AudioBuffer<float> output (2, 256);

                               while (! stop.load())
                                   player.process (output.getArrayOfWritePointers(), 256, true);
                           });

        for (int i = 0; i < 20; ++i)
        {
            player.load (i % 2 == 0 ? a : b);
            expect (waitUntilLoaded (player));
        }

        stop.store (true);
        audio.join();

        expectEquals (player.getGeneration(), 20);
        a.deleteFile();
        b.deleteFile();
    }
};

static SourceTests sourceTests;

} // namespace mixcompare::test
