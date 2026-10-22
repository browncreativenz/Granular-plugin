/**
 * @file PluginEditor.cpp
 * @brief See PluginEditor.h. Coordinates follow the mockup's layout.
 */

#include "PluginEditor.h"

#include "Params.h"

using widgets::RingMode;

namespace {

/* Columns and rows of the layout, in px. */
constexpr int kHeaderH = 68, kFooterH = 36;
constexpr int kLeftW = 224, kRightW = 240;
constexpr int kBodyTop = kHeaderH + 1, kBodyBottom = CapicolaEditor::kHeight - kFooterH;

constexpr uint32_t kLampMs = 160;

/* juce::String reads a plain literal as Latin-1, so non-ASCII text goes
 * through fromUTF8. */
const juce::String kDot   = juce::String::fromUTF8(" \xc2\xb7 ");   // " · "
const juce::String kMinus = juce::String::fromUTF8("\xe2\x88\x92");  // "−"

struct PageInfo
{
    const char*  name;
    juce::Colour colour;
    const char*  hint;
};

const PageInfo kPages[CapicolaEditor::kNumPages] = {
    { "Primary",   theme::kOrange, "The six main controls. Rings follow the modulated position." },
    { "Depth",     theme::kPink,   "How hard each control is modulated. Noon is off." },
    { "Routing",   theme::kBlue,   "Where each control takes its modulation from." },
    { "Secondary", theme::kTeal,   "Deeper engine settings: smoothing, fade, drive, mix." },
};

struct KnobInfo
{
    const char* id;
    const char* label;
    RingMode    mode;
};

const KnobInfo kKnobs[CapicolaEditor::kNumPages][granular::kNumPrimary] = {
    { { "pitch", "PITCH", RingMode::Bipolar }, { "stretch", "STRETCH", RingMode::Bipolar },
      { "threshold", "THRESHOLD", RingMode::Unipolar }, { "grain", "GRAIN SIZE", RingMode::Unipolar },
      { "quality", "QUALITY", RingMode::Unipolar }, { "feedback", "FEEDBACK", RingMode::Unipolar } },
    { { "depth1", "PITCH", RingMode::Bipolar }, { "depth2", "STRETCH", RingMode::Bipolar },
      { "depth3", "THRESHOLD", RingMode::Bipolar }, { "depth4", "GRAIN SIZE", RingMode::Bipolar },
      { "depth5", "QUALITY", RingMode::Bipolar }, { "depth6", "FEEDBACK", RingMode::Bipolar } },
    { { "src1", "PITCH", RingMode::Routing }, { "src2", "STRETCH", RingMode::Routing },
      { "src3", "THRESHOLD", RingMode::Routing }, { "src4", "GRAIN SIZE", RingMode::Routing },
      { "src5", "QUALITY", RingMode::Routing }, { "src6", "FEEDBACK", RingMode::Routing } },
    { { "smoothing", "SMOOTHING", RingMode::Unipolar }, { "fade", "FADE", RingMode::Unipolar },
      { "drive", "DRIVE", RingMode::Unipolar }, { "character", "CHARACTER", RingMode::Bipolar },
      { "mix", "MIX", RingMode::Unipolar }, { "fbBand", "FEEDBACK TONE", RingMode::Unipolar } },
};

const char* const kShortSources[granular::kNumModSources] = { "input", "output", "Mod In A", "Mod In B", "Mod In C" };

void SectionLabel(juce::Graphics& g, const juce::String& text, int x, int y)
{
    g.setColour(theme::kMuted);
    g.setFont(theme::Font(theme::Face::SansSemiBold, 11, 0.14f));
    g.drawText(text, x, y, 200, 14, juce::Justification::centredLeft);
}

void Lamp(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour c, bool lit)
{
    g.setColour(lit ? c : c.withAlpha(0.0f));
    g.fillRoundedRectangle(r, 4);
    g.setColour(lit ? c : theme::kOutline);
    g.drawRoundedRectangle(r.reduced(0.5f), 4, 1);
    g.setColour(lit ? theme::kBackground : theme::kMuted);
    g.setFont(theme::Font(theme::Face::Mono, 10, 0.1f));
    g.drawText(text, r, juce::Justification::centred);
}

} // namespace

CapicolaEditor::CapicolaEditor(CapicolaAudioProcessor& p)
    : juce::AudioProcessorEditor(p), processor(p)
{
    setLookAndFeel(&lookAndFeel);
    setOpaque(true);

    /* Header */
    midiChip.onText = "MIDI out" + kDot + processor.MidiOut().PortName();
    addAndMakeVisible(midiChip);
    buttonAttachments.push_back(std::make_unique<juce::ButtonParameterAttachment>(Param("midiOut"), midiChip));

    /* Pages and knobs */
    for (int i = 0; i < kNumPages; i++)
    {
        tabs[(size_t)i] = std::make_unique<widgets::TabButton>(kPages[i].name, kPages[i].colour);
        tabs[(size_t)i]->onClick = [this, i] { if (tabs[(size_t)i]->getToggleState()) SetPage(i); };
        addAndMakeVisible(*tabs[(size_t)i]);
    }
    for (auto& k : knobs) addAndMakeVisible(k);

    /* Performance */
    slice.onStateChange = [this] { SetSlicePressed(slice.isDown()); };
    addAndMakeVisible(slice);
    addAndMakeVisible(pitchReverse);
    addAndMakeVisible(stretchReverse);
    buttonAttachments.push_back(std::make_unique<juce::ButtonParameterAttachment>(Param("pitchRev"), pitchReverse));
    buttonAttachments.push_back(std::make_unique<juce::ButtonParameterAttachment>(Param("stretchRev"), stretchReverse));

    /* Mod inputs */
    modA.Attach(Param("modA"));
    modB.Attach(Param("modB"));
    modC.Attach(Param("modC"));
    addAndMakeVisible(modA);
    addAndMakeVisible(modB);
    addAndMakeVisible(modC);

    /* MIDI settings: one item per parameter step, in order. */
    for (int ch = 1; ch <= 16; ch++) midiChannel.addItem(juce::String(ch), ch);
    for (int n = 0; n < 128; n++)
    {
        const auto name = juce::MidiMessage::getMidiNoteName(n, true, true, 3);
        sliceNote.addItem(name, n + 1);
        eventNote.addItem(name, n + 1);
        inEnvCc.addItem("CC " + juce::String(n), n + 1);
        outEnvCc.addItem("CC " + juce::String(n), n + 1);
    }
    const std::pair<juce::ComboBox*, const char*> combos[] = {
        { &midiChannel, "midiChannel" }, { &sliceNote, "midiSliceNote" }, { &eventNote, "midiEventNote" },
        { &inEnvCc, "midiInEnvCc" }, { &outEnvCc, "midiOutEnvCc" },
    };
    for (const auto& [box, id] : combos)
    {
        box->setTitle(Param(id).getName(64));
        addAndMakeVisible(*box);
        comboAttachments.push_back(std::make_unique<juce::ComboBoxParameterAttachment>(Param(id), *box));
    }

    lastSlices = processor.Ui().slices.load();
    lastEvents = processor.Ui().events.load();

    const int saved = (int)processor.parameters.state.getProperty("uiPage", (int)Primary);
    SetPage(juce::jlimit(0, kNumPages - 1, saved));

    setSize(kWidth, kHeight);
    setResizable(false, false);
    startTimerHz(30);
}

CapicolaEditor::~CapicolaEditor()
{
    stopTimer();
    if (slice.isDown()) SetSlicePressed(false);
    setLookAndFeel(nullptr);
}

juce::RangedAudioParameter& CapicolaEditor::Param(const char* id)
{
    auto* p = processor.parameters.getParameter(id);
    jassert(p != nullptr);
    return *p;
}

void CapicolaEditor::SetPage(int newPage)
{
    page = newPage;
    tabs[(size_t)page]->setToggleState(true, juce::dontSendNotification);
    for (int i = 0; i < granular::kNumPrimary; i++)
    {
        const KnobInfo& k = kKnobs[page][i];
        knobs[(size_t)i].Attach(Param(k.id), k.mode, kPages[page].colour, k.label);
    }
    processor.parameters.state.setProperty("uiPage", page, nullptr);
    timerCallback();
    repaint();
}

void CapicolaEditor::SetSlicePressed(bool down)
{
    /* The module slices on release; the parameter slices on each off → on
     * change, so a press is on, the release off. */
    auto& p = Param("slice");
    if ((p.getValue() > 0.5f) == down) return;
    p.beginChangeGesture();
    p.setValueNotifyingHost(down ? 1.0f : 0.0f);
    p.endChangeGesture();
}

void CapicolaEditor::timerCallback()
{
    const auto& ui  = processor.Ui();
    const uint32_t now = juce::Time::getMillisecondCounter();

    inEnv  = ui.inEnv.load(std::memory_order_relaxed);
    outEnv = ui.outEnv.load(std::memory_order_relaxed);

    const uint32_t slices = ui.slices.load(std::memory_order_relaxed);
    const uint32_t events = ui.events.load(std::memory_order_relaxed);
    if (slices != lastSlices) { lastSlices = slices; sliceLampUntil = now + kLampMs; }
    if (events != lastEvents) { lastEvents = events; eventLampUntil = now + kLampMs; }
    sliceLamp = (int32_t)(sliceLampUntil - now) > 0;
    eventLamp = (int32_t)(eventLampUntil - now) > 0;
    slice.SetFlash(sliceLamp);

    for (int i = 0; i < granular::kNumPrimary; i++)
    {
        float        eff = -1.0f;
        juce::String sub;
        if (page == Primary)
        {
            eff = ui.effective[(size_t)i].load(std::memory_order_relaxed);
            auto& depthParam  = Param(kKnobs[Depth][i].id);
            auto& sourceParam = Param(kKnobs[Routing][i].id);
            const float depth = granular::taper::Depth(depthParam.getValue());   // normalised = knob position
            if (std::abs(depth) >= 0.005f)
            {
                const int src = juce::jlimit(0, granular::kNumModSources - 1,
                                             (int)std::lround(sourceParam.convertFrom0to1(sourceParam.getValue())));
                sub = juce::String("mod  ") + kShortSources[src] + " "
                    + (depth >= 0 ? juce::String("+") : kMinus)
                    + juce::String(std::abs(depth), 2);
            }
            else
            {
                eff = -1.0f;   // unmodulated: no ghost segment
            }
        }
        knobs[(size_t)i].SetLive(inEnv, eff, sub);
    }

    repaint(0, kBodyTop, kLeftW, 240);   // meters and lamps
}

void CapicolaEditor::paint(juce::Graphics& g)
{
    g.fillAll(theme::kBackground);

    /* Header */
    g.setColour(theme::kHeader);
    g.fillRect(0, 0, kWidth, kHeaderH);
    g.setColour(theme::kRule);
    g.fillRect(0, kHeaderH, kWidth, 1);
    const auto title = theme::Font(theme::Face::Display, 22, 0.22f);
    g.setColour(theme::kText);
    g.setFont(title);
    g.drawText("CAPICOLA", 28, 0, 220, kHeaderH, juce::Justification::centredLeft);
    const float tw = juce::GlyphArrangement::getStringWidth(title, "CAPICOLA");
    g.setColour(theme::kMuted);
    g.setFont(theme::Font(theme::Face::Sans, 13));
    g.drawText("Transient-locked keyframe time stretcher", 28 + (int)tw + 16, 2, 400, kHeaderH,
               juce::Justification::centredLeft);

    /* Column rules and footer */
    g.setColour(theme::kRule);
    g.fillRect(kLeftW, kBodyTop, 1, kBodyBottom - kBodyTop);
    g.fillRect(kWidth - kRightW, kBodyTop, 1, kBodyBottom - kBodyTop);
    g.fillRect(0, kBodyBottom, kWidth, 1);

    const double rate = processor.getSampleRate();
    const int    lat  = processor.getLatencySamples();
    juce::String status = "Engine 48 kHz";
    if (rate > 0)
        status = "Host " + juce::String(rate / 1000.0, std::fmod(rate, 1000.0) < 0.5 ? 0 : 1) + " kHz"
               + kDot + "engine 48 kHz" + kDot + "latency " + juce::String(lat) + " samples ("
               + juce::String(1000.0 * lat / rate, 1) + " ms)";
    g.setColour(theme::kMuted);
    g.setFont(theme::Font(theme::Face::Mono, 12));
    g.drawText(status, 28, kBodyBottom + 1, 700, kFooterH - 1, juce::Justification::centredLeft);
    g.drawText("v" + juce::String(JucePlugin_VersionString) + kDot + "browncreativenz", kWidth - 328,
               kBodyBottom + 1, 300, kFooterH - 1, juce::Justification::centredRight);

    /* Left: followers */
    const int lx = 20, lw = kLeftW - 40;
    SectionLabel(g, "FOLLOWERS", lx, kBodyTop + 22);
    auto meter = [&](int y, const juce::String& name, float level, juce::Colour c, const juce::String& lamp, bool lit) {
        g.setColour(theme::kText);
        g.setFont(theme::Font(theme::Face::Sans, 13));
        g.drawText(name, lx, y, 120, 20, juce::Justification::centredLeft);
        Lamp(g, { (float)(lx + lw - 52), (float)y + 1, 52, 18 }, lamp, c, lit);
        g.setColour(theme::kRule);
        g.fillRoundedRectangle((float)lx, (float)y + 28, (float)lw, 10, 5);
        const float fw = juce::jlimit(0.0f, 1.0f, level) * lw;
        if (fw > 0.5f)
        {
            g.setColour(c);
            g.fillRoundedRectangle((float)lx, (float)y + 28, juce::jmax(fw, 10.0f), 10, 5);
        }
    };
    meter(kBodyTop + 54, "Input", inEnv, theme::kOrange, "SLICE", sliceLamp);
    meter(kBodyTop + 110, "Output", outEnv, theme::kBlue, "EVENT", eventLamp);
    SectionLabel(g, "REVERSE", lx, kBodyTop + 256);

    /* Centre: tab bar, page hint */
    const int cx = kLeftW + 28, cw = kWidth - kLeftW - kRightW - 56;
    g.setColour(theme::kTabBar);
    g.fillRoundedRectangle((float)cx, (float)kBodyTop + 18, (float)cw, 54, 10);
    g.setColour(theme::kMuted);
    g.setFont(theme::Font(theme::Face::Sans, 13));
    g.drawText(kPages[page].hint, cx, kBodyTop + 84, cw, 18, juce::Justification::centredLeft);

    /* Right: mod inputs, MIDI out */
    const int rx = kWidth - kRightW + 20, rw = kRightW - 40;
    SectionLabel(g, "MOD INPUTS", rx, kBodyTop + 22);
    g.setColour(theme::kRule);
    g.fillRect(rx, kBodyTop + 206, rw, 1);
    SectionLabel(g, "MIDI OUT", rx, kBodyTop + 226);
    const char* rows[] = { "Port", "Channel", "Slice note", "Event note", "Input env", "Output env" };
    g.setFont(theme::Font(theme::Face::Sans, 13));
    for (int i = 0; i < 6; i++)
    {
        g.setColour(theme::kMuted);
        g.drawText(rows[i], rx, kBodyTop + 254 + i * 30, 100, 24, juce::Justification::centredLeft);
    }
    g.setColour(theme::kText);
    g.setFont(theme::Font(theme::Face::Mono, 13));
    g.drawText(processor.MidiOut().PortName(), rx, kBodyTop + 254, rw - 2, 24, juce::Justification::centredRight);
}

void CapicolaEditor::resized()
{
    midiChip.setBounds(kWidth - 28 - 210, (kHeaderH - 36) / 2, 210, 36);

    /* Left */
    const int lx = 20, lw = kLeftW - 40;
    slice.setBounds(lx, kBodyTop + 166, lw, 72);
    const int half = (lw - 8) / 2;
    pitchReverse.setBounds(lx, kBodyTop + 280, half, 44);
    stretchReverse.setBounds(lx + half + 8, kBodyTop + 280, lw - half - 8, 44);

    /* Centre */
    const int cx = kLeftW + 28, cw = kWidth - kLeftW - kRightW - 56;
    const int tabW = (cw - 10 - 18) / kNumPages;
    for (int i = 0; i < kNumPages; i++)
        tabs[(size_t)i]->setBounds(cx + 5 + i * (tabW + 6), kBodyTop + 23, tabW, 44);
    const int gridTop = kBodyTop + 116, cellW = (cw - 24) / 3, rowH = 192 + 18;
    for (int i = 0; i < granular::kNumPrimary; i++)
        knobs[(size_t)i].setBounds(cx + (i % 3) * (cellW + 12), gridTop + (i / 3) * rowH, cellW, widgets::RingKnob::kHeight);

    /* Right */
    const int rx = kWidth - kRightW + 20, rw = kRightW - 40;
    modA.setBounds(rx, kBodyTop + 50, rw, 36);
    modB.setBounds(rx, kBodyTop + 100, rw, 36);
    modC.setBounds(rx, kBodyTop + 150, rw, 36);
    juce::ComboBox* boxes[] = { &midiChannel, &sliceNote, &eventNote, &inEnvCc, &outEnvCc };
    for (int i = 0; i < 5; i++)
        boxes[i]->setBounds(rx + rw - 100, kBodyTop + 254 + (i + 1) * 30, 100, 24);
}
