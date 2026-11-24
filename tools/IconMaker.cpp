// Draws μBeat's app icon on the macOS icon grid as icon-1024.png. CMakeLists.txt builds the .icns
// from it. Usage: IconMaker <folder>

#include "ui/Theme.h"

namespace
{
constexpr int size = 1024;

void glowStroke (juce::Graphics& g, const juce::Path& path, juce::Colour colour, float width)
{
    for (int i = 30; i >= 1; --i)
    {
        g.setColour (colour.withAlpha (0.012f));
        g.strokePath (path, juce::PathStrokeType (width + static_cast<float> (i) * 2.5f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }
}

juce::Image drawIcon()
{
    juce::Image image (juce::Image::ARGB, size, size, true);
    juce::Graphics g (image);

    // Apple's grid: an 824 px rounded square centred on the 1024 px canvas.
    const juce::Rectangle<float> tile (100.0f, 100.0f, 824.0f, 824.0f);
    constexpr float corner = 185.0f;

    juce::Path tilePath;
    tilePath.addRoundedRectangle (tile, corner);
    juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 40, { 0, 14 }).drawForPath (g, tilePath);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1c1540), tile.getCentreX(), tile.getY(),
                                             Theme::background, tile.getCentreX(), tile.getBottom(), false));
    g.fillPath (tilePath);

    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (tilePath);

        juce::Path border;
        border.addRoundedRectangle (tile.reduced (14.0f), corner - 14.0f);
        g.setGradientFill (juce::ColourGradient (Theme::magenta, tile.getX(), tile.getY(), Theme::cyan, tile.getRight(),
                                                 tile.getBottom(), false));
        g.strokePath (border, juce::PathStrokeType (8.0f));
    }

    // The lower-case mu, as in the wordmark.
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (Theme::font (620.0f), juce::String::fromUTF8 ("\xce\xbc"), 0.0f, 0.0f);
    juce::Path mu;
    glyphs.createPath (mu);
    const auto textArea = tile.reduced (190.0f);
    mu.applyTransform (mu.getTransformToScaleToFit (textArea, true));

    glowStroke (g, mu, Theme::magenta, 0.0f);
    const auto muBounds = mu.getBounds();
    g.setGradientFill (juce::ColourGradient (Theme::magenta, muBounds.getX(), muBounds.getY(), Theme::cyan, muBounds.getRight(),
                                             muBounds.getBottom(), false));
    g.fillPath (mu);

    return image;
}
} // namespace

static bool writePng (const juce::Image& image, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream stream (file);
    juce::PNGImageFormat png;
    return stream.openedOk() && png.writeImageToStream (image, stream);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    if (argc < 2)
    {
        std::cerr << "Usage: IconMaker <folder>\n";
        return 1;
    }

    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[1]));
    folder.createDirectory();

    const auto file = folder.getChildFile ("icon-1024.png");
    if (! writePng (drawIcon(), file))
    {
        std::cerr << "Couldn't write " << file.getFullPathName() << "\n";
        return 1;
    }

    return 0;
}
