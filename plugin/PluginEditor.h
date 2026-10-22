/**
 * @file PluginEditor.h
 * @brief The plugin window: layout A of the design mockup, fixed 1080×640.
 *
 *   header   name, MIDI out chip
 *   left     follower meters with slice/event lamps, Slice, Reverse toggles
 *   centre   page tabs (Primary, Depth, Routing, Secondary), six ring knobs
 *   right    Mod In A–C, MIDI out settings
 *   footer   sample rates, latency, version
 */

#pragma once

#include <array>
#include <memory>

#include "PluginProcessor.h"
#include "Widgets.h"

class CapicolaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
  public:
    static constexpr int kWidth = 1080, kHeight = 640;

    enum Page { Primary, Depth, Routing, Secondary, kNumPages };

    explicit CapicolaEditor(CapicolaAudioProcessor&);
    ~CapicolaEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Show a page (also remembered with the plugin's state). */
    void SetPage(int page);

  private:
    void timerCallback() override;
    void SetSlicePressed(bool down);
    juce::RangedAudioParameter& Param(const char* id);

    CapicolaAudioProcessor& processor;
    widgets::LookAndFeel    lookAndFeel;

    int page = Primary;

    widgets::ChipToggle midiChip;
    std::array<std::unique_ptr<widgets::TabButton>, kNumPages> tabs;
    std::array<widgets::RingKnob, granular::kNumPrimary>      knobs;

    widgets::SliceButton slice;
    widgets::PillToggle  pitchReverse { "Pitch" }, stretchReverse { "Stretch" };

    widgets::BipolarBar modA { "Mod In A", theme::kWhite };
    widgets::BipolarBar modB { "Mod In B", theme::kViolet };
    widgets::BipolarBar modC { "Mod In C", theme::kGreen };

    juce::ComboBox midiChannel, sliceNote, eventNote, inEnvCc, outEnvCc;

    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>>   buttonAttachments;
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> comboAttachments;

    /* Live state, refreshed by the timer. */
    float    inEnv = 0.0f, outEnv = 0.0f;
    uint32_t lastSlices = 0, lastEvents = 0;
    uint32_t sliceLampUntil = 0, eventLampUntil = 0;
    bool     sliceLamp = false, eventLamp = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CapicolaEditor)
};
