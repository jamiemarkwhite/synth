# μBeat

μBeat (`uBeat` in file and target names) is a groovebox instrument plugin (AU, VST3, standalone) built with JUCE and CMake: five synth tracks, an 808-style drum machine, a mic sampler and a 32-step sequencer. It is made for children to make songs with, so the UI favours big, obvious controls.

## Docs

- [README.md](README.md): what it is, and how to build, run, test and install in Logic.
- [docs/user-guide.md](docs/user-guide.md): every feature, from a player's point of view.
- [docs/developer-guide.md](docs/developer-guide.md): source layout, how a block is processed, threads, parameters, saved state, how to add things, and known gaps.
- [CHANGELOG.md](CHANGELOG.md): user-visible changes.
- [CONTRIBUTING.md](CONTRIBUTING.md): checks and rules for pull requests.

## Working on it

- Build with `cmake --build build -j8`, test with `ctest --test-dir build --output-on-failure`. Run the tests after every change.
- Check UI changes with `build/UiSnapshot_artefacts/Release/UiSnapshot <folder>` and look at the PNGs.
- Engine code (the files in `ENGINE_SOURCES`) must not depend on GUI or plugin modules; the tests build it on its own.
- The audio thread must never lock or allocate. Follow the patterns in the developer guide's Threads section.
- Add new synth tracks, drums or sample slots at the end of their lists: parameter IDs and saved patterns are numbered by position.
- When a change alters what a user sees or does, update the user guide and add a line under `[Unreleased]` in the changelog. When it changes structure, threads, parameters or saved state, update the developer guide.
