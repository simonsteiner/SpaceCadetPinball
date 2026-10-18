<!-- markdownlint-disable-file MD033 -->

# SpaceCadetPinball

Reverse engineering of `3D Pinball for Windows - Space Cadet`, the pinball game bundled with Windows, as a cross-platform C++ port on SDL2.
It runs on desktop Windows, Linux and macOS and plays the original data files from either the Windows or the Full Tilt! Pinball release.

This is [simonsteiner's fork](https://github.com/simonsteiner/SpaceCadetPinball) of [k4zmu2a/SpaceCadetPinball](https://github.com/k4zmu2a/SpaceCadetPinball). Work happens on `main`; `master` tracks upstream. See the [modernization plan](docs/plan/2026-10-18-modernization.md).

## Quickstart

```bash
sudo apt install cmake build-essential ninja-build libsdl2-dev libsdl2-mixer-dev
cmake --preset linux && cmake --build --preset linux
cp -r /path/to/original-game/. bin/  # PINBALL.DAT with its .WAV and .MID files
./bin/SpaceCadetPinball
```

The game data is not included. Other platforms, data search paths and troubleshooting are in [docs/setup.md](docs/setup.md).

## Docs

* [docs/setup.md](docs/setup.md): building on each platform, game data, command-line options, troubleshooting
* [docs/deployment.md](docs/deployment.md): producing release builds
* [docs/reference/](docs/reference/): DAT file format, DAT and PDB dumps, per-function decompilation status sheet
* [docs/plan/](docs/plan/): dated work plans
* [GLOSSARY.md](GLOSSARY.md): game and data terms
* [CONTRIBUTING.md](CONTRIBUTING.md): upstream's issue and pull request policy
* [AGENTS.md](AGENTS.md): rules for coding agents

## Source

* `pinball.exe` from `Windows XP` (SHA-1 `2A5B525E0F631BB6107639E2A69DF15986FB0D05`) and its public PDB
* `CADET.EXE` 32bit version from `Full Tilt! Pinball` (SHA-1 `3F7B5699074B83FD713657CD94671F2156DBEDC4`)

## Tools used

`Ghidra`, `Ida`, `Visual Studio`

## What was done

* All structures were populated, globals and locals named.
* All subs were decompiled, C pseudo code was converted to compilable C++. Loose (namespace?) subs were assigned to classes.

## Known source ports

| Platform           | Author          | URL                                                                                                        |
| ------------------ | --------------- | ---------------------------------------------------------------------------------------------------------- |
| PS Vita            | Axiom           | <https://github.com/suicvne/SpaceCadetPinball_Vita>                                                        |
| Emscripten         | alula           | <https://github.com/alula/SpaceCadetPinball> <br> Play online: <https://alula.github.io/SpaceCadetPinball> |
| Nintendo Switch    | averne          | <https://github.com/averne/SpaceCadetPinball-NX>                                                           |
| webOS TV           | mariotaku       | <https://github.com/webosbrew/SpaceCadetPinball>                                                           |
| Android (WIP)      | Iscle           | https://github.com/Iscle/SpaceCadetPinball                                                                 |
| Nintendo Wii       | MaikelChan      | https://github.com/MaikelChan/SpaceCadetPinball                                                            |
| Nintendo 3DS       | MaikelChan      | https://github.com/MaikelChan/SpaceCadetPinball/tree/3ds                                                   |
| Nintendo DS        | Headshotnoby    | https://github.com/headshot2017/3dpinball-nds                                                              |
| Nintendo Wii U     | IntriguingTiles | https://github.com/IntriguingTiles/SpaceCadetPinball-WiiU                                                  |
| PlayStation 2      | Headshotnoby    | https://github.com/headshot2017/3dpinball-ps2                                                              |
| Sega Dreamcast     | Headshotnoby    | https://github.com/headshot2017/3dpinball-dc                                                               |
| MorphOS            | BeWorld         | https://www.morphos-storage.net/?id=1688897                                                                |
| AmigaOS 4          | rjd324          | http://aminet.net/package/game/actio/spacecadetpinball-aos4                                                |
| Android (WIP)      | fexed           | https://github.com/fexed/Pinball-on-Android                                                                |

Platforms covered by this project: desktop Windows, Linux and macOS.

[![Packaging status](https://repology.org/badge/tiny-repos/spacecadetpinball.svg)](https://repology.org/project/spacecadetpinball/versions)

## Plans

* ~~Decompile original game~~
* ~~Resizable window, scaled graphics~~
* ~~Loader for high-res sprites from CADET.DAT~~
* ~~Cross-platform port using SDL2, SDL2_mixer, ImGui~~
* Full Tilt Cadet features
* Localization support
* Maybe: Support for the other two tables - Dragon and Pirate
* Maybe: Game data editor

## On 64-bit bug that killed the game

I did not find it, decompiled game worked in x64 mode on the first try.\
It was either lost in decompilation or introduced in x64 port/not present in x86 build.\
Based on public description of the bug (no ball collision), I guess that the bug was in `TEdgeManager::TestGridBox`
