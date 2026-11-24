#pragma once

#include "Arpeggiator.h"
#include "DrumMachine.h"
#include "Metronome.h"
#include "PatternBank.h"
#include "Sampler.h"
#include "Scales.h"
#include "SynthEngine.h"
#include "SynthTracks.h"

struct SequencerParams
{
    float bpm = 100.0f;
    float swing = 0.0f; // 0..1, delays odd steps by up to half a step
    int length = Seq::numSteps;
    Scale scale = Scale::none;
    int root = 0; // 0 = C
    bool followHost = true;  // use the host's tempo and transport when it provides them
    bool metronome = false;
    float metronomeDb = -6.0f;
};

struct MixParams
{
    float drumDb = 0.0f;
    float sampleDb = 0.0f;
    float echo = 0.15f;         // 0..1
    float echoFeedback = 0.35f; // 0..0.9
    float space = 0.15f;        // reverb amount, 0..1
    float masterDb = -3.0f;
};

struct SynthTrackParams
{
    SynthParams synth;
    ArpParams arp;
    int octave = 2;          // the grid's bottom row is the key's root in this octave (C2 = MIDI 48)
    float noteLength = 0.5f; // in steps
    bool muted = false;      // silences the track's pattern; live playing still sounds
    bool soloed = false;     // while anything is soloed, only soloed patterns play
};

struct GrooveParams
{
    std::array<SynthTrackParams, Seq::numSynthTracks> tracks;
    DrumParams drums;
    SamplerParams samples;
    SequencerParams sequencer;
    MixParams mix;
};

struct HostTransport
{
    bool playing = false;
    bool hasPosition = false;
    double ppqPosition = 0.0;
    double bpm = 0.0; // 0 when the host doesn't report a tempo
};

// The whole instrument: clock, sequencer, synth tracks (each a synth with its own arpeggiator),
// drum machine, sampler, metronome, echo, reverb and a master limiter.
//
// Live MIDI (the on-screen keyboard and the host) plays the "live" track, which is the one the
// editor has selected.
class Groovebox
{
public:
    static constexpr int liveChannel = 1;
    static constexpr int sequencerChannel = 2;
    static constexpr int previewChannel = 4;
    static constexpr int drumChannel = 10;
    static constexpr int firstSampleNote = 60; // channel 10 notes 60-63 play the sample slots

    // Hit tracks: the drums first, then the sample slots.
    static constexpr int numHitTracks = Seq::numDrumTracks + Seq::numSampleTracks;
    static constexpr int sampleTrack (int slot) { return Seq::numDrumTracks + slot; }

    explicit Groovebox (PatternBank& bank);

    void prepare (double sampleRate, int maxBlockSize);
    void setParams (const GrooveParams& params);
    // The first numInputChannels channels of `buffer` hold the audio input (the mic) on the way in.
    // The output is written over the whole buffer.
    void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midiIn, const HostTransport& host = {},
                  int numInputChannels = 0);
    void reset();

    // Safe to call from the UI thread.
    void setPlaying (bool shouldPlay) { playRequested.store (shouldPlay); }
    bool isPlayRequested() const { return playRequested.load(); }
    bool isRunning() const { return running.load(); }
    void setRecording (bool shouldRecord) { recording.store (shouldRecord); }
    bool isRecording() const { return recording.load(); }
    void previewDrum (int track);
    void previewSample (int slot) { previewDrum (sampleTrack (slot)); }
    void previewNote (int track, int note); // plays a short note without recording or arpeggiating it
    void setLiveTrack (int track) { liveTrack.store (juce::jlimit (0, Seq::numSynthTracks - 1, track)); }
    int getLiveTrack() const { return liveTrack.load(); }

    int getCurrentStep() const { return currentStep.load(); } // -1 when stopped
    int getPlayingPattern() const { return playingPattern.load(); }
    int getSongSlot() const { return songSlot.load(); }
    double getTempo() const { return tempo.load(); }
    juce::uint32 takeDrumHits() { return drumHitsForUi.exchange (0); } // bit per hit track
    Sampler& getSampler() { return sampler; }

private:
    struct DrumHit
    {
        int offset;
        int track;
        float velocity;
    };

    struct PendingNoteOff
    {
        int track;
        int note;
        int channel;
        juce::int64 dueSample;
    };

    struct SynthTrack
    {
        SynthEngine synth;
        Arpeggiator arp;
        juce::MidiBuffer arpInput; // live and pattern notes, before the arpeggiator
        juce::MidiBuffer midi;     // what the synth plays
        bool arpWasEnabled = false;
    };

    // A note being recorded from live playing, waiting for its note-off to set its length.
    struct HeldRecording
    {
        bool active = false;
        double startPosition = 0.0; // in steps
        int pattern = 0, track = 0, step = 0, row = 0;
    };

    struct PreviewNote
    {
        int track;
        int note;
    };

    int patternForLoop (juce::int64 loop, int& slot) const;
    double stepStartTime (juce::int64 step) const;
    void handleStep (juce::int64 step, int offset, double samplesPerStep, int numSamples);
    void recordHit (int track, double position);
    void recordNote (int track, int note, double position);
    void finishRecordedNote (int note, double position);
    void playSequencerNote (int track, int note, int offset, juce::int64 lengthSamples, int numSamples);
    void switchLiveTrack (int track);
    void addDrumHit (int offset, int track, float velocity);
    void flushNoteOffs (int numSamples);
    void playPreviewNotes();
    void stopSequencerNotes();
    void renderHits (int numSamples);
    void renderClicks (int numSamples);
    void applyEcho (int numSamples, double samplesPerStep);
    void writeOutput (juce::AudioBuffer<float>& output, int numSamples);

    PatternBank& bank;
    std::array<std::unique_ptr<SynthTrack>, Seq::numSynthTracks> tracks;
    DrumMachine drums;
    Sampler sampler;
    Metronome metronome;
    GrooveParams params;

    double sampleRate = 44100.0;
    juce::AudioBuffer<float> synthBuffer, trackBuffer, drumBuffer, sampleBuffer;
    int activeLiveTrack = SynthTracks::defaultLiveTrack;

    std::array<DrumHit, 256> drumHits {};
    int numDrumHits = 0;
    std::array<PendingNoteOff, 256> noteOffs {};
    std::array<HeldRecording, 128> heldRecordings {};
    int numNoteOffs = 0;
    std::array<juce::int64, numHitTracks> skipHitStep {};
    std::array<std::pair<int, bool>, 8> clicks {}; // offset, accent
    int numClicks = 0;

    juce::int64 sampleClock = 0;
    double internalPosition = 0.0;   // in steps
    double blockStartPosition = 0.0; // in steps
    double stepsPerSample = 0.0;
    bool wasRunning = false;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> echo { 1 };
    juce::SmoothedValue<float> echoDelaySamples;
    juce::Reverb reverb;
    juce::dsp::Limiter<float> limiter;
    juce::SmoothedValue<float> drumGain, sampleGain, masterGain;

    std::atomic<bool> playRequested { false };
    std::atomic<bool> recording { false };
    std::atomic<bool> running { false };
    std::atomic<juce::uint32> pendingPreviews { 0 };
    juce::AbstractFifo previewFifo { 32 };
    std::array<PreviewNote, 32> previewNotes {};
    std::atomic<int> liveTrack { SynthTracks::defaultLiveTrack };
    std::atomic<juce::uint32> drumHitsForUi { 0 };
    std::atomic<int> currentStep { -1 };
    std::atomic<int> playingPattern { 0 };
    std::atomic<int> songSlot { -1 };
    std::atomic<double> tempo { 100.0 };
};
