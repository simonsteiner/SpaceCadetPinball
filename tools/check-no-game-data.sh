#!/usr/bin/env bash
# Fails if a folder holds anything that looks like the original game's data. The web build's output goes through
# this before it is published, because Microsoft's files must never be hosted.
set -euo pipefail
dir=${1:?usage: tools/check-no-game-data.sh <folder>}
found=$(find "$dir" -type f \( -iname '*.dat' -o -iname '*.wav' -o -iname '*.mid' -o -iname '*.mds' -o -iname '*.exe' \
	-o -iname 'table.bmp' -o -iname 'wavemix.inf' \) -print)
if [ -n "$found" ]; then
	echo "Game data must not be published:"
	echo "$found"
	exit 1
fi
echo "No game data in $dir"
