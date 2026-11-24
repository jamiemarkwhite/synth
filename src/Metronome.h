#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// Short sine blip on each beat, higher on the first beat of the bar.
class Metronome
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        reset();
    }

    void reset() { remaining = 0; }

    void trigger (bool accent)
    {
        phase = 0.0;
        phaseStep = juce::MathConstants<double>::twoPi * (accent ? 1760.0 : 1320.0) / sampleRate;
        amplitude = accent ? 1.0f : 0.7f;
        remaining = static_cast<int> (0.04 * sampleRate);
        decay = static_cast<float> (std::exp (-1.0 / (0.008 * sampleRate)));
    }

    // Adds into both channels.
    void render (juce::AudioBuffer<float>& output, int startSample, int numSamples, float gain)
    {
        const auto count = std::min (numSamples, remaining);
        for (int i = 0; i < count; ++i)
        {
            const auto value = gain * amplitude * static_cast<float> (std::sin (phase));
            for (int channel = 0; channel < output.getNumChannels(); ++channel)
                output.addSample (channel, startSample + i, value);

            phase += phaseStep;
            amplitude *= decay;
        }
        remaining -= count;
    }

    bool isSounding() const { return remaining > 0; }

private:
    double sampleRate = 44100.0;
    double phase = 0.0, phaseStep = 0.0;
    float amplitude = 0.0f, decay = 0.0f;
    int remaining = 0;
};
