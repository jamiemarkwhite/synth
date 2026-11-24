#pragma once

#include "PatternBank.h"
#include "Scales.h"

// Makes patterns: random ones for the Dice buttons and the demo that loads on first launch.
namespace PatternGenerator
{
void randomiseDrums (PatternBank& bank, int pattern, juce::Random& random);
// Writes a melody in the style of the track (see SynthTracks). With no scale picked, Dice uses
// the notes of the minor pentatonic so random parts still fit together.
void randomiseMelody (PatternBank& bank, int pattern, int track, Scale scale, juce::Random& random);
void randomiseAllMelodies (PatternBank& bank, int pattern, Scale scale, juce::Random& random);
void loadDemo (PatternBank& bank);
} // namespace PatternGenerator
