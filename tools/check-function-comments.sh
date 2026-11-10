#!/usr/bin/env bash
# Lists the functions that have a branch or a loop (cyclomatic complexity 2 or more) and no comment above their
# definition. CodeRabbit's Docstring Coverage check counts these on the functions a pull request touches and warns
# below 80 %; a comment on the declaration in the header doesn't count for it (AGENTS.md).
# Usage: tools/check-function-comments.sh [file]...
#   with files, checks those; without, the project's own sources (not ImGui, third_party/ or generated files)
# Exits 1 when a function lacks its comment. lizard measures the complexity; uvx fetches it at the pinned version.
set -euo pipefail

# From the script's own path, not git, which CI's container doesn't trust before safe.directory is set
cd "$(dirname "$0")/.."

files=("$@")
if [[ ${#files[@]} -eq 0 ]]; then
	while IFS= read -r file; do
		files+=("$file")
	done < <(git ls-files 'SpaceCadetPinball/*.cpp' 'SpaceCadetPinball/*.h' 'tools/*.cpp' 'tools/*.py' 'Platform/*.java' \
		':(exclude,glob)SpaceCadetPinball/imgui*' ':(exclude,glob)SpaceCadetPinball/imstb_*' \
		':(exclude)SpaceCadetPinball/imconfig.h' ':(exclude)SpaceCadetPinball/EmbeddedData.cpp' \
		':(exclude)SpaceCadetPinball/shaders/PostEffectShaders.h')
fi

uvx -q --from lizard==1.25.1 lizard --csv "${files[@]}" | python3 -I -c '
import csv, re, sys

# Lines that may stand between a comment and the definition it describes
BETWEEN = re.compile(r"\s*(template\s*<|\[\[|@\w+|static\s*$|inline\s*$)")
sources = {}
missing = total = 0
for row in csv.reader(sys.stdin):
    complexity, path, name, start = int(row[1]), row[6], row[7], int(row[9])
    if complexity < 2:
        continue
    total += 1
    if path not in sources:
        with open(path, encoding="utf-8", errors="replace") as source:
            sources[path] = source.read().split("\n")
    lines = sources[path]
    above = start - 2
    while above >= 0 and BETWEEN.match(lines[above]):
        above -= 1
    before = lines[above].strip() if above >= 0 else ""
    # A Python docstring is the first statement of the body instead
    after = lines[start].strip() if start < len(lines) else ""
    if before.startswith(("//", "/*", "#")) or before.endswith("*/") or after.startswith(("\"\"\"", "\x27\x27\x27")):
        continue
    missing += 1
    print(f"{path}:{start}: {name} (complexity {complexity}) has no comment")
print(f"{total - missing} of {total} functions with a branch or a loop have a comment")
sys.exit(1 if missing else 0)
'
