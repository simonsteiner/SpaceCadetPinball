#!/usr/bin/env bash
# Rejects staged files that look like the original game's data, which is Microsoft's.
# .gitignore already excludes these names; this catches `git add -f`.
# Usage: tools/hooks/no-game-data.sh <staged file>...
set -u

failed=0
for file in "$@"; do
    name=$(basename "$file" | tr '[:upper:]' '[:lower:]')
    case "$name" in
        *.dat | *.wav | *.mid | *.exe | *.7z | table.bmp | wavemix.inf)
            echo "Game data must not be committed: $file" >&2
            failed=1
            ;;
    esac
done
if [ "$failed" -ne 0 ]; then
    echo "Unstage it with: git restore --staged <file>" >&2
fi
exit "$failed"
