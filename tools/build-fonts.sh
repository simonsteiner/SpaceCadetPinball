#!/usr/bin/env bash
# Makes Space Cadet 2026's fonts in third_party/fonts/ from pinned upstream files (third_party/README.md).
# Exo 2 and Noto come as variable fonts, which ImGui's rasteriser reads only at their default instance, so each is cut
# to a static weight and to the scripts the game uses. DSEG7 Classic is copied unchanged: its licence reserves the
# name "DSEG" for unmodified files. The fonts are committed, so building the game needs none of this; run it only to
# change a font. Needs curl, sha256sum, unzip and uv.
set -euo pipefail

out=$(cd "$(dirname "$0")/../third_party/fonts" && pwd)
google_fonts=https://raw.githubusercontent.com/google/fonts/47d54ca1ebb14fd864695f1d39dc734f39017c33/ofl
dseg_zip=https://github.com/keshikan/DSEG/releases/download/v0.46/fonts-DSEG_v046.zip
dseg_sha256=a6c2f43520971ca8067262e78d49025e605f749bf716ec5394bad9a0ee1c238c

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fetch() { # URL, SHA-256, file name in $tmp
	curl -sSfL -o "$tmp/$3" "$1"
	echo "$2  $tmp/$3" | sha256sum -c --quiet
}

fetch "$google_fonts/exo2/Exo2%5Bwght%5D.ttf" \
	205a448676a2586f9c57c25f3d5c58ca8db7e6cf5edf7506783a010c6fe2bfb5 Exo2.ttf
fetch "$google_fonts/notosans/NotoSans%5Bwdth,wght%5D.ttf" \
	bfb7bb691513f12e734dc346c03a03f784912432d7e3fa8e56efcf906fe86b3d NotoSans.ttf
fetch "$google_fonts/notosanshebrew/NotoSansHebrew%5Bwdth,wght%5D.ttf" \
	7ef36a2c3593758cdb622e1bdef4f84523e92fbc3ccc667438dd80ff54c2de88 NotoSansHebrew.ttf
fetch "$google_fonts/notosansarabic/NotoSansArabic%5Bwdth,wght%5D.ttf" \
	63111b5b2e074dd48cc67692e0a2726d86ee94c1c37fe8598257b7b4e87e869e NotoSansArabic.ttf
fetch "$dseg_zip" "$dseg_sha256" dseg.zip
curl -sSfL -o "$out/OFL-Exo2.txt" "$google_fonts/exo2/OFL.txt"
curl -sSfL -o "$out/OFL-Noto.txt" "$google_fonts/notosans/OFL.txt"

unzip -q -o "$tmp/dseg.zip" -d "$tmp/dseg"
install -m 644 "$tmp/dseg/fonts-DSEG_v046/DSEG7-Classic/DSEG7Classic-Bold.ttf" "$out/"
install -m 644 "$tmp/dseg/fonts-DSEG_v046/DSEG-LICENSE.txt" "$out/OFL-DSEG.txt"

uv run -q --no-project --with fonttools==4.67.0 python - "$tmp" "$out" <<'PY'
import sys
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
from fontTools import subset

src, out = sys.argv[1], sys.argv[2]

def ranges(*spans):
    return [u for first, last in spans for u in range(first, last + 1)]

# Latin for every Latin-script language the game has, Cyrillic for Russian, and the punctuation the strings use
latin_cyrillic = ranges((0x20, 0x7E), (0xA0, 0x24F), (0x400, 0x45F), (0x490, 0x491), (0x2010, 0x2027),
                        (0x2030, 0x203A), (0x20AC, 0x20AC), (0x2122, 0x2122), (0x2212, 0x2212))
# Direction marks, which the Hebrew and Arabic strings use, and joiners
marks = ranges((0x200C, 0x200F))
fonts = [
    ('Exo2.ttf', {'wght': 400}, latin_cyrillic, 'Exo2-Regular.ttf'),
    ('Exo2.ttf', {'wght': 600}, latin_cyrillic, 'Exo2-SemiBold.ttf'),
    ('NotoSans.ttf', {'wght': 400, 'wdth': 100}, ranges((0x370, 0x3FF)), 'NotoSans-Greek.ttf'),
    ('NotoSansHebrew.ttf', {'wght': 400, 'wdth': 100}, ranges((0x590, 0x5FF), (0xFB1D, 0xFB4F)) + marks,
     'NotoSansHebrew-Regular.ttf'),
    # Presentation Forms-B holds each letter's joined forms, which the game substitutes itself (DefinitiveText.cpp)
    ('NotoSansArabic.ttf', {'wght': 400, 'wdth': 100}, ranges((0x600, 0x6FF), (0xFE70, 0xFEFF)) + marks,
     'NotoSansArabic-Regular.ttf'),
]
for source, location, unicodes, name in fonts:
    # Keeps the source's modification time, so the same inputs make the same files
    font = instantiateVariableFont(TTFont(f'{src}/{source}', recalcTimestamp=False), location)
    options = subset.Options()
    # ImGui uses neither OpenType layout nor hinting
    options.layout_features = []
    options.hinting = False
    options.name_IDs = ['*']
    options.notdef_outline = True
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=unicodes)
    subsetter.subset(font)
    font.save(f'{out}/{name}')
    print(name, len(font.getGlyphOrder()), 'glyphs')
PY
