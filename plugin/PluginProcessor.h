/**
 * @file PluginProcessor.h
 * @brief The JUCE plugin: host parameters, MIDI slice triggers and state
 *        around granular::CapicolaProcessor.
 */

#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "CapicolaProcessor.h"

class CapicolaAudioProcessor : public juce::AudioProcessor
{
  public:
    CapicolaAudioProcessor();

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool   acceptsMidi() const override  { return true; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }

    /** A freeze can hold, and a stretch can keep playing, long after the input
     *  stops; this keeps hosts from cutting that off. */
    double getTailLengthSeconds() const override { return 60.0; }

    int  getNumPrograms() override                             { return 1; }
    int  getCurrentProgram() override                          { return 0; }
    void setCurrentProgram(int) override                       {}
    const juce::String getProgramName(int) override            { return {}; }
    void changeProgramName(int, const juce::String&) override  {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;

  private:
    static juce::AudioProcessorValueTreeState::ParameterLayout CreateLayout();

    /** Copy host parameter values into the engine; edge-detect Slice. */
    void PullParameters();
    void Render(float* l, float* r, int start, int count);

    granular::CapicolaProcessor engine;

    std::array<std::atomic<float>*, granular::kNumParams> values {};
    std::atomic<float>* sliceButton = nullptr;
    bool sliceHeld = false;

    /* Bypass keeps the dry signal on the same latency as processing, so
     * toggling bypass doesn't shift the audio in time. */
    juce::AudioBuffer<float> bypassDelay;
    int bypassPos = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CapicolaAudioProcessor)
};
