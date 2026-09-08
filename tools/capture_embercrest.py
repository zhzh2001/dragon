"""Render repeatable Embercrest acceptance views with the game's Metal renderer.

Run from any directory after building the game: python3 tools/capture_embercrest.py
Requires native macOS display access even though windows are hidden.
"""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'artifacts/embercrest'
CASES = [
    ('bind-front', ['--bind-pose', '--inspect', '150', '18', '18'], 1),
    ('bind-side', ['--bind-pose', '--inspect', '90', '18', '10'], 1),
    ('bind-top', ['--bind-pose', '--inspect', '0', '20', '88'], 1),
    ('glide', ['--studio', '0', '--inspect', '35', '18', '25'], 90),
    ('flap', ['--studio', '1', '--inspect', '150', '16', '18'], 90),
    ('tuck', ['--studio', '5', '--inspect', '35', '16', '40'], 120),
    ('attack-head', ['--studio', '8', '--inspect', '145', '4.3', '15', '--inspect-head'], 180),
    ('ground', ['--studio', '9', '--inspect', '140', '17', '12'], 240),
    ('turn-soak', ['--studio', '4', '--inspect', '0', '20', '88'], 7200),
]

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, flags, frames in CASES:
        bmp = OUT / (name + '.bmp')
        cmd = [str(ROOT/'build/dragon'), '--headless', '--model', str(ROOT/'assets/embercrest.glb'),
               '--hide-ui', '--frames', str(frames), '--screenshot', str(bmp)] + flags
        with (OUT/(name+'.log')).open('w') as log:
            subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(['sips', '-s', 'format', 'png', str(bmp), '--out', str(OUT/(name+'.png'))],
                       stdout=subprocess.DEVNULL, check=True)
        print(f'{name}: {frames} frames, captured', flush=True)

if __name__ == '__main__':
    main()
