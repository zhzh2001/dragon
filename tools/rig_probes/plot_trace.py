#!/usr/bin/env python3
"""Arc, onion-skin and spacing charts from a trace_probe CSV (Pillow only).

    plot_trace.py trace.csv out.png [--from S] [--to S] [--view side|front|top]
                  [--joints a,b,c] [--ghost N] [--title TEXT]

Three panels:
  1. Onion skin: the skeleton every --ghost frames as a faint line through
     root, chest, neck and head, the tail, the hind leg and the foreleg, with
     each tracked joint's path drawn as one dot per frame. Even dots are
     linear motion, bunched dots an ease, a gap a pop.
  2. Height (or the view's vertical axis) of each listed joint over time.
  3. Speed of each listed joint, metres per frame: a spike is a pop, a flat
     top a linear move, and peaks that line up are parts moving in lockstep.
Action rows (bite, claw, tail, fire) are marked on the time panels.
"""
import argparse
import csv
import math

from PIL import Image, ImageDraw

COLOURS = [(230, 80, 60), (240, 170, 40), (90, 190, 90), (70, 150, 230), (180, 100, 220),
           (230, 230, 230), (40, 210, 210), (250, 120, 180)]


def load(path):
    with open(path) as f:
        rows = list(csv.DictReader(f))
    return rows


def axes(view):
    # (horizontal, vertical) as (column suffix, sign). Forward is -z for the
    # generated assets, so side view puts the nose on the right.
    return {"side": (("_z", -1.0), ("_y", 1.0)), "front": (("_x", 1.0), ("_y", 1.0)),
            "top": (("_x", 1.0), ("_z", 1.0))}[view]


def main():
    p = argparse.ArgumentParser()
    p.add_argument("csv")
    p.add_argument("out")
    p.add_argument("--from", dest="t0", type=float, default=-1e9)
    p.add_argument("--to", dest="t1", type=float, default=1e9)
    p.add_argument("--view", default="side")
    p.add_argument("--joints", default="head,neck1,chest,tail_tip")
    p.add_argument("--ghost", type=int, default=6)
    p.add_argument("--title", default="")
    a = p.parse_args()
    rows = [r for r in load(a.csv) if a.t0 <= float(r["t"]) <= a.t1]
    if not rows:
        raise SystemExit("no rows in range")
    (hx, hs), (vx, vs) = axes(a.view)
    names = sorted({k[:-2] for k in rows[0] if k.endswith("_x")}, key=lambda n: list(rows[0]).index(n + "_x"))
    joints = [j for j in a.joints.split(",") if j in names]

    def pt(r, n):
        return hs * float(r[n + hx]), vs * float(r[n + vx])

    W, H = 1400, 1300
    img = Image.new("RGB", (W, H), (24, 24, 28))
    d = ImageDraw.Draw(img)
    d.text((10, 6), a.title or a.csv, fill=(255, 255, 255))

    # Panel 1: onion skin.
    xs = [pt(r, n)[0] for r in rows for n in names]
    ys = [pt(r, n)[1] for r in rows for n in names]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    box = (20, 30, W - 20, 690)
    s = min((box[2] - box[0]) / max(x1 - x0, 1e-3), (box[3] - box[1]) / max(y1 - y0, 1e-3)) * 0.95

    def map1(x, y):
        return box[0] + (x - x0) * s + 10, box[3] - (y - y0) * s - 10

    chains = [["root", "chest"] + [n for n in names if n.startswith("neck")] + ["head"],
              ["root", "tail_mid", "tail_tip"],
              ["root"] + [n for n in names if n.startswith("leg")],
              ["chest"] + [n for n in names if n.startswith("arm")]]
    for i, r in enumerate(rows):
        if i % a.ghost:
            continue
        shade = int(60 + 140 * i / max(len(rows) - 1, 1))
        for chain in chains:
            chain = [n for n in chain if n in names]
            pts = [map1(*pt(r, n)) for n in chain]
            if len(pts) > 1:
                d.line(pts, fill=(shade, shade, shade), width=1)
    for k, n in enumerate(joints):
        c = COLOURS[k % len(COLOURS)]
        for r in rows:
            x, y = map1(*pt(r, n))
            d.ellipse((x - 1.6, y - 1.6, x + 1.6, y + 1.6), fill=c)
        d.text((box[0] + 10 + 110 * k, box[1] + 4), n, fill=c)
    d.text((box[2] - 260, box[1] + 4), f"{a.view} view, scale {1 / s * 100:.2f} m per 100 px", fill=(200, 200, 200))

    # Panels 2 and 3: height and speed over time.
    t = [float(r["t"]) for r in rows]

    def panel(top, bottom, series, label, unit):
        lo = min(min(v) for v in series.values())
        hi = max(max(v) for v in series.values())
        if hi - lo < 1e-6:
            hi = lo + 1e-3
        d.rectangle((20, top, W - 20, bottom), outline=(70, 70, 80))
        d.text((26, top + 4), f"{label}  [{lo:.3f} .. {hi:.3f} {unit}]", fill=(220, 220, 220))

        def m(ti, v):
            return 20 + (ti - t[0]) / max(t[-1] - t[0], 1e-6) * (W - 40), bottom - 8 - (v - lo) / (hi - lo) * (bottom - top - 24)

        for r, ti in zip(rows, t):
            if r["action"]:
                x = m(ti, lo)[0]
                d.line((x, top, x, bottom), fill=(120, 120, 60))
                d.text((x + 2, top + 16), r["action"], fill=(220, 220, 120))
        for k, (n, v) in enumerate(series.items()):
            d.line([m(ti, vi) for ti, vi in zip(t, v)], fill=COLOURS[k % len(COLOURS)], width=2)

    heights = {n: [pt(r, n)[1] for r in rows] for n in joints}
    panel(700, 990, heights, f"{'height' if a.view != 'top' else 'z'} over time", "m")
    speeds = {}
    for n in joints:
        v = [0.0]
        for r0, r1 in zip(rows, rows[1:]):
            v.append(math.dist([float(r0[n + c]) for c in ("_x", "_y", "_z")],
                               [float(r1[n + c]) for c in ("_x", "_y", "_z")]))
        speeds[n] = v
    panel(1000, H - 10, speeds, "speed, metres per frame (spikes are pops)", "m/frame")
    img.save(a.out)
    print(a.out)


if __name__ == "__main__":
    main()
