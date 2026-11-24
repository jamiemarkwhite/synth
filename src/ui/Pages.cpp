#include "Pages.h"

#include "../ParamIds.h"
#include "../Presets.h"
#include "../SynthTracks.h"

namespace
{
int choiceIndex (const SynthAudioProcessor& processor, const juce::String& paramId)
{
    return static_cast<int> (std::lround (processor.parameters.getRawParameterValue (paramId)->load()));
}

int patternLength (const SynthAudioProcessor& processor)
{
    return (choiceIndex (processor, ParamIds::steps) + 1) * 8;
}

// Only show the playhead on the pattern that is actually playing.
int playheadFor (const SynthAudioProcessor& processor)
{
    if (processor.groovebox.getPlayingPattern() != processor.bank.getSelectedPattern())
        return -1;

    return processor.groovebox.getCurrentStep();
}

void styleActionButton (juce::TextButton& button, juce::Colour accent)
{
    button.setColour (juce::TextButton::buttonOnColourId, accent);
}

void layoutRow (juce::Rectangle<int> area, std::initializer_list<juce::Component*> components, int gap = 8)
{
    const auto count = static_cast<int> (components.size());
    const auto width = (area.getWidth() - gap * (count - 1)) / count;

    for (auto* component : components)
    {
        component->setBounds (area.removeFromLeft (width));
        area.removeFromLeft (gap);
    }
}
} // namespace

//==============================================================================
DrumPage::DrumPage (SynthAudioProcessor& p, std::function<void()> beforeBigChange)
    : processor (p),
      level (p.parameters, ParamIds::drum (0, "Level"), "LEVEL", Theme::drumColour (0)),
      tune (p.parameters, ParamIds::drum (0, "Tune"), "TUNE", Theme::drumColour (0)),
      decay (p.parameters, ParamIds::drum (0, "Decay"), "DECAY", Theme::drumColour (0))
{
    for (int track = 0; track < Seq::numDrumTracks; ++track)
    {
        const auto colour = Theme::drumColour (track);
        auto& pad = pads[static_cast<size_t> (track)];
        pad = std::make_unique<DrumPad> (DrumMachine::trackName (track), colour);
        pad->onTap = [this, track]
        {
            processor.groovebox.previewDrum (track);
            selectTrack (track);
        };
        addAndMakeVisible (*pad);

        auto& mute = mutes[static_cast<size_t> (track)];
        mute = std::make_unique<ParamToggle> (p.parameters, ParamIds::drum (track, "Mute"), "M", Theme::red);
        mute->setTooltip ("Mute " + DrumMachine::trackName (track));
        addAndMakeVisible (*mute);

        auto& solo = solos[static_cast<size_t> (track)];
        solo = std::make_unique<ParamToggle> (p.parameters, ParamIds::drum (track, "Solo"), "S", Theme::yellow);
        solo->setTooltip ("Solo " + DrumMachine::trackName (track) + ": only soloed parts play");
        addAndMakeVisible (*solo);
    }

    grid.numRows = [] { return Seq::numDrumTracks; };
    grid.isOn = [this] (int row, int column)
    { return processor.bank.getDrumStep (processor.bank.getSelectedPattern(), row, column); };
    grid.setCell = [this] (int row, int column, bool on)
    {
        processor.bank.setDrumStep (processor.bank.getSelectedPattern(), row, column, on);
        if (on && ! processor.groovebox.isRunning())
            processor.groovebox.previewDrum (row);
    };
    grid.rowColour = [] (int row) { return Theme::drumColour (row); };
    grid.playhead = [this] { return playheadFor (processor); };
    grid.length = [this] { return patternLength (processor); };
    addAndMakeVisible (grid);

    trackLabel.setFont (Theme::font (22.0f));
    trackLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (trackLabel);

    for (auto* knob : { &level, &tune, &decay })
        addAndMakeVisible (*knob);

    styleActionButton (dice, Theme::yellow);
    styleActionButton (clear, Theme::red);
    dice.onClick = [this, beforeBigChange]
    {
        beforeBigChange();
        processor.randomiseDrums (processor.bank.getSelectedPattern());
    };
    clear.onClick = [this, beforeBigChange]
    {
        beforeBigChange();
        processor.bank.clearDrums (processor.bank.getSelectedPattern());
    };
    addAndMakeVisible (dice);
    addAndMakeVisible (clear);

    selectTrack (0);
}

void DrumPage::selectTrack (int track)
{
    selectedTrack = track;
    const auto colour = Theme::drumColour (track);

    for (int i = 0; i < Seq::numDrumTracks; ++i)
    {
        pads[static_cast<size_t> (i)]->selected = i == track;
        pads[static_cast<size_t> (i)]->repaint();
    }

    level.attachTo (ParamIds::drum (track, "Level"));
    tune.attachTo (ParamIds::drum (track, "Tune"));
    decay.attachTo (ParamIds::drum (track, "Decay"));

    for (auto* knob : { &level, &tune, &decay })
        knob->setAccent (colour);

    trackLabel.setText (DrumMachine::trackName (track).toUpperCase(), juce::dontSendNotification);
    trackLabel.setColour (juce::Label::textColourId, colour);
    repaint();
}

void DrumPage::refresh (const Frame& frame)
{
    for (int track = 0; track < Seq::numDrumTracks; ++track)
    {
        auto& pad = *pads[static_cast<size_t> (track)];
        if ((frame.drumHits & (1u << static_cast<unsigned> (track))) != 0)
            pad.flash();
        else
            pad.decayFlash();
    }

    if (frame.sequencerChanged)
        grid.repaint();
}

void DrumPage::paint (juce::Graphics& g)
{
    Theme::drawPanel (g, stripArea.toFloat(), Theme::drumColour (selectedTrack));
}

void DrumPage::resized()
{
    auto area = getLocalBounds();
    stripArea = area.removeFromBottom (120);
    area.removeFromBottom (10);

    auto padColumn = area.removeFromLeft (150);
    auto muteColumn = area.removeFromLeft (40);
    auto soloColumn = area.removeFromLeft (40);
    area.removeFromLeft (8);
    grid.setBounds (area);

    const auto rowHeight = area.getHeight() / Seq::numDrumTracks;
    for (int track = 0; track < Seq::numDrumTracks; ++track)
    {
        pads[static_cast<size_t> (track)]->setBounds (padColumn.removeFromTop (rowHeight));
        mutes[static_cast<size_t> (track)]->setBounds (muteColumn.removeFromTop (rowHeight).reduced (2, 6));
        solos[static_cast<size_t> (track)]->setBounds (soloColumn.removeFromTop (rowHeight).reduced (2, 6));
    }

    auto strip = stripArea.reduced (16, 8);
    trackLabel.setBounds (strip.removeFromLeft (200));
    auto buttons = strip.removeFromRight (360);
    layoutRow (buttons.withSizeKeepingCentre (buttons.getWidth(), 48), { &dice, &clear }, 12);
    strip.removeFromRight (20);
    layoutRow (strip.withWidth (std::min (strip.getWidth(), 360)), { &level, &tune, &decay }, 12);
}

//==============================================================================
// One sample slot: waveform (tap to play, drop a file on it), record button, reverse, mute, solo,
// clear, level and tune.
class SamplesPage::Card final : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    Card (SynthAudioProcessor& p, int slotIndex)
        : processor (p),
          slot (slotIndex),
          colour (Theme::sampleColour (slotIndex)),
          reverse (p.parameters, ParamIds::sample (slotIndex, "Reverse"), "REV", colour),
          mute (p.parameters, ParamIds::sample (slotIndex, "Mute"), "M", Theme::red),
          solo (p.parameters, ParamIds::sample (slotIndex, "Solo"), "S", Theme::yellow),
          level (p.parameters, ParamIds::sample (slotIndex, "Level"), "LEVEL", colour),
          tune (p.parameters, ParamIds::sample (slotIndex, "Tune"), "TUNE", colour)
    {
        record.setColour (juce::TextButton::buttonOnColourId, Theme::red);
        record.setTooltip ("Record from the mic. Press again to stop (it stops by itself after 5 seconds).");
        record.onClick = [this]
        {
            auto& sampler = processor.groovebox.getSampler();
            if (sampler.getRecordingSlot() == slot)
                sampler.stopRecording();
            else if (sampler.getRecordingSlot() < 0)
                sampler.startRecording (slot);
        };

        clear.setColour (juce::TextButton::buttonOnColourId, Theme::red);
        clear.setTooltip ("Empty this slot");
        clear.onClick = [this] { processor.groovebox.getSampler().clearSample (slot); };

        reverse.setTooltip ("Play backwards");
        reverse.onClick = [this] { repaint(); };
        mute.setTooltip ("Mute this sample in the pattern");
        solo.setTooltip ("Solo this sample: only soloed parts play");

        for (auto* component : std::initializer_list<juce::Component*> { &record, &reverse, &mute, &solo, &clear, &level, &tune })
            addAndMakeVisible (*component);
    }

    void update (float input, juce::uint32 hits)
    {
        auto& sampler = processor.groovebox.getSampler();
        const bool recordingHere = sampler.getRecordingSlot() == slot;

        record.setButtonText (recordingHere ? "STOP" : "REC");
        record.setToggleState (recordingHere, juce::dontSendNotification);
        record.setEnabled (recordingHere || sampler.getRecordingSlot() < 0);
        clear.setEnabled (sampler.hasSample (slot) && ! recordingHere);

        if ((hits & (1u << static_cast<unsigned> (Groovebox::sampleTrack (slot)))) != 0)
            flash = 1.0f;
        else
            flash = flash < 0.05f ? 0.0f : flash * 0.8f;

        if (message.isNotEmpty() && juce::Time::getMillisecondCounter() > messageUntil)
            message = {};

        inputLevel = input;
        repaint();
    }

    void showMessage (const juce::String& text)
    {
        message = text;
        messageUntil = juce::Time::getMillisecondCounter() + 3000;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        Theme::drawPanel (g, getLocalBounds().toFloat(), colour, "SAMPLE " + juce::String (slot + 1));

        auto& sampler = processor.groovebox.getSampler();
        const bool recordingHere = sampler.getRecordingSlot() == slot;
        const auto area = waveArea.toFloat();

        if (const auto version = sampler.getVersion(); version != lastVersion)
        {
            lastVersion = version;
            buildPeaks();
        }

        if (sampler.hasSample (slot))
        {
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (12.0f, false));
            g.drawText (juce::String (sampler.getSampleSeconds (slot), 1) + " s",
                        getLocalBounds().reduced (14, 8).removeFromTop (16), juce::Justification::topRight);
        }

        if (flash > 0.0f || dragging)
            Theme::glow (g, area, dragging ? Theme::text : colour, 10.0f, 8.0f, dragging ? 1.0f : 2.0f * flash);
        if (recordingHere)
            Theme::glow (g, area, Theme::red, 10.0f, 8.0f, 1.2f);

        g.setColour (Theme::panelRaised.interpolatedWith (colour, 0.25f * flash));
        g.fillRoundedRectangle (area, 8.0f);

        if (recordingHere)
        {
            const auto progress = sampler.getRecordingProgress();
            g.setColour (Theme::red.withAlpha (0.25f));
            g.fillRoundedRectangle (area.withWidth (area.getWidth() * progress), 8.0f);
            g.setColour (Theme::text);
            g.setFont (Theme::font (16.0f));
            g.drawText ("RECORDING...", area.withTrimmedBottom (18.0f), juce::Justification::centred);
            drawMeter (g, area);
        }
        else if (! peaks.empty())
        {
            const bool backwards = processor.parameters.getRawParameterValue (ParamIds::sample (slot, "Reverse"))->load() >= 0.5f;
            const auto inner = area.reduced (8.0f, 10.0f);
            const auto columns = static_cast<int> (peaks.size());
            const auto width = inner.getWidth() / static_cast<float> (columns);

            g.setColour (colour);
            for (int i = 0; i < columns; ++i)
            {
                const auto peak = peaks[static_cast<size_t> (backwards ? columns - 1 - i : i)];
                const auto height = std::max (1.0f, peak * inner.getHeight());
                g.fillRect (inner.getX() + static_cast<float> (i) * width, inner.getCentreY() - height / 2.0f, std::max (1.0f, width - 1.0f), height);
            }
        }
        else
        {
            g.setColour (Theme::text);
            g.setFont (Theme::font (14.0f));
            g.drawText ("PRESS REC AND MAKE A SOUND", area.withTrimmedBottom (30.0f), juce::Justification::centred);
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (12.0f, false));
            g.drawText ("or drop a sound file here", area.withTrimmedTop (14.0f).withTrimmedBottom (18.0f),
                        juce::Justification::centred);
            drawMeter (g, area);
        }

        if (message.isNotEmpty())
        {
            g.setColour (Theme::background.withAlpha (0.85f));
            g.fillRoundedRectangle (area, 8.0f);
            g.setColour (Theme::yellow);
            g.setFont (Theme::font (14.0f));
            g.drawFittedText (message, area.reduced (10.0f).toNearestInt(), juce::Justification::centred, 2);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12).withTrimmedTop (22);
        waveArea = area.removeFromTop (96);
        area.removeFromTop (10);

        auto buttons = area.removeFromTop (34);
        record.setBounds (buttons.removeFromLeft (buttons.getWidth() / 3));
        buttons.removeFromLeft (6);
        const auto small = (buttons.getWidth() - 18) / 5;
        reverse.setBounds (buttons.removeFromLeft (small));
        buttons.removeFromLeft (6);
        mute.setBounds (buttons.removeFromLeft (small));
        buttons.removeFromLeft (6);
        solo.setBounds (buttons.removeFromLeft (small));
        buttons.removeFromLeft (6);
        clear.setBounds (buttons);

        area.removeFromTop (6);
        layoutRow (area, { &level, &tune }, 12);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (waveArea.contains (event.getPosition()) && processor.groovebox.getSampler().hasSample (slot))
            processor.groovebox.previewSample (slot);
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return files.size() == 1 && juce::File (files[0]).hasFileExtension ("wav;aif;aiff;mp3;flac;ogg;m4a;caf");
    }

    void fileDragEnter (const juce::StringArray&, int, int) override { dragging = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragging = false; repaint(); }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dragging = false;
        if (! processor.loadSampleFile (slot, juce::File (files[0])))
            showMessage ("Couldn't read that file");
        repaint();
    }

private:
    void drawMeter (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        auto meter = area.reduced (14.0f, 0.0f).removeFromBottom (16.0f).withSizeKeepingCentre (area.getWidth() - 28.0f, 5.0f);
        g.setColour (Theme::cellOff);
        g.fillRoundedRectangle (meter, 2.5f);

        // Scaled in decibels so quiet voices still move the bar.
        const auto db = juce::Decibels::gainToDecibels (inputLevel, -60.0f);
        const auto fill = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        g.setColour (inputLevel > 0.9f ? Theme::red : Theme::lime);
        g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * fill), 2.5f);

        g.setColour (Theme::textDim);
        g.setFont (Theme::font (10.0f));
        g.drawText ("MIC", meter.translated (0.0f, -11.0f).withHeight (10.0f), juce::Justification::left);
    }

    void buildPeaks()
    {
        peaks.clear();
        const auto* sample = processor.groovebox.getSampler().getSampleForDisplay (slot);
        if (sample == nullptr || sample->getNumSamples() == 0)
            return;

        constexpr int columns = 90;
        const auto length = sample->getNumSamples();
        float loudest = 0.0f;

        for (int i = 0; i < columns; ++i)
        {
            const auto start = static_cast<int> (static_cast<juce::int64> (length) * i / columns);
            const auto end = std::max (start + 1, static_cast<int> (static_cast<juce::int64> (length) * (i + 1) / columns));
            peaks.push_back (sample->getMagnitude (0, start, std::min (end, length) - start));
            loudest = std::max (loudest, peaks.back());
        }

        if (loudest > 0.0f)
            for (auto& peak : peaks)
                peak /= loudest;
    }

    SynthAudioProcessor& processor;
    const int slot;
    const juce::Colour colour;

    juce::TextButton record { "REC" }, clear { "CLEAR" };
    ParamToggle reverse, mute, solo;
    Knob level, tune;

    juce::Rectangle<int> waveArea;
    std::vector<float> peaks;
    juce::uint32 lastVersion = 0xffffffff;
    float flash = 0.0f, inputLevel = 0.0f;
    bool dragging = false;
    juce::String message;
    juce::uint32 messageUntil = 0;
};

SamplesPage::SamplesPage (SynthAudioProcessor& p) : processor (p)
{
    for (int slot = 0; slot < Sampler::numSlots; ++slot)
    {
        auto& card = cards[static_cast<size_t> (slot)];
        card = std::make_unique<Card> (p, slot);
        addAndMakeVisible (*card);

        auto& pad = pads[static_cast<size_t> (slot)];
        pad = std::make_unique<DrumPad> ("Sample " + juce::String (slot + 1), Theme::sampleColour (slot));
        pad->onTap = [this, slot] { processor.groovebox.previewSample (slot); };
        addAndMakeVisible (*pad);
    }

    grid.numRows = [] { return Seq::numSampleTracks; };
    grid.isOn = [this] (int row, int column)
    { return processor.bank.getSampleStep (processor.bank.getSelectedPattern(), row, column); };
    grid.setCell = [this] (int row, int column, bool on)
    {
        processor.bank.setSampleStep (processor.bank.getSelectedPattern(), row, column, on);
        if (on && ! processor.groovebox.isRunning())
            processor.groovebox.previewSample (row);
    };
    grid.rowColour = [] (int row) { return Theme::sampleColour (row); };
    grid.playhead = [this] { return playheadFor (processor); };
    grid.length = [this] { return patternLength (processor); };
    addAndMakeVisible (grid);
}

SamplesPage::~SamplesPage() = default;

void SamplesPage::refresh (const Frame& frame)
{
    auto& sampler = processor.groovebox.getSampler();
    const auto recordingSlot = sampler.getRecordingSlot();

    switch (sampler.collectRecording())
    {
        case Sampler::RecordResult::tooQuiet:
            if (recordingSlot >= 0)
                cards[static_cast<size_t> (recordingSlot)]->showMessage ("Too quiet! Get closer to the mic and try again.");
            break;
        case Sampler::RecordResult::recorded:
        case Sampler::RecordResult::none:
            break;
    }

    inputLevel = std::max (sampler.takeInputPeak(), inputLevel * 0.85f);

    if (! isVisible())
        return;

    for (auto& card : cards)
        card->update (inputLevel, frame.drumHits);

    for (int slot = 0; slot < Seq::numSampleTracks; ++slot)
    {
        auto& pad = *pads[static_cast<size_t> (slot)];
        if ((frame.drumHits & (1u << static_cast<unsigned> (Groovebox::sampleTrack (slot)))) != 0)
            pad.flash();
        else
            pad.decayFlash();
    }

    if (frame.sequencerChanged)
        grid.repaint();
}

void SamplesPage::resized()
{
    auto area = getLocalBounds();
    auto cardRow = area.removeFromTop (300);
    area.removeFromTop (12);

    const auto cardWidth = (cardRow.getWidth() - 3 * 12) / Sampler::numSlots;
    for (auto& card : cards)
    {
        card->setBounds (cardRow.removeFromLeft (cardWidth));
        cardRow.removeFromLeft (12);
    }

    // Same columns as the drum grid, so steps line up when switching tabs.
    auto padColumn = area.removeFromLeft (190);
    area.removeFromLeft (8);
    grid.setBounds (area);

    const auto rowHeight = area.getHeight() / Seq::numSampleTracks;
    for (auto& pad : pads)
        pad->setBounds (padColumn.removeFromTop (rowHeight));
}

//==============================================================================
MelodyPage::MelodyPage (SynthAudioProcessor& p, std::function<void()> beforeBigChange)
    : processor (p),
      trackBar (p.parameters),
      scale (p.parameters, ParamIds::scale, "SCALE", Theme::cyan),
      key (p.parameters, ParamIds::root, "KEY", Theme::cyan),
      octave (p.parameters, ParamIds::synth (0, ParamIds::octave), "OCTAVE", Theme::cyan),
      noteLength (p.parameters, ParamIds::synth (0, ParamIds::noteLength), "NOTE LENGTH", Theme::cyan)
{
    trackBar.onSelect = [this] (int newTrack) { processor.groovebox.setLiveTrack (newTrack); showTrack (newTrack); };
    addAndMakeVisible (trackBar);

    grid.highRowsAtTop = true;
    grid.noteMode = true;
    grid.numRows = [] { return Seq::numMelodyRows; };
    grid.noteLengthAt = [this] (int row, int column)
    { return processor.bank.getNoteLength (processor.bank.getSelectedPattern(), track, column, row); };
    grid.noteStartAt = [this] (int row, int column)
    { return processor.bank.findNoteStart (processor.bank.getSelectedPattern(), track, column, row); };
    grid.setNote = [this] (int row, int start, int length)
    {
        const bool isNew = processor.bank.getNoteLength (processor.bank.getSelectedPattern(), track, start, row) == 0;
        processor.bank.setNote (processor.bank.getSelectedPattern(), track, start, row, length);
        if (isNew && ! processor.groovebox.isRunning() && noteForRow (row) <= 127)
            processor.groovebox.previewNote (track, noteForRow (row));
    };
    grid.removeNote = [this] (int row, int column)
    { processor.bank.removeNote (processor.bank.getSelectedPattern(), track, column, row); };
    grid.rowColour = [this] (int row) { return Theme::trackRowColour (track, row, Seq::numMelodyRows); };
    grid.playhead = [this] { return playheadFor (processor); };
    grid.length = [this] { return patternLength (processor); };
    addAndMakeVisible (grid);

    for (auto* component : std::initializer_list<juce::Component*> { &scale, &key, &octave, &noteLength })
        addAndMakeVisible (*component);

    styleActionButton (dice, Theme::yellow);
    styleActionButton (clear, Theme::red);
    dice.onClick = [this, beforeBigChange]
    {
        beforeBigChange();
        processor.randomiseMelody (processor.bank.getSelectedPattern(), track);
    };
    clear.onClick = [this, beforeBigChange]
    {
        beforeBigChange();
        processor.bank.clearMelody (processor.bank.getSelectedPattern(), track);
    };
    addAndMakeVisible (dice);
    addAndMakeVisible (clear);

    showTrack (processor.groovebox.getLiveTrack());
}

void MelodyPage::showTrack (int newTrack)
{
    track = newTrack;
    trackBar.setSelected (track);

    octave.attachTo (ParamIds::synth (track, ParamIds::octave));
    noteLength.attachTo (ParamIds::synth (track, ParamIds::noteLength));
    noteLength.setAccent (Theme::synthColour (track));

    const auto name = juce::String (SynthTracks::tracks[static_cast<size_t> (track)].name).toUpperCase();
    dice.setButtonText ("DICE " + name);
    clear.setButtonText ("CLEAR " + name);
    dice.setTooltip ("Make a random " + name.toLowerCase() + " part");

    lastScaleKey = -1;
    repaint();
    grid.repaint();
}

int MelodyPage::noteForRow (int row) const
{
    return Scales::rowToNote (static_cast<Scale> (choiceIndex (processor, ParamIds::scale)),
                              choiceIndex (processor, ParamIds::root),
                              choiceIndex (processor, ParamIds::synth (track, ParamIds::octave)) + SynthTracks::minOctave, row);
}

void MelodyPage::refresh (const Frame& frame)
{
    if (processor.groovebox.getLiveTrack() != track)
        showTrack (processor.groovebox.getLiveTrack());

    const auto scaleKey = choiceIndex (processor, ParamIds::scale) * 1000 + choiceIndex (processor, ParamIds::root) * 10
                          + choiceIndex (processor, ParamIds::synth (track, ParamIds::octave));
    if (scaleKey != lastScaleKey)
    {
        lastScaleKey = scaleKey;
        repaint (labelArea);
    }

    if (frame.sequencerChanged)
        grid.repaint();
}

void MelodyPage::paint (juce::Graphics& g)
{
    Theme::drawPanel (g, stripArea.toFloat(), Theme::synthColour (track));

    const auto scaleSize = static_cast<int> (Scales::intervals (static_cast<Scale> (choiceIndex (processor, ParamIds::scale))).size());
    const auto rowHeight = static_cast<float> (labelArea.getHeight()) / Seq::numMelodyRows;

    for (int row = 0; row < Seq::numMelodyRows; ++row)
    {
        const auto y = static_cast<float> (labelArea.getBottom()) - static_cast<float> (row + 1) * rowHeight;
        const auto area = juce::Rectangle<float> (static_cast<float> (labelArea.getX()), y,
                                                  static_cast<float> (labelArea.getWidth()) - 8.0f, rowHeight);
        const bool isRoot = row % scaleSize == 0;

        g.setColour (Theme::trackRowColour (track, row, Seq::numMelodyRows).withAlpha (isRoot ? 1.0f : 0.6f));
        g.setFont (Theme::font (isRoot ? 13.0f : 12.0f, isRoot));
        if (const auto note = noteForRow (row); note <= 127)
            g.drawText (Scales::noteName (note), area, juce::Justification::centredRight);
    }
}

void MelodyPage::resized()
{
    auto area = getLocalBounds();
    trackBar.setBounds (area.removeFromTop (44));
    area.removeFromTop (10);
    stripArea = area.removeFromBottom (100);
    area.removeFromBottom (10);

    labelArea = area.removeFromLeft (56);
    area.removeFromLeft (8);
    grid.setBounds (area);

    auto strip = stripArea.reduced (16, 10);
    auto buttons = strip.removeFromRight (380);
    layoutRow (buttons.withSizeKeepingCentre (buttons.getWidth(), 48), { &dice, &clear }, 12);

    auto choices = strip.removeFromLeft (520).withSizeKeepingCentre (520, 50);
    scale.setBounds (choices.removeFromLeft (220));
    choices.removeFromLeft (16);
    key.setBounds (choices.removeFromLeft (110));
    choices.removeFromLeft (16);
    octave.setBounds (choices.removeFromLeft (110));

    strip.removeFromLeft (20);
    noteLength.setBounds (strip.removeFromLeft (110));
}

//==============================================================================
SoundPage::LfoControls::LfoControls (APVTS& state, int index, juce::Colour accent)
    : shape (state, ParamIds::synth (0, ParamIds::lfo (index, "Shape")), "SHAPE", accent),
      destination (state, ParamIds::synth (0, ParamIds::lfo (index, "Destination")), "TO", accent),
      division (state, ParamIds::synth (0, ParamIds::lfo (index, "Division")), "RATE", accent),
      rate (state, ParamIds::synth (0, ParamIds::lfo (index, "Rate")), "RATE", accent),
      depth (state, ParamIds::synth (0, ParamIds::lfo (index, "Depth")), "DEPTH", accent),
      sync (state, ParamIds::synth (0, ParamIds::lfo (index, "Sync")), "SYNC", accent),
      retrigger (state, ParamIds::synth (0, ParamIds::lfo (index, "Retrigger")), "RETRIG", accent)
{
}

void SoundPage::LfoControls::attach (APVTS& state, int trackIndex, int index)
{
    auto id = [&] (const char* field) { return ParamIds::synth (trackIndex, ParamIds::lfo (index, field)); };
    shape.attachTo (id ("Shape"));
    destination.attachTo (id ("Destination"));
    division.attachTo (id ("Division"));
    rate.attachTo (id ("Rate"));
    depth.attachTo (id ("Depth"));
    sync.attachTo (state, id ("Sync"));
    retrigger.attachTo (state, id ("Retrigger"));
}

SoundPage::SoundPage (SynthAudioProcessor& p)
    : processor (p),
      trackBar (p.parameters),
      waveform (p.parameters, ParamIds::synth (0, ParamIds::waveform), "WAVE", Theme::cyan),
      attack (p.parameters, ParamIds::synth (0, ParamIds::attack), "ATTACK", Theme::cyan),
      decay (p.parameters, ParamIds::synth (0, ParamIds::decay), "DECAY", Theme::cyan),
      sustain (p.parameters, ParamIds::synth (0, ParamIds::sustain), "SUSTAIN", Theme::cyan),
      release (p.parameters, ParamIds::synth (0, ParamIds::release), "RELEASE", Theme::cyan),
      cutoff (p.parameters, ParamIds::synth (0, ParamIds::cutoff), "CUTOFF", Theme::cyan),
      resonance (p.parameters, ParamIds::synth (0, ParamIds::resonance), "RESONANCE", Theme::cyan),
      volume (p.parameters, ParamIds::synth (0, ParamIds::gain), "VOLUME", Theme::cyan),
      arpOn (p.parameters, ParamIds::synth (0, ParamIds::arpOn), "ARP ON", Theme::lime),
      arpMode (p.parameters, ParamIds::synth (0, ParamIds::arpMode), "MODE", Theme::lime),
      arpRate (p.parameters, ParamIds::synth (0, ParamIds::arpRate), "RATE", Theme::lime),
      arpOctaves (p.parameters, ParamIds::synth (0, ParamIds::arpOctaves), "OCTAVES", Theme::lime),
      arpGate (p.parameters, ParamIds::synth (0, ParamIds::arpGate), "GATE", Theme::lime)
{
    trackBar.onSelect = [this] (int newTrack) { processor.groovebox.setLiveTrack (newTrack); showTrack (newTrack); };
    addAndMakeVisible (trackBar);

    const auto& presets = soundPresets();
    for (size_t i = 0; i < presets.size(); ++i)
        preset.addItem (presets[i].name, static_cast<int> (i) + 1);

    preset.setTextWhenNothingSelected ("Pick a sound...");
    preset.onChange = [this]
    {
        if (preset.getSelectedItemIndex() >= 0)
            processor.applySoundPreset (track, preset.getSelectedItemIndex());
    };
    addAndMakeVisible (preset);

    for (auto* component : std::initializer_list<juce::Component*> { &waveform, &attack, &decay, &sustain, &release,
                                                                     &cutoff, &resonance, &volume, &arpOn, &arpMode,
                                                                     &arpRate, &arpOctaves, &arpGate })
        addAndMakeVisible (*component);

    const std::array lfoColours { Theme::magenta, Theme::purple };
    for (int i = 0; i < SynthEngine::numLfos; ++i)
    {
        auto& lfo = lfos[static_cast<size_t> (i)];
        lfo = std::make_unique<LfoControls> (p.parameters, i, lfoColours[static_cast<size_t> (i)]);
        for (auto* component : std::initializer_list<juce::Component*> { &lfo->shape, &lfo->destination, &lfo->division,
                                                                         &lfo->rate, &lfo->depth, &lfo->sync, &lfo->retrigger })
            addAndMakeVisible (*component);
    }

    showTrack (processor.groovebox.getLiveTrack());
}

void SoundPage::showTrack (int newTrack)
{
    track = newTrack;
    trackBar.setSelected (track);
    trackColour = Theme::synthColour (track);

    auto& state = processor.parameters;
    auto id = [this] (const char* base) { return ParamIds::synth (track, base); };

    waveform.attachTo (id (ParamIds::waveform));
    attack.attachTo (id (ParamIds::attack));
    decay.attachTo (id (ParamIds::decay));
    sustain.attachTo (id (ParamIds::sustain));
    release.attachTo (id (ParamIds::release));
    cutoff.attachTo (id (ParamIds::cutoff));
    resonance.attachTo (id (ParamIds::resonance));
    volume.attachTo (id (ParamIds::gain));
    arpOn.attachTo (state, id (ParamIds::arpOn));
    arpMode.attachTo (id (ParamIds::arpMode));
    arpRate.attachTo (id (ParamIds::arpRate));
    arpOctaves.attachTo (id (ParamIds::arpOctaves));
    arpGate.attachTo (id (ParamIds::arpGate));

    for (int i = 0; i < SynthEngine::numLfos; ++i)
        lfos[static_cast<size_t> (i)]->attach (state, track, i);

    for (auto* knob : { &attack, &decay, &sustain, &release, &cutoff, &resonance, &volume })
        knob->setAccent (trackColour);

    preset.setSelectedId (0, juce::dontSendNotification);
    preset.setColour (juce::ComboBox::outlineColourId, trackColour);
    preset.setColour (juce::ComboBox::arrowColourId, trackColour);

    refresh ({});
    repaint();
}

void SoundPage::refresh (const Frame&)
{
    if (processor.groovebox.getLiveTrack() != track)
        showTrack (processor.groovebox.getLiveTrack());

    // Synced LFOs show a note length instead of a rate in Hz.
    for (int i = 0; i < SynthEngine::numLfos; ++i)
    {
        auto& lfo = *lfos[static_cast<size_t> (i)];
        const bool synced = lfo.sync.getToggleState();
        lfo.rate.setVisible (! synced);
        lfo.division.setVisible (synced);
    }
}

void SoundPage::paint (juce::Graphics& g)
{
    const auto name = juce::String (SynthTracks::tracks[static_cast<size_t> (track)].name).toUpperCase();
    Theme::drawPanel (g, presetPanel.toFloat(), trackColour, name + " SOUND");
    Theme::drawPanel (g, voicePanel.toFloat(), trackColour, "VOICE");
    Theme::drawPanel (g, lfoPanels[0].toFloat(), Theme::magenta, "LFO 1");
    Theme::drawPanel (g, lfoPanels[1].toFloat(), Theme::purple, "LFO 2");
    Theme::drawPanel (g, arpPanel.toFloat(), Theme::lime, "ARPEGGIATOR");

    g.setColour (Theme::textDim);
    g.setFont (Theme::font (13.0f, false));
    g.drawFittedText ("Start from a ready-made sound, then twist the knobs.",
                      presetPanel.reduced (16).withTrimmedTop (84), juce::Justification::topLeft, 3);
}

void SoundPage::resized()
{
    auto area = getLocalBounds();
    trackBar.setBounds (area.removeFromTop (44));
    area.removeFromTop (10);

    auto top = area.removeFromTop (196);
    area.removeFromTop (12);

    presetPanel = top.removeFromLeft (250);
    top.removeFromLeft (12);
    voicePanel = top;

    preset.setBounds (presetPanel.reduced (16).withTrimmedTop (26).removeFromTop (44));

    auto voice = voicePanel.reduced (16).withTrimmedTop (22);
    waveform.setBounds (voice.removeFromLeft (120).withSizeKeepingCentre (120, 50));
    voice.removeFromLeft (16);
    layoutRow (voice, { &attack, &decay, &sustain, &release, &cutoff, &resonance, &volume });

    const auto panelWidth = (area.getWidth() - 24) / 3;
    lfoPanels[0] = area.removeFromLeft (panelWidth);
    area.removeFromLeft (12);
    lfoPanels[1] = area.removeFromLeft (panelWidth);
    area.removeFromLeft (12);
    arpPanel = area;

    for (size_t i = 0; i < lfos.size(); ++i)
    {
        auto& lfo = *lfos[i];
        auto panel = lfoPanels[i].reduced (14).withTrimmedTop (20);

        auto left = panel.removeFromLeft (130);
        lfo.shape.setBounds (left.removeFromTop (50));
        left.removeFromTop (4);
        lfo.destination.setBounds (left.removeFromTop (50));
        left.removeFromTop (8);
        auto toggles = left.removeFromTop (32);
        lfo.sync.setBounds (toggles.removeFromLeft (63));
        toggles.removeFromLeft (4);
        lfo.retrigger.setBounds (toggles);

        panel.removeFromLeft (10);
        auto rateArea = panel.removeFromLeft (panel.getWidth() / 2);
        lfo.rate.setBounds (rateArea);
        lfo.division.setBounds (rateArea.withSizeKeepingCentre (rateArea.getWidth() - 8, 50));
        lfo.depth.setBounds (panel);
    }

    auto arp = arpPanel.reduced (14).withTrimmedTop (20);
    auto left = arp.removeFromLeft (130);
    arpOn.setBounds (left.removeFromTop (36));
    left.removeFromTop (8);
    arpMode.setBounds (left.removeFromTop (50));
    left.removeFromTop (4);
    arpRate.setBounds (left.removeFromTop (50));

    arp.removeFromLeft (10);
    auto middle = arp.removeFromLeft (arp.getWidth() / 2);
    arpOctaves.setBounds (middle.removeFromTop (50));
    arpGate.setBounds (arp);
}

//==============================================================================
MixPage::MixPage (SynthAudioProcessor& p)
    : drumLevel (p.parameters, ParamIds::drumLevel, "DRUMS", Theme::magenta),
      sampleLevel (p.parameters, ParamIds::sampleLevel, "SAMPLES", Theme::orange),
      echo (p.parameters, ParamIds::echo, "ECHO", Theme::yellow),
      echoFeedback (p.parameters, ParamIds::echoFeedback, "ECHO REPEATS", Theme::yellow),
      space (p.parameters, ParamIds::space, "SPACE", Theme::purple),
      metronomeLevel (p.parameters, ParamIds::metronomeLevel, "CLICK", Theme::orange),
      master (p.parameters, ParamIds::master, "MASTER", Theme::lime),
      followHost (p.parameters, ParamIds::followHost, "FOLLOW HOST", Theme::cyan)
{
    for (int track = 0; track < Seq::numSynthTracks; ++track)
    {
        auto& knob = trackLevels[static_cast<size_t> (track)];
        knob = std::make_unique<Knob> (p.parameters, ParamIds::synth (track, ParamIds::gain),
                                       juce::String (SynthTracks::tracks[static_cast<size_t> (track)].name).toUpperCase(),
                                       Theme::synthColour (track));
        addAndMakeVisible (*knob);
    }

    for (auto* component : std::initializer_list<juce::Component*> { &drumLevel, &sampleLevel, &echo, &echoFeedback,
                                                                     &space, &metronomeLevel, &master, &followHost })
        addAndMakeVisible (*component);

    followHostHint.setText ("In Logic, use Logic's tempo and play button to run the sequencer.",
                            juce::dontSendNotification);
    followHostHint.setFont (Theme::font (13.0f, false));
    followHostHint.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (followHostHint);
}

void MixPage::paint (juce::Graphics& g)
{
    Theme::drawPanel (g, levelsPanel.toFloat(), Theme::cyan, "LEVELS");
    Theme::drawPanel (g, effectsPanel.toFloat(), Theme::lime, "EFFECTS AND MASTER");
}

void MixPage::resized()
{
    auto area = getLocalBounds().reduced (20, 0);
    levelsPanel = area.removeFromTop (area.getHeight() / 2 - 6);
    area.removeFromTop (12);
    effectsPanel = area;

    auto levels = levelsPanel.reduced (24, 16).withTrimmedTop (16);
    std::vector<juce::Component*> levelKnobs;
    for (auto& knob : trackLevels)
        levelKnobs.push_back (knob.get());
    levelKnobs.push_back (&drumLevel);
    levelKnobs.push_back (&sampleLevel);

    constexpr int gap = 14;
    const auto knobWidth = (levels.getWidth() - gap * (static_cast<int> (levelKnobs.size()) - 1)) / static_cast<int> (levelKnobs.size());
    for (auto* knob : levelKnobs)
    {
        knob->setBounds (levels.removeFromLeft (knobWidth));
        levels.removeFromLeft (gap);
    }

    auto effects = effectsPanel.reduced (24, 16).withTrimmedTop (16);
    auto bottom = effects.removeFromBottom (40);
    followHost.setBounds (bottom.removeFromLeft (160));
    bottom.removeFromLeft (16);
    followHostHint.setBounds (bottom);
    effects.removeFromBottom (8);
    layoutRow (effects.withTrimmedRight (effects.getWidth() / 6), { &echo, &echoFeedback, &space, &metronomeLevel, &master }, 16);
}
