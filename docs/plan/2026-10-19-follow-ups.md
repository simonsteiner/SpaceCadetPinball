# Follow-ups after the modernization stack

Date: 2026-10-19. Status: open. The modernization stack (#8, #10 to #22) and its review fixes are merged into `main`; [the modernization plan](2026-10-18-modernization.md) records what was done. This plan collects what the reviews left open: items that need hardware, a decision or an outside party, and small gaps found while fixing.

Each item keeps the replays matching. One that would change gameplay stops and asks first (`AGENTS.md`).

## Needs the player's machine or account

1. **Enable GitHub Pages.** The first `Pages` run on `main` built the site, but `deploy` failed because Pages isn't enabled for the repository. In Settings → Pages, set the source to GitHub Actions, then re-run the workflow. The site holds only the build output and the hash-checked SoundFont; `tools/check-no-game-data.sh` runs before the upload.
2. **Check that the web build saves.** Serve or open the web build, load your own game files, change the volume and a key binding, set a high score, then reload the tab. All three should survive (`SaveSettings` in `winmain.cpp`, called from `Platform/Web/shell.html` before each sync and when the tab is hidden). This was only tested with dummy files, because no game data goes into a test browser.
3. **Try the post-processing switch on Direct3D 12 and Metal.** On Windows and on a Mac, switch Options → Graphics → Post-Processing from CRT to Smooth Edges and back during a game, and check that each effect draws. The fix (every effect's shader created once per GPU renderer, in `PostEffect.cpp`) is reasoned from SDL 3.4's source; the bug it fixes never showed on Vulkan, the only backend tested.
4. **Send the TinySoundFont patch upstream.** `third_party/TinySoundFont/tsf.h` now treats a compressed sample whose `start`/`end` lie outside the `smpl` chunk as empty, instead of reading past it (a corrupt SoundFont crashed the game at startup). The patch is recorded in `third_party/README.md`. Open a pull request at schellingb/TinySoundFont; once it lands, update the vendored copy and drop the note.

## Small changes

5. **Restore the music level.** TinySoundFont renders at −6 dB since #13, so the theme no longer clips, but music is about 6 dB quieter than the SDL2/FluidSynth build (RMS about 5,100 against 11,000 to 13,000), and the default Music Volume is already the maximum. First compare the two by ear. If it's too quiet, add a soft clipper in `RenderSynth` (`midi.cpp`) and raise the gain to about −2 dB, then measure with the `disk` audio driver that peaks stay under full scale over a whole theme loop. No gameplay effect.
6. **Let every ball size be replaced.** A sprite override PNG is matched by the group name of the sprite's first bitmap, so only the smallest of the ball's seven sizes can be replaced (`Upscale.cpp`). Name each bitmap of a group (for example `ball_0.png` to `ball_6.png`), keep the old name for the first, and make Export Original Sprites write the same names.
7. **Release a held trigger or button when its gamepad is unplugged.** Unplugging a gamepad while a trigger or button is held leaves the flipper up until another input releases it. Send the matching up event on `SDL_EVENT_GAMEPAD_REMOVED` (`winmain.cpp`). Input goes through `pb` entry points, so replays stay correct.
8. **Free the startup path strings.** A LeakSanitizer run that fails to load the game data reports 75 bytes leaked in `winmain::WinMain`, most likely SDL path strings. Free them on that path and check with a `linux-sanitize` build.

## From upstream pull requests and forks

A survey of upstream's pull requests and issues and of its forks (2026-10-19) brought in the ball-slot crash fix, per-table high scores, the Chinese string fixes, loading the message font from the player's `pinball.exe` and the 0.2 s Launch Ball push. These were left:

9. **Full Tilt mission fixes: in review as #33.** REA against Full Tilt's `CADET.EXE` confirmed both, and `Pinball.exe` does neither, so they apply in Full Tilt mode only. A completed mission awards a replay ball: 16 mission controllers call the replay routine (`0x422ce0`) with −1.0f right after the mission ends (issue k4zmu2a#238; boisdal/SpaceCadetPinball@1cacc25b9). The mission targets play `soundwave52` when all three `ramp_tgt_lights` are on, and `soundwave49D` otherwise (`0x425272`; issue k4zmu2a#206; boisdal@84671006b). Still to check after the merge: the replay message, the lamp and the two sounds against the original, by eye and by ear.
10. **Bind keys by physical position.** Key bindings store keycodes (`options.cpp`), so on a QWERTZ or AZERTY layout the default flipper keys sit elsewhere. Store scancodes instead and migrate saved bindings; omediodomonte38/SpaceCadetPinball@4b9fd05d2 does this. Input only; replays record bindings after they are resolved.
11. **Where `TPlunger` differs from 3D Pinball.** Checking Launch Ball against `Pinball.exe` showed other differences, most of them from the Full Tilt hybrid: when tilted, collision sends Launch Ball (0.2 s push) in the original; the release case has no started-flag guard and plays the release sound only for `PlungerInputReleased`; `Reset` runs the release path instead of killing every timer; a new pull is refused only while the pullback timer runs. Each changes gameplay; check one at a time and ask.

Decided against:

- **Upstream PR k4zmu2a#262** (push a resting ball on release, cancel the release timer on a quick re-pull). The original does neither, so a plunger that sometimes misses a bouncing ball is faithful (issue k4zmu2a#210).
- **Ignoring toggled cheats for high scores** (issue k4zmu2a#228). `CheatsUsed` is reset at a new game while toggled cheats stay on; the code notes this loose check is likely the original's. It only matters to someone using cheats.

## From testing with Full Tilt data

Running `CADET.EXE`'s table (`gamedata-fulltilt/`) to check #27 and #33 showed these. The panel width, the three resolutions and the message fonts looked right.

12. **Keep the resolution for each table.** Screen Resolution is one setting for every table. 3D Pinball has only one resolution and saves `Screen Resolution=0`, so after a 3D Pinball session Full Tilt opens at 640×480 instead of the size it was set to. Store it per table, as #27 does for high scores.
13. **Show Full Tilt's names in Full Tilt.** The window title says "3D Pinball for Windows - Space Cadet" and the game-over dialog says "3D Pinball / Space Cadet". `CADET.EXE`'s strings are "Full Tilt! - Space Cadet" and "Space Cadet". No gameplay effect.
14. **Check the text shown when a Full Tilt mission ends.** `CADET.EXE`'s mission controllers show string 3057, "Jackpot!\n%ld", with the jackpot score in `mission_text_box` (Practice Mission at `0x426e90`). The fork already adds the Full Tilt jackpot to the score (`SpecialAddScore`), but may still show 3D Pinball's per-mission message. Seen only in the disassembly; compare it in play, then fix the text. Text only.
15. **Check that Full Tilt's music changes track.** MDS music plays through TinySoundFont without errors, but nobody has checked whether it moves between `TABA1`, `TABA2` and `TABA3` the way Full Tilt does.

## Open decision

- **`pre-push` with uncommitted changes:** it warns and goes on, because it builds the working tree, not the pushed commits (#9). Refusing would be stricter but blocks a push over any stray edit. Recommended: keep the warning; CI stays the gate.

## Housekeeping

- Pull `main` into local checkouts: the branches of the merged pull requests still exist on GitHub and locally. Delete them once nothing local depends on them.
