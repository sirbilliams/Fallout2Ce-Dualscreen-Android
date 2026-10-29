#!/usr/bin/env python3
"""Compare the mui_preview PNGs with the committed goldens (tools/mui_preview/golden).

  compare_golden.py <preview dir> [diff dir]

A pixel differs when any channel is off by more than PIXEL_TOLERANCE; an image
fails when more than MAX_DIFF_FRACTION of its pixels differ (anti-aliasing
noise between machines stays below it, a moved, overflowing or overlapping
element does not). Writes <name>.diff.png (golden | new | differences in red)
for each failing image. `--update` copies the new images over the goldens.
Exit 1 on any failure.
"""
import os
import shutil
import sys

import numpy as np
from PIL import Image

PIXEL_TOLERANCE = 24
MAX_DIFF_FRACTION = 0.002
GOLDEN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "golden")
NAMES = ["bottom", "companion", "widgets", "pieces", "poweron"]


def main():
    args = [a for a in sys.argv[1:] if a != "--update"]
    update = "--update" in sys.argv
    src = args[0]
    out = args[1] if len(args) > 1 else os.path.join(src, "diff")
    os.makedirs(out, exist_ok=True)
    if update:
        os.makedirs(GOLDEN, exist_ok=True)
        for n in NAMES:
            shutil.copy(os.path.join(src, n + ".png"), os.path.join(GOLDEN, n + ".png"))
        print("goldens updated")
        return 0
    failed = 0
    for n in NAMES:
        new_path = os.path.join(src, n + ".png")
        gold_path = os.path.join(GOLDEN, n + ".png")
        if not os.path.exists(new_path) or not os.path.exists(gold_path):
            print(f"FAIL {n}: missing {'preview' if not os.path.exists(new_path) else 'golden'} image")
            failed += 1
            continue
        new = Image.open(new_path).convert("RGB")
        gold = Image.open(gold_path).convert("RGB")
        if new.size != gold.size:
            print(f"FAIL {n}: size {new.size} != golden {gold.size}")
            failed += 1
            continue
        a = np.asarray(gold, dtype=np.int16)
        b = np.asarray(new, dtype=np.int16)
        bad = (np.abs(a - b).max(axis=2) > PIXEL_TOLERANCE)
        frac = bad.mean()
        ok = frac <= MAX_DIFF_FRACTION
        print(f"{'ok  ' if ok else 'FAIL'} {n}: {frac * 100:.3f}% pixels differ (limit {MAX_DIFF_FRACTION * 100:.2f}%)")
        if not ok:
            failed += 1
            red = np.asarray(new).copy()
            red[bad] = (255, 0, 0)
            w, h = new.size
            sheet = Image.new("RGB", (w * 3, h))
            sheet.paste(gold, (0, 0))
            sheet.paste(new, (w, 0))
            sheet.paste(Image.fromarray(red), (2 * w, 0))
            sheet.save(os.path.join(out, n + ".diff.png"))
    return 1 if failed else 0


sys.exit(main())
