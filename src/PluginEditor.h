#pragma once

#include "PluginProcessor.h"
#include "ui/NeonLookAndFeel.h"
#include "ui/Pages.h"

class SynthAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        private juce::Timer,
                                        private juce::KeyListener
{
public:
    explicit SynthAudioProcessorEditor (SynthAudioProcessor&);
    ~SynthAudioProcessorEditor() override;

    void resized() override;

private:
    static constexpr int designWidth = 1240;
    static constexpr int designHeight = 800;

    // Everything is laid out at the design size, then scaled to the window.
    class Content final : public juce::Component
    {
    public:
        explicit Content (SynthAudioProcessorEditor& editor) : owner (editor) {}
        void paint (juce::Graphics& g) override { owner.paintContent (g); }
        void resized() override { owner.layoutContent(); }

    private:
        SynthAudioProcessorEditor& owner;
    };

    void paintContent (juce::Graphics&);
    void layoutContent();
    void timerCallback() override;
    using juce::AudioProcessorEditor::keyPressed;
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;

    void showPage (int index);
    void saveUndo();
    void undo();
    void confirmStartFresh();
    void updateHeader();

    SynthAudioProcessor& synthProcessor;
    NeonLookAndFeel lookAndFeel;
    Content content { *this };

    TransportButton playButton { TransportButton::Kind::play };
    TransportButton recordButton { TransportButton::Kind::record };
    TransportButton metronomeButton { TransportButton::Kind::metronome };
    juce::Slider bpm { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::Slider swing { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::Label bpmLabel, swingLabel, patternLabel;
    std::unique_ptr<APVTS::SliderAttachment> bpmAttachment, swingAttachment;
    Choice steps;
    std::array<std::unique_ptr<PatternButton>, Seq::numPatterns> patternButtons;
    juce::TextButton copyButton, clearButton { "CLEAR" }, diceButton { "DICE" }, undoButton { "UNDO" };

    std::array<juce::TextButton, 5> tabs;
    juce::TextButton songButton { "SONG" };
    juce::TextButton freshButton { "START FRESH" };
    SongBar songBar;

    DrumPage drumPage;
    SamplesPage samplesPage;
    MelodyPage melodyPage;
    SoundPage soundPage;
    MixPage mixPage;
    std::array<Page*, 5> pages { &drumPage, &samplesPage, &melodyPage, &soundPage, &mixPage };

    NeonKeyboard keyboard;
    juce::TooltipWindow tooltipWindow { this, 700 };

    juce::ValueTree undoSnapshot;
    juce::uint32 lastVersion = 0;
    int lastStep = -2, lastPlayingPattern = -1, lastSongSlot = -2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthAudioProcessorEditor)
};
