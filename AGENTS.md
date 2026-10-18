# SpaceCadetPinball

A C++ reimplementation of 3D Pinball for Windows (Space Cadet), decompiled from the original binaries and ported to SDL2, SDL2_mixer and ImGui. The loader reads the player's original DAT file into groups, the table builds a component for each group, and components react to each other only by sending messages that the table rules in `SpaceCadetPinball/control.cpp` handle. Rendering composes sprites and depth maps into one software frame that SDL scales to the window.
Domain terms are in `GLOSSARY.md`; dated work plans are in `docs/plan/` (the newest is current).

## Commands

```bash
cmake --preset linux  # configure (presets: linux, linux-debug, linux-vendored, windows-x64, windows-x86, windows-x86-xp, mingw, macos); linux needs SDL2 and SDL2_mixer dev packages
cmake --build --preset linux  # build; the executable lands in bin/ in the source tree
cmake --preset linux-vendored && cmake --build --preset linux-vendored  # build with SDL fetched from source, as the Windows and macOS presets do
./bin/SpaceCadetPinball -noaudio  # run; needs game data, see docs/setup.md
```

There are no automated tests or linters yet. A change is checked by building it and playing it with real game data. The `Build` workflow (`.github/workflows/Build.yml`) builds every preset except `linux-debug` and `windows-x86-xp` on each pull request, with `SCP_WARNINGS=ON`.

## Conventions

### Game code

- **Table rules live only in `SpaceCadetPinball/control.cpp`.** Its `component_info` tables bind components to rule handlers by group name; mission, rank and light logic belongs there, not in component classes.
- **Events between components are `Message(MessageCode, float)` calls.** Add a `MessageCode` value for a new event instead of a new cross-component method.
- **Text shown on the table goes through `pb::get_rc_string(Msg::...)`.** Its translations live in `SpaceCadetPinball/translations.cpp`; never pass a literal to a text box.
- **Settings live only in `optionsStruct` (`SpaceCadetPinball/options.h`).** Add new ones there and clamp them in `options.cpp`, not as globals elsewhere.
- **Open files with `fopenu`, not `fopen`.** It handles UTF-8 paths on Windows (`SpaceCadetPinball/pch.h`).
- **Every game `.cpp` starts with `#include "pch.h"`.** It is the shared precompiled header; only vendored ImGui files and the generated `EmbeddedData.cpp` skip it.

### Build

- **Source files are listed by hand in `CMakeLists.txt`.** Add each new `.cpp` and `.h` to `SOURCE_FILES`; there is no glob.
- **SDL comes from `SCP_VENDORED_SDL`.** `ON` (Windows, macOS, `mingw`, `linux-vendored`) fetches the SDL2 and SDL2_mixer versions pinned in `CMakeLists.txt` and links them statically. `OFF` (the Linux default) uses the system packages. Keep both paths working, because distribution packagers use the system one. To bump SDL, change the URL and the `URL_HASH` together.

### Git

- **Work lands on `main` through pull requests.** `master` mirrors `upstream/master` (k4zmu2a/SpaceCadetPinball) and only receives upstream changes.

## Hard constraints

- **Never commit game data.** The original game's files (`*.DAT`, `*.WAV`, `*.MID`, `Pinball.exe`, `table.bmp`, `wavemix.inf`) are Microsoft's. `.gitignore` excludes them in any letter case; keep game data outside the repository and never force-add it.
- **Never commit to or push `master`.** Merge `upstream/master` into it only when asked.
- **Ask the user before changing gameplay behaviour,** including fixes to physics or table rules. The decompiled physics and table rules reproduce the original; a quirk that looks like a bug may be faithful to it.

## Gotchas

- **Vendored code is ImGui.** `imgui*.cpp`, `imgui*.h`, `imstb_*.h` and `imconfig.h` in `SpaceCadetPinball/` are third-party; upgrade them as a set rather than editing them. `imgui_sdl.cpp` is also third-party: an older ImGui SDL renderer that is still compiled in.
- **The game can't run without game data,** so it can't be play-tested in CI. Use `-noaudio` on machines without a sound device.
- **The main loop prints any pending SDL error as `SDL Error: ...`.** Errors that SDL or SDL_mixer set internally and recovered from show up too, such as `Couldn't open timidity.cfg`; such a line doesn't mean anything failed.
- **Only the `macos` preset is universal.** It sets `CMAKE_OSX_ARCHITECTURES` to `arm64;x86_64`; a build without the preset targets the host architecture.
- **Vendored SDL needs network on the first configure.** FetchContent downloads into `build/<preset>/_deps/`; later configures reuse it.

## Docs

- `README.md` — what the project is, the quickstart, provenance.
- `docs/setup.md` — building on each platform, where the game looks for data, command-line flags, troubleshooting.
- `docs/deployment.md` — producing release builds and where the version number lives.
- `docs/reference/` — DAT file format and dumps of the original DAT and PDB; read before touching `loader.cpp` or `partman.cpp`.
- `docs/plan/` — dated work plans; read the latest before starting modernization work.
- `GLOSSARY.md` — game and data terms.
