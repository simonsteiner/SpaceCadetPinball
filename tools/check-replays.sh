#!/usr/bin/env bash
# Plays back every .replay file in a folder and reports the ones whose play differs.
# Usage: tools/check-replays.sh <replay folder> <game data folder> [executable]
set -u

if [ $# -lt 2 ]; then
    echo "Usage: $0 <replay folder> <game data folder> [executable]" >&2
    exit 2
fi

replays=$(cd "$1" && pwd)
data=$2
exe=${3:-$(cd "$(dirname "$0")/.." && pwd)/bin/SpaceCadetPinball}
if [ ! -x "$exe" ]; then
    echo "Not an executable: $exe" >&2
    exit 2
fi
# Absolute, because the game runs from the data folder
exe=$(cd "$(dirname "$exe")" && pwd)/$(basename "$exe")

failed=0
count=0
for replay in "$replays"/*.replay; do
    [ -e "$replay" ] || continue
    count=$((count + 1))
    # The game looks for its data in the working directory
    if output=$(cd "$data" && "$exe" -replay "$replay" 2>&1); then
        echo "ok    $(basename "$replay"): $(tail -n 1 <<<"$output")"
    else
        status=$?
        echo "FAIL  $(basename "$replay")"
        reason=$(grep -E '^(Replay|Expected|Actual|Recorded|Loaded|Not a|Could not)' <<<"$output")
        if [ -z "$reason" ]; then
            # Not a replay result, such as a crash: show how the game ended
            reason=$(printf 'Exit status %s\n%s' "$status" "$(tail -n 5 <<<"$output")")
        fi
        sed 's/^/      /' <<<"$reason"
        failed=$((failed + 1))
    fi
done

if [ "$count" -eq 0 ]; then
    echo "No .replay files in $replays" >&2
    exit 2
fi
echo "$((count - failed)) of $count replays match."
[ "$failed" -eq 0 ]
