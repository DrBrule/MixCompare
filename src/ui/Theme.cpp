#include "ui/Theme.h"

#include <BinaryData.h>

namespace mixcompare
{

namespace
{
constexpr float cornerSize = 4.0f;

juce::Colour toward (juce::Colour from, juce::Colour to, float amount)
{
    return from.interpolatedWith (to, amount);
}

bool isLight (juce::Colour colour)
{
    return colour.getPerceivedBrightness() > 0.5f;
}

// Colours are written as [r, g, b].
std::optional<juce::Colour> readColour (const juce::var& colours, const char* key)
{
    const auto* rgb = colours[key].getArray();

    if (rgb == nullptr || rgb->size() != 3)
        return std::nullopt;

    const auto channel = [&] (int index) { return (juce::uint8) juce::jlimit (0, 255, (int) (*rgb)[index]); };
    return juce::Colour (channel (0), channel (1), channel (2));
}

// The variations made when a theme leaves them out: away from the background for emphasis, and
// towards it for things that should sit back.
Theme::SourceColours readSource (const juce::var& colours, const juce::String& key, juce::Colour core, juce::Colour background)
{
    const auto light = isLight (background);
    const auto ink = light ? juce::Colours::black : juce::Colours::white;
    const auto read = [&] (const char* suffix, juce::Colour otherwise)
    { return readColour (colours, (key + suffix).toRawUTF8()).value_or (otherwise); };

    return { core,
             read ("Average", toward (core, ink, 0.45f)),
             read ("Dim", toward (core, background, 0.5f)),
             read ("Text", light ? toward (core, juce::Colours::black, 0.5f) : toward (core, juce::Colours::white, 0.25f)) };
}

std::optional<Theme> readTheme (const juce::var& entry, const std::function<juce::Typeface::Ptr (const juce::String&)>& findFont)
{
    const auto colours = entry["colours"];
    const auto background = readColour (colours, "background");
    const auto mix = readColour (colours, "mix");
    const auto comparison = readColour (colours, "comparison");
    const auto name = entry["name"].toString();

    if (name.isEmpty() || ! background || ! mix || ! comparison)
        return std::nullopt;

    const auto ink = isLight (*background) ? juce::Colours::black : juce::Colours::white;
    const auto read = [&] (const char* key, float amount) { return readColour (colours, key).value_or (toward (*background, ink, amount)); };

    Theme theme;
    theme.name = name;
    theme.background = *background;
    theme.panel = read ("panel", 0.05f);
    theme.grid = read ("grid", 0.16f);
    theme.text = read ("text", 0.85f);
    theme.textDim = read ("textDim", 0.5f);
    theme.mix = readSource (colours, "mix", *mix, *background);
    theme.comparison = readSource (colours, "comparison", *comparison, *background);
    theme.fontName = entry["font"].toString();
    theme.typeface = findFont (theme.fontName);
    return theme;
}
} // namespace

juce::Font Theme::font (float height) const
{
    if (typeface != nullptr)
        return juce::Font (juce::FontOptions (typeface).withHeight (height));

    return juce::Font (juce::FontOptions (fontName, height, juce::Font::plain));
}

const Theme& Theme::fallback()
{
    static const auto theme = []
    {
        Theme t;
        t.name = "Fallback";
        t.background = juce::Colour (28, 28, 28);
        t.panel = juce::Colour (39, 39, 39);
        t.grid = juce::Colour (64, 64, 64);
        t.text = juce::Colour (221, 221, 221);
        t.textDim = juce::Colour (142, 142, 142);
        t.mix = { juce::Colour (190, 0, 190), juce::Colour (219, 115, 219), juce::Colour (109, 14, 109), juce::Colour (206, 64, 206) };
        t.comparison = { juce::Colour (0, 76, 153), juce::Colour (115, 157, 199), juce::Colour (14, 52, 90), juce::Colour (64, 121, 178) };
        return t;
    }();

    return theme;
}

ThemeLibrary::ThemeLibrary()
{
    // Every font file built in is available to the themes, by its family name.
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const juce::String file (BinaryData::originalFilenames[i]);

        if (! file.endsWithIgnoreCase (".ttf") && ! file.endsWithIgnoreCase (".otf"))
            continue;

        int size = 0;

        if (const auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size))
            if (auto typeface = juce::Typeface::createSystemTypefaceFor (data, (size_t) size))
                fonts.push_back (typeface);
    }

    const auto findFont = [this] (const juce::String& name) -> juce::Typeface::Ptr
    {
        for (const auto& typeface : fonts)
            if (typeface->getName().equalsIgnoreCase (name))
                return typeface;

        return nullptr;
    };

    int size = 0;
    const auto* json = BinaryData::getNamedResource ("themes_json", size);
    defaultName = parse (json != nullptr ? juce::String::fromUTF8 (json, size) : juce::String(), findFont, themes);
    jassert (! themes.empty()); // resources/themes.json has a mistake in it.

    if (themes.empty())
        themes.push_back (Theme::fallback());
}

const Theme& ThemeLibrary::find (const juce::String& name) const
{
    for (const auto& wanted : { name, defaultName })
        for (const auto& theme : themes)
            if (theme.name == wanted)
                return theme;

    return themes.front();
}

juce::String ThemeLibrary::parse (const juce::String& json,
                                  const std::function<juce::Typeface::Ptr (const juce::String&)>& findFont,
                                  std::vector<Theme>& out)
{
    const auto root = juce::JSON::parse (json);

    if (const auto* entries = root["themes"].getArray())
        for (const auto& entry : *entries)
            if (auto theme = readTheme (entry, findFont))
                out.push_back (std::move (*theme));

    if (out.empty())
        return {};

    const auto defaultName = root["defaultTheme"].toString();
    return defaultName.isNotEmpty() ? defaultName : out.front().name;
}

ThemedLookAndFeel::ThemedLookAndFeel()
{
    setTheme (Theme::fallback());
}

void ThemedLookAndFeel::setTheme (const Theme& newTheme)
{
    theme = newTheme;

    const auto transparent = juce::Colours::transparentBlack;

    // Controls stand a little forward of the panels, so they show up on them too.
    const auto control = toward (theme.panel, theme.text, 0.07f);

    for (const auto& [id, colour] : std::initializer_list<std::pair<int, juce::Colour>> {
             { juce::ResizableWindow::backgroundColourId, theme.background },
             { juce::DocumentWindow::textColourId, theme.text },
             { juce::Label::textColourId, theme.text },
             { juce::Label::backgroundColourId, transparent },
             { juce::Label::outlineColourId, transparent },
             { juce::TextButton::buttonColourId, control },
             { juce::TextButton::buttonOnColourId, theme.grid },
             { juce::TextButton::textColourOffId, theme.text },
             { juce::TextButton::textColourOnId, theme.text },
             { juce::ToggleButton::textColourId, theme.text },
             { juce::ToggleButton::tickColourId, theme.text },
             { juce::ToggleButton::tickDisabledColourId, theme.textDim },
             { juce::ComboBox::backgroundColourId, control },
             { juce::ComboBox::textColourId, theme.text },
             { juce::ComboBox::outlineColourId, transparent },
             { juce::ComboBox::arrowColourId, theme.textDim },
             { juce::ComboBox::focusedOutlineColourId, theme.grid },
             { juce::PopupMenu::backgroundColourId, theme.panel },
             { juce::PopupMenu::textColourId, theme.text },
             { juce::PopupMenu::highlightedBackgroundColourId, theme.grid },
             { juce::PopupMenu::highlightedTextColourId, theme.text },
             { juce::PopupMenu::headerTextColourId, theme.textDim },
             { juce::Slider::backgroundColourId, theme.grid },
             { juce::Slider::trackColourId, theme.textDim },
             { juce::Slider::thumbColourId, theme.text },
             { juce::Slider::textBoxTextColourId, theme.text },
             { juce::Slider::textBoxBackgroundColourId, transparent },
             { juce::Slider::textBoxOutlineColourId, transparent },
             { juce::Slider::textBoxHighlightColourId, theme.grid },
             { juce::TextEditor::backgroundColourId, theme.panel },
             { juce::TextEditor::textColourId, theme.text },
             { juce::TextEditor::highlightColourId, theme.grid },
             { juce::TextEditor::highlightedTextColourId, theme.text },
             { juce::TextEditor::outlineColourId, transparent },
             { juce::TextEditor::focusedOutlineColourId, transparent },
             { juce::CaretComponent::caretColourId, theme.text },
             { juce::ScrollBar::thumbColourId, theme.grid },
             { juce::ScrollBar::trackColourId, transparent },
             { juce::AlertWindow::backgroundColourId, theme.panel },
             { juce::AlertWindow::textColourId, theme.text },
             { juce::AlertWindow::outlineColourId, theme.grid } })
        setColour (id, colour);
}

juce::Font ThemedLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return theme.font (juce::jmin (11.0f, (float) buttonHeight * 0.5f));
}

juce::Font ThemedLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return theme.font (juce::jmin (11.0f, (float) box.getHeight() * 0.5f));
}

juce::Font ThemedLookAndFeel::getPopupMenuFont()
{
    return theme.font (12.0f);
}

juce::Font ThemedLookAndFeel::getLabelFont (juce::Label& label)
{
    return theme.font (juce::jmin (11.0f, label.getFont().getHeight()));
}

juce::Font ThemedLookAndFeel::getSliderPopupFont (juce::Slider&)
{
    return theme.font (11.0f);
}

void ThemedLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour, bool isHighlighted, bool isDown)
{
    auto colour = button.getToggleState() ? theme.grid : backgroundColour;

    if (isDown)
        colour = toward (colour, theme.text, 0.2f);
    else if (isHighlighted)
        colour = toward (colour, theme.text, 0.08f);

    g.setColour (button.isEnabled() ? colour : colour.withMultipliedAlpha (0.5f));
    g.fillRoundedRectangle (button.getLocalBounds().toFloat(), cornerSize);
}

void ThemedLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool isHighlighted, bool)
{
    const auto bounds = button.getLocalBounds().toFloat();
    const auto box = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ bounds.getX() + 10.0f, bounds.getCentreY() });
    const auto alpha = button.isEnabled() ? 1.0f : 0.4f;

    g.setColour ((isHighlighted ? toward (theme.grid, theme.text, 0.15f) : theme.grid).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (box, 2.0f);

    if (button.getToggleState())
    {
        g.setColour (theme.text.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (box.reduced (3.0f), 1.0f);
    }

    g.setColour (theme.text.withMultipliedAlpha (alpha));
    g.setFont (theme.font (11.0f));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().withTrimmedLeft (24), juce::Justification::centredLeft, 1);
}

void ThemedLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<int> (width, height).toFloat();
    const auto alpha = box.isEnabled() ? 1.0f : 0.5f;

    const auto colour = box.findColour (juce::ComboBox::backgroundColourId);
    g.setColour ((box.isMouseOver (true) ? toward (colour, theme.text, 0.08f) : colour).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (bounds, cornerSize);

    const auto arrow = juce::Rectangle<float> (8.0f, 5.0f).withCentre ({ bounds.getRight() - 12.0f, bounds.getCentreY() });
    juce::Path path;
    path.addTriangle (arrow.getTopLeft(), arrow.getTopRight(), arrow.getCentre().withY (arrow.getBottom()));
    g.setColour (theme.textDim.withMultipliedAlpha (alpha));
    g.fillPath (path);
}

void ThemedLookAndFeel::drawLinearSlider (juce::Graphics& g,
                                          int x,
                                          int y,
                                          int width,
                                          int height,
                                          float sliderPos,
                                          float minSliderPos,
                                          float maxSliderPos,
                                          juce::Slider::SliderStyle style,
                                          juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto alpha = slider.isEnabled() ? 1.0f : 0.4f;
    const auto centreY = (float) y + (float) height * 0.5f;
    const auto track = juce::Rectangle<float> ((float) x, centreY - 1.5f, (float) width, 3.0f);

    g.setColour (theme.grid.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (track, 1.5f);
    g.setColour (theme.textDim.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (track.withRight (sliderPos), 1.5f);

    g.setColour (theme.text.withMultipliedAlpha (alpha));
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ sliderPos, centreY }));
}

void ThemedLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<int> (width, height).toFloat(), cornerSize);
}

void ThemedLookAndFeel::drawTextEditorOutline (juce::Graphics&, int, int, juce::TextEditor&)
{
}

const Theme& themeOf (const juce::Component& component)
{
    if (const auto* lookAndFeel = dynamic_cast<const ThemedLookAndFeel*> (&component.getLookAndFeel()))
        return lookAndFeel->getTheme();

    return Theme::fallback();
}

void paintPanel (juce::Graphics& g, const juce::Component& component, const Theme& theme)
{
    g.fillAll (theme.background);
    g.setColour (theme.panel);
    g.fillRoundedRectangle (component.getLocalBounds().toFloat(), cornerSize);
}

} // namespace mixcompare
