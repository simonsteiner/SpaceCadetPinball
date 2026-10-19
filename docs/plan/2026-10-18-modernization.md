# Modernization plan

Date: 2026-10-18. Status: Phase 0 done; Phase 1 steps 1 to 5 done. Decisions of 2026-10-19 are under [Open decisions](#open-decisions).

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
   - `windows-x86-xp` was never built: no runner image has the `v141_xp` toolset. The 32-bit and XP presets were removed on 2026-10-19, so CI now builds five presets.
   - Warnings don't fail the build yet. With `SCP_WARNINGS=ON`, the game code gets about 165 warnings with GCC, 161 with MSVC and about 470 with Apple Clang (a universal build compiles everything twice). Most are unused parameters (`-Wunused-parameter`, MSVC C4100); the rest are the bidi and `fread` warnings from step 1 and a handful of unused variables, conversions and one unreachable-code warning. Step 4 fixes them and makes warnings errors.
   - `windows-latest` already has Visual Studio 2026, so the workflow runs the `windows-*` presets, which use the Visual Studio 2022 generator, on `windows-2022`. Retarget the presets when that image is retired.
3. Done: **Determinism harness.** `-record <file>` writes a replay: each frame's time step and the score and ball state after it, plus every command that reaches the table from outside (input after key binding, new game, pause, cheats and so on). `-replay <file>` plays it back headless and stops at the first frame that differs. `tools/check-replays.sh` checks a folder of replays. Details and the format are in `docs/replays.md`.
   - Replays record the actual time step of every frame instead of forcing a fixed rate, so recording doesn't change how the game plays. While recording, the only differences are the file output and a call to `srand(1)`, which is the seed the C library starts with anyway. Playback takes the same steps from the file.
   - This explains the demo variation from step 1: physics advances by the wall-clock time each frame took, so no two runs take the same steps. The random numbers are the same every run, because `rand()` is never seeded.
   - To make input replayable, `pb::InputDown` and `pb::InputUp` were split. Their table half is now `pb::GameInputDown` and `pb::GameInputUp`, which take already-resolved bindings. Normal play runs the same code in the same order.
   - Verified with GCC 15 against the Phase 0 data: a 30-second demo, a 40-second game with flippers, plunger, nudge, pause and new game sent through XTest, and a cheat session (`hidden test`, extra balls, the random score cheat) all match in every frame (3563, 5662 and 3196 frames). The Debug build matches recordings from the Release build. Deleting the plunger press from a recording makes playback fail at the frame where the ball should appear. These three recordings are in `../replays/`.
   - Not tested: two or more players, Full Tilt data, and replays across compilers. Floating-point results may differ between GCC, MSVC and Apple Clang, so compare recordings only with builds from the same compiler.
   - A synthetic DAT that would let CI play replays can come later.
4. Done: **C++20.** The game builds as C++20 (`CMAKE_CXX_STANDARD 20`, no compiler extensions). The switch itself brought no new warnings with GCC 15, and the code doesn't use C++20 features yet. `SCP_WARNINGS=ON` now makes warnings errors (CMake's `COMPILE_WARNING_AS_ERROR`), so every CI build is warning-free. The new `linux-sanitize` preset (`SCP_SANITIZERS=ON`) adds AddressSanitizer and UBSan.
   - Unused parameters are switched off (`-Wno-unused-parameter`, MSVC `/wd4100`) instead of fixed. They were 149 of the 189 warnings in a GCC Release build, nearly all in the rule handlers in `control.cpp`, timer callbacks and overrides such as `Message` and `Collision`, whose signatures are fixed. Renaming them would touch about 40 files for no gain and make upstream merges worse.
   - The rest is fixed in the code. Variables read only in `assertm` are `[[maybe_unused]]`, so the calls stay. Narrowing conversions that MSVC flagged are explicit casts with the same result. `sprintf` and an unterminated `strncpy` became `snprintf`. The bidi marks in `translations.cpp` are `\u200E` and `\u200F` escapes. The debug overlay's one-pass loop over the balls became a check for the first ball.
   - Short reads are now errors. `partman::load_records` rejects a truncated DAT file instead of loading uninitialised data; a partly read entry is deleted, not passed to `GroupData::AddEntry`, which would parse it. The sound loader and the MDS converter check their reads too. Complete files load exactly as before.
   - MSVC now gets `/utf-8`. Without a BOM it read `translations.cpp` in the system code page, and the `\u` escapes need a UTF-8 execution charset.
   - Vendored ImGui builds as its own static library, `imgui`, so it gets neither the game's precompiled header nor its warning flags. That replaced the `SKIP_PRECOMPILE_HEADERS` list. Apple Clang's 140 `-Wnontrivial-memcall` warnings in ImGui are all instantiated from ImGui's own files and no longer count.
   - Verified with GCC 15: `linux` (Release) and `linux-sanitize` build without warnings, and the three replays in `../replays/` match in both. Under the sanitizers, replay playback reports nothing, including leaks, and a 20-second windowed demo is clean with vendored SDL and with `-noaudio`. With the system SDL_mixer and music on, the sanitized game crashes on exit: `Mix_Quit` unloads FluidSynth while one of its threads still runs. That happens in FluidSynth, not the game. With FluidSynth preloaded, the exit is clean except for leaks inside FluidSynth, libinstpatch and GLib. `docs/setup.md` has the workaround.
   - MSVC and Apple Clang were only checked through CI. The fixes cover every warning from them in the last `main` run.
   - The `windows-x86-xp` preset used the VS 2017 `v141_xp` toolset, whose C++20 support is incomplete. It and `windows-x86` are gone (decided 2026-10-19, see [Open decisions](#open-decisions)); Windows builds are 64-bit only.
5. Done: **SDL3:** port window, renderer, input, audio and timing to SDL3 and SDL3_mixer, and move ImGui to a current release with its SDL3 backends. Drop `imgui_sdl.cpp`. Check SDL3_mixer's MIDI support on every platform before starting. The step lands as several stacked pull requests: ImGui first, then the SDL3 port, then the music.
   - **MIDI check, 2026-10-19.** SDL3_mixer 3.2.4 is current. It plays MIDI only through its Timidity decoder (needs a `timidity.cfg` with patches) and its FluidSynth decoder (needs libfluidsynth and a SoundFont). Native MIDI is gone; the SDL developers moved it to the separate [SDL_native_midi](https://github.com/libsdl-org/SDL_native_midi) library (zlib licence, Windows through winmm, macOS through AudioToolbox, Linux through the ALSA sequencer). `SDL_SOUNDFONTS` is no longer read; a SoundFont is passed as a decoder property instead, and the FluidSynth decoder falls back to `/usr/share/sounds/sf2/default-GM.sf2` and two similar paths.
     - **Windows and macOS:** SDL2_mixer played the music through native MIDI, which is the only synth these systems have without extra files. SDL3_mixer can't, so the game needs SDL_native_midi there.
     - **Linux:** Debian's `libsdl3-mixer` 3.2.4 (forky and sid) is built with `SDLMIXER_MIDI=OFF`, so it plays no MIDI at all. Ubuntu 24.04 has neither SDL3 nor SDL3_mixer; 26.04 has SDL3 3.4.2 but no SDL3_mixer. Homebrew has both (`sdl3` 3.4.18, `sdl3_mixer` 3.2.4). A vendored SDL3_mixer can't use FluidSynth (it requires `SDLMIXER_VENDORED=OFF`), so it has only Timidity, as the vendored SDL2_mixer had.
     - **Emscripten:** no native MIDI and no FluidSynth; Timidity would need patches downloaded. Phase 4 already plans TinySoundFont.
     - **Decision:** music must not depend on a system SDL3_mixer having MIDI. The game plays MIDI itself: SDL_native_midi on Windows and macOS, as before, and elsewhere TinySoundFont (`tsf.h` and `tml.h`, MIT), which renders `PINBALL.MID` with a SoundFont the system provides (`SDL_SOUNDFONTS`, then the usual `/usr/share/sounds/sf2/` paths) into an SDL3_mixer track. That is the same renderer the web build needs. Both libraries are small enough to vendor, so packagers don't need network access.
   - Done: **ImGui 1.93.0.** ImGui moved from 1.89.2 to 1.93.0, still on SDL2, with the official `imgui_impl_sdl2` and `imgui_impl_sdlrenderer2` backends. `imgui_sdl.cpp`, the fallback renderer for SDL older than 2.0.17, is gone, so the system SDL2 must be 2.0.17 or newer. The ImGui files are unmodified now: the two decompression helpers that had been patched into `imgui_draw.cpp` live in `EmbeddedDataDecompress.cpp`, and `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` stays on in `imconfig.h`.
     - ImGui 1.92 loads font glyphs on demand, so the game no longer builds glyph ranges from the translations (`translations::GetGlyphRange` is gone). The UI scale is `style.FontScaleMain`, the text boxes size their font with `PushFont(nullptr, size)` instead of `SetWindowFontScale`, and the sprite viewer draws its image borders with `ImageBorderSize`.
     - Verified with GCC 15: `linux` and `linux-sanitize` build without warnings, the three replays match in both, and a windowed demo under the sanitizers exits clean. The menus open and work with the mouse, and Japanese with a TTF font at UI scale 1.5 renders. A new `SDL Error: That operation is not supported` line appears once at startup; SDL2 sets it inside the new ImGui backend and recovers.
     - MSVC and Apple Clang only through CI.
   - Done: **SDL3 and SDL3_mixer.** Window, renderer, input, gamepads, timing and audio use SDL3 3.4 and SDL3_mixer 3.2, and ImGui uses its `imgui_impl_sdl3` and `imgui_impl_sdlrenderer3` backends. Vendored builds fetch SDL3 3.4.18 and SDL3_mixer 3.2.4 with pinned hashes. The system path finds both through their CMake config files, so `CMakeModules/` is gone.
     - **Sound:** each sound channel is an SDL3_mixer track tagged `sound`, so focus loss pauses the effects and leaves the music track alone. Stereo positioning reproduces the gains SDL2_mixer's `Mix_SetPosition` gave a mono sound on a stereo device; its two left-right swaps for angles over 180° cancel out. Saved volumes keep SDL2_mixer's 0 to 128 range, and saved key and gamepad bindings keep their values (SDL3 renamed the constants, not the numbers).
     - **Music (interim):** music plays through SDL3_mixer's MIDI decoders. The vendored build has Timidity only, as the vendored SDL2_mixer had, and Debian's SDL3_mixer plays no MIDI. Windows and macOS have no music without a Timidity setup until the next pull request brings native MIDI back.
     - **CI and packages:** Ubuntu 24.04 has no SDL3 or SDL3_mixer, so the `linux` job runs in a `debian:forky-slim` container (pinned by digest) with `libsdl3-dev` and `libsdl3-mixer-dev`. The `linux-vendored` job installs more headers, because SDL3 stops configuring when a header for an X11 extension it wants is missing. Ubuntu 26.04 and Fedora have SDL3 but no SDL3_mixer; `docs/setup.md` sends their users to `linux-vendored`.
     - Verified with GCC 15, against the SDL3 3.4.2 and SDL3_mixer 3.2.4 packages from Ubuntu 26.04 and Debian sid (extracted locally, no install) and against the vendored build: both build without warnings, and the three replays match in both and in `linux-sanitize`. A new recording made with the SDL3 build (plunger, nudges, tilt) plays back. Under Xvfb, the menus open and work with the mouse, a game starts with F2 and plays with the keyboard, and a sanitized windowed run with audio exits clean. Audio was checked through SDL's `disk` driver: effects play at the same level as with SDL2, and the plunger sound gets the same left-right ratio (about 0.1).
     - Not verified: running the Windows, MinGW and macOS builds (CI only builds them), gamepads and controller hot-plugging, HiDPI displays, and Wayland. Windows, macOS and MinGW only through CI.
   - Done: **Music.** `midi.cpp` no longer depends on SDL3_mixer having MIDI. It picks the first backend that starts: SDL_native_midi on Windows and macOS, then TinySoundFont with a SoundFont from `SDL_SOUNDFONTS` or the usual `/usr/share/sounds/sf2/` and `/usr/share/soundfonts/` paths, then SDL3_mixer's own decoders. Both libraries are vendored in `third_party/` (zlib and MIT), so offline package builds have them; no SoundFont ships with the game.
     - TinySoundFont renders on the audio thread, in the get callback of an `SDL_AudioStream` that feeds the music track; the main thread switches songs while holding the stream's lock. The song loops when its last event has played, as native MIDI and `Mix_PlayMusic(-1)` did. It renders at -6 dB. At 0 dB its RMS matched SDL2_mixer's FluidSynth (about 10,000 of 32,767 for the theme with FluidR3 through SDL's `disk` driver, against 11,000 to 13,000), but the loud chords went past full scale and clipped: 4,796 clamped samples in 30 seconds at the default Music Volume. At -6 dB the whole theme (one 528-second loop) peaks at 28,877, about -1 dBFS, with RMS 5,100; that is 6 dB quieter than FluidSynth, which applies its own gain first. -5 dB peaked at 30,201 in the first 30 seconds, too close to full scale. TimGM6mb is quieter: peak 20,965, RMS 3,300.
     - TinySoundFont keeps the whole SoundFont in memory as floats: with FluidR3 (148 MB on disk) the game uses about 420 MB, against 300 MB with SDL2_mixer and FluidSynth; with TimGM6mb (6 MB), 140 MB. Only SF2 loads, not SF3.
     - Verified with GCC 15 on Linux: the vendored and the system-package builds play the theme through TinySoundFont with Debian's `default-GM.sf2`, F6 stops and restarts it, the three replays match, and a sanitized windowed run with music, toggling it three times, exits clean.
     - Not verified: native MIDI on Windows and macOS, which CI only compiles; Full Tilt's MDS tracks with TinySoundFont; whether the theme's loop point sounds right after the first pass.
6. **Tooling:** a `.clang-format` as close to the current style as possible, and `clang-tidy` in CI on changed files.
   - The code is never reformatted as a whole. It already follows one Visual Studio style (tabs, braces on their own line), and even a tuned config rewrites about 1,600 lines in 114 of 124 game files, not counting `translations.cpp`. That would make every upstream merge conflict.
   - Only changed lines get formatted: `git clang-format --staged` in the lefthook `pre-commit` hook, and `git clang-format --diff` against the base branch in CI. The version is pinned through `uvx --from clang-format==<version>`, so nobody installs it by hand.
   - A starting config: `BasedOnStyle: Microsoft`, `UseTab: ForContinuationAndIndentation`, `TabWidth: 4`, `ColumnLimit: 120`, `IndentCaseLabels: false`, `MaxEmptyLinesToKeep: 2`, and `SortIncludes: Never`, because `pch.h` must stay first. `translations.cpp` is excluded.

Done when all presets build in CI and the determinism harness passes against the Phase 0 baseline.

## Phase 2: modern presentation

1. Integer scaling with an optional "fit" mode, HiDPI-correct window sizing, and proper full screen on modern displays.
2. Widescreen layout: use the space beside the table for the score panel or artwork instead of black bars.
3. Optional post-processing of the finished frame (CRT and similar filters) using SDL3's GPU API, off by default. Sprite upscaling is item 6, not a post-processing filter; the two can be on at once.
4. Gamepad support through the SDL3 gamepad API, including rumble on flipper and bumper hits. Touch controls, which the web build on tablets needs too.
5. High-refresh rendering: decide whether to interpolate ball and flipper positions between physics updates or simply raise both rates. Interpolation must not change the physics.
6. **Upscaled art with a toggle back to the original.** An option in `optionsStruct` switches between the upscaled sprites and the original ones at any time, including during a game.
   - Upscaled art is derived from Microsoft's sprites, so it gets the same treatment as the game data: never committed, never uploaded as a CI artifact, never hosted. It is produced on the player's machine from their own DAT file. The default is an algorithmic pixel-art upscaler with a permissive licence, such as MMPX, xBR or ScaleFX (MIT); GPL code like xBRZ can't be used under this project's MIT licence. The result may be cached next to the options file. A folder of player-supplied upscaled sprites, for example from a local AI upscaler, can override it.
   - Sprites are composed with depth maps, so each upscaled sprite needs a depth map at the same scale. Scale the depth maps with nearest-neighbour, which keeps the depth steps exact. The frame becomes larger than 600×416 (2× or 4×), which ties in with item 1.
   - Rendering only. Collision, physics and table rules keep the original coordinates, and replays must match with the toggle in either position.
   - Sprites are 8-bit palette indices. Upscaling the indices keeps the palette pipeline unchanged; upscaling colours gives smoother edges but needs a true-colour frame. Check first how lights and flashing are drawn (sprite swaps or palette changes), then pick the approach that keeps them correct.

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
- **Update rate:** it defaults to 60 updates per second under `__EMSCRIPTEN__`, because the game stutters at the desktop default of 120.
- **Game data:** it preloads the game files into the bundle and hosts them. Its `gh-pages` branch, which holds only the build output (`index.html`, `.js`, `.wasm`, `CNAME`), commits that bundle as `SpaceCadetPinball.data`. Point 2 above exists to avoid exactly that, and point 3 must deploy from a CI artifact that never contains the data.

## Role of REA

REA is not a dependency of the game. Use it to settle questions about the original's behaviour: a suspected decompilation bug, a physics quirk the harness reveals, or Full Tilt-only features (an open item in upstream's plans).

## Upstream sync

`master` follows `upstream/master`. Merge it into `main` when upstream changes. After Phase 1, merges will conflict; accept that, and send self-contained fixes upstream as separate small pull requests where they still apply.

## Open decisions

- Decided: `FetchContent` for dependencies, not vcpkg (universal macOS builds, no extra tool, SDL3 and Emscripten are a tag change).
- Decided 2026-10-19: the Windows XP (`v141_xp`) and 32-bit builds are dropped. Windows builds are 64-bit only.
- Decided 2026-10-19: Phase 2 includes upscaled art, generated on the player's machine, with a toggle back to the original (Phase 2 item 6).
- Deferred 2026-10-19: native Android and iOS targets. Phase 2 input work covers gamepads and touch for the web build, not mobile packaging.

## Working order

From 2026-10-19 the remaining work runs as a stack of pull requests, one per step, each based on the one before: Phase 1 steps 5 and 6, then Phase 2, then Phase 4. Each pull request keeps CI green and the replays matching. A step that would change gameplay stops and asks first.
