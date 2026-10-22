/**
 * @file CapicolaProcessor.cpp
 * @brief See CapicolaProcessor.h.
 */

#include "CapicolaProcessor.h"

#include <cmath>
#include <limits>

namespace granular {

CapicolaProcessor::CapicolaProcessor()
{
    for (int p = 0; p < kNumParams; p++) values[p] = kParams[p].def;
}

void CapicolaProcessor::Prepare(int hostSampleRate)
{
    bridge.Prepare(hostSampleRate, CapicolaEngine::LatencySamples());
    Reset();
}

void CapicolaProcessor::Reset()
{
    engine.Prepare();
    bridge.Reset();
    /* Push every secondary on the first frame, as the module does at boot. */
    for (float& v : pushed) v = std::numeric_limits<float>::quiet_NaN();
    frameCountdown = 0;
    manualSlices   = 0;
}

void CapicolaProcessor::Process(const float* inL, const float* inR,
                                float* outL, float* outR, int n)
{
    bridge.Process(inL, inR, outL, outR, n,
                   [this](float* l, float* r, int m) { ProcessInner(l, r, (std::size_t)m); });
}

void CapicolaProcessor::ProcessInner(float* l, float* r, std::size_t n)
{
    while (n > 0)
    {
        /* First sample of a new engine block: run a control frame every
         * kFrameBlocks blocks, before the block's audio is processed. */
        const std::size_t toBlock = engine.SamplesToNextBlock();
        if (toBlock == (std::size_t)CapicolaEngine::kBlock)
        {
            if (frameCountdown == 0)
            {
                ControlFrame();
                frameCountdown = kFrameBlocks;
            }
            frameCountdown--;
        }

        const std::size_t s = (n < toBlock) ? n : toBlock;
        engine.Process(l, r, l, r, s);
        l += s;
        r += s;
        n -= s;
    }
}

void CapicolaProcessor::ControlFrame()
{
    /* Modulation, from the followers as of the last engine block. */
    for (int c = 0; c < kNumPrimary; c++)
    {
        long src = std::lround(values[(int)SourceParam(c)]);
        if (src < 0) src = 0; else if (src >= kNumModSources) src = kNumModSources - 1;
        mod.SetDepth(c, taper::Depth(values[(int)DepthParam(c)]));
        mod.SetSource(c, (ModSource)src);
    }
    mod.Update(engine.EnvNormIn(), engine.EnvNormOut(),
               taper::ModIn(values[(int)Param::ModA]),
               taper::ModIn(values[(int)Param::ModB]),
               taper::ModIn(values[(int)Param::ModC]));

    /* Primaries: pushed every frame, modulated. */
    for (int c = 0; c < kNumPrimary; c++) effective[c] = mod.Apply(c, values[c]);
    engine.SetSlicePitch        (taper::PitchSpeed    (effective[0]));
    engine.SetSliceStretch      (taper::StretchAmount (effective[1]));
    engine.SetTransientThreshold(taper::ThresholdRatio(effective[2]));
    engine.SetGrainSize         (taper::GrainKeyframes(effective[3]));
    engine.SetQuality           (taper::QualityEpsilon(effective[4]));
    engine.SetFeedback          (taper::FeedbackAmount(effective[5]));

    /* Secondaries: pushed when they change, as the module does. */
    auto changed = [this](Param p) {
        const float v = values[(int)p];
        if (v == pushed[(int)p]) return false;
        pushed[(int)p] = v;
        return true;
    };
    if (changed(Param::Smoothing)) engine.SetTkeoCutoff    (taper::SmoothingFc(values[(int)Param::Smoothing]));
    if (changed(Param::Fade))      engine.SetFade          (taper::FadeSamples(values[(int)Param::Fade]));
    if (changed(Param::Drive))     engine.SetDrive         (taper::Drive      (values[(int)Param::Drive]));
    if (changed(Param::Character)) engine.SetDriveCharacter(taper::Character  (values[(int)Param::Character]));
    if (changed(Param::Mix))       engine.SetMix           (taper::Mix        (values[(int)Param::Mix]));
    if (changed(Param::FbBand))    engine.SetFeedbackCutoff(taper::FbBandFc   (values[(int)Param::FbBand]));
}

} // namespace granular
