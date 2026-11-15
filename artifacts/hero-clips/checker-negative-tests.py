"""Exercise each required failure class on temporary copies of the export."""
import copy
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/clips'))
from check_hero_attacks import check
from check_clip import read_glb

ART = Path(__file__).resolve().parent
SOURCE = ROOT/'assets/embercrest-hero.glb'
CLIP = ROOT/'assets/clips/embercrest-hero.attacks.glb'
doc, data = read_glb(CLIP)


def write(path, d, binary):
    j = json.dumps(d,separators=(',',':')).encode()
    j += b' '*(-len(j)%4)
    binary += b'\0'*(-len(binary)%4)
    path.write_bytes(struct.pack('<4sII',b'glTF',2,28+len(j)+len(binary))+
        struct.pack('<I4s',len(j),b'JSON')+j+struct.pack('<I4s',len(binary),b'BIN\0')+binary)


def channel(d, anim, joint):
    a = next(a for a in d['animations'] if a['name']==anim)
    c = next(c for c in a['channels'] if d['nodes'][c['target']['node']]['name']==joint)
    return c,a['samplers'][c['sampler']]


def key(d,b,anim,joint,frame,q):
    _,s = channel(d,anim,joint)
    a = d['accessors'][s['output']]
    v = d['bufferViews'][a['bufferView']]
    offset = v.get('byteOffset',0)+a.get('byteOffset',0)+16*frame
    struct.pack_into('<4f',b,offset,*q)


def mutation(kind,d,b):
    if kind=='joint_names':
        d['nodes'][0]['name']='wrong_root'
    elif kind=='rest':
        d['nodes'][0]['translation'][0] += .001
    elif kind=='translation_keys':
        d['animations'][0]['channels'][0]['target']['path']='translation'
    elif kind=='endpoints':
        key(d,b,'bite','head',0,(.08715574,0,0,.9961947))
    elif kind=='mirror':
        key(d,b,'claw_l','head',7,(0,0,0,1))
    elif kind=='amplitude':
        for f in range(28):
            key(d,b,'bite','jaw',f,(0,0,0,1))
    elif kind=='overlap':
        _,a=channel(d,'tail_r','tail_02')
        _,c=channel(d,'tail_r','tail_03')
        a['output'],c['output']=c['output'],a['output']
    elif kind=='pop':
        key(d,b,'bite','head',7,(.70710678,0,0,.70710678))


rows=[]
for kind in ('joint_names','rest','translation_keys','endpoints','mirror','amplitude','overlap','pop'):
    d,b=copy.deepcopy(doc),bytearray(data)
    mutation(kind,d,b)
    path=ART/'negative-test.glb'
    write(path,d,b)
    try:
        result=check(SOURCE,path)
        failures=[r['check'] for r in result['results'] if not r['pass']]
    except (KeyError,ValueError,IndexError) as exc:
        failures=[type(exc).__name__+': '+str(exc)]
    finally:
        path.unlink()
    assert failures,kind+' passed unexpectedly'
    rows.append('PASS rejected '+kind+': '+', '.join(failures))
text='\n'.join(rows)+'\n'
(ART/'checker-negative-tests.txt').write_text(text)
print(text,end='')
