#!/usr/bin/env python3
"""Benchmark the graphics presets (docs/PORTING.md, R7).

    tools/bench/presets.py [--binary PATH] [--extra "ARGS"] [--presets a,b] [--scenarios a,b]

Runs each preset through fixed, headless scenarios -- deterministic, since a
headless frame is a fixed 1/60 s step -- and reads the run's own summary line
(`frames: ... fps; median ... ms, 1% low ... ms`) from DRAGON_LOG_FILE. The
frame rate is wall clock, so what it measures is the GPU and the driver;
the first second (uploads, pipeline builds) is outside the percentiles.

On x99 a D3D9 run needs the desktop session: run this script through
tools/x99/run_interactive.ps1 with --extra "--gpu-driver direct3d9 --tier sm2".
Prints a Markdown table, and writes the numbers as JSON with --json.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXE = os.path.join(ROOT, "build", "dragon.exe" if sys.platform == "win32" else "dragon")
PRESETS = ["ultra", "high", "medium", "low", "very-low"]
SCENARIOS = {
    # The valley's start: a straight glide, the view the golden scenes take.
    "valley": ["--frames", "600", "--model", "assets/embercrest.glb", "--hide-ui"],
    # A generated course flown by the autopilot: the camera sweeps the valley.
    "course": ["--frames", "1800", "--autopilot", "--course", "1", "--model", "assets/embercrest.glb", "--hide-ui"],
    # A hoard run flown by the demo pilot: rivals, prey, towers and fire.
    "run": ["--frames", "1800", "--autopilot", "--run", "7", "--hide-panels"],
    # Landed and walking: the camera low among grass, rocks and trunks, where
    # the X550 played slowest.
    "ground": ["--frames", "1500", "--input", "-0.25,0,0,0,0,0", "--walk", "0.6,0.15",
               "--model", "assets/embercrest.glb", "--hide-panels"],
}
LINE = re.compile(r"frames: (\d+) in ([\d.]+) s, ([\d.]+) fps; median ([\d.]+) ms, 1% low ([\d.]+) ms")


def run(binary, scenario, preset, extra):
    log = os.path.join(tempfile.gettempdir(), f"bench_{scenario}_{preset}.log")
    if os.path.exists(log):
        os.remove(log)
    cmd = [binary, "--headless", "--preset", preset] + SCENARIOS[scenario] + extra
    env = dict(os.environ, DRAGON_LOG_FILE=log)
    subprocess.run(cmd, cwd=os.path.dirname(binary) if binary != EXE else ROOT, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=1800)
    text = open(log, errors="replace").read() if os.path.exists(log) else ""
    m = LINE.search(text)
    if not m:
        return None
    return {"fps": float(m.group(3)), "median_ms": float(m.group(4)), "low_ms": float(m.group(5))}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--binary", default=EXE)
    parser.add_argument("--extra", default="", help="arguments for every run, e.g. --gpu-driver direct3d9 --tier sm2")
    parser.add_argument("--presets", default=",".join(PRESETS))
    parser.add_argument("--scenarios", default=",".join(SCENARIOS))
    parser.add_argument("--json")
    parser.add_argument("--frames-scale", type=float, default=1.0,
                        help="scale every scenario's frame count (0.5 halves a slow card's run)")
    args = parser.parse_args()
    binary = os.path.abspath(args.binary)
    extra = args.extra.split()
    presets = args.presets.split(",")
    scenarios = args.scenarios.split(",")
    for s in SCENARIOS.values():
        s[1] = str(max(120, int(int(s[1]) * args.frames_scale)))
    results = {}
    for scenario in scenarios:
        for preset in presets:
            r = run(binary, scenario, preset, extra)
            results[f"{scenario}/{preset}"] = r
            print(f"{scenario:7s} {preset:9s} " + (f"{r['fps']:6.1f} fps  median {r['median_ms']:6.1f} ms  "
                                                    f"1% low {r['low_ms']:6.1f} ms" if r else "FAILED"), flush=True)
    print()
    print("| preset | " + " | ".join(f"{s} (fps, median / 1% low ms)" for s in scenarios) + " |")
    print("|---|" + "---|" * len(scenarios))
    for preset in presets:
        cells = []
        for scenario in scenarios:
            r = results[f"{scenario}/{preset}"]
            cells.append(f"{r['fps']:.1f} ({r['median_ms']:.1f} / {r['low_ms']:.1f})" if r else "failed")
        print(f"| {preset} | " + " | ".join(cells) + " |")
    if args.json:
        with open(args.json, "w") as f:
            json.dump({"extra": args.extra, "results": results}, f, indent=1)


if __name__ == "__main__":
    main()
