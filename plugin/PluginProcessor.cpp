/**
 * @file PluginProcessor.cpp
 * @brief See PluginProcessor.h.
 */

#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <cmath>

#include "ParamText.h"

using granular::Param;

namespace {
constexpr const char* kSliceId          = "slice";
constexpr const char* kPitchReverseId   = "pitchRev";
constexpr const char* kStretchReverseId = "stretchRev";
constexpr const char* kMidiOutId        = "midiOut";
constexpr const char* kMidiChannelId    = "midiChannel";
constexpr const char* kSliceNoteId      = "midiSliceNote";
constexpr const char* kEventNoteId      = "midiEventNote";
constexpr const char* kInEnvCcId        = "midiInEnvCc";
constexpr const char* kOutEnvCcId       = "midiOutEnvCc";

/* Sub-block length for following the followers: about 1.5 ms at 44.1 kHz. */
constexpr int kObserveSamples = 64;

/* Settings, not performance controls: kept out of automation. */
std::unique_ptr<juce::AudioParameterInt> Setting(const char* id, const char* name, int lo, int hi, int def,
                                                 std::function<juce::String(int, int)> text = {})
{
    auto attributes = juce::AudioParameterIntAttributes().withAutomatable(false);
    if (text) attributes = attributes.withStringFromValueFunction(std::move(text));
    return std::make_unique<juce::AudioParameterInt>(juce::ParameterID { id, 1 }, name, lo, hi, def, attributes);
}

/* Note names as Live shows them: middle C (60) is C3. */
juce::String NoteName(int note, int) { return juce::MidiMessage::getMidiNoteName(note, true, true, 3); }
} // namespace

CapicolaAudioProcessor::CapicolaAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "Capicola", CreateLayout())
{
    for (int p = 0; p < granular::kNumParams; p++)
        values[(size_t)p] = parameters.getRawParameterValue(granular::kParams[p].id);
    sliceButton    = parameters.getRawParameterValue(kSliceId);
    pitchReverse   = parameters.getRawParameterValue(kPitchReverseId);
    stretchReverse = parameters.getRawParameterValue(kStretchReverseId);
    midiParams.enabled   = parameters.getRawParameterValue(kMidiOutId);
    midiParams.channel   = parameters.getRawParameterValue(kMidiChannelId);
    midiParams.sliceNote = parameters.getRawParameterValue(kSliceNoteId);
    midiParams.eventNote = parameters.getRawParameterValue(kEventNoteId);
    midiParams.inEnvCc   = parameters.getRawParameterValue(kInEnvCcId);
    midiParams.outEnvCc  = parameters.getRawParameterValue(kOutEnvCcId);
}

juce::AudioProcessorValueTreeState::ParameterLayout CapicolaAudioProcessor::CreateLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    juce::StringArray sources;
    for (const char* name : granular::kModSourceNames) sources.add(name);

    /* Knob positions 0..1, as the module stores them; the readout shows what
     * the position means. Order here is the order Live lists them in. */
    for (int i = 0; i < granular::kNumParams; i++)
    {
        const granular::ParamInfo& info = granular::kParams[i];
        const juce::ParameterID    id { info.id, 1 };
        if (info.choices)
        {
            layout.add(std::make_unique<juce::AudioParameterChoice>(id, info.name, sources, (int)info.def));
        }
        else
        {
            const Param p = (Param)i;
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                id, info.name, juce::NormalisableRange<float>(0.0f, 1.0f), info.def,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                    [p](float v, int) { return juce::String(granular::ParamText(p, v)); })));
        }
    }

    /* The module's Slice button. Slices on each off → on change, so it can be
     * automated or MIDI-mapped in Live; any MIDI note-on also slices. */
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { kSliceId, 1 }, "Slice", false));

    /* The module's hold-Slice-and-nudge flip: mirror Pitch or Stretch around
     * noon, so the same speed runs the other way. */
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { kPitchReverseId, 1 }, "Pitch Reverse", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { kStretchReverseId, 1 }, "Stretch Reverse", false));

    /* Follower MIDI output: the module's TRIG and ENV jacks. */
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { kMidiOutId, 1 }, "MIDI Out", true,
        juce::AudioParameterBoolAttributes().withAutomatable(false)));
    layout.add(Setting(kMidiChannelId, "MIDI Channel",       1,  16,  1));
    layout.add(Setting(kSliceNoteId,   "Slice Note",         0, 127, 36, NoteName));
    layout.add(Setting(kEventNoteId,   "Output Event Note",  0, 127, 38, NoteName));
    layout.add(Setting(kInEnvCcId,     "Input Envelope CC",  0, 127, 20));
    layout.add(Setting(kOutEnvCcId,    "Output Envelope CC", 0, 127, 21));
    return layout;
}

bool CapicolaAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void CapicolaAudioProcessor::prepareToPlay(double sampleRate, int)
{
    engine.Prepare((int)std::lround(sampleRate));
    setLatencySamples(engine.LatencySamples());
    followers.Prepare((int)std::lround(sampleRate));
    hostRate = sampleRate;
    anchored = false;

    bypassDelay.setSize(2, juce::jmax(1, engine.LatencySamples()));
    bypassDelay.clear();
    bypassPos = 0;

    /* Don't take a Slice that was left on as a fresh press. */
    sliceHeld = sliceButton->load() > 0.5f;
}

void CapicolaAudioProcessor::reset()
{
    engine.Reset();
    followers.Reset();
    anchored = false;
    bypassDelay.clear();
    bypassPos = 0;
}

void CapicolaAudioProcessor::PullParameters()
{
    for (int p = 0; p < granular::kNumParams; p++)
        engine.SetParam((Param)p, values[(size_t)p]->load(std::memory_order_relaxed));

    /* Reverse mirrors the knob around noon; modulation still adds on top, as
     * the module's flip rewrites the stored knob value. */
    auto flag = [](std::atomic<float>* a) { return a->load(std::memory_order_relaxed) > 0.5f; };
    if (flag(pitchReverse))   engine.SetParam(Param::Pitch,   1.0f - engine.GetParam(Param::Pitch));
    if (flag(stretchReverse)) engine.SetParam(Param::Stretch, 1.0f - engine.GetParam(Param::Stretch));

    const bool on = flag(sliceButton);
    if (on && !sliceHeld) engine.TriggerSlice();
    sliceHeld = on;

    auto value = [](std::atomic<float>* a) { return (int)std::lround(a->load(std::memory_order_relaxed)); };
    midiEnabled = flag(midiParams.enabled);
    midiChannel = value(midiParams.channel);
    followerSettings.sliceNote = (uint8_t)value(midiParams.sliceNote);
    followerSettings.eventNote = (uint8_t)value(midiParams.eventNote);
    followerSettings.inEnvCc   = (uint8_t)value(midiParams.inEnvCc);
    followerSettings.outEnvCc  = (uint8_t)value(midiParams.outEnvCc);
    midiOut.SetEnabled(midiEnabled);
}

void CapicolaAudioProcessor::ObserveFollowers(int offset)
{
    followers.Observe(followerSettings, offset,
                      engine.SliceCount() + engine.ManualSliceCount(), engine.OutEventCount(),
                      engine.EnvNormIn(), engine.EnvNormOut(),
                      [this](const granular::FollowerEvent& e) {
                          if (midiEnabled && !isNonRealtime())   // no live MIDI during an export
                              midiOut.Push({ e, (uint8_t)midiChannel,
                                             blockStartMs + 1000.0 * e.offset / hostRate });
                      });
}

void CapicolaAudioProcessor::AnchorClock(int blockSamples)
{
    constexpr double kLateMs = 50.0;
    const double now      = juce::Time::getMillisecondCounterHiRes();
    const double expected = anchorMs + 1000.0 * sinceAnchor / hostRate;
    if (!anchored || now > expected + kLateMs)
    {
        anchored    = true;
        anchorMs    = now;
        sinceAnchor = 0.0;
    }
    blockStartMs = anchorMs + 1000.0 * sinceAnchor / hostRate;
    sinceAnchor += blockSamples;
}

void CapicolaAudioProcessor::Render(float* l, float* r, int start, int count)
{
    for (int end = start + count; start < end; start += kObserveSamples)
    {
        const int len = juce::jmin(kObserveSamples, end - start);
        engine.Process(l + start, r + start, l + start, r + start, len);
        ObserveFollowers(start + len);
    }
}

void CapicolaAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() < 2) return;

    float* l = buffer.getWritePointer(0);
    float* r = buffer.getWritePointer(1);
    if (getTotalNumInputChannels() == 1) std::copy(l, l + n, r);   // mono in feeds both channels

    AnchorClock(n);
    PullParameters();
    ObserveFollowers(0);   // a Slice press this block triggers at its start

    /* Each note-on slices at its own position in the block. */
    int pos = 0;
    for (const auto event : midi)
    {
        if (!event.getMessage().isNoteOn()) continue;
        const int at = juce::jlimit(pos, n, event.samplePosition);
        Render(l, r, pos, at - pos);
        pos = at;
        engine.TriggerSlice();
        ObserveFollowers(at);   // a manual slice triggers at once, as on the module
    }
    Render(l, r, pos, n - pos);
    followers.Advance(n);
    PublishUi();
}

void CapicolaAudioProcessor::PublishUi()
{
    constexpr auto relaxed = std::memory_order_relaxed;
    ui.inEnv.store(engine.EnvNormIn(), relaxed);
    ui.outEnv.store(engine.EnvNormOut(), relaxed);
    ui.inPeak.store(engine.InPeak(), relaxed);
    ui.slices.store(engine.SliceCount() + engine.ManualSliceCount(), relaxed);
    ui.events.store(engine.OutEventCount(), relaxed);
    for (int i = 0; i < granular::kNumPrimary; i++)
        ui.effective[(size_t)i].store(engine.EffectivePosition(i), relaxed);
}

void CapicolaAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int n   = buffer.getNumSamples();
    const int lat = getLatencySamples();
    if (n == 0 || buffer.getNumChannels() < 2) return;
    if (getTotalNumInputChannels() == 1) buffer.copyFrom(1, 0, buffer, 0, 0, n);
    if (lat <= 0) return;

    float* io[2]   = { buffer.getWritePointer(0), buffer.getWritePointer(1) };
    float* ring[2] = { bypassDelay.getWritePointer(0), bypassDelay.getWritePointer(1) };
    for (int i = 0; i < n; i++)
    {
        for (int c = 0; c < 2; c++)
        {
            const float x = io[c][i];
            io[c][i] = ring[c][bypassPos];
            ring[c][bypassPos] = x;
        }
        bypassPos = (bypassPos + 1) % lat;
    }
}

juce::AudioProcessorEditor* CapicolaAudioProcessor::createEditor()
{
    return new CapicolaEditor(*this);
}

void CapicolaAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state = parameters.copyState();
    state.setProperty("version", JucePlugin_VersionString, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void CapicolaAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CapicolaAudioProcessor();
}
