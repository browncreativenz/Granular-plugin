# Granular-plugin

Copyright (C) 2026 browncreativenz

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU Affero General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option) any
later version. See [LICENSE](LICENSE).

The AGPL-3.0 matches the license of the
[Capicola](https://github.com/heavylight-industries/capicola) firmware, whose
DSP core this plugin builds on.

## Status

Milestones M0 and M1 of [the plan](docs/PLAN.md) are done:

- Capicola's audio engine runs on a desktop and matches the firmware sample for
  sample.
- It works at any host sample rate: the engine always runs at 48 kHz, as the
  hardware does, with high-quality resampling around it at other rates.
- All parameters use the firmware's knob curves and factory defaults, and each
  primary control has the module's modulation routing.

The JUCE plugin comes in M2.

## Build

Requires CMake 3.22 or later and a C++17 compiler (Xcode's clang on macOS).

```sh
git submodule update --init external/capicola   # first checkout only; no --recursive needed
cmake -B build
cmake --build build
ctest --test-dir build
```

The tests run the firmware's own DSP test suite, compare this port's engine with
the firmware's `audio_engine.cpp` bit for bit (`tests/engine_golden.cpp`), check
the knob curves against the firmware's, and measure the resampler at every
sample rate Live offers.

## Render audio from the command line

```sh
./build/capicola_render out.wav                       # built-in drums-and-pad test signal
./build/capicola_render --in loop.wav --set stretch=0.6 --set pitch=0.25 out.wav
./build/capicola_render --list                        # every parameter and its default
```

Parameters are knob positions from 0 to 1, as on the module and in the plugin:
`pitch=0.25` is −1x (reverse), `stretch=0.5` freezes. Input can be any sample
rate.

## Layout

- `external/capicola/` — the Capicola firmware (v1.1.0), as a git submodule.
  Only its `lib/` DSP headers are compiled into the engine.
- `core/` — `CapicolaEngine`, the firmware's audio engine ported off the Daisy;
  `CapicolaProcessor`, which adds sample-rate conversion, the parameters and
  modulation, and is what the plugin will use.
- `tools/render/` — the command-line renderer.
- `tests/` — engine tests, plus stubs that let the firmware engine build on a
  desktop for comparison.
- `docs/PLAN.md` — the build plan and milestones.
