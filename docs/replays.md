# Replays

A replay records a session so it can be played back later and checked frame by frame. It is the determinism harness from plan Phase 1 step 3: record a few sessions before a change that might affect play, play them back after it, and any difference shows up as the first frame whose score or ball state no longer matches.

Replays need the game data, so they run locally, not in CI.

## Recording

```bash
cd ../gamedata  # the folder with PINBALL.DAT
../SpaceCadetPinball/bin/SpaceCadetPinball -record ../replays/my-game.replay
```

Play normally, then quit. Recording works with `-demo`, with sound or `-noaudio`, and at any update rate. Anything that restarts the game ends the recording: changing the language, font, resolution or game data, or resetting the options.

Keep recordings out of the repository. Each one is tied to the data file it was recorded with, and the game refuses to play it back against a different one. In this workspace they live in `../replays/`.

## Playing back

```bash
cd ../gamedata
../SpaceCadetPinball/bin/SpaceCadetPinball -replay ../replays/my-game.replay
```

Playback is headless: no window, no audio, no frame pacing. It runs the recording as fast as it can (a 30-second session takes about 20 ms) and exits with status 0 if every frame matched:

```text
Replay matched all 3563 frames.
```

or with status 1 at the first difference:

```text
Replay diverged at frame 391.
Expected: t=3275 m=1 p=0 n=3 s0=0 b=-
Actual:   t=3266 m=1 p=0 n=3 s0=0 b=-
```

Playback doesn't save options or show dialogs. It uses SDL's `dummy` video driver unless `SDL_VIDEODRIVER` says otherwise.

To check a whole folder:

```bash
tools/check-replays.sh ../replays ../gamedata
```

The script plays every `.replay` file in the first folder with the second folder as the working directory, prints one line per replay and exits non-zero if any of them differ. A third argument picks a different executable; the default is `bin/SpaceCadetPinball`.

## What gets recorded

Everything that reaches the table from outside goes through a handful of `pb` functions, and those are what a replay records:

- each `pb::frame` call with its time step, followed by the state after it
- table input after the key bindings are resolved (`pb::GameInputDown`, `pb::GameInputUp`), so a replay doesn't depend on the player's key bindings
- new game, demo toggle, pause, focus loss, ball launch, cheats, the cheat-mode F10 freeze and mouse ball control (the cheat-mode drag)

The time step comes from the wall clock, so it differs from frame to frame and from run to run. That is why the demo plays out differently between runs, and why a replay records each step instead of assuming a fixed rate. The game's random numbers come from `rand()`, which is never seeded, so it always starts at 1; a replay records that seed and applies it before the table loads.

Each recorded function opens a `replay::Scope`. Only the outermost scope writes a line, so a command that calls another one (`toggle_demo` calls `replay_level`, for example) is played back once. **A new entry point that changes table state from outside `pb::frame` needs a scope too,** or replays of sessions that use it will diverge.

## File format

A replay is a text file. The header comes first:

```text
SpaceCadetPinball replay 1
game 2.1.1 DEV
dat PINBALL.DAT fa8bd21264db7078
seed 1
resolution 0
start
```

`dat` gives the data file name and a 64-bit FNV-1a hash of its contents. After `start`, each line is one command, in the order it happened:

| Line | Meaning |
| --- | --- |
| `f <dt> <state>` | `pb::frame(dt)`, then the state it produced |
| `d <type> <value> <bindings>` | input pressed: `InputTypes`, key or button, comma-separated `GameBindings` (`-` for none) |
| `u <type> <value> <bindings>` | input released |
| `n <demo> <players>` | `pb::replay_level`: new game, or demo if `demo` is 1 |
| `t <players>` | `pb::toggle_demo` with the player count from the options; older recordings have a bare `t` and use the current options |
| `p` | `pb::pause_continue` |
| `s` | `pb::toggle_single_step` (cheat-mode F10) |
| `l` | `pb::loose_focus` |
| `b` | `pb::launch_ball` |
| `m <dx> <dy>` | `pb::ballset` |
| `c <cheat>` | `pb::PushCheat` |

Floating-point values are written with `%a`, so they read back exactly. The state is:

```text
t=<time_ticks> m=<game mode> p=<current player> n=<balls left> s0=<score> ... b=<ball> ...
```

with one `s` per player and one `b` per ball in the table's ball list. An active ball is `x,y,z,direction x,direction y,speed`; an inactive one is `-`.

## Limits

- **Same platform and compiler.** Floating-point results can differ between compilers and architectures, so a replay recorded with GCC on x86-64 may not match a build from MSVC or Apple Clang on arm64. Release and Debug builds from the same GCC match. Compare like with like.
- **The state is a sample.** A change that alters flipper or light state without moving a ball or changing a score shows up only once it does.
- **Format changes break old recordings.** Changing what the state line contains, or the numbering of `InputTypes` or `GameBindings`, means recording the sessions again. Bump the version in the first line when the meaning of a line changes.
