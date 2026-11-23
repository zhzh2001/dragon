"""Bake the hero's additive body loops on its imported armature."""
import argparse
import math
from pathlib import Path
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import bpy
from mathutils import Quaternion, Vector
import make_brake_bank as base
import make_hero_attacks as attacks
from check_clip import read_glb
from make_hero_flight import curve
from hero_flight_glb import write

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'assets/embercrest-hero.glb'
OUTPUT = ROOT / 'assets/clips/embercrest-hero.body.glb'
CACHE = ROOT / 'build-clip-cache'
X = Vector((1, 0, 0))


def pitch(degrees):
    return Quaternion(X, math.radians(degrees))


def flap(t, climb=False):
    pose = {}
    gain = 1.5 if climb else 1
    def bend(degrees):
        return pitch(gain*degrees)

    # The loaded tail drops quickly and uncoils through a longer recovery.
    for j in range(8):
        delay = .15 + .042*j
        flick = curve(t-delay, [(0, -.82), (.14, -.98), (.23, -.74),
                                (.40, .90), (.48, 1), (.69, .43), (.88, -.39)])
        pose[f'tail_{j+1:02}'] = bend((4.4 + .93*j)*flick)
    kick_phase = t - (.03 if climb else 0)
    stroke = [(0, -.62), (.13, -.22), (.28, .80), (.35, 1),
              (.48, .76), (.64, -.12), (.77, -.85), (.88, -1)]
    load = curve(kick_phase, stroke)
    fore = curve(kick_phase-.15, stroke)
    for side in ('r', 'l'):
        pose['thigh_'+side] = bend(-10*load)
        pose['shin_'+side] = bend(6.8*load)
        pose['forearm_'+side] = bend(-5*fore)
        pose['wrist_'+side] = bend(-7*fore)
        for digit in range(1, 4):
            pose[f'toe_hind_{digit}_{side}'] = bend(-12*(1-load)/2)
            pose[f'toe_fore_{digit}_{side}'] = bend(-8*(1-fore)/2)
    arch = curve(t, [(0, 0), (.18, .24), (.42, .96), (.50, 1), (.69, .42), (.84, -.08)])
    if climb:
        arch = .25 + .75*arch
    pose['chest'] = bend(2.4*arch)
    reach = curve(t, [(0, -.18), (.18, .28), (.42, 1), (.55, .87),
                      (.72, .03), (.84, -.28)])
    if climb:
        reach = .25 + .75*reach
    neck = [-2.4-1.9*reach, -1.9-2.6*reach, -1.5-3.1*reach]
    for j, angle in enumerate(neck, 1):
        pose[f'neck_{j:02}'] = bend(angle)
    pose['head'] = bend(-sum(neck))
    return pose


LOOPS = {'flap_body': flap, 'flap_climb_body': lambda t: flap(t, climb=True)}


def bake(rig, nodes, parents, rotations, name):
    frames = 60
    samples = [LOOPS[name](f / frames) for f in range(frames)]
    # Center the wrap sample between its neighbors to match both sampled velocities.
    samples[0] = {n: q.slerp(samples[1][n], .5) for n, q in samples[-1].items()}
    samples.append(samples[0])
    rig.animation_data_create()
    rig.animation_data.action = bpy.data.actions.new(name)
    base.reset_pose(rig)
    tracks = {}
    for i, node in enumerate(nodes):
        joint = node['name']
        if joint not in samples[0]:
            continue
        rest = base.quat(node.get('rotation', (0, 0, 0, 1)))
        parent = rotations[parents[i]]
        pb = rig.pose.bones[joint]
        track = []
        for f, sample in enumerate(samples):
            delta = parent.inverted() @ sample[joint] @ parent
            pb.rotation_quaternion = base.blender_basis(rig, joint, parent, delta)
            pb.keyframe_insert('rotation_quaternion', frame=f, group=joint)
            track.append(base.xyzw((delta @ rest).normalized()))
        tracks[joint] = track
    return tracks


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--through', choices=LOOPS)
    parser.add_argument('--measure-axes', action='store_true')
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    base.SOURCE = attacks.SOURCE = SOURCE
    nodes, parents, _, rotations = base.source_rig(read_glb(SOURCE)[0])
    rig, _ = base.import_mesh()
    if args.measure_axes:
        CACHE.mkdir(exist_ok=True)
        attacks.ART = CACHE
        attacks.measure_axes(rig, nodes, parents, rotations)
    clips = {}
    for name in LOOPS:
        clips[name] = bake(rig, nodes, parents, rotations, name)
        if name == args.through:
            break
    write(OUTPUT, nodes, parents, clips)
    print('Exported', ', '.join(clips), flush=True)


if __name__ == '__main__':
    main()
