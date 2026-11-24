#include "DrumMachine.h"

namespace
{
constexpr double sampleRate = 48000.0;

juce::AudioBuffer<float> renderDrum (DrumMachine& drums, int track, double seconds)
{
    juce::AudioBuffer<float> buffer (1, static_cast<int> (seconds * sampleRate));
    buffer.clear();
    drums.trigger (track, 1.0f);
    drums.render (buffer, 0, buffer.getNumSamples());
    return buffer;
}

int risingZeroCrossings (const juce::AudioBuffer<float>& buffer, int start, int end)
{
    const auto* data = buffer.getReadPointer (0);
    int crossings = 0;
    for (int i = start + 1; i < end; ++i)
        if (data[i - 1] < 0.0f && data[i] >= 0.0f)
            ++crossings;
    return crossings;
}
} // namespace

class DrumMachineTests final : public juce::UnitTest
{
public:
    DrumMachineTests() : juce::UnitTest ("DrumMachine") {}

    void runTest() override
    {
        beginTest ("Every drum makes a sound and then stops");
        for (int track = 0; track < DrumMachine::numTracks; ++track)
        {
            DrumMachine drums;
            drums.prepare (sampleRate);
            const auto output = renderDrum (drums, track, 8.0);
            const auto level = output.getMagnitude (0, 0, static_cast<int> (0.1 * sampleRate));

            expectGreaterThan (level, 0.05f, DrumMachine::trackName (track) + " is too quiet");
            expectLessOrEqual (level, 1.5f, DrumMachine::trackName (track) + " is too loud");
            expect (! drums.isTrackSounding (track), DrumMachine::trackName (track) + " never stops");
        }

        beginTest ("Closed hat cuts off the open hat");
        {
            DrumMachine drums;
            drums.prepare (sampleRate);
            juce::AudioBuffer<float> buffer (1, static_cast<int> (0.2 * sampleRate));
            buffer.clear();

            drums.trigger (DrumMachine::openHat, 1.0f);
            drums.render (buffer, 0, 2400);
            drums.trigger (DrumMachine::closedHat, 1.0f);
            drums.render (buffer, 2400, buffer.getNumSamples() - 2400);

            expect (! drums.isTrackSounding (DrumMachine::openHat));
        }

        beginTest ("Tuning a tom up raises its pitch");
        {
            auto crossingsAtTune = [] (float semitones)
            {
                DrumMachine drums;
                drums.prepare (sampleRate);
                DrumParams params;
                params.tracks[DrumMachine::lowTom].tuneSemitones = semitones;
                drums.setParams (params);
                const auto output = renderDrum (drums, DrumMachine::lowTom, 0.5);
                return risingZeroCrossings (output, 4800, 19200);
            };

            expectGreaterThan (crossingsAtTune (12.0f), crossingsAtTune (0.0f) + 10);
        }

        beginTest ("General MIDI drum notes map to tracks and back");
        for (int track = 0; track < DrumMachine::numTracks; ++track)
            expectEquals (DrumMachine::trackForMidiNote (DrumMachine::midiNoteForTrack (track)), track);
        expectEquals (DrumMachine::trackForMidiNote (60), -1);
    }
};

static DrumMachineTests drumMachineTests;
