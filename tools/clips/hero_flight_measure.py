"""Measure the posed wing landmarks with the worktree rig and the retained libraries."""
import subprocess
import sys

sys.dont_write_bytecode = True
from check_hero_flight import CLIP, ROOT, SOURCE, minus
from check_clip import read_glb

ART = ROOT/'artifacts/hero-flight'
CACHE = ROOT/'build-clip-cache'


def main():
    CACHE.mkdir(exist_ok=True)
    objects = ROOT.parent/'game-claude/build/CMakeFiles/dragon.dir/src'
    units = ('anim/animation','anim/gltf_loader','anim/skeleton','anim/skinned_mesh',
             'game/flight','game/studio','gfx/texture')
    probe = CACHE/'flight-measure'
    subprocess.run(['clang++','-O2','-std=c++20','-Isrc','-I/opt/homebrew/include',
                    str(ART/'measure.cpp'),'src/anim/dragon_rig.cpp',
                    *(str(objects/(u+'.cpp.o')) for u in units),'-Wl,-dead_strip','-L/opt/homebrew/lib',
                    '-lSDL3','-o',str(probe)],cwd=ROOT,check=True)
    source, _ = read_glb(SOURCE)
    nodes = {n['name']:n for n in source['nodes']}
    offsets = []
    for leaf,parent in [('1c','1b'),('2b','2a'),('3b','3a')]:
        def tail(joint):
            return nodes['wing_finger_'+joint+'_r']['extras']['tail']
        offsets.extend(str(x) for x in minus(tail(leaf),tail(parent)))
    for label,clip,effort in [('procedural','-',0),('cruise',CLIP,0),('climb',CLIP,1)]:
        with (ART/(label+'-landmarks.csv')).open('w') as stream:
            subprocess.run([probe,clip,str(effort),*offsets],cwd=ROOT,stdout=stream,check=True)


if __name__ == '__main__':
    main()
