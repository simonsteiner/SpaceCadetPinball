# Setup

How to build the game, where it looks for game data, and how to fix common problems.

## Game data

The game needs the original game data, which is not included. Copy the `.DAT` file and the sound and music files that came with it from an installation of either:

- 3D Pinball for Windows (`PINBALL.DAT`), or
- Full Tilt! Pinball (`CADET.DAT`, or `DEMO.DAT` from the demo).

The game searches these folders in order and uses the first data file it finds:

1. the current working directory
2. the folder containing the executable
3. the user preferences folder (`SDL_GetPrefPath`, for example `~/.local/share/SpaceCadetPinball/` on Linux)
4. Linux and macOS only: `/usr/local/share/SpaceCadetPinball/`, then `/usr/share/SpaceCadetPinball/`

In each folder it tries `CADET.DAT`, then `PINBALL.DAT`, then `DEMO.DAT`, in upper case and then lower case. The "Prefer 3DPB Data" menu option swaps the first two.

## Build

The project uses C++20, CMake 3.24 or newer, and `SDL2` (2.0.17 or newer) and `SDL2_mixer`. `CMakePresets.json` has one preset per platform. Configure and build with the same preset name:

```bash
cmake --preset <preset>
cmake --build --preset <preset>
```

Each preset builds in `build/<preset>/`. The executable is written to `bin/` in the source tree (`bin/Release/` with Visual Studio). All presets write to the same `bin/`, so the last build wins.

| Preset | Host | SDL |
| --- | --- | --- |
| `linux` | Linux | system packages, Release |
| `linux-debug` | Linux | system packages, Debug |
| `linux-sanitize` | Linux | system packages, Debug with AddressSanitizer and UBSan |
| `linux-vendored` | Linux | built from source |
| `windows-x64` | Windows, Visual Studio 2022 | built from source |
| `mingw` | Linux, cross-compiling to Windows x64 | built from source |
| `macos` | macOS, universal `arm64` and `x86_64` | built from source |

The `SCP_VENDORED_SDL` option chooses where SDL comes from. When it is `ON` (the default on Windows and macOS), CMake downloads SDL2 2.32.10 and SDL2_mixer 2.8.1 at configure time, checks them against pinned SHA-256 hashes, and links them statically. SDL_mixer is built with only what the game uses: WAV, and MIDI through Timidity and the platform's native MIDI. Its other codecs and FluidSynth are left out. When it is `OFF` (the default on Linux), CMake uses the installed `SDL2` and `SDL2_mixer` development packages. CI builds every preset except `linux-debug` and `linux-sanitize` on each pull request. Windows builds are 64-bit only; the 32-bit and Windows XP builds were dropped with the move to C++20.

Plain `cmake -S . -B build` without a preset still works and follows the same defaults.

Add `-DSCP_WARNINGS=ON` to the configure command to build the game with extra warnings, `-Wall -Wextra` or `/W4` with MSVC, and treat them as errors. Unused parameters are allowed, because overrides and callbacks keep the signatures their interfaces set. The option applies only to the game, not to SDL or ImGui, and is off by default, so a newer compiler with new warnings doesn't break a packager's build. CI turns it on. To see the warnings without failing the build, add `--compile-no-warning-as-error` to the configure command.

`SCP_SANITIZERS=ON`, which the `linux-sanitize` preset sets, builds the game and ImGui with AddressSanitizer and UndefinedBehaviorSanitizer. It needs GCC or Clang. Run the replays with this build after a change to memory handling (`tools/check-replays.sh`, see `docs/replays.md`). The window, renderer and sound effects are clean too, but music isn't: with the system SDL_mixer, `Mix_Quit` unloads FluidSynth while one of its threads is still running, and the game crashes on exit under ASan. Use `-noaudio` for a windowed run, or preload FluidSynth to keep it loaded: `LD_PRELOAD="$(gcc -print-file-name=libasan.so) /usr/lib/x86_64-linux-gnu/libfluidsynth.so.3"`. LeakSanitizer then reports leaks inside FluidSynth, libinstpatch and GLib; none are in the game.

### Linux

```bash
# Debian and Ubuntu
sudo apt install cmake build-essential ninja-build libsdl2-dev libsdl2-mixer-dev libsdl2-mixer-2.0-0 libsdl2-2.0-0 fluidsynth
# Fedora
sudo dnf install cmake ninja-build SDL2 SDL2-devel SDL2_mixer SDL2_mixer-devel fluidsynth fluidsynth-libs mscore-fonts g++

cmake --preset linux
cmake --build --preset linux
./bin/SpaceCadetPinball
```

Distribution packages should keep `SCP_VENDORED_SDL=OFF`, the Linux default, so the game links against the system SDL. Ubuntu 22.04 ships CMake 3.22, which is too old: install a newer one from [Kitware's APT repository](https://apt.kitware.com/) or with `pip install cmake`.

Tested upstream with GCC 10 and Clang 11. The `linux` preset builds with GCC 15 and CMake 4.2. GCC warns about right-to-left marks in `translations.cpp` and, in optimized builds, about unchecked `fread` return values; both are harmless. `cmake --install build/linux` installs the binary, the `.desktop` file, the AppStream metadata and the icons from `Platform/Linux/`.

The `linux-vendored` preset builds SDL from source instead and needs no SDL packages. SDL loads X11, Wayland, PulseAudio, PipeWire and ALSA at run time, but it can only use the ones whose development headers were installed when it was built. That build plays MIDI through Timidity only, so music needs a `timidity.cfg` with patches.

Some distributions package the game, and it is on [Flathub](https://flathub.org/apps/details/com.github.k4zmu2a.spacecadetpinball): see [Repology](https://repology.org/project/spacecadetpinball/versions).

### Windows

Install Visual Studio 2022 with the "Desktop development with C++" workload. It includes a recent enough CMake. Open the folder in Visual Studio, which picks up `CMakePresets.json`, or build from a terminal:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
```

SDL is downloaded and linked statically, so the executable needs no `SDL2.dll` or `SDL2_mixer.dll`. The `windows-*` presets build a GUI-subsystem executable with no console window. To see the console output, add `-DCMAKE_WIN32_EXECUTABLE=OFF`.

### Cross-compiling for Windows from Linux

Install a 64-bit MinGW with the `posix` threading variant (`x86_64-w64-mingw32-g++-posix`; on Debian and Ubuntu, the `g++-mingw-w64-x86-64-posix` package), then:

```bash
cmake --preset mingw
cmake --build --preset mingw
```

The preset uses `mingwcc.cmake` as its toolchain file, builds SDL from source and links everything statically, so no MinGW SDL packages are needed.

### macOS

Install Xcode or the Command Line Tools (`xcode-select --install`) and CMake 3.24 or newer.

- **Packaged app:** run `bash build-mac-app.sh` from the repository root. It builds the `macos` preset and produces `SpaceCadetPinball-<version>-mac.dmg`. See [deployment.md](deployment.md).
- **Manual build:** `cmake --preset macos && cmake --build --preset macos` builds a universal binary with SDL linked statically. To use SDL from Homebrew (`sdl2`, `sdl2_mixer`) or MacPorts (`libsdl2`, `libsdl2_mixer`) instead, configure without the preset and pass `-DSCP_VENDORED_SDL=OFF`. That build targets only the host architecture unless `CMAKE_OSX_ARCHITECTURES` is set.
- **Tested upstream with:** the earlier framework-based script on macOS Ventura (Apple Silicon) with Command Line Tools 14 and on a GitHub Intel runner with Xcode 13; manual builds on macOS Big Sur (Intel) and Monterey beta (Apple Silicon) with Xcode 13. CI builds the `macos` preset on a `macos-26` runner, but nobody has run the resulting app yet.
- **Prebuilt:** `brew tap draftbrew/tap && brew install --no-quarantine space-cadet-pinball`. `--no-quarantine` bypasses Gatekeeper for this install.

## Command-line options

| Flag | Effect |
| --- | --- |
| `-fullscreen` | start in full screen |
| `-demo` | start in demo mode |
| `-noaudio` | do not open an audio device |
| `-sw` | use the SDL software renderer instead of trying the hardware renderer first |
| `-reset` | reset all options to their defaults |
| `-quick` | skip loading sound effects and music |
| `-record <file>` | record the session to a replay file; see [replays.md](replays.md) |
| `-replay <file>` | play a replay back headless, check every frame, and exit; status 1 if play differs |

## Troubleshooting

- **No background music:** music is on by default (the original game had it off), but a saved `Music=0` in `imgui_pb.ini` from an earlier run keeps it off. Turn it on with F6 or Options → Music.
- **Still no music on Linux:** SDL_mixer plays MIDI through FluidSynth, which needs a SoundFont. Debian and Ubuntu builds find `/usr/share/sounds/sf2/FluidR3_GM.sf2` (package `fluid-soundfont-gm`) on their own. Elsewhere, point `SDL_SOUNDFONTS` at one before launching, for example `export SDL_SOUNDFONTS=/usr/share/soundfonts/FluidR3_GM.sf2` (Fedora).
- **"Could not create renderer" or a black window:** try `-sw`.
- **WSL2:** WSLg provides both a display and a PulseAudio device, so the game runs without extra setup and without `-noaudio`.
- **`SDL Error: Couldn't open timidity.cfg` in the console:** the main loop prints any pending SDL error, including errors that SDL_mixer recovered from internally. This line alone is not proof that music is broken.
- **"Could not load game data":** no data file was found. The message lists the folders searched; see [Game data](#game-data). When a file is found, the console prints `Loading game from: <path>`.
