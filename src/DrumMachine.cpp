#include "DrumMachine.h"

namespace
{
constexpr float twoPi = juce::MathConstants<float>::twoPi;
constexpr float silence = 1.0e-4f;

// Inharmonic square-wave frequencies that give hats and cymbals their metallic ring.
constexpr std::array<float, 6> hatFrequencies { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
constexpr std::array<float, 2> cowbellFrequencies { 540.0f, 800.0f };

float square (double phase)
{
    return phase < 0.5 ? 1.0f : -1.0f;
}

void advancePhase (double& phase, double increment)
{
    phase += increment;
    phase -= std::floor (phase);
}
} // namespace

juce::String DrumMachine::trackName (int track)
{
    static const std::array<const char*, numTracks> names { "Kick", "Snare", "Clap", "Closed Hat",
                                                             "Open Hat", "Low Tom", "High Tom", "Cowbell" };
    return names[static_cast<size_t> (juce::jlimit (0, numTracks - 1, track))];
}

int DrumMachine::midiNoteForTrack (int track)
{
    static const std::array<int, numTracks> notes { 36, 38, 39, 42, 46, 45, 50, 56 };
    return notes[static_cast<size_t> (juce::jlimit (0, numTracks - 1, track))];
}

int DrumMachine::trackForMidiNote (int note)
{
    for (int track = 0; track < numTracks; ++track)
        if (midiNoteForTrack (track) == note)
            return track;

    return -1;
}

void DrumMachine::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    const juce::dsp::ProcessSpec spec { sampleRate, 1, 1 };

    for (auto& voice : voices)
    {
        voice.filterA.prepare (spec);
        voice.filterB.prepare (spec);
    }

    using Type = juce::dsp::StateVariableTPTFilterType;
    voices[snare].filterA.setType (Type::highpass);
    voices[clap].filterA.setType (Type::bandpass);
    voices[closedHat].filterA.setType (Type::bandpass);
    voices[closedHat].filterB.setType (Type::highpass);
    voices[openHat].filterA.setType (Type::bandpass);
    voices[openHat].filterB.setType (Type::highpass);
    voices[cowbell].filterA.setType (Type::bandpass);

    reset();
}

void DrumMachine::setParams (const DrumParams& newParams)
{
    params = newParams;
}

float DrumMachine::coeff (float seconds) const
{
    return std::exp (-1.0f / (std::max (seconds, 1.0e-4f) * static_cast<float> (sampleRate)));
}

float DrumMachine::noise()
{
    // xorshift32: cheap, allocation-free white noise.
    noiseState ^= noiseState << 13;
    noiseState ^= noiseState >> 17;
    noiseState ^= noiseState << 5;
    return static_cast<float> (noiseState) / 2147483648.0f - 1.0f;
}

void DrumMachine::trigger (int track, float velocity)
{
    if (track < 0 || track >= numTracks)
        return;

    const auto& trackParams = params.tracks[static_cast<size_t> (track)];
    const auto decay = trackParams.decay;
    auto& voice = voices[static_cast<size_t> (track)];

    voice.active = true;
    voice.age = 0;
    voice.velocity = velocity;
    voice.tuneRatio = std::pow (2.0f, trackParams.tuneSemitones / 12.0f);
    voice.phases.fill (0.0);
    voice.filterA.reset();
    voice.filterB.reset();
    voice.amp = 1.0f;
    voice.aux = 1.0f;
    voice.pitch = 1.0f;

    switch (track)
    {
        case kick:
            voice.ampCoeff = coeff (0.45f * decay);
            voice.pitchCoeff = coeff (0.035f);
            voice.auxCoeff = coeff (0.003f);
            break;

        case snare:
            voice.ampCoeff = coeff (0.07f);
            voice.auxCoeff = coeff (0.16f * decay);
            voice.filterA.setCutoffFrequency (1500.0f * voice.tuneRatio);
            break;

        case clap:
            voice.filterA.setCutoffFrequency (1100.0f * voice.tuneRatio);
            voice.filterA.setResonance (1.2f);
            break;

        case closedHat:
        case openHat:
            voice.ampCoeff = coeff ((track == closedHat ? 0.035f : 0.32f) * decay);
            voice.filterA.setCutoffFrequency (juce::jmin (10000.0f * voice.tuneRatio, 18000.0f));
            voice.filterB.setCutoffFrequency (juce::jmin (7000.0f * voice.tuneRatio, 16000.0f));

            // A closed hat cuts off a ringing open hat, like on a real kit.
            if (track == closedHat && voices[openHat].active)
                voices[openHat].ampCoeff = coeff (0.005f);
            break;

        case lowTom:
        case highTom:
            voice.ampCoeff = coeff ((track == lowTom ? 0.3f : 0.25f) * decay);
            voice.pitchCoeff = coeff (0.08f);
            voice.auxCoeff = coeff (0.004f);
            break;

        case cowbell:
            voice.ampCoeff = coeff (0.25f * decay);
            voice.auxCoeff = coeff (0.02f);
            voice.filterA.setCutoffFrequency (1800.0f * voice.tuneRatio);
            voice.filterA.setResonance (1.5f);
            break;

        default:
            break;
    }
}

float DrumMachine::metal (Voice& voice, int numOscillators, const float* frequencies)
{
    float sum = 0.0f;

    for (int i = 0; i < numOscillators; ++i)
    {
        auto& phase = voice.phases[static_cast<size_t> (i)];
        sum += square (phase);
        advancePhase (phase, frequencies[i] * voice.tuneRatio / sampleRate);
    }

    return sum / static_cast<float> (numOscillators);
}

float DrumMachine::renderSample (int track, Voice& voice)
{
    const auto seconds = static_cast<float> (voice.age) / static_cast<float> (sampleRate);
    float out = 0.0f;

    switch (track)
    {
        case kick:
        {
            const auto frequency = 47.0f * voice.tuneRatio * (1.0f + 2.2f * voice.pitch);
            const auto body = std::sin (twoPi * static_cast<float> (voice.phases[0])) * voice.amp;
            advancePhase (voice.phases[0], frequency / sampleRate);
            out = std::tanh (1.6f * (body + 0.25f * noise() * voice.aux));
            voice.active = voice.amp > silence;
            break;
        }

        case snare:
        {
            const auto tone = 0.5f * (std::sin (twoPi * static_cast<float> (voice.phases[0]))
                                      + std::sin (twoPi * static_cast<float> (voice.phases[1])));
            advancePhase (voice.phases[0], 180.0f * voice.tuneRatio / sampleRate);
            advancePhase (voice.phases[1], 330.0f * voice.tuneRatio / sampleRate);
            const auto snares = voice.filterA.processSample (0, noise());
            out = 0.55f * tone * voice.amp + 0.8f * snares * voice.aux;
            voice.active = std::max (voice.amp, voice.aux) > silence;
            break;
        }

        case clap:
        {
            // Three quick bursts, then a longer tail: the sound of several hands not quite together.
            float burst = 0.0f;
            for (auto start : { 0.0f, 0.011f, 0.022f })
                if (seconds >= start)
                    burst = std::max (burst, std::exp (-(seconds - start) / 0.007f));

            const auto tailDecay = 0.13f * params.tracks[clap].decay;
            const auto tail = seconds >= 0.025f ? 0.8f * std::exp (-(seconds - 0.025f) / tailDecay) : 0.0f;
            out = 1.8f * voice.filterA.processSample (0, noise()) * std::max (burst, tail);
            voice.active = seconds < 0.025f || tail > silence;
            break;
        }

        case closedHat:
        case openHat:
        {
            const auto source = metal (voice, static_cast<int> (hatFrequencies.size()), hatFrequencies.data())
                                + 0.3f * noise();
            out = 2.5f * voice.filterB.processSample (0, voice.filterA.processSample (0, source)) * voice.amp;
            voice.active = voice.amp > silence;
            break;
        }

        case lowTom:
        case highTom:
        {
            const auto base = track == lowTom ? 95.0f : 150.0f;
            const auto frequency = base * voice.tuneRatio * (1.0f + 0.6f * voice.pitch);
            const auto body = std::sin (twoPi * static_cast<float> (voice.phases[0]));
            advancePhase (voice.phases[0], frequency / sampleRate);
            out = 0.9f * body * voice.amp + 0.15f * noise() * voice.aux;
            voice.active = voice.amp > silence;
            break;
        }

        case cowbell:
        {
            const auto source = metal (voice, static_cast<int> (cowbellFrequencies.size()), cowbellFrequencies.data());
            const auto envelope = 0.6f * voice.aux + 0.4f * voice.amp;
            out = 1.6f * voice.filterA.processSample (0, source) * envelope;
            voice.active = voice.amp > silence;
            break;
        }

        default:
            voice.active = false;
            break;
    }

    voice.amp *= voice.ampCoeff;
    voice.aux *= voice.auxCoeff;
    voice.pitch *= voice.pitchCoeff;
    ++voice.age;

    return out;
}

void DrumMachine::render (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    for (int track = 0; track < numTracks; ++track)
    {
        auto& voice = voices[static_cast<size_t> (track)];
        if (! voice.active)
            continue;

        const auto gain = params.tracks[static_cast<size_t> (track)].level * voice.velocity;

        for (int i = 0; i < numSamples && voice.active; ++i)
        {
            const auto sample = renderSample (track, voice) * gain;

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.addSample (channel, startSample + i, sample);
        }
    }
}

void DrumMachine::reset()
{
    for (auto& voice : voices)
    {
        voice.active = false;
        voice.filterA.reset();
        voice.filterB.reset();
    }
}

bool DrumMachine::isTrackSounding (int track) const
{
    return track >= 0 && track < numTracks && voices[static_cast<size_t> (track)].active;
}
