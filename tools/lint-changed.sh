#!/usr/bin/env bash
# Checks the lines changed since a base commit: clang-format, then clang-tidy.
# Usage: tools/lint-changed.sh <base> [build-dir]
#   base       commit to compare against, for example origin/main
#   build-dir  CMake build with compile_commands.json for clang-tidy; without one, clang-tidy is skipped
# The tools come pinned through uvx; set CLANG_FORMAT_DIR or CLANG_TIDY_DIR to a folder that has them instead.
set -euo pipefail

base=${1:?usage: tools/lint-changed.sh <base> [build-dir]}
build_dir=${2:-}
llvm_version=23.1.3

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

echo "clang-format on lines changed since $base"
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
	echo "clang-tidy on lines changed since $base"
	if ! git diff -U0 "$base" -- "${pathspec[@]}" | run_tool clang-tidy clang-tidy-diff.py -p1 -path "$build_dir" -iregex '.*\.cpp' -quiet -j "$(nproc 2>/dev/null || echo 2)"; then
		status=1
	fi
fi

exit $status
