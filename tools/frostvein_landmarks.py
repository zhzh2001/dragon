"""Plot the corrected source in the rigger's canonical frame for landmark fitting.

Run with a Python environment containing numpy and matplotlib. The source GLB's
node rotates X by 90 degrees; Blender's import plus the rigger's Z half-turn
maps its raw POSITION array to (-x, y, -z). Assert the measured bounds.
"""
import json
import os
import struct
from pathlib import Path
import numpy as np
os.environ.setdefault('MPL_IGNORE_SYSTEM_FONTS', '1')
os.environ.setdefault('MPLCONFIGDIR', '/tmp/frostvein-mpl')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
ROOT=Path(__file__).resolve().parents[1]
b=(ROOT/'assets/frostvein-cand-oneshot.glb').read_bytes()
n=struct.unpack_from('<I',b,12)[0]
j=json.loads(b[20:20+n]); binary=20+n+8
p=j['meshes'][0]['primitives'][0]; a=j['accessors'][p['attributes']['POSITION']]; v=j['bufferViews'][a['bufferView']]
xyz=np.ndarray((a['count'],3),dtype='<f4',buffer=b,offset=binary+v.get('byteOffset',0)+a.get('byteOffset',0),strides=(v.get('byteStride',12),4)).copy()
xyz*=np.array([-1,1,-1]); expected=json.loads((ROOT/'artifacts/frostvein/measurement/mesh_measurements.json').read_text())['bounds']
assert np.allclose(xyz.min(0),expected['lo'],atol=2e-6) and np.allclose(xyz.max(0),expected['hi'],atol=2e-6)
np.save('/tmp/frostvein-points.npy',xyz)
path=ROOT/'tools/skeletons/frostvein.json'; bones=json.loads(path.read_text())['bones'] if path.exists() else []
for name,axes,mask,limits in [('side',(1,2),np.abs(xyz[:,0])<.115,None),('front',(0,2),xyz[:,1]>.08,None),('top',(0,1),xyz[:,2]>.13,None),('wing',(0,1), (xyz[:,0]>.11)&(xyz[:,2]>.17),None),('head',(1,2),(np.abs(xyz[:,0])<.04)&(xyz[:,1]>.26)&(xyz[:,2]>.31),((.26,.39),(.31,.53)))]:
    pts=xyz[mask][::2]; fig,ax=plt.subplots(figsize=(14,9)); ax.scatter(pts[:,axes[0]],pts[:,axes[1]],s=.08,c=pts[:,2],cmap='bone',alpha=.6)
    for bn,head,tail,parent in bones:
        if name=='head' and bn not in ['head','jaw','jaw_tip','neck_03']:continue
        if name=='wing' and not bn.startswith('wing'):continue
        if bn.endswith('_l'):continue
        ax.plot([head[axes[0]],tail[axes[0]]],[head[axes[1]],tail[axes[1]]],color='red',linewidth=1)
        ax.text(head[axes[0]],head[axes[1]],bn,fontsize=6,color='crimson')
    ax.set(xlabel='xyz'[axes[0]],ylabel='xyz'[axes[1]],title='Frostvein canonical '+name); ax.set_aspect('equal');ax.grid(alpha=.3)
    if limits:ax.set_xlim(*limits[0]);ax.set_ylim(*limits[1])
    fig.savefig(ROOT/f'artifacts/frostvein/measurement/landmarks-{name}.png',dpi=150);plt.close(fig)
