/**
 * @file Theme.h
 * @brief Colours and fonts for the interface, as in the design mockup
 *        (layout A). LED colours are the module's own (src/main.cpp).
 */

#pragma once

#include <juce_graphics/juce_graphics.h>

#include "BinaryData.h"

namespace theme {

/* Panel */
inline const juce::Colour kBackground { 0xff131417 };
inline const juce::Colour kHeader     { 0xff16171b };
inline const juce::Colour kRule       { 0xff24262b };
inline const juce::Colour kTabBar     { 0xff1b1c20 };
inline const juce::Colour kChip       { 0xff1f2025 };
inline const juce::Colour kOutline    { 0xff34363c };
inline const juce::Colour kText       { 0xffecebe7 };
inline const juce::Colour kMuted      { 0xffa4a4ac };
inline const juce::Colour kSubText    { 0xffc9c9cf };
inline const juce::Colour kLedOff     { 0xff2a2c31 };
inline const juce::Colour kCentreLine { 0xff4a4c53 };

/* LEDs: page and modulation-source colours */
inline const juce::Colour kOrange { 0xffff6e0f };   // Primary page, input follower
inline const juce::Colour kPink   { 0xffff3c96 };   // Depth page
inline const juce::Colour kBlue   { 0xff4b86ff };   // Routing page, output follower
inline const juce::Colour kTeal   { 0xff00d4c0 };   // Secondary page
inline const juce::Colour kWhite  { 0xfff2f2f2 };   // Mod In A
inline const juce::Colour kViolet { 0xffa27bff };   // Mod In B
inline const juce::Colour kGreen  { 0xff3fd47a };   // Mod In C, MIDI out on
inline const juce::Colour kPeach  { 0xffffb07a };   // orange text on dark

inline const juce::Colour kSourceColours[5] = { kOrange, kBlue, kWhite, kViolet, kGreen };

/* Fonts, embedded (plugin/resources/fonts) so the interface looks the same
 * everywhere. */
enum class Face { Sans, SansMedium, SansSemiBold, Mono, MonoMedium, Display };

inline juce::Typeface::Ptr Typeface(Face f)
{
    static juce::Typeface::Ptr faces[] = {
        juce::Typeface::createSystemTypefaceFor(BinaryData::IBMPlexSansRegular_ttf,  BinaryData::IBMPlexSansRegular_ttfSize),
        juce::Typeface::createSystemTypefaceFor(BinaryData::IBMPlexSansMedium_ttf,   BinaryData::IBMPlexSansMedium_ttfSize),
        juce::Typeface::createSystemTypefaceFor(BinaryData::IBMPlexSansSemiBold_ttf, BinaryData::IBMPlexSansSemiBold_ttfSize),
        juce::Typeface::createSystemTypefaceFor(BinaryData::IBMPlexMonoRegular_ttf,  BinaryData::IBMPlexMonoRegular_ttfSize),
        juce::Typeface::createSystemTypefaceFor(BinaryData::IBMPlexMonoMedium_ttf,   BinaryData::IBMPlexMonoMedium_ttfSize),
        juce::Typeface::createSystemTypefaceFor(BinaryData::SpaceGroteskBold_ttf,    BinaryData::SpaceGroteskBold_ttfSize),
    };
    return faces[(int)f];
}

/** A font at a CSS-style pixel size, with optional letter spacing in ems. */
inline juce::Font Font(Face f, float px, float tracking = 0.0f)
{
    return juce::Font(juce::FontOptions(Typeface(f)).withPointHeight(px)).withExtraKerningFactor(tracking);
}

} // namespace theme
