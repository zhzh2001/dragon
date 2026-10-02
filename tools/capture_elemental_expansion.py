"""Reproduce Sunspear/Rimeplume native-renderer acceptance captures."""
from pathlib import Path
import argparse
import json
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--models', nargs='+', choices=['sunspear', 'rimeplume'],
                    default=['sunspear', 'rimeplume'])
    ap.add_argument('--cases', nargs='+')
    args = ap.parse_args()
    for model in args.models:
        out = ROOT / 'artifacts/elemental-expansion' / model
        out.mkdir(parents=True, exist_ok=True)
        cases = []
        for label, scenario, frame in [('glide', 0, 120), ('flap', 1, 108),
                                      ('dive', 5, 120), ('ground', 9, 180)]:
            for view, angle, elev in [('side', 270, 6), ('front', 180, 6),
                                     ('rear', 0, 6), ('top', 90, 85)]:
                cases.append((f'{label}-{view}', frame,
                              ['--studio', str(scenario), '--inspect', str(angle), ('24' if view == 'top' else '20'), str(elev)]))
            cases.append((f'{label}-head', frame,
                          ['--studio', str(scenario), '--inspect', '270', '3.5', '0', '--inspect-head']))
        cases += [
            ('bind', 1, ['--bind-pose', '--inspect', '140', '20', '12']),
            ('attack-head', 180, ['--studio', '8', '--inspect', '270', '3.5', '0', '--inspect-head']),
            ('ground-feet', 180, ['--studio', '9', '--inspect', '270', '15', '0']),
            ('game-flight', 180, ['--input', '0,0,0,1,0,0', '--inspect', '150', '20', '6']),
            ('game-landed', 900, ['--input', '-0.25,0,0,0,0,0', '--inspect', '270', '15', '0']),
            ('switch-flight', 180, ['--models', f"assets/{'rimefang' if model == 'embercrest' else 'embercrest'}.glb,assets/{model}.glb",
                                    '--cycle-models', '120', '--studio', '1', '--inspect', '150', '20', '6']),
        ]
        if args.cases and set(args.cases) - {c[0] for c in cases}:
            ap.error('unknown capture case')
        results = []
        for name, frames, flags in cases:
            if args.cases and name not in args.cases:
                continue
            bmp = out / f'{name}.bmp'
            cmd = [str(ROOT / 'build/dragon'), '--headless', '--hide-ui', '--model',
                   f'assets/{model}.glb', '--frames', str(frames), '--telemetry', '120',
                   '--screenshot', str(bmp)] + flags
            if not name.startswith(('game-', 'switch-')):
                cmd.append('--hide-vegetation')
            run = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
            log = run.stdout + run.stderr
            (out / f'{name}.log').write_text(shlex.join(cmd) + '\n' + log)
            run.check_returncode()
            if 'mapped rig:' not in log or 'falling back' in log.lower():
                raise RuntimeError(f'{model}/{name}: model failed to load')
            subprocess.run(['sips', '-s', 'format', 'png', str(bmp), '--out',
                            str(out / f'{name}.png')], check=True, stdout=subprocess.DEVNULL)
            bmp.unlink()
            results.append({'case': name, 'command': cmd, 'exit_code': run.returncode})
            print(f'{model}/{name}', flush=True)
        (out / ('captures-selected.json' if args.cases else 'captures.json')).write_text(
            json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
