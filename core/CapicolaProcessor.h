/**
 * @file CapicolaProcessor.h
 * @brief Capicola for a host: any sample rate, knob-position parameters,
 *        modulation. Everything the plugin needs short of the JUCE layer.
 *
 *   host audio ─▶ RateBridge (to 48 kHz) ─▶ control frames + CapicolaEngine
 *              ◀─ RateBridge (to host rate) ◀─┘
 *
 * Control frames: the module reads its knobs, applies modulation and pushes
 * values into the engine once per ~16 ms control frame (alchemy-sdk
 * ControlLoop, kDefaultFrameMs). The processor does the same every
 * kFrameBlocks engine blocks, so parameter changes and modulation step at the
 * module's rate.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "CapicolaEngine.h"
#include "ModMatrix.h"
#include "Params.h"
#include "RateBridge.h"

namespace granular {

class CapicolaProcessor
{
  public:
    /** Engine blocks per control frame: 12 × 64 samples = 16 ms at 48 kHz. */
    static constexpr int kFrameBlocks = 12;

    CapicolaProcessor();

    /** Allocate and reset for the host's sample rate. Parameter values are
     *  kept. Not real-time safe. */
    void Prepare(int hostSampleRate);

    /** Clear audio state (as after Prepare) without reallocating. */
    void Reset();

    /** Total latency in host samples: the engine's block FIFO plus any
     *  resampling. */
    int LatencySamples() const { return bridge.LatencySamples(); }

    /** Knob position 0..1 for continuous parameters, index for choices.
     *  Takes effect at the next control frame. Not thread-safe: call it from
     *  the audio thread, before Process(). */
    void  SetParam(Param p, float value) { values[(int)p] = value; }
    float GetParam(Param p) const        { return values[(int)p]; }

    /** Modulated knob position of primary control i (0..5) as of the last
     *  control frame, for display. */
    float EffectivePosition(int control) const { return effective[control]; }

    /** Manual slice (the module's Slice button); lands in the next engine block. */
    void TriggerSlice() { engine.TriggerSlice(); }

    /** Stereo, any block size; in and out may alias. */
    void Process(const float* inL, const float* inR, float* outL, float* outR, int n);

    /* Follower signals (see CapicolaEngine). */
    uint32_t SliceCount()    const { return engine.SliceCount(); }
    uint32_t OutEventCount() const { return engine.OutEventCount(); }
    float    EnvNormIn()     const { return engine.EnvNormIn(); }
    float    EnvNormOut()    const { return engine.EnvNormOut(); }
    float    InPeak()        const { return engine.InPeak(); }

  private:
    void ProcessInner(float* l, float* r, std::size_t n);
    void ControlFrame();

    CapicolaEngine engine;
    RateBridge     bridge;
    ModMatrix      mod;

    float values   [kNumParams];
    float effective[kNumPrimary] = {};
    float pushed   [kNumParams];      // secondary values last sent to the engine
    int   frameCountdown = 0;         // engine blocks until the next control frame
};

} // namespace granular
