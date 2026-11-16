"""Render the prop inspection cameras from the worktree root."""
import argparse
from pathlib import Path
import subprocess
from PIL import Image

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
SHOTS = {
    'spire_tower': ['--run', '1', '--cam', '236,76,-1238,197,68,-1277'],
    'watchtower': ['--run', '7', '--cam', '669,74,-1159,630,66,-1198'],
    'hoard_pile': ['--run', '7', '--hide-vegetation', '--cam', '759,62,-1162,738,48,-1183'],
    'hoard_trove': ['--run', '7', '--cam', '-228,60,22,-248,48,2'],
    'ward_stone': ['--training', '--attack', '--cam', '565,163,-2307,541,149,-2332'],
}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--label', default='v3')
    parser.add_argument('--only', choices=SHOTS)
    args = parser.parse_args()
    for name, extra in SHOTS.items():
        if args.only and args.only != name:
            continue
        stem = OUT / ('game-' + args.label + '-' + name)
        cmd = [str(ROOT / 'build/dragon'), '--headless', '--model', 'assets/embercrest.glb',
               '--frames', '1' if name == 'ward_stone' else '60', '--hide-ui', '--screenshot', str(stem.with_suffix('.bmp'))] + extra
        with stem.with_suffix('.log').open('w') as log:
            log.write(' '.join(cmd) + '\n')
            log.flush()
            subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        Image.open(stem.with_suffix('.bmp')).convert('RGB').save(stem.with_suffix('.png'))
        stem.with_suffix('.bmp').unlink()
        print(name, flush=True)

if __name__ == '__main__':
    main()
