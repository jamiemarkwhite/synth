#include "PluginEditor.h"
#include "ParamIds.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace
{
void setUpCaption (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (Theme::font (12.0f));
    label.setColour (juce::Label::textColourId, Theme::textDim);
    label.setJustificationType (juce::Justification::centredLeft);
}

// The standalone app mutes the mic by default in case the plugin plays it back through the
// speakers. This plugin never sends its input to the output, so the mic can stay on for recording.
void unmuteStandaloneInput ([[maybe_unused]] const juce::AudioProcessor& processor)
{
   #if JucePlugin_Build_Standalone
    if (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            holder->getMuteInputValue().setValue (false);
   #endif
}

// Buttons and knobs shouldn't take keyboard focus, so the computer keyboard keeps playing notes.
void stopStealingFocus (juce::Component& component)
{
    component.setMouseClickGrabsKeyboardFocus (false);
    for (auto* child : component.getChildren())
        stopStealingFocus (*child);
}
} // namespace

SynthAudioProcessorEditor::SynthAudioProcessorEditor (SynthAudioProcessor& p)
    : AudioProcessorEditor (&p),
      synthProcessor (p),
      steps (p.parameters, ParamIds::steps, "STEPS", Theme::cyan),
      songBar (p.bank),
      drumPage (p, [this] { saveUndo(); }),
      samplesPage (p),
      melodyPage (p, [this] { saveUndo(); }),
      soundPage (p),
      mixPage (p),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);

    playButton.onClick = [this] { synthProcessor.groovebox.setPlaying (! synthProcessor.groovebox.isPlayRequested()); };
    recordButton.onClick = [this] { synthProcessor.groovebox.setRecording (! synthProcessor.groovebox.isRecording()); };
    playButton.setTooltip ("Play / stop (space bar)");
    recordButton.setTooltip ("Record: pads and keys you play go into the pattern");
    content.addAndMakeVisible (playButton);
    content.addAndMakeVisible (recordButton);

    metronomeButton.setTooltip ("Metronome: a click on every beat while playing. Set its volume on the Mix page.");
    metronomeButton.onClick = [this]
    {
        if (auto* param = synthProcessor.parameters.getParameter (ParamIds::metronome))
            param->setValueNotifyingHost (param->getValue() >= 0.5f ? 0.0f : 1.0f);
    };
    content.addAndMakeVisible (metronomeButton);

    setUpCaption (bpmLabel, "BPM");
    setUpCaption (swingLabel, "SWING");
    setUpCaption (patternLabel, "PATTERN");
    for (auto* label : { &bpmLabel, &swingLabel, &patternLabel })
        content.addAndMakeVisible (*label);

    bpm.setColour (juce::Slider::rotarySliderFillColourId, Theme::lime);
    swing.setColour (juce::Slider::rotarySliderFillColourId, Theme::yellow);
    for (auto* slider : { &bpm, &swing })
        content.addAndMakeVisible (*slider);

    bpmAttachment = std::make_unique<APVTS::SliderAttachment> (p.parameters, ParamIds::bpm, bpm);
    swingAttachment = std::make_unique<APVTS::SliderAttachment> (p.parameters, ParamIds::swing, swing);
    bpm.textFromValueFunction = [] (double value) { return juce::String (juce::roundToInt (value)); };
    bpm.updateText();
    if (auto* param = p.parameters.getParameter (ParamIds::swing))
        useFriendlyText (swing, *param);
    content.addAndMakeVisible (steps);

    for (int i = 0; i < Seq::numPatterns; ++i)
    {
        auto& button = patternButtons[static_cast<size_t> (i)];
        button = std::make_unique<PatternButton> (i);
        button->onClick = [this, i] { synthProcessor.bank.setSelectedPattern (i); };
        content.addAndMakeVisible (*button);
    }

    copyButton.setColour (juce::TextButton::buttonOnColourId, Theme::cyan);
    clearButton.setColour (juce::TextButton::buttonOnColourId, Theme::red);
    diceButton.setColour (juce::TextButton::buttonOnColourId, Theme::yellow);
    undoButton.setColour (juce::TextButton::buttonOnColourId, Theme::purple);
    copyButton.setTooltip ("Copy this pattern into the next one and switch to it");
    diceButton.setTooltip ("Make a random beat and a part for every synth track");
    undoButton.setTooltip ("Undo the last Dice, Clear or Copy");
    undoButton.setEnabled (false);

    copyButton.onClick = [this]
    {
        saveUndo();
        const auto from = synthProcessor.bank.getSelectedPattern();
        const auto to = (from + 1) % Seq::numPatterns;
        synthProcessor.bank.copyPattern (from, to);
        synthProcessor.bank.setSelectedPattern (to);
    };
    clearButton.onClick = [this]
    {
        saveUndo();
        synthProcessor.bank.clearPattern (synthProcessor.bank.getSelectedPattern());
    };
    diceButton.onClick = [this]
    {
        saveUndo();
        const auto pattern = synthProcessor.bank.getSelectedPattern();
        synthProcessor.randomiseDrums (pattern);
        synthProcessor.randomiseAllMelodies (pattern);
    };
    undoButton.onClick = [this] { undo(); };
    for (auto* button : { &copyButton, &clearButton, &diceButton, &undoButton })
        content.addAndMakeVisible (*button);

    freshButton.setColour (juce::TextButton::buttonOnColourId, Theme::red);
    freshButton.setTooltip ("Empty everything: all patterns, the song and the samples. Sounds and settings go back to how they start.");
    freshButton.onClick = [this] { confirmStartFresh(); };
    content.addAndMakeVisible (freshButton);

    const std::array<std::pair<const char*, juce::Colour>, 5> tabInfo { {
        { "DRUMS", Theme::magenta }, { "SAMPLES", Theme::orange }, { "MELODY", Theme::cyan },
        { "SOUND", Theme::purple }, { "MIX", Theme::lime } } };
    for (size_t i = 0; i < tabs.size(); ++i)
    {
        tabs[i].setButtonText (tabInfo[i].first);
        tabs[i].setColour (juce::TextButton::buttonOnColourId, tabInfo[i].second);
        tabs[i].setRadioGroupId (1);
        tabs[i].setClickingTogglesState (true);
        tabs[i].onClick = [this, i] { showPage (static_cast<int> (i)); };
        content.addAndMakeVisible (tabs[i]);
    }

    songButton.setColour (juce::TextButton::buttonOnColourId, Theme::orange);
    songButton.setTooltip ("Song mode: play the patterns in the slots from left to right");
    songButton.onClick = [this] { synthProcessor.bank.setSongMode (! synthProcessor.bank.isSongMode()); };
    content.addAndMakeVisible (songButton);

    songBar.playingSlot = [this] { return synthProcessor.groovebox.getSongSlot(); };
    songBar.setTooltip ("Click a slot to pick A, B, C or D. Right-click to empty it.");
    content.addAndMakeVisible (songBar);

    for (auto* page : pages)
        content.addChildComponent (*page);

    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, Theme::background);
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colours::transparentBlack);
    keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, Theme::panel);
    keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonArrowColourId, Theme::cyan);
    keyboard.setAvailableRange (24, 108);
    keyboard.setKeyWidth (32.0f);
    keyboard.setLowestVisibleKey (36);
    keyboard.setKeyPressBaseOctave (4); // A on the computer keyboard plays C2, the bottom of the melody grid
    keyboard.addKeyListener (this);
    content.addAndMakeVisible (keyboard);

    stopStealingFocus (content);
    keyboard.setMouseClickGrabsKeyboardFocus (true);
    keyboard.setWantsKeyboardFocus (true);
    addKeyListener (this);

    // Components built before the look-and-feel was set need to pick it up.
    sendLookAndFeelChange();

    tabs[0].setToggleState (true, juce::dontSendNotification);
    showPage (0);
    updateHeader();

    setResizable (true, true);
    setResizeLimits (designWidth * 3 / 4, designHeight * 3 / 4, designWidth * 3 / 2, designHeight * 3 / 2);
    getConstrainer()->setFixedAspectRatio (static_cast<double> (designWidth) / designHeight);
    setSize (designWidth, designHeight);

    unmuteStandaloneInput (p);
    startTimerHz (30);
}

SynthAudioProcessorEditor::~SynthAudioProcessorEditor()
{
    keyboard.removeKeyListener (this);
    removeKeyListener (this);
    setLookAndFeel (nullptr);
}

void SynthAudioProcessorEditor::resized()
{
    const auto scale = static_cast<float> (getWidth()) / designWidth;
    content.setTransform (juce::AffineTransform::scale (scale));
    content.setBounds (0, 0, designWidth, designHeight);
}

void SynthAudioProcessorEditor::paintContent (juce::Graphics& g)
{
    const auto bounds = content.getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (Theme::backgroundTop, 0.0f, 0.0f, Theme::background, 0.0f, bounds.getHeight(), false));
    g.fillRect (bounds);

    // Faint horizon glow behind the keyboard.
    g.setGradientFill (juce::ColourGradient (Theme::magenta.withAlpha (0.10f), bounds.getCentreX(), bounds.getBottom(),
                                             juce::Colours::transparentBlack, bounds.getCentreX(), bounds.getBottom() - 260.0f, true));
    g.fillRect (bounds);

    // Wordmark.
    const juce::Rectangle<float> wordmark (16.0f, 8.0f, 150.0f, 52.0f);
    const auto name = juce::String::fromUTF8 ("\xce\xbc") + "BEAT"; // lower-case mu: capital Mu looks like M
    g.setFont (Theme::font (34.0f));
    for (auto [dx, dy] : { std::pair { -2.0f, 0.0f }, { 2.0f, 0.0f }, { 0.0f, -2.0f }, { 0.0f, 2.0f } })
    {
        g.setColour (Theme::magenta.withAlpha (0.18f));
        g.drawText (name, wordmark.translated (dx, dy), juce::Justification::centredLeft);
    }
    g.setGradientFill (juce::ColourGradient (Theme::magenta, wordmark.getX(), 0.0f, Theme::cyan, wordmark.getRight(), 0.0f, false));
    g.drawText (name, wordmark, juce::Justification::centredLeft);

    g.setColour (Theme::cyan.withAlpha (0.18f));
    g.fillRect (16.0f, 66.0f, bounds.getWidth() - 32.0f, 1.0f);
}

void SynthAudioProcessorEditor::layoutContent()
{
    auto area = content.getLocalBounds().reduced (16, 0);

    auto header = area.removeFromTop (68).reduced (0, 8);
    header.removeFromLeft (160);
    playButton.setBounds (header.removeFromLeft (64));
    header.removeFromLeft (8);
    recordButton.setBounds (header.removeFromLeft (52));
    header.removeFromLeft (8);
    metronomeButton.setBounds (header.removeFromLeft (52));
    header.removeFromLeft (16);

    auto bpmArea = header.removeFromLeft (110);
    bpmLabel.setBounds (bpmArea.removeFromTop (14));
    bpm.setBounds (bpmArea);
    header.removeFromLeft (12);

    auto swingArea = header.removeFromLeft (84);
    swingLabel.setBounds (swingArea.removeFromTop (14));
    swing.setBounds (swingArea);
    header.removeFromLeft (12);

    steps.setBounds (header.removeFromLeft (76).withTrimmedTop (-2));
    header.removeFromLeft (20);

    auto patternArea = header.removeFromLeft (4 * 50);
    patternLabel.setBounds (patternArea.removeFromTop (14));
    for (auto& button : patternButtons)
        button->setBounds (patternArea.removeFromLeft (50).reduced (2, 0));
    header.removeFromLeft (16);

    for (auto* button : { &undoButton, &diceButton, &clearButton })
    {
        button->setBounds (header.removeFromRight (70));
        header.removeFromRight (6);
    }
    copyButton.setBounds (header);

    area.removeFromTop (6);
    auto navigation = area.removeFromTop (40);
    for (auto& tab : tabs)
    {
        tab.setBounds (navigation.removeFromLeft (112));
        navigation.removeFromLeft (6);
    }
    navigation.removeFromLeft (20);
    songButton.setBounds (navigation.removeFromLeft (80));
    navigation.removeFromLeft (8);
    freshButton.setBounds (navigation.removeFromRight (120));
    navigation.removeFromRight (12);
    songBar.setBounds (navigation);

    area.removeFromTop (12);
    keyboard.setBounds (area.removeFromBottom (126).withTrimmedBottom (10));
    area.removeFromBottom (12);

    for (auto* page : pages)
        page->setBounds (area);
}

void SynthAudioProcessorEditor::showPage (int index)
{
    for (size_t i = 0; i < pages.size(); ++i)
        pages[i]->setVisible (static_cast<int> (i) == index);
}

void SynthAudioProcessorEditor::saveUndo()
{
    undoSnapshot = synthProcessor.bank.toValueTree();
    undoButton.setEnabled (true);
}

void SynthAudioProcessorEditor::undo()
{
    if (! undoSnapshot.isValid())
        return;

    // Swapping means a second Undo puts things back (redo).
    const auto current = synthProcessor.bank.toValueTree();
    synthProcessor.bank.fromValueTree (undoSnapshot);
    undoSnapshot = current;
}

void SynthAudioProcessorEditor::confirmStartFresh()
{
    const auto options = juce::MessageBoxOptions()
                             .withIconType (juce::MessageBoxIconType::WarningIcon)
                             .withTitle ("Start fresh?")
                             .withMessage ("This empties every pattern, the song and all your recorded samples. It can't be undone.")
                             .withButton ("Start fresh", 1)
                             .withButton ("Cancel", 0)
                             .withAssociatedComponent (this);

    juce::AlertWindow::showAsync (options, [safeThis = juce::Component::SafePointer (this)] (int result)
    {
        if (safeThis == nullptr || result != 1)
            return;

        safeThis->synthProcessor.startFresh();
        safeThis->undoSnapshot = {};
        safeThis->undoButton.setEnabled (false);
    });
}

void SynthAudioProcessorEditor::updateHeader()
{
    auto& bank = synthProcessor.bank;
    const auto selected = bank.getSelectedPattern();
    const auto playingPattern = synthProcessor.groovebox.isRunning() ? synthProcessor.groovebox.getPlayingPattern() : -1;

    for (int i = 0; i < Seq::numPatterns; ++i)
    {
        auto& button = *patternButtons[static_cast<size_t> (i)];
        button.setToggleState (i == selected, juce::dontSendNotification);
        button.playing = i == playingPattern;
        button.hasContent = ! bank.isEmpty (i);
        button.repaint();
    }

    copyButton.setButtonText ("COPY TO " + juce::String::charToString (static_cast<juce::juce_wchar> ('A' + (selected + 1) % Seq::numPatterns)));
    songButton.setToggleState (bank.isSongMode(), juce::dontSendNotification);
    songBar.repaint();
}

void SynthAudioProcessorEditor::timerCallback()
{
    auto& groovebox = synthProcessor.groovebox;
    const auto version = synthProcessor.bank.getVersion();
    const auto step = groovebox.getCurrentStep();
    const auto playingPattern = groovebox.getPlayingPattern();
    const auto songSlot = groovebox.getSongSlot();

    Frame frame;
    frame.drumHits = groovebox.takeDrumHits();
    frame.sequencerChanged = version != lastVersion || step != lastStep || playingPattern != lastPlayingPattern
                             || songSlot != lastSongSlot;

    if (frame.sequencerChanged)
    {
        lastVersion = version;
        lastStep = step;
        lastPlayingPattern = playingPattern;
        lastSongSlot = songSlot;
        updateHeader();
    }

    // Keep the on-screen keyboard focused so the computer keyboard plays notes, unless
    // something else (like a value being typed in) has focus.
    if (isShowing() && ! keyboard.hasKeyboardFocus (true))
        if (auto* focused = juce::Component::getCurrentlyFocusedComponent(); focused == nullptr || focused == this)
            keyboard.grabKeyboardFocus();

    playButton.setToggleState (groovebox.isRunning(), juce::dontSendNotification);
    recordButton.setToggleState (groovebox.isRecording(), juce::dontSendNotification);
    metronomeButton.setToggleState (synthProcessor.parameters.getRawParameterValue (ParamIds::metronome)->load() >= 0.5f,
                                    juce::dontSendNotification);

    for (auto* page : pages)
        if (page->isVisible() || page == &drumPage || page == &samplesPage) // the samples page collects finished recordings
            page->refresh (frame);
}

bool SynthAudioProcessorEditor::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress::spaceKey)
    {
        synthProcessor.groovebox.setPlaying (! synthProcessor.groovebox.isPlayRequested());
        return true;
    }

    return false;
}
