#include "ui/UnitViews.h"

namespace mixcompare
{

namespace
{
constexpr float spectrumMinDb = -100.0f, spectrumMaxDb = 0.0f;
constexpr double spectrumMinHz = 20.0, spectrumMaxHz = 20000.0;
constexpr float meterMinLufs = -60.0f, meterMaxLufs = 0.0f;
constexpr double adviceSettleSeconds = 10.0;

// Sizes for the theme's font, which is wide, so these are smaller than usual.
constexpr float captionSize = 9.0f, valueSize = 11.0f, nameSize = 11.0f;

juce::String reading (float value, const char* unit)
{
    return value <= silenceDb ? juce::String ("-inf") + unit : juce::String (value, 1) + unit;
}

const char* nameOf (MonitorSource source)
{
    return source == MonitorSource::mix ? "MIX" : "COMPARISON";
}

juce::String describeBalance (float balance)
{
    if (std::abs (balance) > 0.999f)
        return balance < 0.0f ? "Left only" : "Right only";

    // Balance is (R - L) / (R + L) of the energies, which gives back their ratio in dB.
    const auto db = 10.0f * std::log10 ((1.0f + balance) / (1.0f - balance));

    if (std::abs (db) < 0.05f)
        return "Centred";

    return juce::String (db < 0.0f ? "L +" : "R +") + juce::String (std::abs (db), 1) + " dB";
}

// A caption in the dim text colour with its value after it.
void drawReading (juce::Graphics& g, const Theme& theme, juce::Rectangle<int> area, const juce::String& caption, const juce::String& value, juce::Colour valueColour)
{
    g.setFont (theme.font (captionSize));
    g.setColour (theme.textDim);
    g.drawText (caption, area, juce::Justification::centredLeft);

    g.setFont (theme.font (valueSize));
    g.setColour (valueColour);
    g.drawText (value, area, juce::Justification::centredRight);
}

// A captioned horizontal bar, filled from its origin to the value.
void drawStereoBar (juce::Graphics& g,
                    const Theme& theme,
                    juce::Rectangle<int> area,
                    const juce::String& name,
                    const juce::String& value,
                    const Theme::SourceColours& colours,
                    float position,
                    float origin)
{
    drawReading (g, theme, area.removeFromTop (18), name, value, colours.text);
    area.removeFromTop (2);

    const auto bar = area.removeFromTop (juce::jmin (area.getHeight(), 10)).toFloat();
    const auto xOf = [&] (float proportion) { return bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, proportion); };

    g.setColour (theme.grid);
    g.fillRoundedRectangle (bar, 2.0f);
    g.setColour (colours.core);
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (xOf (origin), xOf (position)),
                                                            bar.getY(),
                                                            juce::jmax (xOf (origin), xOf (position)),
                                                            bar.getBottom()));
    g.setColour (theme.text);
    g.fillRect (xOf (origin) - 0.5f, bar.getY() - 2.0f, 1.0f, bar.getHeight() + 4.0f);
}
} // namespace

void SpectrumView::paint (juce::Graphics& g)
{
    const auto& theme = themeOf (*this);
    paintPanel (g, *this, theme);

    const auto bounds = getLocalBounds().toFloat();
    const auto ratio = spectrumMaxHz / spectrumMinHz;

    const auto xOf = [&] (double hz) { return bounds.getWidth() * (float) (std::log (hz / spectrumMinHz) / std::log (ratio)); };
    const auto yOf = [&] (float db) { return juce::jmap (db, spectrumMinDb, spectrumMaxDb, bounds.getBottom(), bounds.getY()); };

    g.setFont (theme.font (captionSize));

    for (const auto hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        g.setColour (theme.grid);
        g.drawVerticalLine (juce::roundToInt (xOf (hz)), bounds.getY(), bounds.getBottom());
        g.setColour (theme.textDim);
        g.drawText (hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + "K" : juce::String (hz, 0),
                    juce::roundToInt (xOf (hz)) + 4, getHeight() - 16, 40, 14, juce::Justification::left);
    }

    for (auto db = spectrumMinDb + 20.0f; db < spectrumMaxDb; db += 20.0f)
    {
        g.setColour (theme.grid);
        g.drawHorizontalLine (juce::roundToInt (yOf (db)), bounds.getX(), bounds.getRight());
        g.setColour (theme.textDim);
        g.drawText (juce::String ((int) db), 6, juce::roundToInt (yOf (db)) + 2, 40, 14, juce::Justification::left);
    }

    const auto monitored = processor.getMonitorSource();

    // The monitored source goes last, filled and thicker, so it's the one on top.
    for (const auto source : { monitored == MonitorSource::mix ? MonitorSource::comparison : MonitorSource::mix, monitored })
    {
        const auto binHz = processor.getAnalyser (source).getDisplaySpectrum().getDisplayDb (spectrum);
        const auto lastBin = (int) spectrum.size() - 1;

        juce::Path path;
        const auto width = getWidth();

        // Each column shows the loudest bin it covers, or at the low end, where there are fewer
        // bins than columns, the nearest one.
        for (int x = 0; x < width; ++x)
        {
            const auto lowHz = spectrumMinHz * std::pow (ratio, (double) x / width);
            const auto highHz = spectrumMinHz * std::pow (ratio, (double) (x + 1) / width);
            const auto first = juce::jlimit (0, lastBin, juce::roundToInt (lowHz / binHz));
            const auto last = juce::jlimit (first, lastBin, juce::roundToInt (highHz / binHz));
            auto db = silenceDb;

            for (auto bin = first; bin <= last; ++bin)
                db = juce::jmax (db, spectrum[(size_t) bin]);

            const auto y = yOf (juce::jmax (db, spectrumMinDb));

            if (x == 0)
                path.startNewSubPath ((float) x, y);
            else
                path.lineTo ((float) x, y);
        }

        const auto& colours = theme.of (source);

        if (source == monitored)
        {
            auto fill = path;
            fill.lineTo (bounds.getRight(), bounds.getBottom());
            fill.lineTo (bounds.getX(), bounds.getBottom());
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient::vertical (colours.core.withAlpha (0.3f), bounds.getY(), colours.core.withAlpha (0.03f), bounds.getBottom()));
            g.fillPath (fill);
        }

        g.setColour (source == monitored ? colours.core : colours.average.withAlpha (0.8f));
        g.strokePath (path, juce::PathStrokeType (source == monitored ? 1.6f : 1.0f));
    }

    g.setFont (theme.font (captionSize));
    g.setColour (theme.mix.text);
    g.drawText ("MIX", getWidth() - 130, 8, 120, 14, juce::Justification::right);
    g.setColour (theme.comparison.text);
    g.drawText ("COMPARISON", getWidth() - 130, 24, 120, 14, juce::Justification::right);
}

void LoudnessView::paint (juce::Graphics& g)
{
    const auto& theme = themeOf (*this);
    paintPanel (g, *this, theme);

    auto bounds = getLocalBounds().reduced (10);
    g.setFont (theme.font (captionSize));
    g.setColour (theme.textDim);
    g.drawText ("LOUDNESS  LUFS", bounds.removeFromTop (14), juce::Justification::left);
    bounds.removeFromTop (6);

    const auto halfWidth = (bounds.getWidth() - 12) / 2;
    const auto mixArea = bounds.removeFromLeft (halfWidth);
    const auto comparisonArea = bounds.removeFromRight (halfWidth);

    for (const auto& [source, sourceArea] : { std::pair { MonitorSource::mix, mixArea }, std::pair { MonitorSource::comparison, comparisonArea } })
    {
        const auto& colours = theme.of (source);
        const auto& loudness = processor.getAnalyser (source).getLoudness();
        const auto momentary = loudness.getMomentaryLufs();
        const auto shortTerm = loudness.getShortTermLufs();
        const auto integrated = loudness.getIntegratedLufs();

        auto area = sourceArea;
        g.setColour (colours.text);
        g.setFont (theme.font (nameSize));
        g.drawFittedText (nameOf (source), area.removeFromTop (16), juce::Justification::centred, 1);

        auto text = area.removeFromBottom (54);
        text.removeFromTop (6);
        drawReading (g, theme, text.removeFromTop (16), "INST", reading (momentary, ""), colours.text);
        drawReading (g, theme, text.removeFromTop (16), "SHORT", reading (shortTerm, ""), colours.text);
        drawReading (g, theme, text.removeFromTop (16), "AVG", reading (integrated, ""), colours.average);

        auto meter = area.reduced (0, 6).toFloat();
        const auto yOf = [&] (float lufs)
        {
            return juce::jmap (juce::jlimit (meterMinLufs, meterMaxLufs, lufs), meterMinLufs, meterMaxLufs, meter.getBottom(), meter.getY());
        };

        // Instant on the left in the core colour, short-term on the right dimmed, as it's the
        // slower of the two, and the average across both in its own colour.
        const auto drawBar = [&] (juce::Rectangle<float> bar, float lufs, juce::Colour colour)
        {
            g.setColour (theme.grid);
            g.fillRoundedRectangle (bar, 2.0f);
            g.setColour (colour);
            g.fillRoundedRectangle (bar.withTop (yOf (lufs)), 2.0f);
        };

        const auto left = meter.removeFromLeft (meter.getWidth() * 0.5f);
        drawBar (left.reduced (3.0f, 0.0f), momentary, colours.core);
        drawBar (meter.reduced (3.0f, 0.0f), shortTerm, colours.dim);

        if (integrated > silenceDb)
        {
            g.setColour (colours.average);
            g.fillRect (left.getX(), yOf (integrated) - 1.0f, left.getWidth() + meter.getWidth(), 2.0f);
        }
    }
}

void LevelReadout::paint (juce::Graphics& g)
{
    const auto& theme = themeOf (*this);
    paintPanel (g, *this, theme);

    const auto source = processor.getMonitorSource();
    const auto& colours = theme.of (source);
    const auto& levels = processor.getAnalyser (source).getLevels();

    auto area = getLocalBounds().reduced (10, 0);

    g.setFont (theme.font (nameSize));
    g.setColour (colours.text);
    g.drawText (nameOf (source), area.removeFromLeft (150), juce::Justification::centredLeft);

    const auto width = area.getWidth() / 4;
    drawReading (g, theme, area.removeFromLeft (width).withTrimmedRight (24), "RMS L", reading (levels.getRmsDb (0), " dB"), theme.text);
    drawReading (g, theme, area.removeFromLeft (width).withTrimmedRight (24), "RMS R", reading (levels.getRmsDb (1), " dB"), theme.text);
    drawReading (g, theme, area.removeFromLeft (width).withTrimmedRight (24), "CREST L", juce::String (levels.getCrestDb (0), 1) + " dB", theme.text);
    drawReading (g, theme, area.withTrimmedRight (4), "CREST R", juce::String (levels.getCrestDb (1), 1) + " dB", theme.text);
}

void StereoView::paint (juce::Graphics& g)
{
    const auto& theme = themeOf (*this);
    paintPanel (g, *this, theme);

    const auto source = processor.getMonitorSource();
    const auto mode = processor.getStereoMode();
    auto area = getLocalBounds().reduced (10);

    if (mode == StereoMode::stereometer)
    {
        paintStereometer (g, theme, area, source);
        return;
    }

    drawReading (g, theme, area.removeFromBottom (16), "CORRELATION", juce::String (processor.getAnalyser (source).getStereo().getCorrelation(), 2), theme.of (source).text);
    area.removeFromBottom (4);

    paintPlot (g, theme, area.toFloat(), source, mode);
}

void StereoView::paintStereometer (juce::Graphics& g, const Theme& theme, juce::Rectangle<int> area, MonitorSource source)
{
    const auto& stereo = processor.getAnalyser (source).getStereo();
    const auto& colours = theme.of (source);
    const auto correlation = stereo.getCorrelation();
    const auto balance = stereo.getBalance();
    const auto width = stereo.getSideRatio();

    g.setFont (theme.font (nameSize));
    g.setColour (colours.text);
    g.drawText (nameOf (source), area.removeFromTop (16), juce::Justification::centredLeft);
    area.removeFromTop (6);

    const auto rowHeight = juce::jmin (48, area.getHeight() / 3);

    drawStereoBar (g, theme, area.removeFromTop (rowHeight), "CORRELATION", juce::String (correlation, 2), colours, 0.5f + 0.5f * correlation, 0.5f);
    drawStereoBar (g, theme, area.removeFromTop (rowHeight), "BALANCE", describeBalance (balance), colours, 0.5f + 0.5f * balance, 0.5f);
    drawStereoBar (g, theme, area, "WIDTH", juce::String (juce::roundToInt (width * 100.0f)) + "% side", colours, width, 0.0f);
}

void StereoView::paintPlot (juce::Graphics& g, const Theme& theme, juce::Rectangle<float> plot, MonitorSource source, StereoMode mode)
{
    processor.getAnalyser (source).getStereo().copyScope (scope);

    const auto centre = plot.getCentre();
    const auto radius = 0.5f * juce::jmin (plot.getWidth(), plot.getHeight());

    g.setColour (theme.grid);
    g.drawHorizontalLine (juce::roundToInt (centre.y), centre.x - radius, centre.x + radius);
    g.drawVerticalLine (juce::roundToInt (centre.x), centre.y - radius, centre.y + radius);

    // A full-scale mono signal reaches sqrt(2) on the goniometer, hence the scaling.
    const auto scale = radius / juce::MathConstants<float>::sqrt2;
    g.setColour (theme.of (source).core.withAlpha (0.6f));

    for (const auto& sample : scope)
    {
        const auto point = StereoField::project (mode, sample.x, sample.y);
        g.fillRect (centre.x + juce::jlimit (-radius, radius, point.x * scale),
                    centre.y - juce::jlimit (-radius, radius, point.y * scale),
                    1.5f,
                    1.5f);
    }
}

AdviceView::AdviceView (PluginProcessor& p) : processor (p)
{
    text.setMultiLine (true);
    text.setReadOnly (true);
    text.setCaretVisible (false);
    addAndMakeVisible (text);
    addAndMakeVisible (status);
    addAndMakeVisible (evaluateButton);

    evaluateButton.setComponentID ("evaluate");
    evaluateButton.onClick = [this] { evaluate(); };
}

void AdviceView::update()
{
    // Having less audio than last time can only mean that the averages have started again, and
    // whatever is on show describes something that's gone.
    if (! holding || processor.getAdviceSeconds() < evaluatedSeconds)
        evaluate();
    else
        showStatus();
}

void AdviceView::evaluate()
{
    evaluatedSeconds = processor.getAdviceSeconds();
    holding = evaluatedSeconds >= adviceSettleSeconds;

    juce::StringArray lines;

    for (const auto& advice : processor.getAdvice())
        lines.add (juce::String (advice.severity == Advice::Severity::major ? "! " : "- ") + advice.text);

    text.setText (lines.joinIntoString ("\n"), false);
    showStatus();
}

void AdviceView::showStatus()
{
    const auto seconds = juce::String (juce::roundToInt (evaluatedSeconds));

    status.setText (holding ? "Mix advice, from " + seconds + " s of audio. Re-evaluate to update it."
                            : "Mix advice, listening: " + seconds + " s of audio so far",
                    juce::dontSendNotification);
}

void AdviceView::resized()
{
    auto area = getLocalBounds().reduced (6);
    auto header = area.removeFromTop (24);
    evaluateButton.setBounds (header.removeFromRight (120));
    status.setBounds (header);
    area.removeFromTop (2);
    text.setBounds (area);
}

void AdviceView::paint (juce::Graphics& g)
{
    paintPanel (g, *this, themeOf (*this));
}

void AdviceView::lookAndFeelChanged()
{
    // The text editor keeps the font and colour that each piece of text was added with.
    const auto& theme = themeOf (*this);
    status.setColour (juce::Label::textColourId, theme.textDim);
    text.setFont (theme.font (12.0f));
    text.applyFontToAllText (theme.font (12.0f));
    text.applyColourToAllText (theme.text);
    text.setIndents (8, 6);
}

ResizeBar::ResizeBar()
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setRepaintsOnMouseActivity (true);
}

void ResizeBar::paint (juce::Graphics& g)
{
    const auto& theme = themeOf (*this);
    g.setColour (isMouseOverOrDragging() ? theme.textDim : theme.grid);
    g.fillRoundedRectangle (getLocalBounds().withSizeKeepingCentre (48, 2).toFloat(), 1.0f);
}

void ResizeBar::mouseDown (const juce::MouseEvent&)
{
    if (onDragStart != nullptr)
        onDragStart();
}

void ResizeBar::mouseDrag (const juce::MouseEvent& event)
{
    if (onDrag != nullptr)
        onDrag (event.getDistanceFromDragStartY());
}

} // namespace mixcompare
