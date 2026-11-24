#pragma once

#include <juce_core/juce_core.h>

// Finds the sequencer steps that start inside one audio block.
// Positions are measured in steps (16th notes). Swing (0..1) pushes every odd step
// late by up to half a step.
struct StepClock
{
    template <typename Callback>
    static void forEachStep (double blockStart, double stepsPerSample, int numSamples, float swing, Callback&& callback)
    {
        const auto blockEnd = blockStart + stepsPerSample * numSamples;
        const auto swingDelay = 0.5 * static_cast<double> (juce::jlimit (0.0f, 1.0f, swing));

        const auto first = std::max<juce::int64> (0, static_cast<juce::int64> (std::floor (blockStart)) - 1);
        const auto last = static_cast<juce::int64> (std::floor (blockEnd));

        for (auto step = first; step <= last; ++step)
        {
            const auto start = static_cast<double> (step) + ((step & 1) != 0 ? swingDelay : 0.0);

            if (start >= blockStart && start < blockEnd)
            {
                const auto offset = static_cast<int> ((start - blockStart) / stepsPerSample);
                callback (step, juce::jlimit (0, numSamples - 1, offset));
            }
        }
    }
};
