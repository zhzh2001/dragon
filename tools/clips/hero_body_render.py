"""Capture paired body poses with the retained engine."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
MAIN = Path('~/src/game-claude')
ART = ROOT / 'artifacts/hero-body'
CACHE = ROOT / 'build-clip-cache'
CLIP = ROOT / 'assets/clips/embercrest-hero.body.glb'
BEAT = (200, 207, 213, 219, 226, 232, 238, 245)
GLIDE = tuple(range(120, 541, 60))
WALK = tuple(range(200, 248, 6))
STAND = tuple(range(150, 676, 75))

VIEWS = {}
for motion, scenario, frames, cameras in [
    ('beat', 1, BEAT, [('side', '270 14 6'), ('rear', '0 16 14'), ('below', '300 13 -30')]),
    ('climb', 1, BEAT, [('side', '270 14 6')]),
    ('glide', 0, GLIDE, [('side', '270 14 6'), ('above', '90 16 85'), ('rear', '0 16 14')]),
    ('walk', 13, WALK, [('side', '270 14 6'), ('above', '90 14 85'), ('front', '180 14 6')]),
    ('stand', 9, STAND, [('side', '270 14 6'), ('front', '180 14 6'), ('above', '90 14 85')]),
]:
    for view, orbit in cameras:
        extra = ' --flight flap_effort_bias=1' if motion == 'climb' else ''
        VIEWS[f'{motion}-{view}'] = (f'--studio {scenario} --inspect {orbit}{extra}', frames)
VIEWS.update({
    'head-beat': ('--studio 1 --inspect 90 3.5 0 --inspect-head', BEAT[::2]),
    'head-stand': ('--studio 9 --inspect 90 3.5 0 --inspect-head', STAND[::2]),
    'feet': ('--studio 13 --inspect 60 11 3', WALK[::2]),
    'chase': ('--autopilot --course 1', (300, 313, 326, 339)),
    'landed': ('--input -0.25,0,0,0,0,0 --telemetry 60', (900, 960, 1020, 1080)),
})


def capture(task):
    version, group, index = task
    view, frames = VIEWS[group]
    stem = f'{group}-{index}'
    dest = ART / version
    dest.mkdir(exist_ok=True, parents=True)
    bmp = dest / (stem + '.bmp')
    jpeg = dest / (stem + '.jpg')
    env = dict(os.environ, DRAGON_BODY_CLIPS=str(CLIP) if CLIP.exists() else 'none')
    flags = view.split()
    if '--inspect' in flags:
        elevation = flags.index('--inspect') + 3
        if flags[elevation].startswith('-'):
            # The option parser accepts a negative elevation after leading whitespace.
            flags[elevation] = ' ' + flags[elevation]
    args = [MAIN / 'build/dragon-r11b', '--headless', '--hide-ui', '--size', '1920x1080',
            '--sun', '55,60', '--model', 'assets/embercrest-hero.glb', *flags]
    if version == 'before':
        args += ['--rig', 'body_clip=0']
    log = CACHE / f'{version}-{stem}.log'
    with log.open('w') as stream:
        subprocess.run([*args, '--frames', str(frames[index]), '--screenshot', bmp], cwd=MAIN,
                       env=env, stdin=subprocess.DEVNULL, stdout=stream, stderr=subprocess.STDOUT, check=True)
    if version == 'after' and f'in {CLIP}' not in log.read_text():
        raise RuntimeError(f'The engine did not load the body clip. Read {log}.')
    with Image.open(bmp) as im:
        assert im.size == (1920, 1080)
        im.convert('RGB').save(jpeg, quality=95, subsampling=0)
    bmp.unlink()
    print(version, stem, frames[index], flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('version', choices=['before', 'after'])
    parser.add_argument('groups', nargs='*', default=[])
    parser.add_argument('--indices', nargs='+', type=int)
    parser.add_argument('--workers', type=int, default=2, choices=[1, 2])
    parser.add_argument('--replace', action='store_true')
    args = parser.parse_args()
    CACHE.mkdir(exist_ok=True)
    tasks = [(args.version, group, i) for group in args.groups or VIEWS
             for i in (args.indices if args.indices is not None else range(len(VIEWS[group][1])))
             if args.replace or not (ART / args.version / f'{group}-{i}.jpg').exists()]
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        list(pool.map(capture, tasks))
