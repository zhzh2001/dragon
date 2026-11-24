"""Capture the wingbeat at the studio and gameplay camera positions."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
from PIL import Image
from wingbeat.profiles import flight_value

ROOT = Path(__file__).resolve().parents[2]
MAIN = Path('~/src/game-claude')
ART = ROOT / 'artifacts/wingbeat'
CACHE = ROOT / 'build-clip-cache/render'
ORBITS = {'side': '270 14 6', 'rear': '0 16 14', 'front': '180 16 10', 'above': '90 18 85'}
REPORTS = {'tide2': {'0035': '271 13.9 6', '0036': '300 10.8 53'},
           'storm3': {'0037': '223 12.6 -1'}}


STUDIES = {'gold-dragon': 28, 'silver-dragon': 22}


def record_dir(model):
    if model in STUDIES:
        return ROOT / 'artifacts' / (model + '-integration') / 'wingbeat'
    return ART / model


def frames_dir(model, version):
    if version == 'plate' or model in STUDIES:
        return CACHE / model / version
    return ART / model / version


def views(model):
    period = flight_value(ROOT, model, 'flap_period')
    beat = tuple(200 + round(i * 60 * period / 8) for i in range(8))
    groups = {}
    orbits = ORBITS
    rear = '0 16 14'
    chase = (300, 313, 326, 339)
    if model in STUDIES:
        distance = STUDIES[model]
        orbits = {}
        for name, orbit in ORBITS.items():
            angle, _, elevation = orbit.split()
            orbits[name] = f'{angle} {distance} {elevation}'
        rear = orbits['rear']
        chase = (300, 316, 332, 348)
        groups['report'] = (f'--studio 1 --inspect 240 {distance} -13', beat)
    for report, orbit in REPORTS.get(model, {}).items():
        groups[f'report-{report}'] = ('--studio 1 --inspect ' + orbit, beat)
    for mode in ('cruise', 'climb'):
        effort = ' --flight flap_effort_bias=1' if mode == 'climb' else ''
        for name, orbit in orbits.items():
            if mode == 'climb' and name in ('front', 'above'):
                continue
            groups[f'{mode}-{name}'] = ('--studio 1 --inspect ' + orbit + effort, beat)
    groups.update({
        'turn': ('--studio 2 --inspect ' + rear, (230, 245, 260, 275)),
        'glide': ('--studio 0 --inspect ' + rear, (200,)),
        'chase': ('--autopilot --course 1', chase),
        'takeoff': ('--input 0.2,0,0,1,0,0', (40, 52, 64, 76, 88, 100)),
        'head-flap': ('--studio 1 --inspect 90 3.5 0 --inspect-head', (beat[4],)),
        'head-glide': ('--studio 0 --inspect 90 3.5 0 --inspect-head', (200,)),
        'switch': (f'--models assets/embercrest-hero.glb,assets/{model}.glb '
                   '--cycle-models 180 --studio 1 --inspect ' + rear, (beat[4],)),
    })
    return groups


def frame_name(group, frames, index):
    return group if len(frames) == 1 else f'{group}-{index}'


def capture(model, version, group, frame, name, options, candidate=None):
    directory = frames_dir(model, version)
    if candidate:
        directory = CACHE / model / candidate / version
    directory.mkdir(parents=True, exist_ok=True)
    stem = directory / name
    env = os.environ.copy()
    env.pop('DRAGON_FLIGHT_CLIPS', None)
    clip = ROOT / f'assets/clips/{model}.flight.glb'
    if candidate:
        clip = directory.parent / clip.name
    if version == 'plate':
        env['DRAGON_ABLATE'] = 'player'
    elif version == 'after':
        env['DRAGON_FLIGHT_CLIPS'] = str(clip)
    binary = './build/dragon-r12' if model in STUDIES else './build/dragon-r11'
    args = [binary, '--headless', '--hide-ui', '--size', '1920x1080',
            '--sun', '55,60', '--model', f'assets/{model}.glb', *options.split(),
            '--frames', str(frame), '--screenshot', str(stem.with_suffix('.bmp'))]
    log = CACHE / model / version / (name + '.log')
    if candidate:
        log = directory.parent / 'logs' / (name + '.log')
    log.parent.mkdir(exist_ok=True, parents=True)
    with log.open('w') as file:
        subprocess.run(args, cwd=MAIN, env=env, stdin=subprocess.DEVNULL,
                       stdout=file, stderr=subprocess.STDOUT, check=True)
    contents = log.read_text()
    loaded = f'flight clips: 2 in {clip} (flap yes, flap_climb yes)' in contents
    if (version == 'before' and f'{model}.flight.glb (flap yes' in contents
            or version == 'after' and not loaded):
        raise RuntimeError(f'Engine selected an unexpected flight clip. Read {log}.')
    if group == 'switch' and f'player model: [1] assets/{model}.glb' not in contents:
        raise RuntimeError(f'Engine did not switch to {model}. Read {log}.')
    with Image.open(stem.with_suffix('.bmp')) as image:
        if image.size != (1920, 1080):
            raise ValueError(f'Engine wrote a different image size. Read {log}.')
        image.convert('RGB').save(stem.with_suffix('.jpg'), quality=95, subsampling=0)
    stem.with_suffix('.bmp').unlink()
    print(model, version, name, frame, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('model', choices=(*REPORTS, *STUDIES))
    parser.add_argument('version', choices=('before', 'after', 'plate'))
    parser.add_argument('groups', nargs='*')
    parser.add_argument('--jobs', type=int, choices=(1, 2), default=2)
    parser.add_argument('--missing', action='store_true')
    parser.add_argument('--index', type=int, choices=range(8))
    parser.add_argument('--candidate', help='Read a cached clip and write its preview images beside it.')
    args = parser.parse_intermixed_args()
    if args.candidate and (args.version != 'after' or not args.candidate.isalnum()):
        parser.error('Use an alphanumeric candidate name with the after version.')
    groups = views(args.model)
    jobs = [(args.model, args.version, group, frame, frame_name(group, frames, i), options)
            for group in args.groups or groups
            for options, frames in [groups[group]] for i, frame in enumerate(frames) if args.index is None or args.index == i]
    if args.version == 'plate':
        jobs = [j for j in jobs if j[2].startswith(('cruise-', 'report')) and j[4].endswith('-0')]
    if args.missing:
        folder = frames_dir(args.model, args.version).parent
        if args.candidate:
            folder = CACHE / args.model / args.candidate
        jobs = [j for j in jobs if not (folder / args.version / (j[4] + '.jpg')).is_file()]
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        list(pool.map(lambda job: capture(*job, candidate=args.candidate), jobs))
