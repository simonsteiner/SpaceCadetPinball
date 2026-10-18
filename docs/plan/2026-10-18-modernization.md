# Modernization plan

Date: 2026-10-18. Status: Phase 0 done; Phase 1 not started.

The goal is to bring this fork of k4zmu2a/SpaceCadetPinball up to a current toolchain and a better presentation without changing how the game plays. Phases 1, 2 and 4 are the recommended path. Phase 3 stays parked unless there is a specific reason for it.

## Starting point

- About 83k lines of C++, of which about 27k are game code; the rest is vendored ImGui and embedded data. Targets C++11, CMake 3.0, SDL2 and SDL2_mixer, and ImGui 1.89-era (with both the SDL renderer backend and the older custom `imgui_sdl.cpp`).
- Rendering is software: sprites from the DAT file are composed with their depth maps into an 8-bit palettised frame, uploaded to one SDL texture and scaled with nearest-neighbour filtering. The native table area is 600×416.
- Physics and drawing already run at separate rates (`UpdatesPerSecond` and `FramesPerSecond` in `options.cpp`).
- Windows needs SDL copied into `Libs/` by hand. macOS release builds download SDL frameworks. CI is one manually triggered macOS job that checks out `master` on the retired `macos-12` runner.
- There are no automated tests.
- Branches: `master` mirrors upstream; all fork work lands on `main` through pull requests.

## Constraint: game data stays out of the repository

The DAT, sound and music files are Microsoft's. They are never committed, never uploaded as CI artifacts and never hosted. Anything that needs them (replay tests, screenshots, play-testing) runs locally against a copy outside the repository. CI only builds and runs tests that need no game data.

## Phase 0: baseline and hygiene

1. Done: the docs are restructured into one home per kind of doc, plus `AGENTS.md` and `GLOSSARY.md`.
2. Done: `.gitignore` excludes the game data in any letter case: `*.DAT`, `*.WAV`, `*.MID`, `*.EXE`, `*.7z`, `table.bmp` and `wavemix.inf`.
3. Done: `main` is the fork's default branch on GitHub.
4. Done: a local Linux build (SDL2 dev packages, Ninja) and the original game extracted from `../assets/3DPinballforWindows.7z` to `../gamedata/`. The archive's `Pinball.exe` uses the BCJ2 filter, which `py7zr` can't decode. Use 7-Zip or libarchive (`bsdtar`).
5. Done: the unchanged code builds with GCC 15 and CMake 4.2 (one CMake deprecation warning, 23 `-Wbidi-chars` warnings in `translations.cpp`, no errors) and runs under WSLg. The startup log, screenshots, a 25-second demo session and a summary are in `../baseline/`.
6. Done: the archive's `Pinball.exe` matches the SHA-1 in `README.md` (`2A5B525E…0D05`), so it is the reference binary for REA comparisons.

## Phase 1: codebase modernization

Gameplay must stay byte-for-byte equivalent. Each step is its own pull request.

1. **Build:** CMake 3.24+ with `CMakePresets.json` (Linux, Windows MSVC, MinGW cross, macOS), SDL fetched with `FetchContent` (or vcpkg in manifest mode) so no hand-copied `Libs/`. This replaces `CMakeSettings.json` and most of `BuildForWindows.ps1`.
2. **CI:** a pull request workflow that builds the presets on Linux, Windows and macOS with warnings enabled. Fix `ReleaseBuilds.yml` (current macOS runner, check out the triggering ref instead of `master`).
3. **Determinism harness:** record inputs and the random seed for a session, replay them headless at a fixed update rate and compare the score and ball state per frame. Run locally with game data before and after every behaviour-sensitive change. A synthetic DAT for CI can come later.
4. **C++20:** raise the standard, fix new warnings, and enable AddressSanitizer and UBSan in a debug preset. Run the harness under sanitizers.
5. **SDL3:** port window, renderer, input, audio and timing to SDL3 and SDL3_mixer, and move ImGui to a current release with its SDL3 backends. Drop `imgui_sdl.cpp`. Check SDL3_mixer's MIDI support on every platform before starting.
6. **Tooling:** `.clang-format` matching the current style, applied once in its own commit, and `clang-tidy` in CI on changed files.

Done when all presets build in CI and the determinism harness passes against the Phase 0 baseline.

## Phase 2: modern presentation

1. Integer scaling with an optional "fit" mode, HiDPI-correct window sizing, and proper full screen on modern displays.
2. Widescreen layout: use the space beside the table for the score panel or artwork instead of black bars.
3. Optional post-processing (CRT, xBR-style upscaling) using SDL3's GPU API, off by default.
4. Gamepad support through the SDL3 gamepad API, including rumble on flipper and bumper hits. Touch controls for tablets.
5. High-refresh rendering: decide whether to interpolate ball and flipper positions between physics updates or simply raise both rates. Interpolation must not change the physics.

Truly high-resolution art does not exist. Higher-resolution sprites (redrawn or upscaled) would also need matching depth maps, so they are out of scope unless chosen explicitly.

## Phase 3 (parked): rewrite in a new stack

Porting the logic to TypeScript/WebGL or Rust would re-create what the decompilation already gives, with a high risk of subtle physics drift. Reconsider only for a concrete goal the C++ code cannot meet. If it happens, REA (`../rea`) is the tool for checking behaviour against the reference `pinball.exe`.

## Phase 4: web build

1. Emscripten preset on top of the SDL3 port.
2. The player supplies the game data through a file picker. It is kept in IndexedDB and never uploaded.
3. Publish the build without data to GitHub Pages from CI.

Prior art: alula's port (<https://github.com/alula/SpaceCadetPinball>, played at <https://pinball.alula.me>) is five commits on a September 2021 upstream, so only its approach carries over, not its code:

- **Main loop:** the `while` loop in `winmain` becomes a callback for `emscripten_set_main_loop_arg`. Full screen and the Exit menu item are disabled.
- **Music:** Emscripten's SDL_mixer has no MIDI backend. The port renders `PINBALL.MID` with TinySoundFont (`tsf.h`/`tml.h`, MIT) through `Mix_HookMusic`. Its embedded `gm.sf2` is a conversion of the Roland/Microsoft GS Wavetable set and must not be copied; use a freely licensed SoundFont, ideally fetched separately from the main bundle. A smaller option is to render the theme to OGG once on the player's side.
- **Audio unlock:** music is on by default here too, and the browser starts audio only after a user gesture.
- **Game data:** it preloads the game files into the bundle and hosts them. Point 2 above exists to avoid exactly that.

## Role of REA

REA is not a dependency of the game. Use it to settle questions about the original's behaviour: a suspected decompilation bug, a physics quirk the harness reveals, or Full Tilt-only features (an open item in upstream's plans).

## Upstream sync

`master` follows `upstream/master`. Merge it into `main` when upstream changes. After Phase 1, merges will conflict; accept that, and send self-contained fixes upstream as separate small pull requests where they still apply.

## Open decisions

- vcpkg or `FetchContent` for dependencies.
- Whether to keep the Windows XP (`v141_xp`) and 32-bit builds.
- Whether Phase 2 includes upscaled art.
- Whether a native Android or iOS target matters, which would affect the Phase 2 input work.
