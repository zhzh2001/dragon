"""Render Frostvein's acceptance poses with the native game renderer."""
from pathlib import Path
import argparse
import json
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts/frostvein/engine')
    parser.add_argument('--cases', nargs='+', help='Only run these named captures')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cases = []
    for label, scenario, frame in [('glide', 0, 120), ('flap', 1, 108),
                                    ('dive', 5, 120), ('ground', 9, 180),
                                    ('attack', 8, 180)]:
        for view, angle, elev in [('side', 90, 6), ('front', 180, 6),
                                  ('rear', 0, 6), ('top', 90, 85)]:
            cases.append((f'{label}-{view}', frame,
                          ['--studio', str(scenario), '--inspect', str(angle), '18', str(elev)]))
        cases.append((f'{label}-head', frame,
                      ['--studio', str(scenario), '--inspect', '90', '3.5', '0', '--inspect-head']))
    cases += [
        ('bind', 1, ['--bind-pose', '--inspect', '140', '18', '12']),
        ('ground-feet', 180, ['--studio', '9', '--inspect', '60', '11', '3']),
        ('game-flight', 180, ['--input', '0,0,0,1,0,0', '--inspect', '150', '18', '6']),
        ('game-landed', 900, ['--input', '-0.25,0,0,0,0,0', '--inspect', '60', '11', '3']),
        ('switch-flight', 180, ['--models', 'assets/embercrest.glb,assets/frostvein.glb',
                                '--cycle-models', '120', '--studio', '1', '--inspect', '150', '18', '6']),
        ('turn-soak', 7200, ['--studio', '4', '--inspect', '0', '20', '88']),
        ('frost-breath', 180, ['--combat', '--attack', '--inspect', '90', '24', '10']),
    ]
    if args.cases:
        unknown = set(args.cases) - {c[0] for c in cases}
        if unknown:
            parser.error(f'unknown cases: {sorted(unknown)}')
    results = []
    for name, frames, flags in cases:
        if args.cases and name not in args.cases:
            continue
        bmp = out / f'{name}.bmp'
        cmd = [str(ROOT / 'build/dragon'), '--headless', '--hide-ui', '--model',
               'assets/frostvein.glb', '--frames', str(frames), '--telemetry', '120',
               '--screenshot', str(bmp)] + flags
        run = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        log = run.stdout + run.stderr
        (out / f'{name}.log').write_text(shlex.join(cmd) + '\n' + log)
        run.check_returncode()
        if 'mapped rig:' not in log or 'falling back' in log.lower():
            raise RuntimeError(f'{name}: model did not load; see capture log')
        subprocess.run(['sips', '-s', 'format', 'png', str(bmp), '--out',
                        str(out / f'{name}.png')], check=True, stdout=subprocess.DEVNULL)
        bmp.unlink()
        results.append({'case': name, 'frames': frames, 'command': shlex.join(cmd),
                        'load': [line for line in log.splitlines()
                                 if 'mapped rig:' in line or 'frostvein' in line],
                        'exit_code': run.returncode})
        print(name, flush=True)
    (out / 'captures.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
