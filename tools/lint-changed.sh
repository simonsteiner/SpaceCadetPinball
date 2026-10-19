#!/usr/bin/env bash
# Checks the lines changed since a branch left its base: clang-format, then clang-tidy.
# Usage: tools/lint-changed.sh <base> [build-dir]
#   base       branch or commit the work started from, for example main; the lines changed since
#              its merge base with HEAD are checked, so commits that landed on base later don't count
#   build-dir  CMake build with compile_commands.json for clang-tidy; without one, clang-tidy is skipped
# The tools come pinned through uvx, at the version in tools/llvm-requirements.txt (which CI installs);
# set CLANG_FORMAT_DIR or CLANG_TIDY_DIR to a folder that has them instead.
set -euo pipefail

usage='usage: tools/lint-changed.sh <base> [build-dir]'
base_ref=${1:?$usage}
build_dir=${2:-}
repo=$(git rev-parse --show-toplevel)
if [ -n "$build_dir" ]; then
	build_dir=$(cd "$build_dir" && pwd)
fi
cd "$repo" # git diff and clang-tidy-diff.py -p1 name files from the top of the repository
llvm_version=$(sed -n 's/^clang-format==\([0-9.]*\).*/\1/p' "$repo/tools/llvm-requirements.txt")
if [ -z "$llvm_version" ]; then
	echo "no clang-format version in tools/llvm-requirements.txt" >&2
	exit 2
fi
if ! base=$(git merge-base "$base_ref" HEAD); then
	echo "no merge base between '$base_ref' and HEAD" >&2
	exit 2
fi

# Vendored and generated files are never checked; .clang-format-ignore lists the same files for clang-format
pathspec=(':(glob)SpaceCadetPinball/**' ':(exclude,glob)SpaceCadetPinball/imgui*' ':(exclude,glob)SpaceCadetPinball/imstb_*'
	':(exclude)SpaceCadetPinball/imconfig.h' ':(exclude)SpaceCadetPinball/EmbeddedData.cpp' ':(exclude)SpaceCadetPinball/translations.cpp')

run_tool() { # package, command, args...
	local package=$1 command=$2 dir_var
	shift 2
	dir_var=$([ "$package" = clang-format ] && echo "${CLANG_FORMAT_DIR:-}" || echo "${CLANG_TIDY_DIR:-}")
	if [ -n "$dir_var" ]; then
		PATH="$dir_var:$PATH" "$command" "$@"
	else
		uvx -q --from "$package==$llvm_version" "$command" "$@"
	fi
}

status=0

echo "clang-format on lines changed since $base ($base_ref)"
# git-clang-format --diff exits 0 when the lines are formatted, 1 when it prints a diff, 2 on its own errors;
# anything else (127 for a missing tool) is an error too
rc=0
run_tool clang-format git-clang-format --diff "$base" -- "${pathspec[@]}" || rc=$?
if [ "$rc" -eq 1 ]; then
	echo "Changed lines are not formatted. Run: uvx --from clang-format==$llvm_version git-clang-format $base"
	status=1
elif [ "$rc" -ne 0 ]; then
	echo "git-clang-format failed with exit code $rc" >&2
	exit "$rc"
fi

if [ -n "$build_dir" ]; then
	echo "clang-tidy on lines changed since $base ($base_ref)"
	changes=$(git diff -U0 "$base" -- "${pathspec[@]}")
	jobs=$(nproc 2>/dev/null || echo 2)
	# A .cpp runs with its compile command; .clang-tidy's empty HeaderFilterRegex keeps its headers' old warnings out
	if ! run_tool clang-tidy clang-tidy-diff.py -p1 -path "$build_dir" -iregex '.*\.cpp' -quiet -j "$jobs" <<<"$changes"; then
		status=1
	fi
	# A header has no compile command: clang-tidy borrows one from a similar .cpp, and the headers rely on the
	# precompiled header for the standard library and SDL, so pch.h is included first
	if ! run_tool clang-tidy clang-tidy-diff.py -p1 -path "$build_dir" -iregex '.*\.h' -quiet -j "$jobs" \
		-extra-arg=-include -extra-arg="$repo/SpaceCadetPinball/pch.h" <<<"$changes"; then
		status=1
	fi
fi

exit $status
