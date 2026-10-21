/**
 * @file TestSignal.h
 * @brief Deterministic stereo test signal: a pad chord, a kick and a noise hat.
 *
 * Extends Capicola's tests/host/blend_render.cpp source to stereo. The right
 * channel's hits are offset so the two channels slice at different times and
 * exercise the engine's stereo re-alignment guardrail.
 */

#pragma once

#include <cmath>
#include <cstdint>

namespace granular {

class TestSignal
{
  public:
    explicit TestSignal(float sampleRate) : fs(sampleRate) {}

    /** Next stereo frame. */
    void Next(float& l, float& r)
    {
        const double t = (double)n++ / fs;
        const float pad = 0.12f * (float)(std::sin(2.0 * M_PI * 220.0 * t)
                                        + std::sin(2.0 * M_PI * 277.2 * t)
                                        + std::sin(2.0 * M_PI * 330.0 * t));
        l = pad + Kick(t, 0.0) + Hat(t, 0.25, rngL);
        r = pad + Kick(t, 0.125) + Hat(t, 0.375, rngR);
    }

  private:
    float    fs;
    uint64_t n    = 0;
    uint32_t rngL = 1u;
    uint32_t rngR = 7u;

    static float Kick(double t, double offset)   // 2 Hz
    {
        const double tk = std::fmod(t + 0.5 - offset, 0.5);
        if (tk >= 0.15) return 0.0f;
        return 0.8f * (float)(std::sin(2.0 * M_PI * 60.0 * tk) * std::exp(-18.0 * tk));
    }

    static float Hat(double t, double offset, uint32_t& r)
    {
        const double th = std::fmod(t + 0.5 - offset, 0.5);
        if (th >= 0.03) return 0.0f;
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        return 0.35f * ((float)(r >> 8) / 8388608.0f - 1.0f) * (float)std::exp(-120.0 * th);
    }
};

} // namespace granular
