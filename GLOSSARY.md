# Space Cadet Pinball

A decompiled reimplementation of the pinball game bundled with Windows (originally one of three tables in Full Tilt! Pinball). These terms describe the game and its data, not the code layout.

## Game data

**Game data**:
The original Microsoft data files (a DAT file plus its sound and music files) that the player supplies. They are copyrighted and never ship with the project.
_Avoid_: Assets, resources, ROM

**DAT file**:
The single archive holding a table's art, depth maps, palette, collision geometry and strings. It is one of `PINBALL.DAT` (Windows), `CADET.DAT` (Full Tilt) or `DEMO.DAT` (Full Tilt demo).
_Avoid_: Resource file, PARTOUT file

**Group**:
A named record in a DAT file that holds the entries (bitmaps, depth maps, numeric arrays, strings) for one table part.
_Avoid_: Record, chunk

**Depth map**:
A 16-bit per-pixel depth image paired with a sprite, used to sort overlapping art.
_Avoid_: zMap, z-buffer image

**Windows data**:
Game data from the 3D Pinball for Windows release (`PINBALL.DAT`).
_Avoid_: 3DPB data, XP data

**Full Tilt data**:
Game data from Full Tilt! Pinball (`CADET.DAT`). It adds higher-resolution art and Full Tilt-only features.
_Avoid_: Cadet data, HD data

## Table

**Table**:
The whole playfield and its rules: every component, the score state and the current player.
_Avoid_: Board, level, playfield (when the rules are meant)

**Component**:
One interactive or visual part of the table, such as a flipper, bumper, light or text box, built from a group.
_Avoid_: Object, entity, part

**Message**:
A coded event one component sends another, the only way components react to each other.
_Avoid_: Signal, callback, event

**Table rules**:
The scripted game logic that decides how components react to messages: scoring, missions, ranks and lights.
_Avoid_: Control (outside code), game script

**Drain**:
The gap below the flippers. A ball that falls in is lost.
_Avoid_: Outlane (that is a separate lane), gutter

**Tilt**:
The penalty state, triggered by excessive nudging, that disables the player's controls for the current ball.
_Avoid_: Tilt lock (when the player-facing state is meant)

**Nudge**:
The player shoving the table to influence the ball.
_Avoid_: Bump, shake

## Play

**Mission**:
A goal the table assigns, completed by making a series of shots, that awards points and progress toward the next rank.
_Avoid_: Quest, objective

**Rank**:
The player's standing, from Cadet up to Fleet Admiral (nine steps), raised by completing missions.
_Avoid_: Level

**Replay**:
A free game awarded when a score threshold is reached.
_Avoid_: Extra game, free game (as the term), recording

**Extra ball**:
An additional ball awarded within the current game.
_Avoid_: Replay

**Multiball**:
Play with more than one ball on the table at once.

**Jackpot**:
An accumulating bonus pool awarded during play.

**Demo mode**:
The attract mode in which the table plays itself.
_Avoid_: Demo (ambiguous with `DEMO.DAT`), autoplay
