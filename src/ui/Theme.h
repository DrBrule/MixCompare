#pragma once

#include "Parameters.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mixcompare
{

// The colours and font for the whole interface. The themes are defined in resources/themes.json,
// which is built into the plugin.
//
// Each source has a core colour, used for its instantaneous readings, and variations of it:
// average for long-term readings drawn over those, dim for things in the background, and text for
// its name and numbers, which needs more contrast than a line does.
struct Theme
{
    struct SourceColours
    {
        juce::Colour core, average, dim, text;
    };

    juce::String name;
    juce::Colour background, panel, grid, text, textDim;
    SourceColours mix, comparison;

    juce::String fontName;
    juce::Typeface::Ptr typeface; // Null when the font isn't built in, which looks it up by name.

    const SourceColours& of (MonitorSource source) const noexcept { return source == MonitorSource::mix ? mix : comparison; }
    juce::Font font (float height) const;

    // Used when no theme has been set, so that a view can always be drawn.
    static const Theme& fallback();
};

// Loads the themes once and shares them between every open editor.
class ThemeLibrary
{
public:
    ThemeLibrary();

    const std::vector<Theme>& getThemes() const noexcept { return themes; }

    // The default theme for an empty or unknown name.
    const Theme& find (const juce::String& name) const;

    // Reads a theme file. Only the background, mix and comparison colours are required, and any
    // other colour missing is made from those, so a new theme can be as short as that.
    // Returns the name of the default theme, or an empty string if the file can't be read.
    static juce::String parse (const juce::String& json,
                               const std::function<juce::Typeface::Ptr (const juce::String& fontName)>& findFont,
                               std::vector<Theme>& themes);

private:
    std::vector<juce::Typeface::Ptr> fonts;
    std::vector<Theme> themes;
    juce::String defaultName;
};

// Applies a theme to JUCE's own controls, and holds it for the views to draw with.
class ThemedLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ThemedLookAndFeel();

    void setTheme (const Theme& newTheme);
    const Theme& getTheme() const noexcept { return theme; }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getSliderPopupFont (juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour, bool isHighlighted, bool isDown) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool isHighlighted, bool isDown) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isDown, int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

private:
    Theme theme;
};

// The theme a component is drawn with: its look and feel's, or the fallback.
const Theme& themeOf (const juce::Component& component);

// The rounded panel each view sits on.
void paintPanel (juce::Graphics& g, const juce::Component& component, const Theme& theme);

} // namespace mixcompare
