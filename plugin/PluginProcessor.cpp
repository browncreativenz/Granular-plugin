/**
 * @file PluginProcessor.cpp
 * @brief See PluginProcessor.h.
 */

#include "PluginProcessor.h"

#include <cmath>

#include "ParamText.h"

using granular::Param;

namespace {
constexpr const char* kSliceId = "slice";
}

CapicolaAudioProcessor::CapicolaAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "Capicola", CreateLayout())
{
    for (int p = 0; p < granular::kNumParams; p++)
        values[(size_t)p] = parameters.getRawParameterValue(granular::kParams[p].id);
    sliceButton = parameters.getRawParameterValue(kSliceId);
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

    bypassDelay.setSize(2, juce::jmax(1, engine.LatencySamples()));
    bypassDelay.clear();
    bypassPos = 0;

    /* Don't take a Slice that was left on as a fresh press. */
    sliceHeld = sliceButton->load() > 0.5f;
}

void CapicolaAudioProcessor::reset()
{
    engine.Reset();
    bypassDelay.clear();
    bypassPos = 0;
}

void CapicolaAudioProcessor::PullParameters()
{
    for (int p = 0; p < granular::kNumParams; p++)
        engine.SetParam((Param)p, values[(size_t)p]->load(std::memory_order_relaxed));

    const bool on = sliceButton->load(std::memory_order_relaxed) > 0.5f;
    if (on && !sliceHeld) engine.TriggerSlice();
    sliceHeld = on;
}

void CapicolaAudioProcessor::Render(float* l, float* r, int start, int count)
{
    if (count > 0)
        engine.Process(l + start, r + start, l + start, r + start, count);
}

void CapicolaAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() < 2) return;

    float* l = buffer.getWritePointer(0);
    float* r = buffer.getWritePointer(1);
    if (getTotalNumInputChannels() == 1) std::copy(l, l + n, r);   // mono in feeds both channels

    PullParameters();

    /* Each note-on slices at its own position in the block. */
    int pos = 0;
    for (const auto event : midi)
    {
        if (!event.getMessage().isNoteOn()) continue;
        const int at = juce::jlimit(pos, n, event.samplePosition);
        Render(l, r, pos, at - pos);
        pos = at;
        engine.TriggerSlice();
    }
    Render(l, r, pos, n - pos);
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
    return new juce::GenericAudioProcessorEditor(*this);
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
