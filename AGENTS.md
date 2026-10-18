# SpaceCadetPinball

A C++ reimplementation of 3D Pinball for Windows (Space Cadet), decompiled from the original binaries and ported to SDL2, SDL2_mixer and ImGui. The loader reads the player's original DAT file into groups, the table builds a component for each group, and components react to each other only by sending messages that the table rules in `SpaceCadetPinball/control.cpp` handle. Rendering composes sprites and depth maps into one software frame that SDL scales to the window.
Domain terms are in `GLOSSARY.md`; dated work plans are in `docs/plan/` (the newest is current).

## Commands

```bash
cmake -S . -B build -G Ninja  # configure (Linux/macOS); needs SDL2 and SDL2_mixer dev packages
cmake --build build  # build; the executable lands in bin/ in the source tree
cmake -S . -B build-mingw -DCMAKE_TOOLCHAIN_FILE=mingwcc.cmake  # configure a Windows cross-build from Linux
./bin/SpaceCadetPinball -noaudio  # run; needs game data, see docs/setup.md
```

There are no automated tests or linters yet. A change is checked by building it and playing it with real game data.

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

### Git

- **Work lands on `main` through pull requests.** `master` mirrors `upstream/master` (k4zmu2a/SpaceCadetPinball) and only receives upstream changes.

## Hard constraints

- **Never commit game data.** `*.DAT`, `*.WAV` and `*.MID` files from the original game are Microsoft's and `.gitignore` does not yet exclude them; check `git status` before committing.
- **Never commit to or push `master`.** Merge `upstream/master` into it only when asked.
- **Ask the user before changing gameplay behaviour,** including fixes to physics or table rules. The decompiled physics and table rules reproduce the original; a quirk that looks like a bug may be faithful to it.

## Gotchas

- **Vendored code is ImGui.** `imgui*.cpp`, `imgui*.h`, `imstb_*.h` and `imconfig.h` in `SpaceCadetPinball/` are third-party; upgrade them as a set rather than editing them. `imgui_sdl.cpp` is also third-party: an older ImGui SDL renderer that is still compiled in.
- **The game can't run without game data,** so it can't be play-tested in CI. Use `-noaudio` on machines without a sound device.
- **macOS builds are universal.** `CMakeLists.txt` hard-codes `CMAKE_OSX_ARCHITECTURES` to `arm64;x86_64`, which overrides `-D`.

## Docs

- `README.md` — what the project is, the quickstart, provenance.
- `docs/setup.md` — building on each platform, where the game looks for data, command-line flags, troubleshooting.
- `docs/deployment.md` — producing release builds and where the version number lives.
- `docs/reference/` — DAT file format and dumps of the original DAT and PDB; read before touching `loader.cpp` or `partman.cpp`.
- `docs/plan/` — dated work plans; read the latest before starting modernization work.
- `GLOSSARY.md` — game and data terms.
