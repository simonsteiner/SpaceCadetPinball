# Follow-ups after the docstrings and Sonar stack

Date: 2026-11-10. Status: open. The stack #190 to #200 put a comment above every function with a branch or a loop (1,154 of 1,154, `tools/check-function-comments.sh`), took vendored code out of SonarCloud's analysis and fixed 474 of the Sonar issues on lines the fork wrote. This plan collects what its reviews found and the stack left alone, because each item is a code change, a decision or outside its scope.

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

2. **Polygon walls read two floats past what `wall_length` checks.** `TEdgeSegment::install_wall` (upstream's code) ends each polygon line at `floatArrPtr[2]` and `[3]`. For the last of `n` points those are `floatArr[2n + 1]` and `floatArr[2n + 2]`, while `wall_length` accepts an array of `2n + 1` floats. The replays run clean under the sanitizers, so the tables' arrays evidently hold the two floats; a damaged table with exactly `2n + 1` would be read out of bounds. CodeRabbit found it on #191. To do:
   - Look at what the four tables' DAT files hold after a polygon's last point (a repeat of the first point, or something else).
   - If it is the first point repeated: require `2n + 3` in `wall_length`, which changes nothing for valid data.
   - Otherwise ending the last line at the first point (`centerX2`, `centerY2`) would change wall geometry: ask first.
3. **`TableView::Hash` leaves out `Pirates.LastBallRelease`.** The comment on `Hash` now says so (#193). Adding the field changes Skulduggery's view hashes, so check what compares them (`docs/replays.md`) and record again if a reference stores one.
4. **Picking game files twice in the web page can interleave.** `Platform/Web/shell.html` handles each `change` event in its own async run; a second selection made while the first is still being written runs alongside it. It was so before the stack. A guard that queues the second selection, or disables the input while one runs, fixes it.
5. **No CI job compiles `BundledData.cpp`'s Windows block.** It sits behind `SCP_BUNDLED_DATA_HASH && _WIN32`, which only `tools/make-test-build.sh` sets, because it needs game data. #198 changed lines there that were checked by reading only. Either build it in CI with a folder of dummy files (never game data), or build a Windows test build locally after the merge and start it once.

## Checks and tooling

6. **Run `tools/check-function-comments.sh` in CI or in the pre-push hook.** #200 adds the script and the convention to `AGENTS.md` but nothing runs it. It needs `uvx` (lizard 1.25.1) and takes a few seconds for the whole project. Without it, coverage will drift again.
7. **Tell CodeRabbit not to review stacked pull requests on every push, or accept the rounds.** Each push to a layer started another bot round on it, with findings mostly about comments it had passed before. A `.coderabbit.yaml` setting for the review of non-default base branches would settle it either way.

## Text to correct by hand

8. **CodeRabbit's summary on #197 claims a behaviour change.** Its walkthrough says the game-over condition and wrapped-text line advancement change. They don't: all 28 replays match and the edits are equivalent rewrites. The summary is the bot's comment, not the description; nothing to do unless it misleads a reader later.
