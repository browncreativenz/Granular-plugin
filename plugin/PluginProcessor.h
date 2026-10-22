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
#include "FollowerMidiOut.h"
#include "FollowerOut.h"

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

    /** The follower MIDI output (port name, and the queue tests read). */
    FollowerMidiOut& MidiOut() { return midiOut; }

    /** What the interface shows, published by the audio thread at the end of
     *  each block and read by the editor's timer. */
    struct UiState
    {
        std::atomic<float>    inEnv  { 0.0f };   // followers, 0..1
        std::atomic<float>    outEnv { 0.0f };
        std::atomic<float>    inPeak { 0.0f };   // input level, for the clip lamp
        std::atomic<uint32_t> slices { 0 };      // every slice taken, detected or manual
        std::atomic<uint32_t> events { 0 };      // output-follower events
        std::array<std::atomic<float>, granular::kNumPrimary> effective {};   // modulated knob positions
    };
    const UiState& Ui() const { return ui; }

  private:
    static juce::AudioProcessorValueTreeState::ParameterLayout CreateLayout();

    /** Copy host parameter values into the engine; edge-detect Slice. */
    void PullParameters();

    /** Process [start, start + count) in short pieces, checking the followers
     *  after each so MIDI events land close to where they happen. */
    void Render(float* l, float* r, int start, int count);
    void ObserveFollowers(int offset);

    granular::CapicolaProcessor engine;
    UiState ui;
    void PublishUi();

    std::array<std::atomic<float>*, granular::kNumParams> values {};
    std::atomic<float>* sliceButton = nullptr;
    std::atomic<float>* pitchReverse = nullptr;
    std::atomic<float>* stretchReverse = nullptr;
    bool sliceHeld = false;

    /* Follower MIDI output. */
    struct MidiParams
    {
        std::atomic<float>* enabled = nullptr;
        std::atomic<float>* channel = nullptr;
        std::atomic<float>* sliceNote = nullptr;
        std::atomic<float>* eventNote = nullptr;
        std::atomic<float>* inEnvCc = nullptr;
        std::atomic<float>* outEnvCc = nullptr;
    } midiParams;
    granular::FollowerOut           followers;
    granular::FollowerOut::Settings followerSettings;
    FollowerMidiOut                 midiOut;
    bool   midiEnabled  = false;
    int    midiChannel  = 1;
    double hostRate     = 48000.0;

    /* MIDI send times follow the audio sample clock, anchored to the wall
     * clock, so the host's scheduling jitter doesn't reach the MIDI timing.
     * The anchor moves only when processing falls behind (a dropout, or
     * transport restarting after a pause). */
    void   AnchorClock(int blockSamples);
    double blockStartMs = 0.0;
    double anchorMs     = 0.0;
    double sinceAnchor  = 0.0;    // samples since anchorMs
    bool   anchored     = false;

    /* Bypass keeps the dry signal on the same latency as processing, so
     * toggling bypass doesn't shift the audio in time. */
    juce::AudioBuffer<float> bypassDelay;
    int bypassPos = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CapicolaAudioProcessor)
};
