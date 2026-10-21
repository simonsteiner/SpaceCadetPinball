"""Compares DefinitiveText::Shape with python-bidi and arabic-reshaper (tools/check-bidi.sh).

Reads the harness's output from standard input: each case's text and its shaped text, each ended by a NUL byte.
"""

import sys

import arabic_reshaper
from bidi import get_display

# Shape mirrors brackets in right-to-left text, as Unicode's rule L4 asks; python-bidi leaves that to the renderer
MIRRORED = str.maketrans("()[]{}<>«»", ")(][}{><»«")
RIGHT_TO_LEFT = [(0x0590, 0x08FF), (0xFB1D, 0xFDFF), (0xFE70, 0xFEFF), (0x200F, 0x200F)]

# Only the lam-alef ligatures, which Shape makes from Presentation Forms-B (lam with alef maksura is in Forms-A), and
# vowel marks stay
LAM_ALEF = "ARABIC LIGATURE LAM WITH ALEF"
reshaper = arabic_reshaper.ArabicReshaper(
    configuration={
        "delete_harakat": False,
        "support_ligatures": True,
        **{
            name: name == LAM_ALEF or name.startswith(LAM_ALEF + " WITH ")
            for name, _ in arabic_reshaper.ligatures.LIGATURES
        },
    }
)


def has_right_to_left(text):
    return any(first <= ord(c) <= last for c in text for first, last in RIGHT_TO_LEFT)


def expected(text):
    # Shape leaves text with nothing right to left as it is, and reads every other line right to left
    if not has_right_to_left(text):
        return text
    lines = []
    for line in text.split("\n"):
        visual = get_display(reshaper.reshape(line), base_dir="R")
        lines.append(visual.replace("\u200e", "").replace("\u200f", ""))
    return "\n".join(lines)


def candidates(text):
    # A bracket python-bidi didn't mirror can be on either side of the comparison, so both are tried per character
    return text, text.translate(MIRRORED)


def matches(shaped, reference):
    if len(shaped) != len(reference):
        return False
    plain, mirrored = candidates(reference)
    return all(s in (p, m) for s, p, m in zip(shaped, plain, mirrored))


def main():
    fields = sys.stdin.buffer.read().split(b"\0")
    if not fields or fields[-1] != b"" or len(fields) % 2 != 1:
        print("check-bidi: the harness's output is incomplete", file=sys.stderr)
        return 2
    texts = [field.decode("utf-8") for field in fields[:-1]]
    cases = list(zip(texts[0::2], texts[1::2]))
    failed = 0
    for text, shaped in cases:
        reference = expected(text)
        if not matches(shaped, reference):
            failed += 1
            print(
                f"differs: {text!r}\n  Shape:     {shaped!r}\n  reference: {reference!r}"
            )
    checked = sum(has_right_to_left(text) for text, _ in cases)
    print(
        f"check-bidi: {len(cases) - failed} of {len(cases)} cases match ({checked} with right-to-left text)"
    )
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
