#include "ui/PluginEditor.h"

namespace mixcompare
{

namespace
{
constexpr int refreshHz = 30;
constexpr int minAdviceHeight = 80;
constexpr int minStereoHeight = 110;
constexpr int minLoudnessHeight = 180;

// Enough for the stereo display, the LUFS meters and the rows beside them.
constexpr int minHeightAboveAdvice = 360;

// The attachments need the boxes filled before they connect, and the members initialise in
// order, so this happens on the way through the initialiser list.
juce::ComboBox& filled (juce::ComboBox& box, const juce::StringArray& items)
{
    box.addItemList (items, 1);
    return box;
}
} // namespace

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (p),
      pluginProcessor (p),
      spectrumView (p),
      loudnessView (p),
      levelReadout (p),
      stereoView (p),
      adviceView (p),
      monitorAttachment (p.getParameters(), params::monitor, filled (monitorBox, { "Mix", "Comparison" })),
      sourceAttachment (p.getParameters(), params::comparisonSource, filled (sourceBox, { "File", "Noise", "Sidechain" })),
      noiseAttachment (p.getParameters(), params::noiseColour, filled (noiseBox, { "White", "Pink", "Brown" })),
      stereoAttachment (p.getParameters(), params::stereoMode, filled (stereoBox, { "Goniometer", "Lissajous", "Stereometer" })),
      followAttachment (p.getParameters(), params::followHost, followButton),
      trimAttachment (p.getParameters(), params::comparisonTrim, trimSlider),
      resolutionAttachment (p.getParameters(), params::fftOrder, resolutionSlider)
{
    for (juce::Component* child : std::initializer_list<juce::Component*> {
             &monitorBox, &sourceBox, &noiseBox, &stereoBox, &loadButton, &playButton, &resetButton, &followButton,
             &trimSlider, &positionSlider, &resolutionSlider, &resolutionLabel, &statusLabel, &spectrumView, &loudnessView,
             &levelReadout, &stereoView, &adviceView, &adviceBar, &themeBox })
        addAndMakeVisible (child);

    setLookAndFeel (&lookAndFeel);

    for (const auto& theme : themes->getThemes())
        themeBox.addItem (theme.name, themeBox.getNumItems() + 1);

    themeBox.setComponentID ("theme");
    themeBox.setTooltip ("Theme");
    themeBox.onChange = [this]
    {
        pluginProcessor.setThemeName (themeBox.getText());
        applyTheme();
    };
    applyTheme();

    monitorBox.setComponentID ("monitor");
    sourceBox.setComponentID ("source");
    noiseBox.setComponentID ("noise");
    stereoBox.setComponentID ("stereo");
    resolutionSlider.setComponentID ("resolution");
    adviceView.setComponentID ("advice");
    adviceBar.setComponentID ("adviceBar");
    spectrumView.setComponentID ("spectrum");
    stereoView.setComponentID ("stereoView");

    // Replaces the attachment's text, as the number that matters most depends on the sample rate.
    resolutionSlider.textFromValueFunction = [this] (double value)
    {
        const auto size = 1 << (int) value;
        const auto rate = pluginProcessor.getSampleRate();
        return juce::String (size) + (rate > 0.0 ? " pts, " + juce::String (rate / size, rate / size < 10.0 ? 2 : 1) + " Hz" : " pts");
    };
    resolutionSlider.setTextBoxStyle (juce::Slider::TextBoxRight, true, 130, 22);
    resolutionSlider.updateText();

    adviceBar.onDragStart = [this] { adviceHeightAtDragStart = pluginProcessor.getAdviceHeight(); };
    adviceBar.onDrag = [this] (int distance)
    {
        // Dragging up makes the advice taller. resized() keeps it to what the window has room for.
        pluginProcessor.setAdviceHeight (juce::jmax (minAdviceHeight, adviceHeightAtDragStart - distance));
        resized();
    };

    trimSlider.setTextValueSuffix (" dB");
    positionSlider.setTextValueSuffix (" s");
    positionSlider.setRange (0.0, 1.0);
    positionSlider.setNumDecimalPlacesToDisplay (1);

    loadButton.onClick = [this] { chooseFile(); };
    resetButton.onClick = [this] { pluginProcessor.resetAnalysis(); };
    playButton.onClick = [this]
    {
        auto& player = pluginProcessor.getFilePlayer();
        player.setPlaying (! player.isPlaying());
    };
    positionSlider.onDragEnd = [this] { pluginProcessor.getFilePlayer().seek (positionSlider.getValue()); };

    setResizable (true, true);
    setResizeLimits (940, 560, 2400, 1600);
    setSize (1100, 720);

    refresh();
    startTimerHz (refreshHz);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (getTheme().background);
}

void PluginEditor::applyTheme()
{
    const auto& theme = themes->find (pluginProcessor.getThemeName());
    lookAndFeel.setTheme (theme);
    themeBox.setText (theme.name, juce::dontSendNotification);

    for (auto* label : { &statusLabel, &resolutionLabel })
        label->setColour (juce::Label::textColourId, theme.textDim);

    sendLookAndFeelChange();
    repaint();
}

void PluginEditor::resized()
{
    constexpr int gap = 8;
    constexpr int rowHeight = 26;

    auto area = getLocalBounds().reduced (gap);

    const auto takeLabelled = [] (juce::Rectangle<int>& row, juce::Component& component, int width)
    {
        component.setBounds (row.removeFromLeft (width));
        row.removeFromLeft (gap);
    };

    auto top = area.removeFromTop (rowHeight);
    takeLabelled (top, monitorBox, 130);
    takeLabelled (top, sourceBox, 120);
    takeLabelled (top, noiseBox, 90);
    takeLabelled (top, loadButton, 100);
    takeLabelled (top, playButton, 60);
    takeLabelled (top, followButton, 110);
    takeLabelled (top, resetButton, 120);
    trimSlider.setBounds (top);
    area.removeFromTop (gap);

    auto second = area.removeFromTop (rowHeight);
    themeBox.setBounds (second.removeFromRight (130));
    second.removeFromRight (gap);
    statusLabel.setBounds (second.removeFromLeft (second.getWidth() / 2));
    second.removeFromLeft (gap);
    positionSlider.setBounds (second);
    area.removeFromTop (gap);

    // Keeps what the user asked for even when the window can't fit it, so that making the window
    // bigger again brings it back.
    const auto adviceRoom = juce::jmax (minAdviceHeight, area.getHeight() - minHeightAboveAdvice - gap);
    adviceView.setBounds (area.removeFromBottom (juce::jlimit (minAdviceHeight, adviceRoom, pluginProcessor.getAdviceHeight())));
    adviceBar.setBounds (area.removeFromBottom (gap));

    levelReadout.setBounds (area.removeFromBottom (rowHeight));
    area.removeFromBottom (gap);

    auto side = area.removeFromRight (240);
    area.removeFromRight (gap);

    // The stereo display is square when there's room for that, but leaves the LUFS meters theirs.
    stereoBox.setBounds (side.removeFromTop (rowHeight));
    stereoView.setBounds (side.removeFromTop (juce::jmin (side.getWidth(), juce::jmax (minStereoHeight, side.getHeight() - minLoudnessHeight - gap))));
    side.removeFromTop (gap);
    loudnessView.setBounds (side);

    auto resolutionRow = area.removeFromTop (rowHeight);
    resolutionLabel.setBounds (resolutionRow.removeFromLeft (100));
    resolutionSlider.setBounds (resolutionRow);
    area.removeFromTop (gap);

    spectrumView.setBounds (area);
}

void PluginEditor::refresh()
{
    spectrumView.update();
    loudnessView.update();
    levelReadout.update();
    stereoView.update();
    adviceView.update();
    refreshTransport();
}

void PluginEditor::timerCallback()
{
    spectrumView.update();
    loudnessView.update();
    levelReadout.update();
    stereoView.update();
    refreshTransport();

    resolutionSlider.updateText(); // The sample rate can change at any time.

    // A host loading a preset can change the theme while the editor is open.
    if (const auto& wanted = themes->find (pluginProcessor.getThemeName()); wanted.name != getTheme().name)
        applyTheme();

    // Advice is about long averages and has to stay still long enough to read.
    if (--ticksUntilAdvice <= 0)
    {
        ticksUntilAdvice = refreshHz;
        adviceView.update();
    }
}

void PluginEditor::refreshTransport()
{
    const auto usingFile = pluginProcessor.getComparisonSource() == ComparisonSource::file;
    auto& player = pluginProcessor.getFilePlayer();

    noiseBox.setEnabled (pluginProcessor.getComparisonSource() == ComparisonSource::noise);
    loadButton.setEnabled (usingFile);
    playButton.setEnabled (usingFile);
    followButton.setEnabled (usingFile);
    positionSlider.setEnabled (usingFile && player.getStatus() == FilePlayer::Status::ready);
    playButton.setButtonText (player.isPlaying() ? "Stop" : "Play");

    if (const auto length = player.getLengthSeconds(); length > 0.0 && ! positionSlider.isMouseButtonDown())
    {
        positionSlider.setRange (0.0, length, juce::dontSendNotification);
        positionSlider.setValue (player.getPositionSeconds(), juce::dontSendNotification);
    }

    statusLabel.setText (describeComparison(), juce::dontSendNotification);
}

juce::String PluginEditor::describeComparison() const
{
    auto& player = pluginProcessor.getFilePlayer();

    switch (pluginProcessor.getComparisonSource())
    {
        case ComparisonSource::noise:
            return "Comparing with noise";

        case ComparisonSource::sidechain:
            return pluginProcessor.isSidechainConnected() ? "Comparing with the sidechain input"
                                                          : "Route a track or streaming app to this plugin's sidechain input";

        case ComparisonSource::file:
            break;
    }

    switch (player.getStatus())
    {
        case FilePlayer::Status::empty: return "Load a song to compare with";
        case FilePlayer::Status::loading: return "Loading " + player.getFile().getFileName() + "...";
        case FilePlayer::Status::ready: return "Comparing with " + player.getFile().getFileName();
        case FilePlayer::Status::failed: return player.getError();
    }

    return {};
}

void PluginEditor::chooseFile()
{
    auto& player = pluginProcessor.getFilePlayer();
    fileChooser = std::make_unique<juce::FileChooser> ("Choose a song to compare with", juce::File(), player.getFileWildcard());

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
                              {
                                  if (const auto file = chooser.getResult(); file.existsAsFile())
                                      pluginProcessor.getFilePlayer().load (file);
                              });
}

} // namespace mixcompare
