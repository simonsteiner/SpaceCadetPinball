# Follow-ups after the modernization stack

Date: 2026-10-19. Status: open. The modernization stack (#8, #10 to #22) and its review fixes are merged into `main`; [the modernization plan](2026-10-18-modernization.md) records what was done. This plan collects what the reviews left open: items that need hardware, a decision or an outside party, and small gaps found while fixing.

Each item keeps the replays matching. One that would change gameplay stops and asks first (`AGENTS.md`).

## Needs the player's machine or account

1. **Enable GitHub Pages.** The first `Pages` run on `main` built the site, but `deploy` failed because Pages isn't enabled for the repository. In Settings → Pages, set the source to GitHub Actions, then re-run the workflow. The site holds only the build output and the hash-checked SoundFont; `tools/check-no-game-data.sh` runs before the upload.
2. **Check that the web build saves.** Serve or open the web build, load your own game files, change the volume and a key binding, set a high score, then reload the tab. All three should survive (`SaveSettings` in `winmain.cpp`, called from `Platform/Web/shell.html` before each sync and when the tab is hidden). This was only tested with dummy files, because no game data goes into a test browser.
3. **Try the post-processing switch on Direct3D 12 and Metal.** On Windows and on a Mac, switch Options → Graphics → Post-Processing from CRT to Smooth Edges and back during a game, and check that each effect draws. The fix (every effect's shader created once per GPU renderer, in `PostEffect.cpp`) is reasoned from SDL 3.4's source; the bug it fixes never showed on Vulkan, the only backend tested.
4. **Get the TinySoundFont bug fixed upstream, by hand.** `third_party/TinySoundFont/tsf.h` now treats a compressed sample whose `start`/`end` lie outside the `smpl` chunk as empty, instead of reading past it (a corrupt SoundFont crashed the game at startup). The patch is recorded in `third_party/README.md`. Upstream (schellingb/TinySoundFont, `tsf_decode_sf3_samples`) still has the bug, but since July 2026 it refuses issues and pull requests written with an LLM, so our patch can't be sent and no agent should open an issue or pull request there. The player can report the bug or write a fix themselves; once a fix lands, update the vendored copy and drop the note.

## Small changes

5. **Restore the music level.** TinySoundFont renders at −6 dB since #13, so the theme no longer clips, but music is about 6 dB quieter than the SDL2/FluidSynth build (RMS about 5,100 against 11,000 to 13,000), and the default Music Volume is already the maximum. First compare the two by ear. If it's too quiet, add a soft clipper in `RenderSynth` (`midi.cpp`) and raise the gain to about −2 dB, then measure with the `disk` audio driver that peaks stay under full scale over a whole theme loop. No gameplay effect.
6. **Let every ball size be replaced.** A sprite override PNG is matched by the group name of the sprite's first bitmap, so only the smallest of the ball's seven sizes can be replaced (`Upscale.cpp`). Name each bitmap of a group (for example `ball_0.png` to `ball_6.png`), keep the old name for the first, and make Export Original Sprites write the same names.
7. **Release a held trigger or button when its gamepad is unplugged.** Unplugging a gamepad while a trigger or button is held leaves the flipper up until another input releases it. Send the matching up event on `SDL_EVENT_GAMEPAD_REMOVED` (`winmain.cpp`). Input goes through `pb` entry points, so replays stay correct.
8. **Free the startup path strings.** A LeakSanitizer run that fails to load the game data reports 75 bytes leaked in `winmain::WinMain`, most likely SDL path strings. Free them on that path and check with a `linux-sanitize` build.

## Open decision

- **`pre-push` with uncommitted changes:** it warns and goes on, because it builds the working tree, not the pushed commits (#9). Refusing would be stricter but blocks a push over any stray edit. Recommended: keep the warning; CI stays the gate.

## Housekeeping

- Pull `main` into local checkouts: the branches of the merged pull requests still exist on GitHub and locally. Delete them once nothing local depends on them.
