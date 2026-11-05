#pragma once

#include "Parameters.h"
#include "advice/MixAdvisor.h"
#include "analysis/SourceAnalyser.h"
#include "sources/FilePlayer.h"
#include "sources/NoiseGenerator.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace mixcompare
{

class PluginProcessor final : public juce::AudioProcessor
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    FilePlayer& getFilePlayer() noexcept { return filePlayer; }

    const SourceAnalyser& getAnalyser (MonitorSource source) const noexcept;
    MonitorSource getMonitorSource() const noexcept;
    ComparisonSource getComparisonSource() const noexcept;
    StereoMode getStereoMode() const noexcept;
    bool isSidechainConnected() const;

    // Starts the integrated loudness and every average again, for both sources.
    void resetAnalysis() noexcept;

    // Moves queued audio into the slower analysers. The analysis thread keeps calling this, so
    // only tests need to, to make sure a result is up to date.
    void drainAnalysis();

    std::vector<Advice> getAdvice() const;

    // How much audio the advice has to go on: the less of the two sources' amounts.
    double getAdviceSeconds() const noexcept;

    // Part of the plugin's state, so that the layout comes back with the session.
    int getAdviceHeight() const noexcept { return adviceHeight.load(); }
    void setAdviceHeight (int height) noexcept { adviceHeight.store (height); }

    // By name, so that a session keeps its theme if the list of themes changes. Empty means the
    // default one.
    juce::String getThemeName() const;
    void setThemeName (const juce::String& name);

private:
    class AnalysisThread;

    static constexpr int chunkSize = 4096; // Longer blocks are worked through in pieces.

    void processChunk (juce::AudioBuffer<float>& main,
                       const juce::AudioBuffer<float>& sidechain,
                       int start,
                       int numSamples,
                       bool hostIsPlaying) noexcept;
    void renderComparison (const juce::AudioBuffer<float>& sidechain, int start, int numSamples, bool hostIsPlaying) noexcept;
    void resetComparisonIfChanged() noexcept;
    SourceProfile getProfile (const SourceAnalyser& analyser) const;

    juce::AudioProcessorValueTreeState parameters;
    const std::atomic<float>& monitorValue;
    const std::atomic<float>& comparisonSourceValue;
    const std::atomic<float>& noiseColourValue;
    const std::atomic<float>& comparisonTrimValue;
    const std::atomic<float>& followHostValue;
    const std::atomic<float>& stereoModeValue;
    const std::atomic<float>& fftOrderValue;
    std::atomic<int> adviceHeight { 150 };
    juce::String themeName;
    juce::SpinLock themeLock; // Some hosts ask for the state off the message thread.

    FilePlayer filePlayer;
    NoiseGenerator noise;
    SourceAnalyser mixAnalyser, comparisonAnalyser;

    juce::AudioBuffer<float> comparison { 2, chunkSize };
    juce::SmoothedValue<float> comparisonGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> monitorFade;

    int lastSource = -1, lastColour = -1, lastGeneration = -1;

    juce::CriticalSection analysisLock;
    std::unique_ptr<AnalysisThread> analysisThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};

} // namespace mixcompare
