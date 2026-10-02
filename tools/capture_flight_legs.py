"""Full-size brake and dive inspection across every supported dragon rig."""
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
    ap.add_argument('--output', type=Path, default=ROOT/'artifacts/flight-leg-refinement/runtime/final')
    ap.add_argument('--models', nargs='+', default=MODELS)
    ap.add_argument('--baseline', action='store_true', help='only side views for comparison')
    ap.add_argument('--labels', nargs='+', help='render only these case labels')
    args=ap.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    cases=[]
    for model in args.models:
        for label,scenario,peak in [('brake',7,60),('dive',5,120)]:
            for view,angle,elev in (VIEWS[:1] if args.baseline else VIEWS):
                cases.append((model,f'{label}-{view}',peak,['--studio',str(scenario),'--inspect',str(angle),'18',str(elev)]))
            if not args.baseline:
                for frame in ([30,90,120,180,240] if label=='brake' else [270,300,330]):
                    cases.append((model,f'{label}-phase-{frame:03}',frame,['--studio',str(scenario),'--inspect','90','18','6']))
                cases.append((model,f'{label}-head',peak,['--studio',str(scenario),'--inspect','90','5.5','0','--inspect-head']))
        if not args.baseline:
            cases.extend([(model,'glide-head',120,['--studio','0','--inspect','90','5.5','0','--inspect-head']),
                (model,'ground-feet',180,['--studio','9','--inspect','60','14','3']),
                (model,'game-brake',60,['--input','0,0,0,0,0,1','--inspect','90','18','6']),
                (model,'game-dive',120,['--input','-0.25,0,0,0,1,0','--inspect','90','18','6']),
                (model,'game-landed',900,['--input','-0.25,0,0,0,0,0','--inspect','270','14','3']),
                (model,'switch-brake',90,['--models',f"assets/{'rimefang' if model == 'embercrest' else 'embercrest'}.glb,assets/{model}.glb",'--cycle-models','60','--studio','7','--inspect','0','18','6'])])
    if args.labels:
        cases = [case for case in cases if case[1] in args.labels]
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
