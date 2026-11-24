#pragma once

#include <juce_core/juce_core.h>

// Note lengths used for tempo-synced LFOs and the arpeggiator. Lengths are in beats (quarter notes).
namespace Tempo
{
struct Division
{
    const char* name;
    double beats;
};

inline constexpr std::array<Division, 12> lfoDivisions { {
    { "4 bars", 16.0 },
    { "2 bars", 8.0 },
    { "1 bar", 4.0 },
    { "1/2", 2.0 },
    { "1/4", 1.0 },
    { "1/4 dot", 1.5 },
    { "1/4 trip", 2.0 / 3.0 },
    { "1/8", 0.5 },
    { "1/8 dot", 0.75 },
    { "1/8 trip", 1.0 / 3.0 },
    { "1/16", 0.25 },
    { "1/16 trip", 1.0 / 6.0 },
} };

inline constexpr int defaultLfoDivision = 4; // 1/4

inline constexpr std::array<Division, 6> arpDivisions { {
    { "1/4", 1.0 },
    { "1/8", 0.5 },
    { "1/8 trip", 1.0 / 3.0 },
    { "1/16", 0.25 },
    { "1/16 trip", 1.0 / 6.0 },
    { "1/32", 0.125 },
} };

inline constexpr int defaultArpDivision = 3; // 1/16

template <size_t N>
juce::StringArray names (const std::array<Division, N>& divisions)
{
    juce::StringArray result;
    for (const auto& division : divisions)
        result.add (division.name);
    return result;
}
} // namespace Tempo
