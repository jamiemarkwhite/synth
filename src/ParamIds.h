#pragma once

#include <juce_core/juce_core.h>

namespace ParamIds
{
// Synth track parameters are named "synth<track>_<base>", e.g. "synth2_cutoff". The base names
// below (from waveform to arpGate, the LFO fields, octave, noteLength, mute and solo) are per track.
inline juce::String synth (int track, const juce::String& base)
{
    return "synth" + juce::String (track + 1) + "_" + base;
}

inline constexpr auto octave = "trackOctave";
inline constexpr auto noteLength = "noteLength";
inline constexpr auto mute = "mute";
inline constexpr auto solo = "solo";

inline constexpr auto waveform = "waveform";
inline constexpr auto attack = "attack";
inline constexpr auto decay = "decay";
inline constexpr auto sustain = "sustain";
inline constexpr auto release = "release";
inline constexpr auto cutoff = "cutoff";
inline constexpr auto resonance = "resonance";
inline constexpr auto gain = "gain";

inline constexpr auto arpOn = "arpOn";
inline constexpr auto arpMode = "arpMode";
inline constexpr auto arpRate = "arpRate";
inline constexpr auto arpOctaves = "arpOctaves";
inline constexpr auto arpGate = "arpGate";

inline constexpr auto bpm = "bpm";
inline constexpr auto swing = "swing";
inline constexpr auto steps = "steps";
inline constexpr auto scale = "scaleMode";
inline constexpr auto root = "root";
inline constexpr auto followHost = "followHost";
inline constexpr auto metronome = "metronome";
inline constexpr auto metronomeLevel = "metronomeLevel";

inline constexpr auto drumLevel = "drumLevel";
inline constexpr auto sampleLevel = "sampleLevel";
inline constexpr auto echo = "echo";
inline constexpr auto echoFeedback = "echoFeedback";
inline constexpr auto space = "space";
inline constexpr auto master = "master";

// Per-LFO: Shape, Destination, Rate, Depth, Sync, Division, Retrigger
inline juce::String lfo (int index, const char* name)
{
    return "lfo" + juce::String (index + 1) + name;
}

// Per-drum: Level, Tune, Decay, Mute, Solo
inline juce::String drum (int track, const char* name)
{
    return "drum" + juce::String (track + 1) + name;
}

// Per-sample slot: Level, Tune, Reverse, Mute, Solo
inline juce::String sample (int slot, const char* name)
{
    return "sample" + juce::String (slot + 1) + name;
}

inline constexpr std::array<const char*, 7> lfoFields { "Shape", "Destination", "Rate", "Depth", "Sync", "Division", "Retrigger" };

// Everything a sound preset sets on a track, as base names. Other parameters are left alone.
inline juce::StringArray soundParameters()
{
    juce::StringArray ids { waveform, attack, decay, sustain, release, cutoff, resonance, gain,
                            arpOn, arpMode, arpRate, arpOctaves, arpGate };

    for (int i = 0; i < 2; ++i)
        for (auto* field : lfoFields)
            ids.add (lfo (i, field));

    return ids;
}
} // namespace ParamIds
