# Releasing

How release builds are produced. Nothing is deployed to a server: a release is a set of zipped executables and a macOS DMG attached to a GitHub release. Game data is never included.

## Version number

The version appears in three places. Update all three together:

- `SpaceCadetPinball/winmain.h`: `winmain::Version`, shown in the About dialog and printed on startup
- `build-mac-app.sh`: `sw_version`, used in the DMG name and `Info.plist`
- `Platform/Linux/SpaceCadetPinball.metainfo.xml`: a new `<release>` entry for Flathub and distribution packages

## Windows

Run `BuildForWindows.ps1` from the repository root on Windows with Visual Studio 2022 installed. It builds the `windows-x86`, `windows-x64` and `windows-x86-xp` presets in turn (see [setup.md](setup.md#build)) and writes three zips to `bin/`. SDL is linked statically, so each zip holds only `SpaceCadetPinball.exe`:

- `SpaceCadetPinballx86Win.zip`
- `SpaceCadetPinballx64Win.zip`
- `SpaceCadetPinballx86WinXP.zip`, which needs the `v141_xp` MSVC toolset. Whether SDL 2.32 still builds and runs with it is unverified.

## macOS

Run `bash build-mac-app.sh` on a Mac. It builds the `macos` preset, which downloads checksum-pinned SDL sources and links them statically into a universal (`arm64` and `x86_64`) binary, and writes `SpaceCadetPinball-<version>-mac.dmg`.

The `Make Release Builds` GitHub workflow (`.github/workflows/ReleaseBuilds.yml`) runs the same script on a `macos-26` runner when triggered manually. It builds the branch it was triggered on and uploads the DMG as the `mac-build` artifact. It hasn't been run since the switch to presets, but the `Build` workflow builds the same `macos` preset on every pull request.
