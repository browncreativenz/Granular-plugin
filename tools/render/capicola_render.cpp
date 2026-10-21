/**
 * @file capicola_render.cpp
 * @brief Offline renderer: runs a WAV file (or a built-in test signal) through
 *        granular::CapicolaProcessor and writes the result.
 *
 *   capicola_render [options] <out.wav>
 *
 * Parameters are set as knob positions, exactly as the plugin stores them;
 * --list prints each one with its default and what the default means.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "CapicolaProcessor.h"
#include "ParamText.h"
#include "TestSignal.h"
#include "Wav.h"

using namespace granular;

namespace {

struct Settings
{
    std::string in, out;
    double seconds = 8.0;   // test signal length
    int    rate    = 48000; // test signal rate
    std::vector<std::pair<Param, float>> set;
    std::vector<double> slices;   // manual slice times, seconds
};

void Usage()
{
    std::fprintf(stderr,
        "usage: capicola_render [options] <out.wav>\n"
        "\n"
        "  --in FILE         WAV input, mono or stereo, any sample rate. Without\n"
        "                    it, a built-in drums-and-pad test signal is used.\n"
        "  --seconds S       test signal length (default 8)\n"
        "  --rate HZ         test signal sample rate (default 48000)\n"
        "  --set ID=VALUE    set a parameter: a knob position 0..1, or a choice\n"
        "                    index for the src parameters; repeatable\n"
        "  --slice T         manual slice at T seconds; repeatable\n"
        "  --list            list the parameters and their defaults\n"
        "\n"
        "example: capicola_render --in loop.wav --set stretch=0.6 --set pitch=0.25 out.wav\n");
}

bool FindParam(const std::string& id, Param& p)
{
    for (int i = 0; i < kNumParams; i++)
        if (id == kParams[i].id) { p = (Param)i; return true; }
    return false;
}

void List()
{
    std::printf("%-10s %-22s %-8s %s\n", "id", "name", "default", "meaning");
    for (int i = 0; i < kNumParams; i++)
    {
        const ParamInfo& pi = kParams[i];
        std::printf("%-10s %-22s %-8g %s\n", pi.id, pi.name, pi.def, ParamText((Param)i, pi.def).c_str());
    }
    std::printf("\nsrc choices:");
    for (int i = 0; i < kNumModSources; i++) std::printf(" %d = %s%s", i, kModSourceNames[i], i + 1 < kNumModSources ? "," : "\n");
}

bool Parse(int argc, char** argv, Settings& s, bool& list)
{
    for (int i = 1; i < argc; i++)
    {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") return false;
        if (a == "--list") { list = true; return true; }
        if (a.rfind("--", 0) != 0)
        {
            if (!s.out.empty()) { std::fprintf(stderr, "more than one output file\n"); return false; }
            s.out = a;
            continue;
        }
        if (i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", a.c_str()); return false; }
        const std::string v = argv[++i];

        if      (a == "--in")      s.in = v;
        else if (a == "--seconds") s.seconds = std::atof(v.c_str());
        else if (a == "--rate")    s.rate = std::atoi(v.c_str());
        else if (a == "--slice")   s.slices.push_back(std::atof(v.c_str()));
        else if (a == "--set")
        {
            const size_t eq = v.find('=');
            Param p;
            if (eq == std::string::npos || !FindParam(v.substr(0, eq), p))
            { std::fprintf(stderr, "--set needs ID=VALUE with a known ID (see --list): %s\n", v.c_str()); return false; }
            const float value = (float)std::atof(v.c_str() + eq + 1);
            const int   n     = Info(p).choices;
            if (n ? (value < 0 || value > n - 1 || value != std::floor(value)) : (value < 0 || value > 1))
            {
                std::fprintf(stderr, "%s must be %s\n", Info(p).id, n ? "a choice index (see --list)" : "between 0 and 1");
                return false;
            }
            s.set.emplace_back(p, value);
        }
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return false; }
    }
    if (s.out.empty()) { std::fprintf(stderr, "no output file given\n"); return false; }
    if (s.rate < 8000 || s.rate > 384000) { std::fprintf(stderr, "--rate out of range\n"); return false; }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    Settings s;
    bool list = false;
    if (!Parse(argc, argv, s, list)) { Usage(); return 2; }
    if (list) { List(); return 0; }

    StereoAudio input;
    if (!s.in.empty())
    {
        std::string error;
        if (!ReadWav(s.in, input, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    }
    else
    {
        const size_t n = (size_t)(s.seconds * s.rate);
        input.sampleRate = (uint32_t)s.rate;
        input.l.resize(n);
        input.r.resize(n);
        TestSignal sig((float)s.rate);
        for (size_t i = 0; i < n; i++) sig.Next(input.l[i], input.r[i]);
    }

    const int    rate = (int)input.sampleRate;
    const size_t n    = input.l.size();

    CapicolaProcessor proc;
    for (const auto& [p, v] : s.set) proc.SetParam(p, v);
    proc.Prepare(rate);
    const size_t lat = (size_t)proc.LatencySamples();

    /* Render n + latency samples (zero tail) and drop the first `lat`, so the
     * output lines up with the input. Manual slices cut the render into pieces. */
    StereoAudio out;
    out.sampleRate = input.sampleRate;
    out.l.assign(n + lat, 0.0f);
    out.r.assign(n + lat, 0.0f);
    std::copy(input.l.begin(), input.l.end(), out.l.begin());
    std::copy(input.r.begin(), input.r.end(), out.r.begin());

    std::vector<size_t> cuts;
    for (double t : s.slices)
        if (t >= 0.0 && t * rate < (double)n) cuts.push_back((size_t)(t * rate));
    std::sort(cuts.begin(), cuts.end());

    const auto start = std::chrono::steady_clock::now();
    size_t pos = 0;
    for (size_t c = 0; c <= cuts.size(); c++)
    {
        const size_t end = (c < cuts.size()) ? cuts[c] : n + lat;
        for (; pos < end; )   // host-sized blocks, as a DAW would call
        {
            const size_t len = std::min<size_t>(512, end - pos);
            proc.Process(&out.l[pos], &out.r[pos], &out.l[pos], &out.r[pos], (int)len);
            pos += len;
        }
        if (c < cuts.size()) proc.TriggerSlice();
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    out.l.erase(out.l.begin(), out.l.begin() + (long)lat);
    out.r.erase(out.r.begin(), out.r.begin() + (long)lat);

    if (!WriteWav(s.out, out)) { std::fprintf(stderr, "cannot write %s\n", s.out.c_str()); return 1; }

    const double secs = (double)n / rate;
    std::printf("%s: %.2f s at %d Hz, %u slices, %u output events, latency %zu samples, "
                "rendered %.0fx realtime\n",
                s.out.c_str(), secs, rate, proc.SliceCount(), proc.OutEventCount(), lat,
                elapsed > 0.0 ? secs / elapsed : 0.0);
    return 0;
}
