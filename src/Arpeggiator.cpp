#include "Arpeggiator.h"

void Arpeggiator::setParams (const ArpParams& newParams)
{
    params = newParams;
    params.octaves = juce::jlimit (1, 3, params.octaves);
    params.division = juce::jlimit (0, static_cast<int> (Tempo::arpDivisions.size()) - 1, params.division);
}

void Arpeggiator::reset()
{
    numHeld = 0;
    soundingNote = -1;
    stepIndex = 0;
    nextTickAt = 0.0;
}

void Arpeggiator::addHeld (int note, float velocity)
{
    for (int i = 0; i < numHeld; ++i)
        if (heldNotes[static_cast<size_t> (i)] == note)
            return;

    if (numHeld == maxHeldNotes)
        return;

    // Keep held notes sorted low to high.
    int insertAt = numHeld;
    while (insertAt > 0 && heldNotes[static_cast<size_t> (insertAt - 1)] > note)
    {
        heldNotes[static_cast<size_t> (insertAt)] = heldNotes[static_cast<size_t> (insertAt - 1)];
        heldVelocities[static_cast<size_t> (insertAt)] = heldVelocities[static_cast<size_t> (insertAt - 1)];
        --insertAt;
    }

    heldNotes[static_cast<size_t> (insertAt)] = note;
    heldVelocities[static_cast<size_t> (insertAt)] = velocity;
    ++numHeld;
}

void Arpeggiator::removeHeld (int note)
{
    for (int i = 0; i < numHeld; ++i)
    {
        if (heldNotes[static_cast<size_t> (i)] != note)
            continue;

        for (int j = i; j < numHeld - 1; ++j)
        {
            heldNotes[static_cast<size_t> (j)] = heldNotes[static_cast<size_t> (j + 1)];
            heldVelocities[static_cast<size_t> (j)] = heldVelocities[static_cast<size_t> (j + 1)];
        }

        --numHeld;
        return;
    }
}

int Arpeggiator::nextNote (float& velocity)
{
    const auto length = numHeld * params.octaves;
    int index = 0;

    switch (params.mode)
    {
        case ArpMode::up:
            index = stepIndex % length;
            break;

        case ArpMode::down:
            index = length - 1 - stepIndex % length;
            break;

        case ArpMode::upDown:
        {
            // Bounce without repeating the top and bottom notes.
            const auto period = std::max (1, 2 * length - 2);
            const auto position = stepIndex % period;
            index = position < length ? position : period - position;
            break;
        }

        case ArpMode::random:
            index = random.nextInt (length);
            break;
    }

    ++stepIndex;

    const auto held = static_cast<size_t> (index % numHeld);
    velocity = heldVelocities[held];
    return juce::jlimit (0, 127, heldNotes[held] + 12 * (index / numHeld));
}

void Arpeggiator::stopSounding (juce::MidiBuffer& output, int position)
{
    if (soundingNote >= 0)
    {
        output.addEvent (juce::MidiMessage::noteOff (outputChannel, soundingNote), position);
        soundingNote = -1;
    }
}

void Arpeggiator::tick (juce::MidiBuffer& output, int position)
{
    stopSounding (output, position);

    float velocity = 1.0f;
    soundingNote = nextNote (velocity);
    output.addEvent (juce::MidiMessage::noteOn (outputChannel, soundingNote, velocity), position);
    noteOffAt = position + params.gate * tickInterval;
}

void Arpeggiator::advanceTo (juce::MidiBuffer& output, double position)
{
    for (;;)
    {
        const bool offDue = soundingNote >= 0 && noteOffAt < position && noteOffAt <= nextTickAt;
        const bool tickDue = numHeld > 0 && nextTickAt < position;

        if (offDue)
        {
            stopSounding (output, static_cast<int> (noteOffAt));
        }
        else if (tickDue)
        {
            tick (output, static_cast<int> (nextTickAt));
            nextTickAt += tickInterval;
        }
        else if (soundingNote >= 0 && noteOffAt < position)
        {
            stopSounding (output, static_cast<int> (noteOffAt));
        }
        else
        {
            return;
        }
    }
}

void Arpeggiator::process (const juce::MidiBuffer& input, juce::MidiBuffer& output, int numSamples, double samplesPerBeat)
{
    tickInterval = std::max (1.0, samplesPerBeat * Tempo::arpDivisions[static_cast<size_t> (params.division)].beats);

    for (const auto metadata : input)
    {
        const auto message = metadata.getMessage();
        const auto position = metadata.samplePosition;

        if (! params.enabled)
        {
            // Keep track of held keys so turning the arp on mid-chord works straight away.
            if (message.isNoteOn())
                addHeld (message.getNoteNumber(), message.getFloatVelocity());
            else if (message.isNoteOff())
                removeHeld (message.getNoteNumber());

            output.addEvent (message, position);
            continue;
        }

        advanceTo (output, position);

        if (message.isNoteOn())
        {
            const bool wasEmpty = numHeld == 0;
            addHeld (message.getNoteNumber(), message.getFloatVelocity());

            // Start straight away on the first key rather than waiting for the next tick.
            if (wasEmpty)
            {
                stepIndex = 0;
                nextTickAt = position;
            }
        }
        else if (message.isNoteOff())
        {
            removeHeld (message.getNoteNumber());
            if (numHeld == 0)
                stopSounding (output, position);
        }
        else
        {
            output.addEvent (message, position);
        }
    }

    if (! params.enabled)
    {
        stopSounding (output, 0);
        return;
    }

    advanceTo (output, numSamples);

    nextTickAt = std::max (0.0, nextTickAt - numSamples);
    noteOffAt -= numSamples;
}
