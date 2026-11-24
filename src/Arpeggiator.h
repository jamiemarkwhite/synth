#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Tempo.h"

enum class ArpMode
{
    up = 0,
    down,
    upDown,
    random
};

struct ArpParams
{
    bool enabled = false;
    ArpMode mode = ArpMode::up;
    int division = Tempo::defaultArpDivision;
    int octaves = 1;    // 1..3
    float gate = 0.5f;  // fraction of each step the note is held
};

// Turns held keys into a repeating pattern of single notes at a tempo-based rate.
// When disabled, notes pass straight through.
class Arpeggiator
{
public:
    static constexpr int outputChannel = 3;
    static constexpr int maxHeldNotes = 16;

    void setParams (const ArpParams& params);

    // Reads note events from `input` and writes the resulting notes to `output`.
    // Non-note messages pass through. `samplesPerBeat` sets the rate.
    void process (const juce::MidiBuffer& input, juce::MidiBuffer& output, int numSamples, double samplesPerBeat);
    void reset();

    int getNumHeldNotes() const { return numHeld; }

private:
    void advanceTo (juce::MidiBuffer& output, double position);
    void tick (juce::MidiBuffer& output, int position);
    void stopSounding (juce::MidiBuffer& output, int position);
    void addHeld (int note, float velocity);
    void removeHeld (int note);
    int nextNote (float& velocity);

    ArpParams params;
    std::array<int, maxHeldNotes> heldNotes {};
    std::array<float, maxHeldNotes> heldVelocities {};
    int numHeld = 0;

    int soundingNote = -1;
    double nextTickAt = 0.0;   // block-relative sample position
    double noteOffAt = 0.0;    // block-relative sample position
    double tickInterval = 0.0; // samples
    int stepIndex = 0;
    juce::Random random { 7 };
};
