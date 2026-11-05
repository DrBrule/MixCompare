#include "PluginProcessor.h"
#include "TestSignals.h"
#include "ui/PluginEditor.h"

#include <cstdlib>

namespace mixcompare::test
{

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;

// Written only when asked for, so the interface can be looked at without opening a host.
void saveSnapshotIfRequested (juce::Component& component, const char* suffix)
{
    if (const auto* path = std::getenv ("MC_EDITOR_SNAPSHOT"))
    {
        const auto file = juce::File (path).getSiblingFile (juce::File (path).getFileNameWithoutExtension() + suffix + ".png");
        file.deleteFile();

        if (auto stream = file.createOutputStream())
            juce::PNGImageFormat().writeImageToStream (component.createComponentSnapshot (component.getLocalBounds()), *stream);
    }
}

// Something with a bit of everything in it: a low tone, some wide noise, and a burst pattern.
void runMusicLikeAudio (PluginProcessor& processor, double seconds, float lowToneGain = 1.0f)
{
    const auto length = (int) (seconds * sampleRate);
    const auto low = makeSine (80.0, 0.3f * lowToneGain, sampleRate, length);
    const auto left = makeNoise (0.05f, length, 11);
    const auto right = makeNoise (0.05f, length, 12);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    for (int start = 0; start + blockSize <= length; start += blockSize)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const auto index = (size_t) (start + i);
            buffer.setSample (0, i, low[index] + left[index]);
            buffer.setSample (1, i, low[index] + right[index]);
        }

        processor.processBlock (buffer, midi);

        // This runs far faster than real time, and would otherwise overrun the queue.
        if (start % (blockSize * 64) == 0)
            processor.drainAnalysis();
    }

    processor.drainAnalysis();
}

void setChoice (PluginProcessor& processor, const char* id, int index)
{
    auto* parameter = processor.getParameters().getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) index));
}

juce::Component* findById (juce::Component& root, const juce::String& id)
{
    if (root.getComponentID() == id)
        return &root;

    for (auto* child : root.getChildren())
        if (auto* found = findById (*child, id))
            return found;

    return nullptr;
}
} // namespace

class EditorTests final : public juce::UnitTest
{
public:
    EditorTests() : UnitTest ("Editor", "MixCompare") {}

    void runTest() override
    {
        beginTest ("Every control is bound to its parameter");

        PluginProcessor processor;
        processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        {
            PluginEditor editor (processor);

            for (const auto& [id, param, choice] : { std::tuple { "monitor", params::monitor, 1 },
                                                     std::tuple { "source", params::comparisonSource, 1 },
                                                     std::tuple { "noise", params::noiseColour, 2 },
                                                     std::tuple { "stereo", params::stereoMode, 1 } })
            {
                auto* box = dynamic_cast<juce::ComboBox*> (findById (editor, id));
                expect (box != nullptr);

                box->setSelectedItemIndex (choice, juce::sendNotificationSync);
                expectEquals ((int) processor.getParameters().getRawParameterValue (param)->load(), choice);
            }
        }

        beginTest ("The editor can be drawn before any audio has run");
        {
            PluginProcessor idle;
            PluginEditor editor (idle);
            saveSnapshotIfRequested (editor, "_idle");
        }

        beginTest ("The editor draws both sources and every unit with audio running");

        setChoice (processor, params::comparisonSource, (int) ComparisonSource::noise);
        setChoice (processor, params::noiseColour, (int) NoiseColour::pink);
        setChoice (processor, params::stereoMode, (int) StereoMode::goniometer);
        setChoice (processor, params::monitor, (int) MonitorSource::mix);
        runMusicLikeAudio (processor, 5.0);

        PluginEditor editor (processor);
        editor.refresh();
        saveSnapshotIfRequested (editor, "_mix_goniometer");

        setChoice (processor, params::monitor, (int) MonitorSource::comparison);
        setChoice (processor, params::stereoMode, (int) StereoMode::lissajous);
        runMusicLikeAudio (processor, 1.0);
        editor.refresh();
        saveSnapshotIfRequested (editor, "_comparison_lissajous");

        setChoice (processor, params::stereoMode, (int) StereoMode::stereometer);
        runMusicLikeAudio (processor, 1.0);
        editor.refresh();
        saveSnapshotIfRequested (editor, "_comparison_stereometer");

        beginTest ("The stereometer redraws when the stereo image changes");
        {
            setChoice (processor, params::monitor, (int) MonitorSource::mix);
            auto* view = findById (editor, "stereoView");
            expect (view != nullptr);

            const auto drawWith = [&] (float left, float right)
            {
                const auto sine = makeSine (440.0, 0.3f, sampleRate, blockSize * 64);
                juce::AudioBuffer<float> buffer (2, blockSize);
                juce::MidiBuffer midi;

                for (int start = 0; start + blockSize <= (int) sine.size(); start += blockSize)
                {
                    for (int i = 0; i < blockSize; ++i)
                    {
                        buffer.setSample (0, i, left * sine[(size_t) (start + i)]);
                        buffer.setSample (1, i, right * sine[(size_t) (start + i)]);
                    }

                    processor.processBlock (buffer, midi);
                    processor.drainAnalysis();
                }

                editor.refresh();
                return view->createComponentSnapshot (view->getLocalBounds());
            };

            const auto countDifferences = [] (const juce::Image& a, const juce::Image& b)
            {
                auto different = 0;

                for (int y = 0; y < a.getHeight(); ++y)
                    for (int x = 0; x < a.getWidth(); ++x)
                        different += a.getPixelAt (x, y) != b.getPixelAt (x, y) ? 1 : 0;

                return different;
            };

            const auto mono = drawWith (1.0f, 1.0f);
            const auto left = drawWith (1.0f, 0.0f);
            saveSnapshotIfRequested (editor, "_stereometer_left");
            expectGreaterThan (countDifferences (mono, left), 500);
        }

        beginTest ("The resolution slider sets the size of the displayed FFT only");
        {
            auto* slider = dynamic_cast<juce::Slider*> (findById (editor, "resolution"));
            expect (slider != nullptr);
            expectEquals (slider->getTextFromValue (slider->getValue()), juce::String ("8192 pts, 5.86 Hz"));

            const auto& analyser = processor.getAnalyser (MonitorSource::mix);
            const auto averageBinHz = analyser.getSpectrum().getBinHz();

            for (const auto order : { SpectrumAnalyser::maxOrder, SpectrumAnalyser::minOrder })
            {
                slider->setValue (order, juce::sendNotificationSync);
                processor.drainAnalysis();
                editor.refresh();

                expectWithinAbsoluteError (analyser.getDisplaySpectrum().getBinHz(), sampleRate / (1 << order), 1.0e-9);
                expectEquals (analyser.getSpectrum().getBinHz(), averageBinHz);
                editor.createComponentSnapshot (editor.getLocalBounds());
            }

            slider->setValue (SpectrumAnalyser::maxOrder, juce::sendNotificationSync);
            runMusicLikeAudio (processor, 2.0);
            editor.refresh();
            saveSnapshotIfRequested (editor, "_narrow_fft");

            slider->setValue (SpectrumAnalyser::defaultOrder, juce::sendNotificationSync);
        }

        beginTest ("Advice holds still until it's asked to re-evaluate");
        {
            auto* advice = dynamic_cast<AdviceView*> (findById (editor, "advice"));
            auto* button = dynamic_cast<juce::Button*> (findById (editor, "evaluate"));
            expect (advice != nullptr && button != nullptr);

            processor.resetAnalysis();
            runMusicLikeAudio (processor, 12.0);
            expectGreaterThan (processor.getAdviceSeconds(), 10.0);
            advice->update();
            const auto before = advice->getText();
            expect (before.isNotEmpty());

            // Without the low tone, so the averages change and the advice has something new to say.
            runMusicLikeAudio (processor, 12.0, 0.0f);
            advice->update();
            expectEquals (advice->getText(), before, "Nothing asked for it to change");

            button->triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            expect (advice->getText() != before, "Re-evaluate shows what's true now");

            const auto evaluated = advice->getText();
            processor.resetAnalysis();
            runMusicLikeAudio (processor, 1.0);
            advice->update();
            expect (advice->getText() != evaluated, "Starting the averages again drops the old advice");
        }

        beginTest ("Dragging the bar resizes the advice, within what the window has room for");
        {
            auto* advice = findById (editor, "advice");
            auto* bar = dynamic_cast<ResizeBar*> (findById (editor, "adviceBar"));
            auto* spectrum = findById (editor, "spectrum");
            expect (advice != nullptr && bar != nullptr && spectrum != nullptr);

            const auto startHeight = advice->getHeight();
            const auto spectrumHeight = spectrum->getHeight();

            bar->onDragStart();
            bar->onDrag (-60);
            expectEquals (advice->getHeight(), startHeight + 60);
            expectEquals (spectrum->getHeight(), spectrumHeight - 60);
            expectEquals (bar->getBottom(), advice->getY());
            saveSnapshotIfRequested (editor, "_tall_advice");

            bar->onDrag (10000);
            expectEquals (advice->getHeight(), 80, "Can't go below the smallest useful size");

            bar->onDrag (-10000);
            expectGreaterThan (spectrum->getHeight(), 60, "Can't squeeze out everything above it");
            const auto tallest = advice->getHeight();

            beginTest ("The height is kept while the window is too small for it, and in the session");
            editor.setSize (940, 560);
            expectEquals (editor.getWidth(), 940);
            expectLessThan (advice->getHeight(), tallest);
            expectGreaterThan (spectrum->getHeight(), 0);
            editor.createComponentSnapshot (editor.getLocalBounds());
            saveSnapshotIfRequested (editor, "_smallest");
            editor.setSize (1100, 720);
            expectEquals (advice->getHeight(), tallest);

            juce::MemoryBlock state;
            processor.getStateInformation (state);
            PluginProcessor restored;
            restored.setStateInformation (state.getData(), (int) state.getSize());
            expectEquals (restored.getAdviceHeight(), processor.getAdviceHeight());
        }

        beginTest ("Every theme in the file can be chosen, and is drawn and kept with the session");
        {
            processor.setAdviceHeight (150);
            editor.resized();

            auto* box = dynamic_cast<juce::ComboBox*> (findById (editor, "theme"));
            expect (box != nullptr);
            expectEquals (box->getNumItems(), 6);
            expectEquals (box->getText(), juce::String ("Default"), "A new instance starts on the default theme");

            // Starting from the second, as choosing what's already chosen doesn't count as a change.
            for (int step = 1; step <= box->getNumItems(); ++step)
            {
                const auto i = step % box->getNumItems();
                box->setSelectedItemIndex (i, juce::sendNotificationSync);
                const auto name = box->getItemText (i);
                expectEquals (processor.getThemeName(), name);
                expectEquals (editor.getTheme().name, name);

                editor.refresh();
                const auto snapshot = editor.createComponentSnapshot (editor.getLocalBounds());
                expect (snapshot.getPixelAt (2, 2) == editor.getTheme().background, "The background follows the theme");
                saveSnapshotIfRequested (editor, ("_theme_" + name.toLowerCase()).toRawUTF8());
            }

            box->setSelectedItemIndex (1, juce::sendNotificationSync);
            juce::MemoryBlock state;
            processor.getStateInformation (state);
            PluginProcessor restored;
            restored.setStateInformation (state.getData(), (int) state.getSize());
            expectEquals (restored.getThemeName(), juce::String ("Forest"));

            PluginEditor reopened (restored);
            expectEquals (reopened.getTheme().name, juce::String ("Forest"));

            // As when a host loads a preset with the editor open.
            restored.setThemeName ("Ocean");
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            expectEquals (reopened.getTheme().name, juce::String ("Ocean"));

            restored.setThemeName ("No such theme");
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            expectEquals (reopened.getTheme().name, juce::String ("Default"));
        }

        beginTest ("Every theme has its font and readable text");
        {
            juce::SharedResourcePointer<ThemeLibrary> library;

            for (const auto& theme : library->getThemes())
            {
                expect (theme.typeface != nullptr, theme.name + " uses a font that isn't built in");

                // WCAG's contrast ratio, with 3:1 the least for text of this size.
                const auto contrast = [] (juce::Colour a, juce::Colour b)
                {
                    const auto luminance = [] (juce::Colour c)
                    {
                        const auto channel = [] (float v) { return v <= 0.03928f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f); };
                        return 0.2126f * channel (c.getFloatRed()) + 0.7152f * channel (c.getFloatGreen()) + 0.0722f * channel (c.getFloatBlue());
                    };
                    const auto la = luminance (a), lb = luminance (b);
                    return (juce::jmax (la, lb) + 0.05f) / (juce::jmin (la, lb) + 0.05f);
                };

                for (const auto& [what, colour] : { std::pair { "text", theme.text },
                                                    std::pair { "mix text", theme.mix.text },
                                                    std::pair { "comparison text", theme.comparison.text } })
                    expectGreaterThan (contrast (colour, theme.panel), 3.0f, theme.name + " " + what);
            }
        }

        beginTest ("A theme only needs its three core colours");
        {
            std::vector<Theme> parsed;
            const auto defaultName = ThemeLibrary::parse (R"({ "themes": [
                { "name": "Short", "font": "Nothing", "colours": { "background": [250, 250, 250], "mix": [0, 200, 0], "comparison": [0, 0, 200] } },
                { "name": "Overridden", "colours": { "background": [0, 0, 0], "mix": [255, 0, 0], "comparison": [0, 0, 255], "mixAverage": [1, 2, 3] } },
                { "name": "Missing a core colour", "colours": { "background": [0, 0, 0], "mix": [255, 0, 0] } },
                { "colours": { "background": [0, 0, 0], "mix": [255, 0, 0], "comparison": [0, 0, 255] } } ] })",
                                                          [] (const juce::String&) { return juce::Typeface::Ptr(); },
                                                          parsed);

            expectEquals ((int) parsed.size(), 2, "Themes without a name or a core colour are left out");
            expectEquals (defaultName, juce::String ("Short"), "The first is the default when none is named");

            const auto& light = parsed[0];
            expect (light.text.getPerceivedBrightness() < 0.3f, "Text is dark on a light background");
            expect (light.mix.average.getPerceivedBrightness() < light.mix.core.getPerceivedBrightness(), "Emphasis moves away from the background");
            expect (light.mix.dim.getPerceivedBrightness() > light.mix.core.getPerceivedBrightness(), "Dim moves towards it");
            expect (light.font (12.0f).getHeight() > 0.0f, "A font that isn't built in still gives a usable font");

            expect (parsed[1].mix.average == juce::Colour (1, 2, 3), "A colour given in the file is used as it is");
            expect (parsed[1].text.getPerceivedBrightness() > 0.7f);

            std::vector<Theme> none;
            expect (ThemeLibrary::parse ("not json", {}, none).isEmpty() && none.empty());
        }

        processor.releaseResources();
    }
};

static EditorTests editorTests;

} // namespace mixcompare::test
