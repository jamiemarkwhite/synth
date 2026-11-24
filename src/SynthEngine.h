#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "Lfo.h"
#include "Tempo.h"

enum class Waveform
{
    sine = 0,
    saw,
    square
};

struct LfoParams
{
    LfoShape shape = LfoShape::sine;
    LfoDestination destination = LfoDestination::off;
    float rate = 1.0f;  // Hz, used when sync is off
    float depth = 0.0f; // 0..1
    bool sync = false;  // lock the rate to the tempo using `division`
    int division = Tempo::defaultLfoDivision;
    bool retrigger = false; // restart the cycle on every new note
};

struct SynthParams
{
    Waveform waveform = Waveform::saw;
    float attack = 0.01f;   // seconds
    float decay = 0.2f;     // seconds
    float sustain = 0.8f;   // 0..1
    float release = 0.3f;   // seconds
    float cutoff = 8000.0f; // Hz
    float resonance = 0.7f; // filter Q
    float gainDb = -6.0f;
    std::array<LfoParams, 2> lfos;
};

// Polyphonic synth: oscillator + ADSR per voice, then a shared low-pass filter and output gain.
// Two shared LFOs modulate pitch, filter cutoff or amplitude.
class SynthEngine
{
public:
    static constexpr int numVoices = 8;
    static constexpr int numLfos = 2;

    // Full-depth modulation ranges.
    static constexpr float maxPitchSemitones = 12.0f;
    static constexpr float maxCutoffOctaves = 4.0f;

    SynthEngine();

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void setParams (const SynthParams& params);
    void setTempo (double bpm);
    void render (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi);
    void reset();

private:
    // LFO values are applied every this many samples. Amplitude is ramped between steps.
    static constexpr int modulationInterval = 32;

    struct Modulation
    {
        float pitchSemitones = 0.0f;
        float cutoffOctaves = 0.0f;
        float amp = 1.0f;
    };

    Modulation currentModulation() const;
    void applyPitchModulation (float semitones);
    void updateLfoRates();

    juce::Synthesiser synth;
    std::array<Lfo, numLfos> lfos;
    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::dsp::Gain<float> gain;
    SynthParams currentParams;
    double tempo = 120.0;
};
