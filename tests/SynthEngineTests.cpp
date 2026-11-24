#include "SynthEngine.h"

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;

// Renders `seconds` of audio in blocks. The note-on goes into the first block and the
// optional note-off into the block at `noteOffAt` seconds.
juce::AudioBuffer<float> renderNote (SynthEngine& engine, int note, double seconds, double noteOffAt = -1.0)
{
    const auto totalSamples = static_cast<int> (seconds * sampleRate);
    const auto noteOffSample = noteOffAt < 0.0 ? -1 : static_cast<int> (noteOffAt * sampleRate);

    juce::AudioBuffer<float> output (2, totalSamples);
    juce::AudioBuffer<float> block (2, blockSize);

    for (int start = 0; start < totalSamples; start += blockSize)
    {
        const auto numSamples = std::min (blockSize, totalSamples - start);
        block.setSize (2, numSamples, false, false, true);

        juce::MidiBuffer midi;
        if (start == 0 && note >= 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);
        if (noteOffSample >= start && noteOffSample < start + numSamples)
            midi.addEvent (juce::MidiMessage::noteOff (1, note), noteOffSample - start);

        engine.render (block, midi);

        for (int channel = 0; channel < 2; ++channel)
            output.copyFrom (channel, start, block, channel, 0, numSamples);
    }

    return output;
}

std::unique_ptr<SynthEngine> makeEngine (const SynthParams& params)
{
    auto engine = std::make_unique<SynthEngine>();
    engine->setParams (params);
    engine->prepare (sampleRate, blockSize, 2);
    return engine;
}

float peak (const juce::AudioBuffer<float>& buffer, int start, int numSamples)
{
    return buffer.getMagnitude (0, start, numSamples);
}

int toSample (double seconds)
{
    return static_cast<int> (seconds * sampleRate);
}

int risingZeroCrossings (const juce::AudioBuffer<float>& buffer, double fromSeconds, double toSeconds)
{
    const auto* data = buffer.getReadPointer (0);
    int crossings = 0;
    for (int i = toSample (fromSeconds) + 1; i < toSample (toSeconds); ++i)
        if (data[i - 1] < 0.0f && data[i] >= 0.0f)
            ++crossings;
    return crossings;
}

float rms (const juce::AudioBuffer<float>& buffer, double fromSeconds, double toSeconds)
{
    return buffer.getRMSLevel (0, toSample (fromSeconds), toSample (toSeconds) - toSample (fromSeconds));
}
} // namespace

class SynthEngineTests final : public juce::UnitTest
{
public:
    SynthEngineTests() : juce::UnitTest ("SynthEngine") {}

    void runTest() override
    {
        beginTest ("No MIDI gives silence");
        {
            auto engine = makeEngine ({});
            const auto output = renderNote (*engine, -1, 0.5);
            expectEquals (peak (output, 0, output.getNumSamples()), 0.0f);
        }

        beginTest ("Note on produces sound within range");
        {
            auto engine = makeEngine ({});
            const auto output = renderNote (*engine, 60, 0.5);
            const auto level = peak (output, 0, output.getNumSamples());
            expectGreaterThan (level, 0.05f);
            expectLessOrEqual (level, 1.5f);
        }

        beginTest ("A4 sine plays at 440 Hz");
        {
            SynthParams params;
            params.waveform = Waveform::sine;
            params.cutoff = 20000.0f;
            auto engine = makeEngine (params);
            const auto output = renderNote (*engine, 69, 1.5);

            // Count rising zero crossings over one second, skipping the attack.
            const auto* data = output.getReadPointer (0);
            const auto start = static_cast<int> (0.25 * sampleRate);
            const auto end = start + static_cast<int> (sampleRate);
            int crossings = 0;
            for (int i = start + 1; i < end; ++i)
                if (data[i - 1] < 0.0f && data[i] >= 0.0f)
                    ++crossings;

            expectWithinAbsoluteError (crossings, 440, 3);
        }

        beginTest ("Release fades to silence after note off");
        {
            SynthParams params;
            params.release = 0.1f;
            auto engine = makeEngine (params);
            const auto output = renderNote (*engine, 60, 1.0, 0.3);

            const auto tailStart = static_cast<int> (0.6 * sampleRate);
            expectGreaterThan (peak (output, 0, tailStart / 2), 0.05f);
            expectLessThan (peak (output, tailStart, output.getNumSamples() - tailStart), 1.0e-4f);
        }

        beginTest ("Low cutoff reduces saw level");
        {
            SynthParams bright;
            bright.waveform = Waveform::saw;
            bright.cutoff = 20000.0f;
            bright.resonance = 0.7f;

            auto dark = bright;
            dark.cutoff = 200.0f;

            auto brightEngine = makeEngine (bright);
            auto darkEngine = makeEngine (dark);
            const auto brightOutput = renderNote (*brightEngine, 72, 0.5);
            const auto darkOutput = renderNote (*darkEngine, 72, 0.5);

            const auto n = brightOutput.getNumSamples();
            expectLessThan (darkOutput.getRMSLevel (0, 0, n), brightOutput.getRMSLevel (0, 0, n) * 0.5f);
        }

        beginTest ("Plays eight notes at once");
        {
            auto engine = makeEngine ({});
            juce::AudioBuffer<float> block (2, blockSize);
            juce::MidiBuffer midi;
            for (int note = 60; note < 60 + SynthEngine::numVoices; ++note)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.5f), 0);

            engine->render (block, midi);
            expect (! block.hasBeenCleared());
            expectGreaterThan (block.getMagnitude (0, 0, blockSize), 0.0f);
        }
    }
};

static SynthEngineTests synthEngineTests;

class LfoTests final : public juce::UnitTest
{
public:
    LfoTests() : juce::UnitTest ("Lfo") {}

    void runTest() override
    {
        beginTest ("Every shape stays within -1..1");
        for (auto shape : { LfoShape::sine, LfoShape::triangle, LfoShape::saw, LfoShape::square, LfoShape::sampleAndHold })
        {
            Lfo lfo;
            lfo.prepare (sampleRate);
            lfo.setShape (shape);
            lfo.setRate (7.0f);

            float lowest = 1.0f;
            float highest = -1.0f;
            for (int i = 0; i < toSample (2.0); i += 16)
            {
                lowest = std::min (lowest, lfo.getValue());
                highest = std::max (highest, lfo.getValue());
                lfo.advance (16);
            }

            expectGreaterOrEqual (lowest, -1.0f);
            expectLessOrEqual (highest, 1.0f);
            expectGreaterThan (highest - lowest, 0.5f, "shape should move");
        }

        beginTest ("Sine peaks a quarter of the way through a cycle");
        {
            Lfo lfo;
            lfo.prepare (sampleRate);
            lfo.setRate (1.0f);
            expectWithinAbsoluteError (lfo.getValue(), 0.0f, 1.0e-6f);
            lfo.advance (toSample (0.25));
            expectWithinAbsoluteError (lfo.getValue(), 1.0f, 1.0e-4f);
        }

        beginTest ("Square is high for the first half and low for the second");
        {
            Lfo lfo;
            lfo.prepare (sampleRate);
            lfo.setShape (LfoShape::square);
            lfo.setRate (1.0f);
            lfo.advance (toSample (0.25));
            expectEquals (lfo.getValue(), 1.0f);
            lfo.advance (toSample (0.5));
            expectEquals (lfo.getValue(), -1.0f);
        }

        beginTest ("Sample and hold changes value each cycle");
        {
            Lfo lfo;
            lfo.prepare (sampleRate);
            lfo.setShape (LfoShape::sampleAndHold);
            lfo.setRate (10.0f);

            const auto first = lfo.getValue();
            lfo.advance (toSample (0.05));
            expectEquals (lfo.getValue(), first, "holds within a cycle");
            lfo.advance (toSample (0.1));
            expectNotEquals (lfo.getValue(), first, "changes on the next cycle");
        }
    }
};

static LfoTests lfoTests;

class LfoModulationTests final : public juce::UnitTest
{
public:
    LfoModulationTests() : juce::UnitTest ("LFO modulation") {}

    void runTest() override
    {
        beginTest ("Destination Off leaves the sound unchanged");
        {
            SynthParams plain;
            auto withLfo = plain;
            withLfo.lfos[0] = { LfoShape::square, LfoDestination::off, 3.0f, 1.0f };

            auto plainEngine = makeEngine (plain);
            auto lfoEngine = makeEngine (withLfo);
            const auto expected = renderNote (*plainEngine, 60, 0.5);
            const auto actual = renderNote (*lfoEngine, 60, 0.5);

            float maxDifference = 0.0f;
            for (int i = 0; i < expected.getNumSamples(); ++i)
                maxDifference = std::max (maxDifference, std::abs (expected.getSample (0, i) - actual.getSample (0, i)));

            expectEquals (maxDifference, 0.0f);
        }

        beginTest ("Square LFO on pitch shifts A4 up then down a semitone");
        {
            SynthParams params;
            params.waveform = Waveform::sine;
            params.cutoff = 20000.0f;
            params.lfos[0] = { LfoShape::square, LfoDestination::pitch, 1.0f, 1.0f / SynthEngine::maxPitchSemitones };
            auto engine = makeEngine (params);
            const auto output = renderNote (*engine, 69, 1.0);

            // 0.3 s windows inside each half of the LFO cycle.
            const auto up = 440.0 * std::pow (2.0, 1.0 / 12.0) * 0.3;
            const auto down = 440.0 * std::pow (2.0, -1.0 / 12.0) * 0.3;
            expectWithinAbsoluteError (static_cast<double> (risingZeroCrossings (output, 0.1, 0.4)), up, 3.0);
            expectWithinAbsoluteError (static_cast<double> (risingZeroCrossings (output, 0.6, 0.9)), down, 3.0);
        }

        beginTest ("Square LFO on cutoff makes the second half darker");
        {
            SynthParams params;
            params.waveform = Waveform::saw;
            params.cutoff = 1000.0f;
            params.lfos[1] = { LfoShape::square, LfoDestination::cutoff, 1.0f, 1.0f };
            auto engine = makeEngine (params);
            const auto output = renderNote (*engine, 72, 1.0);

            expectLessThan (rms (output, 0.6, 0.9), rms (output, 0.1, 0.4) * 0.5f);
        }

        beginTest ("Full-depth sine LFO on amp dips to near silence");
        {
            SynthParams params;
            params.sustain = 1.0f;
            params.lfos[0] = { LfoShape::sine, LfoDestination::amp, 2.0f, 1.0f };
            auto engine = makeEngine (params);
            const auto output = renderNote (*engine, 60, 1.0);

            float loudest = 0.0f;
            float quietest = 1.0f;
            for (double t = 0.05; t < 0.95; t += 0.01)
            {
                const auto level = peak (output, toSample (t), toSample (0.01));
                loudest = std::max (loudest, level);
                quietest = std::min (quietest, level);
            }

            expectGreaterThan (loudest, 0.1f);
            expectLessThan (quietest, loudest * 0.05f);
        }

        beginTest ("Zero depth leaves the sound unchanged");
        {
            SynthParams plain;
            auto withLfo = plain;
            withLfo.lfos[0] = { LfoShape::saw, LfoDestination::cutoff, 4.0f, 0.0f };
            withLfo.lfos[1] = { LfoShape::sine, LfoDestination::amp, 4.0f, 0.0f };

            auto plainEngine = makeEngine (plain);
            auto lfoEngine = makeEngine (withLfo);
            const auto expected = renderNote (*plainEngine, 60, 0.5);
            const auto actual = renderNote (*lfoEngine, 60, 0.5);

            float maxDifference = 0.0f;
            for (int i = 0; i < expected.getNumSamples(); ++i)
                maxDifference = std::max (maxDifference, std::abs (expected.getSample (0, i) - actual.getSample (0, i)));

            expectLessThan (maxDifference, 1.0e-6f);
        }
    }
};

static LfoModulationTests lfoModulationTests;

class LfoSyncTests final : public juce::UnitTest
{
public:
    LfoSyncTests() : juce::UnitTest ("LFO sync and retrigger") {}

    void runTest() override
    {
        beginTest ("Synced LFO at 60 BPM and 1/4 runs at 1 Hz");
        {
            SynthParams params;
            params.waveform = Waveform::sine;
            params.cutoff = 20000.0f;
            params.lfos[0] = { LfoShape::square, LfoDestination::pitch, 7.0f, 1.0f / SynthEngine::maxPitchSemitones };
            params.lfos[0].sync = true;
            params.lfos[0].division = 4; // 1/4
            auto engine = makeEngine (params);
            engine->setTempo (60.0);
            const auto output = renderNote (*engine, 69, 1.0);

            const auto up = 440.0 * std::pow (2.0, 1.0 / 12.0) * 0.3;
            const auto down = 440.0 * std::pow (2.0, -1.0 / 12.0) * 0.3;
            expectWithinAbsoluteError (static_cast<double> (risingZeroCrossings (output, 0.1, 0.4)), up, 3.0);
            expectWithinAbsoluteError (static_cast<double> (risingZeroCrossings (output, 0.6, 0.9)), down, 3.0);
        }

        beginTest ("Retrigger restarts the LFO on each note");
        {
            // Square tremolo at 1 Hz: the first half of the cycle is full level, the second half silent.
            // A note starting 0.7 s in lands in the silent half unless the LFO restarts.
            auto levelAfterLateNote = [] (bool retrigger)
            {
                SynthParams params;
                params.sustain = 1.0f;
                params.lfos[0] = { LfoShape::square, LfoDestination::amp, 1.0f, 1.0f };
                params.lfos[0].retrigger = retrigger;
                auto engine = makeEngine (params);

                renderNote (*engine, -1, 0.7);
                const auto output = renderNote (*engine, 60, 0.15);
                return peak (output, toSample (0.05), toSample (0.1));
            };

            expectGreaterThan (levelAfterLateNote (true), 0.1f);
            expectLessThan (levelAfterLateNote (false), 0.01f);
        }
    }
};

static LfoSyncTests lfoSyncTests;
