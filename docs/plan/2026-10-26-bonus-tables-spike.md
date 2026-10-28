# Bonus tables spike (Phase F, F1 and F2)

Date: 2026-10-26. Status: F3 decided; F4's sizing (below) narrows the estimate. The spike code is on `spike/skulduggery` (#67), marked `SPIKE (F2)`, and isn't meant to merge. Phase F is in the [Space Cadet 2026 plan](2026-10-20-space-cadet-2026.md).

## F1: reference files

The CD image's Win95 folder has 32-bit builds of both tables, linked on the same day as `CADET.EXE` and, like it, without debug information:

| Table | Folder | Executable | SHA-1 |
| --- | --- | --- | --- |
| Skulduggery | `../gamedata-skulduggery/` | `PIRATES.EXE`, PE32 i386 GUI, 1996-03-17 | `5EB97B0CF190F628C7F1C02A9D148F27A508E580` |
| Dragon's Keep | `../gamedata-dragon/` | `DRAGON.EXE`, PE32 i386 GUI, 1996-03-17 | `7BBC2FEF948613644773B7689FB0CCA28C7F1B5C` |

Each folder holds the table's DAT, its BMP, the EXE and `SOUND/` with the Win95 music (MDS, and MID for Skulduggery) plus the WAVs, which only the CD's `WIN31` folder has. The same method reproduces `../gamedata-fulltilt/` byte for byte. The workspace `AGENTS.md` lists both folders.

## F2: what works

Both tables load and draw with two small loader changes (below), with the rules stubbed: a stand-in that logs the events the rules would get and serves a new ball after a drain.

- **Drawing:** the table, the backdrop, the score panel and the table's own font draw in Classic. Definitive's front end and HUD run over them, and the title's demo plays on the table.
- **Flippers:** every flipper moves. Skulduggery has two on each side and Dragon's Keep has one left and two right; the spike sends the flipper keys to all of them.
- **Plunger and ball, Dragon's Keep:** the plunger pulls and launches; the ball rolls, bounces off rebounders, one-ways and spot targets, drains, and the stub serves the next one.
- **Launcher and ball, Skulduggery:** Skulduggery has no plunger (type 1001) but a launcher, `s_launcher1` (type 1034). Used as a plunger without sprites, it launches the ball up the lane, through the one-ways, onto the raised level at the top. There the ball **falls through the table**: it keeps rolling past the bottom edge at top speed, presumably because the unknown types below (ramp exits, accelerators) would bring it back down. So Skulduggery's main playfield isn't tested yet.
- **Replays:** all 12 Space Cadet and all 6 Full Tilt replays match in both editions with the spike build.
- **Not tested:** sound (runs used `-noaudio`) and music, which can't play anyway (below).

## What's missing

### Loader gaps (fixed on the spike, as approved, and on their own for `main`)

- `loader::sound_list` held 64 sounds; `PIRATES.DAT` has 67 sound groups (Space Cadet has 48, Dragon's Keep 51). Now 128.
- `loader::kicker` didn't know kicker attribute **410**, 8 floats that look like a throw path (two points). 14 kickers in `PIRATES.DAT` and 18 in `DRAGON.DAT` use it. Without it `query_visual` returns early and `TKickout` crashes on a null `FloatArr`. It's now read into `visualKickerStruct` but not used.

### Object types the table skips

| Type | Skulduggery | Dragon's Keep | Likely role (from the group names and attributes) |
| --- | --- | --- | --- |
| 1034 | `s_launcher1` | | Ball launcher: plunger wall (600), ball position (601), boost 100 (402), no sprites |
| 1035 | `s_rexit8`, `s_rexit11`, `s_rexit12` | `s_rexit18`, `s_rexit19`, `s_rexit21` | Ramp exits: one ramp wall (1301) each |
| 1036 | `a_accl1`, `a_accl2` | `a_accl1`, `a_accl3`, `a_accl4`, `a_accl5` | Accelerators: a wall and a kicker |
| 1037 | one unnamed group | | A point (408) on a collision layer (602) |
| 1038 | `light_sweeps` | `light_sweeps` | Light show settings (1600 to 1604) |
| 1042 | `s_magnet1` to `s_magnet6` | | Magnets: a circle (600) and a field strength (305) |
| 1043 | | `a_dvtraa` to `a_dvtrdb` (8) | Diverters |

None of these classes exist in `Pinball.exe` or this code, so they have to be recovered from `PIRATES.EXE` and `DRAGON.EXE`. They may be in `CADET.EXE` too, as part of the engine all three share: its assertion string `sweep == NULL` appears in all three. If so, they're easier to recover there, next to code that's already matched. REA should check that first.

### Known types with other attributes

The spike guarded the first two; the others don't crash but may behave differently. F4 settled the ramps and blockers for Dragon's Keep (the [plan](2026-10-20-space-cadet-2026.md), F4's engine milestone):

- Ramps (1021) without wall 1303: Skulduggery's `a_ramp5`, `a_ramp8`, `a_ramp13`, `a_ramp18`, and six of Dragon's Keep's.
- Blockers (1011) without a wall (600) or material (300): `a_cannon` and `a_bloce`, and Dragon's Keep's `a_sword`, `a_jaw`, `a_center_post`, `a_rocka`, `a_rockb`. They look like animated props more than blockers.
- Popup targets (1006) with a kicker and without the sprite list (100): Skulduggery's `a_drop5` to `a_drop15`.
- Light rollovers (1020) without a soft-hit sound (304), and Dragon's Keep's spot targets (1019) `a_spot10`, `a_spot14`, `a_spot19`, `a_spot26` and `a_spot28` without bitmaps.
- Dragon's Keep's plunger sprite draws with a white rectangle that reaches past the table's right edge; not investigated.

## Space Cadet assumptions outside `control.cpp`

- **Which table is loaded** is two booleans, `pb::FullTiltMode` and `pb::FullTiltDemoMode`. They pick the DAT format, the `SOUND` folder, plunger and kickout behaviour, music, settings prefixes and texts. A third and fourth table need a table identity instead; the spike adds a third boolean.
- **`TPinballTable`** expects exactly one plunger (type 1001) and dereferences it unchecked, keeps one left and one right flipper, and looks up `info_text_box` and `mission_text_box` (the bonus tables have only the first). Its message handler shows Space Cadet's texts (`Msg::STRING…`) and uses Space Cadet's jackpot limits.
- **Component constructors** assume Space Cadet's attributes: `TRamp` needs wall 1303, `TCollisionComponent` needs wall 600 when it builds walls, `TPlunger` needs sprites, and `TKickout` uses `visual.FloatArr` without checking it.
- **`control` outside the rule handlers:** `UnstuckBall` uses Space Cadet's `flip1`, `flip2` and `plunger`; the cheats and Full Tilt's idle "credits" and "quote" drive Space Cadet's components. Serving the next ball, game over and the high score check all run through the rules (`BallDrainControl`).
- **Music:** `midi.cpp` loads `TABA1` to `TABA3`. Skulduggery has `TABB1` to `TABB6` and Dragon's Keep `TABC1` to `TABC7`, and `MidiTracks` has three tracks.
- **Settings:** `options::TableKeyPrefix` knows three tables (F5), and Settings offers "Prefer 3D Pinball's data to Full Tilt's", which assumes two data sets.
- **Texts:** the window title and About say "Full Tilt! - Space Cadet". The bonus tables' texts exist only in English, as resources in their EXEs.
- **Space Cadet 2026:** `TableView` reads the mission from `lite198` and the rank from `middle_circle`. The HUD has Mission and Rank sections and the Mission Guide covers Space Cadet's missions. On a bonus table they show empty.

## Estimate for F3 to F6

The three EXEs share Full Tilt's engine (`Common\Src\…`, same day, same compiler). `PIRATES.EXE`'s code section is only 12 KB larger than `CADET.EXE`'s and `DRAGON.EXE`'s 6 KB, so each table's rules are about as large as Space Cadet's (`control.cpp`, about 4,500 lines). `CADET.EXE` matches the existing code, so REA can match the shared engine functions across the three EXEs and leave only the table's own functions to name. Without symbols that naming is the bulk of the work.

| Step | Work | Estimate |
| --- | --- | --- |
| F3 | Rules layout and table identity (proposal below), `AGENTS.md` | 2 to 4 days |
| F4, engine | The loader changes done properly; 6 new classes from `PIRATES.EXE`; the variants of known types; several plungers, flippers and text boxes; music names and track count | 3 to 5 weeks |
| F4, rules | Skulduggery's rules, one mission or feature per pull request, checked with REA and replays | 8 to 12 weeks |
| F5 | Table select, settings prefixes, Classic's table list, HUD sections per table | 1 to 2 weeks |
| F6 | Dragon's Keep: diverters and its variants (1 to 2 weeks), then its rules (6 to 10 weeks) | 7 to 12 weeks |

In total that's months, not weeks: about **4 to 7 months** of focused work for both tables, most of it in the rules. A playable Skulduggery without rules (engine work only, the ball serving again after a drain) is the first 3 to 5 weeks of that.

## Proposal for F3: rules layout

- **A table identity:** `enum class Tables { SpaceCadet3DPB, SpaceCadetFullTilt, FullTiltDemo, Skulduggery, DragonsKeep }`, set by `pb::SelectDatFile` from the DAT's name. `FullTiltMode` and `FullTiltDemoMode` stay, derived from it, so existing code and upstream merges keep working.
- **One rules file per table:** `control.cpp` stays Space Cadet's, unchanged. Each bonus table gets `control_skulduggery.cpp` and `control_dragon.cpp`, each with its own `component_info` tables bound by group name, as `control.cpp` does.
- **One dispatch point:** components keep calling `control::handler`, `control::table_control_handler` and the other entry points, which hand over to the loaded table's rules. Components and `control.cpp` don't need to know about the bonus tables, and upstream merges stay small. A small `TableRules` struct of function pointers (`MakeLinks`, `ClearLinks`, `Handler`, `TableControl`, `UnstuckBall`, `Cheat`) picked at load is enough; no class hierarchy.
- **New object classes are engine, not rules:** `TLauncher`, `TRampExit`, `TAccelerator`, `TMagnet`, `TLightSweeps`, `TDiverter` (names to be confirmed) go in their own `T*.cpp` like the existing components, react only through `Message`, and are built by `TPinballTable` for any table that has them.
- **Texts** get new `Msg` values in `translations.cpp`, in English only at first.
- **Replays** per table in `../replays/<table>/`, checked with `tools/check-replays.sh`.
- **`AGENTS.md`** then says: table rules live only in `control.cpp` (Space Cadet) and `control_<table>.cpp` (each bonus table), and the loaded table picks its rules.
- **`TableView`** gets the HUD's table-specific fields from the loaded table's rules file, so the presentation still reads only the view.

## Recommendations

1. **Decide F3 as proposed.** It takes a few days, changes nothing in Space Cadet's rules, and settles the layout before any rules code exists.
2. **Size before committing months.** Spend 1 to 3 days with REA on two questions: are types 1034 to 1043 already in `CADET.EXE` (the `sweep == NULL` hint), and how many functions of each table's EXE match the shared engine? The answers narrow the estimate above.
3. **Dragon's Keep first, then Skulduggery,** the reverse of the plan's order. Dragon's Keep has a plunger and already plays a full ball; it needs four new types against Skulduggery's six, and of Space Cadet's assumptions it only breaks the single right flipper, where Skulduggery also breaks the single left flipper and the plunger. Most of the engine work carries over.
4. **An engine milestone before the rules:** the first table playable without rules, every object type built, the ball never leaving the table. Only then rules, one mission or feature per pull request.
5. **The loader changes go to `main` on their own** (128 sounds, kicker attribute 410). They change nothing for Space Cadet.
6. **A reference for the recovered rules.** REA reads the code but nothing records how the original plays. Running the original EXE in an isolated Wine prefix would give reference scores and light patterns; it means running a binary from the CD, which F1 avoided, so it's the owner's call.
7. **A launcher for test runs,** so parallel threads stop killing each other's games: during the spike, another thread's `pkill -x SpaceCadetPinba` ended runs on a shared Xvfb display.

## F4 sizing (2026-10-27)

Ghidra 12.1.4 through REA decompiled the table constructors of `CADET.EXE` and `DRAGON.EXE`, and a headless Ghidra script hashed every function of both, with absolute addresses masked, to match them. The script and its output stay in `~/.cache/scp/f4/`, outside the repository. Ghidra's own analysis misses functions that only data points to (vtables, handler tables), so the script also creates a function at every pointer into code first.

### Are types 1034 to 1043 in `CADET.EXE`?

Yes, all seven. Its table constructor is the same function as `DRAGON.EXE`'s, apart from addresses, and its type switch has a case for each of them. The engine shared by the three EXEs builds every object type; each table's DAT uses only some.

| Type | Class in `CADET.EXE` | Code | Table |
| --- | --- | --- | --- |
| 1034 | No new class: the plunger's constructor (type 1001's) with a flag of 1 | | Skulduggery |
| 1035 | Collision component; overrides `Collision` | about 220 instructions | both |
| 1036 | Collision component; overrides `Collision` and the field effect | about 155 instructions | both |
| 1037 | Own class (555 bytes), kept in a list on the table | | Skulduggery |
| 1038 | Plain component (not collision), kept in its own slot on the table; overrides `Message` and drawing | about 750 instructions | both |
| 1042 | Subclass of type 1029's class (our `TKickout`) with a flag of 1 | | Skulduggery |
| 1043 | Collision component; overrides `Message` and `Collision` | about 420 instructions | Dragon's Keep |

Dragon's Keep's four types (1035, 1036, 1038, 1043) are about 5 KB of code, 1,600 instructions in all, identical in `DRAGON.EXE`. They can be recovered from `CADET.EXE`, whose engine functions around them already match our code.

### How much of `DRAGON.EXE` matches the shared engine?

Of `DRAGON.EXE`'s 925 functions, 801 (140 KB) match a function of `CADET.EXE` instruction for instruction. The engine sits at the same addresses in both EXEs, and the C runtime follows each table's own code. That code is one block after the engine: 122 functions and 37 KB in `DRAGON.EXE`, against 108 functions and 31 KB in `CADET.EXE`. `CADET.EXE`'s block is Space Cadet's rules, which `control.cpp` holds in 116 functions and about 4,500 lines; it also holds the table's credits text and its music names (`tabc1` to `tabc7` in Dragon's Keep). So Dragon's Keep's rules are about 1.2 times Space Cadet's, roughly 5,000 to 5,500 lines.

### Which types and variants does Dragon's Keep need?

- **The four new types** above, each in its own `T*.cpp`.
- **Flippers:** `CADET.EXE` keeps a list of flippers on each side, where our code, from `Pinball.exe`, keeps one. Dragon's Keep needs the lists for its two right flippers.
- **Variants:** `CADET.EXE`'s engine is a later revision than `Pinball.exe`'s, and its constructors already handle what Dragon's Keep leaves out. Its `TRamp` treats wall 1303 as optional, and its `TBlocker` is a different design: up to eight walls, one per sprite, each optional, which fits Dragon's Keep's props (`a_sword`, `a_jaw` and the rest). The spot targets without bitmaps, the light rollovers without sound 304 and the kicker attribute 410 are to be compared the same way.
- **Space Cadet stays as it is.** `TRamp`'s optional wall changes nothing when the wall is there. `TBlocker` is different: porting `CADET.EXE`'s version for all tables could change Space Cadet's blockers, so it goes in only where Space Cadet's data doesn't reach, or as its own class for the bonus tables, unless the owner decides otherwise.

### Narrowed estimate

| Step | Work | Before | Now |
| --- | --- | --- | --- |
| F4, engine | `-table dragon`, the loader changes, music names; four classes from `CADET.EXE`; flipper lists; the variants; the plunger sprite | 3 to 5 weeks (F4 for Skulduggery) | **2 to 3 weeks** |
| F4, rules | Dragon's Keep's rules: about 120 functions to name, against `CADET.EXE`'s rules matched to `control.cpp`, which gives every engine call they make a name | 6 to 10 weeks (F6) | **5 to 8 weeks** |
| F6, engine | Skulduggery: 1034 as a plunger with a flag, 1037 and 1042, left flipper list, its variants | 3 to 5 weeks | **1 to 2 weeks** |

The rules stay the bulk of the work, because without symbols every function and flag of Dragon's Keep's block needs a name. What shrinks is the engine: no class has to be recovered from a table's EXE alone.

## Plunger launch window (2026-10-28)

A released pull launches the ball only if the ball touches the plunger within `PullbackDelay`, 25 ms, after the release: the release sets the plunger's threshold to 0, so a touch gets the push, and `ReleasedTimer` takes the push back 25 ms later. `Pinball.exe` does the same: its `TPlunger::Message` and `TPlunger::Collision` match ours, and its constructor sets `PullbackDelay` to 0.025 (0x3ccccccd).

### Measured

Each run pressed the plunger 40 times, holding it 0.3 to 1.3 s, each time after the ball had rested on it for at least 1.5 s. A press counted as missed when the ball was still on the plunger at each of four screenshots 0.15 to 0.7 s after the release. On our port, a temporary log of every touch and release agreed with the screenshots on all 40 presses.

| Run | Presses | Missed |
| --- | --- | --- |
| `Pinball.exe` under Wine, `PINBALL.DAT` | 40 | 0 |
| Our port, Classic, `PINBALL.DAT`, 120 updates per second (the default) | 40 | 9 |
| `CADET.EXE` under Wine, `CADET.DAT` | 40 | 0 |
| Our port, Classic, `CADET.DAT`, 120 updates per second | 40 | 11 |

### Why

A ball resting on the plunger doesn't stay in contact. It bounces off and gravity brings it back, so it touches at intervals, and how long they are depends on the length of the physics step. A release launches only if a touch falls inside the 25 ms after it. With the temporary log, the ball reset onto the plunger before each press and about 40 presses per run on `PINBALL.DAT`:

| Updates per second | Step | Touch interval (median) | Missed |
| --- | --- | --- | --- |
| 60 | 17 ms, of which a slow ball moves only 10 ms | 68 ms | 72% |
| 120 | 8.3 ms | 33 ms | 26% |
| 240 | 4.2 ms | 34 ms | 34% |
| 360 | 2.8 ms | 28 ms | 58% |
| uncapped, about 800 | 1.25 ms | 20 ms | 1 of 39 |

The originals run their loop as fast as the machine allows, so their steps are short and the ball touches more often than every 25 ms. Our port updates 120 times per second by default, where the touches come every 33 ms and about a quarter of the presses fall between two of them. At 60 updates per second it's worse: `pb::timed_frame` moves a slow ball by at most 10 ms per update, so the touches stretch to 68 ms of real time while the timer still counts real time. The rates in between scatter (240 and 360 missed more than 120) because the touch interval and the 25 ms window beat against each other; only steps short enough to touch every 20 ms or so make the window reliable. Under Xvfb the 360 run also shared the machine with two others.

So our port misses launches that the originals don't, on 3D Pinball's data and Full Tilt's alike. This is the "launch can miss" of #85 and the missed 1.5 s press in [the Wine reference](../wine-reference.md).

### Proposed fixes (not made; gameplay, so the owner decides)

Any fix changes when the ball launches, so the reference replays that contain a missed launch would have to be recorded again.

1. **Keep the push until the ball touches** (recommended). After a release, `ReleasedTimer` takes the push back only once the ball has touched the plunger, or after a limit such as 100 ms if it doesn't (the plunger released with no ball on it). A ball resting on the plunger then always launches, as in the originals; a ball arriving later than that still doesn't. It changes only `TPlunger`.
2. **Shorter physics steps.** `pb::frame` runs `timed_frame` in sub-steps of about 1 ms, which is closer to how the originals ran. It changes every collision's timing, not just the plunger's, so every replay would differ, and it costs CPU.
3. **A longer window**, for example 40 ms. It covers the 33 ms interval at 120 updates per second, but not 60.

## Known differences between Pinball.exe and CADET.EXE (2026-10-28)

Our port follows `Pinball.exe` for Space Cadet, in both its 3D Pinball and its Full Tilt data. `CADET.EXE`'s engine is a later revision, so Full Tilt's own Space Cadet plays differently in a few places. None of these is changed; each would change Space Cadet, so it's the owner's call.

| Where | `Pinball.exe` and our port | `CADET.EXE` | Effect on Space Cadet |
| --- | --- | --- | --- |
| Kicker attribute 404 (throw direction) | Kept as given | Normalised in x and y, like 410 (`loader.cpp`) | **Yes.** Five of Space Cadet's eight throws are diagonal, with length √2: `v_sink1`, `v_sink2`, `a_kout1`, `a_kout2` and one more group. `TBall::throw_ball` takes the direction as given, and the next frame folds its length into the speed, so these sinks and kickouts throw the ball about 1.41 times as fast as `CADET.EXE` does, up to the ball's speed limit. |
| `TRamp` events | Only `ControlCollision` on wall 1303 | Also 0x55 and 0x56 when the ball crosses walls 1301 and 1302, and 0x54 when a ramp plane changes its layer (`TRamp::Collision`, 0x4177d0) | **No.** The events go to the ramp's rules handler and then to the table-wide one (0x42a260), which takes only 0x33, 0x46 and 0x3f1. Space Cadet's ramp handler, `LaunchRampControl` (0x423560 in `CADET.EXE`), acts only on `ControlCollision`, and `s_ramp9` has none. They matter for the bonus tables' rules. |
| `TRamp` layer change | The ball keeps moving | The same crossings set the ball's field 0x9b (`CollisionDisabledFlag`), which stops it for the rest of the frame | **Yes, slightly.** Every crossing of `ramp`'s or `s_ramp9`'s walls costs the ball the rest of that frame's movement, so ramp shots run a little differently. |
| `TSoloTarget` (1019) | Disable and Enable leave a running re-enable timer alone; only Reset stops it | Disable and Enable both stop it (`TSoloTarget::Message`, 0x414be0); with no bitmap for a state it clears the sprite | **No.** Space Cadet's rules never send Disable or Enable to a target. Dragon's Keep's rules might, and then a target its rules turn off within 0.1 s of a hit would come back on in our port. Its targets have two bitmaps or none, which both versions draw alike. |
| `TLightRollover` (1020) | | Same logic step for step (`TLightRollover::Collision`, 0x415840; `Message`, 0x4159b0) | **No.** Dragon's Keep's `v_wlite96` to `v_wlite99` have one bitmap each and no sound 304; both versions show that bitmap on a hit and play no sound. |

## Spike code

On `spike/skulduggery`, marked `SPIKE (F2)`:

- `pb::FindDatFile` also finds `PIRATES.DAT` and `DRAGON.DAT`. They load as Full Tilt data with `pb::BonusTableMode` set.
- `control` skips Space Cadet's rules on a bonus table. A stub logs the first of each event the rules would get (`SPIKE rules event`) and serves a ball after a drain.
- `TPinballTable` logs skipped object types (`SPIKE skipped object type`), and each object it builds with `SPIKE_TRACE=1`. `SPIKE_BALLS=1` logs ball positions and launcher hits.
- The stand-ins listed under "What works", the loader changes, and loader errors that log instead of opening a dialog on a bonus table.

Screenshots of the tables stayed local, as their art is Microsoft's.
