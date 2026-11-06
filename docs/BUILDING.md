# Building from source

## Requirements

| Platform | Toolchain |
|---|---|
| macOS 11+ | Xcode or the Command Line Tools, CMake ≥ 3.22, Ninja (recommended) |
| Windows 10+ | Visual Studio 2022 (Desktop C++), CMake ≥ 3.22 |
| Linux | GCC ≥ 10 or Clang ≥ 12, CMake, Ninja, and JUCE's dependencies (below) |

Linux packages (Debian/Ubuntu):

```bash
sudo apt-get install -y ninja-build libasound2-dev libjack-jackd2-dev libfreetype6-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libgl1-mesa-dev
```

JUCE 9.0.5 and clap-juce-extensions (pinned commit) are fetched by CMake on first configure.

## Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build --target ResonanceVerb_All ResonanceVerb_CLAP
```

Plugins land in `build/ResonanceVerb_artefacts/Release/<format>/`. On macOS the build is universal
(arm64 + x86_64). On Windows, omit `-G Ninja` and add `--config Release` to the build command.

| Target | What it builds |
|---|---|
| `ResonanceVerb_All` | VST3, Standalone, AU (macOS), LV2 (Linux) |
| `ResonanceVerb_CLAP` | CLAP |
| `rv_render` | DSP verification, benchmark, demo audio (no JUCE needed) |
| `rv_plugin_tests` | Undo/redo, presets, MIDI learn and session-state tests (headless) |
| `rv_ui_snapshot` | Renders the editor to a PNG (used for the docs) |

Set your project URL (used for the LV2 URI and the plugin's website field):

```bash
cmake -B build -DRV_HOMEPAGE=https://github.com/<you>/resonance-verb
```

## Test

```bash
./build/rv_render
```

```bash
./build/rv_plugin_tests_artefacts/Release/rv_plugin_tests
```

`rv_render` also writes `demo_dry.wav`, `demo_noblur.wav` and `demo.wav` (drums through four chords). Useful options:

- `--dump file.raw` and `--compare a.raw b.raw`: compare the SIMD build against a `-DRV_FORCE_SCALAR` build.
- `--export-plots docs/tools/data`: the data behind the documentation figures.

The DSP harness also builds without CMake:

```bash
clang++ -std=c++17 -O3 -ISource tests/render_test.cpp -o rv_render && ./rv_render
```

## Validate

- **pluginval** (Tracktion): `pluginval --strictness-level 10 --validate "<path>/Resonance Verb.vst3"`
- **auval** (macOS, after copying the AU to `~/Library/Audio/Plug-Ins/Components`): `auval -v aumf Rsv1 Mtya`
- **clap-validator** (free-audio): `clap-validator validate "<path>/Resonance Verb.clap"`

## Regenerate the documentation figures

```bash
./build/rv_render --export-plots docs/tools/data
```

```bash
python3 docs/tools/make_plots.py
```

```bash
./build/rv_ui_snapshot_artefacts/Release/rv_ui_snapshot docs/images/editor.png 2
```

The snapshot tool takes `out.png [factoryPresetIndex] [browser|learn]`.

## Continuous integration and releases

[`.github/workflows/build.yml`](../.github/workflows/build.yml) runs on every push and pull request:

1. builds macOS (universal), Windows (x64) and Linux (x64);
2. runs `rv_render` and `rv_plugin_tests`;
3. validates the VST3 with pluginval on all three, and the CLAP with clap-validator on Linux;
4. uploads a zip per platform (plus demo audio from macOS) as build artifacts.

Pushing a tag such as `v0.9.0` also creates a GitHub Release with those zips attached:

```bash
git tag v0.9.0 && git push origin v0.9.0
```

Builds are unsigned. For distribution on macOS you would add a Developer ID signature and notarisation to the
workflow.

## Project layout

```
Source/dsp/        framework-free DSP core (header-only)
Source/            JUCE processor, editor, preset manager and browser, factory presets
tests/             DSP harness, plugin tests, UI snapshot tool
docs/              user guide, DSP notes, figures, plot tooling
.github/workflows  CI
```
