#include "Groovebox.h"
#include "PatternGenerator.h"
#include "StepClock.h"

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;
constexpr int samplesPerStepAt120 = 6000; // 120 BPM: a 16th note is 0.125 s

GrooveParams dryParams (float bpm = 120.0f)
{
    GrooveParams params;
    params.sequencer.bpm = bpm;
    params.sequencer.followHost = false;
    params.mix.echo = 0.0f;
    params.mix.space = 0.0f;
    params.mix.masterDb = 0.0f;
    return params;
}

struct Rig
{
    PatternBank bank;
    Groovebox groovebox { bank };

    explicit Rig (const GrooveParams& params = dryParams())
    {
        groovebox.prepare (sampleRate, blockSize);
        groovebox.setParams (params);
    }

    // Renders `numSamples` in blocks. `beforeBlock` can add MIDI or poke the groovebox.
    juce::AudioBuffer<float> render (int numSamples,
                                     std::function<void (int, juce::MidiBuffer&)> beforeBlock = {},
                                     const HostTransport& host = {})
    {
        juce::AudioBuffer<float> output (2, numSamples);
        juce::AudioBuffer<float> block (2, blockSize);

        for (int start = 0; start < numSamples; start += blockSize)
        {
            const auto count = std::min (blockSize, numSamples - start);
            block.setSize (2, count, false, false, true);

            juce::MidiBuffer midi;
            if (beforeBlock)
                beforeBlock (start, midi);

            groovebox.process (block, midi, host);
            for (int channel = 0; channel < 2; ++channel)
                output.copyFrom (channel, start, block, channel, 0, count);
        }

        return output;
    }
};

int firstSampleAbove (const juce::AudioBuffer<float>& buffer, float threshold, int from = 0)
{
    for (int i = from; i < buffer.getNumSamples(); ++i)
        if (std::abs (buffer.getSample (0, i)) > threshold)
            return i;
    return -1;
}

float peakBetween (const juce::AudioBuffer<float>& buffer, int start, int end)
{
    return buffer.getMagnitude (0, start, end - start);
}
} // namespace

class StepClockTests final : public juce::UnitTest
{
public:
    StepClockTests() : juce::UnitTest ("StepClock") {}

    void runTest() override
    {
        const auto stepsPerSample = 1.0 / samplesPerStepAt120;

        beginTest ("Finds each step once at the right sample");
        {
            std::vector<std::pair<juce::int64, int>> found;
            for (int start = 0; start < 4 * samplesPerStepAt120; start += blockSize)
                StepClock::forEachStep (start * stepsPerSample, stepsPerSample,
                                        std::min (blockSize, 4 * samplesPerStepAt120 - start), 0.0f,
                                        [&] (juce::int64 step, int offset) { found.emplace_back (step, start + offset); });

            expectEquals (static_cast<int> (found.size()), 4);
            for (size_t i = 0; i < found.size(); ++i)
            {
                expectEquals (static_cast<int> (found[i].first), static_cast<int> (i));
                expectWithinAbsoluteError (found[i].second, static_cast<int> (i) * samplesPerStepAt120, 1);
            }
        }

        beginTest ("Full swing delays odd steps by half a step");
        {
            std::vector<int> times;
            for (int start = 0; start < 2 * samplesPerStepAt120; start += blockSize)
                StepClock::forEachStep (start * stepsPerSample, stepsPerSample,
                                        std::min (blockSize, 2 * samplesPerStepAt120 - start), 1.0f,
                                        [&] (juce::int64, int offset) { times.push_back (start + offset); });

            expectEquals (static_cast<int> (times.size()), 2);
            if (times.size() == 2)
                expectWithinAbsoluteError (times[1], samplesPerStepAt120 * 3 / 2, 1);
        }
    }
};

static StepClockTests stepClockTests;

class PatternBankTests final : public juce::UnitTest
{
public:
    PatternBankTests() : juce::UnitTest ("PatternBank") {}

    void runTest() override
    {
        beginTest ("Steps can be set, read and cleared");
        {
            PatternBank bank;
            expect (bank.isEmpty (0));
            bank.setDrumStep (0, 2, 31, true);
            bank.setMelodyStep (1, 3, 5, 15, true);
            expect (bank.getDrumStep (0, 2, 31));
            expect (bank.getMelodyStep (1, 3, 5, 15));
            expect (! bank.getMelodyStep (1, 2, 5, 15));
            expect (! bank.getDrumStep (0, 2, 30));
            expect (! bank.isEmpty (0));

            bank.setDrumStep (0, 2, 31, false);
            expect (bank.isEmpty (0));
        }

        beginTest ("Out of range cells are ignored");
        {
            PatternBank bank;
            bank.setDrumStep (9, 0, 0, true);
            bank.setDrumStep (0, 0, 40, true);
            bank.setMelodyStep (0, 0, 0, 20, true);
            bank.setMelodyStep (0, Seq::numSynthTracks, 0, 0, true);
            expect (bank.isEmpty (0));
            expect (! bank.getDrumStep (9, 0, 0));
        }

        beginTest ("Copy duplicates drums and melody");
        {
            PatternBank bank;
            bank.setDrumStep (0, 1, 4, true);
            bank.setMelodyStep (0, 1, 8, 3, true);
            bank.copyPattern (0, 2);
            expect (bank.getDrumStep (2, 1, 4));
            expect (bank.getMelodyStep (2, 1, 8, 3));
        }

        beginTest ("Saving and loading keeps everything");
        {
            PatternBank original;
            PatternGenerator::loadDemo (original);
            original.setDrumStep (3, 7, 31, true);
            original.setSongSlot (0, 0);
            original.setSongSlot (1, 3);
            original.setSelectedPattern (3);
            original.setSongMode (true);

            PatternBank loaded;
            loaded.fromValueTree (original.toValueTree());

            for (int pattern = 0; pattern < Seq::numPatterns; ++pattern)
                for (int step = 0; step < Seq::numSteps; ++step)
                {
                    for (int track = 0; track < Seq::numSynthTracks; ++track)
                        expect (loaded.getMelodyRows (pattern, track, step) == original.getMelodyRows (pattern, track, step));
                    for (int track = 0; track < Seq::numDrumTracks; ++track)
                        expect (loaded.getDrumStep (pattern, track, step) == original.getDrumStep (pattern, track, step));
                }

            expectEquals (loaded.getSongSlot (1), 3);
            expectEquals (loaded.getSongSlot (2), -1);
            expectEquals (loaded.getSelectedPattern(), 3);
            expect (loaded.isSongMode());
        }

        beginTest ("Notes can be held over several steps");
        {
            PatternBank bank;
            bank.setNote (0, 1, 4, 2, 3);
            expectEquals (bank.getNoteLength (0, 1, 4, 2), 3);
            expect (bank.getMelodyStep (0, 1, 4, 2));
            expect (! bank.getMelodyStep (0, 1, 5, 2), "held steps are not new notes");
            expect ((bank.getMelodyHolds (0, 1, 6) & (1u << 2)) != 0);
            expectEquals (bank.findNoteStart (0, 1, 6, 2), 4);
            expectEquals (bank.findNoteStart (0, 1, 7, 2), -1);
            expectEquals (bank.getNoteLength (0, 1, 5, 2), 0);

            bank.setNote (0, 1, 4, 2, 50);
            expectEquals (bank.getNoteLength (0, 1, 4, 2), Seq::numSteps - 4);
        }

        beginTest ("Removing, splitting and overwriting held notes");
        {
            PatternBank bank;
            bank.setNote (0, 0, 0, 5, 8);
            bank.removeNote (0, 0, 6, 5); // clicking any part removes the whole note
            expect (bank.isEmpty (0));
            for (int step = 0; step < Seq::numSteps; ++step)
                expectEquals (static_cast<int> (bank.getMelodyHolds (0, 0, step)), 0);

            bank.setNote (0, 0, 0, 5, 8);
            bank.setMelodyStep (0, 0, 4, 5, true); // a new note in the middle splits it
            expectEquals (bank.getNoteLength (0, 0, 0, 5), 4);
            expectEquals (bank.getNoteLength (0, 0, 4, 5), 4);

            bank.setNote (0, 0, 2, 5, 3); // covers the second note's start: its tail goes too
            expectEquals (bank.getNoteLength (0, 0, 0, 5), 2);
            expectEquals (bank.getNoteLength (0, 0, 2, 5), 3);
            expectEquals (bank.findNoteStart (0, 0, 6, 5), -1);
        }

        beginTest ("Held notes survive copying and saving");
        {
            PatternBank original;
            original.setNote (1, 3, 8, 7, 5);
            original.copyPattern (1, 2);
            expectEquals (original.getNoteLength (2, 3, 8, 7), 5);

            PatternBank loaded;
            loaded.fromValueTree (original.toValueTree());
            expectEquals (loaded.getNoteLength (1, 3, 8, 7), 5);
            expectEquals (loaded.getNoteLength (2, 3, 8, 7), 5);

            original.clearMelody (1, 3);
            expectEquals (static_cast<int> (original.getMelodyHolds (1, 3, 9)), 0);
        }

        beginTest ("An older save with a single melody loads it into the lead track");
        {
            juce::ValueTree tree (PatternBank::treeType);
            juce::ValueTree child ("Pattern");
            child.setProperty ("index", 0, nullptr);
            child.setProperty ("melody", "4,0,1", nullptr);
            tree.appendChild (child, nullptr);

            PatternBank bank;
            bank.fromValueTree (tree);
            expect (bank.getMelodyStep (0, PatternBank::legacyMelodyTrack, 0, 2));
            expect (bank.getMelodyStep (0, PatternBank::legacyMelodyTrack, 2, 0));
            expect (! bank.getMelodyStep (0, 0, 0, 2));
        }

        beginTest ("Random melodies stay on the grid");
        {
            PatternBank bank;
            juce::Random random (42);
            for (int i = 0; i < 20; ++i)
            {
                constexpr int lead = 2;
                PatternGenerator::randomiseMelody (bank, 0, lead, Scale::minorPentatonic, random);
                PatternGenerator::randomiseDrums (bank, 0, random);
                expect (bank.getMelodyStep (0, lead, 0, 0), "melody starts on the root");
                expect (bank.getDrumStep (0, 0, 0), "kick on the first beat");
                for (int step = 0; step < Seq::numSteps; ++step)
                    expect (bank.getMelodyRows (0, lead, step) < (1u << 11), "notes stay within two octaves");
            }
        }
    }
};

static PatternBankTests patternBankTests;

class ScaleTests final : public juce::UnitTest
{
public:
    ScaleTests() : juce::UnitTest ("Scales") {}

    void runTest() override
    {
        beginTest ("Rows follow the scale and wrap into the next octave");
        {
            const std::vector<int> expected { 48, 51, 53, 55, 58, 60, 63 };
            for (int row = 0; row < static_cast<int> (expected.size()); ++row)
                expectEquals (Scales::rowToNote (Scale::minorPentatonic, 0, 2, row), expected[static_cast<size_t> (row)]);

            expectEquals (Scales::rowToNote (Scale::major, 9, 2, 0), 57); // A2

            for (int row = 0; row < 16; ++row)
                expectEquals (Scales::rowToNote (Scale::none, 0, 2, row), 48 + row); // no scale: a semitone per row
        }

        beginTest ("Played notes snap to the nearest row");
        {
            expectEquals (Scales::nearestRow (Scale::minorPentatonic, 0, 2, 51, 16), 1);
            expectEquals (Scales::nearestRow (Scale::minorPentatonic, 0, 2, 49, 16), 0);
            expectEquals (Scales::nearestRow (Scale::minorPentatonic, 0, 2, 100, 16), -1);
        }
    }
};

static ScaleTests scaleTests;

class GrooveboxTests final : public juce::UnitTest
{
public:
    GrooveboxTests() : juce::UnitTest ("Groovebox") {}

    void runTest() override
    {
        beginTest ("Stopped with no input is silent");
        {
            Rig rig;
            PatternGenerator::loadDemo (rig.bank);
            const auto output = rig.render (samplesPerStepAt120 * 8);
            expectEquals (peakBetween (output, 0, output.getNumSamples()), 0.0f);
            expect (! rig.groovebox.isRunning());
            expectEquals (rig.groovebox.getCurrentStep(), -1);
        }

        beginTest ("A kick on step 4 plays exactly on step 4");
        {
            Rig rig;
            rig.bank.setDrumStep (0, DrumMachine::kick, 4, true);
            rig.groovebox.setPlaying (true);
            const auto output = rig.render (samplesPerStepAt120 * 6);
            expectWithinAbsoluteError (firstSampleAbove (output, 0.01f), 4 * samplesPerStepAt120, 32);
            expect (rig.groovebox.isRunning());
        }

        beginTest ("Swing pushes odd steps late");
        {
            auto params = dryParams();
            params.sequencer.swing = 1.0f;
            Rig rig (params);
            rig.bank.setDrumStep (0, DrumMachine::kick, 1, true);
            rig.groovebox.setPlaying (true);
            const auto output = rig.render (samplesPerStepAt120 * 3);
            expectWithinAbsoluteError (firstSampleAbove (output, 0.01f), samplesPerStepAt120 * 3 / 2, 32);
        }

        beginTest ("Muted tracks don't play from the sequencer");
        {
            auto params = dryParams();
            params.drums.tracks[DrumMachine::kick].muted = true;
            Rig rig (params);
            rig.bank.setDrumStep (0, DrumMachine::kick, 0, true);
            rig.groovebox.setPlaying (true);
            const auto output = rig.render (samplesPerStepAt120 * 2);
            expectEquals (peakBetween (output, 0, output.getNumSamples()), 0.0f);
        }

        beginTest ("Pattern length 16 repeats after 16 steps");
        {
            auto levelAtStep16 = [] (int length)
            {
                auto params = dryParams();
                params.sequencer.length = length;
                Rig rig (params);
                rig.bank.setDrumStep (0, DrumMachine::kick, 0, true);
                rig.groovebox.setPlaying (true);
                const auto output = rig.render (samplesPerStepAt120 * 17);
                return peakBetween (output, 16 * samplesPerStepAt120, 16 * samplesPerStepAt120 + 2000);
            };

            expectGreaterThan (levelAtStep16 (16), 0.3f);
            expectLessThan (levelAtStep16 (32), 0.05f);
        }

        beginTest ("Melody notes play the scale note for their row");
        {
            auto params = dryParams (60.0f); // a step is 0.25 s
            params.tracks[1].synth.waveform = Waveform::sine;
            params.tracks[1].synth.cutoff = 20000.0f;
            params.tracks[1].octave = 2;
            params.tracks[1].noteLength = 1.0f;
            params.sequencer.scale = Scale::major;
            params.sequencer.root = 9; // A
            Rig rig (params);
            rig.bank.setMelodyStep (0, 1, 0, 0, true); // A2 (MIDI 57) = 220 Hz
            rig.groovebox.setPlaying (true);
            const auto output = rig.render (static_cast<int> (0.25 * sampleRate));

            int crossings = 0;
            const auto* data = output.getReadPointer (0);
            for (int i = static_cast<int> (0.05 * sampleRate); i < static_cast<int> (0.2 * sampleRate); ++i)
                if (data[i - 1] < 0.0f && data[i] >= 0.0f)
                    ++crossings;

            expectWithinAbsoluteError (crossings, 33, 2);
        }

        beginTest ("Song mode plays the chained patterns in order");
        {
            auto params = dryParams();
            params.sequencer.length = 8;
            Rig rig (params);
            rig.bank.setSongSlot (0, 1);
            rig.bank.setSongSlot (3, 2);
            rig.bank.setSongMode (true);
            rig.groovebox.setPlaying (true);

            rig.render (samplesPerStepAt120 * 2);
            expectEquals (rig.groovebox.getPlayingPattern(), 1);
            expectEquals (rig.groovebox.getSongSlot(), 0);

            rig.render (samplesPerStepAt120 * 8);
            expectEquals (rig.groovebox.getPlayingPattern(), 2);
            expectEquals (rig.groovebox.getSongSlot(), 3);

            rig.render (samplesPerStepAt120 * 8);
            expectEquals (rig.groovebox.getPlayingPattern(), 1);
        }

        beginTest ("Stopping resets the playhead");
        {
            Rig rig;
            rig.groovebox.setPlaying (true);
            rig.render (samplesPerStepAt120 * 3);
            expectEquals (rig.groovebox.getCurrentStep(), 2);
            rig.groovebox.setPlaying (false);
            rig.render (blockSize);
            expectEquals (rig.groovebox.getCurrentStep(), -1);
        }

        beginTest ("Recording a pad hit snaps it to the nearest step without a double hit");
        {
            Rig rig;
            rig.groovebox.setPlaying (true);
            rig.groovebox.setRecording (true);

            // Hit just before step 4: it lands on step 4.
            const auto hitAt = 4 * samplesPerStepAt120 - 1000;
            const auto output = rig.render (6 * samplesPerStepAt120, [&] (int start, juce::MidiBuffer&)
            {
                if (hitAt >= start && hitAt < start + blockSize)
                    rig.groovebox.previewDrum (DrumMachine::snare);
            });

            expect (rig.bank.getDrumStep (0, DrumMachine::snare, 4));
            const auto firstHit = firstSampleAbove (output, 0.01f);
            expectWithinAbsoluteError (firstHit, hitAt, blockSize);

            // The snare is still ringing at step 4, so check it didn't restart there.
            const auto beforeStep = peakBetween (output, 4 * samplesPerStepAt120 - 600, 4 * samplesPerStepAt120);
            const auto afterStep = peakBetween (output, 4 * samplesPerStepAt120, 4 * samplesPerStepAt120 + 600);
            expectLessThan (afterStep, beforeStep * 1.5f + 0.01f);
        }

        beginTest ("Recording a played note adds it to the melody grid");
        {
            Rig rig;
            rig.groovebox.setPlaying (true);
            rig.groovebox.setRecording (true);
            const auto noteAt = 2 * samplesPerStepAt120 + 500;
            rig.render (3 * samplesPerStepAt120, [&] (int start, juce::MidiBuffer& midi)
            {
                if (noteAt >= start && noteAt < start + blockSize)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 51, 0.8f), noteAt - start); // D#2: row 3 with no scale
            });

            expect (rig.bank.getMelodyStep (0, SynthTracks::defaultLiveTrack, 2, 3));
        }

        beginTest ("MIDI channel 10 plays drums even when stopped");
        {
            Rig rig;
            const auto output = rig.render (4096, [] (int start, juce::MidiBuffer& midi)
            {
                if (start == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (10, 38, 1.0f), 0);
            });

            expectGreaterThan (peakBetween (output, 0, 4096), 0.05f);
            expect ((rig.groovebox.takeDrumHits() & (1u << DrumMachine::snare)) != 0);
        }

        beginTest ("Follows the host transport position");
        {
            auto params = dryParams();
            params.sequencer.followHost = true;
            Rig rig (params);
            rig.bank.setDrumStep (0, DrumMachine::kick, 4, true);

            HostTransport host;
            host.playing = true;
            host.hasPosition = true;
            host.bpm = 120.0;
            host.ppqPosition = 1.0; // beat 2 = step 4

            const auto output = rig.render (blockSize, {}, host);
            expectWithinAbsoluteError (firstSampleAbove (output, 0.01f), 0, 32);
            expectEquals (rig.groovebox.getTempo(), 120.0);
        }

        beginTest ("The limiter keeps a very loud pattern below full scale");
        {
            auto params = dryParams();
            params.mix.masterDb = 6.0f;
            params.mix.drumDb = 6.0f;
            params.mix.space = 1.0f;
            Rig rig (params);
            for (int track = 0; track < Seq::numDrumTracks; ++track)
                for (int step = 0; step < Seq::numSteps; ++step)
                    rig.bank.setDrumStep (0, track, step, true);
            rig.groovebox.setPlaying (true);

            const auto output = rig.render (samplesPerStepAt120 * 16);
            expectLessOrEqual (output.getMagnitude (0, output.getNumSamples()), 1.0f);
            expectGreaterThan (output.getMagnitude (0, output.getNumSamples()), 0.5f);
        }
    }
};

static GrooveboxTests grooveboxTests;
