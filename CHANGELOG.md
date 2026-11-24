# Changelog

All notable changes to this project are listed here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- **Start Fresh** button: after asking, empties every pattern, the song and the samples, and puts every sound and setting back to how it starts.

## [0.1.0] - 2026-11-24

First release. Universal (Apple silicon and Intel) macOS builds of the app, AU and VST3.

### Added

- μBeat: a JUCE plugin in AU, VST3 and standalone formats, built with CMake. The AU is an instrument (`aumu Ubt1 Ubea`) so Logic can load it.
- Neon user interface that scales with the window, with an on-screen keyboard and computer keyboard playing.
- **Synth tracks:** five synths playing at once (Bass, Strings, Lead, Keys, Sub Bass), each with its own sound, arpeggiator, octave, note length, mute, volume and melody grid.
  - Synth voice: sine, saw or square oscillator, 8 voices, ADSR, low-pass filter with resonance.
  - Two LFOs per track to pitch, cutoff or amp, with five shapes, tempo sync and retrigger.
  - Arpeggiator per track, for played notes and pattern notes: up, down, up and down, or random, over 1 to 3 octaves, at 1/4 to 1/32, with gate.
  - Eleven presets: Init, Fat Bass, Pluck, Dreamy Pad, Wobble Bass, Chip Lead, Arp Sparkle, Laser, Strings, Sub Bass, Tremolo Keys.
  - Octave from -1 to 7, numbered as in Logic.
- **Melody grids:** 16 rows by 32 steps per track. No Scale (a semitone per row) by default, or one of six scales shared by all tracks, with a shared key. Notes can be drawn over several steps, resized by dragging and removed by clicking.
- **Drum machine:** eight synthesised 808-style drums (kick, snare, clap, closed and open hat, low and high tom, cowbell) with level, tune, decay and mute per drum. The closed hat chokes the open hat.
- **Sampler:** four slots recorded from the microphone (up to 5 seconds, trimmed and normalised) or loaded from audio files, with level, tune (±24 semitones), reverse and mute, each with its own step row.
- **Sequencer:** 32 steps with lengths of 8, 16, 24 or 32, swing, four patterns (A to D), copy to the next pattern, and a 16-slot song chain.
- **Recording:** pad taps and played notes are written into the pattern on the nearest step, without playing twice. Held keys record their length.
- **Dice:** random drums, and a part for each synth track in its own style (held chords, a bass and a held sub bass that follow them, a tune, off-beat stabs). With No Scale it uses minor pentatonic notes. One-level undo and redo for Dice, Clear and Copy.
- **Solo:** an S button on every drum, sample slot and synth track. While any is lit, only soloed parts play from the pattern. Mute wins over solo, and live playing is not affected.
- **Metronome:** a click on every beat while playing, higher on the first beat of the bar, toggled from the top bar.
- **Mix:** volume per synth track, drums and samples, a ping-pong echo on the synths, reverb, master volume, and a limiter on the output.
- **Logic integration:** follows Logic's tempo and transport, plays drums from MIDI channel 10 (General MIDI notes) and sample slots from notes 60 to 63, and plays the selected synth track from other channels. Everything is saved with the project.
- Demo pattern on first launch: drums, bass, held chords and sub bass, and a lead tune.
- Unit tests for the engine (`uBeatTests`, run by CTest) and a tool that renders each tab to PNG (`UiSnapshot`).
- [User guide](docs/user-guide.md), [developer guide](docs/developer-guide.md) and [contributing guide](CONTRIBUTING.md), with screenshots.
- Released under the AGPLv3.

[Unreleased]: https://github.com/jamiemarkwhite/synth/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/jamiemarkwhite/synth/releases/tag/v0.1.0
