# Test builds plan

Date: 2026-10-30. Status: proposed. Decided 2026-10-30: test builds carry the game data (G0), are made on this machine and the Mac, and are never published; the other questions under Decisions wait for an answer.

Space Cadet 2026 has only been played on Linux, under Xvfb, with lavapipe's software Vulkan. This plan makes a build of `main` for each platform there is a device for (the web, Windows, macOS and Android), plays it on that device, and closes what the other plans left for the player's machine: items 2 and 3 of the [follow-ups plan](2026-10-19-follow-ups.md#needs-the-players-machine-or-account), and what Phase D of the [Space Cadet 2026 plan](2026-10-20-space-cadet-2026.md#phase-d-compositor-and-lighting) couldn't check (Direct3D 12, Metal and its hand-written shaders, Windows and macOS builds, multiball). The builds are for testing, not a release.

The test builds carry the game data, so a device needs nothing but the build (G0). They are made outside the repository and outside CI, and copied to the devices over USB or the local network, never through a cloud service; nothing with game data is committed, uploaded or attached to a pull request, as the workspace's rules say. The builds CI and Pages make stay without game data, and `tools/check-no-game-data.sh` still checks every folder they are packed from.

## Where each platform stands

| Platform | Build | Checked so far | Missing |
| --- | --- | --- | --- |
| Web | `web` preset; `Pages.yml` deploys it on every push to `main` | Built and played locally with emsdk in the T3 browser preview; Pages last deployed on 2026-10-21, before Actions stopped running, so the live site predates Phases C to F | Saving across a reload (follow-ups item 2), a phone's browser, touch on a real screen |
| Windows | `windows-x64` (MSVC) and `mingw`; `BuildForWindows.ps1` packs a zip | Built with MSVC on this machine's Windows host and run from WSL; CI's `Build` workflow built both presets until 2026-10-21 | Direct3D 12 (compositor, post-processing), native MIDI, the folder dialog, a gamepad, a whole game on real hardware |
| macOS | `macos` preset, universal; `build-mac-app.sh` makes a DMG, `ReleaseBuilds.yml` runs it on `macos-26` | CI built the preset until 2026-10-21; the DMG script hasn't run since the switch to presets | Metal and the hand-written `.msl` shaders, which have never run; native MIDI; the folder dialog; Gatekeeper |
| Android | None | Touch controls (web) and the portrait layout (E1) exist; native Android was deferred on 2026-10-19 ([modernization plan](2026-10-18-modernization.md)) | Everything: a Gradle project, packaging, getting the game data in, the app's lifecycle, music without native MIDI |

## Decisions

Each comes with a recommendation; none is decided yet.

Decided 2026-10-30:

- **Test builds carry the game data:** one exe for Windows, one DMG for macOS, one APK for Android and one folder for the web, each with all four data sets in it (G0).
- **Windows's exe has the data inside,** not beside it in a folder.
- **The Mac builds its own DMG,** with a script run on it through T3 (`simons-macbook-pro14`); the source comes from Git and the data over the local network.
- **The builds stay on the devices,** so there is no release for them, and decision 2 below is answered.

Still open:

1. **Whether CI builds again.** The test builds don't need it, since G0 makes them on this machine and the Mac. But GitHub Actions has been off on the fork since about 2026-10-25, so nothing since #61 was built for Windows, macOS or the web by CI, and Pages still serves the build of 2026-10-21.
   - **Recommended: turn Actions back on** (a repository setting only the owner can change). `Build` then checks every preset on each pull request again, Pages redeploys the web build, and `ReleaseBuilds.yml` can later make release builds without game data.
   - Otherwise every check stays local, as the hooks and the replays already are.
2. **How the builds reach the devices:** answered by G0, by hand.
3. **Signing.** **Recommended: none for testing.** The DMG is ad-hoc signed, so macOS asks once (Open from the context menu, or `xattr -d com.apple.quarantine`); a Developer ID and notarisation wait for a release. The APK is signed with a debug key and sideloaded. Windows SmartScreen warns for an unsigned exe.
4. **What the builds call themselves.** Today `winmain::Version` says `2.1.1 DEV`. **Recommended:** keep it, and let each test build print its commit (`git describe`) in the About box and its startup line, so a report names the build it came from. The version for a release is decided with the release.
5. **Android's scope.** **Recommended:** arm64 phones and tablets only, the oldest Android version SDL3 supports, both orientations (portrait uses E1's layout), and the game data brought in as the web build does: a file picker copies the chosen files into the app's private storage, where nothing else can read them, and Settings can remove them again. Which Android version the device runs decides whether that is enough; please name it.

## Steps

One pull request each, with a checkpoint after each that stops for review, as in Phase F.

1. **G0: test builds with the game data.** `tools/make-test-build.sh <platform>` with `web`, `windows`, `macos` and later `android`, given the four data folders (3D Pinball, Full Tilt!'s Space Cadet, Dragon's Keep, Skulduggery; by default the workspace's `../gamedata*`). It puts their files into one layout the game already finds: 3D Pinball's files at the top (with `Pinball.exe` for the font), Full Tilt!'s tables in `CADET`, `DRAGON` and `PIRATE` with their `SOUND` folders, leaving out the files the game doesn't read. It writes the build to `../testbuilds/<commit>/` and builds in `../testbuilds/build/`, refuses either inside the repository, and refuses to run in CI. The repository gets only the script and the code below, never data.
   - **Windows:** a CMake option, `SCP_BUNDLED_DATA`, off by default, names that layout. The build packs it into the exe, as `cmake/EmbedFonts.cmake` embeds the fonts; at its first start the game unpacks it into the preferences folder, under a name made from the data's hash, and searches it after the player's chosen folder. Built here with MSVC on the Windows host, or with `mingw`.
   - **Web:** the same option makes Emscripten preload the layout from a `.data` file next to the `.wasm`, and the page skips its file picker when the files are there. A folder to serve over HTTP (the script prints how), since a browser won't run the `.wasm` from `file://`.
   - **macOS:** no option needed; the script copies the layout into the app's `Contents/Resources`, which `SDL_GetBasePath` returns and the game already searches, and runs `build-mac-app.sh`'s steps, ad-hoc signing the app after the data is in. It runs on the Mac.
   - **Android:** with G5, the layout goes into the APK's assets and is copied into the app's storage at the first start.
   Each build prints its commit at startup and in the About box. Done when each build starts on a machine without game data and finds all four tables.
2. **G1: builds from CI** (if decision 1 turns Actions on). Check that `Build` and `Pages` run green on `main`; add Windows, web and, after G5, Android jobs to `ReleaseBuilds.yml` beside macOS, pinned by commit SHA as the existing steps are, each running `tools/check-no-game-data.sh` on what it uploads. Without Actions, G1 is a short `docs/deployment.md` section on making the four builds by hand.
3. **G2: web.** Check G0's web folder in a desktop browser and on the phone: all four tables without loading files, then loading the player's own files in a build without them, saving across a reload (follow-ups item 2), touch, a gamepad, and the Definitive features the web build has.
4. **G3: Windows.** G0's exe on the Windows device: the compositor's lighting, glow and shaded ball on Direct3D 12, the post-processing switch from CRT to Smooth Edges and back during a game (follow-ups item 3), multiball with the compositor on, native MIDI, the folder dialog of the first-run screen, a gamepad.
5. **G4: macOS.** G0's DMG on the Mac: the same list on Metal, which runs the hand-written `.msl` shaders for the first time, on the Mac's own architecture of the universal binary.
6. **G5: Android.** Starts with a spike that builds an APK and loads a table, and sizes the rest; then one pull request per piece:
   - a Gradle project in `Platform/Android/` from SDL3's `android-project` template, building the game through CMake with the vendored SDL, and an `android` preset or script;
   - the game data: G0's in the test APK, and the player's own as decision 5 settles it; `pb::FindTables` then finds the tables in the app's storage as it does in a data folder;
   - the lifecycle: pause on going to the background, and save the settings and high scores then, since Android may end the process without a quit; the back button as Escape; the display cutout through `SDL_GetWindowSafeArea`; full screen;
   - music: there is no native MIDI, so TinySoundFont plays it with the MIT `FluidR3Mono_GM.sf3` the web build already downloads and checks, packed into the APK;
   - rendering: the compositor on SDL's GPU renderer where the device has Vulkan, and the plain frame where it hasn't, as on any machine without a GPU renderer.
   The replays can't be checked on the phone with `tools/check-replays.sh`, and ARM's float maths may drift from the Linux recordings as Windows' does; the simulation code is the same, so the replays stay checked on Linux.
7. **G6: the test pass.** The checklist below on each device, with the results written into this plan; each problem found becomes its own pull request, and a fix to play asks first, as always.

G0 comes first; G2 to G4 then need nothing else and can run in any order, and G1 can come at any time. G5 is the bulk of the work. The F6 follow-ups in the Space Cadet 2026 plan come first, so the builds tested have them.

## Checklist for each device

- The test build finds each table without any files copied: 3D Pinball's Space Cadet, Full Tilt!'s Space Cadet if its data is there, Dragon's Keep and Skulduggery, switched from the Table screen and back. Once per platform, a build without data: the first-run screen (desktop) or the import (web, Android) with the player's own files.
- A whole game of each table in Space Cadet 2026, with Tab to Classic and back during it; the HUD, the Mission Guide and the pause screen.
- Lighting, lamp glow and the shaded ball at the device's resolution, a multiball, and Upscaled Art; the post-processing effects.
- Portrait (phone upright, a turned monitor) and the screen rotation option where the device has it.
- Accessibility: text size, the high-contrast HUD, the colour choices, Reduced Motion.
- Sound: effects, music changing track, room reverb, wider stereo.
- Input: keyboard, a gamepad, touch where there is a screen; rebinding a key.
- Settings, high scores, stats and achievements still there after quitting and starting again.
- Recordings: record a session and play it back to its end and from a point on the timeline.

Done when each of the four builds has run the checklist on its device, its failures have pull requests or are written down here as known, follow-ups items 2 and 3 are closed, and Phase D's unchecked items are checked.
