# Follow-ups after the docstrings and Sonar stack

Date: 2026-11-10. Status: decided, in review (#203 to #209, see [Outcome](#outcome)). The stack #190 to #200 put a comment above every function with a branch or a loop (1,154 of 1,154, `tools/check-function-comments.sh`), took vendored code out of SonarCloud's analysis and fixed 474 of the Sonar issues on lines the fork wrote. This plan collects what its reviews found and the stack left alone, because each item is a code change, a decision or outside its scope.

Each item keeps the replays matching. One that would change gameplay stops and asks first (`AGENTS.md`).

## Needs a decision

1. **What to do with the Sonar issues that stay open.** `main` had 8,382 open issues: 6,512 in vendored code (gone once #195 is analysed on `main`) and 1,870 in the project's own. The stack fixed 474; about 1,400 stay. Read the real number from SonarCloud after the merge. They are of two kinds:
   - About 1,130 on lines upstream wrote, most of them decompiled from the original game. Fixing them makes every later merge from upstream conflict.
   - About 270 on the fork's lines, in rule classes the stack skipped on purpose:

     | Rule | Why skipped |
     | --- | --- |
     | S134, S3776 (nesting, cognitive complexity) | Needs restructuring, not a local edit |
     | S6494 (`std::print`) | GCC 13 MinGW has no `<print>` |
     | S5281 (format string not a literal) | Translated format strings; accepted in SonarCloud before |
     | S7040 (delimited escapes, `\x{}`) | Not on every compiler the game builds with |
     | S5421 (non-const global variables) | File-scope state is the code base's pattern throughout |
     | S2479 (control characters) | The French strings' no-break spaces |
     | S5020, S2245 (`rand`) | The replays depend on its sequence |
     | S3624 (copy and move semantics) | Changes engine types that other files use |
     | S6168 (`std::jthread`) | Not on every compiler the game builds with |
     | S9382 in `Platform/Web/shell.html` | Reading picked files in parallel holds all of them in memory (#196) |

   The choices: accept them in SonarCloud issue by issue, switch the rules off for the project (or for the upstream files by path), or fix the upstream lines as well. Recommended: switch off the rules in the table for the project, and exclude nothing by path, so that new code in upstream's files is still checked; leave upstream's lines as they are.

## Code fixes

2. **Polygon walls read two floats past what `wall_length` checks.** `TEdgeSegment::install_wall` (upstream's code) ends each polygon line at `floatArrPtr[2]` and `[3]`. For the last of `n` points those are `floatArr[2n + 1]` and `floatArr[2n + 2]`, while `wall_length` accepts an array of `2n + 1` floats. CodeRabbit found it on #191. The replays running clean under the sanitizers don't show that the tables hold the two floats: `partman::ReadField` puts 64 zero bytes after each field, so a polygon of exactly `2n + 1` floats reads zeros there, and its last line ends at (0, 0) without a sanitizer noticing. To do:
   - Read the field length of every polygon wall in the four tables' DAT files and, where it is longer, what follows the last point (a repeat of the first point, or something else).
   - If every polygon repeats its first point there: require `2n + 3` in `wall_length`, which changes nothing for that data.
   - Otherwise (a polygon of exactly `2n + 1`, whose last line ends at (0, 0) today, or other values) any fix changes wall geometry: ask first.
3. **`TableView::Hash` leaves out `Pirates.LastBallRelease`.** The comment on `Hash` now says so (#193). Adding the field changes Skulduggery's view hashes, so check what compares them (`docs/replays.md`) and record again if a reference stores one.
4. **Picking game files twice in the web page can interleave.** `Platform/Web/shell.html` handles each `change` event in its own async run; a second selection made while the first is still being written runs alongside it. It was so before the stack. There are two inputs, `pickFiles` and `pickFolder`, so the guard has to cover both: one shared queue for every selection, or both inputs disabled until the write and the sync finish.
5. **No CI job compiles `BundledData.cpp`'s Windows block.** It sits behind `SCP_BUNDLED_DATA_HASH && _WIN32`, which only `tools/make-test-build.sh` sets, because it needs game data. #199 changed lines there that were checked by reading only. Either build it in CI with a folder of dummy files (never game data), or build a Windows test build locally after the merge and start it once.

## Checks and tooling

6. **Run `tools/check-function-comments.sh` in CI or in the pre-push hook.** #200 adds the script and the convention to `AGENTS.md` but nothing runs it. It needs `uvx` (lizard 1.25.1) and takes a few seconds for the whole project. Without it, coverage will drift again.
7. **Tell CodeRabbit not to review stacked pull requests on every push, or accept the rounds.** Each push to a layer started another bot round on it, with findings mostly about comments it had passed before. A `.coderabbit.yaml` setting for the review of non-default base branches would settle it either way.

## Text to correct by hand

8. **CodeRabbit's summary on #197 claims a behaviour change.** Its walkthrough says the game-over condition and wrapped-text line advancement change. They don't: all 28 replays match and the edits are equivalent rewrites. The summary is the bot's comment, not the description; nothing to do unless it misleads a reader later.

## Outcome

Decided on 2026-11-10. SonarCloud's settings (item 1) and the reply on #197 (item 8) are done; the rest is in review as #203 to #209, each based on `main` and independent of the others.

1. **Sonar: switched off what can't be followed, kept the design rules.** SonarCloud's project settings now ignore S5281, S6494, S7040, S6168, S5020, S2245 and S2479 for the whole project, and S5421 for all files instead of only `control*.cpp`. S134, S3776 and S3624 stay on outside the rules files, so new code is still flagged. The one S9382 in `shell.html` is accepted with a comment. Upstream's lines keep their issues, and nothing is excluded by path. SonarCloud showed 1,395 open issues on `main` before the change. #206 lists the rules in `docs/setup.md`.
2. **Polygon walls: `wall_length` requires `2n + 3`** (#203). A log of every polygon wall as the four tables loaded showed all 459 repeating their first point after the last one, with three floats more than the old minimum, so no wall changes.
3. **`Pirates.LastBallRelease` is hashed** (#204). No reference stores a hash, so nothing was recorded again.
4. **Both pickers, Forget and Play are off while files are written and synced** (#205).
5. **CI's `mingw` job builds once more with placeholder bundled data** (#207), checked locally with MinGW 13.
6. **The function comment check runs in CI's `lint` job and in the pre-push hook** (#208), with lizard pinned with hashes in `tools/lizard-requirements.txt`.
7. **CodeRabbit reviews a pull request once, not on every push** (#209); a new round is asked for with `@coderabbitai review`.
8. **A reply on #197** records that its summary's behaviour change isn't one.

Every code change kept all 28 replays matching.
