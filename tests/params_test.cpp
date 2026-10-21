/**
 * @file params_test.cpp
 * @brief Knob tapers and factory defaults against the v1.1.0 firmware.
 *
 * Expected values come from the firmware's src/main.cpp: its knob ranges,
 * PitchMag / StretchMag, the threshold piecewise map, SecValue, and the
 * factory defaults kPageDefaults / kSecDefault with the engine values they
 * are documented to produce.
 */

#include <cmath>
#include <cstring>

#include "Check.h"
#include "ModMatrix.h"
#include "Params.h"

using namespace granular;

static bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

static void Pitch()
{
    using taper::PitchSpeed;
    CHECK(PitchSpeed(0.0f)  == -2.0f, "%g", PitchSpeed(0.0f));
    CHECK(PitchSpeed(0.25f) == -1.0f, "%g", PitchSpeed(0.25f));
    CHECK(PitchSpeed(0.5f)  ==  0.0f, "%g", PitchSpeed(0.5f));
    CHECK(PitchSpeed(0.75f) ==  1.0f, "%g", PitchSpeed(0.75f));
    CHECK(PitchSpeed(1.0f)  ==  2.0f, "%g", PitchSpeed(1.0f));
    /* ±0.03x detent around each unity: 0.0075 of knob travel either side. */
    CHECK(PitchSpeed(0.757f) == 1.0f, "%g", PitchSpeed(0.757f));
    CHECK(PitchSpeed(0.743f) == 1.0f, "%g", PitchSpeed(0.743f));
    CHECK(PitchSpeed(0.76f)  >  1.0f, "%g", PitchSpeed(0.76f));
    CHECK(PitchSpeed(0.243f) == -1.0f, "%g", PitchSpeed(0.243f));
    CHECK(Near(PitchSpeed(0.6f), 0.4, 1e-6), "%g", PitchSpeed(0.6f));
    CHECK(Near(PitchSpeed(0.4f), -0.4, 1e-6), "%g", PitchSpeed(0.4f));
}

static void Stretch()
{
    using taper::StretchAmount;
    CHECK(StretchAmount(0.5f) == 0.0f, "freeze at noon: %g", StretchAmount(0.5f));
    CHECK(StretchAmount(1.0f) == 1.0f, "%g", StretchAmount(1.0f));
    CHECK(StretchAmount(0.0f) == -1.0f, "%g", StretchAmount(0.0f));
    /* "n = 0.75 still lands ~5.7x" slower: 0.5^2.5. */
    CHECK(Near(1.0 / StretchAmount(0.75f), 5.657, 0.01), "%g", 1.0 / StretchAmount(0.75f));
    /* Factory 0.9: "≈ 1.75x slower". */
    CHECK(Near(1.0 / StretchAmount(0.9f), 1.747, 0.01), "%g", 1.0 / StretchAmount(0.9f));
    CHECK(Near(StretchAmount(0.25f), -StretchAmount(0.75f), 1e-7), "symmetric");
}

static void Threshold()
{
    using taper::ThresholdRatio;
    CHECK(ThresholdRatio(0.0f) == 0.0f, "%g", ThresholdRatio(0.0f));
    CHECK(Near(ThresholdRatio(0.9f), 4.0, 1e-5), "90%% = 4: %g", ThresholdRatio(0.9f));
    CHECK(Near(ThresholdRatio(0.45f), 2.0, 1e-5), "%g", ThresholdRatio(0.45f));
    CHECK(Near(ThresholdRatio(0.95f), 6.0, 1e-4), "%g", ThresholdRatio(0.95f));
    CHECK(Near(ThresholdRatio(0.99f), 7.6, 1e-4), "%g", ThresholdRatio(0.99f));
    CHECK(ThresholdRatio(0.995f) >= 100.0f, "top mutes: %g", ThresholdRatio(0.995f));
    CHECK(ThresholdRatio(1.0f)   >= 100.0f, "top mutes: %g", ThresholdRatio(1.0f));
}

static void Linear()
{
    CHECK(taper::GrainKeyframes(0.0f) == 32.0f,   "%g", taper::GrainKeyframes(0.0f));
    CHECK(taper::GrainKeyframes(1.0f) == 4096.0f, "%g", taper::GrainKeyframes(1.0f));
    CHECK(Near(taper::QualityEpsilon(0.0f), 0.08, 1e-7),  "%g", taper::QualityEpsilon(0.0f));
    CHECK(Near(taper::QualityEpsilon(1.0f), 0.001, 1e-7), "%g", taper::QualityEpsilon(1.0f));
    CHECK(taper::FeedbackAmount(1.0f) == 1.5f, "%g", taper::FeedbackAmount(1.0f));
    CHECK(taper::Depth(0.5f) == 0.0f && taper::Depth(0.0f) == -1.0f && taper::Depth(1.0f) == 1.0f, "depth");
    CHECK(taper::ModIn(0.5f) == 0.0f && taper::ModIn(0.0f) == -1.0f && taper::ModIn(1.0f) == 1.0f, "mod in");
}

static void Exponential()
{
    CHECK(Near(taper::SmoothingFc(0.0f), 5e-5, 1e-9),   "%g", taper::SmoothingFc(0.0f));
    CHECK(Near(taper::SmoothingFc(1.0f), 0.125, 1e-6),  "%g", taper::SmoothingFc(1.0f));
    CHECK(Near(taper::FadeSamples(0.0f), 480.0, 1e-3),  "10 ms: %g", taper::FadeSamples(0.0f));
    CHECK(Near(taper::FadeSamples(1.0f), 12000.0, 0.1), "250 ms: %g", taper::FadeSamples(1.0f));
    CHECK(Near(taper::FbBandFc(0.0f), 2e-3, 1e-8),      "%g", taper::FbBandFc(0.0f));
    CHECK(Near(taper::FbBandFc(1.0f), 0.9, 1e-5),       "%g", taper::FbBandFc(1.0f));
}

/* The defaults must reproduce the engine values the firmware documents. */
static void Defaults()
{
    auto def = [](Param p) { return Info(p).def; };
    CHECK(taper::PitchSpeed(def(Param::Pitch)) == 1.0f, "pitch unity");
    CHECK(Near(taper::GrainKeyframes(def(Param::Grain)), 642.0, 0.5), "grain %g",
          taper::GrainKeyframes(def(Param::Grain)));
    CHECK(Near(taper::QualityEpsilon(def(Param::Quality)), 0.001, 1e-7), "quality max");
    CHECK(taper::FeedbackAmount(def(Param::Feedback)) == 0.0f, "feedback off");
    CHECK(Near(taper::SmoothingFc(def(Param::Smoothing)), 0.0014, 0.0014 * 0.005), "smoothing %g",
          taper::SmoothingFc(def(Param::Smoothing)));
    CHECK(Near(taper::FadeSamples(def(Param::Fade)), 960.0, 1.0), "fade %g",
          taper::FadeSamples(def(Param::Fade)));
    CHECK(Near(taper::Drive(def(Param::Drive)), 1.0, 0.001), "drive %g", taper::Drive(def(Param::Drive)));
    CHECK(taper::Character(def(Param::Character)) == 1.0f, "character sinc");
    CHECK(taper::Mix(def(Param::Mix)) == 1.0f, "mix wet");
    CHECK(Near(taper::FbBandFc(def(Param::FbBand)), 0.02, 0.02 * 0.005), "fb band %g",
          taper::FbBandFc(def(Param::FbBand)));
    for (int i = 0; i < kNumPrimary; i++)
    {
        CHECK(taper::Depth(Info(DepthParam(i)).def) == 0.0f, "depth %d off", i + 1);
        CHECK(Info(SourceParam(i)).def == 0.0f, "source %d = input follower", i + 1);
    }

    /* Identifiers are unique: they key saved presets. */
    for (int a = 0; a < kNumParams; a++)
        for (int b = a + 1; b < kNumParams; b++)
            CHECK(std::strcmp(kParams[a].id, kParams[b].id) != 0, "duplicate id %s", kParams[a].id);
}

static void Modulation()
{
    ModMatrix m;
    m.SetDepth(0, 0.5f);
    m.SetSource(0, ModSource::InputFollower);
    m.SetDepth(1, -1.0f);
    m.SetSource(1, ModSource::ModA);
    m.Update(0.4f, 0.9f, 0.3f, 0.0f, 0.0f);
    CHECK(Near(m.Apply(0, 0.5f), 0.7, 1e-6), "%g", m.Apply(0, 0.5f));
    CHECK(Near(m.Apply(1, 0.5f), 0.2, 1e-6), "%g", m.Apply(1, 0.5f));
    CHECK(m.Apply(0, 0.95f) == 1.0f, "clamps high");
    CHECK(m.Apply(1, 0.1f) == 0.0f, "clamps low");
    CHECK(m.Apply(2, 0.33f) == 0.33f, "zero depth leaves the knob alone");
    CHECK(m.Signal(ModSource::OutputFollower) == 0.9f, "signal");
}

int main()
{
    Pitch();
    Stretch();
    Threshold();
    Linear();
    Exponential();
    Defaults();
    Modulation();
    std::printf("params: %s\n", test::Failures() ? "FAIL" : "ok");
    return test::Failures() ? 1 : 0;
}
