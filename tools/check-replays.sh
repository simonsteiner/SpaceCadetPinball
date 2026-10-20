#!/usr/bin/env bash
# Plays back every .replay file in a folder in both editions and reports the ones whose play differs.
# Usage: tools/check-replays.sh <replay folder> <game data folder> [executable]
# Each replay must match its recording in Classic and in Space Cadet 2026, and the table view's hash must be the
# same in both on every frame, so presentation code that changes the table fails at the first frame it does.
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

# Plays one replay in one edition; sets output and status
play() {
    status=0
    # The game looks for its data in the working directory
    output=$(cd "$data" && "$exe" -replay "$1" -edition "$2" 2>&1) || status=$?
}

# Prints why a run failed
report() {
    local reason
    reason=$(grep -E '^(Replay|Expected|Actual|Recorded|Loaded|Not a|Could not|Unknown)' <<<"$output")
    if [ -z "$reason" ]; then
        # Not a replay result, such as a crash: show how the game ended
        reason=$(printf 'Exit status %s\n%s' "$status" "$(grep -v '^view ' <<<"$output" | tail -n 5)")
    fi
    sed 's/^/      /' <<<"$reason"
}

failed=0
count=0
for replay in "$replays"/*.replay; do
    [ -e "$replay" ] || continue
    count=$((count + 1))
    name=$(basename "$replay")

    play "$replay" classic
    if [ "$status" -ne 0 ]; then
        echo "FAIL  $name (Classic)"
        report
        failed=$((failed + 1))
        continue
    fi
    classic=$output

    play "$replay" definitive
    if [ "$status" -ne 0 ]; then
        echo "FAIL  $name (Space Cadet 2026)"
        report
        failed=$((failed + 1))
        continue
    fi

    # The first frame whose view differs between the editions
    first=$(diff <(grep '^view ' <<<"$classic") <(grep '^view ' <<<"$output") | grep -m 1 '^[<>] view ' | cut -d' ' -f3)
    if [ -n "$first" ]; then
        echo "FAIL  $name"
        echo "      The table view differs between Classic and Space Cadet 2026 from frame $first on."
        failed=$((failed + 1))
        continue
    fi
    echo "ok    $name: $(tail -n 1 <<<"$output") Both editions match."
done

if [ "$count" -eq 0 ]; then
    echo "No .replay files in $replays" >&2
    exit 2
fi
echo "$((count - failed)) of $count replays match in both editions."
[ "$failed" -eq 0 ]
