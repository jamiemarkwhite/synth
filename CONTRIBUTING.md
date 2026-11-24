# Contributing

Bug reports, ideas and pull requests are welcome. μBeat is for children, so new controls should be big, clearly labelled and hard to get wrong.

## Before opening a pull request

1. Build and run the tests:

   ```sh
   cmake --build build -j8
   ctest --test-dir build --output-on-failure
   ```

2. If you changed the UI, render every tab and look at the results:

   ```sh
   build/UiSnapshot_artefacts/Release/UiSnapshot /tmp/ubeat-snapshots
   ```

   If a tab shown in `docs/images` changed, render into that folder instead to refresh the screenshots.

3. Update the docs:
   - Add a line under `[Unreleased]` in [CHANGELOG.md](CHANGELOG.md) for anything a player would notice.
   - Update the [user guide](docs/user-guide.md) if what a player sees or does has changed.
   - Update the [developer guide](docs/developer-guide.md) if structure, threads, parameters or saved state have changed.

## Rules for the code

- Engine code (the files in `ENGINE_SOURCES` in `CMakeLists.txt`) must not use GUI or plugin modules, because the tests build it on its own.
- The audio thread must never lock or allocate. The developer guide's [Threads](docs/developer-guide.md#threads) section shows how the audio thread and UI share data.
- Add new synth tracks, drums and sample slots at the end of their lists. Parameter IDs and saved patterns are numbered by position, so inserting in the middle breaks saved sessions.
- Write tests for new engine behaviour in `tests/`.

## Licence

By contributing, you agree that your contributions are released under the [AGPLv3](LICENSE).
