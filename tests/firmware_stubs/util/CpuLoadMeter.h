/**
 * @file util/CpuLoadMeter.h (host stub)
 * @brief No-op stand-in for libDaisy's CpuLoadMeter; see ../daisy_seed.h.
 */

#pragma once

namespace daisy {

class CpuLoadMeter
{
  public:
    void  Init(float, int, float = 1.0f) {}
    void  OnBlockStart() {}
    void  OnBlockEnd() {}
    float GetAvgCpuLoad() const { return 0.0f; }
    float GetMaxCpuLoad() const { return 0.0f; }
};

} // namespace daisy
