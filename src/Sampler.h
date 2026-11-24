#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

struct SampleSlotParams
{
    float levelDb = 0.0f;
    float tuneSemitones = 0.0f;
    bool reverse = false;
    bool muted = false;
    bool soloed = false;
};

struct SamplerParams
{
    std::array<SampleSlotParams, 4> slots;
};

namespace SampleTools
{
// Cuts the quiet parts off both ends, fades the edges and normalises.
// Returns false and leaves the buffer alone if nothing was loud enough.
bool tidyRecording (juce::AudioBuffer<float>& mono, double sampleRate);
} // namespace SampleTools

// Four sample slots that can be recorded from the audio input (a mic) or loaded from files,
// and played as one-shots.
//
// Threads: trigger, render and captureInput run on the audio thread. Everything else runs on
// the message thread. New samples are swapped in under a spin lock that the audio thread only
// ever try-locks, so it never waits.
class Sampler
{
public:
    static constexpr int numSlots = 4;
    static constexpr double maxRecordSeconds = 5.0;
    static constexpr double maxFileSeconds = 10.0;

    enum class RecordResult
    {
        none,
        recorded,
        tooQuiet
    };

    void prepare (double sampleRate);
    void setParams (const SamplerParams& newParams) { params = newParams; }
    void reset();

    // Audio thread.
    void trigger (int slot, float velocity);
    void render (juce::AudioBuffer<float>& output, int startSample, int numSamples); // adds into output
    void captureInput (const juce::AudioBuffer<float>& input, int numInputChannels, int numSamples);
    bool isSlotSounding (int slot) const;

    // Message thread.
    void setSample (int slot, juce::AudioBuffer<float> mono, double sourceSampleRate);
    void clearSample (int slot);
    bool hasSample (int slot) const;
    const juce::AudioBuffer<float>* getSampleForDisplay (int slot) const; // valid until the slot next changes
    double getSampleSeconds (int slot) const;
    juce::uint32 getVersion() const { return version.load(); }

    void startRecording (int slot);
    void stopRecording();
    int getRecordingSlot() const; // -1 when not recording
    float getRecordingProgress() const;
    RecordResult collectRecording(); // call regularly; installs a finished recording
    float takeInputPeak() { return inputPeak.exchange (0.0f); }

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& tree);

    static inline const juce::Identifier treeType { "Samples" };

private:
    struct Sample
    {
        juce::AudioBuffer<float> data; // mono
        double sourceRate = 44100.0;
    };

    // Two voices per slot, so a retrigger can fade the old hit out instead of clicking.
    struct Voice
    {
        bool active = false;
        double position = 0.0;
        double increment = 1.0;
        float gain = 0.0f;
        bool reverse = false;
        int fadeRemaining = -1; // -1 while playing normally
    };

    enum RecordState
    {
        idle,
        recording,
        stopRequested,
        finished
    };

    static constexpr int voicesPerSlot = 2;
    static constexpr int fadeSamples = 128;

    Voice& voiceFor (int slot, int index) { return voices[static_cast<size_t> (slot * voicesPerSlot + index)]; }
    void renderVoice (Voice&, const Sample&, float* left, float* right, int numSamples);

    double sampleRate = 44100.0;
    SamplerParams params;

    std::array<std::unique_ptr<Sample>, numSlots> samples;
    std::array<Voice, numSlots * voicesPerSlot> voices {};
    juce::SpinLock swapLock;
    mutable juce::CriticalSection writerLock; // serialises the message thread with state saving on other threads
    std::atomic<juce::uint32> version { 0 };

    juce::AudioBuffer<float> recordBuffer;
    std::atomic<int> recordState { idle };
    std::atomic<int> recordSlot { -1 };
    std::atomic<int> recordedSamples { 0 };
    std::atomic<float> inputPeak { 0.0f };
};
