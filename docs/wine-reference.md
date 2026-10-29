# Wine reference

The original Full Tilt! executables (`CADET.EXE`, `DRAGON.EXE`, `PIRATES.EXE`) run under Wine as a reference for how the tables play: which texts and lights a rule shows and what it scores. REA reads their code; Wine shows what that code does. Phase F decided how they may run ([plan](plan/2026-10-20-space-cadet-2026.md), Open questions):

- from copies in a scratch folder inside the Wine prefix, never from `../assets/` or the `../gamedata*/` folders;
- in an isolated prefix with networking off;
- with everything they produce, screenshots included, kept outside the repository like the game data.

`tools/wine-reference.sh` does this without root. It keeps everything in `~/.cache/scp/wine` (`SCP_WINE` changes it).

## Setup

```bash
tools/wine-reference.sh setup
```

The script downloads Ubuntu's Wine 10 packages with `apt-get download` and unpacks them into `~/.cache/scp/wine/root`, the way `docs/setup.md`'s system SDL3 works without sudo. Then it creates the prefix. It takes about a minute and needs the network only for the download.

The packages need three fixes, which the script makes:

- **NLS files:** Debian's `wineserver` looks for them in `share/wine/wine/nls`, so a `wine` link to `.` goes into `share/wine`.
- **`zlib1.dll`:** `user32.dll` needs it, from `libz-mingw-w64`. Wine loads it as a native DLL: from its DLL folders while it creates the prefix, and from the prefix's `system32` and `syswow64` afterwards.
- **32-bit EXEs:** there are no 32-bit Unix libraries, so Wine runs them in its experimental WoW64 mode. `CADET.EXE` works in it; the log's `starting ... in experimental wow64 mode` is expected.

The script also cuts the prefix off from the rest of the disk. It removes the `Z:` drive, which maps `/`, and replaces the user folders' links into `$HOME` with empty folders.

## Running a table

```bash
tools/wine-reference.sh start ../gamedata-fulltilt CADET.EXE -nomovie
tools/wine-reference.sh exec uv run --with python-xlib --with pillow python3 -I tools/xtest-drive.py :93 "Space Cadet" /tmp/out k:F2 s:8 t:rmax s:1.5 p:rank
tools/wine-reference.sh stop
```

- **`start`** copies the folder into the prefix's `C:\scratch\` the first time, starts the EXE there and waits for its window. `-nomovie` skips the intro movie, which Wine can't play. The table appears about half a minute later, once the startup sound has played; without a sound device, Wine only times it.
- **Sound:** off. The script disables Wine's audio drivers, so the EXE has no sound device. Under WSLg it would otherwise play through the desktop's PulseAudio server, whose socket (`PULSE_SERVER`) the namespace doesn't cut off. `SCP_WINE_SOUND=1` turns sound on.
- **Networking:** Wine runs in a user and network namespace (`unshare -n --map-current-user`) that has only a loopback interface, which is down.
- **Display:** Xvfb (`:93`, or `SCP_WINE_DISPLAY`) runs inside the same namespace. Under WSLg `/tmp/.X11-unix` is read-only, so Xvfb listens only on an abstract socket, and a client in another network namespace can't reach that. So anything that talks to the display, such as XTest, screenshots or `wineserver`, runs through `exec`.
- **Window position:** Xvfb has no window manager, and `CADET.EXE` puts its window partly off screen, so `start` moves it to 0,0. Don't use Wine's virtual desktop: inside it the window stays off screen and can't be moved.
- **Driving:** `tools/xtest-drive.py` presses keys, types and takes screenshots of the window, step by step. The same steps drive our port on its `tools/run-headless.sh` display, so the two can be compared screenshot by screenshot. Save the screenshots outside the repository.
- **Stopping:** `stop` ends Wine (`wineserver -k`) and the namespace.

## How far it can be trusted

On 2026-10-27, `CADET.EXE` under Wine and our port in Classic on `CADET.DAT` were given the same keys: F2, the cheats `rmax` (each 10 s apart) and `1max`, and a launch. These matched:

- the texts at the start of a game ("Player 1", "Awaiting Deployment");
- the rank promotions ("Promotion to Ensign", then "Lieutenant");
- the table's lights, the rank lights among them;
- the text after a launch ("Hit Mission Targets To Select Mission", or the mission the ball's path selected).

What doesn't match is anything that depends on the ball's path. Wine runs the original's own frame timing, so the same launch takes a different path, and the score after it differs (4,750 against 10,000 in one run). Compare rules by their texts, lights and the score of single events, not by a whole ball.

Three pitfalls showed up on the way:

- **Typing too soon after F2.** Keys typed in the first 3 s after F2 can reach `CADET.EXE` before its game has started, so wait about 8 s.
- **Cheat mode stays on.** `hidden test` turns cheat mode on until it's typed again, across games. In cheat mode `r` alone bumps the rank, so `rmax` then bumps it twice.
- **Text-box queues.** A screenshot of the text box may show a queued message rather than the latest one; messages last up to 8 s.

In one run, our port didn't launch on a 1.5 s plunger press after `1max`, while `CADET.EXE` did. That was the launch window: at our default 120 updates per second about a quarter of the presses missed, where the originals launched on all of 40. Since the fix of 2026-10-28 a released pull keeps its push until the ball touches the plunger, and our port launched on all of 40 presses on each data file ([measurements](plan/2026-10-26-bonus-tables-spike.md#plunger-launch-window-2026-10-28)).
