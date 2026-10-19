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

The project uses C++20, CMake 3.24 or newer, and `SDL3` and `SDL3_mixer` (3.2 or newer each). `CMakePresets.json` has one preset per platform. Configure and build with the same preset name:

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

The `SCP_VENDORED_SDL` option chooses where SDL comes from. When it is `ON` (the default on Windows and macOS), CMake downloads SDL3 3.4.18 and SDL3_mixer 3.2.4 at configure time, checks them against pinned SHA-256 hashes, and links them statically. SDL_mixer is built with only what the game uses: WAV, and MIDI through Timidity. Its other codecs and FluidSynth are left out; the game brings its own MIDI playback (see [Music](#music)). When it is `OFF` (the default on Linux), CMake uses the installed `SDL3` and `SDL3_mixer` development packages through their CMake config files. CI builds every preset except `linux-debug` and `linux-sanitize` on each pull request. Windows builds are 64-bit only: the 32-bit build was dropped by decision, and the Windows XP build because its `v141_xp` toolset lacks full C++20 support.

Plain `cmake -S . -B build` without a preset still works and follows the same defaults.

Add `-DSCP_WARNINGS=ON` to the configure command to build the game with extra warnings, `-Wall -Wextra` or `/W4` with MSVC, and treat them as errors. Unused parameters are allowed, because overrides and callbacks keep the signatures their interfaces set. The option applies only to the game, not to SDL or ImGui, and is off by default, so a newer compiler with new warnings doesn't break a packager's build. CI turns it on. To see the warnings without failing the build, add `--compile-no-warning-as-error` to the configure command.

`SCP_SANITIZERS=ON`, which the `linux-sanitize` preset sets, builds the game and ImGui with AddressSanitizer and UndefinedBehaviorSanitizer. It needs GCC or Clang. Run the replays with this build after a change to memory handling (`tools/check-replays.sh`, see `docs/replays.md`). The window, renderer, sound effects and options are clean too. With SDL2_mixer and FluidSynth, the game used to crash on exit under ASan, because `Mix_Quit` unloaded FluidSynth while one of its threads still ran. Debian's SDL3_mixer has no FluidSynth; with a build that does (Arch or Homebrew, for example), preload it if the exit crashes. With a GCC build, the ASan runtime is a shared library and has to come first: `LD_PRELOAD="$(g++ -print-file-name=libasan.so) /usr/lib/x86_64-linux-gnu/libfluidsynth.so.3"`. Clang links its ASan runtime into the executable, so a Clang build preloads only FluidSynth. The FluidSynth path is the one on Debian and Ubuntu x86_64; other distributions put it elsewhere, and `ldconfig -p | grep libfluidsynth` shows where.

### Linux

SDL3 is packaged in Debian testing (forky) and sid, Ubuntu 26.04, Fedora and Arch, but SDL3_mixer only in Debian testing and sid and Arch. Where there is no SDL3_mixer package, Ubuntu 24.04 and 26.04 and Fedora among them, use the `linux-vendored` preset below.

```bash
# Debian testing or sid
sudo apt install cmake build-essential ninja-build libsdl3-dev libsdl3-mixer-dev fluid-soundfont-gm
# Arch
sudo pacman -S cmake ninja gcc sdl3 sdl3_mixer soundfont-fluid

cmake --preset linux
cmake --build --preset linux
./bin/SpaceCadetPinball
```

Distribution packages should keep `SCP_VENDORED_SDL=OFF`, the Linux default, so the game links against the system SDL. Ubuntu 22.04 ships CMake 3.22, which is too old: install a newer one from [Kitware's APT repository](https://apt.kitware.com/) or with `pip install cmake`.

**Music:** see [Music](#music); on Linux it needs a General MIDI SoundFont, such as Debian's and Ubuntu's `fluid-soundfont-gm` package.

Tested upstream with GCC 10 and Clang 11. The `linux` preset builds with GCC 15 and CMake 4.2. `cmake --install build/linux` installs the binary, the `.desktop` file, the AppStream metadata and the icons from `Platform/Linux/`.

The `linux-vendored` preset builds SDL from source instead and needs no SDL packages, only SDL's build dependencies. SDL loads X11, Wayland, PulseAudio, PipeWire and ALSA at run time, but it can only use the ones whose development headers were installed when it was built, and it stops configuring when a header for an X11 extension it wants is missing. On Debian and Ubuntu:

```bash
sudo apt install cmake build-essential ninja-build \
  libasound2-dev libpulse-dev libpipewire-0.3-dev libx11-dev libxext-dev libxrandr-dev \
  libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev \
  libwayland-dev libdecor-0-dev libegl-dev libgl-dev libdrm-dev libgbm-dev libudev-dev libdbus-1-dev
cmake --preset linux-vendored
cmake --build --preset linux-vendored
```

To leave out a missing X11 extension instead, pass its option, for example `-DSDL_X11_XTEST=OFF`.

Some distributions package the game, and it is on [Flathub](https://flathub.org/apps/details/com.github.k4zmu2a.spacecadetpinball): see [Repology](https://repology.org/project/spacecadetpinball/versions).

### Windows

Install Visual Studio 2022 with the "Desktop development with C++" workload. It includes a recent enough CMake. Open the folder in Visual Studio, which picks up `CMakePresets.json`, or build from a terminal:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
```

SDL is downloaded and linked statically, so the executable needs no `SDL3.dll` or `SDL3_mixer.dll`. The `windows-*` presets build a GUI-subsystem executable with no console window. To see the console output, add `-DCMAKE_WIN32_EXECUTABLE=OFF`.

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
- **Manual build:** `cmake --preset macos && cmake --build --preset macos` builds a universal binary with SDL linked statically. To use SDL from Homebrew (`sdl3`, `sdl3_mixer`) instead, configure without the preset and pass `-DSCP_VENDORED_SDL=OFF`. That build targets only the host architecture unless `CMAKE_OSX_ARCHITECTURES` is set.
- **Tested upstream with:** the earlier framework-based script on macOS Ventura (Apple Silicon) with Command Line Tools 14 and on a GitHub Intel runner with Xcode 13; manual builds on macOS Big Sur (Intel) and Monterey beta (Apple Silicon) with Xcode 13. CI builds the `macos` preset on a `macos-26` runner, but nobody has run the resulting app yet.
- **Prebuilt:** `brew tap draftbrew/tap && brew install --no-quarantine space-cadet-pinball`. `--no-quarantine` bypasses Gatekeeper for this install.

## Formatting and linting

`.clang-format` describes the existing Visual Studio style (tabs, braces on their own line, 120 columns), but the code isn't formatted as a whole: that would rewrite about 1,200 lines in 107 of 126 game files and make every merge from upstream conflict. Only the lines a change touches get formatted. `.clang-format-ignore` leaves out ImGui, `third_party/`, `EmbeddedData.cpp` and `translations.cpp`.

```bash
uvx --from clang-format==23.1.3 git-clang-format --staged   # format the staged lines before committing
uvx --from clang-format==23.1.3 git-clang-format main       # or the lines changed since main
```

`uvx` comes with [uv](https://docs.astral.sh/uv/) and fetches the pinned clang-format, so nothing else needs installing. `.clang-tidy` lists a small set of bug-prone and performance checks. CI's `lint` job runs both on the lines each pull request changes, with `tools/lint-changed.sh`; to run it locally, configure a build with a compile database first:

```bash
cmake -S . -B build/lint -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON
tools/lint-changed.sh main build/lint
```

The script checks the lines changed since the branch left `main` (their merge base), so commits that landed on `main` afterwards don't count; CI passes the base side of the pull request's merge commit. Without the build folder argument, it checks the formatting only. clang-tidy checks changed lines in `.cpp` files and in game headers; a header has no compile command of its own, so it is parsed with `pch.h` included first, as the build does.

The LLVM version is set once, in `tools/llvm-requirements.txt`: CI installs it from there with hashes, and the script reads it for `uvx`. To bump it, regenerate that file's hashes, then update the version in the `uvx` commands above and in `AGENTS.md`.

## Display

The game opens its window at the largest whole multiple of the table that matches the display's scale setting and fits on the display: 600×416 plus the menu at 100 %, twice that at 200 %, and so on. On a Retina or other high-density display it draws at the full pixel resolution. The menus and dialogs follow the display's scale, and Options → Graphics → UI Scale works on top of that.

Options → Graphics:

- **Integer Scaling** (on by default): the table is drawn at a whole multiple of its size, so every pixel of the original art becomes an even block. Off, it fills the window ("fit"), and each table pixel is still drawn the same size (SDL's pixel-art scaling, with SDL 3.4 or newer).
- **Uniform Scaling** (on by default): keeps the table's proportions. Off, it stretches to the window.
- **Linear Filtering** (off by default): smooths the table instead of keeping its pixels sharp.

Full screen (F4 or `-fullscreen`) uses the desktop's resolution, without a mode change. Settings saved by an earlier version keep their values, so a player who had Integer Scaling off or Linear Filtering on keeps that.

## Music

SDL3_mixer can't be relied on for the MIDI music: it has no native MIDI, and some distributions build it without MIDI. The game picks the first of these that works when it starts:

1. **Native MIDI** on Windows and macOS (vendored [SDL_native_midi](https://github.com/libsdl-org/SDL_native_midi)): the system's own synth, which is what the original game used.
2. **TinySoundFont** (vendored) with a SoundFont: the files in `SDL_SOUNDFONTS` (separated by `;`, or also `:` outside Windows), then `/usr/share/sounds/sf2/default-GM.sf2`, `/usr/share/sounds/sf2/FluidR3_GM.sf2`, `/usr/share/soundfonts/default.sf2`, `/usr/share/soundfonts/FluidR3_GM.sf2` and `/usr/share/sounds/sf2/TimGM6mb.sf2`. Only SF2 files load, not SF3. The whole SoundFont is held in memory as floating point, so FluidR3 (148 MB on disk) takes about 400 MB; TimGM6mb (6 MB) is lighter.
3. **SDL3_mixer's own MIDI decoders,** if it has any: Timidity in the vendored build (needs a `timidity.cfg` with GUS patches; set `TIMIDITY_CFG` if it isn't in `/etc/timidity.cfg` or `/etc/timidity/`), FluidSynth in some distribution builds.

No SoundFont ships with the game. Music needs an audio device, so `-noaudio` and `-quick` turn it off.

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
- **Still no music:** the console says which synth plays the music, `Playing music with SoundFont: <path>`, or why none could start. On Linux, install a General MIDI SoundFont or point `SDL_SOUNDFONTS` at one; see [Music](#music).
- **"Could not create renderer" or a black window:** try `-sw`.
- **WSL2:** WSLg provides both a display and a PulseAudio device, so the game runs without extra setup and without `-noaudio`.
- **An `SDL Error:` line in the console:** the main loop prints any pending SDL error, including errors that SDL or SDL_mixer recovered from internally. This line alone is not proof that music is broken.
- **"Could not load game data":** no data file was found. The message lists the folders searched; see [Game data](#game-data). When a file is found, the console prints `Loading game from: <path>`.
