/**
 * @file CapicolaEngine.cpp
 * @brief Capicola audio engine — see CapicolaEngine.h.
 *
 * Impl::ProcessBlock and the setters are Capicola's src/audio/audio_engine.cpp
 * (v1.1.0) with the libDaisy pieces removed. Keep the arithmetic in the same
 * order: tests/engine_golden.cpp checks this file against the firmware
 * sample for sample.
 */

#include "CapicolaEngine.h"

#include <cmath>
#include <cstring>

#include "KeyframeRecorder.h"
#include "Shapers.h"

namespace granular {

using namespace capicola;

namespace {
/* Two independent keyframe rings, one per channel. 2^21 frames × 12 B = 24 MB
 * each (the firmware's SDRAM layout). */
constexpr int kRingFrames = 2097152;   // 2^21 per channel

/* OLA crossfade boot value: the retrigger floor. */
constexpr float kFadeSamples = 960.0f;    // 20 ms @ 48 kHz

/* Stereo re-alignment guardrail. When exactly one channel auto-fires and the two
 * grids have drifted more than this, the quiet channel is forced to slice,
 * re-anchoring to the same delayed tap. Below this they stay independent. */
constexpr double kMaxDriftSamples = 48000.0;   // 1 s @ 48 kHz

/* Feedback bandpass centre (normalised fc). 0.02 ≈ 480 Hz. */
constexpr float kFbFilterFc = 0.02f;

/* Cancels the sinc shaper's 2x small-signal gain so the loop matches tanh. */
constexpr float kFbSatGain = 0.5f;

/* Follower SVF cutoff (envelope smoothing), shared by the input detectors and
 * the output follower. */
constexpr float kTkeoFc = 0.0014f;

/* Envelope expansion for the normalised follower outputs: the low-passed Teager
 * energy is tiny, so norm = sqrt(clamp01(env × kEnvGain)). */
constexpr float kEnvGain = 200.0f;

inline float EnvNorm(float env)
{
    float e = env * kEnvGain;
    if (e < 0.0f) e = 0.0f; else if (e > 1.0f) e = 1.0f;
    return std::sqrt(e);
}

inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
} // namespace

struct CapicolaEngine::Impl
{
    static constexpr std::size_t kMaxBlock = CapicolaEngine::kBlock;

    KeyframeRecorder<kRingFrames> sparse_l;
    KeyframeRecorder<kRingFrames> sparse_r;

    /* Waveshaper LUTs, shared by both channels and the feedback saturator. */
    Shapers shapers;

    float mix = 1.0f;   // dry/wet, 1.0 = full wet (stretched)

    /* Feedback: last block's wet output → sinc saturator → SVF bandpass,
     * scaled by fbAmt into this block's input. Hard ±1 clamp guards runaway. */
    float fbAmt = 0.0f;
    float fbL[kMaxBlock] = {0.0f};
    float fbR[kMaxBlock] = {0.0f};
    StateVariable fbSvfL;
    StateVariable fbSvfR;

    /* Output-side follower (final mono-summed output) + cached IO signals. */
    Detector outDet;
    uint32_t sliceCount = 0;
    float envNormIn  = 0.0f;
    float envNormOut = 0.0f;
    float inPeak     = 0.0f;

    void Init();
    void ProcessBlock(const float* in_l, const float* in_r,
                      float*       out_l, float*       out_r,
                      std::size_t  n);
    void SetTkeoCutoff(float fc);
};

void CapicolaEngine::Impl::Init()
{
    /* Tabulate the waveshapers before the recorders bind to them. */
    shapers.Init();

    /* Start live capture + granular playback on both channels. */
    sparse_l.Init(&shapers);
    sparse_r.Init(&shapers);
    sparse_l.SetThreshold(0.001f);
    sparse_r.SetThreshold(0.001f);
    sparse_l.SetGrainPitch(1.0f);
    sparse_r.SetGrainPitch(1.0f);
    sparse_l.SetGrainLeash(128);
    sparse_r.SetGrainLeash(128);
    sparse_l.SetGrainFade(kFadeSamples);
    sparse_r.SetGrainFade(kFadeSamples);

    mix   = 1.0f;
    fbAmt = 0.0f;
    std::memset(fbL, 0, sizeof fbL);
    std::memset(fbR, 0, sizeof fbR);

    fbSvfL.Init();
    fbSvfR.Init();
    fbSvfL.SetControls(kFbFilterFc, 0.01f);
    fbSvfR.SetControls(kFbFilterFc, 0.01f);

    /* Output-side follower: same chain as the input detectors, run on the final
     * mono-summed output. Never punches — signal source only. */
    outDet.Init(48000.0f);
    SetTkeoCutoff(kTkeoFc);
    sliceCount = 0;
    envNormIn  = envNormOut  = 0.0f;
    inPeak     = 0.0f;

    sparse_l.SubmitRequest(Request::LIVE_EFFECT);
    sparse_r.SubmitRequest(Request::LIVE_EFFECT);
}

void CapicolaEngine::Impl::ProcessBlock(const float* in_l, const float* in_r,
                                        float*       out_l, float*       out_r,
                                        std::size_t  n)
{
    const std::size_t m = (n < kMaxBlock) ? n : kMaxBlock;

    /* Feedback: last block's wet output → sinc → SVF bandpass, scaled and summed
     * into the recorder input. Hard ±1 clamp on the injection. */
    const float* src_l = in_l;
    const float* src_r = in_r;
    float fin_l[kMaxBlock], fin_r[kMaxBlock];
    if (fbAmt > 0.0f)
    {
        for (std::size_t i = 0; i < m; i++)
        {
            fbSvfL.Tick(kFbSatGain * shapers.ReadSinc(fbL[i]));
            fbSvfR.Tick(kFbSatGain * shapers.ReadSinc(fbR[i]));
            const float vl = fbSvfL.GetBandpass();
            const float vr = fbSvfR.GetBandpass();

            float inj_l = vl * fbAmt;
            float inj_r = vr * fbAmt;
            if (inj_l > 1.0f) inj_l = 1.0f; else if (inj_l < -1.0f) inj_l = -1.0f;
            if (inj_r > 1.0f) inj_r = 1.0f; else if (inj_r < -1.0f) inj_r = -1.0f;

            fin_l[i] = in_l[i] + inj_l;
            fin_r[i] = in_r[i] + inj_r;
        }
        src_l = fin_l;
        src_r = fin_r;
    }

    sparse_l.ProcessBlock(src_l, out_l, m);
    sparse_r.ProcessBlock(src_r, out_r, m);

    /* Stereo re-alignment guardrail: only on the block where exactly one channel
     * auto-fired. If they've drifted past the guardrail, force the quiet channel
     * to slice — it re-anchors to the same delayed tap. */
    const bool fired_l = sparse_l.FiredThisBlock();
    const bool fired_r = sparse_r.FiredThisBlock();
    if (fired_l != fired_r)
    {
        const double lag_l = sparse_l.GridLag();
        const double lag_r = sparse_r.GridLag();
        const double drift = (lag_l > lag_r) ? (lag_l - lag_r) : (lag_r - lag_l);
        if (drift > kMaxDriftSamples)
            (fired_l ? sparse_r : sparse_l).SubmitRequest(Request::SLICE);
    }

    /* One slice event per block in which either channel punched. The guardrail
     * slice above is a re-anchor, not a second musical hit, so it adds no tick. */
    if (fired_l || fired_r) sliceCount++;

    /* Stash this block's wet output for next block's feedback, before the
     * dry/wet blend overwrites the out buffers. */
    for (std::size_t i = 0; i < m; i++) { fbL[i] = out_l[i]; fbR[i] = out_r[i]; }

    /* Dry/wet blend: dry = live input, wet = stretched output (lagged). */
    const float wet = mix;
    const float dry = 1.0f - wet;
    for (std::size_t i = 0; i < m; i++)
    {
        out_l[i] = wet * out_l[i] + dry * in_l[i];
        out_r[i] = wet * out_r[i] + dry * in_r[i];
    }

    /* Output follower consumes the final (post-mix) mono sum, fed at 2× so its
     * quadratic TKEO envelope sits level with the per-channel input followers. */
    for (std::size_t i = 0; i < m; i++)
        outDet.Analyze(out_l[i] + out_r[i]);

    const float env_l = sparse_l.TkeoEnvelope();
    const float env_r = sparse_r.TkeoEnvelope();
    envNormIn  = EnvNorm((env_l > env_r) ? env_l : env_r);
    envNormOut = EnvNorm(outDet.Envelope());

    /* Input peak meter (decaying abs max). */
    constexpr float kPeakDecay = 0.95f;
    float pk = inPeak * kPeakDecay;
    for (std::size_t i = 0; i < m; i++)
    {
        const float al = std::fabs(in_l[i]);
        const float ar = std::fabs(in_r[i]);
        if (al > pk) pk = al;
        if (ar > pk) pk = ar;
    }
    inPeak = pk;
}

void CapicolaEngine::Impl::SetTkeoCutoff(float fc)
{
    sparse_l.SetTkeoCutoff(fc);
    sparse_r.SetTkeoCutoff(fc);
    outDet.SetCutoff(fc);          // followers share smoothing
}

/* ── CapicolaEngine ─────────────────────────────────────────────────────── */

CapicolaEngine::CapicolaEngine()  = default;
CapicolaEngine::~CapicolaEngine() = default;

void CapicolaEngine::Prepare()
{
    if (!impl) impl.reset(new Impl);
    impl->Init();

    std::memset(fifoInL,  0, sizeof fifoInL);
    std::memset(fifoInR,  0, sizeof fifoInR);
    std::memset(fifoOutL, 0, sizeof fifoOutL);
    std::memset(fifoOutR, 0, sizeof fifoOutR);
    fifoPos = 0;
}

void CapicolaEngine::Process(const float* in_l, const float* in_r,
                             float*       out_l, float*       out_r,
                             std::size_t  n)
{
    for (std::size_t i = 0; i < n; i++)
    {
        /* Read the input before writing the output: the buffers may alias. */
        const float xl = in_l[i];
        const float xr = in_r[i];
        out_l[i] = fifoOutL[fifoPos];
        out_r[i] = fifoOutR[fifoPos];
        fifoInL[fifoPos] = xl;
        fifoInR[fifoPos] = xr;

        if (++fifoPos == kBlock)
        {
            impl->ProcessBlock(fifoInL, fifoInR, fifoOutL, fifoOutR, kBlock);
            fifoPos = 0;
        }
    }
}

void CapicolaEngine::SetSlicePitch(float speed)
{
    impl->sparse_l.SetGrainPitch(speed);
    impl->sparse_r.SetGrainPitch(speed);
}

void CapicolaEngine::SetSliceStretch(float stretch)
{
    impl->sparse_l.SetGrainStretch(stretch);
    impl->sparse_r.SetGrainStretch(stretch);
}

void CapicolaEngine::SetGrainSize(float keyframes)
{
    const int d = (int)(keyframes + 0.5f);
    impl->sparse_l.SetGrainLeash(d);
    impl->sparse_r.SetGrainLeash(d);
}

void CapicolaEngine::SetQuality(float epsilon)
{
    impl->sparse_l.SetThreshold(epsilon);
    impl->sparse_r.SetThreshold(epsilon);
}

void CapicolaEngine::SetFeedback(float amt)
{
    impl->fbAmt = (amt < 0.0f) ? 0.0f : amt;
}

void CapicolaEngine::SetFeedbackCutoff(float fc)
{
    /* Q stays at the gentle 0.01 (see Init), only fc moves. */
    impl->fbSvfL.SetControls(fc, 0.01f);
    impl->fbSvfR.SetControls(fc, 0.01f);
}

void CapicolaEngine::SetMix(float wet01)
{
    impl->mix = Clamp01(wet01);
}

void CapicolaEngine::SetTransientThreshold(float t)
{
    impl->sparse_l.SetTransientThreshold(t);
    impl->sparse_r.SetTransientThreshold(t);
    impl->outDet.SetThreshold(t);  // followers share threshold
}

void CapicolaEngine::SetTkeoCutoff(float fc)
{
    impl->SetTkeoCutoff(fc);
}

void CapicolaEngine::SetTkeoResonance(float q)
{
    /* Uncapped (filter.h maps Q = 0.5 + 4q); large q rings into
     * self-oscillation. Input side only. */
    const float qq = (q < 0.0f) ? 0.0f : q;
    impl->sparse_l.SetTkeoResonance(qq);
    impl->sparse_r.SetTkeoResonance(qq);
}

void CapicolaEngine::SetFade(float samples)
{
    impl->sparse_l.SetGrainFade(samples);
    impl->sparse_r.SetGrainFade(samples);
}

void CapicolaEngine::SetDrive(float d)
{
    impl->sparse_l.SetDistortDrive(d);
    impl->sparse_r.SetDistortDrive(d);
}

void CapicolaEngine::SetDriveCharacter(float c)
{
    impl->sparse_l.SetDistortCharacter(c);
    impl->sparse_r.SetDistortCharacter(c);
}

void CapicolaEngine::TriggerSlice()
{
    if (impl->sparse_l.GetState() == State::LIVE_EFFECT)
    {
        impl->sparse_l.SubmitRequest(Request::SLICE);
        impl->sparse_r.SubmitRequest(Request::SLICE);
    }
}

uint32_t CapicolaEngine::SliceCount()    const { return impl->sliceCount; }
uint32_t CapicolaEngine::OutEventCount() const { return impl->outDet.Count(); }
float    CapicolaEngine::EnvNormIn()     const { return impl->envNormIn; }
float    CapicolaEngine::EnvNormOut()    const { return impl->envNormOut; }
float    CapicolaEngine::InPeak()        const { return impl->inPeak; }

} // namespace granular
