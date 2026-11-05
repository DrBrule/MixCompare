#include "PluginProcessor.h"
#include "TestSignals.h"

namespace mixcompare::test
{

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;

void setChoice (PluginProcessor& processor, const char* id, int index)
{
    auto* parameter = processor.getParameters().getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) index));
}

void setTrim (PluginProcessor& processor, float db)
{
    auto* parameter = processor.getParameters().getParameter (params::comparisonTrim);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (db));
}

// A sine in the main input, optionally another in the sidechain, returning the final output.
juce::AudioBuffer<float> run (PluginProcessor& processor,
                              double seconds,
                              float mixFrequency = 1000.0f,
                              float mixLevel = 0.25f,
                              float sidechainFrequency = 0.0f)
{
    const auto totalChannels = processor.getTotalNumInputChannels();
    const auto length = (int) (seconds * sampleRate);
    const auto mix = makeSine (mixFrequency, mixLevel, sampleRate, length);
    const auto side = makeSine (sidechainFrequency, 0.5f, sampleRate, length);

    juce::AudioBuffer<float> buffer (totalChannels, blockSize), output (2, blockSize);
    juce::MidiBuffer midi;

    for (int start = 0; start + blockSize <= length; start += blockSize)
    {
        buffer.copyFrom (0, 0, mix.data() + start, blockSize);
        buffer.copyFrom (1, 0, mix.data() + start, blockSize);

        for (int channel = 2; channel < totalChannels; ++channel)
            buffer.copyFrom (channel, 0, side.data() + start, blockSize);

        processor.processBlock (buffer, midi);
    }

    output.copyFrom (0, 0, buffer, 0, 0, blockSize);
    output.copyFrom (1, 0, buffer, 1, 0, blockSize);
    return output;
}

void enableSidechain (PluginProcessor& processor)
{
    auto layout = processor.getBusesLayout();
    layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
    processor.setBusesLayout (layout);
}

float outputDb (const juce::AudioBuffer<float>& buffer)
{
    return rmsDb (buffer.getReadPointer (0), buffer.getNumSamples());
}
} // namespace

class ProcessorTests final : public juce::UnitTest
{
public:
    ProcessorTests() : UnitTest ("Processor", "MixCompare") {}

    void runTest() override
    {
        testLayouts();
        testMonitorSwitch();
        testBothSourcesAlwaysAnalysed();
        testSidechain();
        testOversizedBlocks();
        testComparisonReset();
        testAdvice();
        testState();
        testStereoReadingsFollowTheInput();
    }

private:
    void testStereoReadingsFollowTheInput()
    {
        beginTest ("The live stereo readings follow the input through the whole plugin, within half a second");

        PluginProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);
        const auto& stereo = processor.getAnalyser (MonitorSource::mix).getStereo();
        juce::AudioBuffer<float> buffer (processor.getTotalNumInputChannels(), blockSize);
        juce::MidiBuffer midi;
        auto phase = 0.0f;

        const auto play = [&] (float left, float right)
        {
            for (int block = 0; block < (int) (0.5 * sampleRate / blockSize); ++block)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    phase += 0.05f;
                    buffer.setSample (0, i, left * std::sin (phase));
                    buffer.setSample (1, i, right * std::sin (phase));
                }

                processor.processBlock (buffer, midi);
                processor.drainAnalysis();
            }
        };

        const auto expectReadings = [&] (float correlation, float balance, float width, const char* what)
        {
            expectWithinAbsoluteError (stereo.getCorrelation(), correlation, 0.1f, what);
            expectWithinAbsoluteError (stereo.getBalance(), balance, 0.1f, what);
            expectWithinAbsoluteError (stereo.getSideRatio(), width, 0.1f, what);
        };

        play (0.3f, 0.3f);
        expectReadings (1.0f, 0.0f, 0.0f, "Mono");

        play (0.3f, 0.0f);
        expectReadings (0.0f, -1.0f, 0.5f, "Left only");

        play (0.3f, -0.3f);
        expectReadings (-1.0f, 0.0f, 1.0f, "Out of phase");

        // 50 dB quieter than what came before, which mustn't be able to hide it.
        play (0.001f, 0.001f);
        expectReadings (1.0f, 0.0f, 0.0f, "Quiet mono after loud audio");

        play (0.3f, -0.3f);
        play (0.0f, 0.0f);
        expectReadings (0.0f, 0.0f, 0.0f, "Silence");

        processor.releaseResources();
    }

    void testLayouts()
    {
        beginTest ("Mono and stereo are accepted, with or without a sidechain");

        PluginProcessor processor;
        juce::AudioProcessor::BusesLayout layout;
        const auto mono = juce::AudioChannelSet::mono();
        const auto stereo = juce::AudioChannelSet::stereo();

        layout.inputBuses = { stereo, juce::AudioChannelSet::disabled() };
        layout.outputBuses = { stereo };
        expect (processor.isBusesLayoutSupported (layout));

        layout.inputBuses = { stereo, stereo };
        expect (processor.isBusesLayoutSupported (layout));

        layout.inputBuses = { mono, stereo };
        layout.outputBuses = { mono };
        expect (processor.isBusesLayoutSupported (layout));

        layout.inputBuses = { stereo, stereo };
        layout.outputBuses = { mono };
        expect (! processor.isBusesLayoutSupported (layout));

        layout.inputBuses = { juce::AudioChannelSet::create5point1(), stereo };
        layout.outputBuses = { juce::AudioChannelSet::create5point1() };
        expect (! processor.isBusesLayoutSupported (layout));
    }

    void testMonitorSwitch()
    {
        beginTest ("Monitoring the mix passes it through untouched");

        PluginProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);
        setChoice (processor, params::comparisonSource, (int) ComparisonSource::noise);

        const auto length = blockSize;
        const auto sine = makeSine (1000.0, 0.25f, sampleRate, length);
        juce::AudioBuffer<float> buffer (2, length);
        buffer.copyFrom (0, 0, sine.data(), length);
        buffer.copyFrom (1, 0, sine.data(), length);
        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);

        for (int i = 0; i < length; ++i)
            expectEquals (buffer.getSample (0, i), sine[(size_t) i]);

        beginTest ("Monitoring the comparison replaces the mix with it, after a short fade");

        setChoice (processor, params::monitor, (int) MonitorSource::comparison);
        run (processor, 0.1);

        // Silence in, noise out.
        auto output = run (processor, 0.2, 1000.0f, 0.0f);
        expectWithinAbsoluteError (outputDb (output), NoiseGenerator::targetRmsDb, 3.0f);

        beginTest ("The trim changes the comparison's level");

        setTrim (processor, -12.0f);
        run (processor, 0.1);
        output = run (processor, 0.2, 1000.0f, 0.0f);
        expectWithinAbsoluteError (outputDb (output), NoiseGenerator::targetRmsDb - 12.0f, 3.0f);

        beginTest ("Switching back returns to the mix");

        setChoice (processor, params::monitor, (int) MonitorSource::mix);
        run (processor, 0.1);
        output = run (processor, 0.1, 1000.0f, 0.25f);
        expectWithinAbsoluteError (outputDb (output), -15.05f, 0.5f);

        processor.releaseResources();
    }

    void testBothSourcesAlwaysAnalysed()
    {
        beginTest ("Both sources are measured whichever is being heard");

        for (const auto monitor : { MonitorSource::mix, MonitorSource::comparison })
        {
            PluginProcessor processor;
            processor.prepareToPlay (sampleRate, blockSize);
            setChoice (processor, params::comparisonSource, (int) ComparisonSource::noise);
            setChoice (processor, params::monitor, (int) monitor);

            run (processor, 2.0);
            processor.drainAnalysis();

            const auto& mix = processor.getAnalyser (MonitorSource::mix);
            const auto& comparison = processor.getAnalyser (MonitorSource::comparison);

            expectWithinAbsoluteError (mix.getLevels().getRmsDb (0), -15.05f, 0.1f);
            expectWithinAbsoluteError (mix.getStereo().getCorrelation(), 1.0f, 0.01f);
            expect (mix.getSpectrum().hasAverage());
            expectGreaterThan (mix.getLoudness().getIntegratedLufs(), -20.0f);

            expectWithinAbsoluteError (comparison.getLevels().getRmsDb (0), NoiseGenerator::targetRmsDb, 2.0f);
            expectLessThan (std::abs (comparison.getStereo().getCorrelation()), 0.3f);
            expect (comparison.getSpectrum().hasAverage());
            expectGreaterThan (comparison.getLoudness().getIntegratedLufs(), -30.0f);

            processor.releaseResources();
        }

        beginTest ("The comparison is measured after the trim");

        PluginProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);
        setChoice (processor, params::comparisonSource, (int) ComparisonSource::noise);
        setTrim (processor, -6.0f);
        run (processor, 1.0);
        expectWithinAbsoluteError (processor.getAnalyser (MonitorSource::comparison).getLevels().getRmsDb (0),
                                   NoiseGenerator::targetRmsDb - 6.0f,
                                   2.0f);
        processor.releaseResources();
    }

    void testSidechain()
    {
        beginTest ("The sidechain input becomes the comparison, so live audio can be compared");

        PluginProcessor processor;
        enableSidechain (processor);
        expect (processor.isSidechainConnected());
        processor.prepareToPlay (sampleRate, blockSize);
        setChoice (processor, params::comparisonSource, (int) ComparisonSource::sidechain);
        setChoice (processor, params::monitor, (int) MonitorSource::comparison);

        const auto output = run (processor, 1.0, 1000.0f, 0.25f, 2000.0f);
        processor.drainAnalysis();

        expectWithinAbsoluteError (outputDb (output), -9.03f, 0.5f);
        expectWithinAbsoluteError (processor.getAnalyser (MonitorSource::comparison).getLevels().getRmsDb (0), -9.03f, 0.1f);
        expectWithinAbsoluteError (processor.getAnalyser (MonitorSource::mix).getLevels().getRmsDb (0), -15.05f, 0.1f);
        processor.releaseResources();

        beginTest ("Choosing the sidechain with nothing connected is silent, not an error");

        PluginProcessor unconnected;
        expect (! unconnected.isSidechainConnected());
        unconnected.prepareToPlay (sampleRate, blockSize);
        setChoice (unconnected, params::comparisonSource, (int) ComparisonSource::sidechain);
        run (unconnected, 0.5);
        expectEquals (unconnected.getAnalyser (MonitorSource::comparison).getLevels().getRmsDb (0), silenceDb);
        unconnected.releaseResources();
    }

    void testOversizedBlocks()
    {
        beginTest ("Blocks bigger than promised are still handled");

        PluginProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);
        setChoice (processor, params::comparisonSource, (int) ComparisonSource::noise);
        setChoice (processor, params::monitor, (int) MonitorSource::comparison);
        run (processor, 0.1);

        juce::AudioBuffer<float> buffer (2, 20000);
        buffer.clear();
        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);

        // Beyond the internal chunk size, it should still be noise rather than the input's silence.
        expectWithinAbsoluteError (rmsDb (buffer.getReadPointer (0, 10000), 10000), NoiseGenerator::targetRmsDb, 2.0f);
        processor.releaseResources();
    }

    void testComparisonReset()
    {
        beginTest ("Changing the comparison forgets what was measured of the last one, but not the mix");

        PluginProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);
        setChoice (processor, params::comparisonSource, (int) ComparisonSource::noise);

        run (processor, 2.0);
        processor.drainAnalysis();

        const auto& comparison = processor.getAnalyser (MonitorSource::comparison);
        const auto& mix = processor.getAnalyser (MonitorSource::mix);
        expect (comparison.getSpectrum().hasAverage());

        setChoice (processor, params::noiseColour, (int) NoiseColour::brown);
        run (processor, 0.05);
        processor.drainAnalysis();

        expect (! comparison.getSpectrum().hasAverage());
        expect (mix.getSpectrum().hasAverage());
        processor.releaseResources();
    }

    void testAdvice()
    {
        beginTest ("The advice comes from what's been measured");

        PluginProcessor processor;
        expect (processor.getAdvice()[0].topic == Advice::Topic::status);

        processor.prepareToPlay (sampleRate, blockSize);
        setChoice (processor, params::comparisonSource, (int) ComparisonSource::noise);

        // A low tone is nothing like pink noise, and the advice should point at the lows.
        run (processor, 3.0, 100.0f);
        processor.drainAnalysis();

        const auto advice = processor.getAdvice();
        expect (advice[0].severity == Advice::Severity::major);
        expect (advice[0].topic == Advice::Topic::tone);

        const auto mentionsBass = std::any_of (advice.begin(), advice.end(), [] (const Advice& a) { return a.text.startsWith ("Bass") && a.text.contains ("stronger"); });
        expect (mentionsBass);

        processor.releaseResources();
    }

    void testState()
    {
        beginTest ("Settings and the chosen song survive a save and reload");

        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mixcompare_test_state.wav");
        file.deleteFile();

        juce::MemoryBlock state;
        {
            PluginProcessor processor;
            setChoice (processor, params::comparisonSource, (int) ComparisonSource::noise);
            setChoice (processor, params::noiseColour, (int) NoiseColour::brown);
            setChoice (processor, params::stereoMode, (int) StereoMode::lissajous);
            setChoice (processor, params::monitor, (int) MonitorSource::comparison);
            setTrim (processor, -9.0f);
            processor.getFilePlayer().load (file);
            processor.getStateInformation (state);
        }

        PluginProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());

        expect (restored.getComparisonSource() == ComparisonSource::noise);
        expect (restored.getStereoMode() == StereoMode::lissajous);
        expect (restored.getMonitorSource() == MonitorSource::comparison);
        expectWithinAbsoluteError (restored.getParameters().getRawParameterValue (params::comparisonTrim)->load(), -9.0f, 0.01f);
        expectEquals ((int) restored.getParameters().getRawParameterValue (params::noiseColour)->load(), (int) NoiseColour::brown);
        expectEquals (restored.getFilePlayer().getFile().getFullPathName(), file.getFullPathName());

        beginTest ("Garbage state is ignored");

        PluginProcessor other;
        const char garbage[] = "not a real state";
        other.setStateInformation (garbage, (int) sizeof (garbage));
        expect (other.getComparisonSource() == ComparisonSource::file);
    }
};

static ProcessorTests processorTests;

} // namespace mixcompare::test
