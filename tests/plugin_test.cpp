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
#include <mutex>
#include <vector>

/* The checks compare floats exactly on purpose; JUCE's warning flags, applied
 * after this target's own, would flag every one. */
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

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

/* Run the plugin over s in host blocks, with note-ons at the given samples.
 * With `midi`, drains the plugin's MIDI queue after each block, as its MIDI
 * thread would. */
void RunPlugin(CapicolaAudioProcessor& plugin, Stereo& s, const std::vector<int>& notes, int channels = 2,
               std::vector<FollowerMidiOut::Pending>* midiOut = nullptr)
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
        FollowerMidiOut::Pending p;
        while (midiOut != nullptr && plugin.MidiOut().Pop(p)) midiOut->push_back(p);
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

/* Reverse mirrors the knob around noon: Pitch 75% reversed is Pitch 25%. */
void Reverse()
{
    CapicolaAudioProcessor plugin;
    plugin.setPlayConfigDetails(2, 2, kRate, kBlock);
    SetParam(plugin, "pitch", 0.75f);
    SetParam(plugin, "stretch", 0.9f);
    SetParam(plugin, "pitchRev", 1.0f);
    SetParam(plugin, "stretchRev", 1.0f);
    plugin.prepareToPlay(kRate, kBlock);

    CapicolaProcessor core;
    core.SetParam(Param::Pitch, 0.25f);
    core.SetParam(Param::Stretch, 1.0f - 0.9f);
    core.Prepare(kRate);

    Stereo a = Signal(3.0), b = a;
    RunPlugin(plugin, a, {});
    RunCore(core, b, {});
    CHECK(a.l == b.l && a.r == b.r, "Pitch/Stretch Reverse is not the mirrored knob");
    std::printf("  Pitch and Stretch Reverse: mirror the knobs around noon\n");
}

/* The follower MIDI output: what the plugin queues for its port. */
void MidiOut()
{
    using granular::FollowerEvent;
    using Events = std::vector<FollowerMidiOut::Pending>;
    auto count = [](const Events& ev, FollowerEvent::Kind kind, int number) {
        int n = 0;
        for (const auto& e : ev) n += (e.event.kind == kind && e.event.number == number);
        return n;
    };

    /* Defaults, auto-slicing on: slice notes, output-event notes, envelopes. */
    {
        CapicolaAudioProcessor plugin;
        plugin.setPlayConfigDetails(2, 2, kRate, kBlock);
        plugin.prepareToPlay(kRate, kBlock);
        Stereo s = Signal(4.0);
        Events got;
        RunPlugin(plugin, s, {}, 2, &got);

        const int slices = count(got, FollowerEvent::Note, 36);
        const int events = count(got, FollowerEvent::Note, 38);
        const int ccIn   = count(got, FollowerEvent::Controller, 20);
        const int ccOut  = count(got, FollowerEvent::Controller, 21);
        CHECK(slices > 5, "%d slice notes", slices);
        CHECK(events > 5, "%d output-event notes", events);
        CHECK(slices + events + ccIn + ccOut == (int)got.size(), "unexpected numbers");
        /* At most one value per 10 ms per controller: 400 in 4 s. */
        CHECK(ccIn > 20 && ccIn <= 401 && ccOut > 20 && ccOut <= 401, "envelope CCs %d / %d", ccIn, ccOut);

        bool channel = true, ordered = true;
        for (size_t i = 0; i < got.size(); i++)
        {
            channel &= (got[i].channel == 1);
            if (i > 0) ordered &= (got[i].dueMs >= got[i - 1].dueMs);
        }
        CHECK(channel, "not on channel 1");
        CHECK(ordered, "due times go backwards");
        std::printf("  MIDI out: %d slice notes, %d output-event notes, %d + %d envelope CCs in 4 s\n",
                    slices, events, ccIn, ccOut);
    }

    /* Auto-slicing off, settings changed: the only slices are four MIDI notes,
     * the second inside the first's 10 ms hold. */
    {
        CapicolaAudioProcessor plugin;
        plugin.setPlayConfigDetails(2, 2, kRate, kBlock);
        SetParam(plugin, "threshold", 1.0f);
        SetParam(plugin, "midiChannel", 5.0f);
        SetParam(plugin, "midiSliceNote", 40.0f);
        plugin.prepareToPlay(kRate, kBlock);

        const std::vector<int> notes = {10000, 10100, 50000, 99999};
        Stereo s = Signal(3.0);
        Events got;
        RunPlugin(plugin, s, notes, 2, &got);

        std::vector<const FollowerMidiOut::Pending*> sliceNotes;
        bool channel = true;
        for (const auto& e : got)
        {
            channel &= (e.channel == 5);
            if (e.event.kind == FollowerEvent::Note && e.event.number == 40) sliceNotes.push_back(&e);
        }
        CHECK(channel, "channel setting ignored");
        CHECK(count(got, FollowerEvent::Note, 38) == 0, "output events with auto-slicing off");
        CHECK(sliceNotes.size() == 3, "%zu slice notes for 4 slices, one inside the hold", sliceNotes.size());
        const int expect[3] = {10000 % kBlock, 50000 % kBlock, 99999 % kBlock};
        for (size_t k = 0; k < sliceNotes.size() && k < 3; k++)
            CHECK(sliceNotes[k]->event.offset == expect[k], "slice note %zu at offset %d, expected %d",
                  k, sliceNotes[k]->event.offset, expect[k]);
        /* Due times follow the audio clock: 40000 samples apart. */
        if (sliceNotes.size() == 3)
        {
            const double gapMs = sliceNotes[1]->dueMs - sliceNotes[0]->dueMs;
            CHECK(std::fabs(gapMs - 1000.0 * 40000 / kRate) < 0.01, "slice notes %.3f ms apart", gapMs);
        }
        std::printf("  MIDI out: manual slices land at their sample positions, on the audio clock\n");
    }

    /* MIDI Out off: nothing queued. */
    {
        CapicolaAudioProcessor quiet;
        quiet.setPlayConfigDetails(2, 2, kRate, kBlock);
        SetParam(quiet, "midiOut", 0.0f);
        quiet.prepareToPlay(kRate, kBlock);
        Stereo t = Signal(1.0);
        Events got;
        RunPlugin(quiet, t, {1000}, 2, &got);
        CHECK(got.empty(), "%zu events queued with MIDI Out off", got.size());
    }
}

/* End to end through a real virtual MIDI port: open the plugin's port as an
 * input and listen. Needs a MIDI system, so it is skipped where the port
 * can't be created (this container; CI on macOS runs it). */
void MidiPort()
{
    struct Listener : juce::MidiInputCallback
    {
        std::mutex lock;
        std::vector<std::pair<double, juce::MidiMessage>> got;
        void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override
        {
            std::lock_guard<std::mutex> g(lock);
            got.emplace_back(juce::Time::getMillisecondCounterHiRes(), m);
        }
    } listener;

    CapicolaAudioProcessor plugin;
    plugin.setPlayConfigDetails(2, 2, kRate, kBlock);
    SetParam(plugin, "threshold", 1.0f);   // only the two manual slices below
    plugin.prepareToPlay(kRate, kBlock);
    plugin.MidiOut().SetEnabled(true);
    if (!plugin.MidiOut().UpdatePort())
    {
        std::printf("  MIDI port: skipped, no MIDI system here\n");
        return;
    }

    std::unique_ptr<juce::MidiInput> input;
    for (int tries = 0; tries < 40 && input == nullptr; tries++)
    {
        for (const auto& d : juce::MidiInput::getAvailableDevices())
            if (d.name == plugin.MidiOut().PortName())
                input = juce::MidiInput::openDevice(d.identifier, &listener);
        if (input == nullptr) juce::Thread::sleep(50);
    }
    CHECK(input != nullptr, "port %s not visible as a MIDI input", plugin.MidiOut().PortName().toRawUTF8());
    if (input == nullptr) return;
    input->start();

    /* Two slices 20000 samples (453.5 ms) apart, then wait for the MIDI
     * thread to send them on the audio clock. */
    Stereo s = Signal(1.0);
    RunPlugin(plugin, s, {10000, 30000});
    juce::Thread::sleep(1500);
    input->stop();

    std::vector<double> ons, offs;
    int ccs = 0;
    {
        std::lock_guard<std::mutex> g(listener.lock);
        for (const auto& [t, m] : listener.got)
        {
            if (m.isNoteOn() && m.getNoteNumber() == 36)  ons.push_back(t);
            else if (m.isNoteOff() && m.getNoteNumber() == 36) offs.push_back(t);
            else if (m.isController()) ccs++;
        }
    }
    CHECK(ons.size() == 2 && offs.size() == 2, "%zu note-ons, %zu note-offs", ons.size(), offs.size());
    if (ons.size() == 2 && offs.size() == 2)
    {
        const double gap = ons[1] - ons[0], len = offs[0] - ons[0];
        CHECK(std::fabs(gap - 453.5) < 15.0, "slice notes %.1f ms apart, expected 453.5", gap);
        CHECK(len > 5.0 && len < 40.0, "trigger note lasted %.1f ms, expected about 10", len);
        std::printf("  MIDI port %s: 2 notes %.1f ms apart (453.5 expected), %.1f ms long, %d CCs\n",
                    plugin.MidiOut().PortName().toRawUTF8(), gap, len, ccs);
    }
    CHECK(ccs > 0, "no envelope CCs arrived");
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
    Reverse();
    MidiOut();
    MidiPort();
    std::printf("plugin: %s\n", test::Failures() ? "FAIL" : "ok");
    return test::Failures() ? 1 : 0;
}
