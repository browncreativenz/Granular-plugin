/**
 * @file processor_test.cpp
 * @brief CapicolaProcessor: parameter wiring, modulation, control frames,
 *        other sample rates, block-size independence and reset.
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "CapicolaProcessor.h"
#include "Check.h"
#include "TestSignal.h"

using namespace granular;

namespace {

struct Stereo { std::vector<float> l, r; };

Stereo Signal(int rate, double seconds)
{
    Stereo s;
    s.l.resize((size_t)(seconds * rate));
    s.r.resize(s.l.size());
    TestSignal sig((float)rate);
    for (size_t i = 0; i < s.l.size(); i++) sig.Next(s.l[i], s.r[i]);
    return s;
}

/* Process in place in host blocks of `block` (0 = random sizes). */
void Run(CapicolaProcessor& p, Stereo& s, int block, uint32_t seed = 777u)
{
    uint32_t rng = seed;
    for (size_t pos = 0; pos < s.l.size();)
    {
        size_t len = (size_t)block;
        if (block == 0) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; len = 1 + rng % 1500; }
        if (len > s.l.size() - pos) len = s.l.size() - pos;
        p.Process(&s.l[pos], &s.r[pos], &s.l[pos], &s.r[pos], (int)len);
        pos += len;
    }
}

double Rms(const std::vector<float>& x)
{
    double e = 0;
    for (float v : x) e += (double)v * v;
    return std::sqrt(e / (double)x.size());
}

/* At 48 kHz with factory settings, the processor must equal the engine driven
 * directly with the factory values. Checks the parameter wiring end to end. */
void FactoryMatchesEngine()
{
    Stereo in = Signal(48000, 6.0);

    static CapicolaEngine engine;
    engine.Prepare();
    auto def = [](Param p) { return Info(p).def; };
    engine.SetSlicePitch        (taper::PitchSpeed    (def(Param::Pitch)));
    engine.SetSliceStretch      (taper::StretchAmount (def(Param::Stretch)));
    engine.SetTransientThreshold(taper::ThresholdRatio(def(Param::Threshold)));
    engine.SetGrainSize         (taper::GrainKeyframes(def(Param::Grain)));
    engine.SetQuality           (taper::QualityEpsilon(def(Param::Quality)));
    engine.SetFeedback          (taper::FeedbackAmount(def(Param::Feedback)));
    engine.SetTkeoCutoff        (taper::SmoothingFc   (def(Param::Smoothing)));
    engine.SetFade              (taper::FadeSamples   (def(Param::Fade)));
    engine.SetDrive             (taper::Drive         (def(Param::Drive)));
    engine.SetDriveCharacter    (taper::Character     (def(Param::Character)));
    engine.SetMix               (taper::Mix           (def(Param::Mix)));
    engine.SetFeedbackCutoff    (taper::FbBandFc      (def(Param::FbBand)));
    Stereo direct = in;
    engine.Process(direct.l.data(), direct.r.data(), direct.l.data(), direct.r.data(), direct.l.size());

    CapicolaProcessor p;
    p.Prepare(48000);
    CHECK(p.LatencySamples() == 64, "48 kHz latency %d", p.LatencySamples());
    Stereo viaProc = in;
    Run(p, viaProc, 0);

    CHECK(viaProc.l == direct.l && viaProc.r == direct.r, "processor differs from the engine");
    CHECK(p.SliceCount() == engine.SliceCount() && p.SliceCount() > 0, "slices %u vs %u",
          p.SliceCount(), engine.SliceCount());
    std::printf("  48 kHz factory settings: identical to the engine (%u slices)\n", p.SliceCount());
}

/* Mod In A at full scale with depth −1 pulls Pitch to the bottom stop; depth 0
 * leaves it; the input follower moves Stretch with the drums. */
void Modulation()
{
    CapicolaProcessor p;
    p.Prepare(48000);
    p.SetParam(Param::Source1, (float)ModSource::ModA);
    p.SetParam(Param::Depth1, 0.0f);          // depth −1
    p.SetParam(Param::ModA, 1.0f);            // +1
    p.SetParam(Param::Depth2, 1.0f);          // depth +1 on Stretch, from the input follower
    p.SetParam(Param::Stretch, 0.6f);
    p.SetParam(Param::Source3, (float)ModSource::OutputFollower);
    p.SetParam(Param::Depth3, 0.0f);          // depth −1 on Threshold, from the output follower

    /* Each Process call starts on a frame boundary, so its first sample runs a
     * control frame with the followers as they stand before the call. */
    Stereo s = Signal(48000, 4.0);
    float lo1 = 1.0f, hi1 = 0.0f, lo2 = 1.0f, hi2 = 0.0f;
    const int frame = CapicolaProcessor::kFrameBlocks * CapicolaEngine::kBlock;
    for (size_t pos = 0; pos + (size_t)frame <= s.l.size(); pos += (size_t)frame)
    {
        const float inEnv = p.EnvNormIn(), outEnv = p.EnvNormOut();
        p.Process(&s.l[pos], &s.r[pos], &s.l[pos], &s.r[pos], frame);

        CHECK(p.EffectivePosition(0) == 0.0f, "pitch position %g", p.EffectivePosition(0));
        const float e1 = p.EffectivePosition(1), e2 = p.EffectivePosition(2);
        CHECK(e1 == std::fmin(1.0f, 0.6f + inEnv), "stretch %g, expected 0.6 + input %g", e1, inEnv);
        CHECK(e2 == std::fmax(0.0f, 0.5f - outEnv), "threshold %g, expected 0.5 - output %g", e2, outEnv);
        CHECK(p.EffectivePosition(3) == Info(Param::Grain).def, "grain moved without depth");
        lo1 = std::fmin(lo1, e1); hi1 = std::fmax(hi1, e1);
        lo2 = std::fmin(lo2, e2); hi2 = std::fmax(hi2, e2);
    }
    CHECK(hi1 - lo1 > 0.2f, "input follower barely moved Stretch: %g..%g", lo1, hi1);
    CHECK(hi2 - lo2 > 0.1f, "output follower barely moved Threshold: %g..%g", lo2, hi2);
    std::printf("  modulation: Mod In A pins Pitch; input follower sweeps Stretch %.2f..%.2f;\n"
                "              output follower sweeps Threshold %.2f..%.2f\n", lo1, hi1, lo2, hi2);

    /* Parameters land at control frames, 768 samples apart at 48 kHz. */
    CapicolaProcessor q;
    q.Prepare(48000);
    Stereo t = Signal(48000, 0.1);
    q.Process(t.l.data(), t.r.data(), t.l.data(), t.r.data(), 100);   // frame 0 ran
    q.SetParam(Param::ModA, 1.0f);
    q.SetParam(Param::Source1, (float)ModSource::ModA);
    q.SetParam(Param::Depth1, 0.0f);
    q.Process(t.l.data() + 100, t.r.data() + 100, t.l.data() + 100, t.r.data() + 100, 600);
    CHECK(q.EffectivePosition(0) == 0.75f, "applied before the next frame: %g", q.EffectivePosition(0));
    q.Process(t.l.data() + 700, t.r.data() + 700, t.l.data() + 700, t.r.data() + 700, 100);
    CHECK(q.EffectivePosition(0) == 0.0f, "not applied at the next frame: %g", q.EffectivePosition(0));
}

/* Other host rates: the engine still runs at 48 kHz, so the musical result
 * (slices, level) matches the 48 kHz run of the same material. */
void OtherRates()
{
    CapicolaProcessor ref;
    ref.Prepare(48000);
    Stereo a = Signal(48000, 8.0);
    Run(ref, a, 512);

    for (int rate : {44100, 96000})
    {
        CapicolaProcessor p;
        p.Prepare(rate);
        Stereo b = Signal(rate, 8.0);
        Run(p, b, 512);

        bool finite = true;
        for (size_t i = 0; i < b.l.size(); i++) finite &= std::isfinite(b.l[i]) && std::isfinite(b.r[i]);
        CHECK(finite, "%d Hz: non-finite output", rate);

        const int ds = (int)p.SliceCount() - (int)ref.SliceCount();
        CHECK(std::abs(ds) <= 2, "%d Hz: %u slices vs %u at 48 kHz", rate, p.SliceCount(), ref.SliceCount());
        const double ratio = Rms(b.l) / Rms(a.l);
        CHECK(std::fabs(ratio - 1.0) < 0.05, "%d Hz: level %.3f of the 48 kHz run", rate, ratio);

        /* Block sizes still don't matter. */
        CapicolaProcessor q;
        q.Prepare(rate);
        Stereo c = Signal(rate, 3.0);
        Stereo d = c;
        Run(q, c, 0, 1u);
        q.Reset();
        Run(q, d, 1024);
        const bool same = std::equal(c.l.begin(), c.l.end(), d.l.begin())
                       && std::equal(c.r.begin(), c.r.end(), d.r.begin());
        CHECK(same, "%d Hz: output depends on block size, or Reset() is incomplete", rate);

        std::printf("  %d Hz: %u slices (48 kHz: %u), level %.3f, latency %d samples, "
                    "block-size independent\n",
                    rate, p.SliceCount(), ref.SliceCount(), ratio, p.LatencySamples());
    }
}

} // namespace

int main()
{
    std::printf("processor:\n");
    FactoryMatchesEngine();
    Modulation();
    OtherRates();
    std::printf("processor: %s\n", test::Failures() ? "FAIL" : "ok");
    return test::Failures() ? 1 : 0;
}
