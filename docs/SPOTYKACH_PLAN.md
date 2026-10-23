# Spotykach plugin: build plan

A desktop audio plugin that runs the firmware of
[Spotykach](https://github.com/Synthux-Academy/spotykach), Synthux Academy's
two-deck looping and granular instrument, in Ableton Live. It builds on the
work done for Capicola in this repo (see [PLAN.md](PLAN.md)): the same JUCE
setup, resampler, MIDI-out port and macOS CI.

## What the firmware is

- Repository: `github.com/Synthux-Academy/spotykach`, tag `v1.3.1`
  (commit `bdc0f77`), the release Synthux lists as Spotykach v1.3.1. Newer
  `v1.4.0.b*` betas exist.
- Licence: **MIT** (Synthux Academy). Its dependencies are DaisySP (MIT,
  pinned at `599511b`) and a libDaisy fork by bleeptools (MIT). The firmware
  uses none of DaisySP's LGPL modules. MIT code can go into this AGPL-3.0
  repo, as long as its copyright notices are kept.
- Hardware: Daisy Seed (STM32H750, 480 MHz), 48 kHz, 96-sample blocks.
  2 stereo decks, 12 touch pads, 15 pots, 7 CV inputs, 2 gate inputs and
  outputs, a clock input, 2 LFO/follower CV outputs, MIDI over USB and DIN,
  two 32-LED rings, and an SD card for saved loops.
- Code layout:
  - `src/core/` (about 5,000 lines): the audio engine. `Core` holds two `Deck`s
    (buffer, detector, grain `Generator` with voices, `Track` sequencer,
    `Fx` with Grit and Flux), two `Modulator`s (LFO or envelope follower),
    a `Panner`, a `Click` and a `Driver` (clock, tempo, key intervals). The
    deck modes are Reel, Slice and Drift. The routings are Stereo, Double
    Mono and Generative Stereo.
  - `src/ui/` (about 2,500 lines): `CoreUI` turns pots, pads, switches, CV
    and MIDI into engine calls, and draws the LEDs. Much of the instrument's
    behaviour lives here: pad gestures, Alt and Shift combinations, pitch
    quantising, the speed map and the factory values.
  - `src/hw/`, `src/memory/`: drivers, the SDRAM pool, SD-card and flash
    storage.

## Feasibility check (done)

The firmware's `src/core` was built on Linux with g++ and none of its files
were changed:

- 25 of its 27 `.cpp` files compiled once a short stub header was added.
  The stub supplies the few libDaisy pieces they use (`System::GetNow`,
  `StopwatchTimer`, `ScopedIrqBlocker`, `Logger`) and the standard headers
  that the ARM toolchain pulls in indirectly.
- The other two, `core.cpp` and the memory pool, reach the SD-card driver
  through `hw/buffer.sdram.h`. A heap-backed pool with the same interface
  replaced that header, and then everything linked against DaisySP.
- A test program set Deck A to Reel and gave it the UI's factory values.
  It pressed Record twice: the first press armed the deck and the second
  closed the loop. The deck recorded a 1.99 s loop and played it back with
  the input silent.
- It rendered 6 s of audio in under 0.1 s (one deck playing). CPU will not
  be a problem.

So the engine itself needs no porting, only a thin shell around it. The real
work is outside `src/core`: the clock, the controls, and saving state.

## Decisions (proposed, following Capicola's)

| Topic | Choice |
|---|---|
| Use | Personal use only; no public release ("Spotykach" is Synthux's product name) |
| Host | Ableton Live on macOS; VST3 (main), AU, Standalone |
| Repo | This repo, next to Capicola: a second plugin target sharing `core/Resampler.h`, `RateBridge.h` and the MIDI-out code |
| Firmware | `v1.3.1` as a git submodule (`external/spotykach`), plus DaisySP at the firmware's pinned commit (`external/DaisySP`). Firmware sources compiled **unmodified** |
| Engine rate | Always 48 kHz, 96-sample blocks, as on the hardware. `RateBridge` converts other host rates |
| Controls | Run the firmware's own `CoreUI` against a desktop `Hardware` class, so that every gesture and LED behaves as on the module |
| Clock | Follows Live's tempo and transport by default; the module's internal clock is an option |
| Saved loops | Recorded audio is saved in the Live set with the plugin state (open question below) |
| Outputs | Gate outs, LFO outs and clock out as MIDI on a virtual port, as Capicola's TRIG and ENV outputs are |
| Licence | Repo stays AGPL-3.0; the firmware's MIT notices are kept |

## Architecture

```
external/spotykach/         submodule, v1.3.1: src/core and src/ui compiled as is
external/DaisySP/           submodule, at the firmware's pinned commit
spotykach/                  desktop shell, plain C++17, no JUCE
  shim/                     libDaisy stand-ins: System (sample clock), StopwatchTimer,
                            ScopedIrqBlocker, Logger, UiEventQueue, PotMonitor, MIDI types
  shim/hw/                  Hardware (pots, CV, pads, switches, gates, LED framebuffer),
                            buffer.sdram.h (heap pool, allocated in prepare)
  shim/memory/              Storage and Settings backed by plugin state, not SD or flash
  SpotykachProcessor.h/.cpp host-facing: rate bridge, 96-sample FIFO, clock sync, params
plugin/spotykach/           JUCE AudioProcessor and editor
tools/render/               spotykach_render: scripted pad presses, WAV in and out
tests/spotykach/            scripted scenario tests (record, play, modes, sync)
```

Shim headers come first on the include path. The firmware's own includes,
such as `"hw/buffer.sdram.h"` or `<daisy_seed.h>`, then resolve to the
desktop versions. No firmware file is patched.

### Timing: one firmware "tick" per 96 samples

On the hardware, several things run at different rates:

| Hardware | Rate | In the plugin |
|---|---|---|
| Audio callback: `ProcessAnalogControls`, `CoreUI::tick`, `read_cv`, `Core::process` | every 96 samples | the same, on each full 96-sample block |
| TIM5 interrupt: `process_gate_in` (drives `Driver::tick`), `Core::prepare`; LEDs every 4th call | 500 Hz = every 2 ms | once per block, since 96 samples at 48 kHz is exactly 2 ms |
| Main loop: `CoreUI::process` (pots, pads, switches, MIDI) | as fast as it can | once per block |
| DAC callback: LFO outputs | DAC rate | read once per block |

Everything runs in that order on the audio thread, one block at a time.
That removes the races between interrupts and the main loop, and makes
renders repeatable. The shim's `System::GetNow()`, `GetTick()` and
`StopwatchTimer` count engine samples instead of wall-clock time. Hold
times, tap tempo and the clock timeout then behave the same in an offline
export as in real time.

A 96-sample FIFO decouples the engine from host block sizes, as Capicola's
64-sample FIFO does. That adds 2 ms of latency, reported to Live, plus
about 2.7 ms for resampling at rates other than 48 kHz.

### Clock sync with Live

`Driver` already accepts an external clock at 24 PPQN, its MIDI clock mode.
The processor reads Live's tempo and beat position each block and makes
24 PPQN ticks at the exact sample where each falls. It also sends Start on
the transport's start and Stop on its stop. Quantised record, Slice mode's
grid and the sequencer then lock to Live's bars. When Live's transport is
stopped, the module's own internal clock can be used instead.

## Parameters

The 15 pots, the switches and the pads, taken from `Hardware.h` and
`CoreUI::_init_values` (v1.3.1). Parameters are 0..1 positions, as on the
module; `CoreUI` applies its own curves.

Each pot does several jobs, depending on what is held while it turns
(`CoreUI::_process_ui_queue`):

| Pot (per deck) | Alone | With Alt | With Grit / Flux held | With Tap held |
|---|---|---|---|---|
| SOS | in/out mix | overdub feedback | FX mix | key interval |
| Pos | start | start offset (once there is a cue) | Grit character / Flux feedback | |
| Size | size (Slice: quarters) | window size (Drift) | | |
| Pitch | speed, or pitch in Slice | quantised | Grit / Flux intensity | |
| Env | envelope shape | envelope size (Drift) | | |
| Mod Freq | mod speed | | | tempo |
| Mod Amount | mod amount | | | click level |

Plus the Crossfade pot (shared), and these:

| Group | Parameters (per deck A/B, unless noted) |
|---|---|
| Switches | Route (Stereo, Double Mono, Generative Stereo, shared); deck mode (Reel, Slice, Drift); mod type (LFO square, LFO random, Follow) |
| CV inputs | Size/Pos, Mix, V/Oct; Crossfade (shared): automatable, like Capicola's Mod In |
| Pads and buttons | Play, Rev, Grit, Flux, Seq per deck; Spot; Alt; Tap: button parameters plus MIDI notes |

On the module the layers use pick-up (`MValue`): a value moves only once
the pot passes it, so nothing jumps when you switch layers. To be settled
in S1: either give each layered control its own parameter, which is easy to
automate in Live and needs a small adapter that writes straight to the
`MValue`s, or expose only the 15 physical pots and keep the module's layers
exactly. The first is probably better in Live; the window can still show
the module's layers.

MIDI into the plugin works as on the module: `CoreMIDI` handles its note map
and its CC map (`config.h`: CC 3 crossfade, 20–28 deck controls, 85/86
direction, 102–109 FX, and so on), on channels 1 and 2 by default.

## Milestones

| # | Milestone | Where | Done when |
|---|---|---|---|
| S0 | Submodules; `shim/` with heap pool and sample clock; firmware `src/core` built unmodified into `spotykach_core`; `spotykach_render` with scripted pad presses; CTest scenarios | Cloud session | Reel, Slice and Drift record and play in scripted tests; output is identical whatever the host block size |
| S1 | Desktop `Hardware`; firmware `CoreUI` and `CoreMIDI` running unmodified; `Storage` and `Settings` backed by memory; `SpotykachProcessor` with rate bridge, FIFO and Live clock sync | Cloud session | Gesture tests pass (hold to clear, Alt combos, quantised record locked to a simulated host transport); multi-rate checks pass |
| S2 | JUCE plugin with generic controls; pads as buttons and MIDI notes; state save and load including recorded loops; macOS CI with `auval` and `pluginval` | Cloud session, then your Mac | Loads in Live, records and loops in time with Live's tempo, and a saved set reopens with its loops |
| S3 | Gate outs, LFO outs and clock out as MIDI on a "Spotykach 1" virtual port, reusing Capicola's MIDI-out code | Cloud plus your Mac | Gate notes play a Drum Rack; LFO CCs MIDI-map in Live |
| S4 | The plugin window, from a mockup you approve first: 12 pads, two 32-LED rings drawn from the firmware's own LED output, pots, switches, crossfader | Cloud session builds it and renders screenshots; your Mac for feel | Looks like the mockup, and the LEDs match the module |
| S5 (optional) | WAV import and export of loops (the firmware has `memory/wav.cpp`), drag and drop with Live; SD-card slots as loop banks | Cloud plus your Mac | A Live clip dropped on a deck plays; a deck loop drags out as a clip |

## Risks and open questions

- **Saving loops.** Each deck holds up to 42 s of stereo audio, about 16 MB as
  floats. Saving the recorded part in the plugin state makes Live sets
  bigger, by up to about 32 MB per instance, but keeps everything in one
  file. The alternatives are 16-bit storage (half the size) or not saving
  loops at all, as the module forgets them on power-off unless saved to SD.
- **Pads and a mouse.** Many gestures press two pads at once: Alt plus a
  pad, holding Play, Seq combinations. With a mouse that means a latching Alt
  and Shift in the window. MIDI notes from a pad controller allow true
  chords; a Push or any pad controller is the natural way to play it.
- **Firmware behaviour tied to timing.** `CoreUI` measures holds and
  blinks with libDaisy timers. Driving those from the sample clock keeps
  them right, but the first scenario tests should confirm hold-to-clear
  and tap tempo.
- **Memory.** Three 42 s stereo buffers (two decks plus undo), about 48 MB,
  plus delay lines, per instance. They are allocated once in
  `prepareToPlay`, never on the audio thread.
- **Calibration and boot.** The module calibrates its CV inputs and plays
  a boot animation. The shim reports calibration as done and moves
  `CoreUI` straight to its ready state, after `_init_values`.
- **Firmware updates.** Pin to tags; scenario renders are kept as golden
  files, so an update that changes the sound is noticed.
