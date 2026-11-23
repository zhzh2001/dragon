"""Measure body-frame paths over matched cycles with the supplied probe."""
import argparse
import csv
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys

sys.dont_write_bytecode = True
from hero_body_render import ROOT, MAIN, ART, CACHE, CLIP
from PIL import Image, ImageDraw

# Include the first sample of the next cycle to measure the closing segment.
WINDOWS = {'beat': (1, 204, 51), 'climb': (1, 204, 51), 'glide': (0, 480, 480),
           'walk': (13, 200, 48), 'stand': (9, 600, 600)}
POINTS = ('tail_tip', 'tail_mid', 'head', 'leg2', 'arm2')


def probe(motion, version):
    scene, first, frames = WINDOWS[motion]
    model = Path('assets/embercrest-hero.glb')
    if motion == 'climb':
        # The probe reads flight tuning beside its model and has no --flight option.
        model = CACHE / 'climb' / model.name
        model.parent.mkdir(exist_ok=True)
        source = MAIN / 'assets' / model.name
        for link, target in [(model, source), (Path(str(model)+'.rig.cfg'), Path(str(source)+'.rig.cfg'))]:
            if not link.exists():
                link.symlink_to(target)
        flight = Path(str(source)+'.flight.cfg').read_text()
        Path(str(model)+'.flight.cfg').write_text(flight + '\nflap_effort_bias 1\n')
    env = dict(os.environ, DRAGON_BODY_CLIPS='none' if version == 'before' else str(CLIP),
               DRAGON_FLIGHT_CLIPS=str(MAIN / 'assets/clips/embercrest-hero.flight.glb'))
    with (CACHE/f'{motion}-{version}.csv').open('w') as output, (CACHE/f'{motion}-{version}-probe.log').open('w') as log:
        subprocess.run([MAIN/'build/trace_probe-r11b', model, str(scene), str((first-.25)/60),
                        str((first+frames+.25)/60)], env=env, cwd=MAIN, stdout=output,
                       stderr=log, stdin=subprocess.DEVNULL, check=True)


def distance(row, joint):
    return tuple(float(row[f'{joint}_{axis}']) for axis in 'xyz')


def metrics(motion, version):
    with (CACHE/f'{motion}-{version}.csv').open() as stream:
        rows = list(csv.DictReader(stream))
    assert len(rows) == WINDOWS[motion][2]+1, (motion, len(rows))
    length = statistics.mean(math.dist(distance(r,'head'), distance(r,'tail_tip')) for r in rows)
    paths = []
    for joint in POINTS:
        p = [distance(r,joint) for r in rows]
        paths.append(sum(math.dist(a,b) for a,b in zip(p,p[1:])))
    return length, paths


def chart():
    sheet = Image.new('RGB', (2800, 5*1336), (24,24,28))
    for i,motion in enumerate(WINDOWS):
        for j,version in enumerate(('before','after')):
            stem = f'{motion}-{version}'
            png = CACHE / (stem+'.png')
            view = 'side' if motion in ('beat','climb') else 'front'
            subprocess.run([sys.executable, '-B', ROOT/'tools/rig_probes/plot_trace.py',
                            CACHE/(stem+'.csv'), png, '--joints', 'tail_tip,head,leg2',
                            '--view', view, '--ghost', str(max(1,WINDOWS[motion][2]//12)),
                            '--title', f'{motion} / {version} / {view}'], check=True)
            with Image.open(png) as im:
                sheet.paste(im,(1400*j,1336*i))
            length, paths = metrics(motion, version)
            ImageDraw.Draw(sheet).text((1400*j+15,1336*i+1306),
                f'Length {length:.2f}   Paths: tail tip {paths[0]:.2f}, head {paths[2]:.2f}, hind ankle {paths[3]:.2f}   (probe units)',fill='white')
    sheet.save(ART/'traces.jpg',quality=93)


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('mode', choices=['before','after','report','charts'])
    ap.add_argument('motions',nargs='*')
    args = ap.parse_args()
    if args.mode == 'charts':
        chart()
    else:
        for motion in args.motions or WINDOWS:
            for version in (('before','after') if args.mode == 'report' else [args.mode]):
                if args.mode != 'report':
                    probe(motion,version)
                length, paths = metrics(motion,version)
                print(motion, version, 'length', round(length,3), 'paths', *(round(n,3) for n in paths),flush=True)
