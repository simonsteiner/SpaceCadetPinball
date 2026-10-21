#!/usr/bin/env bash
# Compares DefinitiveText::Shape, Space Cadet 2026's right-to-left text, with python-bidi and arabic-reshaper on every
# Hebrew and Arabic string in translations.cpp, the edition hint as the game composes it, and a few made-up cases.
# Brackets may differ by mirroring, which Shape does and python-bidi doesn't. Needs no game data.
# Usage: tools/check-bidi.sh [build-dir]
#   build-dir  a configured CMake build (default build/linux), in which the bidi-harness target is built
# The reference comes pinned through uv from tools/bidi/requirements.txt; set BIDI_PYTHON to a Python that has those
# packages instead, as CI does.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
build_dir=${1:-$root/build/linux}
if [ ! -f "$build_dir/CMakeCache.txt" ]; then
	echo "Not a configured CMake build: $build_dir (run cmake --preset linux first)" >&2
	exit 2
fi
cmake --build "$build_dir" --target bidi-harness >/dev/null

# Mixed scripts the translations don't have: numbers after Latin text, separators, brackets, several lines
cases=(
	"אפשרויות > Space Cadet 2026"
	"Space Cadet 2026 גרסה"
	"שלב 2: 1,000 נקודות"
	"כדור 3/5"
	"50% הנחה"
	"(שלום) [עולם] {x}"
	$'שורה ראשונה\nSpace Cadet 2026 שורה'
	"النتيجة 1,250 نقطة"
	"«مرحبا» Space Cadet 2026 v1.2"
	"لا إله"
)

compare=$root/tools/bidi/compare.py
if [ -n "${BIDI_PYTHON:-}" ]; then
	"$build_dir/bidi-harness" "${cases[@]}" | "$BIDI_PYTHON" -I "$compare"
else
	"$build_dir/bidi-harness" "${cases[@]}" |
		uv run -q --no-project --with-requirements "$root/tools/bidi/requirements.txt" python -I "$compare"
fi
