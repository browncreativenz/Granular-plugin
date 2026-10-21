/**
 * @file resampler_test.cpp
 * @brief RateBridge at the host rates Live offers: latency, passband
 *        accuracy, distortion, alias rejection, block-size independence.
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "Check.h"
#include "RateBridge.h"

using namespace granular;

namespace {

constexpr int kInnerDelay = 64;   // stands in for the engine's block FIFO

/* Inner process: a plain 64-sample delay at 48 kHz, like the engine's FIFO. */
struct Delay64
{
    float bufL[kInnerDelay] = {}, bufR[kInnerDelay] = {};
    int   pos = 0;
    void operator()(float* l, float* r, int n)
    {
        for (int i = 0; i < n; i++)
        {
            const float xl = l[i], xr = r[i];
            l[i] = bufL[pos];
            r[i] = bufR[pos];
            bufL[pos] = xl;
            bufR[pos] = xr;
            pos = (pos + 1) % kInnerDelay;
        }
    }
};

/* Run a mono signal through a fresh bridge in blocks of `block` samples (0 =
 * random sizes); both channels carry the same signal. */
template <class Inner>
std::vector<float> Run(int rate, const std::vector<float>& x, int block, Inner inner,
                       int* latency = nullptr)
{
    RateBridge bridge;
    bridge.Prepare(rate, kInnerDelay);
    if (latency) *latency = bridge.LatencySamples();

    std::vector<float> l(x), r(x);
    uint32_t rng = 12345u;
    for (size_t pos = 0; pos < x.size();)
    {
        size_t len = (size_t)block;
        if (block == 0) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; len = 1 + rng % 1500; }
        if (len > x.size() - pos) len = x.size() - pos;
        bridge.Process(&l[pos], &r[pos], &l[pos], &r[pos], (int)len, inner);
        pos += len;
    }
    CHECK(bridge.QueueFaults() == 0, "%d Hz: %llu output queue faults", rate,
          (unsigned long long)bridge.QueueFaults());
    for (size_t i = 0; i < x.size(); i++)
        if (l[i] != r[i]) { CHECK(false, "channels differ at %zu", i); break; }
    return l;
}

/* Least-squares fit of a sine at freq (plus DC) over y[from..]: returns the
 * fitted amplitude and the residual RMS. */
void FitSine(const std::vector<float>& y, size_t from, double freq, int rate,
             double& amp, double& resid)
{
    double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0, s1 = 0, c1 = 0, y1 = 0;
    const size_t n = y.size() - from;
    for (size_t i = from; i < y.size(); i++)
    {
        const double w = 2.0 * M_PI * freq * (double)i / rate;
        const double s = std::sin(w), c = std::cos(w);
        ss += s * s; cc += c * c; sc += s * c;
        ys += y[i] * s; yc += y[i] * c;
        s1 += s; c1 += c; y1 += y[i];
    }
    /* Solve the 3x3 normal equations for a·sin + b·cos + d. */
    const double A[3][3] = {{ss, sc, s1}, {sc, cc, c1}, {s1, c1, (double)n}};
    const double B[3]    = {ys, yc, y1};
    auto det = [](const double m[3][3]) {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
             - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
             + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    };
    double coef[3];
    const double D = det(A);
    for (int k = 0; k < 3; k++)
    {
        double M[3][3];
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) M[i][j] = (j == k) ? B[i] : A[i][j];
        coef[k] = det(M) / D;
    }
    amp = std::hypot(coef[0], coef[1]);
    double e = 0;
    for (size_t i = from; i < y.size(); i++)
    {
        const double w = 2.0 * M_PI * freq * (double)i / rate;
        const double d = y[i] - (coef[0] * std::sin(w) + coef[1] * std::cos(w) + coef[2]);
        e += d * d;
    }
    resid = std::sqrt(e / (double)n);
}

double Rms(const std::vector<float>& y, size_t from)
{
    double e = 0;
    for (size_t i = from; i < y.size(); i++) e += (double)y[i] * y[i];
    return std::sqrt(e / (double)(y.size() - from));
}

std::vector<float> Sine(int rate, double freq, double seconds, float amp = 0.5f)
{
    std::vector<float> x((size_t)(seconds * rate));
    for (size_t i = 0; i < x.size(); i++) x[i] = amp * (float)std::sin(2.0 * M_PI * freq * (double)i / rate);
    return x;
}

void Rate(int rate)
{
    const double nyqMin = 0.5 * (rate < 48000 ? rate : 48000);

    /* Latency: cross-correlate white noise in against out. */
    {
        std::vector<float> x((size_t)rate);
        uint32_t s = 99u;
        for (float& v : x) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; v = (float)s / 4294967296.0f - 0.5f; }
        int lat = 0;
        const std::vector<float> y = Run(rate, x, 512, Delay64{}, &lat);
        auto xc = [&](int lag) {
            double a = 0;
            for (size_t i = (size_t)lag; i < x.size(); i++) a += (double)x[i - (size_t)lag] * y[i];
            return a;
        };
        int best = 0;
        double bestV = -1e300;
        for (int lag = 0; lag < lat + 64; lag++)
        {
            const double v = xc(lag);
            if (v > bestV) { bestV = v; best = lag; }
        }
        const double a = xc(best - 1), b = bestV, c = xc(best + 1);
        const double peak = best + 0.5 * (a - c) / (a - 2 * b + c);
        CHECK(std::fabs(peak - lat) <= 0.5, "%d Hz: reported latency %d, measured %.2f", rate, lat, peak);
        std::printf("  %6d Hz: latency %d samples (%.2f ms), measured %.2f\n",
                    rate, lat, 1000.0 * lat / rate, peak);
    }

    /* Passband: unity gain and clean sines up to 0.85 × the lower Nyquist. */
    double worstDb = 0, worstResid = -400;
    for (double f : {50.0, 997.0, 5000.0, 12000.0, 0.85 * nyqMin})
    {
        const std::vector<float> y = Run(rate, Sine(rate, f, 1.0), 512, Delay64{});
        double amp, resid;
        FitSine(y, (size_t)(rate / 10), f, rate, amp, resid);
        const double gainDb  = 20 * std::log10(amp / 0.5);
        const double residDb = 20 * std::log10(resid / (0.5 / std::sqrt(2.0)));
        if (std::fabs(gainDb) > std::fabs(worstDb)) worstDb = gainDb;
        if (residDb > worstResid) worstResid = residDb;
        CHECK(std::fabs(gainDb) < 0.01, "%d Hz: %.0f Hz gain %.4f dB", rate, f, gainDb);
        CHECK(residDb < -90.0, "%d Hz: %.0f Hz distortion+noise %.1f dB", rate, f, residDb);
    }
    std::printf("            passband gain error %.5f dB, distortion+noise %.1f dB\n", worstDb, worstResid);

    /* Above the lower Nyquist nothing may leak through. */
    if (rate > 48000)
    {
        /* Host content above 24 kHz must not fold back into the audio band. */
        const std::vector<float> y = Run(rate, Sine(rate, 30000.0, 0.5), 512, Delay64{});
        const double db = 20 * std::log10(Rms(y, (size_t)(rate / 10)) / (0.5 / std::sqrt(2.0)));
        CHECK(db < -90.0, "%d Hz: 30 kHz input leaks at %.1f dB", rate, db);
        std::printf("            30 kHz input rejected: %.1f dB\n", db);
    }
    else
    {
        /* A 23 kHz tone made inside the 48 kHz engine is above this host's
         * Nyquist and must not alias down. */
        auto tone = [](float* l, float* r, int n) {
            static uint64_t t = 0;
            for (int i = 0; i < n; i++, t++)
                l[i] = r[i] = 0.5f * (float)std::sin(2.0 * M_PI * 23000.0 * (double)t / 48000.0);
        };
        const std::vector<float> y = Run(rate, std::vector<float>((size_t)(rate / 2), 0.0f), 512, tone);
        const double db = 20 * std::log10(Rms(y, (size_t)(rate / 10)) / (0.5 / std::sqrt(2.0)) + 1e-30);
        CHECK(db < -90.0, "%d Hz: 23 kHz engine tone aliases at %.1f dB", rate, db);
        std::printf("            23 kHz engine tone rejected: %.1f dB\n", db);
    }

    /* The host's block sizes must not change the output. */
    {
        const std::vector<float> x = Sine(rate, 440.0, 0.5);
        const std::vector<float> a = Run(rate, x, 512, Delay64{});
        const std::vector<float> b = Run(rate, x, 0, Delay64{});
        const std::vector<float> c = Run(rate, x, 1, Delay64{});
        CHECK(a == b && a == c, "%d Hz: output depends on block size", rate);
    }
}

} // namespace

int main()
{
    std::printf("rate bridge:\n");

    /* 48 kHz is a pass-through: exactly the inner process, no extra latency. */
    {
        const std::vector<float> x = Sine(48000, 440.0, 0.1);
        int lat = 0;
        const std::vector<float> y = Run(48000, x, 0, Delay64{}, &lat);
        CHECK(lat == kInnerDelay, "48 kHz latency %d", lat);
        bool same = true;
        for (size_t i = 0; i < x.size(); i++)
            same &= (y[i] == (i < (size_t)kInnerDelay ? 0.0f : x[i - kInnerDelay]));
        CHECK(same, "48 kHz is not a pass-through");
        std::printf("   48000 Hz: pass-through, latency %d samples\n", lat);
    }

    for (int rate : {44100, 88200, 96000, 176400, 192000, 32000})
        Rate(rate);

    std::printf("rate bridge: %s\n", test::Failures() ? "FAIL" : "ok");
    return test::Failures() ? 1 : 0;
}
