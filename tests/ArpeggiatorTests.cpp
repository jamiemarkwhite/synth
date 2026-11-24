#include "Arpeggiator.h"

namespace
{
constexpr double samplesPerBeat = 24000.0; // 120 BPM at 48 kHz, so a 1/16 tick is 6000 samples
constexpr int blockSize = 512;

struct ArpResult
{
    std::vector<int> notes;
    std::vector<int> noteOnTimes;
    std::vector<int> noteOffTimes;
    std::vector<int> passthroughChannels;
};

// Holds `held` from sample 0 and runs the arp for `numSamples`, collecting what comes out.
ArpResult runArp (const ArpParams& params, std::initializer_list<int> held, int numSamples, int releaseAt = -1)
{
    Arpeggiator arp;
    arp.setParams (params);
    ArpResult result;

    for (int start = 0; start < numSamples; start += blockSize)
    {
        juce::MidiBuffer input, output;
        if (start == 0)
            for (auto note : held)
                input.addEvent (juce::MidiMessage::noteOn (1, note, 0.9f), 0);

        const auto count = std::min (blockSize, numSamples - start);
        if (releaseAt >= start && releaseAt < start + count)
            for (auto note : held)
                input.addEvent (juce::MidiMessage::noteOff (1, note), releaseAt - start);

        arp.process (input, output, count, samplesPerBeat);

        for (const auto metadata : output)
        {
            const auto message = metadata.getMessage();
            if (message.getChannel() != Arpeggiator::outputChannel)
                result.passthroughChannels.push_back (message.getChannel());
            else if (message.isNoteOn())
            {
                result.notes.push_back (message.getNoteNumber());
                result.noteOnTimes.push_back (start + metadata.samplePosition);
            }
            else if (message.isNoteOff())
                result.noteOffTimes.push_back (start + metadata.samplePosition);
        }
    }

    return result;
}

ArpParams arpWith (ArpMode mode, int octaves = 1)
{
    ArpParams params;
    params.enabled = true;
    params.mode = mode;
    params.octaves = octaves;
    params.division = 3; // 1/16
    params.gate = 0.5f;
    return params;
}
} // namespace

class ArpeggiatorTests final : public juce::UnitTest
{
public:
    ArpeggiatorTests() : juce::UnitTest ("Arpeggiator") {}

    void runTest() override
    {
        beginTest ("Up plays the chord low to high, starting straight away");
        {
            const auto result = runArp (arpWith (ArpMode::up), { 67, 60, 64 }, 4 * 6000);
            expect (result.notes == std::vector<int> { 60, 64, 67, 60 });
            expect (result.noteOnTimes == std::vector<int> { 0, 6000, 12000, 18000 });
        }

        beginTest ("Gate sets how long each note is held");
        {
            const auto result = runArp (arpWith (ArpMode::up), { 60 }, 6000);
            expect (! result.noteOffTimes.empty());
            if (! result.noteOffTimes.empty())
                expectEquals (result.noteOffTimes.front(), 3000);
        }

        beginTest ("Down plays high to low");
        {
            const auto result = runArp (arpWith (ArpMode::down), { 60, 64, 67 }, 3 * 6000);
            expect (result.notes == std::vector<int> { 67, 64, 60 });
        }

        beginTest ("Up+Down bounces without repeating the ends");
        {
            const auto result = runArp (arpWith (ArpMode::upDown), { 60, 64, 67 }, 6 * 6000);
            expect (result.notes == std::vector<int> { 60, 64, 67, 64, 60, 64 });
        }

        beginTest ("Two octaves repeats the chord an octave up");
        {
            const auto result = runArp (arpWith (ArpMode::up, 2), { 60, 64, 67 }, 6 * 6000);
            expect (result.notes == std::vector<int> { 60, 64, 67, 72, 76, 79 });
        }

        beginTest ("Random only plays held notes");
        {
            const auto result = runArp (arpWith (ArpMode::random), { 60, 64, 67 }, 20 * 6000);
            expectEquals (static_cast<int> (result.notes.size()), 20);
            for (auto note : result.notes)
                expect (note == 60 || note == 64 || note == 67);
        }

        beginTest ("Releasing the keys stops the arp");
        {
            const auto result = runArp (arpWith (ArpMode::up), { 60, 64 }, 10 * 6000, 6000 + 100);
            expectEquals (static_cast<int> (result.notes.size()), 2);
        }

        beginTest ("Disabled arp passes notes straight through");
        {
            ArpParams params;
            const auto result = runArp (params, { 60, 64 }, 6000);
            expect (result.notes.empty());
            expectEquals (static_cast<int> (result.passthroughChannels.size()), 2);
        }
    }
};

static ArpeggiatorTests arpeggiatorTests;
