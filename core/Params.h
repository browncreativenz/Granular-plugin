/**
 * @file Params.h
 * @brief The plugin's parameters: identity, factory defaults and knob tapers.
 *
 * Every continuous parameter is stored as a 0..1 knob position, as on the
 * module, and mapped to engine units by the same tapers the v1.1.0 firmware
 * uses (src/main.cpp: the VirtualKnob declarations, PitchMag, StretchMag, the
 * threshold piecewise map and SecValue). Choice parameters store an index.
 */

#pragma once

#include <cmath>
#include <cstdint>

namespace granular {

enum class Param : int
{
    /* Primary (the module's performance page). These six take modulation. */
    Pitch, Stretch, Threshold, Grain, Quality, Feedback,
    /* Secondary settings. */
    Smoothing, Fade, Drive, Character, Mix, FbBand,
    /* Modulation depth per primary control. */
    Depth1, Depth2, Depth3, Depth4, Depth5, Depth6,
    /* Modulation source per primary control (choice, see ModSource). */
    Source1, Source2, Source3, Source4, Source5, Source6,
    /* Stand-ins for the module's CV inputs. */
    ModA, ModB, ModC,
    Count
};

constexpr int kNumParams  = (int)Param::Count;
constexpr int kNumPrimary = 6;

/** Number of modulation sources; see ModMatrix.h for their order. */
constexpr int kNumModSources = 5;

struct ParamInfo
{
    const char* id;          // stable identifier, saved with presets
    const char* name;        // display name
    float       def;         // factory default: knob position, or choice index
    int         choices;     // 0 = continuous 0..1, otherwise number of choices
};

inline constexpr ParamInfo kParams[kNumParams] = {
    {"pitch",     "Pitch",          0.75f,   0},   // +1x
    {"stretch",   "Stretch",        0.9f,    0},   // about 1.75x slower
    {"threshold", "Threshold",      0.5f,    0},
    {"grain",     "Grain Size",     0.15f,   0},   // 642 keyframes
    {"quality",   "Quality",        1.0f,    0},   // best
    {"feedback",  "Feedback",       0.0f,    0},

    {"smoothing", "Env Smoothing",  0.4259f, 0},   // fc 0.0014
    {"fade",      "Fade",           0.2153f, 0},   // 20 ms
    {"drive",     "Drive",          0.1429f, 0},   // 1.0
    {"character", "Character",      1.0f,    0},   // sinc
    {"mix",       "Mix",            1.0f,    0},   // full wet
    {"fbBand",    "Feedback Band",  0.3769f, 0},   // fc 0.02

    {"depth1",    "Pitch Depth",     0.5f,   0},   // noon = no modulation
    {"depth2",    "Stretch Depth",   0.5f,   0},
    {"depth3",    "Threshold Depth", 0.5f,   0},
    {"depth4",    "Grain Depth",     0.5f,   0},
    {"depth5",    "Quality Depth",   0.5f,   0},
    {"depth6",    "Feedback Depth",  0.5f,   0},

    {"src1",      "Pitch Source",     0.0f, kNumModSources},   // input follower
    {"src2",      "Stretch Source",   0.0f, kNumModSources},
    {"src3",      "Threshold Source", 0.0f, kNumModSources},
    {"src4",      "Grain Source",     0.0f, kNumModSources},
    {"src5",      "Quality Source",   0.0f, kNumModSources},
    {"src6",      "Feedback Source",  0.0f, kNumModSources},

    {"modA",      "Mod In A",       0.5f,    0},   // noon = 0
    {"modB",      "Mod In B",       0.5f,    0},
    {"modC",      "Mod In C",       0.5f,    0},
};

inline constexpr const char* kModSourceNames[kNumModSources] = {
    "Input follower", "Output follower", "Mod In A", "Mod In B", "Mod In C",
};

inline const ParamInfo& Info(Param p) { return kParams[(int)p]; }

/** Depth and source parameters for primary control i (0..5). */
inline Param DepthParam(int i)  { return (Param)((int)Param::Depth1 + i); }
inline Param SourceParam(int i) { return (Param)((int)Param::Source1 + i); }

/* ── Tapers: knob position (0..1) → engine units ─────────────────────────── */
namespace taper {

inline float Clamp01(float n) { return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n); }

/** Pitch: playback speed, linear ±2x. Noon stops the head, below noon plays
 *  backward, and a ±0.03 detent holds each unity (n = 0.25 and 0.75). */
inline float PitchSpeed(float n)
{
    constexpr float kMax = 2.0f, kDetent = 0.03f;
    n = Clamp01(n);
    float m = std::fabs(2.0f * n - 1.0f) * kMax;
    if (std::fabs(m - 1.0f) < kDetent) m = 1.0f;
    return (n < 0.5f) ? -m : m;
}

/** Stretch: bipolar, |2n − 1|^2.5 on each side. A true freeze at noon,
 *  realtime at the stops, reverse below noon. */
inline float StretchAmount(float n)
{
    n = Clamp01(n);
    const float m = std::pow(std::fabs(2.0f * n - 1.0f), 2.5f);
    return (n < 0.5f) ? -m : m;
}

/** Threshold: keep ratio over the envelope average. 0..90% of the sweep is
 *  0..4, the last 10% climbs 4..8, and the very top mutes auto-slicing. */
inline float ThresholdRatio(float n)
{
    n = Clamp01(n);
    if (n > 0.99f) return 1.0e9f;
    return (n <= 0.9f) ? (n * (4.0f / 0.9f)) : (4.0f + (n - 0.9f) * 40.0f);
}

inline float GrainKeyframes(float n) { return 32.0f + Clamp01(n) * (4096.0f - 32.0f); }

/** Quality: analyzer ε, 0.08 (crunchy) at 0 to 0.001 (best) at 1. */
inline float QualityEpsilon(float n) { return 0.08f + Clamp01(n) * (0.001f - 0.08f); }

inline float FeedbackAmount(float n) { return Clamp01(n) * 1.5f; }

/** Follower smoothing: normalised cutoff, exponential 5e-5..0.125. */
inline float SmoothingFc(float n)    { return 5.0e-5f * std::pow(0.125f / 5.0e-5f, Clamp01(n)); }

/** Fade: crossfade in 48 kHz samples, exponential 10..250 ms. */
inline float FadeSamples(float n)    { return 480.0f * std::pow(12000.0f / 480.0f, Clamp01(n)); }

inline float Drive(float n)          { return 0.5f + 3.5f * Clamp01(n); }
inline float Character(float n)      { return Clamp01(n); }
inline float Mix(float n)            { return Clamp01(n); }

/** Feedback band: normalised bandpass centre, exponential 2e-3..0.9. */
inline float FbBandFc(float n)       { return 2.0e-3f * std::pow(0.9f / 2.0e-3f, Clamp01(n)); }

/** Modulation depth, −1..+1 with zero at noon. */
inline float Depth(float n)          { return -1.0f + 2.0f * Clamp01(n); }

/** Mod In A–C, bipolar −1..+1 with zero at noon, as the module reads CV. */
inline float ModIn(float n)          { return (Clamp01(n) - 0.5f) * 2.0f; }

} // namespace taper
} // namespace granular
