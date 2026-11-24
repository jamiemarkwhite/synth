#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Theme
{
inline const juce::Colour background { 0xff06060c };
inline const juce::Colour backgroundTop { 0xff0d0b1c };
inline const juce::Colour panel { 0xff0e0e1a };
inline const juce::Colour panelRaised { 0xff171728 };
inline const juce::Colour cellOff { 0xff181829 };
inline const juce::Colour cellOffBeat { 0xff222238 };
inline const juce::Colour outline { 0xff2b2b46 };
inline const juce::Colour text { 0xffecebff };
inline const juce::Colour textDim { 0xff8786ad };
inline const juce::Colour ink { 0xff05050b }; // text drawn on lit buttons

inline const juce::Colour cyan { 0xff00f0ff };
inline const juce::Colour magenta { 0xffff2bd6 };
inline const juce::Colour lime { 0xffb4ff1f };
inline const juce::Colour yellow { 0xffffe14d };
inline const juce::Colour orange { 0xffff8a1f };
inline const juce::Colour purple { 0xffa45bff };
inline const juce::Colour pink { 0xffff4f8b };
inline const juce::Colour mint { 0xff21ffb0 };
inline const juce::Colour red { 0xffff2e4d };

inline juce::Colour drumColour (int track)
{
    static const std::array<juce::Colour, 8> colours { magenta, cyan, yellow, lime, mint, orange, pink, purple };
    return colours[static_cast<size_t> (juce::jlimit (0, 7, track))];
}

inline const juce::Colour sky { 0xff4d9dff };

inline juce::Colour sampleColour (int slot)
{
    static const std::array<juce::Colour, 4> colours { orange, mint, pink, sky };
    return colours[static_cast<size_t> (juce::jlimit (0, 3, slot))];
}

inline juce::Colour synthColour (int track)
{
    static const std::array<juce::Colour, 5> colours { purple, mint, cyan, yellow, pink };
    return colours[static_cast<size_t> (juce::jlimit (0, 4, track))];
}

// A track's colour, shifting hue a little from low rows to high rows.
inline juce::Colour trackRowColour (int track, int row, int numRows)
{
    const auto t = numRows > 1 ? static_cast<float> (row) / static_cast<float> (numRows - 1) : 0.0f;
    return synthColour (track).withRotatedHue (-0.02f + 0.04f * t).withMultipliedBrightness (0.8f + 0.2f * t);
}

inline juce::Colour patternColour (int pattern)
{
    static const std::array<juce::Colour, 4> colours { cyan, magenta, lime, orange };
    return colours[static_cast<size_t> (juce::jlimit (0, 3, pattern))];
}

// Low notes purple, high notes cyan.
inline juce::Colour rowColour (int row, int numRows)
{
    const auto t = numRows > 1 ? static_cast<float> (row) / static_cast<float> (numRows - 1) : 0.0f;
    return juce::Colour::fromHSV (0.82f - 0.32f * t, 0.85f, 1.0f, 1.0f);
}

inline juce::Font font (float height, bool bold = true)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

// Soft neon halo: stacked, expanding translucent rounded rectangles.
inline void glow (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, float radius, float corner, float strength = 1.0f)
{
    constexpr int layers = 4;
    for (int i = layers; i >= 1; --i)
    {
        const auto grow = radius * static_cast<float> (i) / layers;
        g.setColour (colour.withAlpha (0.09f * strength));
        g.fillRoundedRectangle (area.expanded (grow), corner + grow);
    }
}

inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour accent, const juce::String& title = {})
{
    g.setColour (panel);
    g.fillRoundedRectangle (area, 10.0f);
    g.setColour (accent.withAlpha (0.35f));
    g.drawRoundedRectangle (area.reduced (0.5f), 10.0f, 1.2f);

    if (title.isNotEmpty())
    {
        g.setColour (accent);
        g.setFont (font (13.0f));
        g.drawText (title, area.reduced (14.0f, 8.0f).removeFromTop (16.0f), juce::Justification::topLeft);
    }
}
} // namespace Theme
