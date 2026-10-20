# SpaceCadetPinball

A C++ reimplementation of 3D Pinball for Windows (Space Cadet), decompiled from the original binaries and ported to SDL3, SDL3_mixer and ImGui. The loader reads the player's original DAT file into groups, the table builds a component for each group, and components react to each other only by sending messages that the table rules in `SpaceCadetPinball/control.cpp` handle. Rendering composes sprites and depth maps into one software frame that SDL scales to the window.
Domain terms are in `GLOSSARY.md`; dated work plans are in `docs/plan/` (the newest is current).

## Commands

```bash
cmake --preset linux  # configure (presets: linux, linux-debug, linux-sanitize, linux-vendored, windows-x64, mingw, macos, web); linux needs SDL3 and SDL3_mixer dev packages
cmake --build --preset linux  # build; the executable lands in bin/ in the source tree
cmake --preset linux-vendored && cmake --build --preset linux-vendored  # build with SDL fetched from source, as the Windows and macOS presets do
./bin/SpaceCadetPinball -noaudio  # run; needs game data, see docs/setup.md
tools/check-replays.sh ../replays ../gamedata  # play back recorded sessions and report any that play differently
lefthook install  # optional Git hooks from lefthook.yml: game-data guard and source-list checks, build and replays before push
uvx --from clang-format==23.1.3 git-clang-format main  # format only the lines changed since main; never a whole file
tools/lint-changed.sh main build/lint  # clang-format and clang-tidy on changed lines, as CI runs them (docs/setup.md)
```

There are no automated tests yet. A change is checked by building it and playing it with real game data. The `lint` job checks only the lines a pull request changes, with clang-format and clang-tidy; don't reformat code you didn't change, because whole-file formatting would make every upstream merge conflict. Before a change that could affect play, record sessions with `-record` and check them with `tools/check-replays.sh` afterwards (`docs/replays.md`). The `Build` workflow (`.github/workflows/Build.yml`) builds every preset except `linux-debug` and `linux-sanitize` on each pull request, with `SCP_WARNINGS=ON`, which makes warnings errors. After a change to memory handling, run the replays with a `linux-sanitize` build (AddressSanitizer and UBSan).

## Conventions

### Game code

- **Table rules live only in `SpaceCadetPinball/control.cpp`.** Its `component_info` tables bind components to rule handlers by group name; mission, rank and light logic belongs there, not in component classes.
- **Outside input reaches the table only through `pb` functions that open a `replay::Scope`.** A new entry point that changes table state from outside `pb::frame` needs one, or replays diverge (`docs/replays.md`).
- **Events between components are `Message(MessageCode, float)` calls.** Add a `MessageCode` value for a new event instead of a new cross-component method.
- **Text shown on the table goes through `pb::get_rc_string(Msg::...)`.** Its translations live in `SpaceCadetPinball/translations.cpp`; never pass a literal to a text box.
- **Settings live only in `optionsStruct` (`SpaceCadetPinball/options.h`).** Add new ones there and clamp them in `options.cpp`, not as globals elsewhere.
- **Open files with `fopenu`, not `fopen`.** It handles UTF-8 paths on Windows (`SpaceCadetPinball/pch.h`).
- **Every game `.cpp` starts with `#include "pch.h"`.** It is the shared precompiled header; only vendored ImGui files and the generated `EmbeddedData.cpp` skip it.
- **The game compiles without warnings.** CI builds with `SCP_WARNINGS=ON`, which treats warnings as errors with GCC, Clang and MSVC. Fix a new warning instead of silencing it; a variable used only in `assertm` gets `[[maybe_unused]]`, because Release builds drop the assert.
- **Upscaled Art keeps a second, larger frame in step with the original one.** `render` composes both: every paint at 1x is repeated at `HiScale` with the art from `Upscale`, and the score panel, which text boxes and digits draw straight into the 1x frame, is magnified from there when it changes. A new way of drawing into the frame has to do the same, or it shows only with the toggle off.

### Build

- **Source files are listed by hand in `CMakeLists.txt`.** Add each new `.cpp` and `.h` to `SOURCE_FILES`; there is no glob. Vendored ImGui is listed in `IMGUI_SOURCE_FILES` and builds as the separate `imgui` library, without the game's precompiled header and warnings.
- **SDL comes from `SCP_VENDORED_SDL`.** `ON` (Windows, macOS, `mingw`, `linux-vendored`) fetches the SDL3 and SDL3_mixer versions pinned in `CMakeLists.txt` and links them statically. `OFF` (the Linux default) uses the system packages. Keep both paths working, because distribution packagers use the system one. CI builds the system path in a Debian testing container, because Ubuntu 24.04 has no SDL3 packages. To bump SDL, change the URL and the `URL_HASH` together.
- **Post-processing shaders are compiled ahead of time.** `SpaceCadetPinball/shaders/*.frag.hlsl` become SPIR-V and DXIL, and the hand-written `*.frag.msl` are embedded as text, in the generated `PostEffectShaders.h`; run `tools/build-shaders.sh` after changing a shader (it fetches a pinned DXC). Keep each `.msl` in step with its `.hlsl`.

### Git

- **Work lands on `main` through pull requests.** `master` mirrors `upstream/master` (k4zmu2a/SpaceCadetPinball) and only receives upstream changes.

## Hard constraints

- **Never commit game data.** The original game's files (`*.DAT`, `*.WAV`, `*.MID`, `Pinball.exe`, `table.bmp`, `wavemix.inf`) are Microsoft's, and so is anything made from them, such as exported or upscaled sprites. `.gitignore` excludes them in any letter case; keep game data outside the repository and never force-add it.
- **The web build never contains game data.** `.github/workflows/Pages.yml` publishes only the page, the game's code and the MIT SoundFont, after `tools/check-no-game-data.sh` has checked the folder. The player's files go into their browser's IndexedDB and nowhere else.
- **Never commit to or push `master`.** Merge `upstream/master` into it only when asked.
- **Ask the user before changing gameplay behaviour,** including fixes to physics or table rules. The decompiled physics and table rules reproduce the original; a quirk that looks like a bug may be faithful to it.

## Gotchas

- **Vendored code is ImGui and `third_party/`.** `imgui*.cpp`, `imgui*.h`, `imstb_*.h` and `imconfig.h` in `SpaceCadetPinball/` are third-party; upgrade them as a set rather than editing them. The ImGui files are unmodified upstream releases with only `imconfig.h` set up, so an upgrade is a copy of the new release's files. `third_party/` holds SDL_native_midi and TinySoundFont (with stb_vorbis, for the SF3 SoundFonts TinySoundFont loads), which play the music, and MMPX, which upscales the art; `third_party/README.md` lists their versions.
- **Music doesn't go through SDL3_mixer's MIDI support.** `midi.cpp` plays it through native MIDI on Windows and macOS, else TinySoundFont with a system SoundFont, else SDL3_mixer's decoders (`docs/setup.md`, Music). TinySoundFont renders on the audio thread, in the callback of the stream that feeds the music track; change its song only while holding that stream's lock.
- **The game can't run without game data,** so it can't be play-tested in CI. Use `-noaudio` on machines without a sound device.
- **The main loop prints any pending SDL error as `SDL Error: ...`.** Errors that SDL or SDL_mixer set internally and recovered from show up too, such as a missing `timidity.cfg`; such a line doesn't mean anything failed.
- **Only the `macos` preset is universal.** It sets `CMAKE_OSX_ARCHITECTURES` to `arm64;x86_64`; a build without the preset targets the host architecture.
- **Vendored SDL needs network on the first configure.** FetchContent downloads into `build/<preset>/_deps/`; later configures reuse it.

## Docs

- `README.md` — what the project is, the quickstart, provenance.
- `docs/setup.md` — building on each platform, Git hooks, where the game looks for data, command-line flags, troubleshooting.
- `docs/deployment.md` — producing release builds and where the version number lives.
- `docs/replays.md` — recording and checking sessions, and the replay file format.
- `docs/reference/` — DAT file format and dumps of the original DAT and PDB; read before touching `loader.cpp` or `partman.cpp`.
- `docs/plan/` — dated work plans; read the latest before starting new work.
- `GLOSSARY.md` — game and data terms.
