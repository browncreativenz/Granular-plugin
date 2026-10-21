/**
 * @file Resampler.h
 * @brief Streaming rational sample-rate converter (polyphase windowed sinc).
 *
 * Converts between integer rates whose ratio reduces to L/M (44100 → 48000 is
 * 160/147). The prototype low-pass is a Kaiser-windowed sinc designed at the
 * L × input rate, with its transition band just under the lower of the two
 * Nyquist frequencies:
 *
 *   passband to 0.9 × Nyquist(lower rate), stopband from Nyquist, ~100 dB.
 *
 * The filter is linear phase and causal, so the converter adds a fixed group
 * delay, GroupDelaySeconds(). Output for a given input stream does not depend
 * on how the input is split into calls.
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

namespace granular {

class RationalResampler
{
  public:
    /** Design the filter and size the history. Allocates; not real-time safe. */
    void Prepare(int fromRate, int toRate)
    {
        const int g = std::gcd(fromRate, toRate);
        L      = toRate / g;
        M      = fromRate / g;
        inRate = fromRate;

        constexpr double kStopDb  = 100.0;
        constexpr double kPassEnd = 0.9;   // passband edge, fraction of the lower Nyquist
        const double nyqMin  = 0.5 * (fromRate < toRate ? fromRate : toRate);
        const double proto   = (double)fromRate * L;            // prototype rate, Hz
        const double fPass   = kPassEnd * nyqMin;
        const double fStop   = nyqMin;
        const double fCut    = 0.5 * (fPass + fStop) / proto;   // cycles per proto sample
        const double dOmega  = 2.0 * M_PI * (fStop - fPass) / proto;
        const double beta    = 0.1102 * (kStopDb - 8.7);
        const int    nMin    = (int)std::ceil((kStopDb - 7.95) / (2.285 * dOmega)) + 1;

        K = (nMin + L - 1) / L;    // taps per phase
        const int n = K * L;       // prototype length

        std::vector<double> h((size_t)n);
        double sum = 0.0;
        const double centre = 0.5 * (n - 1);
        for (int j = 0; j < n; j++)
        {
            const double x    = j - centre;
            const double sinc = (x == 0.0) ? 2.0 * fCut
                                           : std::sin(2.0 * M_PI * fCut * x) / (M_PI * x);
            const double r    = 2.0 * j / (n - 1) - 1.0;
            const double w    = BesselI0(beta * std::sqrt(1.0 - r * r)) / BesselI0(beta);
            h[(size_t)j] = sinc * w;
            sum += h[(size_t)j];
        }

        /* Each phase sees one input in L, so the prototype sums to L for unity
         * gain. Phase p, tap k is prototype tap p + k·L. */
        phases.assign((size_t)n, 0.0f);
        for (int p = 0; p < L; p++)
            for (int k = 0; k < K; k++)
                phases[(size_t)(p * K + k)] = (float)(h[(size_t)(p + k * L)] * L / sum);

        groupDelaySec = centre / proto;
        history.assign((size_t)(2 * K), 0.0f);
        Reset();
    }

    /** Clear the history; the next input is treated as the first. */
    void Reset()
    {
        std::fill(history.begin(), history.end(), 0.0f);
        w     = 0;
        phase = 0;
        lag   = 0;
    }

    /** Most outputs Process() can write for n inputs. */
    int MaxOutput(int n) const { return (int)(((int64_t)n * L + M - 1) / M) + 1; }

    /** Consume n inputs, write the outputs they complete; returns the count. */
    int Process(const float* in, int n, float* out)
    {
        int produced = 0;
        for (int i = 0; i < n; i++)
        {
            w = (w == 0) ? K - 1 : w - 1;
            history[(size_t)w] = history[(size_t)(w + K)] = in[i];

            /* lag = how many inputs past the newest the next output's anchor
             * lies; it is due once the anchor is the input just pushed. */
            while (lag == 0)
            {
                const float* c = &phases[(size_t)(phase * K)];
                const float* x = &history[(size_t)w];
                float acc = 0.0f;
                for (int k = 0; k < K; k++) acc += c[k] * x[k];
                out[produced++] = acc;

                phase += M;
                lag   += phase / L;
                phase %= L;
            }
            lag--;
        }
        return produced;
    }

    /** Delay through the filter, in seconds. */
    double GroupDelaySeconds() const { return groupDelaySec; }

    int UpFactor()   const { return L; }
    int DownFactor() const { return M; }
    int InputRate()  const { return inRate; }

  private:
    int L = 1, M = 1, K = 1, inRate = 48000;
    std::vector<float> phases;    // L phases × K taps
    std::vector<float> history;   // last K inputs, stored twice for contiguous reads
    int    w     = 0;             // newest input's slot in history
    int    phase = 0;             // next output's phase, 0..L-1
    int    lag   = 0;             // inputs still to arrive before the next output is due
    double groupDelaySec = 0.0;

    static double BesselI0(double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 64; k++)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum  += term;
            if (term < sum * 1e-17) break;
        }
        return sum;
    }
};

} // namespace granular
