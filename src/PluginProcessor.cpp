#include "PluginProcessor.h"
#include "ParamIds.h"
#include "PatternGenerator.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace
{
using namespace juce;

std::unique_ptr<AudioParameterFloat> floatParam (const String& id, const String& name, NormalisableRange<float> range,
                                                 float defaultValue, const String& label = {})
{
    return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, defaultValue,
                                                  AudioParameterFloatAttributes().withLabel (label));
}

std::unique_ptr<AudioParameterChoice> choiceParam (const String& id, const String& name, const StringArray& choices,
                                                   int defaultIndex)
{
    return std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, choices, defaultIndex);
}

std::unique_ptr<AudioParameterBool> boolParam (const String& id, const String& name, bool defaultValue)
{
    return std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, defaultValue);
}

NormalisableRange<float> skewed (float min, float max, float centre, float interval = 0.0f)
{
    NormalisableRange<float> range (min, max, interval);
    range.setSkewForCentre (centre);
    return range;
}

int asInt (const std::atomic<float>* value)
{
    return static_cast<int> (std::lround (value->load()));
}

bool asBool (const std::atomic<float>* value)
{
    return value->load() >= 0.5f;
}
} // namespace

SynthAudioProcessor::SynthAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "Params", createParameterLayout (initValues))
{
    auto get = [this] (const juce::String& id)
    {
        auto* value = parameters.getRawParameterValue (id);
        jassert (value != nullptr);
        return value;
    };

    using namespace ParamIds;

    for (int t = 0; t < Seq::numSynthTracks; ++t)
    {
        auto& track = raw.tracks[static_cast<size_t> (t)];
        auto getTrack = [&] (const juce::String& base) { return get (synth (t, base)); };

        track.waveform = getTrack (waveform);
        track.attack = getTrack (attack);
        track.decay = getTrack (decay);
        track.sustain = getTrack (sustain);
        track.release = getTrack (release);
        track.cutoff = getTrack (cutoff);
        track.resonance = getTrack (resonance);
        track.gain = getTrack (gain);

        for (int i = 0; i < SynthEngine::numLfos; ++i)
            track.lfos[static_cast<size_t> (i)] = { getTrack (lfo (i, "Shape")), getTrack (lfo (i, "Destination")),
                                                    getTrack (lfo (i, "Rate")), getTrack (lfo (i, "Depth")),
                                                    getTrack (lfo (i, "Sync")), getTrack (lfo (i, "Division")),
                                                    getTrack (lfo (i, "Retrigger")) };

        track.arpOn = getTrack (arpOn);
        track.arpMode = getTrack (arpMode);
        track.arpRate = getTrack (arpRate);
        track.arpOctaves = getTrack (arpOctaves);
        track.arpGate = getTrack (arpGate);
        track.octave = getTrack (octave);
        track.noteLength = getTrack (noteLength);
        track.mute = getTrack (mute);
        track.solo = getTrack (solo);
    }

    for (int track = 0; track < DrumMachine::numTracks; ++track)
        raw.drums[static_cast<size_t> (track)] = { get (drum (track, "Level")), get (drum (track, "Tune")),
                                                   get (drum (track, "Decay")), get (drum (track, "Mute")),
                                                   get (drum (track, "Solo")) };

    for (int slot = 0; slot < Sampler::numSlots; ++slot)
        raw.samples[static_cast<size_t> (slot)] = { get (sample (slot, "Level")), get (sample (slot, "Tune")),
                                                    get (sample (slot, "Reverse")), get (sample (slot, "Mute")),
                                                    get (sample (slot, "Solo")) };

    raw.bpm = get (bpm);
    raw.swing = get (swing);
    raw.steps = get (steps);
    raw.scale = get (scale);
    raw.root = get (root);
    raw.followHost = get (followHost);
    raw.metronome = get (metronome);
    raw.metronomeLevel = get (metronomeLevel);

    raw.drumLevel = get (drumLevel);
    raw.sampleLevel = get (sampleLevel);
    raw.echo = get (echo);
    raw.echoFeedback = get (echoFeedback);
    raw.space = get (space);
    raw.master = get (master);

    // A beat and a tune to play with on first launch. A saved session replaces it.
    PatternGenerator::loadDemo (bank);
}

juce::AudioProcessorValueTreeState::ParameterLayout SynthAudioProcessor::createParameterLayout (std::map<juce::String, float>& initValues)
{
    using namespace juce;
    using namespace ParamIds;

    AudioProcessorValueTreeState::ParameterLayout layout;

    // Synth tracks. Each track's defaults come from its default preset (see SynthTracks).
    for (int t = 0; t < Seq::numSynthTracks; ++t)
    {
        const auto& info = SynthTracks::tracks[static_cast<size_t> (t)];
        const auto* preset = findSoundPreset (info.preset);
        const auto trackName = String (info.name) + " ";

        auto presetValue = [preset, &initValues] (const String& base, float fallback)
        {
            initValues[base] = fallback;
            if (preset != nullptr)
                for (const auto& [id, value] : preset->values)
                    if (id == base)
                        return value;
            return fallback;
        };
        auto addFloat = [&] (const String& base, const String& name, NormalisableRange<float> range, float fallback,
                             const String& label = {})
        { layout.add (floatParam (synth (t, base), trackName + name, range, presetValue (base, fallback), label)); };
        auto addChoice = [&] (const String& base, const String& name, const StringArray& choices, int fallback)
        { layout.add (choiceParam (synth (t, base), trackName + name, choices, roundToInt (presetValue (base, static_cast<float> (fallback))))); };
        auto addBool = [&] (const String& base, const String& name, bool fallback)
        { layout.add (boolParam (synth (t, base), trackName + name, presetValue (base, fallback ? 1.0f : 0.0f) >= 0.5f)); };

        addChoice (waveform, "Waveform", { "Sine", "Saw", "Square" }, 1);
        addFloat (attack, "Attack", skewed (0.001f, 5.0f, 0.3f), 0.01f, "s");
        addFloat (decay, "Decay", skewed (0.001f, 5.0f, 0.3f), 0.3f, "s");
        addFloat (sustain, "Sustain", { 0.0f, 1.0f }, 0.5f);
        addFloat (release, "Release", skewed (0.001f, 5.0f, 0.3f), 0.3f, "s");
        addFloat (cutoff, "Cutoff", skewed (20.0f, 20000.0f, 1000.0f), 3000.0f, "Hz");
        addFloat (resonance, "Resonance", { 0.5f, 10.0f }, 1.0f);
        addFloat (gain, "Volume", { -48.0f, 6.0f }, -6.0f, "dB");

        // Destinations default to Pitch and Cutoff with zero depth, so a new LFO is ready to turn up.
        const std::array defaultDestinations { 1, 2 };
        const std::array defaultRates { 5.0f, 0.5f };

        for (int i = 0; i < SynthEngine::numLfos; ++i)
        {
            const auto prefix = "LFO " + String (i + 1) + " ";
            const auto index = static_cast<size_t> (i);
            addChoice (lfo (i, "Shape"), prefix + "Shape", { "Sine", "Triangle", "Saw", "Square", "S&H" }, 0);
            addChoice (lfo (i, "Destination"), prefix + "Destination", { "Off", "Pitch", "Cutoff", "Amp" }, defaultDestinations[index]);
            addFloat (lfo (i, "Rate"), prefix + "Rate", skewed (0.01f, 20.0f, 2.0f), defaultRates[index], "Hz");
            // Skewed so small depths (e.g. subtle vibrato) get more of the knob's travel.
            addFloat (lfo (i, "Depth"), prefix + "Depth", skewed (0.0f, 1.0f, 0.2f), 0.0f);
            addBool (lfo (i, "Sync"), prefix + "Sync", false);
            addChoice (lfo (i, "Division"), prefix + "Division", Tempo::names (Tempo::lfoDivisions), Tempo::defaultLfoDivision);
            addBool (lfo (i, "Retrigger"), prefix + "Retrigger", false);
        }

        addBool (arpOn, "Arp On", false);
        addChoice (arpMode, "Arp Mode", { "Up", "Down", "Up+Down", "Random" }, 0);
        addChoice (arpRate, "Arp Rate", Tempo::names (Tempo::arpDivisions), Tempo::defaultArpDivision);
        addChoice (arpOctaves, "Arp Octaves", { "1", "2", "3" }, 0);
        addFloat (arpGate, "Arp Gate", { 0.05f, 1.0f }, 0.5f);

        StringArray octaves;
        for (int o = SynthTracks::minOctave; o <= SynthTracks::maxOctave; ++o)
            octaves.add (String (o));
        layout.add (choiceParam (synth (t, octave), trackName + "Octave", octaves, info.octave - SynthTracks::minOctave));
        layout.add (floatParam (synth (t, noteLength), trackName + "Note Length", skewed (0.1f, 16.0f, 1.0f, 0.01f),
                                info.noteLength, "steps"));
        layout.add (boolParam (synth (t, mute), trackName + "Mute", false));
        layout.add (boolParam (synth (t, solo), trackName + "Solo", false));
    }

    // Drums
    for (int track = 0; track < DrumMachine::numTracks; ++track)
    {
        const auto name = DrumMachine::trackName (track) + " ";
        layout.add (floatParam (drum (track, "Level"), name + "Level", { 0.0f, 1.0f }, 0.8f));
        layout.add (floatParam (drum (track, "Tune"), name + "Tune", { -12.0f, 12.0f }, 0.0f, "st"));
        layout.add (floatParam (drum (track, "Decay"), name + "Decay", skewed (0.2f, 2.5f, 1.0f), 1.0f));
        layout.add (boolParam (drum (track, "Mute"), name + "Mute", false));
        layout.add (boolParam (drum (track, "Solo"), name + "Solo", false));
    }

    // Samples
    for (int slot = 0; slot < Sampler::numSlots; ++slot)
    {
        const auto name = "Sample " + String (slot + 1) + " ";
        layout.add (floatParam (sample (slot, "Level"), name + "Level", { -36.0f, 6.0f }, 0.0f, "dB"));
        layout.add (floatParam (sample (slot, "Tune"), name + "Tune", { -24.0f, 24.0f, 1.0f }, 0.0f, "st"));
        layout.add (boolParam (sample (slot, "Reverse"), name + "Reverse", false));
        layout.add (boolParam (sample (slot, "Mute"), name + "Mute", false));
        layout.add (boolParam (sample (slot, "Solo"), name + "Solo", false));
    }

    // Sequencer
    layout.add (floatParam (bpm, "Tempo", { 40.0f, 200.0f, 1.0f }, 100.0f, "BPM"));
    layout.add (floatParam (swing, "Swing", { 0.0f, 1.0f }, 0.0f));
    layout.add (choiceParam (steps, "Steps", { "8", "16", "24", "32" }, 3));
    layout.add (choiceParam (scale, "Scale", Scales::names(), static_cast<int> (Scale::none)));
    layout.add (choiceParam (root, "Key", Scales::rootNames(), 0));
    layout.add (boolParam (followHost, "Follow Host", true));
    layout.add (boolParam (metronome, "Metronome", false));
    layout.add (floatParam (metronomeLevel, "Metronome Level", { -36.0f, 0.0f }, -6.0f, "dB"));

    // Mix
    layout.add (floatParam (drumLevel, "Drum Level", { -36.0f, 6.0f }, 0.0f, "dB"));
    layout.add (floatParam (sampleLevel, "Sample Level", { -36.0f, 6.0f }, 0.0f, "dB"));
    layout.add (floatParam (echo, "Echo", { 0.0f, 1.0f }, 0.15f));
    layout.add (floatParam (echoFeedback, "Echo Repeats", { 0.0f, 0.9f }, 0.35f));
    layout.add (floatParam (space, "Space", { 0.0f, 1.0f }, 0.15f));
    layout.add (floatParam (master, "Master", { -36.0f, 6.0f }, -3.0f, "dB"));

    return layout;
}

GrooveParams SynthAudioProcessor::readParams() const
{
    GrooveParams p;

    for (size_t t = 0; t < p.tracks.size(); ++t)
    {
        const auto& r = raw.tracks[t];
        auto& track = p.tracks[t];
        auto& synth = track.synth;
        synth.waveform = static_cast<Waveform> (asInt (r.waveform));
        synth.attack = r.attack->load();
        synth.decay = r.decay->load();
        synth.sustain = r.sustain->load();
        synth.release = r.release->load();
        synth.cutoff = r.cutoff->load();
        synth.resonance = r.resonance->load();
        synth.gainDb = r.gain->load();

        for (size_t i = 0; i < synth.lfos.size(); ++i)
        {
            const auto& rl = r.lfos[i];
            auto& lfo = synth.lfos[i];
            lfo.shape = static_cast<LfoShape> (asInt (rl.shape));
            lfo.destination = static_cast<LfoDestination> (asInt (rl.destination));
            lfo.rate = rl.rate->load();
            lfo.depth = rl.depth->load();
            lfo.sync = asBool (rl.sync);
            lfo.division = asInt (rl.division);
            lfo.retrigger = asBool (rl.retrigger);
        }

        track.arp.enabled = asBool (r.arpOn);
        track.arp.mode = static_cast<ArpMode> (asInt (r.arpMode));
        track.arp.division = asInt (r.arpRate);
        track.arp.octaves = asInt (r.arpOctaves) + 1;
        track.arp.gate = r.arpGate->load();
        track.octave = asInt (r.octave) + SynthTracks::minOctave;
        track.noteLength = r.noteLength->load();
        track.muted = asBool (r.mute);
        track.soloed = asBool (r.solo);
    }

    for (size_t track = 0; track < p.drums.tracks.size(); ++track)
    {
        const auto& r = raw.drums[track];
        p.drums.tracks[track] = { r.level->load(), r.tune->load(), r.decay->load(), asBool (r.mute), asBool (r.solo) };
    }

    for (size_t slot = 0; slot < p.samples.slots.size(); ++slot)
    {
        const auto& r = raw.samples[slot];
        p.samples.slots[slot] = { r.level->load(), r.tune->load(), asBool (r.reverse), asBool (r.mute), asBool (r.solo) };
    }

    auto& seq = p.sequencer;
    seq.bpm = raw.bpm->load();
    seq.swing = raw.swing->load();
    seq.length = (asInt (raw.steps) + 1) * 8;
    seq.scale = static_cast<Scale> (asInt (raw.scale));
    seq.root = asInt (raw.root);
    seq.followHost = asBool (raw.followHost);
    seq.metronome = asBool (raw.metronome);
    seq.metronomeDb = raw.metronomeLevel->load();

    p.mix = { raw.drumLevel->load(), raw.sampleLevel->load(), raw.echo->load(),
              raw.echoFeedback->load(), raw.space->load(), raw.master->load() };

    return p;
}

void SynthAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    keyboardState.reset();
    groovebox.setParams (readParams());
    groovebox.prepare (sampleRate, samplesPerBlock);
}

bool SynthAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();
    const auto& input = layouts.getMainInputChannelSet();
    const auto monoOrStereo = [] (const juce::AudioChannelSet& set)
    { return set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo(); };

    // The input (for recording samples) is optional.
    return monoOrStereo (output) && (input.isDisabled() || monoOrStereo (input));
}

void SynthAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Merges notes played on the on-screen keyboard into the incoming MIDI.
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    HostTransport host;
    if (auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            host.playing = position->getIsPlaying();
            if (const auto hostBpm = position->getBpm())
                host.bpm = *hostBpm;
            if (const auto ppq = position->getPpqPosition())
            {
                host.ppqPosition = *ppq;
                host.hasPosition = true;
            }
        }
    }

    groovebox.setParams (readParams());
    groovebox.process (buffer, midi, host, getTotalNumInputChannels());
    midi.clear();
}

juce::AudioProcessorEditor* SynthAudioProcessor::createEditor()
{
    return new SynthAudioProcessorEditor (*this);
}

void SynthAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.removeChild (state.getChildWithName (PatternBank::treeType), nullptr);
    state.appendChild (bank.toValueTree(), nullptr);
    state.removeChild (state.getChildWithName (Sampler::treeType), nullptr);
    state.appendChild (groovebox.getSampler().toValueTree(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SynthAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    const auto patterns = state.getChildWithName (PatternBank::treeType);
    if (patterns.isValid())
    {
        bank.fromValueTree (patterns);
        state.removeChild (patterns, nullptr);
    }

    const auto samples = state.getChildWithName (Sampler::treeType);
    if (samples.isValid())
    {
        groovebox.getSampler().fromValueTree (samples);
        state.removeChild (samples, nullptr);
    }

    parameters.replaceState (state);
}

void SynthAudioProcessor::applySoundPreset (int track, int index)
{
    const auto& presets = soundPresets();
    if (index < 0 || index >= static_cast<int> (presets.size()))
        return;

    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* param = parameters.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    for (const auto& base : ParamIds::soundParameters())
        set (ParamIds::synth (track, base), initValues[base]);

    for (const auto& [base, value] : presets[static_cast<size_t> (index)].values)
        set (ParamIds::synth (track, base), value);
}

Scale SynthAudioProcessor::getScale() const
{
    return static_cast<Scale> (asInt (raw.scale));
}

bool SynthAudioProcessor::loadSampleFile (int slot, const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return false;

    const auto length = static_cast<int> (std::min<juce::int64> (reader->lengthInSamples,
                                                                  static_cast<juce::int64> (Sampler::maxFileSeconds * reader->sampleRate)));
    const auto channels = static_cast<int> (reader->numChannels);
    juce::AudioBuffer<float> source (channels, length);
    if (! reader->read (&source, 0, length, 0, true, true))
        return false;

    juce::AudioBuffer<float> mono (1, length);
    mono.clear();
    for (int channel = 0; channel < channels; ++channel)
        mono.addFrom (0, 0, source, channel, 0, length, 1.0f / static_cast<float> (channels));

    groovebox.getSampler().setSample (slot, std::move (mono), reader->sampleRate);
    return true;
}

void SynthAudioProcessor::randomiseDrums (int pattern)
{
    PatternGenerator::randomiseDrums (bank, pattern, random);
}

void SynthAudioProcessor::randomiseMelody (int pattern, int track)
{
    PatternGenerator::randomiseMelody (bank, pattern, track, getScale(), random);
}

void SynthAudioProcessor::randomiseAllMelodies (int pattern)
{
    PatternGenerator::randomiseAllMelodies (bank, pattern, getScale(), random);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SynthAudioProcessor();
}
