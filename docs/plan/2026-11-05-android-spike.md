# Android spike (G5)

Date: 2026-11-05. Status: an APK builds from this repository and from `tools/make-test-build.sh android`, with all four data sets in it, signed with the workspace's test key. **It hasn't run on the Galaxy A54 yet:** the phone wasn't connected on the day, so the frame rate, Vulkan on the Mali-G68 and everything else that needs the device are still open (below). The spike code is on `spike/android` and isn't meant to merge as it is; the pull requests at the end split it up. G5 is in the [test builds plan](2026-10-30-test-builds.md).

## What the spike does

- **The Gradle project, `Platform/Android/`:** made from SDL3's `android-project` template, with AGP 8.13.2, the Gradle 8.14.3 wrapper (its SHA-256 pinned), `compileSdk` and `targetSdk` 36, `minSdk` 21 (SDL3's own, as the template has it), NDK 29.0.14206865, CMake 4.1.2, arm64-v8a only. It builds the repository's own `CMakeLists.txt`, with the vendored SDL, through `externalNativeBuild`. Nothing about the native build is duplicated in Gradle.
- **SDL's Java half matches its native half:** Gradle reads SDL's URL and SHA-256 out of `CMakeLists.txt`, downloads and checks the tarball once, adds its `android-project/app/src/main/java` to the sources and hands the unpacked tree to CMake as `FETCHCONTENT_SOURCE_DIR_SDL3`. Bumping SDL in `CMakeLists.txt` bumps the Java sources with it, and nothing of SDL is copied into the repository.
- **The game is `libmain.so`:** on Android, `CMakeLists.txt` makes the game a shared library named `main`, with SDL3 and SDL3_mixer linked statically into it; `SpaceCadetPinball.cpp` includes `SDL_main.h` there, which turns `main` into the `SDL_main` the activity calls. `SpaceCadetActivity` extends `SDLActivity` and loads only `main`.
- **Logs:** Android drops stdout, so on Android `main` pipes stdout and stderr to logcat, tag `SpaceCadetPinball`. All the game's `printf` progress lines reach `adb logcat -s SpaceCadetPinball SDL`.
- **Back as Escape:** the manifest opts into predictive back (`enableOnBackInvokedCallback="true"`, Android 16's default for an app that targets it; the `false` opt-out still works there, but is deprecated), and the activity registers an `OnBackInvokedCallback` that sends Escape to SDL, so the gesture opens the pause screen instead of closing the app. Before API 33, SDL's view takes the back key itself and turns it into `SDLK_AC_BACK`, which the game ignores, so `onBackPressed` never runs; there the activity's `dispatchKeyEvent` makes a non-mouse back key Escape too. SDL's template had opted out (`false`) and lets the system finish the activity.
- **Data from the APK:** `make-test-build.sh android` puts G0's layout into the APK's assets under `bundled/`, with `bundled.txt` listing the files under the same hash of names and contents the Windows build computes. On Android `BundledData::Folder` reads that list through `SDL_LoadFile` (which, for a relative path, looks in the app's `files/` first and then in the assets), copies the files into `SDL_GetPrefPath`, the app's private `files/`, as `bundled-<hash>`, by way of a `.part` folder, and returns it; the game then searches it as on Windows. An APK without the list returns nothing, which is where the picker comes in later.
- **Music:** the MIT `FluidR3Mono_GM.sf3` and its licence, pinned by SHA-256 as `Pages.yml` and `make-test-build.sh web` pin them, go into every APK's assets; `midi.cpp` tries it on Android after any `SDL_SOUNDFONTS`, by its asset name, through the same `SDL_LoadFile` it already uses.
- **Signing:** one test keystore, made once in `../android-keystore/` (PKCS12, RSA 3072, alias `test`, certificate SHA-256 `E9:5C:B7:C5:…:C7:61`), passed to Gradle as `scpSigning`; the workspace `AGENTS.md` lists the folder.
- **Version:** `versionName` is `2.1.1 DEV` and `git describe`, `versionCode` the commit count, passed as Gradle properties by `make-test-build.sh`, since it builds from a `git archive` copy without `.git`, or read from git for a build in the checkout. `2.1.1 DEV` comes from `winmain::Version`. Only `make-test-build.sh`'s `scpDescribe` sets `SCP_BUILD_COMMIT`, so only a test APK calls itself a test build in the About box. On this branch that reads `2.1.1 DEV Release_2.1.0-919-g<commit>`, versionCode 1192.

## What was checked without the phone

- The release APK verifies with `apksigner` against the test key; `zipalign -c -P 16` passes and `libmain.so`'s LOAD segments are aligned to 16 KB (`0x4000`), so it runs on 16 KB-page devices. NDK r29 does this by default.
- `libmain.so` exports `SDL_main`, `JNI_OnLoad` and SDL's 67 `Java_org_libsdl_app_*` functions, so the static SDL kept its JNI half. It needs only system libraries (`liblog`, `libandroid`, `libOpenSLES`, `libGLESv1_CM`, `libGLESv2`, `libdl`, `libm`, `libc`); SDL loads Vulkan at run time.
- SDL's Android configuration has the GPU renderer and its Vulkan backend (`SDL_VIDEO_RENDER_GPU`, `SDL_GPU_VULKAN`), OpenGL ES 2 for the fallback, and AAudio with OpenSL ES for sound.
- The game compiles for Android with `SCP_WARNINGS=ON` (`-PscpWarnings`) without a warning. The only warning in the build is in vendored `stb_vorbis.c` (`-Wtautological-compare`), which isn't part of the game target and isn't an error.
- The Linux build (`linux` preset, `SCP_WARNINGS=ON`) still builds, and the replays match (checked before the push, below).

## What broke and how the spike got past it

Nothing failed outright: the first Gradle build linked. What needed a decision:

- **The template's back handling is wrong for Android 16:** it opts out of predictive back and lets the system finish the activity. Replaced by the callback above. Whether Android 16 on One UI still plays its "back to home" animation before the callback runs has to be seen on the phone.
- **stdout goes nowhere on Android,** which hides every line the game logs. Fixed with the logcat pipe.
- **SDL's Java and native sources must be the same release,** or the JNI calls don't match. Taken from the one pinned tarball, as above.
- **Assets aren't files,** and the game opens its data with `fopen`. Copied to private storage at the first start, as the Windows test build unpacks its resource.
- **`make-test-build.sh` builds from a `git archive` copy,** which has no `.git` for `versionCode`; the script passes the version as properties, and Gradle's build folder is kept in `../testbuilds/build/android-gradle`, so the SDL and SoundFont downloads survive the fresh copy.
- **`minSdk` 21 makes AGP compress `libmain.so`** (legacy packaging, extracted at install), where `minSdk` 23 or newer would store it uncompressed and map it straight from the APK. It saves 3.6 MB of APK but costs 6 MB on the device; a question below.

## Measured so far

| What | Value |
| --- | --- |
| APK without data (release, unsigned) | 13.6 MB: `libmain.so` 6.2 MB (2.6 MB compressed), the SoundFont 14.6 MB (10.9 MB compressed), `classes.dex` 100 KB |
| Test APK with all four data sets | 23.8 MB, 227 files |
| Build from clean (SDL downloaded) | about 55 s on this machine for `make-test-build.sh android` |
| Frame rate on the A54 | **not measured** (no phone) |
| Vulkan compositor or fallback | **not known** (no phone); both are compiled in |

## Still to check on the phone

When the A54 is connected: `adb install -r` the test APK, start it, and read `adb logcat -s SpaceCadetPinball SDL`.

1. The first start copies the data (expected: `Bundled game data: /data/user/0/…/files/bundled-677e7f0213eea1e1/`) and the Table list shows all four tables.
2. The renderer: the `CreateRenderer` line names `gpu`, and Space Cadet 2026's compositor runs; else the plain frame.
3. A table plays: flippers and plunger by touch (the web build's touch zones), sounds and music.
4. Frame rate over a minute of play, from the game's own counter.
5. Back opens the pause screen and doesn't close the app (the A54 covers API 33 and up; the `dispatchKeyEvent` path for older versions needs an older device); going home and back resumes; rotation doesn't restart it.
6. Where the HUD and touch zones sit against the camera cutout and the gesture bar, with Android 16's forced edge to edge.

## The rest of G5, as pull requests

In order; each is small enough to review on its own. Sizes: S is up to a day, M a few days, L about a week.

1. **build(android): Gradle project and `libmain.so`** (S, mostly from this spike). `Platform/Android/` as on the spike, the `CMakeLists.txt` shared-library target, `SDL_main` and the logcat pipe, `.gitignore`, `AGENTS.md` and `docs/setup.md` (building for Android). No `android` CMake preset: Gradle drives CMake, and a preset would duplicate the NDK toolchain arguments AGP passes; a CMake preset can come later if lint needs it.
2. **feat(android): the test build's data** (S, from this spike). `BundledData` on Android, `make-test-build.sh android`, `docs/deployment.md`. Needs one run on the phone first.
3. **ci(android): an Android job** (S). In `Build.yml`, beside the other presets: Java 17, the SDK's NDK and CMake from `sdkmanager`, `./gradlew assembleRelease -PscpWarnings`, no data and no key (the unsigned APK), actions pinned by SHA, `tools/check-no-game-data.sh` on the APK's unpacked contents, since it can't see inside the zip. G1 adds it to `ReleaseBuilds.yml` later. Can go right after 1.
4. **feat(android): lifecycle** (M, needs the phone). Save the settings, high scores and stats on `SDL_EVENT_WILL_ENTER_BACKGROUND` and pause the game, since Android may end the process without a quit; back as Escape (on the spike already) and what back does on the pause screen; full screen and the forced edge to edge: the HUD, the menus and the touch zones inside `SDL_GetWindowSafeArea`, updated on `SDL_EVENT_WINDOW_SAFE_AREA_CHANGED`; both orientations, with E1's portrait layout; hide the Exit item as on the web, since Android closes apps itself.
5. **feat(android): rendering** (S to M, needs the phone). Confirm the compositor on Vulkan on the Mali-G68 and the plain frame where SDL's GPU renderer can't start (forced with `-sw` or a hint, to test the path); fix what the Mali driver shows. If the GPU renderer fails, this is where it gets bigger.
6. **feat(android): music** (S, needs the phone). The SoundFont is in already; check the music plays, its latency through AAudio, and the CPU the synth takes on the A54. A smaller SoundFont only if the size question below says so.
7. **feat(android): the player's own files** (M to L). The later picker PR, for an APK without data. SDL 3.4.18's Android dialog opens files but not folders (`SDL_FILEDIALOG_OPENFOLDER` is `SDL_Unsupported()` in `SDL_androiddialog.c`). Two ways: `SDL_ShowOpenFileDialog` with several files allowed, whose `content://` URIs `SDL_IOFromFile` reads, so the copy needs no Java, but the player picks the files of each table, and a table's `SOUND` folder can't be picked as a folder; or `ACTION_OPEN_DOCUMENT_TREE` from the activity, which takes a whole folder, with a little Java that walks it through the content resolver. Either way the files are copied into private storage, `pb::FindTables` finds them there, Settings deletes them again, and no storage permission is needed. The tree picker is the better fit for the four layouts; it's the bigger PR.
8. **feat(android): flipper vibration** (S, optional). A setting in `optionsStruct` and a short vibration on a flipper through the activity's `Vibrator`; the idea is from fexed/Pinball-on-Android, written anew.

The replays stay checked on Linux; the simulation code is the same on ARM.

## Questions for the user

1. **Application ID:** the spike uses `io.github.simonsteiner.spacecadetpinball`. Keep it? Changing it later means a new app on the phone, without the old one's settings and scores.
2. **`minSdk`:** keep SDL3's 21, or raise it to 23 or 26, so `libmain.so` stays uncompressed and mapped from the APK (13.6 MB becomes about 17 MB, but 6 MB less on the device), and older Android versions nobody tests drop out?
3. **SoundFont size:** at 10.9 MB it's 80 % of the APK without data. Keep FluidR3Mono, which the web build uses too, or look for a smaller MIT or public-domain General MIDI SoundFont?
4. **The player's files (PR 7):** the tree picker (a whole folder, a little Java) or SDL's multi-file picker (no Java, but each table's files one by one)? The spike's recommendation is the tree picker.
5. **The Gradle wrapper's `gradle-wrapper.jar`** is a 44 KB binary in the repository, as every Gradle project has it; its distribution's SHA-256 is pinned. Fine to commit, or should a script fetch it?
6. **Phone run:** the spike needs one session with the A54 connected, to fill in the frame rate, Vulkan and what the screen shows, before PR 2 and the phone-bound PRs start.
