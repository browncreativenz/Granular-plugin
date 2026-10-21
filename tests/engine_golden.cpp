/**
 * @file engine_golden.cpp
 * @brief Checks granular::CapicolaEngine against the firmware's own engine.
 *
 *   engine_golden <scenario>
 *
 * Builds Capicola's src/audio/audio_engine.cpp (through the stubs in
 * firmware_stubs/) and the port side by side, feeds both the same stereo
 * input and the same parameter changes, and requires the outputs to match
 * bit for bit. The firmware runs fixed 64-sample blocks; the port is fed
 * random host block sizes in place, so its output must equal the firmware's
 * delayed by LatencySamples().
 *
 * One scenario per process: the firmware engine keeps its rings in statics.
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "audio/audio_engine.h"   // firmware
#include "CapicolaEngine.h"       // port
#include "TestSignal.h"

namespace {

using Firmware = AlchemyLabAudio::AudioEngine;
using Port     = granular::CapicolaEngine;

constexpr int kBlock = 64;
static_assert(Port::kBlock == kBlock, "port and firmware block sizes differ");

enum class Op { Pitch, Stretch, Threshold, Grain, Quality, Feedback, FbBand,
                Smoothing, Resonance, Fade, Drive, Character, Mix, Slice };

struct Event
{
    int   block;   // applied before this firmware block
    Op    op;
    float value;
};

template <class Engine>
void Apply(Engine& e, const Event& ev)
{
    switch (ev.op)
    {
        case Op::Pitch:     e.SetSlicePitch(ev.value);         break;
        case Op::Stretch:   e.SetSliceStretch(ev.value);       break;
        case Op::Threshold: e.SetTransientThreshold(ev.value); break;
        case Op::Grain:     e.SetGrainSize(ev.value);          break;
        case Op::Quality:   e.SetQuality(ev.value);            break;
        case Op::Feedback:  e.SetFeedback(ev.value);           break;
        case Op::FbBand:    e.SetFeedbackCutoff(ev.value);     break;
        case Op::Smoothing: e.SetTkeoCutoff(ev.value);         break;
        case Op::Resonance: e.SetTkeoResonance(ev.value);      break;
        case Op::Fade:      e.SetFade(ev.value);               break;
        case Op::Drive:     e.SetDrive(ev.value);              break;
        case Op::Character: e.SetDriveCharacter(ev.value);     break;
        case Op::Mix:       e.SetMix(ev.value);                break;
        case Op::Slice:     e.TriggerSlice();                  break;
    }
}

constexpr int BlockAt(double seconds) { return (int)(seconds * 48000.0 / kBlock); }

/* Factory defaults from the v1.1.0 firmware, in engineering units. */
void Factory(std::vector<Event>& ev)
{
    ev.push_back({0, Op::Pitch,     1.0f});
    ev.push_back({0, Op::Stretch,   std::pow(0.8f, 2.5f)});   // knob 0.9
    ev.push_back({0, Op::Threshold, 0.5f * (4.0f / 0.9f)});   // knob 0.5
    ev.push_back({0, Op::Grain,     32.0f + 0.15f * 4064.0f}); // 642 keyframes
    ev.push_back({0, Op::Quality,   0.001f});
    ev.push_back({0, Op::Feedback,  0.0f});
}

bool Scenario(int id, std::vector<Event>& ev, double& seconds)
{
    Factory(ev);
    switch (id)
    {
        case 0:   // factory settings, untouched
            seconds = 12.0;
            return true;

        case 1:   // pitch, feedback, drive and mix, with direction changes
            seconds = 12.0;
            ev.push_back({0, Op::Pitch,     std::exp2(5.0f / 12.0f)});
            ev.push_back({0, Op::Feedback,  0.8f});
            ev.push_back({0, Op::FbBand,    0.05f});
            ev.push_back({0, Op::Mix,       0.6f});
            ev.push_back({0, Op::Drive,     2.5f});
            ev.push_back({0, Op::Character, 0.2f});
            ev.push_back({0, Op::Fade,      2400.0f});
            ev.push_back({BlockAt(4.0),  Op::Pitch,     -0.7f});
            ev.push_back({BlockAt(5.0),  Op::Slice,      0.0f});
            ev.push_back({BlockAt(6.0),  Op::Stretch,   -0.3f});
            ev.push_back({BlockAt(7.5),  Op::Character,  0.9f});
            ev.push_back({BlockAt(8.0),  Op::Stretch,    0.0f});   // freeze
            ev.push_back({BlockAt(10.0), Op::Feedback,   1.5f});
            return true;

        case 2:   // sweeps of every secondary control, manual slices, muted finder
            seconds = 16.0;
            for (int k = 0, i = 0; k < BlockAt(seconds); k += 37, i++)
            {
                ev.push_back({k, Op::Grain,   32.0f + (float)((i * 523) % 4064)});
                ev.push_back({k, Op::Quality, 0.08f - 0.079f * (float)((i * 7) % 10) / 9.0f});
            }
            for (int k = 100; k < BlockAt(seconds); k += 200)
                ev.push_back({k, Op::Slice, 0.0f});
            ev.push_back({BlockAt(2.0),  Op::Smoothing, 0.02f});
            ev.push_back({BlockAt(3.0),  Op::Resonance, 0.8f});
            ev.push_back({BlockAt(4.0),  Op::Threshold, 1.0e9f});   // auto-slicing muted
            ev.push_back({BlockAt(7.0),  Op::Threshold, 0.4f});
            ev.push_back({BlockAt(9.0),  Op::Fade,      480.0f});
            ev.push_back({BlockAt(11.0), Op::Fade,      12000.0f});
            ev.push_back({BlockAt(12.0), Op::Pitch,     2.0f});
            ev.push_back({BlockAt(13.0), Op::Stretch,   -1.0f});
            ev.push_back({BlockAt(14.0), Op::Mix,       0.0f});
            return true;

        case 3:   // drums left, steady tone right, slow stretch: the channels
                  // drift apart until the stereo guardrail re-anchors the right
            seconds = 12.0;
            ev.push_back({0, Op::Stretch, 0.05f});
            return true;

        default:
            return false;
    }
}

/* Same engines for the whole process: the firmware rings are statics, and the
 * port allocates about 48 MB. */
Firmware firmware;
Port     port;

} // namespace

int main(int argc, char** argv)
{
    std::vector<Event> events;
    double seconds = 0.0;
    if (argc < 2 || !Scenario(std::atoi(argv[1]), events, seconds))
    {
        std::fprintf(stderr, "usage: engine_golden <scenario 0..3>\n");
        return 2;
    }

    const int    blocks = BlockAt(seconds);
    const size_t n      = (size_t)blocks * kBlock;

    std::vector<float> inL(n), inR(n);
    granular::TestSignal sig(48000.0f);
    for (size_t i = 0; i < n; i++) sig.Next(inL[i], inR[i]);
    if (std::atoi(argv[1]) == 3)
        for (size_t i = 0; i < n; i++)
            inR[i] = 0.3f * (float)std::sin(2.0 * M_PI * 150.0 * (double)i / 48000.0);

    /* Firmware: fixed 64-sample blocks, events between blocks. */
    std::vector<float> fwL(n), fwR(n);
    firmware.Init();
    for (int k = 0; k < blocks; k++)
    {
        for (const Event& ev : events) if (ev.block == k) Apply(firmware, ev);
        firmware.ProcessBlock(&inL[k * kBlock], &inR[k * kBlock],
                              &fwL[k * kBlock], &fwR[k * kBlock], kBlock);
    }

    /* Port: random host blocks, processed in place. An event for firmware block
     * k must land after the port has run block k-1 (input sample 64k-1) and
     * before it runs block k (input sample 64k+63), so each block's events get
     * one random point inside that window, keep their order, and the host
     * blocks are cut there. */
    std::vector<float> L(inL), R(inR);

    /* Dirty the port first, then Prepare() again: a host re-prepares an engine
     * that has already run, so Prepare() must restore the full boot state, not
     * just the parts a freshly zeroed allocation hides. */
    port.Prepare();
    {
        port.SetSlicePitch(-1.3f);
        port.SetSliceStretch(0.2f);
        port.SetFeedback(1.2f);
        port.SetFeedbackCutoff(0.3f);
        port.SetMix(0.3f);
        port.SetDrive(3.0f);
        port.SetDriveCharacter(0.1f);
        port.SetTkeoResonance(0.5f);
        std::vector<float> dl(inR.rbegin(), inR.rend()), dr(inL.rbegin(), inL.rend());
        const size_t warm = dl.size() < 192000 ? dl.size() : 192000;
        for (size_t pos = 0; pos < warm; pos += 333)
        {
            const size_t len = (warm - pos < 333) ? warm - pos : 333;
            port.Process(&dl[pos], &dr[pos], &dl[pos], &dr[pos], len);
            if (pos % 9990 == 0) port.TriggerSlice();
        }
    }
    port.Prepare();
    {
        uint32_t rng = 0x9e3779b9u ^ (uint32_t)std::atoi(argv[1]);
        auto next = [&rng]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };

        std::vector<size_t> offset(blocks);
        for (size_t& o : offset) o = next() % kBlock;
        std::vector<size_t> point(events.size());
        for (size_t j = 0; j < events.size(); j++)
            point[j] = (size_t)events[j].block * kBlock + offset[events[j].block];

        size_t pos = 0;
        while (true)
        {
            for (size_t j = 0; j < events.size(); j++)
                if (point[j] == pos) Apply(port, events[j]);
            if (pos == n) break;

            size_t end = pos + 1 + next() % 700;
            if (end > n) end = n;
            for (size_t p : point) if (p > pos && p < end) end = p;

            port.Process(&L[pos], &R[pos], &L[pos], &R[pos], end - pos);
            pos = end;
        }
    }

    /* Compare: port[t + latency] == firmware[t]. */
    const size_t lat = (size_t)Port::LatencySamples();
    int    failures  = 0;
    double maxDiff   = 0.0;
    size_t firstBad  = n;
    for (size_t t = 0; t < lat; t++)
        if (L[t] != 0.0f || R[t] != 0.0f) { firstBad = t; break; }
    for (size_t t = 0; t + lat < n; t++)
    {
        const double dl = std::fabs((double)L[t + lat] - fwL[t]);
        const double dr = std::fabs((double)R[t + lat] - fwR[t]);
        const double d  = dl > dr ? dl : dr;
        if (d > maxDiff) maxDiff = d;
        if (d != 0.0 && firstBad == n) firstBad = t + lat;
    }
    if (firstBad != n)
    {
        std::printf("FAIL audio: first mismatch at sample %zu, max |diff| %.3g\n", firstBad, maxDiff);
        failures++;
    }

    auto check = [&failures](const char* what, double a, double b) {
        if (a != b) { std::printf("FAIL %s: port %.9g, firmware %.9g\n", what, a, b); failures++; }
    };
    check("slice count",     port.SliceCount(),    firmware.SliceCount());
    check("out event count", port.OutEventCount(), firmware.OutEventCount());
    check("input envelope",  port.EnvNormIn(),     firmware.EnvNormIn());
    check("output envelope", port.EnvNormOut(),    firmware.EnvNormOut());
    check("input peak",      port.InPeak(),        firmware.InPeak());

    /* Guard against a vacuous pass: the scenario must actually produce sound
     * and slices. */
    double rms = 0.0;
    for (size_t t = 0; t < n; t++) rms += (double)fwL[t] * fwL[t];
    rms = std::sqrt(rms / (double)n);
    if (rms < 1e-3 || firmware.SliceCount() == 0)
    {
        std::printf("FAIL scenario is silent or never slices (rms %.3g, slices %u)\n",
                    rms, firmware.SliceCount());
        failures++;
    }

    std::printf("scenario %s: %.1f s, %u slices, %u output events, rms %.4f: %s\n",
                argv[1], seconds, firmware.SliceCount(), firmware.OutEventCount(), rms,
                failures ? "FAIL" : "bit-exact");
    return failures ? 1 : 0;
}
