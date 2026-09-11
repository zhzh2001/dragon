#!/usr/bin/env python3
"""Split a four-panel turnaround into the plates the Hunyuan one-shot wants.

    python3 tools/split_turnaround.py artifacts/dragon-options/ashcoil-turnaround.png \
        --out artifacts/dragon-options/ashcoil-views

The generator lays a turnaround out as front, left, back, right at one scale on
one flat background, but the panels are not equal quarters -- a long-bodied
animal's profile is three times the width of its front view -- so slicing at
width/4 clips tails and wingtips. Splitting on empty columns does not work
either: on a serpent turnaround one view's tail tip reaches *past* the next
view's wingtip, so the panels overlap in x with no blank column between them.

So this labels connected components instead. Each animal is one blob, which
gives an exact bounding box per view, and any part of a *neighbouring* view
that reaches into that box is repainted with the background colour -- otherwise
the plate would carry a floating tail tip that Hunyuan's subject segmentation
could latch onto.

Output names follow the one-shot's slot order (tools/hunyuan_oneshot.md,
artifacts/dragon-options/README.md): 1-front, 2-back, 3-left, 4-right. The
panels are *drawn* front, left, back, right, so 2 and 3 swap when named. The
background is deliberately left in -- Hunyuan segments the subject itself, and
the RGBA cutouts the local pipeline needs are wasted work here.

Asserts it found exactly four panels rather than writing whatever it found: a
silent miscount hands the generator a plate with two animals on it.
"""
import argparse
import pathlib
import sys

import numpy as np
from PIL import Image

# Panel order as drawn, left to right, mapped to the upload slot names.
PANEL_NAMES = ['1-front', '3-left', '2-back', '4-right']


def row_runs(row):
    """[(start, end_inclusive)] for each True run in a boolean row."""
    edges = np.flatnonzero(np.diff(np.concatenate(([False], row, [False]))))
    return list(zip(edges[0::2], edges[1::2] - 1))


def components(mask):
    """Label 8-connected components run by run. Returns [(count, bbox, runs)]."""
    parent = []

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[max(ra, rb)] = min(ra, rb)

    labelled = []          # (y, x0, x1, label)
    prev = []              # runs of the previous row as (x0, x1, label)
    for y in range(mask.shape[0]):
        cur = []
        for x0, x1 in row_runs(mask[y]):
            lab = None
            for px0, px1, plab in prev:
                if px0 <= x1 + 1 and x0 <= px1 + 1:      # 8-connected overlap
                    if lab is None:
                        lab = find(plab)
                    else:
                        union(lab, plab)
            if lab is None:
                lab = len(parent)
                parent.append(lab)
            cur.append((x0, x1, lab))
            labelled.append((y, x0, x1, lab))
        prev = cur

    merged = {}
    for y, x0, x1, lab in labelled:
        root = find(lab)
        count, bx0, by0, bx1, by1, runs = merged.get(
            root, (0, mask.shape[1], mask.shape[0], -1, -1, []))
        merged[root] = (count + (x1 - x0 + 1), min(bx0, x0), min(by0, y),
                        max(bx1, x1), max(by1, y), runs + [(y, x0, x1)])
    return [(c, (bx0, by0, bx1, by1), runs)
            for c, bx0, by0, bx1, by1, runs in merged.values()]


ap = argparse.ArgumentParser()
ap.add_argument('image', help='the four-panel turnaround PNG')
ap.add_argument('--out', required=True, help='directory to write the four plates into')
ap.add_argument('--threshold', type=int, default=14,
                help='per-channel deviation from the background that counts as subject')
ap.add_argument('--pad', type=float, default=0.06,
                help='padding around each subject, as a fraction of its larger side')
args = ap.parse_args()

img = Image.open(args.image).convert('RGB')
px = np.asarray(img).astype(np.int16)
h, w, _ = px.shape

# Most of the frame is background, so its median is the background colour.
bg = np.median(px.reshape(-1, 3), axis=0)
mask = np.abs(px - bg).max(axis=2) > args.threshold

comps = sorted(components(mask), key=lambda c: -c[0])
if len(comps) < 4:
    print(f'FAIL {args.image}: {len(comps)} components, expected at least 4',
          file=sys.stderr)
    sys.exit(1)

# The four animals dwarf any speck of compression noise; check that, rather
# than trusting the sort, so a turnaround where two views actually touch is
# caught here instead of downstream in the generator.
panels = comps[:4]
if len(comps) > 4 and comps[4][0] * 8 > panels[3][0]:
    print(f'FAIL {args.image}: 5th component has {comps[4][0]} px against the '
          f'4th panel\'s {panels[3][0]} -- the panels did not separate cleanly',
          file=sys.stderr)
    sys.exit(1)

panels.sort(key=lambda c: c[1][0])       # left to right, as drawn

# Paint every panel's own pixels white in its own stencil, so a crop can erase
# whatever belongs to a neighbour.
own = np.zeros((len(panels), h, w), dtype=bool)
for i, (_, _, runs) in enumerate(panels):
    for y, x0, x1 in runs:
        own[i, y, x0:x1 + 1] = True

out = pathlib.Path(args.out)
out.mkdir(parents=True, exist_ok=True)

for i, ((count, (x0, y0, x1, y1), _), name) in enumerate(zip(panels, PANEL_NAMES)):
    pad = int(round(args.pad * max(x1 - x0, y1 - y0)))
    bx0, by0 = max(0, x0 - pad), max(0, y0 - pad)
    bx1, by1 = min(w, x1 + 1 + pad), min(h, y1 + 1 + pad)

    plate = px[by0:by1, bx0:bx1].copy()
    intruder = mask[by0:by1, bx0:bx1] & ~own[i, by0:by1, bx0:bx1]
    erased = int(intruder.sum())
    plate[intruder] = bg

    Image.fromarray(plate.astype(np.uint8)).save(out / f'{name}.png')
    print(f'{out / f"{name}.png"}  {bx1 - bx0}x{by1 - by0}  '
          f'subject {count} px, erased {erased} px of neighbours')
