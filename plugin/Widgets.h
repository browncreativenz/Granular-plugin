/**
 * @file Widgets.h
 * @brief The interface's controls, drawn to match the design mockup.
 *
 * RingKnob is the module's pot and LED ring: 16 LEDs at 45-minute steps from
 * 7:30, 13 forming the arc to 4:30 and three pips at the bottom, the middle
 * one (6:00) showing the input follower (src/main.cpp, hardware.h).
 */

#pragma once

#include <cmath>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"

namespace widgets {

enum class RingMode { Unipolar, Bipolar, Routing };

/* ── RingKnob ─────────────────────────────────────────────────────────── */

class RingKnob : public juce::Slider
{
  public:
    static constexpr int kWidth = 178, kHeight = 192;

    RingKnob()
    {
        setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        setRotaryParameters(juce::degreesToRadians(-135.0f), juce::degreesToRadians(135.0f), true);
        setMouseDragSensitivity(220);
        setPopupDisplayEnabled(false, false, nullptr);
    }

    /** Point the knob at a parameter (changes with the page). */
    void Attach(juce::RangedAudioParameter& p, RingMode m, juce::Colour c, const juce::String& name)
    {
        attachment.reset();
        param  = &p;
        mode   = m;
        colour = c;
        label  = name;
        attachment = std::make_unique<juce::SliderParameterAttachment>(p, *this);
        setDoubleClickReturnValue(true, p.convertFrom0to1(p.getDefaultValue()));
        setTitle(name);
        repaint();
    }

    /** Live values from the engine: input follower for the 6:00 pip, the
     *  modulated position (negative: none) and the line under the value. */
    void SetLive(float pipLevel, float effective, const juce::String& subText)
    {
        if (std::abs(pipLevel - pip) < 0.01f && std::abs(effective - eff) < 0.002f && subText == sub) return;
        pip = pipLevel;
        eff = effective;
        sub = subText;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const float bx = (getWidth() - 132) * 0.5f;
        const float cx = bx + 66.0f, cy = 66.0f;
        const float pos = (float)valueToProportionOfLength(getValue());
        const int   src = (int)std::lround(getValue());
        const float knobPos = (mode == RingMode::Routing) ? (src + 0.5f) / 5.0f : pos;

        /* LEDs */
        const float R = 56.0f, D = 8.0f;
        const float t = pos * 12.0f, e = eff * 12.0f;
        for (int s = 0; s < 16; s++)
        {
            const float hour = 7.5f + 0.75f * s;
            const float a    = std::fmod(hour, 12.0f) / 12.0f * juce::MathConstants<float>::twoPi;
            const float x = cx + R * std::sin(a), y = cy - R * std::cos(a);
            juce::Colour c = theme::kLedOff;
            float alpha = 1.0f;
            bool  glow  = false;

            if (s <= 12)
            {
                if (mode == RingMode::Routing)
                {
                    if (s >= 1 && s <= 11)
                    {
                        const int z = juce::jmin(4, (s - 1) * 5 / 11);
                        c = theme::kSourceColours[z];
                        alpha = (z == src) ? 1.0f : 0.22f;
                        glow  = (z == src);
                    }
                }
                else
                {
                    /* The arc runs from its origin (7:30, or noon when
                     * bipolar) to a position. With modulation, LEDs both
                     * the knob and the modulated position reach are lit;
                     * those only one reaches are the dim white difference. */
                    const bool bi = (mode == RingMode::Bipolar);
                    auto reaches = [&](float v) {
                        const float lo = bi ? juce::jmin(6.0f, v) : 0.0f, hi = bi ? juce::jmax(6.0f, v) : v;
                        const bool atNoon = bi && std::abs(v - 6.0f) < 0.01f;
                        return s >= lo - 0.001f && s <= hi + 0.001f && !(atNoon && s != 6);
                    };
                    const bool byKnob = reaches(t), byMod = (eff >= 0.0f) ? reaches(e) : byKnob;
                    const bool on = byKnob && byMod;
                    if (on) { c = colour; glow = true; }
                    else if (byKnob || byMod) { c = juce::Colours::white; alpha = 0.55f; }
                    if (bi && s == 6 && !on && !(byKnob || byMod)) { c = colour; alpha = 0.35f; }
                }
            }
            else if (s == 14)
            {
                c = theme::kOrange;
                alpha = 0.18f + 0.82f * juce::jlimit(0.0f, 1.0f, pip);
                glow = pip > 0.05f;
            }

            if (glow)
            {
                g.setColour(c.withAlpha(0.30f * alpha));
                g.fillEllipse(x - D, y - D, 2 * D, 2 * D);
            }
            g.setColour(c.withAlpha(alpha));
            g.fillEllipse(x - D / 2, y - D / 2, D, D);
        }

        /* Knob */
        juce::Path body;
        body.addEllipse(cx - 36, cy - 36, 72, 72);
        juce::DropShadow(juce::Colours::black.withAlpha(0.55f), 14, { 0, 6 }).drawForPath(g, body);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3a3c43), cx - 11, cy - 14,
                                               juce::Colour(0xff1c1d21), cx + 30, cy + 30, true));
        g.fillPath(body);
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawEllipse(cx - 35.5f, cy - 35.5f, 71, 71, 1.0f);

        juce::Path pointer;
        pointer.addRoundedRectangle(cx - 2, cy - 36 + 7, 4, 20, 2);
        const float angle = juce::degreesToRadians(-135.0f + knobPos * 270.0f);
        g.setColour(theme::kText);
        g.fillPath(pointer, juce::AffineTransform::rotation(angle, cx, cy));

        /* Text */
        const int w = getWidth();
        g.setColour(theme::kMuted);
        g.setFont(theme::Font(theme::Face::SansSemiBold, 12, 0.12f));
        g.drawText(label, 0, 138, w, 16, juce::Justification::centred);
        g.setColour(theme::kText);
        g.setFont(theme::Font(theme::Face::MonoMedium, 15));
        g.drawText(param != nullptr ? param->getCurrentValueAsText() : juce::String(), 0, 156, w, 20,
                   juce::Justification::centred);
        if (sub.isNotEmpty())
        {
            g.setColour(theme::kSubText);
            g.setFont(theme::Font(theme::Face::Mono, 11));
            g.drawText(sub, 0, 178, w, 14, juce::Justification::centred);
        }
    }

  private:
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
    juce::RangedAudioParameter* param = nullptr;
    RingMode     mode   = RingMode::Unipolar;
    juce::Colour colour = theme::kOrange;
    juce::String label, sub;
    float pip = 0.0f, eff = -1.0f;
};

/* ── Page tab ─────────────────────────────────────────────────────────── */

class TabButton : public juce::Button
{
  public:
    TabButton(const juce::String& name, juce::Colour c) : juce::Button(name), colour(c)
    {
        setClickingTogglesState(true);
        setRadioGroupId(1);
    }

    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        const bool on = getToggleState();
        auto r = getLocalBounds().toFloat();
        if (on)
        {
            g.setColour(colour);
            g.fillRoundedRectangle(r, 7);
        }
        else if (over)
        {
            g.setColour(juce::Colours::white.withAlpha(0.04f));
            g.fillRoundedRectangle(r, 7);
        }
        const auto font = theme::Font(theme::Face::SansMedium, 14);
        const float tw = juce::GlyphArrangement::getStringWidth(font, getName());
        const float x0 = r.getCentreX() - (8 + 8 + tw) * 0.5f;
        g.setColour(on ? theme::kBackground : colour);
        g.fillEllipse(x0, r.getCentreY() - 4, 8, 8);
        g.setColour(on ? theme::kBackground : theme::kText);
        g.setFont(font);
        g.drawText(getName(), juce::Rectangle<float>(x0 + 16, r.getY(), tw + 4, r.getHeight()),
                   juce::Justification::centredLeft);
    }

  private:
    juce::Colour colour;
};

/* ── Slice ────────────────────────────────────────────────────────────── */

class SliceButton : public juce::Button
{
  public:
    SliceButton() : juce::Button("Slice") {}

    void SetFlash(bool on)
    {
        if (on == flash) return;
        flash = on;
        repaint();
    }

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        const bool lit = flash || down;
        auto r = getLocalBounds().toFloat().reduced(1);
        if (lit)
        {
            juce::Path p;
            p.addRoundedRectangle(r, 12);
            juce::DropShadow(theme::kOrange.withAlpha(0.6f), 24, {}).drawForPath(g, p);
        }
        g.setColour(lit ? theme::kOrange : theme::kOrange.withAlpha(over ? 0.14f : 0.08f));
        g.fillRoundedRectangle(r, 12);
        g.setColour(theme::kOrange);
        g.drawRoundedRectangle(r, 12, 2);
        g.setColour(lit ? theme::kBackground : theme::kPeach);
        g.setFont(theme::Font(theme::Face::Display, 18, 0.24f));
        g.drawText("SLICE", r, juce::Justification::centred);
    }

  private:
    bool flash = false;
};

/* ── Toggles ──────────────────────────────────────────────────────────── */

class PillToggle : public juce::ToggleButton
{
  public:
    explicit PillToggle(const juce::String& name) : juce::ToggleButton(name) {}

    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        const bool on = getToggleState();
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(on ? theme::kOrange.withAlpha(0.16f) : juce::Colours::white.withAlpha(over ? 0.04f : 0.0f));
        g.fillRoundedRectangle(r, 8);
        g.setColour(on ? theme::kOrange : theme::kOutline);
        g.drawRoundedRectangle(r, 8, 1);
        g.setColour(on ? theme::kPeach : theme::kText);
        g.setFont(theme::Font(theme::Face::SansMedium, 14));
        g.drawText(getButtonText(), r, juce::Justification::centred);
    }
};

class ChipToggle : public juce::ToggleButton
{
  public:
    ChipToggle() : juce::ToggleButton("MIDI out") {}

    juce::String onText = "MIDI out", offText = "MIDI out off";

    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        const bool on = getToggleState();
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(over ? theme::kChip.brighter(0.08f) : theme::kChip);
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        g.setColour(on ? juce::Colour(0xff3a5f3f) : theme::kOutline);
        g.drawRoundedRectangle(r, r.getHeight() * 0.5f, 1);
        const float dy = r.getCentreY() - 4;
        if (on)
        {
            g.setColour(theme::kGreen.withAlpha(0.35f));
            g.fillEllipse(r.getX() + 10, dy - 4, 16, 16);
        }
        g.setColour(on ? theme::kGreen : juce::Colour(0xff55575e));
        g.fillEllipse(r.getX() + 14, dy, 8, 8);
        g.setColour(theme::kText);
        g.setFont(theme::Font(theme::Face::Sans, 13));
        g.drawText(on ? onText : offText, r.withTrimmedLeft(32).withTrimmedRight(14),
                   juce::Justification::centredLeft);
    }
};

/* ── Mod In bar ───────────────────────────────────────────────────────── */

class BipolarBar : public juce::Slider
{
  public:
    BipolarBar(const juce::String& name, juce::Colour c) : label(name), colour(c)
    {
        setSliderStyle(juce::Slider::LinearHorizontal);
        setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        setTitle(name);
    }

    void Attach(juce::RangedAudioParameter& p)
    {
        param = &p;
        attachment = std::make_unique<juce::SliderParameterAttachment>(p, *this);
        setDoubleClickReturnValue(true, p.convertFrom0to1(p.getDefaultValue()));
    }

    void paint(juce::Graphics& g) override
    {
        const float w = (float)getWidth();
        g.setColour(colour);
        g.fillEllipse(0, 6, 8, 8);
        g.setColour(theme::kText);
        g.setFont(theme::Font(theme::Face::Sans, 13));
        g.drawText(label, 16, 0, 120, 20, juce::Justification::centredLeft);
        g.setFont(theme::Font(theme::Face::Mono, 13));
        g.drawText(param != nullptr ? param->getCurrentValueAsText() : juce::String(), 0, 0, (int)w, 20,
                   juce::Justification::centredRight);

        const float y = 26, v = (float)valueToProportionOfLength(getValue()) * 2.0f - 1.0f;
        g.setColour(theme::kRule);
        g.fillRoundedRectangle(0, y, w, 8, 4);
        const float x0 = w * 0.5f, x1 = w * 0.5f * (1.0f + v);
        g.setColour(colour);
        g.fillRoundedRectangle(juce::jmin(x0, x1), y, std::abs(x1 - x0), 8, 4);
        g.setColour(theme::kCentreLine);
        g.fillRect(x0 - 0.5f, y - 3, 1.0f, 14.0f);
    }

  private:
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
    juce::RangedAudioParameter* param = nullptr;
    juce::String label;
    juce::Colour colour;
};

/* ── Look and feel: the small value menus and their pop-ups ───────────── */

class LookAndFeel : public juce::LookAndFeel_V4
{
  public:
    LookAndFeel()
    {
        setColour(juce::PopupMenu::backgroundColourId, theme::kChip);
        setColour(juce::PopupMenu::textColourId, theme::kText);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, theme::kOrange.withAlpha(0.25f));
        setColour(juce::PopupMenu::highlightedTextColourId, theme::kText);
        setColour(juce::ComboBox::textColourId, theme::kText);
    }

    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        if (box.isMouseOver(true) || box.hasKeyboardFocus(false))
        {
            g.setColour(juce::Colours::white.withAlpha(0.05f));
            g.fillRoundedRectangle(0, 0, (float)w, (float)h, 4);
        }
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override { return theme::Font(theme::Face::Mono, 13); }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(box.getLocalBounds().withTrimmedRight(2));
        label.setFont(getComboBoxFont(box));
        label.setJustificationType(juce::Justification::centredRight);
    }

    juce::Font getPopupMenuFont() override { return theme::Font(theme::Face::Mono, 13); }
};

} // namespace widgets
