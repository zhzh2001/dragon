#!/usr/bin/env python3
"""Render a grounded-stance candidate: species, candidate name, override lines.
Writes <S>/cand/<species>-<name>/ with symlinked glb + cfgs and an appended rig.cfg,
renders four views (side, front, rear, top) and stitches them into <S>/out/<species>-<name>.png."""
import os, subprocess, sys
from PIL import Image
S=os.environ.get("RIG_PROBE_OUT", "/tmp/rig_probes"); REPO=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
VIEWS=[('side','90 14 6'),('front','0 14 6'),('rear','180 14 20'),('top','90 14 85')]
def render(species, name, overrides, views=VIEWS, frames=150, size=(960,540)):
    d=f'{S}/cand/{species}-{name}'; os.makedirs(d,exist_ok=True); os.makedirs(f'{S}/out',exist_ok=True)
    glb=f'{d}/model.glb'
    for src,dst in [(f'{REPO}/assets/{species}.glb',glb),(f'{REPO}/assets/{species}.glb.flight.cfg',glb+'.flight.cfg'),(f'{REPO}/assets/{species}.glb.breath.cfg',glb+'.breath.cfg')]:
        if os.path.lexists(dst): os.remove(dst)
        if os.path.exists(src): os.symlink(src,dst)
    base=open(f'{REPO}/assets/{species}.glb.rig.cfg').read()
    open(glb+'.rig.cfg','w').write(base+'\n# --- candidate overrides\n'+overrides.strip()+'\n')
    ims=[]
    for label,cam in views:
        bmp=f'{d}/{label}.bmp'
        subprocess.run([f'{REPO}/build/dragon','--headless','--frames',str(frames),'--hide-ui','--model',glb,'--studio','9','--inspect',*cam.split(),'--screenshot',bmp],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=False)
        subprocess.run(['sips','-s','format','png',bmp,'--out',bmp[:-4]+'.png'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        ims.append(Image.open(bmp[:-4]+'.png').resize(size))
    sheet=Image.new('RGB',(size[0]*2,size[1]*((len(ims)+1)//2)))
    for i,im in enumerate(ims): sheet.paste(im,((i%2)*size[0],(i//2)*size[1]))
    out=f'{S}/out/{species}-{name}.png'; sheet.save(out); return out
if __name__=='__main__':
    species,name=sys.argv[1],sys.argv[2]; overrides=sys.stdin.read()
    print(render(species,name,overrides))
