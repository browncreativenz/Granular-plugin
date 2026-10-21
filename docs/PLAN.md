# Capicola plugin: build plan

A desktop audio plugin that runs the DSP core of
[Capicola](https://github.com/heavylight-industries/capicola), the real-time time
stretcher, pitch shifter, envelope follower, and transient detector for the
Hermetic Modular Alchemy Lab.

## Decisions

| Topic | Choice |
|---|---|
| Use | Personal use only; no public release |
| Host | Ableton Live on macOS |
| Platform | macOS, built for your Mac's architecture |
| Formats | VST3 (main), AU, and Standalone for quick tests |
| Framework | JUCE 8 (AGPLv3 open-source licence) with CMake |
| First interface | JUCE generic controls; a faceplate-style interface comes later |
| Follower outputs | MIDI notes for triggers, MIDI CC for envelopes, sent on a virtual MIDI port the plugin creates |
| Mod inputs | Plain automatable parameters, driven by Live's own modulation |
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
   (`kMaxBlock`), so the engine must always run full 64-sample blocks. `Process()`
   buffers through a 64-sample FIFO and runs the firmware block on every 64th
   sample. Output is then identical whatever block sizes the host sends, at a cost
   of 64 samples (1.3 ms at 48 kHz) of latency on both dry and wet paths. Parameter
   and modulation updates land at the next block boundary.
3. **Sample rate.** The library already carries `fs` through `Analyzer` and
   `Detector`. The one hard-coded value is `KeyframeRecorder::Init`, which calls
   `detector.Init(48000.0f)`. Patch it to take a sample rate, and offer the change
   upstream as a pull request. Until it is merged, carry it as a patch applied by
   CMake. Engine constants then scale with the rate:
   - durations in samples (fade 960 = 20 ms, drift guard 48000 = 1 s): multiply by `fs / 48000`
   - normalised cutoffs (feedback band 0.02, follower smoothing 0.0014): multiply by `48000 / fs`
4. **Latency.** Report the FIFO's 64 samples to the host so Live compensates for
   them. The wet signal also lags by design (the Fade time); do not report that,
   because the dry/wet mix uses the live input and the hardware behaves the same
   way.

Acceptance:
- At 48 kHz, the engine's output for a fixed input matches the firmware's
  `AudioEngine` sample for sample (`tests/engine_golden.cpp`).
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
| `modA`..`modC` | Mod In A–C | −1..+1 | 0 |

As in the firmware's `ModRouter`, the modulated position is the knob position plus
`depth × source`, clamped to 0..1. It is mapped through the knob's taper afterwards.
The followers are unipolar (0..1). Mod In A–C are bipolar and stand in for the
hardware's CV inputs. They are ordinary automatable parameters, so in Live you drive
them with clip envelopes, Max for Live LFO or Envelope Follower devices, or a MIDI
mapping.

### Triggers

- **Slice:** a momentary button parameter, plus a MIDI note-on (default C3). It
  forces a slice exactly as the hardware's Slice button does.
- The hardware's polarity-flip and SHIFT-snap gestures are not needed. Pitch and
  Stretch are already bipolar, and "reset to default" is a host feature.

## Follower outputs (MIDI out)

Live does not route MIDI out of audio-effect plugins, so the plugin sends these on
its own virtual MIDI port instead. On macOS a plugin can create one with
`juce::MidiOutput::createNewDevice`, so no IAC setup is needed. Each instance names
its port, for example "Capicola 1", and the name is shown in the plugin window.

| Signal | MIDI | Default |
|---|---|---|
| Input slice (each slice the engine takes) | note on/off, fixed length | note C1 |
| Output event (output follower transient) | note on/off | note D1 |
| Input envelope | CC, sent when the value changes | CC 20 |
| Output envelope | CC | CC 21 |

Channel, notes, and CC numbers are settings saved with plugin state.

Using it in Live:
1. In Settings > Link, Tempo & MIDI, turn on **Track** and **Remote** for the
   "Capicola" input port.
2. **Triggers:** on a MIDI track, set MIDI From to the Capicola port and Monitor to
   In. The slice notes then play a Drum Rack or any instrument.
3. **Envelopes:** with Remote on, MIDI-map the envelope CCs (Cmd-M) to any Live
   control, such as a filter cutoff on another track.

Risks to check early in M3: whether a port created by a plugin appears in Live
without restarting Live, and how much timing jitter the port adds. MIDI sent this
way is not sample-accurate. If the port proves awkward, the fallback is an IAC Driver
bus that you enable once in Audio MIDI Setup.

## Milestones

| # | Milestone | Where it can be done | Done when |
|---|---|---|---|
| M0 ✅ | Scaffold: submodule, CMake, `core/` engine port, CLI renderer, CTest | Cloud session | 48 kHz output matches the firmware; firmware tests pass |
| M1 | Sample-rate patch, `Params.h` tapers, `ModMatrix`, unit tests | Cloud session | taper tests match the firmware formulas; multi-rate checks pass |
| M2 | JUCE plugin with generic controls, state save/load, Slice button and note; macOS CI | Cloud session writes it; CI builds it on a macOS runner; your Mac for listening | `auval` and `pluginval` pass in CI; it loads and sounds right in Live |
| M3 | Modulation, virtual MIDI port output, follower meters | Cloud plus your Mac | followers visibly drive the controls; slice notes play a Drum Rack in Live |
| M4 | Faceplate interface: six LED-ring knobs, three buttons, page views | Your Mac (needs visual iteration) | matches the interactive manual's panel |

No release milestone: the plugin is for your own use. Build it on your Mac with
`cmake --build`; JUCE copies the VST3 and AU into `~/Library/Audio/Plug-Ins`, and
the build signs them ad hoc, which is enough for a Mac that built them. A plugin
downloaded from CI instead is quarantined by macOS; clear that with
`xattr -dr com.apple.quarantine` on the bundle. After installing, rescan plugins in
Live (Settings > Plug-Ins).

A macOS GitHub Actions runner can build the plugin and run `auval` and `pluginval`,
so the cloud session can check that the plugin compiles and passes validation
without your Mac. Listening tests and DAW checks still need your Mac, either
directly or through a `claude remote-control` session started in the repo folder.

## Risks and open questions

- **Licence.** For private use the AGPL places no obligations on you; they apply
  only if you give the plugin to others or offer it over a network. The
  sample-rate patch is still worth offering upstream.
- **CPU.** The firmware's offline renderer ran about 130x faster than real time per
  channel on a cloud x86 machine. That should be comfortable on any Mac, but measure
  it in Live at a 32-sample buffer.
- **Memory.** About 48 MB per instance for the two rings. If many instances are a
  concern, a smaller ring is a template parameter. Measure how much history 2^21
  keyframes holds before shrinking it.
- **Firmware updates.** Pin to tags, and keep the golden-render test so an update
  that changes the sound is noticed.
