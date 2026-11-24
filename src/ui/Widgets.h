#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "../PatternBank.h"
#include "Theme.h"

using APVTS = juce::AudioProcessorValueTreeState;

// Human-friendly text for a parameter value, e.g. "10 ms", "3.0 kHz", "80%".
juce::String formatParameterValue (const juce::RangedAudioParameter& param, double value);
double parseParameterValue (const juce::RangedAudioParameter& param, const juce::String& text);
void useFriendlyText (juce::Slider& slider, const juce::RangedAudioParameter& param);

// Rotary knob with a caption above and value below, attached to a parameter.
class Knob final : public juce::Component
{
public:
    Knob (APVTS& state, const juce::String& paramId, const juce::String& caption, juce::Colour accent);

    void attachTo (const juce::String& paramId); // rebinds to another parameter
    void setCaption (const juce::String& caption) { label.setText (caption, juce::dontSendNotification); }
    void setAccent (juce::Colour accent);
    void resized() override;

private:
    APVTS& state;
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label label;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

// Drop-down with a caption, attached to a choice parameter.
class Choice final : public juce::Component
{
public:
    Choice (APVTS& state, const juce::String& paramId, const juce::String& caption, juce::Colour accent);
    void attachTo (const juce::String& paramId); // rebinds to another parameter with the same choices
    void resized() override;

private:
    APVTS& state;
    juce::ComboBox box;
    juce::Label label;
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
};

// Lit/unlit button attached to a bool parameter.
class ParamToggle final : public juce::TextButton
{
public:
    ParamToggle (APVTS& state, const juce::String& paramId, const juce::String& text, juce::Colour accent);
    void attachTo (APVTS& state, const juce::String& paramId);

private:
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

// Picks which synth track the Melody and Sound pages show and the keyboard plays,
// with mute and solo buttons per track.
class TrackBar final : public juce::Component
{
public:
    explicit TrackBar (APVTS& state);

    std::function<void (int track)> onSelect;
    void setSelected (int track);
    int getSelected() const { return selected; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> trackArea (int track) const;

    std::array<std::unique_ptr<ParamToggle>, Seq::numSynthTracks> mutes, solos;
    int selected = 0;
};

// Grid of on/off cells: drum steps or melody notes. Click to toggle, drag to paint.
//
// In note mode (the melody grid) a note can last several steps: click an empty cell and drag
// right to draw a long note, drag an existing note to change its length, and click a note
// without dragging to remove it.
class StepGrid final : public juce::Component
{
public:
    bool noteMode = false;
    std::function<int (int row, int column)> noteLengthAt; // steps of the note starting here, or 0
    std::function<int (int row, int column)> noteStartAt;  // start of the note covering the cell, or -1
    std::function<void (int row, int start, int length)> setNote;
    std::function<void (int row, int column)> removeNote;

    std::function<int()> numRows;
    std::function<bool (int row, int column)> isOn;
    std::function<void (int row, int column, bool on)> setCell;
    std::function<juce::Colour (int row)> rowColour;
    std::function<int()> playhead; // column being played, or -1
    std::function<int()> length;   // columns from here on are dimmed
    bool highRowsAtTop = false;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> cellBounds (int row, int column) const;
    void paintNotes (juce::Graphics&, int rows, int activeLength, int playColumn);

    struct NoteDrag
    {
        int row = -1, start = 0, length = 1;
        int pressedColumn = 0;
        bool existing = false, moved = false;
    };
    NoteDrag noteDrag;
    std::optional<std::pair<int, int>> cellAt (juce::Point<float> position) const;

    bool paintValue = true;
    std::pair<int, int> lastCell { -1, -1 };
};

// The 16 song slots. Click cycles a slot through A, B, C, D and empty. Right-click empties it.
class SongBar final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit SongBar (PatternBank& patternBank) : bank (patternBank) {}

    std::function<int()> playingSlot;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    PatternBank& bank;
};

// Drum track header: tap to hear it and select it. Flashes when the drum plays.
class DrumPad final : public juce::Component
{
public:
    DrumPad (const juce::String& trackName, juce::Colour trackColour) : name (trackName), colour (trackColour) {}

    std::function<void()> onTap;
    bool selected = false;

    void flash() { flashLevel = 1.0f; repaint(); }
    bool decayFlash();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::String name;
    juce::Colour colour;
    float flashLevel = 0.0f;
};

// On-screen keyboard drawn in the neon style.
class NeonKeyboard final : public juce::MidiKeyboardComponent
{
public:
    using juce::MidiKeyboardComponent::MidiKeyboardComponent;

    void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour lineColour, juce::Colour textColour) override;
    void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour fillColour) override;
};

// Button that briefly lights up the pattern it plays.
class PatternButton final : public juce::TextButton
{
public:
    PatternButton (int index);
    bool playing = false;
    bool hasContent = false;
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    int index;
};

// Big transport buttons drawn as icons.
class TransportButton final : public juce::Button
{
public:
    enum class Kind
    {
        play,
        record,
        metronome
    };

    explicit TransportButton (Kind kind);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Kind kind;
};
