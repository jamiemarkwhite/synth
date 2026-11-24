#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

struct DrumTrackParams
{
    float level = 0.8f;         // 0..1
    float tuneSemitones = 0.0f; // -12..12
    float decay = 1.0f;         // multiplier on the voice's natural decay time
    bool muted = false;         // only silences the sequencer, pads still play
    bool soloed = false;
};

struct DrumParams
{
    std::array<DrumTrackParams, 8> tracks;
};

// Eight synthesised analogue-style drum voices. No samples: every sound is built from
// oscillators, noise, filters and envelopes.
class DrumMachine
{
public:
    static constexpr int numTracks = 8;

    enum Track
    {
        kick = 0,
        snare,
        clap,
        closedHat,
        openHat,
        lowTom,
        highTom,
        cowbell
    };

    static juce::String trackName (int track);
    static int midiNoteForTrack (int track);  // General MIDI drum map
    static int trackForMidiNote (int note);   // -1 if the note isn't mapped

    void prepare (double sampleRate);
    void setParams (const DrumParams& params);
    void trigger (int track, float velocity);

    // Adds `numSamples` of drum audio, starting at `startSample`, to every channel of `buffer`.
    void render (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    void reset();
    bool isTrackSounding (int track) const;

private:
    using Filter = juce::dsp::StateVariableTPTFilter<float>;

    struct Voice
    {
        bool active = false;
        int age = 0; // samples since trigger
        float velocity = 1.0f;
        float tuneRatio = 1.0f;
        float amp = 0.0f, ampCoeff = 0.0f;
        float aux = 0.0f, auxCoeff = 0.0f;     // second envelope (noise, click or attack)
        float pitch = 0.0f, pitchCoeff = 0.0f; // pitch sweep envelope
        std::array<double, 6> phases {};
        Filter filterA, filterB;
    };

    float renderSample (int track, Voice& voice);
    float metal (Voice& voice, int numOscillators, const float* frequencies);
    float noise();
    float coeff (float seconds) const;

    std::array<Voice, numTracks> voices;
    DrumParams params;
    double sampleRate = 44100.0;
    juce::uint32 noiseState = 0x12345678;
};
