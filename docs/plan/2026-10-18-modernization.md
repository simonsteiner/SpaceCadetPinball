# Modernization plan

Date: 2026-10-18. Status: Phase 0 done; Phase 1 steps 1 to 3 done.

The goal is to bring this fork of k4zmu2a/SpaceCadetPinball up to a current toolchain and a better presentation without changing how the game plays. Phases 1, 2 and 4 are the recommended path. Phase 3 stays parked unless there is a specific reason for it.

## Starting point

- About 83k lines of C++, of which about 27k are game code; the rest is vendored ImGui and embedded data. Targets C++11, CMake 3.0, SDL2 and SDL2_mixer, and ImGui 1.89-era (with both the SDL renderer backend and the older custom `imgui_sdl.cpp`).
- Rendering is software: sprites from the DAT file are composed with their depth maps into an 8-bit palettised frame, uploaded to one SDL texture and scaled with nearest-neighbour filtering. The native table area is 600×416.
- Physics and drawing already run at separate rates (`UpdatesPerSecond` and `FramesPerSecond` in `options.cpp`).
- Windows needed SDL copied into `Libs/` by hand, and macOS release builds downloaded SDL frameworks (both replaced in Phase 1 step 1). CI was one manually triggered macOS job that checked out `master` on the retired `macos-12` runner (replaced in Phase 1 step 2).
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

1. Done: **Build.** CMake 3.24+ with `CMakePresets.json` (`linux`, `linux-debug`, `linux-vendored`, `windows-x64`, `windows-x86`, `windows-x86-xp`, `mingw`, `macos`). With `SCP_VENDORED_SDL=ON`, the default on Windows and macOS, SDL2 2.32.10 and SDL2_mixer 2.8.1 come from `FetchContent` with pinned hashes and are linked statically. Linux keeps the system packages by default for distribution packagers. `CMakeSettings.json` is gone; `BuildForWindows.ps1` and `build-mac-app.sh` just build presets, and `Libs/` is no longer used.
   - Verified locally (GCC 15.2, CMake 4.2.3): `linux` and `linux-vendored` build and run the demo against the Phase 0 game data. The `mingw` preset's SDL and game code also built and linked with llvm-mingw (clang, libc++) instead of the GCC MinGW it names, but only after force-including `<functional>` and `<new>`, which `control.cpp` and `winmain.cpp` use without including. The release scripts and the `windows-*` and `macos` presets are untested; step 2 must build them. Apple's libc++ may hit the same missing includes.
   - Warnings: the CMake deprecation warning is gone. Plain `cmake -S . -B build` still gives the baseline's 23 `-Wbidi-chars` warnings. The `linux` preset is a Release build, which adds 11 `-Wunused-result` warnings on `fread` calls (glibc fortify only checks at `-O1` and up). `linux-vendored` adds one more warning inside SDL itself.
   - The demo is not reproducible between runs: with the same binary, it sometimes plays and sometimes waits at "Awaiting Deployment" for over 20 seconds. Untouched `main` behaves the same way. Step 3 needs to account for this.
2. Done: **CI.** `.github/workflows/Build.yml` runs on every pull request, on pushes to `main` and on demand. It builds `linux`, `linux-vendored` and `mingw` on Ubuntu 24.04, `windows-x64` and `windows-x86` on `windows-2022`, and `macos` on `macos-26`, each with the new `SCP_WARNINGS=ON` option (`-Wall -Wextra`, `/W4` with MSVC, for the game target only). All six pass in about two minutes each. `ReleaseBuilds.yml` now runs on `macos-26` and builds the triggering ref. Actions are pinned by SHA, the token is read-only and no game data is involved.
   - `control.h` and `winmain.cpp` now include `<functional>` and `<new>` themselves, instead of relying on libstdc++ to pull them in. The `macos` preset builds with Apple's libc++ without any force-includes. CI's `mingw` build uses GCC and libstdc++, so the llvm-mingw build from step 1 hasn't been retried.
   - `windows-x86-xp` isn't built: no runner image has the `v141_xp` toolset, and installing it on every run would be slow. This stays open together with the decision on keeping XP.
   - Warnings don't fail the build yet. With `SCP_WARNINGS=ON`, the game code gets about 165 warnings with GCC, 161 with MSVC and about 610 with Apple Clang (a universal build compiles everything twice). Most are unused parameters (`-Wunused-parameter`, MSVC C4100); the rest are the bidi and `fread` warnings from step 1 and a handful of unused variables, conversions and one unreachable-code warning. Step 4 fixes them and makes warnings errors.
   - `windows-latest` already has Visual Studio 2026, so the workflow runs the `windows-*` presets, which use the Visual Studio 2022 generator, on `windows-2022`. Retarget the presets when that image is retired.
3. Done: **Determinism harness.** `-record <file>` writes a replay: each frame's time step and the score and ball state after it, plus every command that reaches the table from outside (input after key binding, new game, pause, cheats and so on). `-replay <file>` plays it back headless and stops at the first frame that differs. `tools/check-replays.sh` checks a folder of replays. Details and the format are in `docs/replays.md`.
   - Replays record the actual time step of every frame instead of forcing a fixed rate, so recording doesn't change how the game plays. While recording, the only differences are the file output and a call to `srand(1)`, which is the seed the C library starts with anyway. Playback takes the same steps from the file.
   - This explains the demo variation from step 1: physics advances by the wall-clock time each frame took, so no two runs take the same steps. The random numbers are the same every run, because `rand()` is never seeded.
   - To make input replayable, `pb::InputDown` and `pb::InputUp` were split. Their table half is now `pb::GameInputDown` and `pb::GameInputUp`, which take already-resolved bindings. Normal play runs the same code in the same order.
   - Verified with GCC 15 against the Phase 0 data: a 30-second demo, a 40-second game with flippers, plunger, nudge, pause and new game sent through XTest, and a cheat session (`hidden test`, extra balls, the random score cheat) all match in every frame (3563, 5662 and 3196 frames). The Debug build matches recordings from the Release build. Deleting the plunger press from a recording makes playback fail at the frame where the ball should appear. These three recordings are in `../replays/`.
   - Not tested: two or more players, Full Tilt data, and replays across compilers. Floating-point results may differ between GCC, MSVC and Apple Clang, so compare recordings only with builds from the same compiler.
   - A synthetic DAT that would let CI play replays can come later.
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

- Decided: `FetchContent` for dependencies, not vcpkg (universal macOS builds, no extra tool, SDL3 and Emscripten are a tag change).
- Whether to keep the Windows XP (`v141_xp`) and 32-bit builds.
- Whether Phase 2 includes upscaled art.
- Whether a native Android or iOS target matters, which would affect the Phase 2 input work.
