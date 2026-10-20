# Setup

How to build the game, where it looks for game data, and how to fix common problems.

## Game data

The game needs the original game data, which is not included. Copy the `.DAT` file and the sound and music files that came with it from an installation of either:

- 3D Pinball for Windows (`PINBALL.DAT`), or
- Full Tilt! Pinball (`CADET.DAT`, or `DEMO.DAT` from the demo).

3D Pinball for Windows came with Windows NT 4.0, Windows 2000, Windows Me and Windows XP, and before them with Microsoft Plus! for Windows 95. On an installed 32-bit Windows XP it is in (the x64 Edition shipped without it) `C:\Program Files\Windows NT\Pinball`. On the Windows XP CD its files are compressed (`PINBALL.DA_`, `SOUND1.WA_` and so on); unpack them with `expand` on Windows or `cabextract` on Linux and macOS. Full Tilt! Pinball, which has the same table, came on its own CD. Use a copy you own: the project doesn't distribute the data or point to downloads of it, and the web build only reads game files the player picks.

The game searches these folders in order and uses the first data file it finds:

1. the current working directory
2. the folder containing the executable
3. the user preferences folder (`SDL_GetPrefPath`, for example `~/.local/share/SpaceCadetPinball/` on Linux)
4. Linux and macOS only: `/usr/local/share/SpaceCadetPinball/`, then `/usr/share/SpaceCadetPinball/`

In each folder it tries `CADET.DAT`, then `PINBALL.DAT`, then `DEMO.DAT`, in upper case and then lower case. The "Prefer 3DPB Data" menu option swaps the first two.

3D Pinball keeps the bitmap font of its table messages not in `PINBALL.DAT` but in `PINBALL.EXE`, as a resource. Copy the executable too, into the same folder as the DAT file; the game looks for `PINBALL.EXE`, `pinball.exe` and `Pinball.exe` there and only reads the font out of it, it never runs it. Without it, or if it holds no font, the console says so and the messages are drawn in the ImGui font instead. Full Tilt! Pinball has its fonts in `CADET.DAT` and needs no executable.

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
| `web` | Linux or macOS with Emscripten, for browsers | built from source |

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

### Web

The `web` preset builds the game with [Emscripten](https://emscripten.org/) into one page: `bin/SpaceCadetPinball.html`, `.js` and `.wasm`. `tools/install-emsdk.sh <folder>` installs the pinned Emscripten SDK; set `EMSDK` to that folder before configuring.

```bash
tools/install-emsdk.sh ~/emsdk
export EMSDK=~/emsdk
cmake --preset web
cmake --build --preset web
```

To try it, copy the three files into a folder, rename the page to `index.html`, add `FluidR3Mono_GM.sf3` for music if you like, and serve the folder over HTTP (`python3 -m http.server`); browsers don't run it from a `file:` URL. The page asks for the game data with a file picker, including `PINBALL.EXE` for the message font. The files are kept in the browser's IndexedDB, so they are only asked for once, and are never uploaded; "Forget the game data" removes them. Settings and high scores are kept the same way.

- The game's loop yields to the browser after every frame (`-sASYNCIFY` and `emscripten_sleep`), so the browser shows the frame and delivers input in between. It runs without threads, because GitHub Pages can't send the headers that threads need.
- Music plays with TinySoundFont and the SoundFont the player adds with the game files (`.sf2` or `.sf3`), or else the free `FluidR3Mono_GM.sf3` that the page offers to download. It takes about 400 MB of memory once loaded.
- The browser lets the game start audio only after a click, so the game starts from the Play button.
- There is no Exit in the menu; closing the tab ends the game. Post-processing isn't available, because SDL's GPU renderer doesn't run in browsers.

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

## Editions

The game has two editions: **Space Cadet 2026**, the definitive edition, and **Classic**, the game as it was. Both play the same table with the same physics and rules. Space Cadet 2026 is the default, also for settings saved by an earlier version, and the first start shows a hint that names the key for switching. Tab switches at any time, also during a game, and so does **Options → Space Cadet 2026**; the key can be changed in Player Controls. The choice is saved.

So far the editions differ only in their own copy of the drawing options under Options → Graphics: Integer Scaling, Linear Filtering, Backdrop, Panel Under Table in Tall Windows, Upscaled Art and Upscaled Art Size. Each edition remembers its own, so switching changes them together and switching back restores them. Classic starts from the defaults below; Space Cadet 2026 starts with Upscaled Art on at 2×. Everything else, such as Uniform Scaling, Post-Processing, the frame rate, sound and controls, is shared.

## Display

The game opens its window at the largest whole multiple of the table that matches the display's scale setting and fits on the display: one table's size (600×416 for 3D Pinball, or the chosen Full Tilt resolution) plus the menu at 100 %, twice that at 200 %, and so on. On a Retina or other high-density display it draws at the full pixel resolution. The menus and dialogs follow the display's scale, and Options → Graphics → UI Scale works on top of that.

Options → Graphics:

- **Integer Scaling** (on by default): the table is drawn at a whole multiple of its size, so every pixel of the original art becomes an even block. Off, it fills the window ("fit"), and each table pixel is still drawn the same size (SDL's pixel-art scaling, with SDL 3.4 or newer).
- **Uniform Scaling** (on by default): keeps the table's proportions. Off, it stretches to the window.
- **Linear Filtering** (off by default): smooths the table instead of keeping its pixels sharp.
- **Backdrop** (on by default): fills the space around the table with a blurred, darkened copy of the table, made from the player's own data the first time it is shown, instead of black bars.
- **Upscaled Art** (off by default, F7 switches it at any time, also during a game): draws the table from upscaled copies of its art, made with [MMPX](https://casual-effects.com/research/McGuire2021PixelArt/) when the option is turned on, from the player's own data file. **Upscaled Art Size** picks 2× or 4×; the frame is then that much larger, so it looks best with the window at a matching multiple. Depth maps are scaled without smoothing, so sprites still overlap exactly as in the original. The upscaled art lives only in memory; nothing is written.
  - **Replacing sprites:** a PNG named after a sprite in the `sprites` folder of the user preferences folder (`~/.local/share/SpaceCadetPinball/sprites/` on Linux) replaces the generated copy of that sprite. A sprite's name is its group's number and name and the table resolution, such as `0108-ball-r0.png`; a component with several bitmaps, such as the ball with its seven sizes, adds the bitmap's index from the second on, so in 3D Pinball the ball's larger sizes are `0108-ball_1-r0.png` to `0108-ball_6-r0.png` (names such as `0109--r0.png`, which earlier versions used for those, still work). Full Tilt!'s `CADET.DAT` numbers its groups differently, with the ball at `0213-ball0`, so take the exact names from Export Original Sprites, below. A replacement must be exactly 2× or 4× the original's size, matching Upscaled Art Size. Pixels less than half opaque are transparent and the rest are drawn solid, because the table has no blending. **Upscaled Art Size → Export Original Sprites** writes every sprite of the current table resolution to `sprites/originals/`, under the names the game looks for, for example to feed an AI upscaler, and says how many it wrote. Replacing and exporting sprites need SDL 3.4 or newer; with an older SDL the export item is greyed out and replacement PNGs are ignored. These files come from the player's own data and are theirs to keep; don't share them.
- **Post-Processing** (None by default): **CRT** adds scanlines and an aperture grille, **Smooth Edges** rounds the steps of diagonal edges (EPX, the edge-directed scaler xBR grew out of). Both draw through SDL's GPU renderer, the only one that takes custom shaders, so they need SDL 3.4 or newer and Vulkan, Direct3D 12 or Metal. The game creates that renderer when it starts whenever it can, so switching effects is instant and never restarts the game. If the GPU renderer can't be created, the menu says so and the table is drawn plain. With SDL older than 3.4 the menu isn't there. Not with `-sw`.
- **Panel Under Table in Tall Windows** (on by default): in a tall window, such as a portrait screen, the score panel moves under the table when that lets the table be drawn larger. It needs Uniform Scaling.

Full screen (F4 or `-fullscreen`) uses the desktop's resolution, without a mode change. Settings saved by an earlier version keep their values, so a player who had Integer Scaling off or Linear Filtering on keeps that.

## Frame rate

Options → Graphics sets how often the table updates (UPS, 120 by default) and how often it is drawn (FPS, 60 by default). Updates are the physics: keep UPS at 120 to play as the original data was tuned. FPS can go higher than UPS for a high-refresh display: the table still updates 120 times a second, in equal steps, and the ball is drawn between its last two positions in the frames in between, which delays it by at most one update (8 ms). In this mode **Uncapped UPS** reads **Uncapped FPS**: it lifts the frame limit, and the table still updates at UPS. **Match Display Refresh Rate** (off by default) sets FPS to the display's refresh rate, also after the window moves to another display.

## Controls

Options → Player Controls (F8) binds a key, a mouse button and a gamepad button to each action. Gamepads work through SDL's gamepad database: by default the shoulder buttons are the flippers, A launches the ball, and the D-pad nudges the table. The analog triggers can be bound too: they count as pressed past half way and released below a quarter. **Options → Gamepad Rumble** (on by default) shakes the gamepad when a flipper fires and when the ball hits a bumper; the demo doesn't rumble.

On a touch screen, touching the left half of the table works the left flipper and the right half the right flipper, as long as the finger stays down. Touching the score panel pulls the plunger back, and lifting the finger launches the ball. Several fingers work at once. Touches elsewhere, such as the margins, do nothing to the table, and the menus take touches as mouse clicks.

## Music

SDL3_mixer can't be relied on for the MIDI music: it has no native MIDI, and some distributions build it without MIDI. The game picks the first of these that works when it starts:

1. **Native MIDI** on Windows and macOS (vendored [SDL_native_midi](https://github.com/libsdl-org/SDL_native_midi)): the system's own synth, which is what the original game used.
2. **TinySoundFont** (vendored) with a SoundFont: the files in `SDL_SOUNDFONTS` (separated by `;`, or also `:` outside Windows), then `/usr/share/sounds/sf3/default-GM.sf3`, `/usr/share/sounds/sf2/default-GM.sf2`, `/usr/share/sounds/sf2/FluidR3_GM.sf2`, `/usr/share/soundfonts/default.sf2`, `/usr/share/soundfonts/FluidR3_GM.sf2` and `/usr/share/sounds/sf2/TimGM6mb.sf2`. SF2 and SF3 (compressed) files both load; Debian's newer default `/usr/share/sounds/sf3/default-GM.sf3` comes first. The whole SoundFont is held in memory as floating point, so FluidR3 (148 MB on disk) takes about 400 MB (loading `FluidR3_GM.sf2` with SF3 support on peaks at 570 MB before settling back to 288 MB, because TinySoundFont keeps the raw copy while sizing its first float buffer); TimGM6mb (6 MB) is lighter.
3. **SDL3_mixer's own MIDI decoders,** if it has any: Timidity in the vendored build (needs a `timidity.cfg` with GUS patches; set `TIMIDITY_CFG` if it isn't in `/etc/timidity.cfg` or `/etc/timidity/`), FluidSynth in some distribution builds.

No SoundFont ships with the game. Music needs an audio device, so `-noaudio` and `-quick` turn it off.

## Git hooks

`lefthook.yml` defines Git hooks that catch mistakes before CI does. They are optional. Install [lefthook](https://lefthook.dev/install/) (for example `brew install lefthook`, `go install github.com/evilmartians/lefthook/v2@latest` or `pipx install lefthook`), then run `lefthook install` once in the repository.

| Hook | Checks |
| --- | --- |
| `pre-commit` | no staged game data, even with `git add -f`; no trailing whitespace or conflict markers in the staged changes, outside vendored code (ImGui, `third_party/`), the generated `PostEffectShaders.h` and `docs/reference/`, and not while concluding a merge; every staged `.cpp` and `.h` in `SpaceCadetPinball/` is listed in `CMakeLists.txt`, and every game `.cpp` starts with `#include "pch.h"` |
| `pre-push` | on Linux, when the push changes code or build files: builds the `linux` preset with `SCP_WARNINGS=ON` in `build/pre-push/` (it needs the SDL3 packages the preset does; CMake also finds them through `CMAKE_PREFIX_PATH`), then plays back the replays in `../replays` against `../gamedata` if both exist (`SCP_REPLAYS_DIR` and `SCP_GAMEDATA_DIR` override the paths) |

`pre-push` builds the working tree, not the commits being pushed: uncommitted changes are part of the build (it warns about them), and pushing another branch still tests the checked-out code. CI stays the gate. The first `pre-push` build compiles the whole game; later ones are incremental. It writes `bin/SpaceCadetPinball` like every preset. The scripts are in `tools/hooks/`. Skip the hooks once with `LEFTHOOK=0` or `--no-verify`. Personal changes go in `lefthook-local.yml`, which Git ignores.

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
