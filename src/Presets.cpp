#include "Presets.h"
#include "ParamIds.h"

using namespace ParamIds;

const std::vector<SoundPreset>& soundPresets()
{
    // Choice indices: waveform 0 sine / 1 saw / 2 square; LFO shape 0 sine / 1 triangle / 2 saw /
    // 3 square / 4 S&H; LFO destination 0 off / 1 pitch / 2 cutoff / 3 amp.
    static const std::vector<SoundPreset> presets {
        { "Init", {} },
        { "Fat Bass",
          { { waveform, 1 }, { attack, 0.005f }, { decay, 0.25f }, { sustain, 0.6f }, { release, 0.12f },
            { cutoff, 700 }, { resonance, 2.5f }, { gain, -4 } } },
        { "Pluck",
          { { waveform, 2 }, { attack, 0.002f }, { decay, 0.22f }, { sustain, 0 }, { release, 0.25f },
            { cutoff, 2800 }, { resonance, 1.5f } } },
        { "Dreamy Pad",
          { { waveform, 1 }, { attack, 0.9f }, { decay, 1.2f }, { sustain, 0.8f }, { release, 1.8f },
            { cutoff, 1800 }, { resonance, 0.8f },
            { lfo (0, "Destination"), 1 }, { lfo (0, "Rate"), 5 }, { lfo (0, "Depth"), 0.02f },
            { lfo (1, "Destination"), 2 }, { lfo (1, "Shape"), 1 }, { lfo (1, "Rate"), 0.15f }, { lfo (1, "Depth"), 0.25f } } },
        { "Wobble Bass",
          { { waveform, 1 }, { attack, 0.005f }, { decay, 0.3f }, { sustain, 0.9f }, { release, 0.1f },
            { cutoff, 350 }, { resonance, 4 },
            { lfo (1, "Destination"), 2 }, { lfo (1, "Sync"), 1 }, { lfo (1, "Division"), 7 },
            { lfo (1, "Depth"), 0.55f }, { lfo (1, "Retrigger"), 1 } } },
        { "Chip Lead",
          { { waveform, 2 }, { attack, 0.001f }, { decay, 0.1f }, { sustain, 0.7f }, { release, 0.08f },
            { cutoff, 9000 }, { resonance, 0.7f },
            { lfo (0, "Destination"), 1 }, { lfo (0, "Rate"), 6 }, { lfo (0, "Depth"), 0.03f } } },
        { "Arp Sparkle",
          { { waveform, 2 }, { attack, 0.001f }, { decay, 0.18f }, { sustain, 0.1f }, { release, 0.3f },
            { cutoff, 4500 }, { resonance, 1.2f },
            { arpOn, 1 }, { arpMode, 2 }, { arpRate, 3 }, { arpOctaves, 1 }, { arpGate, 0.5f } } },
        { "Laser",
          { { waveform, 1 }, { attack, 0.001f }, { decay, 0.15f }, { sustain, 0 }, { release, 0.1f },
            { cutoff, 6000 }, { resonance, 3 },
            { lfo (0, "Shape"), 2 }, { lfo (0, "Destination"), 1 }, { lfo (0, "Sync"), 1 },
            { lfo (0, "Division"), 10 }, { lfo (0, "Depth"), 0.35f }, { lfo (0, "Retrigger"), 1 } } },
        { "Strings",
          { { waveform, 1 }, { attack, 0.35f }, { decay, 1.0f }, { sustain, 0.85f }, { release, 1.2f },
            { cutoff, 2400 }, { resonance, 0.7f }, { gain, -12 },
            { lfo (0, "Destination"), 1 }, { lfo (0, "Rate"), 5.5f }, { lfo (0, "Depth"), 0.015f },
            { lfo (1, "Destination"), 2 }, { lfo (1, "Shape"), 1 }, { lfo (1, "Rate"), 0.2f }, { lfo (1, "Depth"), 0.15f } } },
        { "Sub Bass",
          { { waveform, 0 }, { attack, 0.005f }, { decay, 0.4f }, { sustain, 0.8f }, { release, 0.15f },
            { cutoff, 20000 }, { gain, -2 } } },
        { "Tremolo Keys",
          { { waveform, 0 }, { attack, 0.01f }, { decay, 0.6f }, { sustain, 0.5f }, { release, 0.5f },
            { cutoff, 5000 },
            { lfo (0, "Destination"), 3 }, { lfo (0, "Sync"), 1 }, { lfo (0, "Division"), 7 }, { lfo (0, "Depth"), 0.6f },
            { gain, -9 } } },
    };

    return presets;
}

const SoundPreset* findSoundPreset (const juce::String& name)
{
    for (const auto& preset : soundPresets())
        if (name == preset.name)
            return &preset;
    return nullptr;
}
