#include "Groovebox.h"

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 512;

juce::AudioBuffer<float> tone (int numSamples, float amplitude, int numChannels = 1, double frequency = 440.0)
{
    juce::AudioBuffer<float> buffer (numChannels, numSamples);
    for (int i = 0; i < numSamples; ++i)
        for (int channel = 0; channel < numChannels; ++channel)
            buffer.setSample (channel, i, amplitude * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * frequency * i / rate)));
    return buffer;
}

juce::AudioBuffer<float> ramp (int numSamples)
{
    juce::AudioBuffer<float> buffer (1, numSamples);
    for (int i = 0; i < numSamples; ++i)
        buffer.setSample (0, i, 0.5f * static_cast<float> (i + 1) / static_cast<float> (numSamples));
    return buffer;
}

// Feeds `input` to the sampler in blocks, as the audio thread would.
void feed (Sampler& sampler, const juce::AudioBuffer<float>& input)
{
    for (int start = 0; start < input.getNumSamples(); start += block)
    {
        const auto count = std::min (block, input.getNumSamples() - start);
        juce::AudioBuffer<float> chunk (input.getNumChannels(), count);
        for (int channel = 0; channel < input.getNumChannels(); ++channel)
            chunk.copyFrom (channel, 0, input, channel, start, count);
        sampler.captureInput (chunk, input.getNumChannels(), count);
    }
}

juce::AudioBuffer<float> renderSampler (Sampler& sampler, int numSamples)
{
    juce::AudioBuffer<float> output (2, numSamples);
    output.clear();
    for (int start = 0; start < numSamples; start += block)
        sampler.render (output, start, std::min (block, numSamples - start));
    return output;
}

int firstAbove (const juce::AudioBuffer<float>& buffer, float threshold, int from = 0)
{
    for (int i = from; i < buffer.getNumSamples(); ++i)
        if (std::abs (buffer.getSample (0, i)) > threshold)
            return i;
    return -1;
}
} // namespace

class SamplerTests final : public juce::UnitTest
{
public:
    SamplerTests() : juce::UnitTest ("Sampler") {}

    void runTest() override
    {
        beginTest ("Tidying a recording trims silence and normalises");
        {
            juce::AudioBuffer<float> recording (1, 60000);
            recording.clear();
            const auto burst = tone (9600, 0.1f);
            recording.copyFrom (0, 24000, burst, 0, 0, burst.getNumSamples());

            expect (SampleTools::tidyRecording (recording, rate));
            const auto expectedLength = static_cast<int> (0.01 * rate) + 9600 + static_cast<int> (0.15 * rate);
            expect (std::abs (recording.getNumSamples() - expectedLength) < 100, juce::String (recording.getNumSamples()));
            expectWithinAbsoluteError (recording.getMagnitude (0, 0, recording.getNumSamples()), 0.9f, 0.01f);
            expectWithinAbsoluteError (recording.getSample (0, 0), 0.0f, 0.001f);
        }

        beginTest ("Tidying refuses a recording that is only background noise");
        {
            auto quiet = tone (24000, 0.005f);
            expect (! SampleTools::tidyRecording (quiet, rate));
            expectEquals (quiet.getNumSamples(), 24000);
        }

        beginTest ("Recording from the input installs a sample");
        {
            Sampler sampler;
            sampler.prepare (rate);
            sampler.startRecording (2);
            expectEquals (sampler.getRecordingSlot(), 2);

            feed (sampler, tone (24000, 0.5f, 2));
            expect (sampler.getRecordingProgress() > 0.09f);
            expect (sampler.collectRecording() == Sampler::RecordResult::none); // still recording

            sampler.stopRecording();
            feed (sampler, tone (block, 0.5f, 2)); // the audio thread acknowledges the stop
            expect (sampler.collectRecording() == Sampler::RecordResult::recorded);
            expect (sampler.hasSample (2));
            expect (! sampler.hasSample (0));
            expectEquals (sampler.getRecordingSlot(), -1);
            expect (sampler.takeInputPeak() > 0.45f);
            expectEquals (sampler.takeInputPeak(), 0.0f);
        }

        beginTest ("A silent recording is reported as too quiet");
        {
            Sampler sampler;
            sampler.prepare (rate);
            sampler.startRecording (0);
            juce::AudioBuffer<float> silence (1, 24000);
            silence.clear();
            feed (sampler, silence);
            sampler.stopRecording();
            feed (sampler, silence);
            expect (sampler.collectRecording() == Sampler::RecordResult::tooQuiet);
            expect (! sampler.hasSample (0));
        }

        beginTest ("Recording stops by itself at the maximum length");
        {
            Sampler sampler;
            sampler.prepare (rate);
            sampler.startRecording (1);
            feed (sampler, tone (static_cast<int> ((Sampler::maxRecordSeconds + 1.0) * rate), 0.5f));
            expect (sampler.collectRecording() == Sampler::RecordResult::recorded);
            const auto* sample = sampler.getSampleForDisplay (1);
            expect (sample != nullptr && sample->getNumSamples() <= static_cast<int> (Sampler::maxRecordSeconds * rate));
        }

        beginTest ("A sample plays back as recorded");
        {
            Sampler sampler;
            sampler.prepare (rate);
            sampler.setSample (0, ramp (1000), rate);
            sampler.trigger (0, 1.0f);
            const auto output = renderSampler (sampler, 2000);

            expectWithinAbsoluteError (output.getSample (0, 0), 0.5f / 1000.0f, 1.0e-5f);
            expectWithinAbsoluteError (output.getSample (0, 499), 0.25f, 1.0e-5f);
            expectWithinAbsoluteError (output.getSample (1, 499), 0.25f, 1.0e-5f);
            expectEquals (output.getSample (0, 1200), 0.0f);
            expect (! sampler.isSlotSounding (0));
        }

        beginTest ("Tune and reverse");
        {
            SamplerParams params;
            params.slots[0].tuneSemitones = 12.0f;
            params.slots[1].reverse = true;

            Sampler sampler;
            sampler.prepare (rate);
            sampler.setParams (params);
            sampler.setSample (0, ramp (1000), rate);
            sampler.setSample (1, ramp (1000), rate);

            sampler.trigger (0, 1.0f);
            auto output = renderSampler (sampler, 1000);
            expect (std::abs (output.getSample (0, 499)) > 0.4f, "an octave up plays twice as fast");
            expectEquals (output.getSample (0, 520), 0.0f);

            sampler.trigger (1, 1.0f);
            output = renderSampler (sampler, 1000);
            expectWithinAbsoluteError (output.getSample (0, 0), 0.5f, 1.0e-5f);
            expect (output.getSample (0, 900) < 0.06f);
        }

        beginTest ("Samples recorded at another rate keep their pitch");
        {
            Sampler sampler;
            sampler.prepare (rate);
            sampler.setSample (0, ramp (1000), rate / 2.0); // 1000 samples at 24 kHz last 2000 samples at 48 kHz
            sampler.trigger (0, 1.0f);
            const auto output = renderSampler (sampler, 3000);
            expect (std::abs (output.getSample (0, 1990)) > 0.4f);
            expectEquals (output.getSample (0, 2010), 0.0f);
        }

        beginTest ("Retriggering fades the previous hit out");
        {
            Sampler sampler;
            sampler.prepare (rate);
            juce::AudioBuffer<float> flat (1, 48000);
            for (int i = 0; i < flat.getNumSamples(); ++i)
                flat.setSample (0, i, 0.5f);
            sampler.setSample (0, flat, rate);

            sampler.trigger (0, 1.0f);
            renderSampler (sampler, 1000);
            sampler.trigger (0, 1.0f);
            const auto output = renderSampler (sampler, 1000);
            // Old voice fading from 0.5 plus new voice at 0.5: never jumps above their sum.
            expect (output.getMagnitude (0, 0, 1000) <= 1.0f + 1.0e-5f);
            expectWithinAbsoluteError (output.getSample (0, 500), 0.5f, 1.0e-5f);
        }

        beginTest ("Samples survive saving and loading");
        {
            Sampler original;
            original.prepare (rate);
            original.setSample (3, ramp (777), 44100.0);

            Sampler restored;
            restored.prepare (rate);
            restored.setSample (0, ramp (10), rate);
            restored.fromValueTree (original.toValueTree());

            expect (! restored.hasSample (0));
            const auto* sample = restored.getSampleForDisplay (3);
            expect (sample != nullptr);
            if (sample != nullptr)
            {
                expectEquals (sample->getNumSamples(), 777);
                expectWithinAbsoluteError (sample->getSample (0, 776), 0.5f, 1.0f / 32767.0f);
            }
        }
    }
};

class GrooveboxSamplerTests final : public juce::UnitTest
{
public:
    GrooveboxSamplerTests() : juce::UnitTest ("Groovebox samples and metronome") {}

    void runTest() override
    {
        constexpr int samplesPerStep = 6000; // 120 BPM

        beginTest ("Sample steps play on their step");
        {
            Rig rig;
            rig.groovebox.getSampler().setSample (0, ramp (1000), rate);
            rig.bank.setSampleStep (0, 0, 2, true);
            rig.groovebox.setPlaying (true);
            const auto output = rig.render (samplesPerStep * 4);
            expectEquals (firstAbove (output, 0.0f), 2 * samplesPerStep);
        }

        beginTest ("Muted sample slots stay quiet");
        {
            auto params = Rig::dry();
            params.samples.slots[0].muted = true;
            Rig rig (params);
            rig.groovebox.getSampler().setSample (0, ramp (1000), rate);
            rig.bank.setSampleStep (0, 0, 0, true);
            rig.groovebox.setPlaying (true);
            const auto output = rig.render (samplesPerStep * 2);
            expectEquals (output.getMagnitude (0, 0, output.getNumSamples()), 0.0f);
        }

        beginTest ("Channel 10 notes 60 to 63 play the sample slots");
        {
            Rig rig;
            rig.groovebox.getSampler().setSample (1, ramp (1000), rate);
            const auto output = rig.render (2048, [] (int start, juce::MidiBuffer& midi)
            {
                if (start == 1024)
                    midi.addEvent (juce::MidiMessage::noteOn (Groovebox::drumChannel, Groovebox::firstSampleNote + 1, 1.0f), 100);
            });
            expectEquals (firstAbove (output, 0.0f), 1124);
        }

        beginTest ("Tapping a sample pad while recording writes the step");
        {
            Rig rig;
            rig.groovebox.getSampler().setSample (2, ramp (1000), rate);
            rig.groovebox.setPlaying (true);
            rig.groovebox.setRecording (true);
            rig.render (samplesPerStep * 3, [&rig] (int start, juce::MidiBuffer&)
            {
                if (start == 11776) // just before step 2
                    rig.groovebox.previewSample (2);
            });
            expect (rig.bank.getSampleStep (0, 2, 2));
            expect (! rig.bank.getDrumStep (0, 2, 2));
            expect ((rig.groovebox.takeDrumHits() & (1u << Groovebox::sampleTrack (2))) != 0);
        }

        beginTest ("The mic input is recorded through process()");
        {
            Rig rig;
            auto& sampler = rig.groovebox.getSampler();
            sampler.startRecording (0);

            const auto input = tone (24000, 0.5f, 2);
            for (int start = 0; start < input.getNumSamples(); start += block)
            {
                juce::AudioBuffer<float> buffer (2, block);
                for (int channel = 0; channel < 2; ++channel)
                    buffer.copyFrom (channel, 0, input, channel, start, block);
                rig.groovebox.process (buffer, {}, {}, 2);
                expect (buffer.getMagnitude (0, block) < 1.0e-6f, "the mic is not played through");
            }

            sampler.stopRecording();
            juce::AudioBuffer<float> buffer (2, block);
            buffer.clear();
            rig.groovebox.process (buffer, {}, {}, 2);
            expect (sampler.collectRecording() == Sampler::RecordResult::recorded);
        }

        beginTest ("The metronome clicks on every beat while playing");
        {
            auto params = Rig::dry();
            params.sequencer.metronome = true;
            Rig rig (params);
            rig.groovebox.setPlaying (true);
            const auto output = rig.render (samplesPerStep * 9);

            // The click is a sine starting at zero, so it crosses the threshold a sample or two in.
            auto near = [this] (int actual, int expected) { expect (actual >= expected && actual <= expected + 2, juce::String (actual)); };
            near (firstAbove (output, 0.01f), 0);
            expect (output.getMagnitude (0, 3000, samplesPerStep * 4 - 3000) < 1.0e-6f);
            near (firstAbove (output, 0.01f, 3000), samplesPerStep * 4);
            near (firstAbove (output, 0.01f, samplesPerStep * 4 + 3000), samplesPerStep * 8);
        }

        beginTest ("The metronome is silent when off or stopped");
        {
            Rig rig;
            rig.groovebox.setPlaying (true);
            auto output = rig.render (samplesPerStep * 8);
            expectEquals (output.getMagnitude (0, 0, output.getNumSamples()), 0.0f);

            auto params = Rig::dry();
            params.sequencer.metronome = true;
            Rig stopped (params);
            output = stopped.render (samplesPerStep * 8);
            expectEquals (output.getMagnitude (0, 0, output.getNumSamples()), 0.0f);
        }
    }

private:
    struct Rig
    {
        static GrooveParams dry()
        {
            GrooveParams params;
            params.sequencer.bpm = 120.0f;
            params.sequencer.followHost = false;
            params.mix.echo = 0.0f;
            params.mix.space = 0.0f;
            params.mix.masterDb = 0.0f;
            return params;
        }

        PatternBank bank;
        Groovebox groovebox { bank };

        explicit Rig (const GrooveParams& params = dry())
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
};

static SamplerTests samplerTests;
static GrooveboxSamplerTests grooveboxSamplerTests;
