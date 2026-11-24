#include "Sampler.h"

namespace
{
bool validSlot (int slot)
{
    return slot >= 0 && slot < Sampler::numSlots;
}
} // namespace

bool SampleTools::tidyRecording (juce::AudioBuffer<float>& mono, double sampleRate)
{
    const auto numSamples = mono.getNumSamples();
    if (numSamples == 0)
        return false;

    const auto peak = mono.getMagnitude (0, 0, numSamples);
    if (peak < 0.01f) // quieter than -40 dBFS: nothing but background noise
        return false;

    const auto threshold = std::max (0.004f, peak * 0.08f);
    const auto* data = mono.getReadPointer (0);

    int first = 0;
    while (std::abs (data[first]) <= threshold)
        ++first;

    int last = numSamples - 1;
    while (std::abs (data[last]) <= threshold)
        --last;

    // A little before the sound starts, and a tail so words and claps can ring out.
    const auto start = std::max (0, first - static_cast<int> (0.01 * sampleRate));
    const auto end = std::min (numSamples, last + 1 + static_cast<int> (0.15 * sampleRate));
    const auto length = end - start;

    juce::AudioBuffer<float> tidied (1, length);
    tidied.copyFrom (0, 0, mono, 0, start, length);
    tidied.applyGain (std::min (0.9f / peak, 30.0f));

    const auto fadeIn = std::min (length / 4, static_cast<int> (0.003 * sampleRate));
    const auto fadeOut = std::min (length / 4, static_cast<int> (0.02 * sampleRate));
    tidied.applyGainRamp (0, 0, fadeIn, 0.0f, 1.0f);
    tidied.applyGainRamp (0, length - fadeOut, fadeOut, 1.0f, 0.0f);

    mono = std::move (tidied);
    return true;
}

void Sampler::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    recordBuffer.setSize (1, static_cast<int> (maxRecordSeconds * sampleRate));
    recordedSamples.store (0);
    recordSlot.store (-1);
    recordState.store (idle);
    reset();
}

void Sampler::reset()
{
    for (auto& voice : voices)
        voice = {};
}

void Sampler::trigger (int slot, float velocity)
{
    if (! validSlot (slot))
        return;

    const juce::SpinLock::ScopedTryLockType lock (swapLock);
    if (! lock.isLocked())
        return;

    const auto* sample = samples[static_cast<size_t> (slot)].get();
    if (sample == nullptr || sample->data.getNumSamples() == 0)
        return;

    int free = -1;
    for (int i = 0; i < voicesPerSlot; ++i)
    {
        auto& voice = voiceFor (slot, i);
        if (! voice.active)
            free = free < 0 ? i : free;
        else if (voice.fadeRemaining < 0)
            voice.fadeRemaining = fadeSamples;
    }

    // Both voices busy (very fast retriggers): take over the one fading out.
    auto& voice = voiceFor (slot, std::max (free, 0));
    const auto& slotParams = params.slots[static_cast<size_t> (slot)];

    voice.active = true;
    voice.reverse = slotParams.reverse;
    voice.increment = sample->sourceRate / sampleRate * std::pow (2.0, slotParams.tuneSemitones / 12.0);
    voice.position = voice.reverse ? sample->data.getNumSamples() - 1 : 0.0;
    voice.gain = velocity * juce::Decibels::decibelsToGain (slotParams.levelDb);
    voice.fadeRemaining = -1;
}

void Sampler::renderVoice (Voice& voice, const Sample& sample, float* left, float* right, int numSamples)
{
    const auto* data = sample.data.getReadPointer (0);
    const auto last = sample.data.getNumSamples() - 1;

    for (int i = 0; i < numSamples; ++i)
    {
        if (voice.position < 0.0 || voice.position > last || voice.fadeRemaining == 0)
        {
            voice.active = false;
            return;
        }

        const auto index = static_cast<int> (voice.position);
        const auto fraction = static_cast<float> (voice.position - index);
        const auto next = std::min (index + 1, last);
        const auto value = data[index] + fraction * (data[next] - data[index]);

        auto gain = voice.gain;
        if (voice.fadeRemaining > 0)
            gain *= static_cast<float> (voice.fadeRemaining--) / fadeSamples;

        left[i] += value * gain;
        if (right != left)
            right[i] += value * gain;

        voice.position += voice.reverse ? -voice.increment : voice.increment;
    }
}

void Sampler::render (juce::AudioBuffer<float>& output, int startSample, int numSamples)
{
    if (numSamples <= 0)
        return;

    const juce::SpinLock::ScopedTryLockType lock (swapLock);
    if (! lock.isLocked())
        return;

    auto* left = output.getWritePointer (0, startSample);
    auto* right = output.getNumChannels() > 1 ? output.getWritePointer (1, startSample) : left;

    for (int slot = 0; slot < numSlots; ++slot)
    {
        const auto* sample = samples[static_cast<size_t> (slot)].get();
        if (sample == nullptr)
            continue;

        for (int i = 0; i < voicesPerSlot; ++i)
            if (auto& voice = voiceFor (slot, i); voice.active)
                renderVoice (voice, *sample, left, right, numSamples);
    }
}

bool Sampler::isSlotSounding (int slot) const
{
    if (! validSlot (slot))
        return false;

    for (int i = 0; i < voicesPerSlot; ++i)
        if (voices[static_cast<size_t> (slot * voicesPerSlot + i)].active)
            return true;

    return false;
}

void Sampler::captureInput (const juce::AudioBuffer<float>& input, int numInputChannels, int numSamples)
{
    numInputChannels = std::min (numInputChannels, input.getNumChannels());
    const auto state = recordState.load();

    if (state == stopRequested)
    {
        recordState.store (finished);
        return;
    }

    const auto written = recordedSamples.load();
    const auto capacity = recordBuffer.getNumSamples();
    const auto toWrite = state == recording ? std::min (numSamples, capacity - written) : 0;
    auto* destination = toWrite > 0 ? recordBuffer.getWritePointer (0, written) : nullptr;

    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        float mono = 0.0f;
        for (int channel = 0; channel < numInputChannels; ++channel)
            mono += input.getSample (channel, i);
        if (numInputChannels > 1)
            mono /= static_cast<float> (numInputChannels);

        peak = std::max (peak, std::abs (mono));
        if (i < toWrite)
            destination[i] = mono;
    }

    auto previous = inputPeak.load();
    while (peak > previous && ! inputPeak.compare_exchange_weak (previous, peak)) {}

    if (state != recording)
        return;

    recordedSamples.store (written + toWrite);
    if (written + toWrite >= capacity)
        recordState.store (finished);
}

void Sampler::setSample (int slot, juce::AudioBuffer<float> mono, double sourceSampleRate)
{
    if (! validSlot (slot))
        return;

    std::unique_ptr<Sample> sample;
    if (mono.getNumSamples() > 0)
    {
        sample = std::make_unique<Sample>();
        sample->data = std::move (mono);
        sample->sourceRate = sourceSampleRate;
    }

    const juce::ScopedLock writer (writerLock);
    {
        const juce::SpinLock::ScopedLockType lock (swapLock);
        std::swap (samples[static_cast<size_t> (slot)], sample);
        for (int i = 0; i < voicesPerSlot; ++i)
            voiceFor (slot, i) = {};
    }

    version.fetch_add (1);
    // The old sample is freed here, outside the spin lock.
}

void Sampler::clearSample (int slot)
{
    setSample (slot, {}, sampleRate);
}

bool Sampler::hasSample (int slot) const
{
    return validSlot (slot) && samples[static_cast<size_t> (slot)] != nullptr;
}

const juce::AudioBuffer<float>* Sampler::getSampleForDisplay (int slot) const
{
    if (! hasSample (slot))
        return nullptr;

    return &samples[static_cast<size_t> (slot)]->data;
}

double Sampler::getSampleSeconds (int slot) const
{
    if (! hasSample (slot))
        return 0.0;

    const auto& sample = *samples[static_cast<size_t> (slot)];
    return sample.data.getNumSamples() / sample.sourceRate;
}

void Sampler::startRecording (int slot)
{
    if (! validSlot (slot) || recordBuffer.getNumSamples() == 0 || recordState.load() != idle)
        return;

    recordedSamples.store (0);
    recordSlot.store (slot);
    recordState.store (recording);
}

void Sampler::stopRecording()
{
    auto expected = static_cast<int> (recording);
    recordState.compare_exchange_strong (expected, stopRequested);
}

void Sampler::cancelRecording()
{
    if (recordState.load() == idle)
        return;

    discardRecording.store (true);
    stopRecording();
}

int Sampler::getRecordingSlot() const
{
    return recordState.load() == idle ? -1 : recordSlot.load();
}

float Sampler::getRecordingProgress() const
{
    const auto capacity = recordBuffer.getNumSamples();
    return capacity > 0 ? static_cast<float> (recordedSamples.load()) / static_cast<float> (capacity) : 0.0f;
}

Sampler::RecordResult Sampler::collectRecording()
{
    if (recordState.load() != finished)
        return RecordResult::none;

    if (discardRecording.exchange (false))
    {
        recordState.store (idle);
        return RecordResult::none;
    }

    const auto length = recordedSamples.load();
    const auto slot = recordSlot.load();

    juce::AudioBuffer<float> recorded (1, length);
    if (length > 0)
        recorded.copyFrom (0, 0, recordBuffer, 0, 0, length);

    recordSlot.store (-1);
    recordState.store (idle);

    if (length == 0 || ! SampleTools::tidyRecording (recorded, sampleRate))
        return RecordResult::tooQuiet;

    setSample (slot, std::move (recorded), sampleRate);
    return RecordResult::recorded;
}

juce::ValueTree Sampler::toValueTree() const
{
    juce::ValueTree tree (treeType);
    const juce::ScopedLock writer (writerLock);

    for (int slot = 0; slot < numSlots; ++slot)
    {
        const auto* sample = samples[static_cast<size_t> (slot)].get();
        if (sample == nullptr)
            continue;

        // 16-bit is plenty for a mic recording and halves the size of the saved project.
        const auto length = sample->data.getNumSamples();
        juce::MemoryBlock block (static_cast<size_t> (length) * sizeof (juce::int16));
        auto* pcm = static_cast<juce::int16*> (block.getData());
        const auto* data = sample->data.getReadPointer (0);
        for (int i = 0; i < length; ++i)
            pcm[i] = static_cast<juce::int16> (std::lround (juce::jlimit (-1.0f, 1.0f, data[i]) * 32767.0f));

        juce::ValueTree child ("Sample");
        child.setProperty ("slot", slot, nullptr);
        child.setProperty ("rate", sample->sourceRate, nullptr);
        child.setProperty ("pcm16", block.toBase64Encoding(), nullptr);
        tree.appendChild (child, nullptr);
    }

    return tree;
}

void Sampler::fromValueTree (const juce::ValueTree& tree)
{
    if (! tree.hasType (treeType))
        return;

    for (int slot = 0; slot < numSlots; ++slot)
        clearSample (slot);

    for (const auto& child : tree)
    {
        const int slot = child.getProperty ("slot", -1);
        const double rate = child.getProperty ("rate", 0.0);
        juce::MemoryBlock block;
        if (! validSlot (slot) || rate <= 0.0 || ! block.fromBase64Encoding (child.getProperty ("pcm16").toString()))
            continue;

        const auto length = static_cast<int> (block.getSize() / sizeof (juce::int16));
        const auto* pcm = static_cast<const juce::int16*> (block.getData());
        juce::AudioBuffer<float> mono (1, length);
        for (int i = 0; i < length; ++i)
            mono.setSample (0, i, pcm[i] / 32767.0f);

        setSample (slot, std::move (mono), rate);
    }
}
