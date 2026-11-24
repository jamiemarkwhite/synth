#include "PatternGenerator.h"
#include "DrumMachine.h"
#include "SynthTracks.h"

namespace PatternGenerator
{
void randomiseDrums (PatternBank& bank, int pattern, juce::Random& random)
{
    bank.clearDrums (pattern);

    auto chance = [&random] (float probability) { return random.nextFloat() < probability; };
    auto set = [&] (int track, int step) { bank.setDrumStep (pattern, track, step, true); };

    // 32 steps = two bars of 16th notes. Beats fall on every 4th step.
    for (int step = 0; step < Seq::numSteps; ++step)
    {
        const bool downbeat = step % 16 == 0;
        const bool beat = step % 4 == 0;
        const bool even = step % 2 == 0;

        if (downbeat || (step % 16 == 8 && chance (0.6f)) || (even && ! beat && chance (0.15f)) || (! even && chance (0.04f)))
            set (DrumMachine::kick, step);

        if (step % 8 == 4)
            set (DrumMachine::snare, step);
        else if (! beat && chance (0.05f))
            set (DrumMachine::snare, step);
    }

    if (chance (0.4f))
        for (int step = 4; step < Seq::numSteps; step += 8)
            set (DrumMachine::clap, step);

    const bool sixteenths = chance (0.4f);
    for (int step = 0; step < Seq::numSteps; step += sixteenths ? 1 : 2)
        if (! chance (0.1f))
            set (DrumMachine::closedHat, step);

    for (auto step : { 14, 30 })
    {
        if (chance (0.45f))
        {
            set (DrumMachine::openHat, step);
            bank.setDrumStep (pattern, DrumMachine::closedHat, step, false);
        }
    }

    // Occasional tom fill at the end of the second bar.
    if (chance (0.35f))
    {
        set (DrumMachine::highTom, 28);
        set (DrumMachine::highTom, 29);
        set (DrumMachine::lowTom, 30);
        set (DrumMachine::lowTom, 31);
    }

    if (chance (0.2f))
        for (int i = 0; i < 3; ++i)
            set (DrumMachine::cowbell, random.nextInt (Seq::numSteps));
}

namespace
{
// Dice works in scale degrees: degree 0 is the root, `perOctave` degrees later is the root an
// octave up. `rows` gives the grid row of each degree that fits on the grid.
struct Palette
{
    std::vector<int> rows;
    int perOctave = 1;

    int size() const { return static_cast<int> (rows.size()); }
    int row (int degree) const { return degree >= 0 && degree < size() ? rows[static_cast<size_t> (degree)] : -1; }

    int degreeOf (int gridRow) const
    {
        for (int degree = 0; degree < size(); ++degree)
            if (rows[static_cast<size_t> (degree)] == gridRow)
                return degree;
        return 0;
    }

    // Degree closest to a fifth above the root, e.g. G in C major or C minor pentatonic.
    int fifth() const { return juce::roundToInt (static_cast<float> (perOctave) * 7.0f / 12.0f); }
};

Palette paletteFor (Scale scale)
{
    Palette palette;

    if (scale == Scale::none)
    {
        // Every grid row is a semitone, so pick the minor pentatonic's rows out of them.
        const auto intervals = Scales::intervals (Scale::minorPentatonic);
        palette.perOctave = static_cast<int> (intervals.size());
        for (int degree = 0;; ++degree)
        {
            const auto row = 12 * (degree / palette.perOctave) + intervals[static_cast<size_t> (degree % palette.perOctave)];
            if (row >= Seq::numMelodyRows)
                break;
            palette.rows.push_back (row);
        }
    }
    else
    {
        palette.perOctave = static_cast<int> (Scales::intervals (scale).size());
        for (int row = 0; row < Seq::numMelodyRows; ++row)
            palette.rows.push_back (row);
    }

    return palette;
}

int findTrack (SynthTracks::Style style)
{
    for (int track = 0; track < Seq::numSynthTracks; ++track)
        if (SynthTracks::tracks[static_cast<size_t> (track)].style == style)
            return track;
    return -1;
}

// Degree of the lowest note of the chord playing at `step` on the chords track, or 0 (the root)
// if none, so bass and stabs follow the chords.
int chordRootAt (const PatternBank& bank, const Palette& palette, int pattern, int step)
{
    const auto chordTrack = findTrack (SynthTracks::Style::chords);
    for (int s = step; s >= 0; --s)
        if (const auto rows = bank.getMelodyRows (pattern, chordTrack, s); rows != 0)
            return palette.degreeOf (juce::findHighestSetBit (rows & (~rows + 1))); // lowest set bit
    return 0;
}
} // namespace

void randomiseMelody (PatternBank& bank, int pattern, int track, Scale scale, juce::Random& random)
{
    if (track < 0 || track >= Seq::numSynthTracks)
        return;

    const auto palette = paletteFor (scale);
    const auto perOctave = palette.perOctave;

    bank.clearMelody (pattern, track);
    auto set = [&] (int step, int degree)
    {
        if (const auto row = palette.row (degree); row >= 0)
            bank.setMelodyStep (pattern, track, step, row, true);
    };
    auto chord = [&] (int step, int root, int notes)
    {
        for (int i = 0; i < notes; ++i)
            set (step, root + 2 * i);
    };
    auto chance = [&random] (float probability) { return random.nextFloat() < probability; };

    switch (SynthTracks::tracks[static_cast<size_t> (track)].style)
    {
        case SynthTracks::Style::bass:
        {
            const std::array<int, 4> choices { 0, 0, palette.fifth(), perOctave };
            for (int step = 0; step < Seq::numSteps; step += 2)
            {
                const auto root = chordRootAt (bank, palette, pattern, step) % perOctave;
                if (step % 8 == 0)
                    set (step, root);
                else if (chance (0.35f))
                    set (step, root + choices[static_cast<size_t> (random.nextInt (4))]);
            }
            break;
        }

        case SynthTracks::Style::sub:
        {
            for (int step = 0; step < Seq::numSteps; step += 8)
                if (const auto row = palette.row (chordRootAt (bank, palette, pattern, step) % perOctave); row >= 0)
                    bank.setNote (pattern, track, step, row, 8);
            break;
        }

        case SynthTracks::Style::chords:
        {
            // A new chord every 8 steps, starting on the root chord. Chords on the 4th to 6th
            // degrees where the whole chord fits on the grid, otherwise the 2nd or 3rd.
            std::vector<int> roots;
            for (int root : { 3, 4, 5 })
                if (root < perOctave && palette.row (root + 4) >= 0)
                    roots.push_back (root);
            if (roots.empty())
                for (int root : { 1, 2 })
                    if (palette.row (root + 4) >= 0)
                        roots.push_back (root);
            roots.push_back (0);

            for (int step = 0; step < Seq::numSteps; step += 8)
            {
                const auto root = step == 0 ? 0 : roots[static_cast<size_t> (random.nextInt (static_cast<int> (roots.size())))];
                for (int i = 0; i < 3; ++i)
                    if (const auto row = palette.row (root + 2 * i); row >= 0)
                        bank.setNote (pattern, track, step, row, 8);
            }
            break;
        }

        case SynthTracks::Style::lead:
        {
            // A random walk around the scale. Starting and ending on the root makes it sound finished.
            const auto topDegree = std::min (palette.size() - 1, perOctave * 2);
            const auto density = 0.3f + 0.25f * random.nextFloat();
            int degree = 0;

            for (int step = 0; step < Seq::numSteps; ++step)
            {
                if (step != 0 && random.nextFloat() >= density)
                    continue;

                if (step != 0)
                    degree = juce::jlimit (0, topDegree, degree + random.nextInt ({ -2, 3 }));

                set (step, step == 0 ? 0 : degree);
            }

            set (28, perOctave);
            break;
        }

        case SynthTracks::Style::stabs:
        {
            for (int step = 2; step < Seq::numSteps; step += 4)
                if (chance (0.7f))
                    chord (step, chordRootAt (bank, palette, pattern, step) % perOctave, 2);
            break;
        }
    }
}

void randomiseAllMelodies (PatternBank& bank, int pattern, Scale scale, juce::Random& random)
{
    // Chords first, so the bass and stabs can follow them.
    const auto chordTrack = findTrack (SynthTracks::Style::chords);
    randomiseMelody (bank, pattern, chordTrack, scale, random);

    for (int track = 0; track < Seq::numSynthTracks; ++track)
        if (track != chordTrack)
            randomiseMelody (bank, pattern, track, scale, random);
}

void loadDemo (PatternBank& bank)
{
    bank.clearAll();

    constexpr int pattern = 0;
    auto drum = [&bank] (int track, std::initializer_list<int> steps)
    {
        for (auto step : steps)
            bank.setDrumStep (pattern, track, step, true);
    };

    drum (DrumMachine::kick, { 0, 7, 10, 16, 23, 26 });
    drum (DrumMachine::snare, { 4, 12, 20, 28 });
    drum (DrumMachine::clap, { 28 });
    drum (DrumMachine::closedHat, { 0, 2, 4, 6, 8, 10, 12, 16, 18, 20, 22, 24, 26, 28 });
    drum (DrumMachine::openHat, { 14, 30 });

    // With the default of no scale, each row is a semitone above the track's C. The parts use
    // C minor pentatonic notes (C Eb F G Bb). Pairs are {step, row}.
    auto notes = [&bank] (SynthTracks::Style style, std::initializer_list<std::pair<int, int>> stepsAndRows)
    {
        for (const auto& [step, row] : stepsAndRows)
            bank.setMelodyStep (pattern, findTrack (style), step, row, true);
    };

    notes (SynthTracks::Style::lead, { { 0, 0 }, { 3, 0 }, { 6, 5 }, { 8, 7 }, { 11, 7 }, { 14, 10 },
                                       { 16, 12 }, { 19, 12 }, { 22, 10 }, { 24, 7 }, { 27, 5 }, { 30, 3 } });
    notes (SynthTracks::Style::bass, { { 0, 0 }, { 7, 0 }, { 10, 7 }, { 16, 3 }, { 23, 3 }, { 26, 10 } });

    // Chords and sub bass held for a bar each: C minor, then E flat major.
    for (int row : { 0, 3, 7 })
        bank.setNote (pattern, findTrack (SynthTracks::Style::chords), 0, row, 16);
    for (int row : { 3, 7, 10 })
        bank.setNote (pattern, findTrack (SynthTracks::Style::chords), 16, row, 16);
    bank.setNote (pattern, findTrack (SynthTracks::Style::sub), 0, 0, 16);
    bank.setNote (pattern, findTrack (SynthTracks::Style::sub), 16, 3, 16);

    bank.setSelectedPattern (0);
}
} // namespace PatternGenerator
