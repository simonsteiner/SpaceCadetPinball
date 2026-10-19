#!/usr/bin/env bash
# Checks staged game sources against two conventions from AGENTS.md:
# every .cpp and .h is listed in CMakeLists.txt, and every game .cpp starts with #include "pch.h".
# Reads the staged versions, so unstaged edits don't hide a problem.
# Usage: tools/hooks/check-sources.sh <staged file>...
set -u

cmake_lists=$(git show :CMakeLists.txt)
bom=$'\xEF\xBB\xBF'
failed=0
for file in "$@"; do
    case "$file" in
        SpaceCadetPinball/*.cpp | SpaceCadetPinball/*.h) ;;
        *) continue ;;
    esac
    # Only files directly in SpaceCadetPinball/
    [ "$(dirname "$file")" = SpaceCadetPinball ] || continue

    if ! grep -qF "$file" <<<"$cmake_lists"; then
        echo "$file is not listed in CMakeLists.txt; add it to SOURCE_FILES" >&2
        failed=1
    fi

    name=$(basename "$file")
    case "$name" in
        *.h | imgui* | imstb* | EmbeddedData.cpp) continue ;;
    esac
    # Some files start with a UTF-8 byte order mark. Bash makes the bytes, since BSD sed doesn't read \x escapes.
    first_include=$(git show ":$file" | sed "1s/^$bom//" | grep -m 1 '^[[:space:]]*#[[:space:]]*include' | tr -d '\r')
    if [ "$first_include" != '#include "pch.h"' ]; then
        echo "$file must start with #include \"pch.h\" (first include is: ${first_include:-none})" >&2
        failed=1
    fi
done
exit "$failed"
