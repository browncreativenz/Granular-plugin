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
CMakeLists.txt              top level; options GRANULAR_BUILD_TESTS, GRANULAR_BUILD_TOOLS
external/capicola/          git submodule, pinned to v1.1.0 (only lib/ is compiled)
core/                       plain C++17, no JUCE, no Daisy
  CapicolaEngine.h/.cpp     port of src/audio/audio_engine.*, always at 48 kHz
  Resampler.h, RateBridge.h host rate ↔ 48 kHz around the engine
  Params.h                  knob tapers and defaults, copied from src/main.cpp
  ModMatrix.h               per-control source + depth, as in main.cpp's ModRouter
  CapicolaProcessor.h/.cpp  all of the above behind one host-facing class
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
   `KeyframeRecorder<2^21>` rings (about 24 MB each) move to the heap, allocated
   once in `prepareToPlay`, never on the audio thread.
2. **Block size.** The feedback path keeps exactly one 64-sample block
   (`kMaxBlock`), so the engine must always run full 64-sample blocks. `Process()`
   buffers through a 64-sample FIFO and runs the firmware block on every 64th
   sample. Output is then identical whatever block sizes the host sends, at a cost
   of 64 samples (1.3 ms at 48 kHz) of latency on both dry and wet paths. Parameter
   and modulation updates land at the next block boundary.
3. **Sample rate.** The engine always runs at 48 kHz, as the hardware does, and
   `RateBridge` resamples other host rates to 48 kHz and back around it. At
   48 kHz the bridge is a pass-through.

   *Changed in M1 from patching the firmware.* A close read showed more than the
   one hard-coded `detector.Init(48000)`: the detector's Teager-energy envelope
   scales with the square of the sample rate (moving the follower levels, and so
   the modulation depth), its silence floor is a fixed level, and the grain
   crossfade has a fixed 48-sample floor. These are private to the firmware
   library, so native rates would mean patching several places and re-tuning them
   by ear. Resampling keeps the engine exactly the hardware's at every rate and
   leaves the firmware untouched.

   The converter is a polyphase Kaiser-windowed sinc: flat to 0.9 of the lower
   Nyquist, about 100 dB stopband. Measured at 44.1, 88.2, 96, 176.4, 192 and
   32 kHz: passband error under 0.0001 dB, distortion and noise around −120 dB,
   anything above Nyquist rejected by more than 100 dB (`tests/resampler_test.cpp`).
4. **Latency.** Report the total to the host so Live compensates: the FIFO's 64
   samples at 48 kHz (1.3 ms), plus about 2.7 ms for resampling at other rates
   (188 samples in all at 44.1 kHz, 385 at 96 kHz). The wet signal also lags by
   design (the Fade time); do not report that, because the dry/wet mix uses the
   live input and the hardware behaves the same way.
5. **Control frames.** The module reads its knobs, applies modulation and pushes
   values into the engine once per 16 ms control frame (the SDK's default
   `ControlLoop` rate). `CapicolaProcessor` does the same every 12 engine blocks
   (768 samples), so automation and modulation step at the module's rate.

Acceptance:
- At 48 kHz, the engine's output for a fixed input matches the firmware's
  `AudioEngine` sample for sample (`tests/engine_golden.cpp`).
- The firmware's own host tests still pass.
- At 44.1 and 96 kHz, the same material gives the same number of slices and the
  same level as at 48 kHz (`tests/processor_test.cpp`).

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

- **Slice:** a button parameter that slices each time it turns on, plus any MIDI note-on, at its sample position. It
  forces a slice exactly as the hardware's Slice button does.
- **Reverse toggles (M3).** On the module, holding Slice and nudging
  Pitch or Stretch flips its direction at the same speed (1.5x forward becomes
  1.5x reverse). In the plugin this can be two toggle parameters, Pitch Reverse
  and Stretch Reverse, that mirror the knob around noon. They are MIDI-mappable in
  Live, so they work as performance buttons.
- SHIFT-snap and the save history are not needed: double-click resets a knob, and
  Live saves presets and sets.

## Follower outputs (MIDI out)

Live does not route MIDI out of audio-effect plugins, so the plugin sends these on
its own virtual MIDI port instead. On macOS a plugin can create one with
`juce::MidiOutput::createNewDevice`, so no IAC setup is needed. Each instance takes
the lowest free number for its port: "Capicola 1", "Capicola 2", and so on.

How it is built (M3):
- `core/FollowerOut.h` (plain C++) watches the counters and envelopes and emits
  note and controller events with their sample position in the block.
- The plugin checks the followers every 64 samples and after each manual slice,
  and pushes events into a lock-free queue: the audio thread does nothing else.
- A message-thread timer opens the port; a MIDI thread sends each event when it
  falls due and the note-off 10 ms later. Due times follow the audio sample clock,
  anchored to the wall clock and re-anchored only after a dropout, so the host's
  scheduling jitter doesn't reach the notes. Nothing is sent during an export.
- Envelope controllers go out when their 7-bit value changes, at most every 10 ms
  each. Trigger notes use a fixed velocity of 100.

| Signal | MIDI | Default |
|---|---|---|
| Input slice (each slice the engine takes) | note on/off, fixed length | note C1 |
| Output event (output follower transient) | note on/off | note D1 |
| Input envelope | CC, sent when the value changes | CC 20 |
| Output envelope | CC | CC 21 |

Channel, notes, and CC numbers are settings saved with plugin state.

Trigger semantics, from the module's manual and `src/main.cpp`:
- **Input slice** fires on *every slice that happens*: detected transients and
  manual slices (Slice button, MIDI slice note) alike. The engine's `SliceCount()`
  only counts detected ones, so the plugin adds its own manual slices, as the
  firmware does for B2 and Trig In.
- **Output event** fires on each transient the output follower keeps. It never
  slices anything.
- Like the module's 10 ms trigger pulses, a trigger is not re-sent while the
  previous one is still "high" (10 ms). The left and right channels can slice
  in adjacent blocks for one stereo hit, and this keeps that to one note.

Using it in Live:
1. In Settings > Link, Tempo & MIDI, turn on **Track** and **Remote** for the
   "Capicola" input port.
2. **Triggers:** on a MIDI track, set MIDI From to the Capicola port and Monitor to
   In. The slice notes then play a Drum Rack or any instrument.
3. **Envelopes:** with Remote on, MIDI-map the envelope CCs (Cmd-M) to any Live
   control, such as a filter cutoff on another track.

To check in Live: whether a port created by a plugin appears without restarting
Live, and how tight the notes feel. MIDI sent this way is not sample-accurate, and
it leaves about a buffer ahead of the audio it belongs to. If the port proves
awkward, the fallback is an IAC Driver bus that you enable once in Audio MIDI
Setup.

## Milestones

| # | Milestone | Where it can be done | Done when |
|---|---|---|---|
| M0 ✅ | Scaffold: submodule, CMake, `core/` engine port, CLI renderer, CTest | Cloud session | 48 kHz output matches the firmware; firmware tests pass |
| M1 ✅ | Sample-rate support (resampling), `Params.h` tapers, `ModMatrix`, `CapicolaProcessor`, unit tests | Cloud session | taper tests match the firmware formulas; multi-rate checks pass |
| M2 ✅ | JUCE plugin with generic controls, state save/load, Slice button and note; macOS CI | Cloud session writes it; CI builds it on a macOS runner; your Mac for listening | `auval` and `pluginval` pass in CI; it loads and sounds right in Live |
| M3 🔶 | Virtual MIDI port output, Pitch and Stretch Reverse (modulation landed in M1; follower meters move to M4, where the interface can show them) | Cloud plus your Mac | slice notes play a Drum Rack in Live; envelope CCs MIDI-map |
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
  sample-rate findings may still interest the author.
- **CPU.** Rendering on a cloud x86 machine ran about 150x faster than real
  time at 48 kHz and about 50x at 44.1 kHz, where the resampler costs about twice
  what the engine does. Either is a small share of one core, but measure it in
  Live at a 32-sample buffer. If it matters, the resampler's stopband can drop
  from 100 dB to 90 dB to shorten its filters.
- **Memory.** About 48 MB per instance for the two rings. The manual puts 2^21
  keyframes at about 44 seconds of full-rate audio and, since only extrema are
  kept, minutes in practice. Freeze and reverse depend on that history, so keep
  the full size.
- **Firmware updates.** Pin to tags, and keep the golden-render test so an update
  that changes the sound is noticed.
