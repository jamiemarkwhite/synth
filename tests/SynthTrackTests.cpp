#include "Groovebox.h"
#include "PatternGenerator.h"

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 512;
constexpr int samplesPerStep = 6000; // 120 BPM

GrooveParams cleanParams()
{
    GrooveParams params;
    params.sequencer.bpm = 120.0f;
    params.sequencer.followHost = false;
    params.mix.echo = 0.0f;
    params.mix.space = 0.0f;
    params.mix.masterDb = 0.0f;

    // Plain organ-like tones that stop as soon as the note ends.
    for (auto& track : params.tracks)
    {
        track.synth.waveform = Waveform::sine;
        track.synth.cutoff = 20000.0f;
        track.synth.attack = 0.001f;
        track.synth.sustain = 1.0f;
        track.synth.release = 0.005f;
        track.synth.gainDb = -6.0f;
    }

    return params;
}

struct Rig
{
    PatternBank bank;
    Groovebox groovebox { bank };

    explicit Rig (const GrooveParams& params = cleanParams())
    {
        groovebox.prepare (rate, block);
        groovebox.setParams (params);
    }

    juce::AudioBuffer<float> render (int numSamples, std::function<void (int, juce::MidiBuffer&)> beforeBlock = {})
    {
        juce::AudioBuffer<float> output (2, numSamples);
        juce::AudioBuffer<float> buffer (2, block);

        for (int start = 0; start < numSamples; start += block)
        {
            const auto count = std::min (block, numSamples - start);
            buffer.setSize (2, count, false, false, true);
            buffer.clear();

            juce::MidiBuffer midi;
            if (beforeBlock)
                beforeBlock (start, midi);

            groovebox.process (buffer, midi);
            for (int channel = 0; channel < 2; ++channel)
                output.copyFrom (channel, start, buffer, channel, 0, count);
        }

        return output;
    }
};

float level (const juce::AudioBuffer<float>& buffer, int start, int end)
{
    return buffer.getMagnitude (0, start, end - start);
}

int zeroCrossings (const juce::AudioBuffer<float>& buffer, int start, int end)
{
    int crossings = 0;
    const auto* data = buffer.getReadPointer (0);
    for (int i = start + 1; i < end; ++i)
        if (data[i - 1] < 0.0f && data[i] >= 0.0f)
            ++crossings;
    return crossings;
}
} // namespace

class SynthTrackTests final : public juce::UnitTest
{
public:
    SynthTrackTests() : juce::UnitTest ("Synth tracks") {}

    void runTest() override
    {
        beginTest ("Each track plays its own grid in its own octave");
        {
            auto params = cleanParams();
            params.sequencer.scale = Scale::major;
            params.sequencer.root = 9; // A
            params.tracks[0].octave = 1; // A1 = 110 Hz
            params.tracks[3].octave = 3; // A3 = 440 Hz
            params.tracks[0].noteLength = 4.0f;
            params.tracks[3].noteLength = 4.0f;

            Rig rig (params);
            rig.bank.setMelodyStep (0, 0, 0, 0, true);
            rig.bank.setMelodyStep (0, 3, 8, 0, true);
            rig.groovebox.setPlaying (true);
            const auto output = rig.render (samplesPerStep * 12);

            // 0.1 s windows: 11 crossings at 110 Hz, 44 at 440 Hz.
            expectWithinAbsoluteError (zeroCrossings (output, 1000, 5800), 11, 1);
            expectWithinAbsoluteError (zeroCrossings (output, 8 * samplesPerStep + 1000, 8 * samplesPerStep + 5800), 44, 1);
            expect (level (output, 5 * samplesPerStep, 7 * samplesPerStep) < 0.001f, "the bass note ended after 4 steps");
        }

        beginTest ("The lowest and highest octaves play the right pitch");
        {
            auto params = cleanParams();
            params.sequencer.scale = Scale::major;
            params.sequencer.root = 9;                     // A
            params.tracks[0].octave = SynthTracks::minOctave; // A-1 = 27.5 Hz
            params.tracks[0].noteLength = 8.0f;
            params.tracks[3].octave = SynthTracks::maxOctave; // A7 (MIDI 117) = 7040 Hz
            params.tracks[3].noteLength = 8.0f;

            Rig low (params);
            low.bank.setMelodyStep (0, 0, 0, 0, true);
            low.groovebox.setPlaying (true);
            const auto lowOutput = low.render (samplesPerStep * 8);
            expectWithinAbsoluteError (zeroCrossings (lowOutput, 4800, 24000), 11, 1); // 0.4 s

            Rig high (params);
            high.bank.setMelodyStep (0, 3, 0, 0, true);
            high.groovebox.setPlaying (true);
            const auto highOutput = high.render (samplesPerStep * 2);
            expectWithinAbsoluteError (zeroCrossings (highOutput, 1000, 5800), 704, 4); // 0.1 s
        }

        beginTest ("Rows above the highest MIDI note stay silent");
        {
            auto params = cleanParams();
            params.sequencer.scale = Scale::major;
            params.sequencer.root = 9;
            params.tracks[3].octave = SynthTracks::maxOctave; // row 6 would be MIDI 128
            Rig rig (params);
            rig.bank.setMelodyStep (0, 3, 0, 6, true);
            rig.groovebox.setPlaying (true);
            const auto output = rig.render (samplesPerStep * 2);
            expectEquals (output.getMagnitude (0, output.getNumSamples()), 0.0f);
        }

        beginTest ("A note drawn over several steps lasts that long");
        {
            auto params = cleanParams();
            params.tracks[2].noteLength = 0.5f; // ignored for long notes
            Rig rig (params);
            rig.bank.setNote (0, 2, 0, 0, 4);
            rig.groovebox.setPlaying (true);
            const auto output = rig.render (samplesPerStep * 6);
            expectGreaterThan (level (output, 3 * samplesPerStep, 4 * samplesPerStep - 200), 0.2f);
            expectLessThan (level (output, 4 * samplesPerStep + 600, 6 * samplesPerStep), 0.001f);
        }

        beginTest ("Long notes stop at the end of the pattern");
        {
            auto params = cleanParams();
            params.sequencer.length = 8;
            Rig rig (params);
            rig.bank.setNote (0, 2, 6, 0, 6);
            rig.groovebox.setPlaying (true);
            const auto output = rig.render (samplesPerStep * 10);
            expectGreaterThan (level (output, 7 * samplesPerStep, 8 * samplesPerStep - 200), 0.2f);
            expectLessThan (level (output, 8 * samplesPerStep + 600, 10 * samplesPerStep), 0.001f);
        }

        beginTest ("Recording a held key records how long it was held");
        {
            Rig rig;
            rig.groovebox.setLiveTrack (1);
            rig.groovebox.setPlaying (true);
            rig.groovebox.setRecording (true);
            const auto noteOn = 2 * samplesPerStep + 100;
            const auto noteOff = 6 * samplesPerStep - 300;
            rig.render (8 * samplesPerStep, [&] (int start, juce::MidiBuffer& midi)
            {
                if (noteOn >= start && noteOn < start + block)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 51, 0.8f), noteOn - start); // D#2: row 3, no scale
                if (noteOff >= start && noteOff < start + block)
                    midi.addEvent (juce::MidiMessage::noteOff (1, 51), noteOff - start);
            });

            expectEquals (rig.bank.getNoteLength (0, 1, 2, 3), 4);
        }

        beginTest ("A quick tap records a one-step note");
        {
            Rig rig;
            rig.groovebox.setLiveTrack (1);
            rig.groovebox.setPlaying (true);
            rig.groovebox.setRecording (true);
            rig.render (4 * samplesPerStep, [&] (int start, juce::MidiBuffer& midi)
            {
                if (start == 0)
                {
                    midi.addEvent (juce::MidiMessage::noteOn (1, 48, 0.8f), 10);
                    midi.addEvent (juce::MidiMessage::noteOff (1, 48), 400);
                }
            });

            expectEquals (rig.bank.getNoteLength (0, 1, 0, 0), 1);
        }

        beginTest ("Layered tracks sound together");
        {
            auto quiet = cleanParams(); // well under the limiter
            for (auto& track : quiet.tracks)
                track.synth.gainDb = -24.0f;

            Rig single (quiet);
            single.bank.setMelodyStep (0, 0, 0, 0, true);
            single.groovebox.setPlaying (true);
            const auto one = single.render (samplesPerStep);

            Rig layered (quiet);
            for (int track = 0; track < Seq::numSynthTracks; ++track)
                layered.bank.setMelodyStep (0, track, 0, 0, true);
            layered.groovebox.setPlaying (true);
            const auto all = layered.render (samplesPerStep);

            expectGreaterThan (level (all, 500, 2500), level (one, 500, 2500) * 1.5f);
        }

        beginTest ("A muted track's pattern is silent");
        {
            auto params = cleanParams();
            params.tracks[1].muted = true;
            Rig rig (params);
            rig.bank.setMelodyStep (0, 1, 0, 0, true);
            rig.groovebox.setPlaying (true);
            const auto output = rig.render (samplesPerStep * 2);
            expectEquals (output.getMagnitude (0, output.getNumSamples()), 0.0f);
        }

        beginTest ("Soloing a track silences every other pattern");
        {
            auto params = cleanParams();
            params.tracks[0].soloed = true;
            Rig rig (params);
            rig.bank.setMelodyStep (0, 2, 0, 0, true);
            rig.bank.setDrumStep (0, DrumMachine::kick, 0, true);
            rig.bank.setSampleStep (0, 0, 0, true);
            rig.groovebox.setPlaying (true);
            const auto output = rig.render (samplesPerStep * 2);
            expectEquals (output.getMagnitude (0, output.getNumSamples()), 0.0f);
        }

        beginTest ("Soloed tracks play");
        {
            auto params = cleanParams();
            params.tracks[0].soloed = true;
            params.drums.tracks[DrumMachine::kick].soloed = true;
            Rig synthRig (params);
            synthRig.bank.setMelodyStep (0, 0, 0, 0, true);
            synthRig.groovebox.setPlaying (true);
            expectGreaterThan (level (synthRig.render (samplesPerStep * 2), 500, 2500), 0.05f);

            Rig drumRig (params);
            drumRig.bank.setDrumStep (0, DrumMachine::kick, 0, true);
            drumRig.groovebox.setPlaying (true);
            const auto drums = drumRig.render (samplesPerStep * 2);
            expectGreaterThan (drums.getMagnitude (0, drums.getNumSamples()), 0.1f);
        }

        beginTest ("Mute wins over solo");
        {
            auto params = cleanParams();
            params.tracks[0].soloed = true;
            params.tracks[0].muted = true;
            Rig rig (params);
            rig.bank.setMelodyStep (0, 0, 0, 0, true);
            rig.groovebox.setPlaying (true);
            const auto output = rig.render (samplesPerStep * 2);
            expectEquals (output.getMagnitude (0, output.getNumSamples()), 0.0f);
        }

        beginTest ("Live playing still sounds while another track is soloed");
        {
            auto params = cleanParams();
            params.tracks[0].soloed = true;
            Rig rig (params);
            rig.groovebox.setLiveTrack (2);
            const auto output = rig.render (samplesPerStep * 2, [] (int start, juce::MidiBuffer& midi)
            {
                if (start == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
            });
            expectGreaterThan (level (output, 500, 2500), 0.05f);
        }

        beginTest ("A long note retriggered before it ends keeps playing");
        {
            auto params = cleanParams();
            params.tracks[1].noteLength = 8.0f;
            Rig rig (params);
            rig.bank.setMelodyStep (0, 1, 0, 0, true);
            rig.bank.setMelodyStep (0, 1, 4, 0, true); // same note again, while the first is held
            rig.groovebox.setPlaying (true);
            const auto output = rig.render (samplesPerStep * 14);

            // The first note's end (step 8) must not cut the second one, which ends at step 12.
            expectGreaterThan (level (output, 9 * samplesPerStep, 11 * samplesPerStep), 0.2f);
            expectLessThan (level (output, 13 * samplesPerStep, 14 * samplesPerStep), 0.001f);
        }

        beginTest ("Live notes play only the selected track");
        {
            auto params = cleanParams();
            params.tracks[0].synth.gainDb = -48.0f;
            params.tracks[2].synth.gainDb = 0.0f;

            auto playNote = [&params] (int liveTrack)
            {
                Rig rig (params);
                rig.groovebox.setLiveTrack (liveTrack);
                return rig.render (4096, [] (int start, juce::MidiBuffer& midi)
                {
                    if (start == 0)
                        midi.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), 0);
                });
            };

            const auto quiet = playNote (0);
            const auto loud = playNote (2);
            expectGreaterThan (level (loud, 1000, 4000), level (quiet, 1000, 4000) * 50.0f);
        }

        beginTest ("Switching the selected track releases its held notes");
        {
            Rig rig;
            rig.groovebox.setLiveTrack (2);
            const auto output = rig.render (8192, [&rig] (int start, juce::MidiBuffer& midi)
            {
                if (start == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), 0); // never released
                if (start == 2048)
                    rig.groovebox.setLiveTrack (0);
            });

            expectGreaterThan (level (output, 1000, 2000), 0.1f);
            expectLessThan (level (output, 4096, 8192), 0.001f);
        }

        beginTest ("Played notes are recorded into the selected track in its octave");
        {
            auto params = cleanParams();
            params.tracks[0].octave = 1;
            params.sequencer.scale = Scale::minorPentatonic;
            Rig rig (params);
            rig.groovebox.setLiveTrack (0);
            rig.groovebox.setPlaying (true);
            rig.groovebox.setRecording (true);
            rig.render (samplesPerStep * 2, [] (int start, juce::MidiBuffer& midi)
            {
                if (start == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 39, 0.8f), 10); // D#1 = row 1 in C minor pentatonic
            });

            expect (rig.bank.getMelodyStep (0, 0, 0, 1));
            expect (! rig.bank.getMelodyStep (0, SynthTracks::defaultLiveTrack, 0, 1));
        }

        beginTest ("Previews play on the requested track");
        {
            auto params = cleanParams();
            for (auto& track : params.tracks)
                track.synth.gainDb = -48.0f;
            params.tracks[3].synth.gainDb = 0.0f;
            Rig rig (params);
            rig.groovebox.previewNote (3, 69);
            const auto output = rig.render (4096);
            expectGreaterThan (level (output, 1000, 4000), 0.3f);
        }
    }
};

class MelodyStyleTests final : public juce::UnitTest
{
public:
    MelodyStyleTests() : juce::UnitTest ("Dice melody styles") {}

    void runTest() override
    {
        juce::Random random (7);
        constexpr int scaleSize = 5;
        constexpr auto scale = Scale::minorPentatonic;

        auto trackWithStyle = [] (SynthTracks::Style style)
        {
            for (int track = 0; track < Seq::numSynthTracks; ++track)
                if (SynthTracks::tracks[static_cast<size_t> (track)].style == style)
                    return track;
            return -1;
        };

        const auto bass = trackWithStyle (SynthTracks::Style::bass);
        const auto chords = trackWithStyle (SynthTracks::Style::chords);
        const auto stabs = trackWithStyle (SynthTracks::Style::stabs);

        beginTest ("Chords hold three notes every 8 steps");
        {
            for (int i = 0; i < 10; ++i)
            {
                PatternBank bank;
                PatternGenerator::randomiseMelody (bank, 0, chords, scale, random);
                for (int step = 0; step < Seq::numSteps; ++step)
                {
                    const auto notes = juce::countNumberOfBits (bank.getMelodyRows (0, chords, step));
                    expectEquals (notes, step % 8 == 0 ? 3 : 0);
                }
                expect (bank.getMelodyStep (0, chords, 0, 0), "starts on the root chord");
            }
        }

        beginTest ("The bass plays the chord's root at the start of each chord");
        {
            for (int i = 0; i < 10; ++i)
            {
                PatternBank bank;
                PatternGenerator::randomiseAllMelodies (bank, 0, scale, random);
                for (int step = 0; step < Seq::numSteps; step += 8)
                {
                    const auto chordRows = bank.getMelodyRows (0, chords, step);
                    const auto lowest = juce::findHighestSetBit (chordRows & (~chordRows + 1));
                    expect (bank.getMelodyStep (0, bass, step, lowest % scaleSize));
                }
            }
        }

        beginTest ("Stabs land off the beat");
        {
            for (int i = 0; i < 10; ++i)
            {
                PatternBank bank;
                PatternGenerator::randomiseAllMelodies (bank, 0, scale, random);
                for (int step = 0; step < Seq::numSteps; ++step)
                    if (bank.getMelodyRows (0, stabs, step) != 0)
                        expectEquals (step % 4, 2);
            }
        }

        beginTest ("With no scale, Dice only uses minor pentatonic notes");
        {
            const juce::uint32 pentatonicRows = (1u << 0) | (1u << 3) | (1u << 5) | (1u << 7) | (1u << 10) | (1u << 12) | (1u << 15);
            for (int i = 0; i < 20; ++i)
            {
                PatternBank bank;
                PatternGenerator::randomiseAllMelodies (bank, 0, Scale::none, random);
                bool anyNotes = false;
                for (int track = 0; track < Seq::numSynthTracks; ++track)
                    for (int step = 0; step < Seq::numSteps; ++step)
                    {
                        const auto rows = bank.getMelodyRows (0, track, step);
                        expect ((rows & ~pentatonicRows) == 0);
                        anyNotes = anyNotes || rows != 0;
                    }
                expect (anyNotes);
                expectEquals (juce::countNumberOfBits (bank.getMelodyRows (0, chords, 8)), 3);
            }
        }

        beginTest ("Chords and the sub bass hold for 8 steps, the sub on the chord's root");
        {
            const auto sub = trackWithStyle (SynthTracks::Style::sub);
            for (int i = 0; i < 10; ++i)
            {
                PatternBank bank;
                PatternGenerator::randomiseAllMelodies (bank, 0, scale, random);
                for (int step = 0; step < Seq::numSteps; step += 8)
                {
                    const auto chordRows = bank.getMelodyRows (0, chords, step);
                    const auto lowest = juce::findHighestSetBit (chordRows & (~chordRows + 1));
                    expectEquals (bank.getNoteLength (0, chords, step, lowest), 8);
                    expectEquals (bank.getNoteLength (0, sub, step, lowest % scaleSize), 8);
                }
            }
        }

        beginTest ("The demo has a bass, chords and a lead");
        {
            PatternBank bank;
            PatternGenerator::loadDemo (bank);
            for (auto track : { bass, chords, trackWithStyle (SynthTracks::Style::lead) })
            {
                bool any = false;
                for (int step = 0; step < Seq::numSteps; ++step)
                    any = any || bank.getMelodyRows (0, track, step) != 0;
                expect (any);
            }
        }
    }
};

static SynthTrackTests synthTrackTests;
static MelodyStyleTests melodyStyleTests;
