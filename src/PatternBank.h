#pragma once

#include <juce_data_structures/juce_data_structures.h>

// Sequencer sizes shared by the engine and the UI.
namespace Seq
{
constexpr int numPatterns = 4;
constexpr int numSteps = 32;
constexpr int numDrumTracks = 8;
constexpr int numSampleTracks = 4;
constexpr int numMelodyRows = 16;
constexpr int numSynthTracks = 5;
constexpr int songLength = 16;
} // namespace Seq

// Drum, sample and melody patterns plus the song chain. Every cell is an atomic bit, so the UI can
// edit while the audio thread plays without locks.
class PatternBank
{
public:
    PatternBank();

    bool getDrumStep (int pattern, int track, int step) const;
    void setDrumStep (int pattern, int track, int step, bool on);

    bool getSampleStep (int pattern, int slot, int step) const;
    void setSampleStep (int pattern, int slot, int step, bool on);

    // Melody notes start on a cell and can be held over the following steps.
    bool getMelodyStep (int pattern, int track, int step, int row) const; // a note starts here
    void setMelodyStep (int pattern, int track, int step, int row, bool on); // one-step note; off removes the whole note
    juce::uint32 getMelodyRows (int pattern, int track, int step) const;  // bit per row: notes starting here
    juce::uint32 getMelodyHolds (int pattern, int track, int step) const; // bit per row: notes held over from earlier

    int getNoteLength (int pattern, int track, int step, int row) const; // in steps; 0 if no note starts here
    void setNote (int pattern, int track, int step, int row, int length); // replaces whatever those cells held
    int findNoteStart (int pattern, int track, int step, int row) const; // start of the note covering a cell, or -1
    void removeNote (int pattern, int track, int step, int row);         // removes the note covering a cell

    int getSongSlot (int slot) const; // pattern index, or -1 for an empty slot
    void setSongSlot (int slot, int pattern);

    int getSelectedPattern() const { return selectedPattern.load(); }
    void setSelectedPattern (int pattern);

    bool isSongMode() const { return songMode.load(); }
    void setSongMode (bool on);

    void clearDrums (int pattern);
    void clearMelody (int pattern, int track);
    void clearSamples (int pattern);
    void clearPattern (int pattern);
    void copyPattern (int from, int to);
    bool isEmpty (int pattern) const;
    void clearAll();

    // Bumped on every change so the UI knows when to repaint.
    juce::uint32 getVersion() const { return version.load(); }

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& tree);

    static inline const juce::Identifier treeType { "Patterns" };
    static constexpr size_t legacyMelodyTrack = 2; // the lead

private:
    static bool validPattern (int pattern) { return pattern >= 0 && pattern < Seq::numPatterns; }
    static bool validTrack (int track) { return track >= 0 && track < Seq::numSynthTracks; }
    static bool validCell (int pattern, int track, int step, int row)
    {
        return validPattern (pattern) && validTrack (track) && step >= 0 && step < Seq::numSteps && row >= 0
               && row < Seq::numMelodyRows;
    }

    std::atomic<juce::uint32>& startMask (int pattern, int track, int step)
    {
        return melody[static_cast<size_t> (pattern)][static_cast<size_t> (track)][static_cast<size_t> (step)];
    }

    std::atomic<juce::uint32>& holdMask (int pattern, int track, int step)
    {
        return holds[static_cast<size_t> (pattern)][static_cast<size_t> (track)][static_cast<size_t> (step)];
    }
    void changed() { version.fetch_add (1); }

    std::array<std::array<std::atomic<juce::uint32>, Seq::numDrumTracks>, Seq::numPatterns> drums;
    std::array<std::array<std::atomic<juce::uint32>, Seq::numSampleTracks>, Seq::numPatterns> samples;
    using MelodyTrack = std::array<std::atomic<juce::uint32>, Seq::numSteps>; // bit per row, per step
    std::array<std::array<MelodyTrack, Seq::numSynthTracks>, Seq::numPatterns> melody, holds;
    std::array<std::atomic<int>, Seq::songLength> song;
    std::atomic<int> selectedPattern { 0 };
    std::atomic<bool> songMode { false };
    std::atomic<juce::uint32> version { 0 };
};
