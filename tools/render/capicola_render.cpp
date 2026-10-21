/**
 * @file capicola_render.cpp
 * @brief Offline renderer: runs a WAV file (or a built-in test signal) through
 *        granular::CapicolaEngine and writes the result.
 *
 *   capicola_render [options] <out.wav>
 *
 * Parameters take engineering units, as CapicolaEngine's setters do; defaults
 * are the v1.1.0 firmware's factory settings. Run with --help for the list.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "CapicolaEngine.h"
#include "TestSignal.h"
#include "Wav.h"

namespace {

struct Settings
{
    std::string in, out;
    double seconds    = 8.0;      // test signal length
    float  pitch      = 1.0f;
    float  stretch    = 0.5724334f;   // knob 0.9: 0.8^2.5
    float  threshold  = 2.2222222f;   // knob 0.5: 0.5 × 4 / 0.9
    float  grain      = 641.6f;       // knob 0.15: 32 + 0.15 × 4064
    float  quality    = 0.001f;
    float  feedback   = 0.0f;
    float  fbBand     = 0.02f;
    float  smoothing  = 0.0014f;
    float  fadeMs     = 20.0f;
    float  drive      = 1.0f;
    float  character  = 1.0f;
    float  mix        = 1.0f;
    std::vector<double> slices;   // manual slice times, seconds
};

void Usage()
{
    std::fprintf(stderr,
        "usage: capicola_render [options] <out.wav>\n"
        "\n"
        "  --in FILE         48 kHz WAV input (mono or stereo). Without it, a\n"
        "                    built-in drums-and-pad test signal is used.\n"
        "  --seconds S       test signal length (default 8)\n"
        "\n"
        "  --pitch X         playback speed, -2..2, negative = reverse (default 1)\n"
        "  --stretch X       -1..1, 0 = freeze, 1 = realtime (default 0.572)\n"
        "  --threshold X     transient keep ratio 0..8, 1e9 = no auto-slicing (default 2.22)\n"
        "  --grain X         grain size, 32..4096 keyframes (default 642)\n"
        "  --quality X       analyzer epsilon, 0.08 crunchy .. 0.001 best (default 0.001)\n"
        "  --feedback X      0..1.5 (default 0)\n"
        "  --fb-band X       feedback bandpass fc, 0.002..0.9 normalised (default 0.02)\n"
        "  --smoothing X     follower fc, 5e-5..0.125 normalised (default 0.0014)\n"
        "  --fade-ms X       crossfade, 10..250 ms (default 20)\n"
        "  --drive X         0.5..4 (default 1)\n"
        "  --character X     0 quake, 0.5 clean, 1 sinc (default 1)\n"
        "  --mix X           0 dry .. 1 wet (default 1)\n"
        "  --slice T         manual slice at T seconds; repeatable\n");
}

bool Parse(int argc, char** argv, Settings& s)
{
    struct Flag { const char* name; float* value; };
    const Flag flags[] = {
        {"--pitch", &s.pitch},         {"--stretch", &s.stretch},
        {"--threshold", &s.threshold}, {"--grain", &s.grain},
        {"--quality", &s.quality},     {"--feedback", &s.feedback},
        {"--fb-band", &s.fbBand},      {"--smoothing", &s.smoothing},
        {"--fade-ms", &s.fadeMs},      {"--drive", &s.drive},
        {"--character", &s.character}, {"--mix", &s.mix},
    };

    for (int i = 1; i < argc; i++)
    {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") return false;
        if (a.rfind("--", 0) != 0)
        {
            if (!s.out.empty()) { std::fprintf(stderr, "more than one output file\n"); return false; }
            s.out = a;
            continue;
        }
        if (i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", a.c_str()); return false; }
        const char* v = argv[++i];

        if (a == "--in")           { s.in = v; continue; }
        if (a == "--seconds")      { s.seconds = std::atof(v); continue; }
        if (a == "--slice")        { s.slices.push_back(std::atof(v)); continue; }
        bool known = false;
        for (const Flag& f : flags)
            if (a == f.name) { *f.value = (float)std::atof(v); known = true; }
        if (!known) { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return false; }
    }
    if (s.out.empty()) { std::fprintf(stderr, "no output file given\n"); return false; }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    Settings s;
    if (!Parse(argc, argv, s)) { Usage(); return 2; }

    constexpr double kFs = granular::CapicolaEngine::kSampleRate;

    granular::StereoAudio input;
    if (!s.in.empty())
    {
        std::string error;
        if (!granular::ReadWav(s.in, input, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
        if (input.sampleRate != (uint32_t)kFs)
        {
            std::fprintf(stderr, "%s is %u Hz; only 48000 Hz is supported until sample-rate "
                                 "scaling lands (milestone M1)\n", s.in.c_str(), input.sampleRate);
            return 1;
        }
    }
    else
    {
        const size_t n = (size_t)(s.seconds * kFs);
        input.sampleRate = (uint32_t)kFs;
        input.l.resize(n);
        input.r.resize(n);
        granular::TestSignal sig((float)kFs);
        for (size_t i = 0; i < n; i++) sig.Next(input.l[i], input.r[i]);
    }

    const size_t n   = input.l.size();
    const size_t lat = (size_t)granular::CapicolaEngine::LatencySamples();

    granular::CapicolaEngine engine;
    engine.Prepare();
    engine.SetSlicePitch(s.pitch);
    engine.SetSliceStretch(s.stretch);
    engine.SetTransientThreshold(s.threshold);
    engine.SetGrainSize(s.grain);
    engine.SetQuality(s.quality);
    engine.SetFeedback(s.feedback);
    engine.SetFeedbackCutoff(s.fbBand);
    engine.SetTkeoCutoff(s.smoothing);
    engine.SetFade(s.fadeMs * 0.001f * (float)kFs);
    engine.SetDrive(s.drive);
    engine.SetDriveCharacter(s.character);
    engine.SetMix(s.mix);

    /* Render n + latency samples (zero tail) and drop the first `lat`, so the
     * output lines up with the input. Manual slices cut the render into pieces. */
    granular::StereoAudio out;
    out.sampleRate = input.sampleRate;
    out.l.assign(n + lat, 0.0f);
    out.r.assign(n + lat, 0.0f);
    std::copy(input.l.begin(), input.l.end(), out.l.begin());
    std::copy(input.r.begin(), input.r.end(), out.r.begin());

    std::vector<size_t> cuts;
    for (double t : s.slices)
        if (t >= 0.0 && t * kFs < (double)n) cuts.push_back((size_t)(t * kFs));
    std::sort(cuts.begin(), cuts.end());

    const auto start = std::chrono::steady_clock::now();
    size_t pos = 0;
    for (size_t c = 0; c <= cuts.size(); c++)
    {
        const size_t end = (c < cuts.size()) ? cuts[c] : n + lat;
        engine.Process(&out.l[pos], &out.r[pos], &out.l[pos], &out.r[pos], end - pos);
        pos = end;
        if (c < cuts.size()) engine.TriggerSlice();
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    out.l.erase(out.l.begin(), out.l.begin() + (long)lat);
    out.r.erase(out.r.begin(), out.r.begin() + (long)lat);

    if (!granular::WriteWav(s.out, out)) { std::fprintf(stderr, "cannot write %s\n", s.out.c_str()); return 1; }

    const double secs = (double)n / kFs;
    std::printf("%s: %.2f s, %u slices, %u output events, rendered %.0fx realtime\n",
                s.out.c_str(), secs, engine.SliceCount(), engine.OutEventCount(),
                elapsed > 0.0 ? secs / elapsed : 0.0);
    return 0;
}
