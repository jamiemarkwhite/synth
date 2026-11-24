#include "Groovebox.h"
#include "StepClock.h"

#include <algorithm>

namespace
{
constexpr float melodyVelocity = 0.8f;
constexpr double echoBeats = 0.75; // dotted 8th
constexpr double previewSeconds = 0.25;

bool anySoloed (const GrooveParams& params)
{
    const auto soloed = [] (const auto& item) { return item.soloed; };
    return std::any_of (params.tracks.begin(), params.tracks.end(), soloed)
        || std::any_of (params.drums.tracks.begin(), params.drums.tracks.end(), soloed)
        || std::any_of (params.samples.slots.begin(), params.samples.slots.end(), soloed);
}

// Muting and soloing only affect patterns; live playing always sounds.
template <typename Item>
bool isSilenced (const Item& item, bool soloActive)
{
    return item.muted || (soloActive && ! item.soloed);
}
} // namespace

Groovebox::Groovebox (PatternBank& patternBank) : bank (patternBank)
{
    for (auto& track : tracks)
        track = std::make_unique<SynthTrack>();

    skipHitStep.fill (-1);
}

void Groovebox::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;

    for (auto& track : tracks)
    {
        track->synth.prepare (sampleRate, maxBlockSize, 2);
        track->arpInput.ensureSize (2048);
        track->midi.ensureSize (4096);
    }

    drums.prepare (sampleRate);
    sampler.prepare (sampleRate);
    metronome.prepare (sampleRate);

    synthBuffer.setSize (2, maxBlockSize);
    trackBuffer.setSize (2, maxBlockSize);
    drumBuffer.setSize (2, maxBlockSize);
    sampleBuffer.setSize (2, maxBlockSize);

    const juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (maxBlockSize), 2 };
    echo.prepare (spec);
    echo.setMaximumDelayInSamples (static_cast<int> (sampleRate * 2.5));
    echoDelaySamples.reset (sampleRate, 0.1);

    reverb.setSampleRate (sampleRate);

    limiter.prepare (spec);
    limiter.setThreshold (-1.0f);
    limiter.setRelease (100.0f);

    for (auto* gain : { &drumGain, &sampleGain, &masterGain })
        gain->reset (sampleRate, 0.02);

    setParams (params);
    drumGain.setCurrentAndTargetValue (drumGain.getTargetValue());
    sampleGain.setCurrentAndTargetValue (sampleGain.getTargetValue());
    masterGain.setCurrentAndTargetValue (masterGain.getTargetValue());

    reset();
}

void Groovebox::setParams (const GrooveParams& newParams)
{
    params = newParams;
    params.sequencer.length = juce::jlimit (1, Seq::numSteps, params.sequencer.length);

    for (size_t i = 0; i < tracks.size(); ++i)
    {
        tracks[i]->synth.setParams (params.tracks[i].synth);
        tracks[i]->arp.setParams (params.tracks[i].arp);
    }

    drums.setParams (params.drums);
    sampler.setParams (params.samples);

    juce::Reverb::Parameters reverbParams;
    reverbParams.roomSize = 0.78f;
    reverbParams.damping = 0.45f;
    reverbParams.width = 1.0f;
    reverbParams.dryLevel = 0.5f; // unity: juce::Reverb doubles the dry level internally
    reverbParams.wetLevel = 0.3f * params.mix.space;
    reverb.setParameters (reverbParams);

    drumGain.setTargetValue (juce::Decibels::decibelsToGain (params.mix.drumDb));
    sampleGain.setTargetValue (juce::Decibels::decibelsToGain (params.mix.sampleDb));
    masterGain.setTargetValue (juce::Decibels::decibelsToGain (params.mix.masterDb));
}

void Groovebox::reset()
{
    for (auto& track : tracks)
    {
        track->synth.reset();
        track->arp.reset();
    }

    drums.reset();
    sampler.reset();
    metronome.reset();
    echo.reset();
    reverb.reset();
    limiter.reset();

    numNoteOffs = 0;
    numDrumHits = 0;
    numClicks = 0;
    skipHitStep.fill (-1);
    internalPosition = 0.0;
    wasRunning = false;
    currentStep.store (-1);
    songSlot.store (-1);
}

void Groovebox::previewDrum (int track)
{
    if (track >= 0 && track < numHitTracks)
        pendingPreviews.fetch_or (juce::uint32 (1) << static_cast<juce::uint32> (track));
}

void Groovebox::previewNote (int track, int note)
{
    if (track < 0 || track >= Seq::numSynthTracks)
        return;

    const auto scope = previewFifo.write (1);
    if (scope.blockSize1 > 0)
        previewNotes[static_cast<size_t> (scope.startIndex1)] = { track, note };
    else if (scope.blockSize2 > 0)
        previewNotes[static_cast<size_t> (scope.startIndex2)] = { track, note };
}

void Groovebox::playPreviewNotes()
{
    const auto scope = previewFifo.read (previewFifo.getNumReady());
    scope.forEach ([this] (int index)
    {
        const auto [track, requestedNote] = previewNotes[static_cast<size_t> (index)];
        const auto note = juce::jlimit (0, 127, requestedNote);
        if (numNoteOffs == static_cast<int> (noteOffs.size()))
            return;

        tracks[static_cast<size_t> (track)]->midi.addEvent (juce::MidiMessage::noteOn (previewChannel, note, melodyVelocity), 0);
        noteOffs[static_cast<size_t> (numNoteOffs++)] = { track, note, previewChannel,
                                                          sampleClock + static_cast<juce::int64> (previewSeconds * sampleRate) };
    });
}

int Groovebox::patternForLoop (juce::int64 loop, int& slot) const
{
    slot = -1;

    if (bank.isSongMode())
    {
        std::array<int, Seq::songLength> filledSlots {};
        int count = 0;

        for (int i = 0; i < Seq::songLength; ++i)
            if (bank.getSongSlot (i) >= 0)
                filledSlots[static_cast<size_t> (count++)] = i;

        if (count > 0)
        {
            slot = filledSlots[static_cast<size_t> (loop % count)];
            return bank.getSongSlot (slot);
        }
    }

    return bank.getSelectedPattern();
}

double Groovebox::stepStartTime (juce::int64 step) const
{
    const auto swingDelay = 0.5 * static_cast<double> (juce::jlimit (0.0f, 1.0f, params.sequencer.swing));
    return static_cast<double> (step) + ((step & 1) != 0 ? swingDelay : 0.0);
}

void Groovebox::addDrumHit (int offset, int track, float velocity)
{
    if (numDrumHits < static_cast<int> (drumHits.size()))
        drumHits[static_cast<size_t> (numDrumHits++)] = { offset, track, velocity };
}

void Groovebox::handleStep (juce::int64 step, int offset, double samplesPerStep, int numSamples)
{
    const auto length = params.sequencer.length;
    const auto stepInPattern = static_cast<int> (step % length);
    int slot = -1;
    const auto pattern = patternForLoop (step / length, slot);

    currentStep.store (stepInPattern);
    playingPattern.store (pattern);
    songSlot.store (slot);

    // Counted from the start of the song, so the bar lines up with the host's bars.
    if (params.sequencer.metronome && step % 4 == 0 && numClicks < static_cast<int> (clicks.size()))
        clicks[static_cast<size_t> (numClicks++)] = { offset, step % 16 == 0 };

    // Slight accent on the beat gives the groove some bounce.
    const auto velocity = stepInPattern % 4 == 0 ? 1.0f : 0.82f;
    const bool soloActive = anySoloed (params);

    for (int track = 0; track < numHitTracks; ++track)
    {
        const bool isDrum = track < Seq::numDrumTracks;
        const auto sampleSlot = track - Seq::numDrumTracks;
        const bool on = isDrum ? bank.getDrumStep (pattern, track, stepInPattern)
                               : bank.getSampleStep (pattern, sampleSlot, stepInPattern);
        const bool silenced = isDrum ? isSilenced (params.drums.tracks[static_cast<size_t> (track)], soloActive)
                                     : isSilenced (params.samples.slots[static_cast<size_t> (sampleSlot)], soloActive);
        if (! on || silenced)
            continue;

        // This step was just recorded from a live hit, which already played it.
        if (skipHitStep[static_cast<size_t> (track)] == step)
        {
            skipHitStep[static_cast<size_t> (track)] = -1;
            continue;
        }

        addDrumHit (offset, track, velocity);
    }

    const auto& seq = params.sequencer;

    for (int track = 0; track < Seq::numSynthTracks; ++track)
    {
        const auto& trackParams = params.tracks[static_cast<size_t> (track)];
        const auto rows = bank.getMelodyRows (pattern, track, stepInPattern);
        if (rows == 0 || isSilenced (trackParams, soloActive))
            continue;

        for (int row = 0; row < Seq::numMelodyRows; ++row)
        {
            if ((rows & (juce::uint32 (1) << static_cast<juce::uint32> (row))) == 0)
                continue;

            // High octaves push the top rows past MIDI's highest note; those rows stay silent.
            const auto note = Scales::rowToNote (seq.scale, seq.root, trackParams.octave, row);
            if (note < 0 || note > 127)
                continue;

            // Notes drawn over several steps last exactly that long (up to the end of the pattern).
            // One-step notes use the track's Note Length.
            const auto drawnSteps = std::min (bank.getNoteLength (pattern, track, stepInPattern, row), length - stepInPattern);
            const auto steps = drawnSteps > 1 ? static_cast<double> (drawnSteps) : static_cast<double> (trackParams.noteLength);
            playSequencerNote (track, note, offset, std::max<juce::int64> (1, static_cast<juce::int64> (steps * samplesPerStep)),
                               numSamples);
        }
    }
}

void Groovebox::playSequencerNote (int track, int note, int offset, juce::int64 lengthSamples, int numSamples)
{
    // Through the arpeggiator, which passes notes straight on when it is off.
    auto& midi = tracks[static_cast<size_t> (track)]->arpInput;

    // A long note may still be held from an earlier step. End it here, or its late note-off
    // would cut this new note short.
    for (int i = numNoteOffs - 1; i >= 0; --i)
    {
        const auto& pending = noteOffs[static_cast<size_t> (i)];
        if (pending.track == track && pending.channel == sequencerChannel && pending.note == note)
        {
            midi.addEvent (juce::MidiMessage::noteOff (sequencerChannel, note), offset);
            noteOffs[static_cast<size_t> (i)] = noteOffs[static_cast<size_t> (--numNoteOffs)];
        }
    }

    if (numNoteOffs == static_cast<int> (noteOffs.size()))
        return;

    midi.addEvent (juce::MidiMessage::noteOn (sequencerChannel, note, melodyVelocity), offset);

    const auto due = sampleClock + offset + lengthSamples;
    if (due < sampleClock + numSamples)
        midi.addEvent (juce::MidiMessage::noteOff (sequencerChannel, note), static_cast<int> (due - sampleClock));
    else
        noteOffs[static_cast<size_t> (numNoteOffs++)] = { track, note, sequencerChannel, due };
}

void Groovebox::flushNoteOffs (int numSamples)
{
    for (int i = numNoteOffs - 1; i >= 0; --i)
    {
        const auto& pending = noteOffs[static_cast<size_t> (i)];
        if (pending.dueSample >= sampleClock + numSamples)
            continue;

        const auto offset = static_cast<int> (std::max<juce::int64> (0, pending.dueSample - sampleClock));
        auto& track = *tracks[static_cast<size_t> (pending.track)];
        auto& midi = pending.channel == sequencerChannel ? track.arpInput : track.midi;
        midi.addEvent (juce::MidiMessage::noteOff (pending.channel, pending.note), offset);
        noteOffs[static_cast<size_t> (i)] = noteOffs[static_cast<size_t> (--numNoteOffs)];
    }
}

void Groovebox::stopSequencerNotes()
{
    for (int i = numNoteOffs - 1; i >= 0; --i)
    {
        const auto& pending = noteOffs[static_cast<size_t> (i)];
        if (pending.channel != sequencerChannel)
            continue;

        // Releases the note in the arpeggiator too.
        tracks[static_cast<size_t> (pending.track)]->arpInput.addEvent (juce::MidiMessage::noteOff (sequencerChannel, pending.note), 0);
        noteOffs[static_cast<size_t> (i)] = noteOffs[static_cast<size_t> (--numNoteOffs)];
    }

    for (auto& track : tracks)
        track->midi.addEvent (juce::MidiMessage::allNotesOff (sequencerChannel), 0);
}

void Groovebox::switchLiveTrack (int track)
{
    // Notes held on the old track would otherwise hang.
    auto& old = *tracks[static_cast<size_t> (activeLiveTrack)];
    old.arp.reset();
    old.midi.addEvent (juce::MidiMessage::allNotesOff (liveChannel), 0);
    old.midi.addEvent (juce::MidiMessage::allNotesOff (Arpeggiator::outputChannel), 0);
    activeLiveTrack = track;
}

void Groovebox::recordHit (int track, double position)
{
    if (position < 0.0)
        return;

    const auto step = static_cast<juce::int64> (std::llround (position));
    const auto length = params.sequencer.length;
    int slot = -1;
    const auto pattern = patternForLoop (step / length, slot);
    const auto stepInPattern = static_cast<int> (step % length);
    if (track < Seq::numDrumTracks)
        bank.setDrumStep (pattern, track, stepInPattern, true);
    else
        bank.setSampleStep (pattern, track - Seq::numDrumTracks, stepInPattern, true);

    // If that step hasn't been played yet in this or an earlier block, don't play it again on top of the live hit.
    if (stepStartTime (step) >= blockStartPosition)
        skipHitStep[static_cast<size_t> (track)] = step;
}

void Groovebox::recordNote (int track, int note, double position)
{
    if (position < 0.0)
        return;

    const auto& seq = params.sequencer;
    const auto octave = params.tracks[static_cast<size_t> (track)].octave;
    const auto row = Scales::nearestRow (seq.scale, seq.root, octave, note, Seq::numMelodyRows);
    if (row < 0)
        return;

    const auto step = static_cast<juce::int64> (std::llround (position));
    int slot = -1;
    const auto pattern = patternForLoop (step / seq.length, slot);
    const auto stepInPattern = static_cast<int> (step % seq.length);
    bank.setMelodyStep (pattern, track, stepInPattern, row, true);

    if (note >= 0 && note < static_cast<int> (heldRecordings.size()))
        heldRecordings[static_cast<size_t> (note)] = { true, position, pattern, track, stepInPattern, row };
}

void Groovebox::finishRecordedNote (int note, double position)
{
    if (note < 0 || note >= static_cast<int> (heldRecordings.size()))
        return;

    auto& held = heldRecordings[static_cast<size_t> (note)];
    if (! held.active)
        return;

    held.active = false;
    const auto steps = static_cast<int> (std::llround (position - held.startPosition));
    if (steps >= 2)
        bank.setNote (held.pattern, held.track, held.step, held.row, std::min (steps, params.sequencer.length - held.step));
}

void Groovebox::renderHits (int numSamples)
{
    drumBuffer.clear();
    sampleBuffer.clear();

    std::sort (drumHits.begin(), drumHits.begin() + numDrumHits,
               [] (const DrumHit& a, const DrumHit& b) { return a.offset < b.offset; });

    int position = 0;
    for (int i = 0; i < numDrumHits; ++i)
    {
        const auto& hit = drumHits[static_cast<size_t> (i)];
        if (hit.offset > position)
        {
            drums.render (drumBuffer, position, hit.offset - position);
            sampler.render (sampleBuffer, position, hit.offset - position);
            position = hit.offset;
        }

        if (hit.track < Seq::numDrumTracks)
            drums.trigger (hit.track, hit.velocity);
        else
            sampler.trigger (hit.track - Seq::numDrumTracks, hit.velocity);

        drumHitsForUi.fetch_or (juce::uint32 (1) << static_cast<juce::uint32> (hit.track));
    }

    drums.render (drumBuffer, position, numSamples - position);
    sampler.render (sampleBuffer, position, numSamples - position);
}

void Groovebox::renderClicks (int numSamples)
{
    const auto gain = juce::Decibels::decibelsToGain (params.sequencer.metronomeDb);
    int position = 0;

    for (int i = 0; i < numClicks; ++i)
    {
        const auto [offset, accent] = clicks[static_cast<size_t> (i)];
        metronome.render (synthBuffer, position, offset - position, gain);
        position = offset;
        metronome.trigger (accent);
    }

    metronome.render (synthBuffer, position, numSamples - position, gain);
}

void Groovebox::applyEcho (int numSamples, double samplesPerStep)
{
    const auto maxDelay = static_cast<float> (echo.getMaximumDelayInSamples() - 1);
    echoDelaySamples.setTargetValue (juce::jlimit (1.0f, maxDelay, static_cast<float> (echoBeats * 4.0 * samplesPerStep)));

    const auto mix = params.mix.echo;
    const auto feedback = juce::jlimit (0.0f, 0.9f, params.mix.echoFeedback);
    auto* left = synthBuffer.getWritePointer (0);
    auto* right = synthBuffer.getWritePointer (1);

    // Ping-pong: the echo bounces between left and right.
    for (int i = 0; i < numSamples; ++i)
    {
        const auto delay = echoDelaySamples.getNextValue();
        const auto echoLeft = echo.popSample (0, delay);
        const auto echoRight = echo.popSample (1, delay);
        echo.pushSample (0, 0.5f * (left[i] + right[i]) + echoRight * feedback);
        echo.pushSample (1, echoLeft * feedback);
        left[i] += mix * echoLeft;
        right[i] += mix * echoRight;
    }
}

void Groovebox::writeOutput (juce::AudioBuffer<float>& output, int numSamples)
{
    const auto outputChannels = output.getNumChannels();

    if (outputChannels == 1)
    {
        output.copyFrom (0, 0, synthBuffer, 0, 0, numSamples);
        output.addFrom (0, 0, synthBuffer, 1, 0, numSamples);
        output.applyGain (0, 0, numSamples, 0.5f);
    }
    else
    {
        for (int channel = 0; channel < outputChannels; ++channel)
        {
            if (channel < 2)
                output.copyFrom (channel, 0, synthBuffer, channel, 0, numSamples);
            else
                output.clear (channel, 0, numSamples);
        }
    }

    // Final safety net after the limiter.
    for (int channel = 0; channel < outputChannels; ++channel)
        juce::FloatVectorOperations::clip (output.getWritePointer (channel), output.getReadPointer (channel), -1.0f, 1.0f,
                                           numSamples);
}

void Groovebox::process (juce::AudioBuffer<float>& output, const juce::MidiBuffer& midiIn, const HostTransport& host,
                         int numInputChannels)
{
    const auto numSamples = output.getNumSamples();
    if (numSamples == 0)
        return;

    sampler.captureInput (output, numInputChannels, numSamples);

    synthBuffer.setSize (2, numSamples, false, false, true);
    trackBuffer.setSize (2, numSamples, false, false, true);
    drumBuffer.setSize (2, numSamples, false, false, true);
    sampleBuffer.setSize (2, numSamples, false, false, true);
    for (auto& track : tracks)
    {
        track->arpInput.clear();
        track->midi.clear();
    }
    numDrumHits = 0;
    numClicks = 0;

    // Clock: follow the host when it is playing, otherwise run our own transport.
    const auto& seq = params.sequencer;
    const bool followingHost = seq.followHost && host.playing && host.hasPosition;
    const auto bpm = (seq.followHost && host.bpm > 0.0) ? host.bpm : static_cast<double> (seq.bpm);
    tempo.store (bpm);

    const auto samplesPerBeat = sampleRate * 60.0 / bpm;
    const auto samplesPerStep = samplesPerBeat / 4.0;
    stepsPerSample = 1.0 / samplesPerStep;

    bool runningNow = false;
    if (followingHost)
    {
        runningNow = true;
        blockStartPosition = host.ppqPosition * 4.0;
    }
    else
    {
        runningNow = playRequested.load();
        if (runningNow && ! wasRunning)
            internalPosition = 0.0;
        blockStartPosition = internalPosition;
    }

    if (wasRunning && ! runningNow)
    {
        for (auto& held : heldRecordings)
            held.active = false;
        stopSequencerNotes();
        currentStep.store (-1);
        songSlot.store (-1);
    }

    running.store (runningNow);
    wasRunning = runningNow;

    flushNoteOffs (numSamples);
    playPreviewNotes();

    if (const auto requestedLiveTrack = liveTrack.load(); requestedLiveTrack != activeLiveTrack)
        switchLiveTrack (requestedLiveTrack);

    // Switching an arp on or off would otherwise leave notes hanging.
    for (size_t i = 0; i < tracks.size(); ++i)
    {
        auto& track = *tracks[i];
        const auto arpEnabled = params.tracks[i].arp.enabled;
        if (arpEnabled != track.arpWasEnabled)
        {
            track.midi.addEvent (juce::MidiMessage::allNotesOff (liveChannel), 0);
            track.midi.addEvent (juce::MidiMessage::allNotesOff (sequencerChannel), 0);
            track.midi.addEvent (juce::MidiMessage::allNotesOff (Arpeggiator::outputChannel), 0);
            track.arpWasEnabled = arpEnabled;
        }
    }

    const bool recordingNow = recording.load() && runningNow;

    // Drum and sample pads tapped in the UI.
    const auto previews = pendingPreviews.exchange (0);
    for (int track = 0; track < numHitTracks; ++track)
    {
        if ((previews & (juce::uint32 (1) << static_cast<juce::uint32> (track))) == 0)
            continue;

        addDrumHit (0, track, 1.0f);
        if (recordingNow)
            recordHit (track, blockStartPosition);
    }

    // Incoming MIDI: channel 10 plays drums and samples, everything else plays the live track.
    auto& live = *tracks[static_cast<size_t> (activeLiveTrack)];
    for (const auto metadata : midiIn)
    {
        auto message = metadata.getMessage();
        const auto offset = juce::jlimit (0, numSamples - 1, metadata.samplePosition);
        const auto position = blockStartPosition + offset * stepsPerSample;

        if (message.isNoteOnOrOff() && message.getChannel() == drumChannel)
        {
            const auto note = message.getNoteNumber();
            auto track = DrumMachine::trackForMidiNote (note);
            if (note >= firstSampleNote && note < firstSampleNote + Seq::numSampleTracks)
                track = sampleTrack (note - firstSampleNote);

            if (message.isNoteOn() && track >= 0)
            {
                addDrumHit (offset, track, message.getFloatVelocity());
                if (recordingNow)
                    recordHit (track, position);
            }
            continue;
        }

        if (message.isNoteOn() && recordingNow)
            recordNote (activeLiveTrack, message.getNoteNumber(), position);
        else if (message.isNoteOff() && runningNow)
            finishRecordedNote (message.getNoteNumber(), position);

        if (message.getChannel() > 0)
            message.setChannel (liveChannel);

        live.arpInput.addEvent (message, offset);
    }

    if (runningNow)
    {
        StepClock::forEachStep (blockStartPosition, stepsPerSample, numSamples, seq.swing,
                                [&] (juce::int64 step, int offset) { handleStep (step, offset, samplesPerStep, numSamples); });

        if (! followingHost)
            internalPosition += numSamples * stepsPerSample;
    }

    // After the sequencer, so pattern notes are arpeggiated as well as live ones.
    for (auto& track : tracks)
        track->arp.process (track->arpInput, track->midi, numSamples, samplesPerBeat);

    synthBuffer.clear();
    for (auto& track : tracks)
    {
        track->synth.setTempo (bpm);
        track->synth.render (trackBuffer, track->midi);
        for (int channel = 0; channel < 2; ++channel)
            synthBuffer.addFrom (channel, 0, trackBuffer, channel, 0, numSamples);
    }

    renderHits (numSamples);

    applyEcho (numSamples, samplesPerStep);
    drumGain.applyGain (drumBuffer, numSamples);
    sampleGain.applyGain (sampleBuffer, numSamples);

    for (int channel = 0; channel < 2; ++channel)
    {
        synthBuffer.addFrom (channel, 0, drumBuffer, channel, 0, numSamples);
        synthBuffer.addFrom (channel, 0, sampleBuffer, channel, 0, numSamples);
    }

    reverb.processStereo (synthBuffer.getWritePointer (0), synthBuffer.getWritePointer (1), numSamples);
    masterGain.applyGain (synthBuffer, numSamples);
    renderClicks (numSamples); // after the effects, so the click stays dry and tight

    juce::dsp::AudioBlock<float> block (synthBuffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    limiter.process (context);

    writeOutput (output, numSamples);
    sampleClock += numSamples;
}
