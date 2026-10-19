#!/usr/bin/env bash
# Builds the game with SCP_WARNINGS=ON, as CI does, and plays back the reference replays.
# Runs only on Linux, and only when the push changes code or build files.
# The replays need the game data, which lives outside the repository; they are skipped when it isn't there.
# Usage: tools/hooks/pre-push.sh [pushed file]...
set -u

if [ "$(uname -s)" != Linux ]; then
    echo "pre-push: build check runs on Linux only, skipped"
    exit 0
fi

# lefthook skips this job when nothing is pushed, so no arguments only happens on a manual run
# (or lefthook run --force); then check everything
if [ $# -gt 0 ]; then
    relevant=0
    for file in "$@"; do
        case "$file" in
            SpaceCadetPinball/* | CMakeLists.txt | CMakePresets.json | CMakeModules/* | tools/*) relevant=1 ;;
        esac
    done
    if [ "$relevant" -eq 0 ]; then
        echo "pre-push: no code or build changes, skipped"
        exit 0
    fi
fi

root=$(git rev-parse --show-toplevel)
cd "$root" || exit 1

# Its own build directory, so the user's linux preset keeps its settings.
# The executable still lands in bin/, like every preset's.
build_dir=build/pre-push
cmake --preset linux -B "$build_dir" -DSCP_WARNINGS=ON >/dev/null || exit 1
cmake --build "$build_dir" || exit 1

replays=${SCP_REPLAYS_DIR:-../replays}
gamedata=${SCP_GAMEDATA_DIR:-../gamedata}
if [ -d "$replays" ] && [ -d "$gamedata" ]; then
    tools/check-replays.sh "$replays" "$gamedata" || exit 1
else
    echo "pre-push: no replays or game data at $replays and $gamedata, replay check skipped"
    echo "pre-push: set SCP_REPLAYS_DIR and SCP_GAMEDATA_DIR to point at them"
fi
