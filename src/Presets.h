#pragma once

#include <juce_core/juce_core.h>

// Ready-made synth sounds. Values are in each parameter's real units; choices use their index.
struct SoundPreset
{
    const char* name;
    std::vector<std::pair<juce::String, float>> values;
};

const std::vector<SoundPreset>& soundPresets();
const SoundPreset* findSoundPreset (const juce::String& name);
