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

The project uses C++11, CMake and the `SDL2` and `SDL2_mixer` development packages. The executable is written to `bin/` in the source tree (`bin/<Config>/`, such as `bin/Release/`, with Visual Studio).

### Linux

```bash
# Debian and Ubuntu
sudo apt install cmake build-essential ninja-build libsdl2-dev libsdl2-mixer-dev libsdl2-mixer-2.0-0 libsdl2-2.0-0 fluidsynth
# Fedora
sudo dnf install cmake ninja-build SDL2 SDL2-devel SDL2_mixer SDL2_mixer-devel fluidsynth fluidsynth-libs mscore-fonts g++

cmake -S . -B build -G Ninja
cmake --build build
./bin/SpaceCadetPinball
```

Tested upstream with GCC 10 and Clang 11. It also builds with GCC 15 and CMake 4.2. CMake warns that compatibility with versions before 3.10 is deprecated, and GCC warns about right-to-left marks in `translations.cpp`; both are harmless. `cmake --install build` installs the binary, the `.desktop` file, the AppStream metadata and the icons from `Platform/Linux/`.

Some distributions package the game, and it is on [Flathub](https://flathub.org/apps/details/com.github.k4zmu2a.spacecadetpinball): see [Repology](https://repology.org/project/spacecadetpinball/versions).

### Windows

1. Download the Visual C++ development packages for `SDL2` and `SDL2_mixer` and unpack them to `Libs/SDL2` and `Libs/SDL2_mixer` (the paths `CMakeLists.txt` expects).
2. Open the folder in Visual Studio (tested upstream with 2019), which picks up `CMakeSettings.json`, or build from a Developer Command Prompt:

   ```powershell
   cmake -S . -B build -A x64
   cmake --build build --config Release
   ```

The build copies `SDL2.dll` and `SDL2_mixer.dll` next to the executable.

### Cross-compiling for Windows from Linux

Install a 64-bit MinGW (the `posix` threading variant, `x86_64-w64-mingw32-g++-posix`) and its `SDL2` and `SDL2_mixer` builds under `/usr/x86_64-w64-mingw32`, then:

```bash
cmake -S . -B build-mingw -DCMAKE_TOOLCHAIN_FILE=mingwcc.cmake
cmake --build build-mingw
```

MinGW builds link statically.

### macOS

Install Xcode or the Command Line Tools (`xcode-select --install`) and CMake.

- **Packaged app:** run `bash build-mac-app.sh` from the repository root. It downloads the SDL frameworks into `Libs/` and produces `SpaceCadetPinball-<version>-mac.dmg`. See [deployment.md](deployment.md).
- **Manual build:** install SDL with Homebrew (`sdl2`, `sdl2_mixer`) or MacPorts (`libsdl2`, `libsdl2_mixer`), then build with CMake as on Linux. `CMakeLists.txt` hard-codes `CMAKE_OSX_ARCHITECTURES` to `arm64;x86_64`, which overrides a `-D` on the command line. Single-architecture Homebrew libraries need that line edited.
- **Tested upstream with:** the script on macOS Ventura (Apple Silicon) with Command Line Tools 14 and on a GitHub Intel runner with Xcode 13; manual builds on macOS Big Sur (Intel) and Monterey beta (Apple Silicon) with Xcode 13.
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

## Troubleshooting

- **No background music on Linux:** MIDI playback needs a SoundFont. Point `SDL_SOUNDFONT` at one before launching, for example `export SDL_SOUNDFONT=/usr/share/sounds/sf2/FluidR3_GM.sf2` (Debian and Ubuntu) or `/usr/share/soundfonts/FluidR3_GM.sf2` (Fedora).
- **"Could not create renderer" or a black window:** try `-sw`.
- **WSL2:** WSLg provides both a display and a PulseAudio device, so the game runs without extra setup and without `-noaudio`.
- **`SDL Error: Couldn't open timidity.cfg` in the console:** the main loop prints any pending SDL error, including errors that SDL_mixer recovered from internally. This line alone is not proof that music is broken.
- **"Could not load game data":** no data file was found. The message lists the folders searched; see [Game data](#game-data). When a file is found, the console prints `Loading game from: <path>`.
