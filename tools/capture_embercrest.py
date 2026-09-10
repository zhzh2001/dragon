"""Render repeatable Embercrest acceptance views with the game's Metal renderer.

Run from any directory after building the game: python3 tools/capture_embercrest.py
Requires native macOS display access even though windows are hidden.
"""
from pathlib import Path
import subprocess
import argparse

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'artifacts/embercrest'
CASES = [
    ('bind-front', ['--bind-pose', '--inspect', '150', '18', '18'], 1),
    ('bind-side', ['--bind-pose', '--inspect', '90', '18', '10'], 1),
    ('bind-top', ['--bind-pose', '--inspect', '0', '20', '88'], 1),
    ('glide', ['--studio', '0', '--inspect', '35', '18', '25'], 90),
    ('flap', ['--studio', '1', '--inspect', '150', '16', '18'], 90),
    ('flap-upstroke', ['--studio', '1', '--inspect', '150', '16', '18'], 54),
    ('brake', ['--studio', '7', '--inspect', '90', '16', '12'], 180),
    ('pull-out', ['--studio', '6', '--inspect', '35', '16', '40'], 210),
    ('attack-rest', ['--studio', '8', '--inspect', '90', '4.3', '5', '--inspect-head'], 90),
    ('attack-mouth', ['--studio', '8', '--inspect', '90', '4.3', '5', '--inspect-head'], 180),
    ('attack-opening', ['--studio', '8', '--inspect', '90', '4.3', '5', '--inspect-head'], 132),
    ('attack-closed-again', ['--studio', '8', '--inspect', '90', '4.3', '5', '--inspect-head'], 330),
    ('tuck', ['--studio', '5', '--inspect', '35', '16', '40'], 120),
    ('attack-head', ['--studio', '8', '--inspect', '145', '4.3', '15', '--inspect-head'], 180),
    ('ground', ['--studio', '9', '--inspect', '140', '17', '12'], 240),
    ('turn-soak', ['--studio', '4', '--inspect', '0', '20', '88'], 7200),
]
CASES += [(f'wing-cycle-{i}', ['--studio', '1', '--inspect', '150', '16', '18'],
           108 + i * 6) for i in range(9)]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, default=ROOT/'build/dragon')
    parser.add_argument('--model', type=Path, default=ROOT/'assets/embercrest.glb')
    parser.add_argument('--output', type=Path, default=OUT)
    parser.add_argument('--cases', nargs='+', choices=[case[0] for case in CASES])
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    for name, flags, frames in CASES:
        if args.cases and name not in args.cases:
            continue
        bmp = output / (name + '.bmp')
        cmd = [str(args.executable.resolve()), '--headless', '--model', str(args.model.resolve()),
               '--hide-ui', '--frames', str(frames), '--screenshot', str(bmp)] + flags
        with (output/(name+'.log')).open('w') as log:
            subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(['sips', '-s', 'format', 'png', str(bmp), '--out', str(output/(name+'.png'))],
                       stdout=subprocess.DEVNULL, check=True)
        print(f'{name}: {frames} frames, captured', flush=True)

if __name__ == '__main__':
    main()
