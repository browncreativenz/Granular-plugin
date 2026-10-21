/**
 * @file ParamText.h
 * @brief What a knob position means, as short display text: "+1.00x",
 *        "1.75x slower", "20.0 ms". Used by the plugin's parameter readouts
 *        and the renderer's --list.
 */

#pragma once

#include <cmath>
#include <cstdio>
#include <string>

#include "Params.h"

namespace granular {

/** Normalised filter cutoffs (filter.h: g = tan(π/2 · fc)) are fractions of
 *  Nyquist at the engine's 48 kHz. */
inline float CutoffHz(float fc) { return fc * 24000.0f; }

inline std::string ParamText(Param p, float v)
{
    char b[48];
    switch (p)
    {
        case Param::Pitch:
            std::snprintf(b, sizeof b, "%+.2fx", taper::PitchSpeed(v));
            break;
        case Param::Stretch:
        {
            const float s = taper::StretchAmount(v);
            if (std::fabs(s) < 1e-12f) std::snprintf(b, sizeof b, "Freeze");
            else           std::snprintf(b, sizeof b, "%.2fx slower%s", 1.0f / std::fabs(s), s < 0 ? ", rev" : "");
            break;
        }
        case Param::Threshold:
        {
            const float r = taper::ThresholdRatio(v);
            if (r >= 100.0f) std::snprintf(b, sizeof b, "Auto off");
            else             std::snprintf(b, sizeof b, "%.2f", r);
            break;
        }
        case Param::Grain:     std::snprintf(b, sizeof b, "%.0f kf", taper::GrainKeyframes(v)); break;
        case Param::Quality:   std::snprintf(b, sizeof b, "eps %.4f", taper::QualityEpsilon(v)); break;
        case Param::Feedback:  std::snprintf(b, sizeof b, "%.2f", taper::FeedbackAmount(v)); break;
        case Param::Smoothing: std::snprintf(b, sizeof b, "%.1f Hz", CutoffHz(taper::SmoothingFc(v))); break;
        case Param::Fade:      std::snprintf(b, sizeof b, "%.1f ms", taper::FadeSamples(v) / 48.0f); break;
        case Param::Drive:     std::snprintf(b, sizeof b, "%.2f", taper::Drive(v)); break;
        case Param::Character:
        {
            const float c = taper::Character(v);
            if (std::fabs(c - 0.5f) < 1e-6f) std::snprintf(b, sizeof b, "Clean");
            else if (c < 0.5f) std::snprintf(b, sizeof b, "Quake %.0f%%", 100.0f * (1.0f - 2.0f * c));
            else               std::snprintf(b, sizeof b, "Sinc %.0f%%", 100.0f * (2.0f * c - 1.0f));
            break;
        }
        case Param::Mix:       std::snprintf(b, sizeof b, "%.0f%% wet", 100.0f * taper::Mix(v)); break;
        case Param::FbBand:    std::snprintf(b, sizeof b, "%.0f Hz", CutoffHz(taper::FbBandFc(v))); break;
        case Param::Depth1: case Param::Depth2: case Param::Depth3:
        case Param::Depth4: case Param::Depth5: case Param::Depth6:
            std::snprintf(b, sizeof b, "%+.2f", taper::Depth(v));
            break;
        case Param::Source1: case Param::Source2: case Param::Source3:
        case Param::Source4: case Param::Source5: case Param::Source6:
        {
            long i = std::lround(v);
            if (i < 0) i = 0; else if (i >= kNumModSources) i = kNumModSources - 1;
            std::snprintf(b, sizeof b, "%s", kModSourceNames[i]);
            break;
        }
        case Param::ModA: case Param::ModB: case Param::ModC:
            std::snprintf(b, sizeof b, "%+.2f", taper::ModIn(v));
            break;
        case Param::Count:
            b[0] = '\0';
            break;
    }
    return b;
}

} // namespace granular
