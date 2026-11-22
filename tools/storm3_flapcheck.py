"""Check the flank, hind legs, crown and rudder during a wing-only stroke."""
import sys
from pathlib import Path

import bpy

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from tide2_flapcheck import check


def joint(name):
    rig = next(o for o in bpy.context.scene.objects if o.type == 'ARMATURE')
    return rig.data.bones[name].head_local


def crest(x, y, z):
    skull = joint('head')
    return (y > skull.y - .095) & (y < skull.y + .055) & (z > skull.z + .008)


def hind_legs(x, y, z):
    hip, foot = joint('thigh_r'), joint('foot_r')
    return ((abs(x) > abs(hip.x)) & (z < hip.z - .065)
            & (y > min(hip.y, foot.y) - .095) & (y < max(hip.y, foot.y) + .095))


def flank(x, y, z):
    hip, shoulder = joint('thigh_r'), joint('wing_root_r')
    return ((abs(x) > .045) & (abs(x) < .080) & (y > hip.y - .055) & (y < shoulder.y + .01)
            & (z > hip.z) & (z < shoulder.z + .024))


if __name__ == '__main__':
    travel = check('storm3', {
        'crest and horns': crest,
        'hind legs': hind_legs,
        'flank at the web': flank,
        'tail and rudder': lambda x, y, z: y < joint('tail_04').y,
    })
    if travel != 0:
        raise SystemExit(1)
