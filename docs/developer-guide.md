# Developer guide

How the code is put together, and what to change when adding things. For what the instrument does from a player's point of view, see the [user guide](user-guide.md).

## Building and testing

Needs CMake and the Xcode Command Line Tools. The first configure downloads JUCE 9.0.6 with FetchContent. Builds are universal (arm64 and x86_64); check with `lipo -archs` on the binary inside a bundle.

```sh
cmake -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

CMake builds four things:

| Target | What it is |
| --- | --- |
| `uBeat_AU`, `uBeat_VST3`, `uBeat_Standalone` | The plugin in each format, from the shared `uBeat` code. AU type `aumu`, manufacturer `Ubea`, subtype `Ubt1`. Files and targets use the ASCII name `uBeat`; hosts show `PLUGIN_NAME`, `μBeat`. |
| `uBeatTests` | Console app running every `juce::UnitTest` in `tests/`. Prints `ALL TESTS PASSED` or exits with 1. Registered with CTest. |
| `UiSnapshot` | Renders each tab of the editor to a PNG without opening a window: `build/UiSnapshot_artefacts/Release/UiSnapshot <folder>`. The screenshots in `docs/images` come from it. |

`ENGINE_SOURCES` in `CMakeLists.txt` lists the files that have no UI or plugin dependencies. The tests build only those, linked against `juce_audio_basics`, `juce_data_structures` and `juce_dsp`, so engine code must not include plugin or GUI modules.

## Making a release

1. Set the version in `project(uBeat VERSION ...)` in `CMakeLists.txt`.
2. Move the `[Unreleased]` entries in `CHANGELOG.md` under a new version heading with the date.
3. Build in a clean folder, run the tests, and check every binary is universal:

   ```sh
   cmake -B build-release -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
   cmake --build build-release -j8
   ctest --test-dir build-release --output-on-failure
   ```

4. Commit, tag `v<version>` and push the tag.
5. Zip each format with `ditto -c -k --keepParent <bundle> uBeat-<version>-macOS-<App|AU|VST3>.zip`, and attach the zips to a GitHub release for the tag (`gh release create`).

## Source layout

| File | Responsibility |
| --- | --- |
| `Groovebox.h/.cpp` | The whole instrument for one audio block: clock, sequencer, synth tracks, drums, sampler, metronome, effects and limiter. |
| `PatternBank.h/.cpp` | Every pattern and the song chain, stored as atomic bitmasks. Saves to and loads from a `ValueTree`. |
| `SynthTracks.h` | The synth tracks: name, Dice style, default preset, octave and note length, and the octave range. |
| `SynthEngine.h/.cpp`, `Lfo.h` | One polyphonic synth (8 voices, PolyBLEP oscillators, ADSR, TPT state-variable filter, 2 LFOs). Each synth track owns one. |
| `Arpeggiator.h/.cpp` | Turns held notes into an arpeggio. Each synth track owns one. |
| `DrumMachine.h/.cpp` | The 8 synthesised drums. |
| `Sampler.h/.cpp` | The 4 sample slots: recording from the input, playback, tidying and saving. |
| `Metronome.h` | The click. |
| `StepClock.h` | Finds which steps start inside a block, with swing. |
| `Scales.h` | Scales and the mapping from grid rows to MIDI notes. |
| `Tempo.h` | Note-length divisions for synced LFOs and the arpeggiator. |
| `PatternGenerator.h/.cpp` | Dice for drums and melodies, and the demo pattern. |
| `ParamIds.h` | Every parameter ID. |
| `Presets.h/.cpp` | The synth sound presets. |
| `PluginProcessor.h/.cpp` | The JUCE `AudioProcessor`: parameter layout, reading parameters for the audio thread, saving and loading state, loading sample files. |
| `StandaloneApp.cpp` | The standalone app: JUCE's standard app and window, with an Options menu that saves and opens `.ubeat` files. Built because `JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP` is set. |
| `PluginEditor.h/.cpp` | The window: top bar, tabs, song bar, keyboard, undo, and a 30 Hz timer that refreshes the pages. |
| `ui/Pages.h/.cpp` | One page per tab: Drums, Samples, Melody, Sound and Mix. |
| `ui/Widgets.h/.cpp` | Knobs, choices, toggles, the step grid, track bar, song bar, pads, keyboard and transport buttons. |
| `ui/NeonLookAndFeel.h/.cpp`, `ui/Theme.h` | Colours, fonts and the neon drawing style. |

## How a block is processed

`SynthAudioProcessor::processBlock` merges the on-screen keyboard's MIDI into the incoming MIDI, reads the host's transport, copies every parameter into a `GrooveParams` struct, and calls `Groovebox::process`. `Groovebox::process` then:

1. Copies the audio input (the first input channels of the buffer) into the sampler, which keeps it if a recording is running.
2. Works out the clock. If Follow Host is on and the host is playing, the position comes from the host (`ppqPosition * 4` steps). Otherwise the internal transport runs from the Play button.
3. Sends any note-offs that fall in this block, and plays notes previewed from the grid.
4. If the editor selected a different synth track, releases the notes held on the old one.
5. Turns pad taps and incoming MIDI into drum hits, sample hits and live notes. MIDI on channel 10 goes to the drums (notes 60 to 63 to the sample slots); anything else goes to the selected track. If recording, these are written into the pattern.
6. Calls `handleStep` for every step that starts in the block, which adds drum and sample hits, metronome clicks, and each track's notes. A drum, sample slot or synth track is skipped if it is muted, or if anything is soloed and it isn't.
7. Runs each track's arpeggiator over its live and pattern notes (`SynthTrack::arpInput`). With the arp off, notes pass straight through to the synth.
8. Renders each synth track and sums them, then renders drums and samples with each hit at its exact sample offset.
9. Applies the echo to the synth tracks, adds drums and samples, applies reverb and master gain, adds the metronome click, then the limiter (-1 dB) and a hard clip at ±1.

The plugin never sends its input to its output.

### Timing

Positions are measured in steps, where a step is a 16th note. `StepClock::forEachStep` finds every step whose start time falls in the block. Swing delays odd steps by up to half a step. The metronome counts beats from the start of the song (every fourth step), so in Logic it lines up with Logic's bars.

### Internal MIDI channels

Each synth track gets its own MIDI buffer. Channels inside that buffer say where a note came from, so one source can be silenced without the others:

| Channel | Source |
| --- | --- |
| 1 | Live playing (keyboard or host) |
| 2 | The sequencer |
| 3 | The arpeggiator's output |
| 4 | Notes previewed from the grid |

### Note lengths

A melody note on one cell lasts for the track's Note Length (0.1 to 16 steps). A note drawn over several cells lasts exactly that many steps, cut off at the end of the pattern. If a note is triggered again while still held from an earlier step, `playSequencerNote` ends the old one at that moment, so its late note-off cannot cut the new note short.

When recording, a note-on writes a one-step note and remembers it in `heldRecordings`. Its note-off sets the length, rounded to whole steps.

## Threads

The audio thread must never wait for the UI. Everything the two share is built for that:

- **Parameters** live in an `AudioProcessorValueTreeState`. The processor caches a `std::atomic<float>*` for every parameter in `RawParams`, so the audio thread never looks a parameter up by name.
- **Patterns** are atomic `uint32` bitmasks in `PatternBank`, one bit per step for drums and samples, and one bit per row for melody note starts and held steps. The UI writes cells directly while the audio thread plays them.
- **Pad taps** set bits in an atomic mask (`pendingPreviews`). **Grid previews** go through a lock-free FIFO. The **selected track** is an atomic int.
- **Samples** are swapped in under a spin lock that the audio thread only try-locks. If the message thread holds it for the moment of a swap, that block skips the sampler rather than waiting. Recording writes into a buffer allocated in `prepare`. The message thread collects a finished recording from the editor's timer (`Sampler::collectRecording`), tidies it, and swaps it in.
- **UI feedback** (the playhead, which drums just hit, the mic level) goes the other way through atomics that the editor's 30 Hz timer reads.

## Parameters

All IDs are in `ParamIds.h`.

- Synth track parameters are named `synth<track>_<base>`, for example `synth2_cutoff`, built with `ParamIds::synth (track, base)`. The base names are the voice settings, the LFO fields (`lfo1Shape` and so on), the arpeggiator settings, `trackOctave`, `noteLength`, `mute` and `solo`.
- Drum parameters are `drum<n><Field>`, and sample slot parameters are `sample<n><Field>`.
- A track's defaults come from its default preset in `SynthTracks.h`. The values a parameter has with no preset (the Init sound) are recorded in `initValues` while the layout is built; applying a preset starts from those, then sets the preset's values.
- The octave choice runs from `SynthTracks::minOctave` (-1) to `maxOctave` (7), Logic numbering, where middle C (MIDI 60) is C3. The grid's bottom row is `12 * (octave + 2) + key`.

Sessions store choice parameters by index. If a choice's options change meaning, give the parameter a new ID so older sessions use the default instead of landing on the wrong option.

## Saved state

`getStateInformation` writes the parameter tree (type `Params`) as XML, with two extra children:

- `Patterns`, from `PatternBank::toValueTree`. Properties: `selected`, `songMode`, `song` (16 comma-separated pattern indices, -1 for empty). One `Pattern` child per pattern with comma-separated hex masks: `drums` (one per drum), `samples` (one per slot), and `melody1` to `melody5` and `hold1` to `hold5` (one per step, a bit per row, for note starts and held steps). A pattern with a single `melody` property and no `melody1`, from before there were several synth tracks, loads that melody into the lead track.
- `Samples`, from `Sampler::toValueTree`. One `Sample` child per filled slot with `slot`, `rate` (the sample's own sample rate) and `pcm16` (16-bit mono PCM, base64).

The standalone app saves state when it quits. A `.ubeat` song file holds exactly the bytes `getStateInformation` writes. A new processor loads the demo pattern; a saved session replaces it.

## Adding things

- **A synth parameter:** add the base name to `ParamIds.h`, add it in the track loop in `createParameterLayout`, cache it in `RawParams::Track`, copy it in `readParams`, and add it to `soundParameters()` if presets should reset it. Then show it on the Sound page and rebind it in `SoundPage::showTrack`.
- **A preset:** add it to `soundPresets()` in `Presets.cpp`. Values are in real units, and choices are given by index.
- **A synth track:** add an entry to `SynthTracks::tracks`, raise `Seq::numSynthTracks`, add a colour to `Theme::synthColour`, and give it a Dice style in `PatternGenerator.cpp` if the existing ones don't fit. Add new tracks at the end: track parameter IDs and saved melodies are numbered by position.
- **A test:** add a `juce::UnitTest` subclass with a static instance in a file under `tests/`, and list the file in the `uBeatTests` sources in `CMakeLists.txt`.

## Known gaps

- The AU passes `auval`, but has not been tried in a Logic session.
- Release builds are ad-hoc signed and not notarised, because there is no Developer ID certificate. Users have to clear the quarantine flag (see the user guide's Installing section).
- Recording samples from inside Logic is untested. The plugin has an audio input, but how Logic routes a microphone into an instrument has not been checked.
- The standalone app mutes its input by default to avoid feedback. The editor turns that off on opening (`unmuteStandaloneInput`); this builds but has not been confirmed on screen.
- Undo covers Dice, Clear and Copy only. Single grid edits and clearing a sample slot cannot be undone.
