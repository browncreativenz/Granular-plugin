/**
 * @file CapicolaEngine.h
 * @brief Capicola's audio engine, ported off the Daisy for desktop hosts.
 *
 * Derived from Capicola src/audio/audio_engine.{h,cpp} (v1.1.0), AGPL-3.0.
 * The DSP is unchanged: two KeyframeRecorder chains (L and R), the filtered
 * feedback loop, the dry/wet mix and the output follower. What changed:
 *
 *   - No libDaisy: the CPU meter is gone and the two 2^21-frame rings live on
 *     the heap instead of in Daisy SDRAM.
 *   - Process() accepts any block size. The firmware runs fixed 64-sample
 *     blocks (its feedback path holds exactly one block), so Process() buffers
 *     through a 64-sample FIFO and runs the firmware block on every 64th sample.
 *     Output is therefore independent of the host's block size, at a cost of
 *     LatencySamples() = 64 samples of latency on both dry and wet paths.
 *   - Input is copied before processing, so in-place buffers are safe.
 *
 * Setters take engineering units, as the firmware's do, and land at the next
 * 64-sample block boundary.
 *
 * Always runs at 48 kHz, as the hardware does: the firmware's constants are in
 * samples at 48 kHz. CapicolaProcessor resamples other host rates around it.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace granular {

class CapicolaEngine
{
  public:
    static constexpr int    kBlock      = 64;        // firmware AUDIO_BLOCK_SAMPLES
    static constexpr double kSampleRate = 48000.0;   // firmware AUDIO_SAMPLE_RATE_HZ

    CapicolaEngine();
    ~CapicolaEngine();

    CapicolaEngine(const CapicolaEngine&)            = delete;
    CapicolaEngine& operator=(const CapicolaEngine&) = delete;

    /** Allocate the rings (first call only, about 48 MB) and reset the engine
     *  to its boot state. Not real-time safe on the first call. */
    void Prepare();

    /** Stereo processing, any block size; in and out may alias. */
    void Process(const float* in_l, const float* in_r,
                 float*       out_l, float*       out_r,
                 std::size_t  n);

    /** Delay added by the 64-sample block FIFO. */
    static constexpr int LatencySamples() { return kBlock; }

    /** Input samples still to arrive before the next firmware block runs
     *  (1..64). A setter called now lands in that block. */
    std::size_t SamplesToNextBlock() const { return kBlock - fifoPos; }

    /* ── Parameter setters — ENGINEERING UNITS (as the firmware) ─────────── */
    void SetSlicePitch          (float speed);    // signed playback speed (1 = unity, <0 = reverse)
    void SetSliceStretch        (float stretch);  // signed time stretch, 0 = freeze, ±1 = realtime
    void SetTransientThreshold  (float t);        // finder keep ratio (TKEO gate)
    void SetGrainSize           (float keyframes);// grain size (adaptive leash, keyframes)
    void SetMix                 (float wet01);    // dry/wet blend, 1 = full wet
    void SetFeedback            (float amt);      // TKEO-excited feedback gain
    void SetFeedbackCutoff      (float fc);       // feedback bandpass centre (normalised fc)
    void SetTkeoCutoff          (float fc);       // follower SVF cutoff (envelope smoothing)
    void SetTkeoResonance       (float q);        // finder SVF resonance, uncapped
    void SetFade                (float samples);  // OLA crossfade = retrigger floor
    void SetDrive               (float d);        // keyframe waveshaper drive
    void SetDriveCharacter      (float c);        // 0 = quake, 0.5 = clean, 1 = sinc
    void SetQuality             (float epsilon);  // analyzer keyframe threshold ε

    /** Manual slice on both channels (the hardware's Slice button). */
    void TriggerSlice();

    /* ── Follower signals, refreshed every 64-sample block ───────────────── */
    uint32_t SliceCount()    const;   // monotonic count of blocks in which a channel auto-sliced
    uint32_t OutEventCount() const;   // monotonic count of output-follower events
    float    EnvNormIn()     const;   // 0..1, max of L/R input followers
    float    EnvNormOut()    const;   // 0..1, output follower
    float    InPeak()        const;   // decaying abs max of the input

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    /* Block FIFO: input collects here until a full firmware block is ready;
     * output plays back the previous block's result. */
    float       fifoInL [kBlock] = {};
    float       fifoInR [kBlock] = {};
    float       fifoOutL[kBlock] = {};
    float       fifoOutR[kBlock] = {};
    std::size_t fifoPos          = 0;
};

} // namespace granular
