#pragma once

#include <juce_core/juce_core.h>

enum class LfoShape
{
    sine = 0,
    triangle,
    saw,
    square,
    sampleAndHold
};

enum class LfoDestination
{
    off = 0,
    pitch,
    cutoff,
    amp
};

// Free-running low-frequency oscillator. Outputs values in -1..1.
class Lfo
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        reset();
    }

    void reset()
    {
        phase = 0.0;
        random.setSeed (1);
        held = nextRandom();
    }

    // Restarts the cycle from the beginning, e.g. when a new note starts.
    void restart()
    {
        phase = 0.0;
        held = nextRandom();
    }

    void setShape (LfoShape newShape) { shape = newShape; }
    void setRate (float hz) { rate = hz; }

    float getValue() const
    {
        const auto p = static_cast<float> (phase);

        switch (shape)
        {
            case LfoShape::sine:          return std::sin (juce::MathConstants<float>::twoPi * p);
            case LfoShape::triangle:      return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
            case LfoShape::saw:           return 2.0f * p - 1.0f;
            case LfoShape::square:        return p < 0.5f ? 1.0f : -1.0f;
            case LfoShape::sampleAndHold: return held;
        }

        return 0.0f;
    }

    void advance (int numSamples)
    {
        phase += static_cast<double> (rate) * numSamples / sampleRate;

        while (phase >= 1.0)
        {
            phase -= 1.0;
            held = nextRandom();
        }
    }

private:
    float nextRandom() { return random.nextFloat() * 2.0f - 1.0f; }

    LfoShape shape = LfoShape::sine;
    double sampleRate = 44100.0;
    double phase = 0.0;
    float rate = 1.0f;
    float held = 0.0f;
    juce::Random random { 1 };
};
