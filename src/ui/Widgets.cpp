#include "Widgets.h"
#include "../ParamIds.h"
#include "../SynthTracks.h"

juce::String formatParameterValue (const juce::RangedAudioParameter& param, double value)
{
    const auto unit = param.getLabel();
    const auto range = param.getNormalisableRange();

    if (unit == "Hz" && value >= 1000.0)
        return juce::String (value / 1000.0, value >= 10000.0 ? 1 : 2) + " kHz";

    if (unit == "s")
        return value < 1.0 ? juce::String (juce::roundToInt (value * 1000.0)) + " ms" : juce::String (value, 2) + " s";

    if (unit == "dB" || unit == "st")
        return juce::String (value, 1) + " " + unit;

    if (unit.isEmpty() && range.start >= 0.0f && range.end <= 1.0f)
        return juce::String (juce::roundToInt (value * 100.0)) + "%";

    const auto magnitude = std::abs (value);
    const auto decimals = magnitude >= 100.0 ? 0 : (magnitude >= 10.0 ? 1 : 2);
    return juce::String (value, decimals) + (unit.isEmpty() ? juce::String() : " " + unit);
}

double parseParameterValue (const juce::RangedAudioParameter& param, const juce::String& text)
{
    const auto unit = param.getLabel();
    const auto range = param.getNormalisableRange();
    auto value = text.retainCharacters ("-0123456789.").getDoubleValue();

    if (text.containsIgnoreCase ("khz"))
        value *= 1000.0;
    else if (unit == "s" && text.containsIgnoreCase ("ms"))
        value /= 1000.0;
    else if (unit.isEmpty() && range.start >= 0.0f && range.end <= 1.0f)
        value /= 100.0;

    return juce::jlimit (static_cast<double> (range.start), static_cast<double> (range.end), value);
}

void useFriendlyText (juce::Slider& slider, const juce::RangedAudioParameter& param)
{
    slider.textFromValueFunction = [&param] (double value) { return formatParameterValue (param, value); };
    slider.valueFromTextFunction = [&param] (const juce::String& text) { return parseParameterValue (param, text); };
    slider.updateText();
}

Knob::Knob (APVTS& stateToUse, const juce::String& paramId, const juce::String& caption, juce::Colour accent)
    : state (stateToUse)
{
    label.setText (caption, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (Theme::font (12.0f));
    label.setColour (juce::Label::textColourId, Theme::textDim);
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);

    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 16);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    addAndMakeVisible (slider);

    setAccent (accent);
    attachTo (paramId);
}

void Knob::attachTo (const juce::String& paramId)
{
    attachment.reset();
    attachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, slider);

    if (auto* param = state.getParameter (paramId))
        useFriendlyText (slider, *param);
}

void Knob::setAccent (juce::Colour accent)
{
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    repaint();
}

void Knob::resized()
{
    auto area = getLocalBounds();
    label.setBounds (area.removeFromTop (16));
    slider.setBounds (area);
}

Choice::Choice (APVTS& stateToUse, const juce::String& paramId, const juce::String& caption, juce::Colour accent)
    : state (stateToUse)
{
    label.setText (caption, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centredLeft);
    label.setFont (Theme::font (12.0f));
    label.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (label);

    if (auto* param = state.getParameter (paramId))
        box.addItemList (param->getAllValueStrings(), 1);

    box.setColour (juce::ComboBox::outlineColourId, accent);
    box.setColour (juce::ComboBox::arrowColourId, accent);
    addAndMakeVisible (box);
    attachTo (paramId);
}

void Choice::attachTo (const juce::String& paramId)
{
    attachment.reset();
    attachment = std::make_unique<APVTS::ComboBoxAttachment> (state, paramId, box);
}

void Choice::resized()
{
    auto area = getLocalBounds();
    label.setBounds (area.removeFromTop (16));
    box.setBounds (area.removeFromTop (juce::jmin (30, area.getHeight())));
}

ParamToggle::ParamToggle (APVTS& state, const juce::String& paramId, const juce::String& text, juce::Colour accent)
{
    setButtonText (text);
    setClickingTogglesState (true);
    setColour (juce::TextButton::buttonOnColourId, accent);
    attachTo (state, paramId);
}

void ParamToggle::attachTo (APVTS& state, const juce::String& paramId)
{
    attachment.reset();
    attachment = std::make_unique<APVTS::ButtonAttachment> (state, paramId, *this);
}

juce::Rectangle<float> StepGrid::cellBounds (int row, int column) const
{
    const auto rows = numRows();
    const auto cellWidth = static_cast<float> (getWidth()) / Seq::numSteps;
    const auto cellHeight = static_cast<float> (getHeight()) / static_cast<float> (rows);
    const auto displayRow = highRowsAtTop ? rows - 1 - row : row;
    return { static_cast<float> (column) * cellWidth, static_cast<float> (displayRow) * cellHeight, cellWidth, cellHeight };
}

std::optional<std::pair<int, int>> StepGrid::cellAt (juce::Point<float> position) const
{
    const auto rows = numRows();
    if (! getLocalBounds().toFloat().contains (position) || rows <= 0)
        return std::nullopt;

    const auto column = static_cast<int> (position.x / (static_cast<float> (getWidth()) / Seq::numSteps));
    auto displayRow = static_cast<int> (position.y / (static_cast<float> (getHeight()) / static_cast<float> (rows)));
    displayRow = juce::jlimit (0, rows - 1, displayRow);
    const auto row = highRowsAtTop ? rows - 1 - displayRow : displayRow;
    return std::pair { row, juce::jlimit (0, Seq::numSteps - 1, column) };
}

void StepGrid::paint (juce::Graphics& g)
{
    const auto rows = numRows();
    const auto activeLength = length ? length() : Seq::numSteps;
    const auto playColumn = playhead ? playhead() : -1;
    const auto cellWidth = static_cast<float> (getWidth()) / Seq::numSteps;

    // Shade every other group of four steps so beats are easy to count.
    for (int group = 0; group < Seq::numSteps / 4; group += 2)
    {
        g.setColour (juce::Colours::white.withAlpha (0.025f));
        g.fillRect (juce::Rectangle<float> (static_cast<float> (group * 4) * cellWidth, 0.0f, cellWidth * 4.0f,
                                            static_cast<float> (getHeight())));
    }

    if (playColumn >= 0)
    {
        const juce::Rectangle<float> column (static_cast<float> (playColumn) * cellWidth, 0.0f, cellWidth,
                                             static_cast<float> (getHeight()));
        g.setColour (Theme::text.withAlpha (0.10f));
        g.fillRoundedRectangle (column, 4.0f);
    }

    if (noteMode)
    {
        paintNotes (g, rows, activeLength, playColumn);
        return;
    }

    for (int row = 0; row < rows; ++row)
    {
        const auto colour = rowColour (row);

        for (int column = 0; column < Seq::numSteps; ++column)
        {
            const auto cell = cellBounds (row, column).reduced (2.5f);
            const bool inactive = column >= activeLength;
            const auto corner = juce::jmin (5.0f, cell.getHeight() * 0.3f);

            if (isOn (row, column))
            {
                const bool hit = column == playColumn;
                const auto lit = hit ? colour.brighter (0.7f) : colour;

                if (! inactive)
                    Theme::glow (g, cell, lit, hit ? 9.0f : 5.0f, corner, hit ? 1.6f : 1.0f);

                g.setColour (inactive ? lit.withAlpha (0.3f) : lit);
                g.fillRoundedRectangle (cell, corner);
            }
            else
            {
                const auto off = column % 4 == 0 ? Theme::cellOffBeat : Theme::cellOff;
                g.setColour (inactive ? off.withAlpha (0.35f) : off);
                g.fillRoundedRectangle (cell, corner);
            }
        }
    }
}

void StepGrid::paintNotes (juce::Graphics& g, int rows, int activeLength, int playColumn)
{
    for (int row = 0; row < rows; ++row)
    {
        for (int column = 0; column < Seq::numSteps; ++column)
        {
            const auto cell = cellBounds (row, column).reduced (2.5f);
            const auto off = column % 4 == 0 ? Theme::cellOffBeat : Theme::cellOff;
            g.setColour (column >= activeLength ? off.withAlpha (0.35f) : off);
            g.fillRoundedRectangle (cell, juce::jmin (5.0f, cell.getHeight() * 0.3f));
        }
    }

    // Each note is one bar from its first step to its last.
    for (int row = 0; row < rows; ++row)
    {
        const auto colour = rowColour (row);

        for (int column = 0; column < Seq::numSteps; ++column)
        {
            const auto noteLength = noteLengthAt (row, column);
            if (noteLength <= 0)
                continue;

            const auto last = column + noteLength - 1;
            const auto bar = cellBounds (row, column).getUnion (cellBounds (row, last)).reduced (2.5f);
            const auto corner = juce::jmin (5.0f, bar.getHeight() * 0.3f);
            const bool inactive = column >= activeLength;
            const bool sounding = playColumn >= column && playColumn <= last;
            const auto lit = sounding ? colour.brighter (0.7f) : colour;

            if (! inactive)
                Theme::glow (g, bar, lit, sounding ? 9.0f : 5.0f, corner, sounding ? 1.6f : 1.0f);

            g.setColour (inactive ? lit.withAlpha (0.3f) : lit);
            g.fillRoundedRectangle (bar, corner);

            // A brighter cap on the first step shows where each note starts.
            if (noteLength > 1)
            {
                g.setColour (juce::Colours::white.withAlpha (0.35f));
                g.fillRoundedRectangle (cellBounds (row, column).reduced (2.5f).withWidth (4.0f), 2.0f);
            }

            column = last;
        }
    }
}

void StepGrid::mouseDown (const juce::MouseEvent& event)
{
    if (noteMode)
    {
        if (const auto cell = cellAt (event.position))
        {
            const auto [row, column] = *cell;
            const auto start = noteStartAt (row, column);
            noteDrag = { row, start >= 0 ? start : column, 1, column, start >= 0, false };

            if (noteDrag.existing)
                noteDrag.length = noteLengthAt (row, start);
            else
                setNote (row, column, 1);

            repaint();
        }
        return;
    }

    if (const auto cell = cellAt (event.position))
    {
        paintValue = ! isOn (cell->first, cell->second);
        setCell (cell->first, cell->second, paintValue);
        lastCell = *cell;
        repaint();
    }
}

void StepGrid::mouseDrag (const juce::MouseEvent& event)
{
    if (noteMode)
    {
        if (noteDrag.row < 0)
            return;

        const auto cellWidth = static_cast<float> (getWidth()) / Seq::numSteps;
        const auto column = juce::jlimit (0, Seq::numSteps - 1, static_cast<int> (event.position.x / cellWidth));
        if (column != noteDrag.pressedColumn)
            noteDrag.moved = true;

        const auto newLength = std::max (1, column - noteDrag.start + 1);
        if (noteDrag.moved && newLength != noteDrag.length)
        {
            noteDrag.length = newLength;
            setNote (noteDrag.row, noteDrag.start, newLength);
            repaint();
        }
        return;
    }

    if (const auto cell = cellAt (event.position); cell && *cell != lastCell)
    {
        if (isOn (cell->first, cell->second) != paintValue)
            setCell (cell->first, cell->second, paintValue);

        lastCell = *cell;
        repaint();
    }
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    if (noteMode && noteDrag.existing && ! noteDrag.moved)
    {
        removeNote (noteDrag.row, noteDrag.start);
        repaint();
    }

    noteDrag.row = -1;
}

void SongBar::paint (juce::Graphics& g)
{
    const auto slotWidth = static_cast<float> (getWidth()) / Seq::songLength;
    const auto playing = playingSlot ? playingSlot() : -1;
    const bool active = bank.isSongMode();

    for (int slot = 0; slot < Seq::songLength; ++slot)
    {
        const auto area = juce::Rectangle<float> (static_cast<float> (slot) * slotWidth, 0.0f, slotWidth,
                                                  static_cast<float> (getHeight()))
                              .reduced (2.0f);
        const auto pattern = bank.getSongSlot (slot);

        if (pattern < 0)
        {
            g.setColour (Theme::cellOff);
            g.fillRoundedRectangle (area, 5.0f);
            g.setColour (Theme::textDim.withAlpha (0.4f));
            g.setFont (Theme::font (11.0f, false));
            g.drawText (juce::String (slot + 1), area, juce::Justification::centred);
            continue;
        }

        const auto colour = Theme::patternColour (pattern).withMultipliedAlpha (active ? 1.0f : 0.45f);
        if (slot == playing)
            Theme::glow (g, area, colour, 7.0f, 5.0f, 1.5f);

        g.setColour (slot == playing ? colour : colour.withMultipliedAlpha (0.75f));
        g.fillRoundedRectangle (area, 5.0f);
        g.setColour (Theme::ink);
        g.setFont (Theme::font (14.0f));
        g.drawText (juce::String::charToString (static_cast<juce::juce_wchar> ('A' + pattern)), area,
                    juce::Justification::centred);
    }
}

void SongBar::mouseDown (const juce::MouseEvent& event)
{
    const auto slot = juce::jlimit (0, Seq::songLength - 1,
                                    static_cast<int> (event.position.x / (static_cast<float> (getWidth()) / Seq::songLength)));

    if (event.mods.isPopupMenu())
    {
        bank.setSongSlot (slot, -1);
    }
    else
    {
        const auto current = bank.getSongSlot (slot);
        bank.setSongSlot (slot, current + 1 >= Seq::numPatterns ? -1 : current + 1);
    }

    repaint();
}

bool DrumPad::decayFlash()
{
    if (flashLevel <= 0.0f)
        return false;

    flashLevel = flashLevel < 0.05f ? 0.0f : flashLevel * 0.75f;
    repaint();
    return true;
}

void DrumPad::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (3.0f);

    if (flashLevel > 0.0f)
        Theme::glow (g, area, colour, 10.0f, 7.0f, 2.0f * flashLevel);

    g.setColour (Theme::panelRaised.interpolatedWith (colour, 0.6f * flashLevel));
    g.fillRoundedRectangle (area, 7.0f);

    g.setColour (colour);
    g.fillRoundedRectangle (area.withWidth (5.0f), 2.5f);

    if (selected)
    {
        g.setColour (colour);
        g.drawRoundedRectangle (area, 7.0f, 2.0f);
    }

    g.setColour (flashLevel > 0.5f ? Theme::ink : Theme::text);
    g.setFont (Theme::font (14.0f));
    g.drawText (name.toUpperCase(), area.withTrimmedLeft (14.0f), juce::Justification::centredLeft);
}

void DrumPad::mouseDown (const juce::MouseEvent&)
{
    if (onTap)
        onTap();
}

void NeonKeyboard::drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                  juce::Colour, juce::Colour)
{
    auto key = area.reduced (1.0f, 0.0f).withTrimmedBottom (2.0f);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff24243a), key.getX(), key.getY(), juce::Colour (0xff17172a),
                                             key.getX(), key.getBottom(), false));
    g.fillRoundedRectangle (key, 3.0f);

    if (isDown)
    {
        g.setColour (Theme::cyan.withAlpha (0.85f));
        g.fillRoundedRectangle (key, 3.0f);
        Theme::glow (g, key.withTrimmedTop (key.getHeight() * 0.6f), Theme::cyan, 6.0f, 3.0f, 1.2f);
    }
    else if (isOver)
    {
        g.setColour (Theme::cyan.withAlpha (0.18f));
        g.fillRoundedRectangle (key, 3.0f);
    }

    if (note % 12 == 0)
    {
        g.setColour (isDown ? Theme::ink : Theme::textDim);
        g.setFont (Theme::font (11.0f));
        g.drawText (juce::MidiMessage::getMidiNoteName (note, true, true, 3), key.removeFromBottom (18.0f),
                    juce::Justification::centred);
    }
}

void NeonKeyboard::drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour)
{
    const auto key = area.reduced (1.0f, 0.0f);

    g.setColour (juce::Colour (0xff06060c));
    g.fillRoundedRectangle (key, 3.0f);

    if (isDown)
    {
        Theme::glow (g, key, Theme::magenta, 5.0f, 3.0f, 1.2f);
        g.setColour (Theme::magenta);
        g.fillRoundedRectangle (key, 3.0f);
        return;
    }

    g.setColour (isOver ? Theme::magenta.withAlpha (0.5f) : Theme::outline);
    g.drawRoundedRectangle (key.reduced (0.5f), 3.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRoundedRectangle (key.reduced (3.0f).withHeight (key.getHeight() * 0.7f), 2.0f);
}

PatternButton::PatternButton (int patternIndex) : index (patternIndex)
{
    setButtonText (juce::String::charToString (static_cast<juce::juce_wchar> ('A' + patternIndex)));
    setColour (juce::TextButton::buttonOnColourId, Theme::patternColour (patternIndex));
}

void PatternButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    juce::TextButton::paintButton (g, highlighted, down);

    const auto colour = Theme::patternColour (index);
    auto area = getLocalBounds().toFloat().reduced (8.0f, 5.0f);

    if (hasContent && ! getToggleState())
    {
        g.setColour (colour);
        g.fillEllipse (area.removeFromTop (6.0f).withSizeKeepingCentre (5.0f, 5.0f).translated (0.0f, -1.0f));
    }

    if (playing)
    {
        const auto bar = getLocalBounds().toFloat().removeFromBottom (4.0f).reduced (8.0f, 0.0f);
        Theme::glow (g, bar, colour, 4.0f, 2.0f, 1.5f);
        g.setColour (getToggleState() ? Theme::ink : colour);
        g.fillRoundedRectangle (bar, 2.0f);
    }
}

TransportButton::TransportButton (Kind buttonKind) : juce::Button ({}), kind (buttonKind)
{
    setClickingTogglesState (false);
}

void TransportButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto on = getToggleState();
    const auto colour = kind == Kind::play ? Theme::lime : (kind == Kind::record ? Theme::red : Theme::orange);
    const auto area = getLocalBounds().toFloat().reduced (3.0f);

    if (on || highlighted)
        Theme::glow (g, area, colour, on ? 9.0f : 5.0f, 10.0f, on ? 1.4f : 0.6f);

    g.setColour (on ? colour : (down ? Theme::panelRaised.brighter (0.2f) : Theme::panelRaised));
    g.fillRoundedRectangle (area, 10.0f);
    g.setColour (colour.withAlpha (on ? 1.0f : 0.7f));
    g.drawRoundedRectangle (area, 10.0f, 1.8f);

    const auto icon = area.withSizeKeepingCentre (area.getHeight() * 0.42f, area.getHeight() * 0.42f);
    g.setColour (on ? Theme::ink : colour);

    if (kind == Kind::record)
    {
        g.fillEllipse (icon);
    }
    else if (kind == Kind::metronome)
    {
        // A metronome: tapered body with a swinging arm.
        const auto body = icon.expanded (2.0f, 3.0f);
        juce::Path shape;
        shape.addTriangle (body.getCentreX(), body.getY(), body.getRight(), body.getBottom(), body.getX(), body.getBottom());
        g.strokePath (shape, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.drawLine (body.getCentreX(), body.getBottom() - 3.0f, body.getRight() + 1.0f, body.getY() + 2.0f, 2.2f);
    }
    else if (on)
    {
        g.fillRoundedRectangle (icon, 2.0f); // stop square while playing
    }
    else
    {
        juce::Path triangle;
        triangle.addTriangle (icon.getX() + 2.0f, icon.getY(), icon.getRight() + 2.0f, icon.getCentreY(),
                              icon.getX() + 2.0f, icon.getBottom());
        g.fillPath (triangle);
    }
}

TrackBar::TrackBar (APVTS& state)
{
    for (int track = 0; track < Seq::numSynthTracks; ++track)
    {
        auto& mute = mutes[static_cast<size_t> (track)];
        mute = std::make_unique<ParamToggle> (state, ParamIds::synth (track, ParamIds::mute), "M", Theme::red);
        mute->setTooltip ("Mute the " + juce::String (SynthTracks::tracks[static_cast<size_t> (track)].name).toLowerCase()
                          + " pattern");
        addAndMakeVisible (*mute);

        auto& solo = solos[static_cast<size_t> (track)];
        solo = std::make_unique<ParamToggle> (state, ParamIds::synth (track, ParamIds::solo), "S", Theme::yellow);
        solo->setTooltip ("Solo the " + juce::String (SynthTracks::tracks[static_cast<size_t> (track)].name).toLowerCase()
                          + " pattern: only soloed parts play");
        addAndMakeVisible (*solo);
    }
}

void TrackBar::setSelected (int track)
{
    if (track != selected)
    {
        selected = track;
        repaint();
    }
}

juce::Rectangle<int> TrackBar::trackArea (int track) const
{
    constexpr int gap = 10;
    const auto width = (getWidth() - gap * (Seq::numSynthTracks - 1)) / Seq::numSynthTracks;
    return { track * (width + gap), 0, width, getHeight() };
}

void TrackBar::paint (juce::Graphics& g)
{
    for (int track = 0; track < Seq::numSynthTracks; ++track)
    {
        const auto colour = Theme::synthColour (track);
        const auto area = trackArea (track).toFloat().reduced (2.0f);
        const bool isSelected = track == selected;

        if (isSelected)
            Theme::glow (g, area, colour, 8.0f, 8.0f, 1.3f);

        g.setColour (isSelected ? colour : Theme::panelRaised);
        g.fillRoundedRectangle (area, 8.0f);
        g.setColour (colour.withAlpha (isSelected ? 1.0f : 0.6f));
        g.drawRoundedRectangle (area, 8.0f, 1.5f);

        if (! isSelected)
        {
            g.setColour (colour);
            g.fillRoundedRectangle (area.withWidth (5.0f), 2.5f);
        }

        g.setColour (isSelected ? Theme::ink : Theme::text);
        g.setFont (Theme::font (16.0f));
        g.drawText (juce::String (SynthTracks::tracks[static_cast<size_t> (track)].name).toUpperCase(),
                    area.withTrimmedLeft (16.0f), juce::Justification::centredLeft);
    }
}

void TrackBar::resized()
{
    for (int track = 0; track < Seq::numSynthTracks; ++track)
    {
        auto buttons = trackArea (track).reduced (8);
        solos[static_cast<size_t> (track)]->setBounds (buttons.removeFromRight (36));
        buttons.removeFromRight (6);
        mutes[static_cast<size_t> (track)]->setBounds (buttons.removeFromRight (36));
    }
}

void TrackBar::mouseDown (const juce::MouseEvent& event)
{
    for (int track = 0; track < Seq::numSynthTracks; ++track)
    {
        if (trackArea (track).contains (event.getPosition()))
        {
            setSelected (track);
            if (onSelect)
                onSelect (track);
            return;
        }
    }
}
