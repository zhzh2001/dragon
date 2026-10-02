"""Full-size Metal regression captures for the generated dragons' flight profiles."""
from pathlib import Path
import argparse
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MODELS = ['embercrest', 'blightmaw', 'rimefang', 'ironroot', 'tidewrack', 'stormsail']
VIEWS = [('side', 90, 6), ('front', 180, 6), ('rear', 0, 6), ('top', 90, 85)]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--models', nargs='+', default=MODELS, choices=MODELS)
    parser.add_argument('--output', type=Path, default=ROOT/'artifacts/flight-pose-fix/final')
    args = parser.parse_args()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    for model in args.models:
        cases = []
        for scenario, frame, label in [(0,120,'glide'), (1,108,'upstroke'), (5,120,'dive')]:
            for view, angle, elevation in VIEWS:
                cases.append((f'{label}-{view}', frame, ['--studio',str(scenario),'--inspect',str(angle),'18',str(elevation)]))
        for scenario, label in [(0,'glide'), (1,'flap'), (5,'dive')]:
            cases.append((f'{label}-head',120,['--studio',str(scenario),'--inspect','90','3.5','0','--inspect-head']))
        cases += [
            ('ground-feet',180,['--studio','9','--inspect','60','11','3']),
            ('game-flight',180,['--input','0,0,0,1,0,0','--inspect','150','18','6']),
            ('game-landed',900,['--input','-0.25,0,0,0,0,0','--inspect','60','11','3']),
            ('switch-flight',180,['--models',f"assets/{'rimefang' if model == 'embercrest' else 'embercrest'}.glb,assets/{model}.glb",'--cycle-models','120','--studio','1','--inspect','0','18','6']),
        ]
        for label, frames, flags in cases:
            stem = args.output/f'{model}-{label}'
            bmp = stem.with_suffix('.bmp')
            cmd = [str(ROOT/'build/dragon'),'--headless','--hide-ui','--model',f'assets/{model}.glb','--frames',str(frames),'--screenshot',str(bmp)] + flags
            with stem.with_suffix('.log').open('w') as log:
                log.write(shlex.join(cmd)+'\n'); log.flush()
                subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=log,check=True)
            subprocess.run(['sips','-s','format','png',str(bmp),'--out',str(stem.with_suffix('.png'))],stdout=subprocess.DEVNULL,check=True)
            bmp.unlink()
            print(stem.name,flush=True)

if __name__ == '__main__':
    main()
