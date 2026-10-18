# Releasing

How release builds are produced. Nothing is deployed to a server: a release is a set of zipped executables and a macOS DMG attached to a GitHub release. Game data is never included.

## Version number

The version appears in three places. Update all three together:

- `SpaceCadetPinball/winmain.h`: `winmain::Version`, shown in the About dialog and printed on startup
- `build-mac-app.sh`: `sw_version`, used in the DMG name and `Info.plist`
- `Platform/Linux/SpaceCadetPinball.metainfo.xml`: a new `<release>` entry for Flathub and distribution packages

## Windows

Run `BuildForWindows.ps1` from a Developer Command Prompt for Visual Studio, with the SDL packages in `Libs/` (see [setup.md](setup.md#windows)). It writes three zips to `bin/`, each with the executable plus `SDL2.dll` and `SDL2_mixer.dll`:

- `SpaceCadetPinballx86Win.zip`
- `SpaceCadetPinballx64Win.zip`
- `SpaceCadetPinballx86WinXP.zip`, which needs the `v141_xp` MSVC toolset

## macOS

Run `bash build-mac-app.sh` on a Mac. It downloads checksum-pinned SDL frameworks, builds a universal (`arm64` and `x86_64`) binary, and writes `SpaceCadetPinball-<version>-mac.dmg`.

The `Make Release Builds` GitHub workflow (`.github/workflows/ReleaseBuilds.yml`) runs the same script when triggered manually and uploads the DMG as the `mac-build` artifact. It checks out `master` and runs on `macos-12`.
