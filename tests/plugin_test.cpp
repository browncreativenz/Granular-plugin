/**
 * @file plugin_test.cpp
 * @brief The JUCE wrapper against the core it wraps.
 *
 * Drives CapicolaAudioProcessor the way a host does and checks it against
 * granular::CapicolaProcessor run directly: same audio sample for sample,
 * note-ons slicing at their sample position, mono input, parameters reaching
 * the engine, state round trips, and bypass staying time-aligned.
 */

#include <cmath>
#include <cstdio>
#include <vector>

#include <juce_events/juce_events.h>

#include "Check.h"
#include "PluginProcessor.h"
#include "TestSignal.h"

using namespace granular;

namespace {

constexpr int kRate  = 44100;
constexpr int kBlock = 480;

struct Stereo { std::vector<float> l, r; };

Stereo Signal(double seconds)
{
    Stereo s;
    s.l.resize((size_t)(seconds * kRate));
    s.r.resize(s.l.size());
    TestSignal sig((float)kRate);
    for (size_t i = 0; i < s.l.size(); i++) sig.Next(s.l[i], s.r[i]);
    return s;
}

/* Run the plugin over s in host blocks, with note-ons at the given samples. */
void RunPlugin(CapicolaAudioProcessor& plugin, Stereo& s, const std::vector<int>& notes, int channels = 2)
{
    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos < s.l.size(); pos += kBlock)
    {
        const int n = (int)std::min<size_t>(kBlock, s.l.size() - pos);
        buf.setSize(2, n, false, false, true);
        buf.copyFrom(0, 0, &s.l[pos], n);
        if (channels == 2) buf.copyFrom(1, 0, &s.r[pos], n);
        else               buf.clear(1, 0, n);   // a mono host leaves channel 2 undefined
        midi.clear();
        for (int t : notes)
            if (t >= (int)pos && t < (int)pos + n)
                midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), t - (int)pos);
        plugin.processBlock(buf, midi);
        std::copy(buf.getReadPointer(0), buf.getReadPointer(0) + n, &s.l[pos]);
        std::copy(buf.getReadPointer(1), buf.getReadPointer(1) + n, &s.r[pos]);
    }
}

/* The same through the core directly: slices at the same samples. */
void RunCore(CapicolaProcessor& core, Stereo& s, const std::vector<int>& notes)
{
    size_t pos = 0;
    for (int t : notes)
    {
        core.Process(&s.l[pos], &s.r[pos], &s.l[pos], &s.r[pos], (int)((size_t)t - pos));
        pos = (size_t)t;
        core.TriggerSlice();
    }
    core.Process(&s.l[pos], &s.r[pos], &s.l[pos], &s.r[pos], (int)(s.l.size() - pos));
}

void SetParam(CapicolaAudioProcessor& plugin, const char* id, float value)
{
    auto* p = plugin.parameters.getParameter(id);
    p->setValueNotifyingHost(p->convertTo0to1(value));
}

void MatchesCore()
{
    const std::vector<int> notes = {30000, 30001, 97000, 150123};

    CapicolaAudioProcessor plugin;
    plugin.setPlayConfigDetails(2, 2, kRate, kBlock);
    SetParam(plugin, "stretch", 0.62f);
    SetParam(plugin, "pitch", 0.3f);
    SetParam(plugin, "depth2", 0.8f);
    SetParam(plugin, "src4", 1.0f);   // choice index: Output follower
    SetParam(plugin, "depth4", 0.1f);
    SetParam(plugin, "mix", 0.7f);
    plugin.prepareToPlay(kRate, kBlock);
    CHECK(plugin.getLatencySamples() == 188, "latency %d", plugin.getLatencySamples());

    CapicolaProcessor core;
    core.SetParam(Param::Stretch, 0.62f);
    core.SetParam(Param::Pitch, 0.3f);
    core.SetParam(Param::Depth2, 0.8f);
    core.SetParam(Param::Source4, 1.0f);
    core.SetParam(Param::Depth4, 0.1f);
    core.SetParam(Param::Mix, 0.7f);
    core.Prepare(kRate);

    Stereo a = Signal(5.0), b = a;
    RunPlugin(plugin, a, notes);
    RunCore(core, b, notes);
    CHECK(a.l == b.l && a.r == b.r, "plugin output differs from the core");
    std::printf("  44.1 kHz with parameters, modulation and 4 MIDI slices: identical to the core\n");

    /* Mono input feeds both channels. */
    CapicolaAudioProcessor mono;
    mono.setPlayConfigDetails(1, 2, kRate, kBlock);
    mono.prepareToPlay(kRate, kBlock);
    CapicolaProcessor core2;
    core2.Prepare(kRate);
    Stereo m = Signal(2.0), c = m;
    c.r = c.l;
    RunPlugin(mono, m, {}, 1);
    RunCore(core2, c, {});
    CHECK(m.l == c.l && m.r == c.r, "mono input is not fed to both channels");
    std::printf("  mono input: feeds both channels\n");
}

void State()
{
    CapicolaAudioProcessor a;
    SetParam(a, "fade", 0.66f);
    SetParam(a, "src6", 3.0f);
    SetParam(a, "modB", 0.1f);
    juce::MemoryBlock blob;
    a.getStateInformation(blob);

    CapicolaAudioProcessor b;
    b.setStateInformation(blob.getData(), (int)blob.getSize());
    auto get = [](CapicolaAudioProcessor& p, const char* id) {
        return p.parameters.getRawParameterValue(id)->load();
    };
    CHECK(std::fabs(get(b, "fade") - 0.66f) < 1e-6f, "fade %g", get(b, "fade"));
    CHECK(get(b, "src6") == 3.0f, "src6 %g", get(b, "src6"));
    CHECK(std::fabs(get(b, "modB") - 0.1f) < 1e-6f, "modB %g", get(b, "modB"));
    CHECK(get(b, "pitch") == Info(Param::Pitch).def, "untouched pitch %g", get(b, "pitch"));

    /* Garbage state is ignored, not crashed on. */
    const char junk[] = "not a state";
    b.setStateInformation(junk, sizeof junk);
    CHECK(std::fabs(get(b, "fade") - 0.66f) < 1e-6f, "junk state changed fade");
    std::printf("  state: round-trips, ignores junk\n");

    /* Readouts show what the knob means. */
    auto* stretch = a.parameters.getParameter("stretch");
    CHECK(stretch->getText(0.5f, 32) == "Freeze", "%s", stretch->getText(0.5f, 32).toRawUTF8());
    CHECK(a.parameters.getParameter("pitch")->getText(0.75f, 32) == "+1.00x", "pitch text");
    auto* fade = a.parameters.getParameter("fade");
    CHECK(fade->getText(Info(Param::Fade).def, 32) == "20.0 ms", "%s",
          fade->getText(Info(Param::Fade).def, 32).toRawUTF8());
}

/* Bypassed, the dry signal is delayed by the reported latency, so toggling
 * bypass doesn't shift anything in time. */
void Bypass()
{
    CapicolaAudioProcessor plugin;
    plugin.setPlayConfigDetails(2, 2, kRate, kBlock);
    plugin.prepareToPlay(kRate, kBlock);
    const int lat = plugin.getLatencySamples();

    Stereo s = Signal(0.5);
    const Stereo dry = s;
    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos < s.l.size(); pos += kBlock)
    {
        const int n = (int)std::min<size_t>(kBlock, s.l.size() - pos);
        buf.setSize(2, n, false, false, true);
        buf.copyFrom(0, 0, &s.l[pos], n);
        buf.copyFrom(1, 0, &s.r[pos], n);
        plugin.processBlockBypassed(buf, midi);
        std::copy(buf.getReadPointer(0), buf.getReadPointer(0) + n, &s.l[pos]);
        std::copy(buf.getReadPointer(1), buf.getReadPointer(1) + n, &s.r[pos]);
    }
    bool aligned = true;
    for (size_t i = 0; i < s.l.size(); i++)
    {
        const float el = (i < (size_t)lat) ? 0.0f : dry.l[i - (size_t)lat];
        const float er = (i < (size_t)lat) ? 0.0f : dry.r[i - (size_t)lat];
        aligned &= (s.l[i] == el && s.r[i] == er);
    }
    CHECK(aligned, "bypass is not the dry signal delayed by %d samples", lat);
    std::printf("  bypass: dry signal delayed by the reported %d samples\n", lat);
}

/* The Slice parameter slices on each off → on change, not while held. */
void SliceButton()
{
    CapicolaAudioProcessor plugin;
    plugin.setPlayConfigDetails(2, 2, kRate, kBlock);
    SetParam(plugin, "threshold", 1.0f);   // auto-slicing off: only manual slices
    plugin.prepareToPlay(kRate, kBlock);

    CapicolaProcessor core;
    core.SetParam(Param::Threshold, 1.0f);
    core.Prepare(kRate);

    Stereo a = Signal(3.0), b = a;
    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer midi;
    std::vector<int> slices;
    for (size_t pos = 0, k = 0; pos < a.l.size(); pos += kBlock, k++)
    {
        const bool on = (k >= 50 && k < 60) || (k >= 150 && k < 152);   // two presses
        SetParam(plugin, "slice", on ? 1.0f : 0.0f);
        if ((k == 50 || k == 150)) slices.push_back((int)pos);
        const int n = (int)std::min<size_t>(kBlock, a.l.size() - pos);
        buf.setSize(2, n, false, false, true);
        buf.copyFrom(0, 0, &a.l[pos], n);
        buf.copyFrom(1, 0, &a.r[pos], n);
        plugin.processBlock(buf, midi);
        std::copy(buf.getReadPointer(0), buf.getReadPointer(0) + n, &a.l[pos]);
        std::copy(buf.getReadPointer(1), buf.getReadPointer(1) + n, &a.r[pos]);
    }
    RunCore(core, b, slices);
    CHECK(a.l == b.l && a.r == b.r, "Slice parameter does not slice once per press");
    std::printf("  Slice parameter: one slice per press\n");
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    std::printf("plugin:\n");
    MatchesCore();
    State();
    Bypass();
    SliceButton();
    std::printf("plugin: %s\n", test::Failures() ? "FAIL" : "ok");
    return test::Failures() ? 1 : 0;
}
