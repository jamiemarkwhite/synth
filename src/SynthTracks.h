#pragma once

#include "PatternBank.h"

// The synth tracks that play at the same time, each with its own sound and melody grid.
namespace SynthTracks
{
// How Dice writes a melody for the track.
enum class Style
{
    bass,   // low notes on the beat, mostly the root and fifth
    chords, // long held chords
    lead,   // a tune that wanders around the scale
    stabs,  // short off-beat chords
    sub     // the chord's root, held for the whole chord
};

struct Info
{
    const char* name;
    Style style;
    const char* preset; // default sound, by preset name
    int octave;         // minOctave..maxOctave; the grid's bottom row is the key's root in this octave (Logic naming)
    float noteLength;   // in steps
};

inline constexpr std::array<Info, Seq::numSynthTracks> tracks { {
    { "Bass", Style::bass, "Fat Bass", 1, 1.0f },
    { "Strings", Style::chords, "Strings", 2, 8.0f },
    { "Lead", Style::lead, "Chip Lead", 3, 0.5f },
    { "Keys", Style::stabs, "Tremolo Keys", 3, 1.0f },
    { "Sub Bass", Style::sub, "Sub Bass", 0, 1.0f },
} };

inline constexpr int defaultLiveTrack = 2; // the lead

// Logic's octave numbers, where middle C is C3. C-1 is MIDI 12 and C7 is MIDI 108; grid rows that
// would go above MIDI 127 stay silent.
inline constexpr int minOctave = -1;
inline constexpr int maxOctave = 7;
} // namespace SynthTracks
