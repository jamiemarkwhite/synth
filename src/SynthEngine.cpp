#include "SynthEngine.h"

namespace
{
// PolyBLEP correction smooths the discontinuities in saw and square waves to reduce aliasing.
float polyBlep (float phase, float increment)
{
    if (phase < increment)
    {
        const auto t = phase / increment;
        return t + t - t * t - 1.0f;
    }

    if (phase > 1.0f - increment)
    {
        const auto t = (phase - 1.0f) / increment;
        return t * t + t + t + 1.0f;
    }

    return 0.0f;
}

bool chunkHasNoteOn (const juce::MidiBuffer& midi, int start, int numSamples)
{
    for (auto it = midi.findNextSamplePosition (start); it != midi.cend(); ++it)
    {
        const auto metadata = *it;
        if (metadata.samplePosition >= start + numSamples)
            break;
        if (metadata.getMessage().isNoteOn())
            return true;
    }

    return false;
}

struct SynthSound final : public juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class SynthVoice final : public juce::SynthesiserVoice
{
public:
    void setParams (const SynthParams& params)
    {
        waveform = params.waveform;
        adsr.setParameters ({ params.attack, params.decay, params.sustain, params.release });
    }

    void setPitchRatio (float ratio)
    {
        pitchRatio = ratio;
        increment = baseIncrement * pitchRatio;
    }

    bool canPlaySound (juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<SynthSound*> (sound) != nullptr;
    }

    void startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) override
    {
        adsr.setSampleRate (getSampleRate());
        baseIncrement = static_cast<float> (juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber) / getSampleRate());
        increment = baseIncrement * pitchRatio;
        level = velocity;
        phase = 0.0f;
        adsr.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            adsr.noteOff();
        }
        else
        {
            adsr.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override
    {
        if (! isVoiceActive())
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto sample = nextOscillatorSample() * level * adsr.getNextSample();

            for (int channel = 0; channel < output.getNumChannels(); ++channel)
                output.addSample (channel, startSample + i, sample);

            if (! adsr.isActive())
            {
                clearCurrentNote();
                break;
            }
        }
    }

private:
    float nextOscillatorSample()
    {
        float value = 0.0f;

        switch (waveform)
        {
            case Waveform::sine:
                value = std::sin (juce::MathConstants<float>::twoPi * phase);
                break;

            case Waveform::saw:
                value = 2.0f * phase - 1.0f - polyBlep (phase, increment);
                break;

            case Waveform::square:
            {
                const auto shifted = std::fmod (phase + 0.5f, 1.0f);
                value = (phase < 0.5f ? 1.0f : -1.0f) + polyBlep (phase, increment) - polyBlep (shifted, increment);
                break;
            }
        }

        phase += increment;
        if (phase >= 1.0f)
            phase -= 1.0f;

        return value;
    }

    juce::ADSR adsr;
    Waveform waveform = Waveform::saw;
    float phase = 0.0f;
    float increment = 0.0f;
    float baseIncrement = 0.0f;
    float pitchRatio = 1.0f;
    float level = 0.0f;
};
} // namespace

SynthEngine::SynthEngine()
{
    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new SynthVoice());

    synth.addSound (new SynthSound());
    filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
}

void SynthEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);

    const juce::dsp::ProcessSpec spec { sampleRate,
                                        static_cast<juce::uint32> (maxBlockSize),
                                        static_cast<juce::uint32> (numChannels) };
    filter.prepare (spec);
    gain.prepare (spec);
    gain.setRampDurationSeconds (0.02);

    for (auto& lfo : lfos)
        lfo.prepare (sampleRate);

    setParams (currentParams);
    reset();
}

void SynthEngine::setParams (const SynthParams& params)
{
    currentParams = params;

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
            voice->setParams (params);

    for (size_t i = 0; i < lfos.size(); ++i)
        lfos[i].setShape (params.lfos[i].shape);

    updateLfoRates();

    filter.setCutoffFrequency (params.cutoff);
    filter.setResonance (params.resonance);
    gain.setGainDecibels (params.gainDb);
}

void SynthEngine::setTempo (double bpm)
{
    if (bpm > 0.0 && ! juce::exactlyEqual (bpm, tempo))
    {
        tempo = bpm;
        updateLfoRates();
    }
}

void SynthEngine::updateLfoRates()
{
    for (size_t i = 0; i < lfos.size(); ++i)
    {
        const auto& lfoParams = currentParams.lfos[i];

        if (lfoParams.sync)
        {
            const auto index = static_cast<size_t> (juce::jlimit (0, static_cast<int> (Tempo::lfoDivisions.size()) - 1,
                                                                  lfoParams.division));
            lfos[i].setRate (static_cast<float> (tempo / 60.0 / Tempo::lfoDivisions[index].beats));
        }
        else
        {
            lfos[i].setRate (lfoParams.rate);
        }
    }
}

SynthEngine::Modulation SynthEngine::currentModulation() const
{
    Modulation mod;

    for (size_t i = 0; i < lfos.size(); ++i)
    {
        const auto& lfoParams = currentParams.lfos[i];
        const auto value = lfos[i].getValue() * lfoParams.depth;

        switch (lfoParams.destination)
        {
            case LfoDestination::off:
                break;
            case LfoDestination::pitch:
                mod.pitchSemitones += value * maxPitchSemitones;
                break;
            case LfoDestination::cutoff:
                mod.cutoffOctaves += value * maxCutoffOctaves;
                break;
            case LfoDestination::amp:
                // Tremolo dips the level from 1 down to (1 - depth), never above 1.
                mod.amp *= 1.0f - lfoParams.depth * 0.5f * (1.0f - lfos[i].getValue());
                break;
        }
    }

    return mod;
}

void SynthEngine::applyPitchModulation (float semitones)
{
    const auto ratio = std::pow (2.0f, semitones / 12.0f);

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
            voice->setPitchRatio (ratio);
}

void SynthEngine::render (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi)
{
    buffer.clear();

    const auto numSamples = buffer.getNumSamples();
    juce::dsp::AudioBlock<float> fullBlock (buffer);
    auto mod = currentModulation();

    for (int start = 0; start < numSamples; start += modulationInterval)
    {
        const auto chunkSize = std::min (modulationInterval, numSamples - start);

        if (chunkHasNoteOn (midi, start, chunkSize))
        {
            bool restarted = false;

            for (size_t i = 0; i < lfos.size(); ++i)
            {
                if (currentParams.lfos[i].retrigger)
                {
                    lfos[i].restart();
                    restarted = true;
                }
            }

            if (restarted)
                mod = currentModulation();
        }

        applyPitchModulation (mod.pitchSemitones);
        filter.setCutoffFrequency (juce::jlimit (20.0f, 20000.0f,
                                                 currentParams.cutoff * std::pow (2.0f, mod.cutoffOctaves)));

        synth.renderNextBlock (buffer, midi, start, chunkSize);

        auto chunk = fullBlock.getSubBlock (static_cast<size_t> (start), static_cast<size_t> (chunkSize));
        juce::dsp::ProcessContextReplacing<float> chunkContext (chunk);
        filter.process (chunkContext);

        for (auto& lfo : lfos)
            lfo.advance (chunkSize);

        const auto next = currentModulation();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.applyGainRamp (channel, start, chunkSize, mod.amp, next.amp);

        mod = next;
    }

    juce::dsp::ProcessContextReplacing<float> context (fullBlock);
    gain.process (context);
}

void SynthEngine::reset()
{
    synth.allNotesOff (0, false);
    filter.reset();
    gain.reset();

    for (auto& lfo : lfos)
        lfo.reset();
}
