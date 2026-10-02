"""Repeatable full-size Metal melee inspection: body axes, timing, heads and gameplay."""
from pathlib import Path
import argparse
from concurrent.futures import ThreadPoolExecutor
import subprocess
import shlex

ROOT = Path(__file__).resolve().parents[1]
MODELS = ['embercrest', 'rimefang', 'frostvein', 'blightmaw', 'ironroot', 'stormsail', 'tidewrack']
VIEWS = [('side',90,6), ('front',180,6), ('rear',0,6), ('top',90,85)]

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, default=ROOT/'build/dragon')
    ap.add_argument('--output', type=Path, default=ROOT/'artifacts/melee-body/runtime/final')
    ap.add_argument('--models', nargs='+', default=MODELS)
    ap.add_argument('--baseline', action='store_true', help='only the strike peaks for comparison')
    args=ap.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    cases=[]
    for model in args.models:
        for label,scenario,peak,times in [('bite',10,31,[18,24,38,48]),('claw',11,32,[18,25,40,50]),('tail',12,40,[18,29,49,66])]:
            for view,angle,elev in VIEWS:
                cases.append((model,f'{label}-{view}',peak,['--studio',str(scenario),'--inspect',str(angle),'24',str(elev)]))
            if not args.baseline:
                angle,elev=(90,85) if label=='tail' else (90,6)
                for frame in times:
                    cases.append((model,f'{label}-phase-{frame:03}',frame,['--studio',str(scenario),'--inspect',str(angle),'24',str(elev)]))
                head_view = ['270','5','20'] if model=='alt/prowler' and label=='tail' else ['90', ('5.5' if model=='ironroot' or (label=='bite' and model in ['rimefang','tidewrack']) else '3.5'), '0']
                cases.append((model,f'{label}-head',peak,['--studio',str(scenario),'--inspect',*head_view,'--inspect-head']))
        if not args.baseline:
            cases.extend([(model,'glide-head',120,['--studio','0','--inspect','90',('5.5' if model=='ironroot' else '3.5'),'0','--inspect-head']),
                (model,'ground-feet',180,['--studio','9','--inspect','60','14','3']),
                (model,'game-flight-attack',48,['--attack','--training','--inspect','90','24','6']),
                (model,'game-landed',900,['--input','-0.25,0,0,0,0,0','--inspect','60','14','3']),
                (model,'switch-claw',116,['--models',f"assets/{'rimefang' if model == 'embercrest' else 'embercrest'}.glb,assets/{model}.glb",'--cycle-models','60','--studio','11','--inspect','0','24','6'])])
    def render(case):
        model,label,frame,flags=case
        stem=args.output/(model.replace('/','-')+'-'+label)
        bmp=stem.with_suffix('.bmp')
        cmd=[str(args.binary.resolve()),'--headless','--hide-ui','--model',f'assets/{model}.glb','--frames',str(frame),'--screenshot',str(bmp.resolve())]+flags
        with stem.with_suffix('.log').open('w') as log:
            log.write(shlex.join(cmd)+'\n'); log.flush()
            subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=log,check=True)
        subprocess.run(['sips','-s','format','png',str(bmp),'--out',str(stem.with_suffix('.png'))],stdout=subprocess.DEVNULL,check=True)
        bmp.unlink()
        print(stem.name,flush=True)
    with ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(render,cases))
if __name__=='__main__': main()
