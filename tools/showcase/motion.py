#!/usr/bin/env python3
"""How much of a frame moves: a number for "stiff" and "dead world".

The tool renders one view at frame N and at frame N + D with the camera in
the same place, shrinks both eight times with a box filter, and reports the
share of pixels that changed by more than a threshold. It prints the whole
frame's share and a 3 by 3 grid, top row first. A photograph scores 0.

The camera must not move between the two frames, or the number measures the
camera. Pass a fixed view: `--cam x,y,z,tx,ty,tz`, or `--studio N --inspect
...`, which holds the camera on the creature.

    tools/showcase/motion.py -- --cam 364,50,-2110,345,44,-2111 --hide-ui
    tools/showcase/motion.py --at 120 --dt 30 -- --studio 0 --inspect 90 14 6 --hide-ui
    tools/showcase/motion.py --floor -- --cam ...   # the same frame twice: the noise floor

Headless frames are 1/60 s, so the default D of 60 is one second. The idea
is Crimson Loong's `motion.py` (docs/SHOWCASE.md, "What other showcases do
that this build does not"); the code is ours.

Needs numpy and Pillow: run it with ~/.local/py314/bin/python3.
"""
import argparse
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def render(binary, frames, args, path, env_extra):
    cmd = [binary, '--headless', '--frames', str(frames), '--screenshot', path] + args
    env = dict(os.environ)
    env.update(env_extra)
    done = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True)
    if done.returncode != 0 or not os.path.exists(path):
        sys.exit('The render failed: %s\n%s' % (' '.join(cmd), done.stderr[-2000:]))


def shrunk(path, factor):
    image = np.asarray(Image.open(path).convert('RGB'), dtype=np.float32)
    h = image.shape[0] // factor * factor
    w = image.shape[1] // factor * factor
    image = image[:h, :w]
    return image.reshape(h // factor, factor, w // factor, factor, 3).mean(axis=(1, 3))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--at', type=int, default=120, help='the first frame (default 120)')
    parser.add_argument('--dt', type=int, default=60, help='frames between the two renders (default 60, one second)')
    parser.add_argument('--threshold', type=float, default=6.0, help='levels of 255 a shrunk pixel must change by')
    parser.add_argument('--shrink', type=int, default=8)
    parser.add_argument('--floor', action='store_true',
                        help='freeze the world at the first frame: what two renders of one moment differ by')
    parser.add_argument('--binary', default=os.path.join(ROOT, 'build', 'dragon'))
    parser.add_argument('--keep', help='write a.bmp, b.bmp and diff.png to this directory')
    parser.add_argument('args', nargs=argparse.REMAINDER, help='after --, the view: the flags passed to the game')
    options = parser.parse_args()
    view = [a for a in options.args if a != '--'] if options.args[:1] == ['--'] else options.args
    if not view:
        sys.exit('No view was given. Pass the game flags after --, for example: -- --cam x,y,z,tx,ty,tz --hide-ui')

    out = options.keep or tempfile.mkdtemp(prefix='motion-')
    os.makedirs(out, exist_ok=True)
    a_path = os.path.join(out, 'a.bmp')
    b_path = os.path.join(out, 'b.bmp')
    env = {'DRAGON_FREEZE_AT': str(options.at)} if options.floor else {}
    render(options.binary, options.at, view, a_path, env)
    render(options.binary, options.at + options.dt, view, b_path, env)

    a = shrunk(a_path, options.shrink)
    b = shrunk(b_path, options.shrink)
    change = np.abs(a - b).max(axis=2)
    moved = change > options.threshold

    print('frames %d and %d, shrunk %dx to %dx%d, threshold %g levels%s' % (
        options.at, options.at + options.dt, options.shrink, a.shape[1], a.shape[0], options.threshold,
        ' (world frozen: the noise floor)' if options.floor else ''))
    print('moved: %.1f%% of the frame, mean change %.2f levels' % (100.0 * moved.mean(), change.mean()))
    rows = np.array_split(np.arange(moved.shape[0]), 3)
    columns = np.array_split(np.arange(moved.shape[1]), 3)
    for row in rows:
        print('  ' + '  '.join('%5.1f%%' % (100.0 * moved[np.ix_(row, column)].mean()) for column in columns))
    if options.keep:
        heat = np.clip(change * 8.0, 0, 255).astype(np.uint8)
        Image.fromarray(heat).resize((heat.shape[1] * 4, heat.shape[0] * 4), Image.NEAREST).save(
            os.path.join(out, 'diff.png'))
        print('wrote %s' % out)


if __name__ == '__main__':
    main()
