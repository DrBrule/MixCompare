#include "PluginProcessor.h"

#include "ui/PluginEditor.h"

namespace mixcompare
{

namespace
{
constexpr auto comparisonFileProperty = "comparisonFile";
constexpr auto adviceHeightProperty = "adviceHeight";
constexpr auto themeProperty = "theme";
constexpr double monitorFadeSeconds = 0.02;
constexpr double trimSmoothingSeconds = 0.02;

const std::atomic<float>& rawValue (juce::AudioProcessorValueTreeState& state, const char* id)
{
    auto* value = state.getRawParameterValue (id);
    jassert (value != nullptr);
    return *value;
}
} // namespace

class PluginProcessor::AnalysisThread final : public juce::Thread
{
public:
    explicit AnalysisThread (PluginProcessor& p) : Thread ("Mix Compare analysis"), processor (p) {}

    void run() override
    {
        while (! threadShouldExit())
        {
            processor.drainAnalysis();
            wait (15);
        }
    }

private:
    PluginProcessor& processor;
};

PluginProcessor::PluginProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      parameters (*this, nullptr, "MixCompare", createParameterLayout()),
      monitorValue (rawValue (parameters, params::monitor)),
      comparisonSourceValue (rawValue (parameters, params::comparisonSource)),
      noiseColourValue (rawValue (parameters, params::noiseColour)),
      comparisonTrimValue (rawValue (parameters, params::comparisonTrim)),
      followHostValue (rawValue (parameters, params::followHost)),
      stereoModeValue (rawValue (parameters, params::stereoMode)),
      fftOrderValue (rawValue (parameters, params::fftOrder)),
      analysisThread (std::make_unique<AnalysisThread> (*this))
{
}

PluginProcessor::~PluginProcessor()
{
    analysisThread->stopThread (2000);
}

void PluginProcessor::prepareToPlay (double sampleRate, int)
{
    {
        const juce::ScopedLock lock (analysisLock);
        mixAnalyser.prepare (sampleRate);
        comparisonAnalyser.prepare (sampleRate);
    }

    filePlayer.prepare (sampleRate);
    noise.prepare (sampleRate);

    comparisonGain.reset (sampleRate, trimSmoothingSeconds);
    comparisonGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (comparisonTrimValue.load()));
    monitorFade.reset (sampleRate, monitorFadeSeconds);
    monitorFade.setCurrentAndTargetValue ((float) (getMonitorSource() == MonitorSource::comparison));

    analysisThread->startThread();
}

void PluginProcessor::releaseResources()
{
    analysisThread->stopThread (2000);
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();
    const auto main = layouts.getMainOutputChannelSet();

    if ((main != mono && main != stereo) || layouts.getMainInputChannelSet() != main)
        return false;

    if (layouts.inputBuses.size() < 2)
        return true;

    const auto sidechain = layouts.inputBuses.getReference (1);
    return sidechain.isDisabled() || sidechain == mono || sidechain == stereo;
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const juce::ScopedNoDenormals noDenormals;

    auto main = getBusBuffer (buffer, true, 0);
    const auto sidechain = getBusBuffer (buffer, true, 1);

    if (main.getNumChannels() == 0)
        return;

    auto hostIsPlaying = true; // Without a transport to follow, as in the standalone app, just play.

    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            hostIsPlaying = position->getIsPlaying();

    resetComparisonIfChanged();
    comparisonGain.setTargetValue (juce::Decibels::decibelsToGain (comparisonTrimValue.load()));
    monitorFade.setTargetValue ((float) (getMonitorSource() == MonitorSource::comparison));

    for (int start = 0; start < main.getNumSamples(); start += chunkSize)
        processChunk (main, sidechain, start, juce::jmin (chunkSize, main.getNumSamples() - start), hostIsPlaying);
}

void PluginProcessor::resetComparisonIfChanged() noexcept
{
    // What's been measured of the old comparison says nothing about the new one.
    const auto source = (int) comparisonSourceValue.load();
    const auto colour = (int) noiseColourValue.load();
    const auto generation = filePlayer.getGeneration();

    const auto changed = source != lastSource
                         || (source == (int) ComparisonSource::noise && colour != lastColour)
                         || (source == (int) ComparisonSource::file && generation != lastGeneration);

    if (changed)
        comparisonAnalyser.requestReset();

    lastSource = source;
    lastColour = colour;
    lastGeneration = generation;
}

void PluginProcessor::renderComparison (const juce::AudioBuffer<float>& sidechain, int start, int numSamples, bool hostIsPlaying) noexcept
{
    auto* const* channels = comparison.getArrayOfWritePointers();

    switch ((ComparisonSource) (int) comparisonSourceValue.load())
    {
        case ComparisonSource::file:
            filePlayer.process (channels, numSamples, filePlayer.isPlaying() && (hostIsPlaying || followHostValue.load() < 0.5f));
            break;

        case ComparisonSource::noise:
            noise.process (channels, numSamples, (NoiseColour) (int) noiseColourValue.load());
            break;

        case ComparisonSource::sidechain:
            if (sidechain.getNumChannels() > 0)
            {
                comparison.copyFrom (0, 0, sidechain, 0, start, numSamples);
                comparison.copyFrom (1, 0, sidechain, juce::jmin (1, sidechain.getNumChannels() - 1), start, numSamples);
            }
            else
            {
                comparison.clear (0, numSamples);
            }
            break;
    }

    // The analyser comes after the trim, so the metrics always describe what's being heard.
    comparisonGain.applyGain (comparison, numSamples);
}

void PluginProcessor::processChunk (juce::AudioBuffer<float>& main,
                                    const juce::AudioBuffer<float>& sidechain,
                                    int start,
                                    int numSamples,
                                    bool hostIsPlaying) noexcept
{
    const auto numChannels = main.getNumChannels();
    auto* left = main.getWritePointer (0) + start;
    auto* right = numChannels > 1 ? main.getWritePointer (1) + start : left;

    mixAnalyser.processAudio (left, right, numChannels, numSamples);

    renderComparison (sidechain, start, numSamples, hostIsPlaying);
    const auto* comparisonLeft = comparison.getReadPointer (0);
    const auto* comparisonRight = comparison.getReadPointer (1);
    comparisonAnalyser.processAudio (comparisonLeft, comparisonRight, 2, numSamples);

    if (! monitorFade.isSmoothing() && monitorFade.getCurrentValue() < 0.5f)
        return; // Listening to the mix, which is already in the output.

    for (int i = 0; i < numSamples; ++i)
    {
        const auto fade = monitorFade.getNextValue();

        if (numChannels > 1)
        {
            left[i] += fade * (comparisonLeft[i] - left[i]);
            right[i] += fade * (comparisonRight[i] - right[i]);
        }
        else
        {
            left[i] += fade * (0.5f * (comparisonLeft[i] + comparisonRight[i]) - left[i]);
        }
    }
}

void PluginProcessor::drainAnalysis()
{
    const juce::ScopedNoDenormals noDenormals;
    const juce::ScopedLock lock (analysisLock);
    const auto fftOrder = (int) fftOrderValue.load();
    mixAnalyser.setDisplayOrder (fftOrder);
    comparisonAnalyser.setDisplayOrder (fftOrder);
    mixAnalyser.drain();
    comparisonAnalyser.drain();
}

void PluginProcessor::resetAnalysis() noexcept
{
    mixAnalyser.requestReset();
    comparisonAnalyser.requestReset();
}

const SourceAnalyser& PluginProcessor::getAnalyser (MonitorSource source) const noexcept
{
    return source == MonitorSource::mix ? mixAnalyser : comparisonAnalyser;
}

MonitorSource PluginProcessor::getMonitorSource() const noexcept
{
    return (MonitorSource) (int) monitorValue.load();
}

ComparisonSource PluginProcessor::getComparisonSource() const noexcept
{
    return (ComparisonSource) (int) comparisonSourceValue.load();
}

StereoMode PluginProcessor::getStereoMode() const noexcept
{
    return (StereoMode) (int) stereoModeValue.load();
}

bool PluginProcessor::isSidechainConnected() const
{
    return getChannelCountOfBus (true, 1) > 0;
}

SourceProfile PluginProcessor::getProfile (const SourceAnalyser& analyser) const
{
    SourceProfile profile;
    profile.hasData = analyser.getSpectrum().hasAverage();
    profile.integratedLufs = analyser.getLoudness().getIntegratedLufs();
    profile.crestDb = analyser.getLevels().getAverageCrestDb();
    profile.correlation = analyser.getStereo().getAverageCorrelation();
    profile.sideRatio = analyser.getStereo().getAverageSideRatio();
    profile.bands = analyser.getSpectrum().getAverageBands();
    return profile;
}

std::vector<Advice> PluginProcessor::getAdvice() const
{
    AdviceInput input;
    input.mix = getProfile (mixAnalyser);
    input.comparison = getProfile (comparisonAnalyser);
    input.comparisonIsNoise = getComparisonSource() == ComparisonSource::noise;
    input.comparisonTrimDb = comparisonTrimValue.load();
    return getMixAdvice (input);
}

double PluginProcessor::getAdviceSeconds() const noexcept
{
    return juce::jmin (mixAnalyser.getSpectrum().getAverageSeconds(), comparisonAnalyser.getSpectrum().getAverageSeconds());
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty (comparisonFileProperty, filePlayer.getFile().getFullPathName(), nullptr);
    state.setProperty (adviceHeightProperty, adviceHeight.load(), nullptr);
    state.setProperty (themeProperty, getThemeName(), nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    parameters.replaceState (juce::ValueTree::fromXml (*xml));
    adviceHeight.store (parameters.state.getProperty (adviceHeightProperty, adviceHeight.load()));
    setThemeName (parameters.state.getProperty (themeProperty).toString());

    // A session opened on another machine may not have the song, which shows up as a failed load.
    if (const auto path = parameters.state.getProperty (comparisonFileProperty).toString(); path.isNotEmpty())
        filePlayer.load (juce::File (path));
}

juce::String PluginProcessor::getThemeName() const
{
    const juce::SpinLock::ScopedLockType lock (themeLock);
    return themeName;
}

void PluginProcessor::setThemeName (const juce::String& name)
{
    const juce::SpinLock::ScopedLockType lock (themeLock);
    themeName = name;
}

} // namespace mixcompare

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mixcompare::PluginProcessor();
}
