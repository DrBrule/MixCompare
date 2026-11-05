#pragma once

#include "PluginProcessor.h"
#include "ui/UnitViews.h"

namespace mixcompare
{

// Every unit's view and control, drawn in the theme chosen from the drop-down.
class PluginEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit PluginEditor (PluginProcessor& processor);
    ~PluginEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Brings every view up to date. Normally on a timer; public so tests can look straight away.
    void refresh();

    const Theme& getTheme() const noexcept { return lookAndFeel.getTheme(); }

private:
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    void timerCallback() override;
    void chooseFile();
    void refreshTransport();
    juce::String describeComparison() const;
    void applyTheme();

    PluginProcessor& pluginProcessor;

    // Before the components, so that it outlives them.
    ThemedLookAndFeel lookAndFeel;
    juce::SharedResourcePointer<ThemeLibrary> themes;

    juce::ComboBox monitorBox, sourceBox, noiseBox, stereoBox, themeBox;
    juce::TextButton loadButton { "Load song..." }, playButton { "Play" }, resetButton { "Reset averages" };
    juce::ToggleButton followButton { "Follow host" };
    juce::Slider trimSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider positionSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider resolutionSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label statusLabel, resolutionLabel { {}, "FFT resolution" };

    SpectrumView spectrumView;
    LoudnessView loudnessView;
    LevelReadout levelReadout;
    StereoView stereoView;
    AdviceView adviceView;
    ResizeBar adviceBar;

    ComboAttachment monitorAttachment, sourceAttachment, noiseAttachment, stereoAttachment;
    ButtonAttachment followAttachment;
    SliderAttachment trimAttachment, resolutionAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;
    int ticksUntilAdvice = 0;
    int adviceHeightAtDragStart = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};

} // namespace mixcompare
