# Releasing

How release builds are produced. A release is a set of zipped executables and a macOS DMG attached to a GitHub release; the web build is published to GitHub Pages. Game data is never included in either.

## Version number

The version appears in three places. Update all three together:

- `SpaceCadetPinball/winmain.h`: `winmain::Version`, shown in the About dialog and printed on startup
- `build-mac-app.sh`: `sw_version`, used in the DMG name and `Info.plist`
- `Platform/Linux/SpaceCadetPinball.metainfo.xml`: a new `<release>` entry for Flathub and distribution packages

## Windows

Run `BuildForWindows.ps1` from the repository root on Windows with Visual Studio 2022 installed. It builds the `windows-x64` preset (see [setup.md](setup.md#build)) and writes `bin/SpaceCadetPinballx64Win.zip`. SDL is linked statically, so the zip holds only `SpaceCadetPinball.exe`. There are no 32-bit or Windows XP builds.

## macOS

Run `bash build-mac-app.sh` on a Mac. It builds the `macos` preset, which downloads checksum-pinned SDL sources and links them statically into a universal (`arm64` and `x86_64`) binary, and writes `SpaceCadetPinball-<version>-mac.dmg`.

The `Make Release Builds` GitHub workflow (`.github/workflows/ReleaseBuilds.yml`) runs the same script on a `macos-26` runner when triggered manually. It builds the branch it was triggered on and uploads the DMG as the `mac-build` artifact. It hasn't been run since the switch to presets, but the `Build` workflow builds the same `macos` preset on every pull request.

## Web (GitHub Pages)

`.github/workflows/Pages.yml` builds the `web` preset on every push to `main` (and when triggered by hand) and publishes it to GitHub Pages. The site holds `index.html`, `SpaceCadetPinball.js`, `SpaceCadetPinball.wasm`, and the free `FluidR3Mono_GM.sf3` SoundFont with its licence file, both downloaded from MuseScore's v2.3.2 release and checked against pinned SHA-256 hashes. `tools/check-no-game-data.sh` fails the job if anything that looks like game data is in the folder, including a test build's `.data` file. Players add their own game data in the browser.

Pages has to be turned on once: in the repository's Settings → Pages, set **Source** to **GitHub Actions**. Until then the `deploy` job fails, and the `build` job still checks that the site builds. The `Build` workflow builds the `web` preset on every pull request too.

## Test builds

Test builds carry the game data of all four tables, so a device needs nothing but the build ([test builds plan](plan/2026-10-30-test-builds.md), G0). They hold Microsoft's files, so they are made only on the developer's machine, copied to the devices over USB or the local network, and never committed, uploaded, attached to a pull request or made in CI. `tools/make-test-build.sh <platform>` makes one from the committed `HEAD`; uncommitted changes are not in it. It refuses to run when `CI` is set.

The data comes from the workspace's `../gamedata` (3D Pinball), `../gamedata-fulltilt`, `../gamedata-dragon` and `../gamedata-skulduggery`; `SCP_DATA_3DPB`, `SCP_DATA_FULLTILT`, `SCP_DATA_DRAGON` and `SCP_DATA_SKULDUGGERY` point it elsewhere, or leave a table out when empty. The script copies only what the game reads into one layout: both Space Cadets at the top, Dragon's Keep in `DRAGON` and Skulduggery in `PIRATE`, each with its DAT file, its picture, the WAV files its DAT file names and the music `midi.cpp` loads, in upper case, and 3D Pinball's `PINBALL.EXE` for its message font. Full Tilt!'s executables, 3D Pinball's `FONT.DAT`, `PINBALL2.MID`, `table.bmp` and `wavemix.inf`, and the sounds and music no table loads stay out. Full Tilt!'s Space Cadet is not in a `CADET` folder because the game looks in the top folder first and would find 3D Pinball's there; with both at the top, the Table list shows one Space Cadet, Full Tilt!'s, and "Prefer 3D Pinball's data to Full Tilt's" in Settings → Edition (Classic: Options → Game Data → Prefer 3DPB Data) switches it to 3D Pinball's.

The build goes to `../testbuilds/<commit>/<platform>/`, and is built in `../testbuilds/build/`; `SCP_TESTBUILDS` moves both, and the script refuses either inside a Git repository. `SCP_CMAKE_ARGS` adds configure arguments, such as `-DSCP_WARNINGS=ON`. A test build's version reads `2.1.1 DEV, test build <git describe>`, at startup, in the About box and in its recordings, so a report names the build it came from.

- **Windows:** `tools/make-test-build.sh windows` cross-compiles with MinGW (`x86_64-w64-mingw32-gcc` on `PATH`, a version with C++23's `<print>`); `--msvc` builds from WSL with the Windows host's newest Visual Studio, in `%TEMP%\scp-testbuild`, which keeps a copy of the data until it is deleted. The CMake option `SCP_BUNDLED_DATA` names the layout, which the build packs into the executable as a resource. Its first start unpacks it into the preferences folder, `%APPDATA%\SpaceCadetPinball\bundled-<hash>`, named after the data, so later builds with the same data use the same copy; the game searches it right after the Game Data Folder the player chose. The result is `SpaceCadetPinball.exe`, about 24 MB.
- **Web:** `tools/make-test-build.sh web`, with `EMSDK` set as for the `web` preset. `SCP_BUNDLED_DATA` makes Emscripten pack the layout into `SpaceCadetPinball.data`, next to the `.wasm`, which the page loads into `/bundled` before the game starts; the page then shows a note instead of asking for files, and a build without the option asks as before. The folder holds `index.html`, the game, the `.data` file and the SoundFont with its licence, pinned as `Pages.yml` pins them. Serve it over HTTP, since browsers don't run it from a `file:` URL: `python3 -m http.server --directory ../testbuilds/<commit>/web 8000`, and for a phone add `--bind` with this machine's address on the local network. `tools/check-no-game-data.sh` fails on a `.data` file, so a test build can't go out through `Pages.yml`.
- **macOS:** `tools/make-test-build.sh macos`, on a Mac, with Xcode's command-line tools and CMake. It builds the `macos` preset and makes the app as `build-mac-app.sh` does, with the layout copied into `Contents/Resources`, the folder `SDL_GetBasePath` returns for an app, which the game already searches; no build option is needed. It signs the app ad hoc after the data is in and writes `SpaceCadetPinball-<commit>-mac.dmg`. Without a Developer ID, macOS asks once at the first start (Open from the context menu, or `xattr -d com.apple.quarantine` on the app). The script runs with macOS's bash 3.2 and BSD tools.
