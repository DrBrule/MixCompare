#pragma once

#include "PluginProcessor.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mixcompare
{

// Plain views onto each unit, one per job, so that each can be redesigned on its own later.
// Each repaints from the processor's latest readings when its update() is called.

class SpectrumView final : public juce::Component
{
public:
    explicit SpectrumView (PluginProcessor& p) : processor (p) {}

    void update() { repaint(); }
    void paint (juce::Graphics& g) override;

private:
    PluginProcessor& processor;
    SpectrumAnalyser::Spectrum spectrum {};
};

// Always shows both sources, side by side.
class LoudnessView final : public juce::Component
{
public:
    explicit LoudnessView (PluginProcessor& p) : processor (p) {}

    void update() { repaint(); }
    void paint (juce::Graphics& g) override;

private:
    PluginProcessor& processor;
};

// Follows whichever source is being monitored.
class LevelReadout final : public juce::Component
{
public:
    explicit LevelReadout (PluginProcessor& p) : processor (p) {}

    void update() { repaint(); }
    void paint (juce::Graphics& g) override;

private:
    PluginProcessor& processor;
};

// Follows whichever source is being monitored.
class StereoView final : public juce::Component
{
public:
    explicit StereoView (PluginProcessor& p) : processor (p) {}

    void update() { repaint(); }
    void paint (juce::Graphics& g) override;

private:
    void paintPlot (juce::Graphics& g, const Theme& theme, juce::Rectangle<float> plot, MonitorSource source, StereoMode mode);
    void paintStereometer (juce::Graphics& g, const Theme& theme, juce::Rectangle<int> area, MonitorSource source);

    PluginProcessor& processor;
    StereoField::Scope scope;
};

// Advice follows the audio until it has enough to go on, then stays put so that it can be read,
// until the button asks for it again. If the averages start over, it goes back to following.
class AdviceView final : public juce::Component
{
public:
    explicit AdviceView (PluginProcessor& p);

    void update();
    void evaluate();
    void resized() override;
    void paint (juce::Graphics& g) override;
    void lookAndFeelChanged() override;

    juce::String getText() const { return text.getText(); }

private:
    void showStatus();

    PluginProcessor& processor;
    juce::Label status;
    juce::TextButton evaluateButton { "Re-evaluate" };
    juce::TextEditor text;
    bool holding = false;
    double evaluatedSeconds = 0.0;
};

// The bar between two stacked sections. Reports how far it's been dragged since the mouse went
// down, and leaves it to the owner to decide what to do with that.
class ResizeBar final : public juce::Component
{
public:
    ResizeBar();

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;

    std::function<void()> onDragStart;
    std::function<void (int distance)> onDrag;
};

} // namespace mixcompare
