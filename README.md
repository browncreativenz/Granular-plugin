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

Milestone M0 of [the plan](docs/PLAN.md) is done: Capicola's audio engine runs
on a desktop and matches the firmware sample for sample. The JUCE plugin comes
in M2.

## Build

Requires CMake 3.22 or later and a C++17 compiler (Xcode's clang on macOS).

```sh
git submodule update --init external/capicola   # first checkout only; no --recursive needed
cmake -B build
cmake --build build
ctest --test-dir build
```

The tests run the firmware's own DSP test suite and compare this port's engine
with the firmware's `audio_engine.cpp` bit for bit (`tests/engine_golden.cpp`).

## Render audio from the command line

```sh
./build/capicola_render out.wav                       # built-in drums-and-pad test signal
./build/capicola_render --in loop.wav --stretch 0.2 --pitch -1 out.wav
./build/capicola_render --help                        # every parameter
```

Input must be a 48 kHz WAV for now; other sample rates come in milestone M1.

## Layout

- `external/capicola/` — the Capicola firmware (v1.1.0), as a git submodule.
  Only its `lib/` DSP headers are compiled into the engine.
- `core/` — `CapicolaEngine`, the firmware's audio engine ported off the Daisy.
- `tools/render/` — the command-line renderer.
- `tests/` — engine tests, plus stubs that let the firmware engine build on a
  desktop for comparison.
- `docs/PLAN.md` — the build plan and milestones.
