"""Capture flight comparisons with the retained engine and the supplied hero."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import subprocess

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT.parent / 'game-claude'
ART = ROOT / 'artifacts/hero-flight'
CACHE = ROOT / 'build-clip-cache'
CLIP = ROOT / 'assets/clips/embercrest-hero.flight.glb'
BEAT = (200, 207, 213, 219, 226, 232, 238, 245)
VIEWS = {}
for mode in ('cruise', 'climb'):
    for view, orbit in [('side', '269 12.7 6'), ('rear', '0 16 14'),
                        ('front', '180 16 10'), ('above', '90 18 85')]:
        if mode == 'climb' and view in ('front', 'above'):
            continue
        effort = ' --flight flap_effort_bias=1' if mode == 'climb' else ''
        VIEWS[f'{mode}-{view}'] = ('--studio 1 --inspect ' + orbit + effort, BEAT)
VIEWS.update({
    'turn': ('--studio 2 --inspect 0 16 14', (230, 245, 260, 275)),
    'glide': ('--studio 0 --inspect 0 16 14', (200,)),
    'chase': ('--autopilot --course 1', (300, 313, 326, 339)),
    'takeoff': ('--input 0.2,0,0,1,0,0', (40, 52, 64, 76, 88, 100)),
})
DETAILS = {
    'head-flap': ('--studio 1 --inspect 90 3.5 0 --inspect-head', 226),
    'head-glide': ('--studio 0 --inspect 90 3.5 0 --inspect-head', 200),
    'climb-front': ('--studio 1 --flight flap_effort_bias=1 --inspect 180 20 10', 232),
    'climb-above': ('--studio 1 --flight flap_effort_bias=1 --inspect 90 23 85', 238),
    'wide-side': ('--studio 1 --flight flap_effort_bias=1 --inspect 269 23 6', 207),
    'switch': ('--models assets/rimefang.glb,assets/embercrest-hero.glb --cycle-models 180 --studio 1 --inspect 0 16 14', 238),
}


def capture(task):
    version, name, view, frame = task
    directory = ART / version
    directory.mkdir(parents=True, exist_ok=True)
    stem = directory / name
    env = os.environ.copy()
    env.pop('DRAGON_FLIGHT_CLIPS', None)
    if version != 'before':
        env['DRAGON_FLIGHT_CLIPS'] = str(CLIP)
    args = ['./build/dragon-r10', '--headless', '--hide-ui', '--size', '1920x1080',
            '--sun', '55,60', '--model', 'assets/embercrest-hero.glb',
            *view.split(), '--frames', str(frame), '--screenshot', str(stem.with_suffix('.bmp'))]
    log = CACHE / f'{version}-{name}.log'
    with log.open('w') as stream:
        subprocess.run(args, cwd=MAIN, env=env, stdin=subprocess.DEVNULL,
                       stdout=stream, stderr=subprocess.STDOUT, check=True)
    loaded = '(flap yes, flap_climb yes)' in log.read_text()
    if loaded != (version != 'before'):
        raise RuntimeError(f'Flight clip selection failed. Read {log}.')
    with Image.open(stem.with_suffix('.bmp')) as image:
        assert image.size == (1920, 1080)
        image.convert('RGB').save(stem.with_suffix('.jpg'), quality=95, subsampling=0)
    stem.with_suffix('.bmp').unlink()
    print(version, name, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('version', choices=['before', 'after', 'details'])
    parser.add_argument('groups', nargs='*')
    args = parser.parse_args()
    tasks = [(args.version, group if group == 'glide' else f'{group}-{i}', view, frame)
             for group, (view, frames) in VIEWS.items() if not args.groups or group in args.groups
             for i, frame in enumerate(frames)]
    if args.version == 'details':
        tasks = [('details', name, view, frame) for name, (view, frame) in DETAILS.items()
                 if not args.groups or name in args.groups]
    CACHE.mkdir(exist_ok=True)
    with ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(capture, tasks))
