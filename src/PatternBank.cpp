#include "PatternBank.h"

namespace
{
juce::uint32 bit (int index)
{
    return juce::uint32 (1) << static_cast<juce::uint32> (index);
}

void setBit (std::atomic<juce::uint32>& mask, int index, bool on)
{
    if (on)
        mask.fetch_or (bit (index));
    else
        mask.fetch_and (~bit (index));
}
} // namespace

PatternBank::PatternBank()
{
    clearAll();
}

void PatternBank::clearAll()
{
    for (int pattern = 0; pattern < Seq::numPatterns; ++pattern)
        clearPattern (pattern);

    for (auto& slot : song)
        slot.store (-1);

    selectedPattern.store (0);
    songMode.store (false);
    changed();
}

bool PatternBank::getDrumStep (int pattern, int track, int step) const
{
    if (! validPattern (pattern) || track < 0 || track >= Seq::numDrumTracks || step < 0 || step >= Seq::numSteps)
        return false;

    return (drums[static_cast<size_t> (pattern)][static_cast<size_t> (track)].load() & bit (step)) != 0;
}

void PatternBank::setDrumStep (int pattern, int track, int step, bool on)
{
    if (! validPattern (pattern) || track < 0 || track >= Seq::numDrumTracks || step < 0 || step >= Seq::numSteps)
        return;

    setBit (drums[static_cast<size_t> (pattern)][static_cast<size_t> (track)], step, on);
    changed();
}

bool PatternBank::getSampleStep (int pattern, int slot, int step) const
{
    if (! validPattern (pattern) || slot < 0 || slot >= Seq::numSampleTracks || step < 0 || step >= Seq::numSteps)
        return false;

    return (samples[static_cast<size_t> (pattern)][static_cast<size_t> (slot)].load() & bit (step)) != 0;
}

void PatternBank::setSampleStep (int pattern, int slot, int step, bool on)
{
    if (! validPattern (pattern) || slot < 0 || slot >= Seq::numSampleTracks || step < 0 || step >= Seq::numSteps)
        return;

    setBit (samples[static_cast<size_t> (pattern)][static_cast<size_t> (slot)], step, on);
    changed();
}

bool PatternBank::getMelodyStep (int pattern, int track, int step, int row) const
{
    if (row < 0 || row >= Seq::numMelodyRows)
        return false;

    return (getMelodyRows (pattern, track, step) & bit (row)) != 0;
}

juce::uint32 PatternBank::getMelodyRows (int pattern, int track, int step) const
{
    if (! validPattern (pattern) || ! validTrack (track) || step < 0 || step >= Seq::numSteps)
        return 0;

    return melody[static_cast<size_t> (pattern)][static_cast<size_t> (track)][static_cast<size_t> (step)].load();
}

juce::uint32 PatternBank::getMelodyHolds (int pattern, int track, int step) const
{
    if (! validPattern (pattern) || ! validTrack (track) || step < 0 || step >= Seq::numSteps)
        return 0;

    return holds[static_cast<size_t> (pattern)][static_cast<size_t> (track)][static_cast<size_t> (step)].load();
}

void PatternBank::setMelodyStep (int pattern, int track, int step, int row, bool on)
{
    if (! validCell (pattern, track, step, row))
        return;

    if (on)
    {
        // Starting a note inside a held note splits it: the earlier note now ends here.
        setBit (holdMask (pattern, track, step), row, false);
        setBit (startMask (pattern, track, step), row, true);
    }
    else
    {
        setBit (startMask (pattern, track, step), row, false);
        for (int s = step + 1; s < Seq::numSteps && (getMelodyHolds (pattern, track, s) & bit (row)) != 0; ++s)
            setBit (holdMask (pattern, track, s), row, false);
    }

    changed();
}

int PatternBank::getNoteLength (int pattern, int track, int step, int row) const
{
    if (! getMelodyStep (pattern, track, step, row))
        return 0;

    int length = 1;
    while (step + length < Seq::numSteps && (getMelodyHolds (pattern, track, step + length) & bit (row)) != 0)
        ++length;
    return length;
}

void PatternBank::setNote (int pattern, int track, int step, int row, int length)
{
    if (! validCell (pattern, track, step, row))
        return;

    const auto end = std::min (Seq::numSteps, step + std::max (1, length));

    // Clear the cells, and the held tail of any note whose start is overwritten.
    for (int s = step; s < end; ++s)
    {
        setBit (startMask (pattern, track, s), row, false);
        setBit (holdMask (pattern, track, s), row, false);
    }
    for (int s = end; s < Seq::numSteps && (getMelodyHolds (pattern, track, s) & bit (row)) != 0; ++s)
        setBit (holdMask (pattern, track, s), row, false);

    setBit (startMask (pattern, track, step), row, true);
    for (int s = step + 1; s < end; ++s)
        setBit (holdMask (pattern, track, s), row, true);

    changed();
}

int PatternBank::findNoteStart (int pattern, int track, int step, int row) const
{
    if (! validCell (pattern, track, step, row))
        return -1;

    for (int s = step; s >= 0; --s)
    {
        if (getMelodyStep (pattern, track, s, row))
            return s;
        if ((getMelodyHolds (pattern, track, s) & bit (row)) == 0)
            return -1;
    }

    return -1;
}

void PatternBank::removeNote (int pattern, int track, int step, int row)
{
    if (const auto start = findNoteStart (pattern, track, step, row); start >= 0)
        setMelodyStep (pattern, track, start, row, false);
}

int PatternBank::getSongSlot (int slot) const
{
    if (slot < 0 || slot >= Seq::songLength)
        return -1;

    return song[static_cast<size_t> (slot)].load();
}

void PatternBank::setSongSlot (int slot, int pattern)
{
    if (slot < 0 || slot >= Seq::songLength)
        return;

    song[static_cast<size_t> (slot)].store (validPattern (pattern) ? pattern : -1);
    changed();
}

void PatternBank::setSelectedPattern (int pattern)
{
    if (validPattern (pattern))
    {
        selectedPattern.store (pattern);
        changed();
    }
}

void PatternBank::setSongMode (bool on)
{
    songMode.store (on);
    changed();
}

void PatternBank::clearDrums (int pattern)
{
    if (! validPattern (pattern))
        return;

    for (auto& track : drums[static_cast<size_t> (pattern)])
        track.store (0);

    changed();
}

void PatternBank::clearMelody (int pattern, int track)
{
    if (! validPattern (pattern) || ! validTrack (track))
        return;

    for (auto& step : melody[static_cast<size_t> (pattern)][static_cast<size_t> (track)])
        step.store (0);
    for (auto& step : holds[static_cast<size_t> (pattern)][static_cast<size_t> (track)])
        step.store (0);

    changed();
}

void PatternBank::clearSamples (int pattern)
{
    if (! validPattern (pattern))
        return;

    for (auto& slot : samples[static_cast<size_t> (pattern)])
        slot.store (0);

    changed();
}

void PatternBank::clearPattern (int pattern)
{
    clearDrums (pattern);
    clearSamples (pattern);
    for (int track = 0; track < Seq::numSynthTracks; ++track)
        clearMelody (pattern, track);
}

void PatternBank::copyPattern (int from, int to)
{
    if (! validPattern (from) || ! validPattern (to) || from == to)
        return;

    for (size_t track = 0; track < Seq::numDrumTracks; ++track)
        drums[static_cast<size_t> (to)][track].store (drums[static_cast<size_t> (from)][track].load());

    for (size_t slot = 0; slot < Seq::numSampleTracks; ++slot)
        samples[static_cast<size_t> (to)][slot].store (samples[static_cast<size_t> (from)][slot].load());

    for (size_t track = 0; track < Seq::numSynthTracks; ++track)
        for (size_t step = 0; step < Seq::numSteps; ++step)
        {
            melody[static_cast<size_t> (to)][track][step].store (melody[static_cast<size_t> (from)][track][step].load());
            holds[static_cast<size_t> (to)][track][step].store (holds[static_cast<size_t> (from)][track][step].load());
        }

    changed();
}

bool PatternBank::isEmpty (int pattern) const
{
    if (! validPattern (pattern))
        return true;

    for (const auto& track : drums[static_cast<size_t> (pattern)])
        if (track.load() != 0)
            return false;

    for (const auto& slot : samples[static_cast<size_t> (pattern)])
        if (slot.load() != 0)
            return false;

    for (const auto& track : melody[static_cast<size_t> (pattern)])
        for (const auto& step : track)
            if (step.load() != 0)
                return false;

    return true;
}

juce::ValueTree PatternBank::toValueTree() const
{
    juce::ValueTree tree (treeType);
    tree.setProperty ("selected", selectedPattern.load(), nullptr);
    tree.setProperty ("songMode", songMode.load(), nullptr);

    juce::StringArray songSlots;
    for (const auto& slot : song)
        songSlots.add (juce::String (slot.load()));
    tree.setProperty ("song", songSlots.joinIntoString (","), nullptr);

    for (int pattern = 0; pattern < Seq::numPatterns; ++pattern)
    {
        juce::StringArray drumMasks, sampleMasks;
        for (const auto& track : drums[static_cast<size_t> (pattern)])
            drumMasks.add (juce::String::toHexString (static_cast<juce::int64> (track.load())));
        for (const auto& slot : samples[static_cast<size_t> (pattern)])
            sampleMasks.add (juce::String::toHexString (static_cast<juce::int64> (slot.load())));

        juce::ValueTree child ("Pattern");
        child.setProperty ("index", pattern, nullptr);
        child.setProperty ("drums", drumMasks.joinIntoString (","), nullptr);
        child.setProperty ("samples", sampleMasks.joinIntoString (","), nullptr);

        for (int track = 0; track < Seq::numSynthTracks; ++track)
        {
            juce::StringArray melodyMasks, holdMasks;
            for (const auto& step : melody[static_cast<size_t> (pattern)][static_cast<size_t> (track)])
                melodyMasks.add (juce::String::toHexString (static_cast<juce::int64> (step.load())));
            for (const auto& step : holds[static_cast<size_t> (pattern)][static_cast<size_t> (track)])
                holdMasks.add (juce::String::toHexString (static_cast<juce::int64> (step.load())));
            child.setProperty ("melody" + juce::String (track + 1), melodyMasks.joinIntoString (","), nullptr);
            child.setProperty ("hold" + juce::String (track + 1), holdMasks.joinIntoString (","), nullptr);
        }

        tree.appendChild (child, nullptr);
    }

    return tree;
}

void PatternBank::fromValueTree (const juce::ValueTree& tree)
{
    if (! tree.hasType (treeType))
        return;

    clearAll();

    auto readMasks = [] (const juce::var& value, auto& masks)
    {
        const auto parts = juce::StringArray::fromTokens (value.toString(), ",", {});
        for (int i = 0; i < std::min (parts.size(), static_cast<int> (masks.size())); ++i)
            masks[static_cast<size_t> (i)].store (static_cast<juce::uint32> (parts[i].getHexValue64()));
    };

    for (const auto& child : tree)
    {
        const int pattern = child.getProperty ("index", -1);
        if (! validPattern (pattern))
            continue;

        readMasks (child.getProperty ("drums"), drums[static_cast<size_t> (pattern)]);
        readMasks (child.getProperty ("samples"), samples[static_cast<size_t> (pattern)]);
        for (int track = 0; track < Seq::numSynthTracks; ++track)
        {
            readMasks (child.getProperty ("melody" + juce::String (track + 1)),
                       melody[static_cast<size_t> (pattern)][static_cast<size_t> (track)]);
            readMasks (child.getProperty ("hold" + juce::String (track + 1)),
                       holds[static_cast<size_t> (pattern)][static_cast<size_t> (track)]);
        }

        // Sessions saved before there were several synth tracks have one "melody"; it becomes the lead.
        if (child.hasProperty ("melody") && ! child.hasProperty ("melody1"))
            readMasks (child.getProperty ("melody"), melody[static_cast<size_t> (pattern)][legacyMelodyTrack]);
    }

    const auto songSlots = juce::StringArray::fromTokens (tree.getProperty ("song").toString(), ",", {});
    for (int i = 0; i < std::min (songSlots.size(), Seq::songLength); ++i)
        setSongSlot (i, songSlots[i].getIntValue());

    setSelectedPattern (tree.getProperty ("selected", 0));
    setSongMode (tree.getProperty ("songMode", false));
    changed();
}
