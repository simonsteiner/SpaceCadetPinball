#!/usr/bin/env bash
# Checks that a commit message header follows Conventional Commits and fits in 72 characters.
# Merges, reverts and fixup or squash commits that git writes itself pass unchanged.
# Usage: tools/hooks/commit-msg.sh <commit message file>
set -u

# The first line that isn't a comment
header=$(grep -v -m 1 '^#' "$1" | tr -d '\r')

case "$header" in
    "Merge "* | "Revert \""* | "fixup! "* | "squash! "* | "amend! "*) exit 0 ;;
esac

types='feat|fix|refactor|perf|test|docs|style|build|ci|chore|revert'
if ! grep -qE "^($types)(\([a-z0-9/_.-]+\))?!?: [^ ]" <<<"$header"; then
    echo "Commit message header isn't a Conventional Commit: $header" >&2
    echo "Expected <type>[(scope)][!]: <description>, with type one of: ${types//|/, }" >&2
    exit 1
fi
if [ "${#header}" -gt 72 ]; then
    echo "Commit message header is ${#header} characters; keep it to 72: $header" >&2
    exit 1
fi
