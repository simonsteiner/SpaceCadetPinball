# Bonus tables spike (Phase F, F1 and F2)

Date: 2026-10-26. Status: report for decision; the spike code on `spike/skulduggery` is marked `SPIKE (F2)` and isn't meant to merge. Phase F is in the [Space Cadet 2026 plan](2026-10-20-space-cadet-2026.md).

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

### Loader gaps (fixed on the spike, as approved)

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

The spike guarded the first two; the others don't crash but may behave differently:

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

## Spike code

On `spike/skulduggery`, marked `SPIKE (F2)`:

- `pb::FindDatFile` also finds `PIRATES.DAT` and `DRAGON.DAT`. They load as Full Tilt data with `pb::BonusTableMode` set.
- `control` skips Space Cadet's rules on a bonus table. A stub logs the first of each event the rules would get (`SPIKE rules event`) and serves a ball after a drain.
- `TPinballTable` logs skipped object types (`SPIKE skipped object type`), and each object it builds with `SPIKE_TRACE=1`. `SPIKE_BALLS=1` logs ball positions and launcher hits.
- The stand-ins listed under "What works", the loader changes, and loader errors that log instead of opening a dialog on a bonus table.

Screenshots of the tables stayed local, as their art is Microsoft's.
