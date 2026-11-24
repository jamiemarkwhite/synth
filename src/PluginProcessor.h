#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "Groovebox.h"
#include "PatternBank.h"

class SynthAudioProcessor final : public juce::AudioProcessor
{
public:
    SynthAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Message thread helpers for the editor.
    void applySoundPreset (int track, int index);
    void randomiseDrums (int pattern);
    void randomiseMelody (int pattern, int track);
    void randomiseAllMelodies (int pattern);
    void startFresh(); // stops, and empties every pattern, the song and the samples; resets every parameter
    Scale getScale() const;
    bool loadSampleFile (int slot, const juce::File& file);

private:
    // A track's sound parameter values when no preset is applied (the "Init" sound), by base name.
    // Presets start from these. Declared before `parameters`, which fills it in.
    std::map<juce::String, float> initValues;

public:
    juce::AudioProcessorValueTreeState parameters;
    juce::MidiKeyboardState keyboardState;
    PatternBank bank;
    Groovebox groovebox { bank };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (std::map<juce::String, float>& initValues);
    GrooveParams readParams() const;

    // Cached so the audio thread never looks parameters up by name.
    using Raw = std::atomic<float>*;

    struct RawParams
    {
        struct Lfo
        {
            Raw shape, destination, rate, depth, sync, division, retrigger;
        };

        struct Drum
        {
            Raw level, tune, decay, mute, solo;
        };

        struct Sample
        {
            Raw level, tune, reverse, mute, solo;
        };

        struct Track
        {
            Raw waveform, attack, decay, sustain, release, cutoff, resonance, gain;
            std::array<Lfo, SynthEngine::numLfos> lfos;
            Raw arpOn, arpMode, arpRate, arpOctaves, arpGate;
            Raw octave, noteLength, mute, solo;
        };

        std::array<Track, Seq::numSynthTracks> tracks;
        std::array<Drum, DrumMachine::numTracks> drums;
        std::array<Sample, Sampler::numSlots> samples;
        Raw bpm, swing, steps, scale, root, followHost, metronome, metronomeLevel;
        Raw drumLevel, sampleLevel, echo, echoFeedback, space, master;
    };

    RawParams raw {};
    juce::Random random;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthAudioProcessor)
};
