#include "NeonLookAndFeel.h"

NeonLookAndFeel::NeonLookAndFeel()
{
    using namespace juce;

    setColour (ResizableWindow::backgroundColourId, Theme::background);
    setColour (Label::textColourId, Theme::text);
    setColour (Slider::textBoxTextColourId, Theme::text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Slider::textBoxHighlightColourId, Theme::cyan.withAlpha (0.4f));
    setColour (Slider::rotarySliderFillColourId, Theme::cyan);
    setColour (Slider::rotarySliderOutlineColourId, Theme::cellOffBeat);
    setColour (TextButton::buttonColourId, Theme::panelRaised);
    setColour (TextButton::buttonOnColourId, Theme::cyan);
    setColour (TextButton::textColourOffId, Theme::text);
    setColour (TextButton::textColourOnId, Theme::ink);
    setColour (ComboBox::backgroundColourId, Theme::panelRaised);
    setColour (ComboBox::textColourId, Theme::text);
    setColour (ComboBox::outlineColourId, Theme::cyan);
    setColour (ComboBox::arrowColourId, Theme::cyan);
    setColour (PopupMenu::backgroundColourId, Theme::panel);
    setColour (PopupMenu::textColourId, Theme::text);
    setColour (PopupMenu::highlightedBackgroundColourId, Theme::cyan.withAlpha (0.25f));
    setColour (PopupMenu::highlightedTextColourId, Theme::text);
    setColour (TextEditor::backgroundColourId, Theme::panelRaised);
    setColour (TextEditor::textColourId, Theme::text);
    setColour (TextEditor::highlightColourId, Theme::cyan.withAlpha (0.4f));
    setColour (CaretComponent::caretColourId, Theme::cyan);
}

void NeonLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                        float startAngle, float endAngle, juce::Slider& slider)
{
    using namespace juce;

    const auto accent = slider.findColour (Slider::rotarySliderFillColourId);
    const auto bounds = Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
    const auto radius = jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const auto arcRadius = radius - 4.0f;
    const auto angle = startAngle + sliderPos * (endAngle - startAngle);

    Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (Theme::cellOffBeat);
    g.strokePath (track, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));

    if (sliderPos > 0.001f)
    {
        Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);

        // Neon tube: wide faint strokes under a thin bright core.
        for (auto [strokeWidth, alpha] : { std::pair { 12.0f, 0.10f }, { 8.0f, 0.18f }, { 4.0f, 1.0f } })
        {
            g.setColour (accent.withAlpha (alpha));
            g.strokePath (value, PathStrokeType (strokeWidth, PathStrokeType::curved, PathStrokeType::rounded));
        }
    }

    const auto knobRadius = arcRadius * 0.66f;
    g.setGradientFill (ColourGradient (Theme::panelRaised.brighter (0.25f), centre.x, centre.y - knobRadius,
                                       Theme::panel, centre.x, centre.y + knobRadius, false));
    g.fillEllipse (Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre));
    g.setColour (Theme::outline);
    g.drawEllipse (Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre), 1.0f);

    const auto tip = centre.getPointOnCircumference (knobRadius * 0.8f, angle);
    const auto base = centre.getPointOnCircumference (knobRadius * 0.25f, angle);
    g.setColour (accent);
    g.drawLine ({ base, tip }, 3.0f);
}

void NeonLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                        float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    using namespace juce;

    if (style != Slider::LinearBar)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minPos, maxPos, style, slider);
        return;
    }

    const auto accent = slider.findColour (Slider::rotarySliderFillColourId);
    const auto area = Rectangle<int> (x, y, width, height).toFloat().reduced (1.0f);

    g.setColour (Theme::panelRaised);
    g.fillRoundedRectangle (area, 6.0f);
    g.setColour (accent.withAlpha (0.3f));
    g.fillRoundedRectangle (area.withRight (jlimit (area.getX(), area.getRight(), sliderPos)), 6.0f);
    g.setColour (accent.withAlpha (0.8f));
    g.drawRoundedRectangle (area, 6.0f, 1.5f);
}

juce::Label* NeonLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (Theme::font (slider.getSliderStyle() == juce::Slider::LinearBar ? 18.0f : 12.0f));
    label->setColour (juce::Label::textColourId, Theme::text);
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return label;
}

void NeonLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                            bool highlighted, bool down)
{
    const auto accent = button.findColour (juce::TextButton::buttonOnColourId);
    const auto area = button.getLocalBounds().toFloat().reduced (2.0f);
    const auto corner = 7.0f;
    const bool on = button.getToggleState();

    if (on)
    {
        Theme::glow (g, area, accent, 6.0f, corner);
        g.setColour (down ? accent.darker (0.2f) : accent);
        g.fillRoundedRectangle (area, corner);
        return;
    }

    g.setColour (down ? Theme::panelRaised.brighter (0.15f) : Theme::panelRaised);
    g.fillRoundedRectangle (area, corner);

    if (highlighted)
        Theme::glow (g, area, accent, 4.0f, corner, 0.6f);

    g.setColour (accent.withAlpha (highlighted ? 0.95f : 0.5f));
    g.drawRoundedRectangle (area, corner, 1.5f);
}

void NeonLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (button.getToggleState() ? Theme::ink : Theme::text);
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (4, 2), juce::Justification::centred, 2);
}

juce::Font NeonLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return Theme::font (juce::jmin (15.0f, static_cast<float> (buttonHeight) * 0.45f));
}

void NeonLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    using namespace juce;

    const auto accent = box.findColour (ComboBox::outlineColourId);
    auto area = Rectangle<int> (0, 0, width, height).toFloat().reduced (1.0f);

    g.setColour (Theme::panelRaised);
    g.fillRoundedRectangle (area, 6.0f);
    g.setColour (accent.withAlpha (box.isMouseOver (true) ? 0.95f : 0.55f));
    g.drawRoundedRectangle (area, 6.0f, 1.5f);

    const auto arrowArea = area.removeFromRight (20.0f).reduced (6.0f, area.getHeight() * 0.36f);
    Path arrow;
    arrow.addTriangle (arrowArea.getX(), arrowArea.getY(), arrowArea.getRight(), arrowArea.getY(),
                       arrowArea.getCentreX(), arrowArea.getBottom());
    g.setColour (accent);
    g.fillPath (arrow);
}

juce::Font NeonLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return Theme::font (juce::jmin (15.0f, static_cast<float> (box.getHeight()) * 0.5f));
}

void NeonLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 26, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Font NeonLookAndFeel::getPopupMenuFont()
{
    return Theme::font (15.0f, false);
}

void NeonLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::outline);
    g.drawRect (0, 0, width, height, 1);
}

juce::Font NeonLookAndFeel::getLabelFont (juce::Label& label)
{
    return label.getFont();
}
