#!/usr/bin/env bash
# Checks that Space Cadet 2026's presentation code, the SpaceCadetPinball/Definitive*.cpp and .h files, reads the
# table only through the table view: it may include pch.h, TableView.h, translations.h, options.h, ImGui, SDL, the
# standard library and other Definitive files, but not the table's or the components' headers (AGENTS.md).
# Usage: tools/check-presentation-includes.sh [file]...
#   with files, checks those that are presentation code (lefthook passes the staged files); without, checks them all
set -u

repo=$(git rev-parse --show-toplevel)
cd "$repo" || exit 2

is_presentation() {
    case "$1" in
        SpaceCadetPinball/Definitive*.cpp | SpaceCadetPinball/Definitive*.h) return 0 ;;
        *) return 1 ;;
    esac
}

files=()
if [ $# -gt 0 ]; then
    for file in "$@"; do
        if is_presentation "$file" && [ -f "$file" ]; then
            files+=("$file")
        fi
    done
else
    for file in SpaceCadetPinball/Definitive*.cpp SpaceCadetPinball/Definitive*.h; do
        [ -f "$file" ] && files+=("$file")
    done
fi

# In a variable, because bash can't take the quotes and angle brackets literally inside [[ =~ ]]
include_pattern='^[[:space:]]*#[[:space:]]*include[[:space:]]*(["<])([^">]+)[">]'
failed=0
for file in "${files[@]}"; do
    line_number=0
    while IFS= read -r line || [ -n "$line" ]; do
        line_number=$((line_number + 1))
        line=${line%$'\r'}
        [[ $line =~ $include_pattern ]] || continue
        kind=${BASH_REMATCH[1]} header=${BASH_REMATCH[2]}
        allowed=0
        if [ "$kind" = '<' ]; then
            # SDL and the standard library; the game's own headers are never included with angle brackets
            case "$header" in
                SDL3/* | SDL3_mixer/*) allowed=1 ;;
                *.*) ;;
                *) allowed=1 ;;
            esac
        else
            case "$header" in
                pch.h | TableView.h | translations.h | options.h | imgui.h | imgui_internal.h) allowed=1 ;;
                Definitive*.h) [[ $header != */* ]] && allowed=1 ;;
            esac
        fi
        if [ "$allowed" -eq 0 ]; then
            closing='"'
            [ "$kind" = '<' ] && closing='>'
            echo "$file:$line_number: presentation code may not include ${kind}${header}${closing}; read the table through TableView.h" >&2
            failed=1
        fi
    done <"$file"
done
exit "$failed"
