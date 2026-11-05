# Mix Compare

An AU / VST3 / Standalone plugin, built with JUCE, for A/B comparing a mix against a reference
song or a noise profile while reading the same metrics for both.

```
Main input ───────────────► Mix analyser ───────────┐
                                                    ├─► Monitor select ─► Output
File / Noise / Sidechain ─► Trim ─► Comparison analyser ┘
```

Both analysers run all the time, whichever source is being heard. That's what lets the spectrum
overlay them and the advice compare them. The metrics describe whichever source is monitored.

## Themes

Every colour and the font come from [resources/themes.json](resources/themes.json), which is built
into the plugin. Six themes ship: Default, Forest, Sunny, Nighttime, Ocean and Ember. The theme
drop-down sits on the second row, and the choice is saved with the session.

Each theme has a background and a core colour for each source. The rest are variations of those:

| Colour | Used for |
| --- | --- |
| `panel`, `grid` | The panels the units sit on, and grid lines, tracks and empty meters |
| `text`, `textDim` | Readings, and the captions beside them |
| `mix`, `comparison` | Instantaneous readings: the spectrum, the instant LUFS bar, the stereo display |
| `mixAverage`, `comparisonAverage` | Long-term readings drawn over those, such as the integrated LUFS line |
| `mixDim`, `comparisonDim` | The slower short-term LUFS bar |
| `mixText`, `comparisonText` | Each source's name and numbers, with more contrast than a line needs |

A new theme only needs `name`, `background`, `mix` and `comparison`. Any other colour left out is
made from those, by moving towards or away from the background. `font` is a family name: fonts in
`resources/fonts` are built in and found by that name, and any other name is looked up on the
system. Adding a font file means adding it to `juce_add_binary_data` in `CMakeLists.txt` too.

The font is [Michroma](https://github.com/googlefonts/Michroma-font), chosen as a free alternative
to Cardonas: wide, squared and technical. It's under the SIL Open Font License, which allows
commercial use and bundling, and its licence is built in alongside it.

| Unit | What it does | Code |
| --- | --- | --- |
| Source select | Click-free A/B between the mix and the comparison | `PluginProcessor` |
| Song comparison | Loads WAV, AIFF, FLAC, Ogg, MP3 and AAC into memory, loops, follows the host transport | `sources/FilePlayer` |
| Live comparison | Anything routed to the plugin's sidechain input | `PluginProcessor` |
| Noise comparison | Decorrelated stereo white, pink and brown noise, all at -18 dBFS RMS | `sources/NoiseGenerator` |
| FFT | Display from 2048 to 65536 points, 87.5% overlap, set with a slider | `analysis/SpectrumAnalyser` |
| LUFS | BS.1770 / R128 instant (400 ms), short-term (3 s) and gated average, for both sources side by side | `analysis/LoudnessMeter` |
| RMS and crest factor | Per channel: RMS over 300 ms, crest over 3 s | `analysis/LevelMeter` |
| Stereo field | Goniometer, Lissajous or stereometer (correlation, balance and width bars) | `analysis/StereoField` |
| Mix advice | First-pass rules on tone, loudness, dynamics and width, in a resizable section | `advice/MixAdvisor` |

The FFT slider only changes the display. Larger sizes separate closer frequencies, but take longer
to respond (65536 points is about 1.4 s of audio at 48 kHz), and noise-like content reads about 3 dB
lower with each step up, while a steady tone stays at the same level. The advice always uses its own
8192 point average, so moving the slider can't change what it says.

The advice follows the audio for its first 10 s, then holds still until **Re-evaluate** is pressed.
That reads the current averages and doesn't clear them, which is what **Reset averages** is for.
Clearing them, or changing the comparison, sets the advice following again.

## Build

Requires CMake 3.24+ and Xcode. JUCE 9.0.5 is downloaded on first configure, unless pointed at an
existing checkout with `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE`.

```bash
cmake --preset debug
```

```bash
cmake --build --preset debug
```

Building installs the AU and VST3 to the user plugin folders. Turn that off with
`-DMC_COPY_AFTER_BUILD=OFF`.

## Test

```bash
ctest --preset debug
```

To look at the interface without opening a host, this writes PNGs next to the given path:

```bash
MC_EDITOR_SNAPSHOT=/tmp/editor.png ./build/debug/MixCompareTests_artefacts/Debug/MixCompareTests
```

## Comparing with a streaming service

Streaming services don't give plugins access to their audio, so the plugin doesn't connect to them
directly. Instead, route the app's output to the sidechain input with a loopback driver such as
BlackHole, then choose **Sidechain** as the comparison source.

## Threads

- **Audio thread:** runs LUFS, RMS and crest directly, and never locks, allocates or reads the disk.
- **Analysis thread:** runs the FFT and stereo analysis on audio received through a lock-free queue.
- **Loader thread:** decodes songs. The audio thread only ever tries the lock around the current
  song, and outputs silence for that block on the rare occasion that a swap is in progress.

## Known limits

- The song isn't locked to the host's timeline. It starts and stops with the transport but doesn't
  follow its position.
- Songs at another sample rate are resampled without an anti-aliasing filter, so a song with content
  above the session's Nyquist frequency can alias. Matching rates play back bit for bit.
- Songs are held in memory, up to 1.5 GB decoded.
- The sidechain is what the host provides, so it stops with the transport in some hosts.
- The displays only move while the host is sending audio. A host that stops processing the plugin
  leaves them on their last readings.
- Pink noise is tuned for 44.1 and 48 kHz. Its slope drifts slightly below about 40 Hz at higher rates.

## Licence

JUCE is dual-licensed under the AGPLv3 and a commercial licence. Distributing this plugin requires
meeting one or the other: see the [JUCE licence terms](https://juce.com/legal/juce-9-licence/).
