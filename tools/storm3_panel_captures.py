"""Render Storm3 from the report cameras and the ground cameras."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
MAIN = Path('~/src/game-claude')
OUTPUT = ROOT / 'artifacts/storm3/pass3'
CACHE = ROOT / 'build-rig-cache'
sys.path.insert(0, str(ROOT / 'tools/clips'))
from wingbeat_render import flap_period


def views():
    period = flap_period('storm3')
    beat = [200 + round(i * 60 * period / 8) for i in range(8)]
    result = []
    for name, orbit in [('report', '223 12.6 14.5'), ('report-low', '223 12.6 -1'), ('panel-above', '200 8 35'),
                        ('panel-below', '320 8 -35'), ('rear', '0 16 14'),
                        ('procedural-report', '223 12.6 14.5'), ('procedural-low', '223 12.6 -1')]:
        frames = beat[::2] if name.startswith('procedural') else beat
        for i, frame in enumerate(frames):
            result.append((f'{name}-{i}', 1, orbit, frame))
    return result + [
        ('glide-rear', 0, '0 16 14', 200), ('glide-above', 0, '90 18 85', 200),
        ('glide-below', 0, '90 14 -60', 200), ('turn', 2, '0 16 14', 260),
        ('dive', 5, '270 16 6', 300), ('stand-side', 9, '270 14 6', 200),
        ('stand-front', 9, '180 14 6', 200), ('stand-rear', 9, '0 14 6', 200),
        ('procedural-hem-above', 1, '200 8 35', 252), ('procedural-hem-below', 1, '320 8 -35', 252),
    ]


def capture(version, view, model=None):
    name, studio, orbit, frame = view
    folder = OUTPUT / version
    folder.mkdir(parents=True, exist_ok=True)
    logs = CACHE / (version + '-logs')
    logs.mkdir(parents=True, exist_ok=True)
    bmp = folder / (name + '.bmp')
    if model is None:
        model = ROOT / 'assets/storm3.glb'
    options = ['--rig', 'flap_clip=0'] if name.startswith('procedural') else []
    # The engine treats a leading minus as another option. SDL accepts a leading space.
    angle, distance, elevation = orbit.split()
    if elevation.startswith('-'):
        elevation = ' ' + elevation
    cmd = ['./build/dragon-r11c', '--headless', '--hide-ui', '--size', '1920x1080',
           '--sun', '55,60', '--model', str(model), '--studio', str(studio),
           '--inspect', angle, distance, elevation, *options, '--frames', str(frame), '--screenshot', str(bmp)]
    env = os.environ.copy()
    env.pop('DRAGON_FLIGHT_CLIPS', None)
    env.pop('DRAGON_ABLATE', None)
    log = logs / (name + '.log')
    with log.open('w') as stream:
        subprocess.run(cmd, cwd=MAIN, env=env, stdin=subprocess.DEVNULL, stdout=stream,
                       stderr=subprocess.STDOUT, check=True)
    clip = MAIN / 'assets/clips/storm3.flight.glb'
    if f'flight clips: 2 in {clip} (flap yes, flap_climb yes)' not in log.read_text():
        raise RuntimeError(f'The engine did not report both flight clips. Read {log}.')
    with Image.open(bmp) as image:
        assert image.size == (1920, 1080)
        image.convert('RGB').save(bmp.with_suffix('.jpg'), quality=96, subsampling=0)
    bmp.unlink()
    print(name, frame, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('version', choices=['before', 'after'])
    parser.add_argument('names', nargs='*', help='Capture only these view names.')
    args = parser.parse_args()
    chosen = [view for view in views() if not args.names or view[0] in args.names]
    if args.names and len(chosen) != len(set(args.names)):
        parser.error('Unknown view name. Check the names in views().')
    with ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(lambda view: capture(args.version, view), chosen))
