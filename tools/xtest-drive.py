#!/usr/bin/env python3
# Sends keys to a window through XTest and takes screenshots of it, for comparing runs step by step.
# Usage: uv run --with python-xlib --with pillow python3 -I tools/xtest-drive.py DISPLAY TITLE OUTDIR [--move] STEP...
# TITLE is part of the window's title or class. --move puts the window at 0,0 first (Xvfb has no window manager).
# Steps: k:KEYSYM[:HOLD_SECONDS] presses a key, t:TEXT types letters and spaces, s:SECONDS waits,
# p:NAME saves the window as OUTDIR/NAME.png.
import sys
import time

from PIL import ImageGrab
from Xlib import X, XK, display
from Xlib.ext import xtest

args = sys.argv[1:]
move = "--move" in args
if move:
    args.remove("--move")
display_name, title, out_dir, steps = args[0], args[1], args[2], args[3:]

d = display.Display(display_name)
root = d.screen().root


def find(window):
    for child in window.query_tree().children:
        name = child.get_wm_name() or ""
        classes = child.get_wm_class() or ()
        if child.get_attributes().map_state == X.IsViewable and (
            title in name or any(title in c for c in classes)
        ):
            return child
        found = find(child)
        if found:
            return found
    return None


win = find(root)
if not win:
    sys.exit(f"no window matching {title!r} on {display_name}")
if move:
    win.configure(x=0, y=0)
    d.sync()
    time.sleep(1)
geometry = win.get_geometry()
origin = win.translate_coords(root, 0, 0)
x0, y0 = -origin.x, -origin.y
win.set_input_focus(X.RevertToParent, X.CurrentTime)
d.sync()


def press(keysym, hold=0.08):
    code = d.keysym_to_keycode(XK.string_to_keysym(keysym))
    xtest.fake_input(d, X.KeyPress, code)
    d.sync()
    time.sleep(hold)
    xtest.fake_input(d, X.KeyRelease, code)
    d.sync()
    time.sleep(0.15)


for step in steps:
    op, arg = step.split(":", 1)
    if op == "k":
        keysym, _, hold = arg.partition(":")
        press(keysym, float(hold) if hold else 0.08)
    elif op == "t":
        for ch in arg:
            press("space" if ch == " " else ch)
    elif op == "s":
        time.sleep(float(arg))
    elif op == "p":
        box = (x0, y0, x0 + geometry.width, y0 + geometry.height)
        ImageGrab.grab(xdisplay=display_name).crop(box).save(f"{out_dir}/{arg}.png")
    else:
        sys.exit(f"unknown step {step!r}")
