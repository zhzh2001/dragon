#!/usr/bin/env python3
"""Golden screenshots: the regression gate for every shader backend.

    tools/golden/golden.py capture [--set NAME] [--out DIR] [--only NAME,...]
    tools/golden/golden.py compare [--set NAME] [--out DIR] [--only NAME,...]
    tools/golden/golden.py compare --set metal --loose     # on another backend

`capture` renders each scene below headless with build/dragon and writes
tests/golden/<set>/<name>.png. `compare` renders the same scenes into --out
(default a scratch directory) and diffs each one against the set.

There is one set per backend, named for it (metal, vulkan, d3d12) and chosen
by platform when --set is omitted. Different GPUs do not filter alike: on
Vulkan on an RTX 5060 Ti against Metal on an M5, every frame of the valley
differed by a mean of 1.7/255 from the first frame on. The differences sat
on the anisotropically filtered terrain, the alpha-tested cards and the
dragon's maps, and the analytic sky was exact. So a backend's set is
captured on it once, after looking at it beside Metal's. `--loose` against
metal is the cross-backend sanity check: it passes filtering differences
and fails a frame that is wrong. It prints the mean and maximum channel error, the share
of pixels off by more than 8/255, and the worst 32x32 tile, writes an
amplified difference image beside each render, and exits 1 when a scene is
over its tolerance.

The set was captured from the hand-written MSL on 2026-10-01, before the
shaders were translated to HLSL (docs/PORTING.md, P1). The renders are
deterministic (the frame step is pinned in headless), so a frame that
changes is a frame that changed. Each scene is chosen so one shader family
dominates it. Every scene uses one model, --model assets/embercrest.glb,
which is gitignored: the gate runs on a machine that has the roster, not on
CI.
"""
import argparse
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BINARY = os.path.join(ROOT, 'build', 'dragon')
MODEL = ['--model', 'assets/embercrest.glb']

# name -> (frames, args). Kept short: each is a few seconds.
SCENES = {
    # Terrain, sky, fog, water, the shadow map, far foliage and the chase rig.
    'valley': (40, ['--hide-ui']),
    # The same frame without bloom and grade: the scene_hdr output alone.
    'valley-nopost': (40, ['--hide-ui', '--no-post']),
    # Skinned PBR creature, side on, and its shadow on the ground.
    'dragon-side': (60, ['--studio', '0', '--inspect', '90', '14', '6', '--hide-ui']),
    # Normal and ORM maps up close, on the head.
    'dragon-head': (60, ['--studio', '0', '--inspect', '90', '3.5', '0', '--inspect-head', '--hide-ui']),
    # Debug lines over the skinned mesh.
    'skeleton': (60, ['--studio', '0', '--inspect', '90', '14', '6', '--skeleton', '--hide-ui']),
    # Landed among grass and trees: alpha-tested cards, translucency, near shadows.
    'ground': (900, ['--input', '-0.25,0,0,0,0,0', '--hide-ui']),
    # Breath particles, bolts and the bloom they drive, with the HUD.
    'fire': (60, ['--training', '--attack', '--hide-panels']),
    # The run HUD: every ImGui-drawn readout, both faces.
    'run-hud': (120, ['--run', '7', '--hide-panels']),
}

# A translation through SPIR-V reorders float maths, so a pixel may move by
# a unit or two; a wrong shader moves thousands by much more.
TOLERANCE = {'mean': 0.6, 'over8': 0.002}
# Against another backend's set: filtering differs, so only a frame that is
# wrong fails -- a missing fog, a black pass, a flipped matrix all move the
# mean by tens. `ground` is the loosest scene, being mostly near cards.
LOOSE = {'mean': 6.0, 'over8': 0.25}
DEFAULT_SET = {'darwin': 'metal', 'linux': 'vulkan', 'win32': 'd3d12'}.get(sys.platform, 'metal')


def render(name, out_dir):
    frames, args = SCENES[name]
    bmp = os.path.join(out_dir, name + '.bmp')
    cmd = [BINARY, '--headless', '--frames', str(frames), '--screenshot', bmp] + MODEL + args
    result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if result.returncode != 0 or not os.path.exists(bmp):
        sys.exit(f'{name}: render failed\n{result.stderr[-2000:]}')
    png = os.path.join(out_dir, name + '.png')
    Image.open(bmp).convert('RGB').save(png, optimize=True)
    os.remove(bmp)
    return png


def diff(golden_png, test_png, diff_png):
    a = Image.open(golden_png).convert('RGB')
    b = Image.open(test_png).convert('RGB')
    if a.size != b.size:
        return {'mean': 255.0, 'max': 255, 'over8': 1.0, 'tile': (0, 0, 255.0)}
    d = ImageChops.difference(a, b)
    channels = d.tobytes()
    total = len(channels)
    mean = sum(channels) / total
    peak = max(channels)
    grey = d.convert('L')
    px = grey.tobytes()
    over8 = sum(1 for p in px if p > 8) / len(px)
    worst = (0, 0, 0.0)
    w, h = grey.size
    for ty in range(0, h, 32):
        for tx in range(0, w, 32):
            tile = grey.crop((tx, ty, min(tx + 32, w), min(ty + 32, h))).tobytes()
            m = sum(tile) / len(tile)
            if m > worst[2]:
                worst = (tx, ty, m)
    d.point(lambda v: min(255, v * 16)).save(diff_png)
    return {'mean': mean, 'max': peak, 'over8': over8, 'tile': worst}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('mode', choices=['capture', 'compare'])
    parser.add_argument('--set', default=DEFAULT_SET)
    parser.add_argument('--loose', action='store_true')
    parser.add_argument('--out')
    parser.add_argument('--only')
    parser.add_argument('--binary', help='render with this executable instead of build/dragon, '
                        'e.g. a package\'s Dragon.app/Contents/MacOS/dragon to check its baked shaders')
    args = parser.parse_args()
    global BINARY
    if args.binary:
        BINARY = os.path.abspath(args.binary)
    names = args.only.split(',') if args.only else list(SCENES)

    golden = os.path.join(ROOT, 'tests', 'golden', args.set)
    tolerance = LOOSE if args.loose else TOLERANCE
    if args.mode == 'capture':
        out = args.out or golden
        os.makedirs(out, exist_ok=True)
        for name in names:
            print(f'{name}: {render(name, out)}')
        return

    out = args.out or tempfile.mkdtemp(prefix='golden-')
    os.makedirs(out, exist_ok=True)
    failed = []
    for name in names:
        test = render(name, out)
        r = diff(os.path.join(golden, name + '.png'), test, os.path.join(out, name + '-diff.png'))
        ok = r['mean'] <= tolerance['mean'] and r['over8'] <= tolerance['over8']
        tx, ty, tm = r['tile']
        print(f"{'ok  ' if ok else 'FAIL'} {name:14s} mean {r['mean']:.3f}  max {r['max']:3d}  "
              f">8 {100 * r['over8']:.3f}%  worst tile ({tx},{ty}) {tm:.1f}")
        if not ok:
            failed.append(name)
    print(f'renders and diffs in {out}')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
