#!/usr/bin/env bash
# Lists the functions that have a branch or a loop (cyclomatic complexity 2 or more) and no comment above their
# definition. CodeRabbit's Docstring Coverage check counts these on the functions a pull request touches and warns
# below 80 %; a comment on the declaration in the header doesn't count for it (AGENTS.md). This is an approximation of
# that check, not its rules: it looks only at the line above a definition (for Python, also at the first line of the
# body), so a code line that ends in a block comment passes too.
# Usage: tools/check-function-comments.sh [file]...
#   with files, checks those; without, the project's own sources (not ImGui, third_party/ or generated files)
# Exits 1 when a function lacks its comment, 2 when a file is missing or the sources can't be listed, and with
# lizard's status when that fails. lizard measures the complexity; uvx fetches it at the pinned version.
set -euo pipefail

# From the script's own path, not git, which CI's container doesn't trust before safe.directory is set
cd "$(dirname "$0")/.."

files=("$@")
for file in "${files[@]}"; do
	if [[ ! -f "$file" ]]; then
		echo "no such file, from the top of the repository: $file" >&2
		exit 2
	fi
done
if [[ ${#files[@]} -eq 0 ]]; then
	# Captured, so a failing git stops the script; lizard would check the whole folder when given no files
	listed=$(git ls-files 'SpaceCadetPinball/*.cpp' 'SpaceCadetPinball/*.h' 'tools/*.cpp' 'tools/*.py' 'Platform/*.java' \
		':(exclude,glob)SpaceCadetPinball/imgui*' ':(exclude,glob)SpaceCadetPinball/imstb_*' \
		':(exclude)SpaceCadetPinball/imconfig.h' ':(exclude)SpaceCadetPinball/EmbeddedData.cpp' \
		':(exclude)SpaceCadetPinball/shaders/PostEffectShaders.h')
	if [[ -z "$listed" ]]; then
		echo "git lists no source files" >&2
		exit 2
	fi
	mapfile -t files <<<"$listed"
fi

# Measured before the report is made, so a failing lizard prints no count
measured=$(uvx -q --from lizard==1.25.1 lizard --csv "${files[@]}")

python3 -I -c '
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
    if path.endswith(".py"):
        # A # comment above, or a docstring as the first statement of the body, after a signature of any length
        body = start - 1
        while body < len(lines) and not lines[body].split("#")[0].rstrip().endswith(":"):
            body += 1
        body += 1
        while body < len(lines) and not lines[body].strip():
            body += 1
        first = lines[body].strip() if body < len(lines) else ""
        documented = before.startswith("#") or first.startswith(("\"\"\"", "\x27\x27\x27"))
    else:
        # Not a line that starts with #: in C++ that is a preprocessor directive
        documented = before.startswith(("//", "/*")) or before.endswith("*/")
    if documented:
        continue
    missing += 1
    print(f"{path}:{start}: {name} (complexity {complexity}) has no comment")
print(f"{total - missing} of {total} functions with a branch or a loop have a comment")
sys.exit(1 if missing else 0)
' <<<"$measured"
