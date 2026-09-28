"""Plot measured canonical points and optional skeleton; no inherited landmarks.
Python with numpy/Pillow: tools/rimeplume_landmarks.py rimeplume|sunspear
"""
import sys,json
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
species=sys.argv[1]
assert species in ('rimeplume','sunspear')
root=Path(__file__).resolve().parents[1]
p=np.load(root/f'artifacts/{species}/measurement/canonical-points.npy')
sk=root/f'tools/skeletons/{species}.json'
bones=json.loads(sk.read_text())['bones'] if sk.exists() else []
for name,axes in [('side',(1,2)),('front',(0,2)),('top',(0,1))]:
 a,b=axes; lo=p.min(0);hi=p.max(0); scale=min(1450/(hi[a]-lo[a]),1050/(hi[b]-lo[b])); im=Image.new('RGB',(1600,1200),'white');d=ImageDraw.Draw(im)
 def xy(v):return (int(75+(v[a]-lo[a])*scale),int(1120-(v[b]-lo[b])*scale))
 subset=p if name!='side' else p[abs(p[:,0])<.13]
 for v in subset[::max(1,len(subset)//220000)]:
  x,y=xy(v); im.putpixel((max(0,min(1599,x)),max(0,min(1199,y))),(105,115,125))
 for axis in axes:
  for n in np.arange(np.floor(lo[axis]/.05)*.05,hi[axis]+.05,.05):
   v=lo.copy();v[axis]=n;w=hi.copy();w[axis]=n
   d.line([xy(v),xy(w)], fill=(200,210,220));d.text(xy(v),f'{n:.2f}',fill='blue')
 for bn,h,t,parent in bones:
  if bn.endswith('_l'):continue
  d.line([xy(h),xy(t)],fill='red',width=2);d.text(xy(h),bn,fill='red')
 d.text((30,20),f'{species}: canonical {name}; horizontal {"xyz"[a]}, vertical {"xyz"[b]}',fill='black')
 out=root/f'artifacts/{species}/measurement';out.mkdir(parents=True,exist_ok=True);im.save(out/f'landmarks-{name}.png')
