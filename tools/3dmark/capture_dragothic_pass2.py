#!/usr/bin/env python3
"""Native benchmark repair inspection; all UL pixels are local ignored outputs."""
import argparse, subprocess, concurrent.futures
from pathlib import Path
from PIL import Image,ImageDraw
p=argparse.ArgumentParser();p.add_argument('--before',action='store_true');p.add_argument('names',nargs='*');a=p.parse_args()
root=Path('artifacts/dragothic-rig/pass2');root.mkdir(parents=True,exist_ok=True)
model='assets/alt/3dmark2001/'+('pass2-before/' if a.before else '')+'dragothic.glb'
shots={
'side': ['--studio','0','--inspect','270','12','6'],
'front':['--studio','0','--inspect','180','18','6'],
'rear':['--studio','0','--inspect','0','18','6'],
'above':['--studio','0','--inspect','270','23','85'],
'head-front':['--studio','0','--inspect','180','2.7','0','--inspect-head'],
'head-below':['--studio','0','--inspect','180','2.7',' -30','--inspect-head'],
'head-top':['--studio','0','--inspect','160','4.5','65','--inspect-head'],
'head-glide':['--studio','0','--inspect','270','3.5','0','--inspect-head'],
'head-ground':['--studio','9','--inspect','270','3.5','0','--inspect-head','--frames','360'],
'ground-side':['--studio','9','--inspect','270','12','6','--frames','360'],
'ground-front':['--studio','9','--inspect','180','18','6','--frames','360'],
'ground-rear':['--studio','9','--inspect','0','18','6','--frames','360'],
'ground-above':['--studio','9','--inspect','270','23','85','--frames','360'],
'feet':['--studio','9','--inspect','300','11','3','--frames','360'],
'feet-front':['--studio','9','--inspect','180','8','3','--frames','360'],
'feet-rear':['--studio','9','--inspect','0','8','3','--frames','360'],
'landed':['--inspect','300','11','3','--input','-0.25,0,0,0,0,0','--frames','900','--telemetry','300'],
'switch':['--models',str(Path('assets/alt').resolve().parent/'embercrest.glb')+','+model,'--cycle-models','60','--frames','100','--inspect','180','18','6'],
'bind-side':['--bind-pose','--inspect','270','12','6'],
'bind-top':['--bind-pose','--inspect','270','23','85'],
'flap':['--studio','1','--inspect','180','18','6','--frames','64'],
}
def capture(name):
    stem=('before-' if a.before else '')+name
    bmp=root/(stem+'.bmp')
    cmd=['./build/dragon','--headless','--frames','60','--model',model,'--size','1024x768','--hide-ui','--hide-vegetation','--no-post',*shots[name],'--screenshot',str(bmp)]
    with (root/(stem+'.log')).open('w') as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
    with Image.open(bmp) as im:im.convert('RGB').save(root/(stem+'.png'))
    print(stem,flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:list(pool.map(capture,a.names or list(shots)))
if not a.before:
    ref=Path('assets/alt/3dmark2001/reference')
    for shot,frame in [('side','024'),('head-front','031'),('head-below','033'),('head-top','032'),('above','025')]:
        out=root/(shot+'.png')
        if not out.exists():continue
        l=Image.open(ref/f'dragothic-high-{frame}.png').convert('RGB');r=Image.open(out).convert('RGB')
        plate=Image.new('RGB',(2048,800),(25,25,25));plate.paste(l,(0,32));plate.paste(r,(1024,32));d=ImageDraw.Draw(plate)
        d.text((12,10),f'3DMark2001 SE High Detail {frame}',fill='white');d.text((1036,10),f'Dragon Engine {shot}; no post; warm sun',fill='white');plate.save(root/f'compare-{shot}.png')
