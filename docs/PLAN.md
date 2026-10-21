# Capicola plugin: build plan

A desktop audio plugin that runs the DSP core of
[Capicola](https://github.com/heavylight-industries/capicola), the real-time time
stretcher, pitch shifter, envelope follower, and transient detector for the
Hermetic Modular Alchemy Lab.

## Decisions

| Topic | Choice |
|---|---|
| Platform | macOS (Apple Silicon and Intel, universal binary) |
| Formats | VST3, AU, Standalone |
| Framework | JUCE 8 (AGPLv3 open-source licence) with CMake |
| First interface | JUCE generic controls; a faceplate-style interface comes later |
| Follower outputs | MIDI notes for triggers, MIDI CC for envelopes |
| Licence | AGPL-3.0, matching Capicola |
| Firmware version | Capicola `v1.1.0`, pinned as a git submodule |

## Repository layout

```
CMakeLists.txt              top level; options CAPICOLA_BUILD_PLUGIN, CAPICOLA_BUILD_TESTS
external/capicola/          git submodule, pinned to v1.1.0 (only lib/ is compiled)
core/                       plain C++17, no JUCE, no Daisy
  CapicolaEngine.h/.cpp     port of src/audio/audio_engine.*
  Params.h                  knob tapers and defaults, copied from src/main.cpp
  ModMatrix.h               per-control source + depth, as in main.cpp's ModRouter
  FollowerOut.h             turns follower counts and envelopes into MIDI events
tools/render/               command-line WAV in, WAV out renderer
tests/                      CTest: firmware host tests, golden renders, taper tests
plugin/                     JUCE AudioProcessor and editor
.github/workflows/          macOS CI: build, auval, pluginval
```

`core/` never includes JUCE, so the same engine can later back a norns
SuperCollider UGen.

## The engine port (`core/CapicolaEngine`)

Start from `src/audio/audio_engine.cpp` (about 290 lines) and change only this:

1. **Daisy dependencies.** Drop `daisy::CpuLoadMeter` and `DSY_SDRAM_BSS`. The two
   `KeyframeRecorder<2^21>` rings (about 24 MB each) become heap members allocated
   once in `prepareToPlay`, never on the audio thread.
2. **Block size.** The feedback path keeps exactly one 64-sample block
   (`kMaxBlock`). `process()` cuts the host buffer into 64-sample chunks, and the
   last chunk may be shorter. Parameter and modulation updates happen once per chunk.
3. **Sample rate.** The library already carries `fs` through `Analyzer` and
   `Detector`. The one hard-coded value is `KeyframeRecorder::Init`, which calls
   `detector.Init(48000.0f)`. Patch it to take a sample rate, and offer the change
   upstream as a pull request. Until it is merged, carry it as a patch applied by
   CMake. Engine constants then scale with the rate:
   - durations in samples (fade 960 = 20 ms, drift guard 48000 = 1 s): multiply by `fs / 48000`
   - normalised cutoffs (feedback band 0.02, follower smoothing 0.0014): multiply by `48000 / fs`
4. **Latency.** The wet signal lags by design: the Fade time is the latency. Do not
   report it to the host, because the dry/wet mix uses the live input and the
   hardware behaves the same way. Revisit if users ask for aligned output.

Acceptance:
- At 48 kHz, the engine's output for a fixed input matches the firmware's
  `AudioEngine` sample for sample, using the same settings as
  `tests/host/blend_render.cpp`.
- The firmware's own host tests still pass.
- At 44.1 and 96 kHz, slice timing and fade length in seconds stay within one
  64-sample block of the 48 kHz result.

## Parameters

All parameters are host-automatable and stored as 0..1 positions, using the
firmware's tapers and factory defaults (from `src/main.cpp`, v1.1.0).

### Primary

| ID | Name | Mapping | Default position (value) |
|---|---|---|---|
| `pitch` | Pitch | linear −2x..+2x; noon stops; ±0.03 detent at ±1x; below noon plays backward | 0.75 (+1x) |
| `stretch` | Stretch | bipolar, sign × \|2n−1\|^2.5; freeze at noon, realtime at the stops, CCW is reverse | 0.9 (about 1.75x slower) |
| `threshold` | Threshold | 0..0.9 maps to keep ratio 0..4; 0.9..1 maps to 4..8; above 0.99 mutes auto-slicing | 0.5 |
| `grain` | Grain Size | linear 32..4096 keyframes | 0.15 (642) |
| `quality` | Quality | linear ε 0.08 (crunchy) .. 0.001 (best) | 1.0 (0.001) |
| `feedback` | Feedback | linear 0..1.5 | 0 |

### Secondary

| ID | Name | Mapping | Default (value) |
|---|---|---|---|
| `smoothing` | Envelope Smoothing | exponential fc 5e-5..0.125 | 0.4259 (0.0014) |
| `fade` | Fade | exponential 10..250 ms | 0.2153 (20 ms) |
| `drive` | Drive | linear 0.5..4 | 0.1429 (1.0) |
| `character` | Character | 0 quake, 0.5 clean, 1 sinc | 1.0 (sinc) |
| `mix` | Mix | linear dry..wet | 1.0 (wet) |
| `fbBand` | Feedback Band | exponential fc 2e-3..0.9 | 0.3769 (0.02) |

### Modulation (one set per primary control)

| ID | Name | Range | Default |
|---|---|---|---|
| `depth1`..`depth6` | Depth | −1..+1 | 0 |
| `src1`..`src6` | Source | Input follower, Output follower, Mod In A, Mod In B, Mod In C | Input follower |

As in the firmware's `ModRouter`, the modulated position is the knob position plus
`depth × source`, clamped to 0..1. It is mapped through the knob's taper afterwards.
The followers are unipolar (0..1). Mod In A–C are bipolar (−1..+1) and stand in for
the hardware's CV inputs; they are driven by assignable MIDI CCs.

### Triggers

- **Slice:** a momentary button parameter, plus a MIDI note-on (default C3). It
  forces a slice exactly as the hardware's Slice button does.
- The hardware's polarity-flip and SHIFT-snap gestures are not needed. Pitch and
  Stretch are already bipolar, and "reset to default" is a host feature.

## Follower outputs (MIDI out)

| Signal | MIDI | Default |
|---|---|---|
| Input slice (each slice the engine takes) | note on/off, fixed length | note C1 |
| Output event (output follower transient) | note on/off | note D1 |
| Input envelope | CC, sent when the value changes | CC 20 |
| Output envelope | CC | CC 21 |

Channel, notes, and CC numbers are settings saved with plugin state.

**Host caveat.** As far as I know, Logic cannot route MIDI out of an AU audio
effect, and Ableton Live does not route MIDI out of audio-effect plugins either.
Reaper, Bitwig, and Cubase can. To cover Logic and Live, the Standalone build (and
optionally the plugin) can also send to a virtual MIDI port such as the macOS IAC
bus. The internal modulation works in every host regardless.

## Milestones

| # | Milestone | Where it can be done | Done when |
|---|---|---|---|
| M0 | Scaffold: submodule, CMake, `core/` engine port, CLI renderer, CTest | Cloud session | 48 kHz output matches the firmware; firmware tests pass |
| M1 | Sample-rate patch, `Params.h` tapers, `ModMatrix`, unit tests | Cloud session | taper tests match the firmware formulas; multi-rate checks pass |
| M2 | JUCE plugin with generic controls, state save/load, Slice button and note; macOS CI | Cloud session writes it; CI builds it on a macOS runner; your Mac for listening | `auval` and `pluginval` pass in CI; it loads and sounds right in your DAW |
| M3 | Modulation, MIDI out, IAC option, follower meters | Cloud plus your Mac | followers visibly drive the controls; MIDI arrives in a test host |
| M4 | Faceplate interface: six LED-ring knobs, three buttons, page views | Your Mac (needs visual iteration) | matches the interactive manual's panel |
| M5 | Release: universal binary, codesign, notarise, installer, source offer for AGPL | Your Mac (needs your Apple Developer ID) | a signed `.pkg` installs cleanly on a clean Mac |

A macOS GitHub Actions runner can build the plugin and run `auval` and `pluginval`,
so the cloud session can check that the plugin compiles and passes validation
without your Mac. Listening tests and DAW checks still need your Mac, either
directly or through a `claude remote-control` session started in the repo folder.

## Risks and open questions

- **Upstream relationship.** Contact heavylight-industries before a public release.
  The sample-rate patch is a natural first pull request.
- **CPU.** The firmware's offline renderer ran about 130x faster than real time per
  channel on a cloud x86 machine. That should be comfortable on any Mac, but measure
  it in a DAW at a 32-sample buffer.
- **Memory.** About 48 MB per instance for the two rings. If many instances are a
  concern, a smaller ring is a template parameter. Measure how much history 2^21
  keyframes holds before shrinking it.
- **Firmware updates.** Pin to tags, and keep the golden-render test so an update
  that changes the sound is noticed.
