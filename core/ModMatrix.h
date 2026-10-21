/**
 * @file ModMatrix.h
 * @brief Per-control modulation routing, as the firmware's ModRouter.
 *
 * Each primary control takes one source and a bipolar depth. Its modulated
 * knob position is clamp01(stored + depth × source), and the knob's taper is
 * applied after that, exactly as the module adds CV to a knob (alchemy-sdk
 * VirtualKnob::Norm).
 */

#pragma once

#include <cstdint>

#include "Params.h"

namespace granular {

enum class ModSource : uint8_t
{
    InputFollower,    // 0..1
    OutputFollower,   // 0..1
    ModA,             // −1..+1
    ModB,
    ModC,
};

class ModMatrix
{
  public:
    void SetDepth(int control, float depth)       { depth_[control] = depth; }
    void SetSource(int control, ModSource source) { source_[control] = source; }

    /** Refresh the source signals and every control's offset. */
    void Update(float inEnv, float outEnv, float modA, float modB, float modC)
    {
        signal_[0] = inEnv;
        signal_[1] = outEnv;
        signal_[2] = modA;
        signal_[3] = modB;
        signal_[4] = modC;
        for (int c = 0; c < kNumPrimary; c++)
            delta_[c] = depth_[c] * signal_[(int)source_[c]];
    }

    /** Offset to add to control c's knob position. */
    float Delta(int control) const { return delta_[control]; }

    /** Current value of a source, for meters. */
    float Signal(ModSource s) const { return signal_[(int)s]; }

    /** clamp01(stored + offset): control c's modulated knob position. */
    float Apply(int control, float stored) const
    {
        const float v = stored + delta_[control];
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }

  private:
    float     depth_ [kNumPrimary]    = {};
    ModSource source_[kNumPrimary]    = {};   // input follower
    float     signal_[kNumModSources] = {};
    float     delta_ [kNumPrimary]    = {};
};

} // namespace granular
