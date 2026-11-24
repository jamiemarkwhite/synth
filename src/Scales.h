#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

enum class Scale
{
    none = 0, // every note: the grid goes up a semitone per row
    major,
    minor,
    majorPentatonic,
    minorPentatonic,
    blues,
    dorian
};

// The melody grid has one row per scale note, so with a scale picked anything drawn on it stays in key.
namespace Scales
{
inline juce::StringArray names()
{
    return { "No Scale", "Major", "Minor", "Major Pentatonic", "Minor Pentatonic", "Blues", "Dorian" };
}

inline juce::StringArray rootNames()
{
    return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

inline std::vector<int> intervals (Scale scale)
{
    switch (scale)
    {
        case Scale::none:            return { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
        case Scale::major:           return { 0, 2, 4, 5, 7, 9, 11 };
        case Scale::minor:           return { 0, 2, 3, 5, 7, 8, 10 };
        case Scale::majorPentatonic: return { 0, 2, 4, 7, 9 };
        case Scale::minorPentatonic: return { 0, 3, 5, 7, 10 };
        case Scale::blues:           return { 0, 3, 5, 6, 7, 10 };
        case Scale::dorian:          return { 0, 2, 3, 5, 7, 9, 10 };
    }

    return { 0 };
}

// Lowest note of the grid. Octave numbers follow Logic, where middle C (MIDI 60) is C3,
// so octave 2 with root C gives MIDI note 48 (C2).
inline int baseNote (int root, int octave)
{
    return 12 * (octave + 2) + root;
}

inline int rowToNote (Scale scale, int root, int octave, int row)
{
    static const std::array<std::vector<int>, 7> table {
        intervals (Scale::none), intervals (Scale::major), intervals (Scale::minor), intervals (Scale::majorPentatonic),
        intervals (Scale::minorPentatonic), intervals (Scale::blues), intervals (Scale::dorian)
    };

    const auto& steps = table[static_cast<size_t> (scale)];
    const auto size = static_cast<int> (steps.size());
    return baseNote (root, octave) + 12 * (row / size) + steps[static_cast<size_t> (row % size)];
}

// Row whose note is closest to `note`, or -1 if the note is more than a tone outside the grid.
inline int nearestRow (Scale scale, int root, int octave, int note, int numRows)
{
    int best = -1;
    int bestDistance = 3;

    for (int row = 0; row < numRows; ++row)
    {
        const auto distance = std::abs (rowToNote (scale, root, octave, row) - note);
        if (distance < bestDistance)
        {
            best = row;
            bestDistance = distance;
        }
    }

    return best;
}

inline juce::String noteName (int note)
{
    return juce::MidiMessage::getMidiNoteName (note, true, true, 3);
}
} // namespace Scales
