/**
 * @file editor_snapshot.cpp
 * @brief Renders the plugin window to PNG, one image per page, after playing
 *        a few seconds of the test signal through the plugin so the meters
 *        and rings show live values. Settings match the design mockup's
 *        examples.
 *
 *   editor_snapshot <output-dir>
 */

#include <cstdio>

#include <juce_events/juce_events.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "TestSignal.h"

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: editor_snapshot <output-dir>\n"); return 2; }
    juce::ScopedJuceInitialiser_GUI juce;
    const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    dir.createDirectory();

    constexpr int kRate = 44100, kBlock = 512;
    CapicolaAudioProcessor plugin;
    plugin.setPlayConfigDetails(2, 2, kRate, kBlock);

    auto set = [&](const char* id, float v) {
        auto* p = plugin.parameters.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(v));
    };
    set("depth2", 0.8f);    // Stretch +0.60 from the input follower
    set("depth3", 0.35f);   // Threshold −0.30 ...
    set("src3", 1.0f);      // ... from the output follower
    set("feedback", 0.25f);
    set("depth6", 0.62f);   // Feedback +0.24 ...
    set("src6", 2.0f);      // ... from Mod In A
    set("src5", 3.0f);
    set("modA", 0.71f);
    set("modB", 0.425f);
    plugin.prepareToPlay(kRate, kBlock);

    /* Play until a moment just after a hit, so the meters are up. */
    granular::TestSignal sig((float)kRate);
    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer midi;
    const int blocks = (int)(2.03 * kRate / kBlock);
    for (int b = 0; b < blocks; b++)
    {
        for (int i = 0; i < kBlock; i++)
        {
            float l, r;
            sig.Next(l, r);
            buf.setSample(0, i, l);
            buf.setSample(1, i, r);
        }
        plugin.processBlock(buf, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> base(plugin.createEditor());
    auto* editor = dynamic_cast<CapicolaEditor*>(base.get());
    if (editor == nullptr) { std::fprintf(stderr, "not a CapicolaEditor\n"); return 1; }

    const char* names[] = { "primary", "depth", "routing", "secondary" };
    for (int page = 0; page < CapicolaEditor::kNumPages; page++)
    {
        editor->SetPage(page);
        const juce::Image image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        const juce::File out = dir.getChildFile(juce::String("capicola-") + names[page] + ".png");
        out.deleteFile();
        juce::FileOutputStream stream(out);
        juce::PNGImageFormat png;
        if (!stream.openedOk() || !png.writeImageToStream(image, stream))
        {
            std::fprintf(stderr, "cannot write %s\n", out.getFullPathName().toRawUTF8());
            return 1;
        }
        std::printf("%s\n", out.getFullPathName().toRawUTF8());
    }
    editor->SetPage(0);
    return 0;
}
