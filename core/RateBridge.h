/**
 * @file RateBridge.h
 * @brief Runs a fixed-rate stereo process inside a host running at any rate.
 *
 * Capicola's engine is a 48 kHz design: its timing constants, filters and
 * Teager-energy envelopes all assume 48 kHz. Rather than re-tune it per rate,
 * the bridge resamples the host's audio to 48 kHz, runs the inner process,
 * and resamples back, so the engine behaves exactly as the hardware does at
 * any project rate. At 48 kHz the bridge is a pass-through.
 *
 * Output does not depend on how the host splits its blocks. The bridge adds a
 * fixed latency, LatencySamples(), which includes the inner process's own.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Resampler.h"

namespace granular {

class RateBridge
{
  public:
    static constexpr int kInnerRate = 48000;

    /** innerLatency is the inner process's delay in inner-rate samples.
     *  Allocates; not real-time safe. */
    void Prepare(int hostRate, int innerLatency)
    {
        bypass = (hostRate == kInnerRate);
        if (bypass)
        {
            latency = innerLatency;
            return;
        }

        for (int c = 0; c < 2; c++)
        {
            up[c].Prepare(hostRate, kInnerRate);
            down[c].Prepare(kInnerRate, hostRate);
        }

        const int innerMax = up[0].MaxOutput(kChunk);
        innerL.assign((size_t)innerMax, 0.0f);
        innerR.assign((size_t)innerMax, 0.0f);

        const int hostMax = down[0].MaxOutput(innerMax);
        hostL.assign((size_t)hostMax, 0.0f);
        hostR.assign((size_t)hostMax, 0.0f);

        /* Both converters emit an output as soon as the newest input it is
         * anchored to arrives, so after T host inputs the down converter has
         * produced at least T host samples (ceil(ceil(T·L/M)·M/L) >= T): the
         * queue never runs dry and needs no priming. It can run a few samples
         * ahead, so it holds at most one chunk's output plus that slack. */
        const size_t cap = (size_t)(hostMax + 8);
        queueL.assign(cap, 0.0f);
        queueR.assign(cap, 0.0f);

        const double seconds = up[0].GroupDelaySeconds()
                             + (double)innerLatency / kInnerRate
                             + down[0].GroupDelaySeconds();
        latency = (int)std::lround(seconds * hostRate);
        Reset();
    }

    /** Clear all state; the output restarts with LatencySamples() of silence. */
    void Reset()
    {
        if (bypass) return;
        for (int c = 0; c < 2; c++) { up[c].Reset(); down[c].Reset(); }
        std::fill(queueL.begin(), queueL.end(), 0.0f);
        std::fill(queueR.begin(), queueR.end(), 0.0f);
        head   = 0;
        count  = 0;
        faults = 0;
    }

    /** Total delay through the bridge and the inner process, host samples. */
    int LatencySamples() const { return latency; }

    /** Output-queue underflows or overflows since Prepare; always 0 unless the
     *  queue analysis in Prepare is wrong. For tests. */
    uint64_t QueueFaults() const { return faults; }

    /**
     * Process n host samples (in and out may alias). `inner(l, r, count)`
     * processes inner-rate audio in place.
     */
    template <class Inner>
    void Process(const float* inL, const float* inR, float* outL, float* outR,
                 int n, Inner&& inner)
    {
        if (bypass)
        {
            if (inL != outL) std::copy(inL, inL + n, outL);
            if (inR != outR) std::copy(inR, inR + n, outR);
            inner(outL, outR, n);
            return;
        }

        const size_t cap = queueL.size();
        for (int pos = 0; pos < n; pos += kChunk)
        {
            const int c = std::min(kChunk, n - pos);

            /* Up to the inner rate, process, back down into the output queue. */
            const int m = up[0].Process(inL + pos, c, innerL.data());
            up[1].Process(inR + pos, c, innerR.data());
            inner(innerL.data(), innerR.data(), m);

            const int k = down[0].Process(innerL.data(), m, hostL.data());
            down[1].Process(innerR.data(), m, hostR.data());
            for (int i = 0; i < k; i++)
            {
                if (count == cap) { faults++; break; }
                const size_t slot = (head + count) % cap;
                queueL[slot] = hostL[(size_t)i];
                queueR[slot] = hostR[(size_t)i];
                count++;
            }

            /* Pop this chunk's outputs. The queue cannot run dry or overflow
             * (see Prepare); the guards count it if that analysis were wrong. */
            for (int i = 0; i < c; i++)
            {
                float l = 0.0f, r = 0.0f;
                if (count > 0)
                {
                    l = queueL[head];
                    r = queueR[head];
                    head = (head + 1) % cap;
                    count--;
                }
                else faults++;
                outL[pos + i] = l;
                outR[pos + i] = r;
            }
        }
    }

  private:
    static constexpr int kChunk = 256;   // host samples per internal pass

    bool bypass  = true;
    int  latency = 0;

    RationalResampler up[2], down[2];
    std::vector<float> innerL, innerR, hostL, hostR;
    std::vector<float> queueL, queueR;
    size_t head = 0, count = 0;
    uint64_t faults = 0;
};

} // namespace granular
