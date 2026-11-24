#pragma once

#include "../PluginProcessor.h"
#include "Widgets.h"

// What changed since the last UI frame.
struct Frame
{
    juce::uint32 drumHits = 0;
    bool sequencerChanged = false; // patterns, playhead or selection
};

class Page : public juce::Component
{
public:
    virtual void refresh (const Frame&) {}
};

class DrumPage final : public Page
{
public:
    DrumPage (SynthAudioProcessor&, std::function<void()> beforeBigChange);

    void refresh (const Frame&) override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void selectTrack (int track);

    SynthAudioProcessor& processor;
    std::array<std::unique_ptr<DrumPad>, Seq::numDrumTracks> pads;
    std::array<std::unique_ptr<ParamToggle>, Seq::numDrumTracks> mutes, solos;
    StepGrid grid;
    juce::Rectangle<int> stripArea;
    juce::Label trackLabel;
    Knob level, tune, decay;
    juce::TextButton dice { "DICE DRUMS" }, clear { "CLEAR DRUMS" };
    int selectedTrack = 0;
};

class SamplesPage final : public Page
{
public:
    explicit SamplesPage (SynthAudioProcessor&);
    ~SamplesPage() override;

    void refresh (const Frame&) override;
    void resized() override;

private:
    class Card;

    SynthAudioProcessor& processor;
    std::array<std::unique_ptr<Card>, Sampler::numSlots> cards;
    std::array<std::unique_ptr<DrumPad>, Seq::numSampleTracks> pads;
    StepGrid grid;
    float inputLevel = 0.0f;
};

class MelodyPage final : public Page
{
public:
    MelodyPage (SynthAudioProcessor&, std::function<void()> beforeBigChange);

    void refresh (const Frame&) override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int noteForRow (int row) const;
    void showTrack (int track);

    SynthAudioProcessor& processor;
    TrackBar trackBar;
    StepGrid grid;
    juce::Rectangle<int> labelArea, stripArea;
    Choice scale, key, octave;
    Knob noteLength;
    juce::TextButton dice, clear;
    int track = -1;
    int lastScaleKey = -1;
};

class SoundPage final : public Page
{
public:
    explicit SoundPage (SynthAudioProcessor&);

    void refresh (const Frame&) override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct LfoControls
    {
        LfoControls (APVTS&, int index, juce::Colour accent);
        void attach (APVTS&, int trackIndex, int index);
        Choice shape, destination, division;
        Knob rate, depth;
        ParamToggle sync, retrigger;
    };

    void showTrack (int track);

    SynthAudioProcessor& processor;
    TrackBar trackBar;
    int track = -1;
    juce::ComboBox preset;
    Choice waveform;
    Knob attack, decay, sustain, release, cutoff, resonance, volume;
    std::array<std::unique_ptr<LfoControls>, SynthEngine::numLfos> lfos;
    ParamToggle arpOn;
    Choice arpMode, arpRate, arpOctaves;
    Knob arpGate;

    juce::Rectangle<int> presetPanel, voicePanel, arpPanel;
    juce::Colour trackColour;
    std::array<juce::Rectangle<int>, SynthEngine::numLfos> lfoPanels;
};

class MixPage final : public Page
{
public:
    explicit MixPage (SynthAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    std::array<std::unique_ptr<Knob>, Seq::numSynthTracks> trackLevels;
    Knob drumLevel, sampleLevel, echo, echoFeedback, space, metronomeLevel, master;
    ParamToggle followHost;
    juce::Label followHostHint;
    juce::Rectangle<int> levelsPanel, effectsPanel;
};
