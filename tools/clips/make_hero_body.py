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
from hero_body_glb import write

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'assets/embercrest-hero.glb'
OUTPUT = ROOT / 'assets/clips/embercrest-hero.body.glb'
CACHE = ROOT / 'build-clip-cache'
X = Vector((1, 0, 0))
Y = Vector((0, 1, 0))
Z = Vector((0, 0, 1))


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
    # The hips spread after the kick, then gather forward on recovery.
    hip = -10*load + 7*max(0, -load)**2
    spread = 14*(.45 + curve(kick_phase-.24, stroke))
    spread_axis = (Z + .7*Y).normalized()
    for side, sign in [('r', 1), ('l', -1)]:
        lateral = Quaternion(spread_axis, math.radians(sign*gain*spread))
        pose['thigh_'+side] = lateral @ bend(hip)
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


def glide(t):
    pose = {}
    breath = curve(2*t, [(0, -.30), (.26, .68), (.43, 1), (.74, -.16), (.88, -.36)])
    pose['chest'] = pitch(1.5*breath)
    glance = [(0, 0), (.09, .06), (.23, 8), (.30, 7.3), (.46, .1),
              (.56, 0), (.73, -6), (.80, -5.6), (.95, 0)]
    dip = curve(t, [(0, 0), (.50, 0), (.74, -2.4), (.83, -1.2), (.96, 0)])
    for j, (joint, share) in enumerate(zip(('neck_01', 'neck_02', 'neck_03', 'head'),
                                           (.24, .21, .19, .36))):
        yaw = curve(t-.009*j, glance)*share
        pose[joint] = Quaternion(Y, math.radians(yaw)) @ pitch(dip*share)
    for j in range(8):
        sway = curve(t-.25*j/7, [(0, -.28), (.23, 1), (.40, .76), (.69, -1), (.84, -.87)])
        twitch = curve(t-.013*j, [(0, 0), (.41, 0), (.46, .35), (.50, -1), (.56, .13), (.62, 0)])
        yaw = (1.65+.12*j)*sway + max(0, j-4)*.75*twitch
        pose[f'tail_{j+1:02}'] = Quaternion(Y, math.radians(yaw))
    drift = [(0, -.25), (.16, -1), (.44, -.18), (.58, .92), (.74, .78), (.92, -.04)]
    for side, delay in [('l', 0), ('r', .32)]:
        pose['thigh_'+side] = pitch(3.8*curve(t-delay, drift))
    flex = curve(t, [(0, 0), (.32, 0), (.43, 1), (.49, .88), (.60, 0)])
    for digit in range(1, 4):
        pose[f'toe_hind_{digit}_r'] = pitch(-10*flex)
    shift = curve(t, [(0, 0), (.57, 0), (.70, 1), (.76, .82), (.91, 0)])
    settle = curve(t, [(0, 0), (.09, 0), (.22, 1), (.29, .72), (.44, 0)])
    pose['forearm_r'] = pitch(2.6*settle)
    pose['wrist_r'] = pitch(-1.8*settle)
    pose['forearm_l'] = pitch(-3.2*shift)
    pose['wrist_l'] = pitch(2.6*shift)
    return pose


def walk(t):
    # The right forefoot bears weight during the first half of the stride.
    side = curve(t, [(0, -.05), (.12, -.66), (.24, -1), (.38, -.72),
                     (.50, .05), (.63, .73), (.74, 1), (.88, .68)])
    land = curve(2*t, [(0, 1), (.17, .72), (.44, .06), (.57, -.18), (.79, .16)])
    pose = {}
    for j, share in enumerate((.55, .27, .18), 1):
        pose[f'neck_{j:02}'] = Quaternion(Y, math.radians(3.8*share*side)) @ pitch(-4.2*share*land)
    pose['head'] = pitch(4.2*land)
    for j in range(8):
        sway = curve(t-.15*j/7, [(0, .08), (.22, 1), (.40, .62),
                                  (.51, -.08), (.72, -1), (.88, -.70)])
        pose[f'tail_{j+1:02}'] = Quaternion(Y, math.radians((2.26+.11*j)*sway))
    bounce = curve(2*t-.055, [(0, -1), (.19, -.54), (.44, .77), (.62, .49), (.83, -.56)])
    for side, sign in [('r', 1), ('l', -1)]:
        pose['wing_wrist_'+side] = Quaternion(Z, math.radians(sign*2.5*bounce))
    return pose


def stand(t):
    breath = curve(3*t, [(0, -.24), (.18, .16), (.37, .91), (.45, 1),
                         (.63, .45), (.83, -.20), (.93, -.26)])
    look = [(0, 0), (.12, 0), (.19, 2.2), (.28, 12), (.41, 12),
            (.49, 6.8), (.57, 0), (.88, 0)]
    tilt = curve(t, [(0, 0), (.57, 0), (.65, 2.1), (.70, 5), (.75, 4.2),
                     (.82, .8), (.88, 0)])
    pose = {}
    for j, (joint, share) in enumerate(zip(('neck_01', 'neck_02', 'neck_03', 'head'),
                                           (.35, .24, .16, .25))):
        yaw = curve(t-.004*j, look)*share
        pose[joint] = Quaternion(Y, math.radians(yaw))
    pose['neck_01'] = pose['neck_01'] @ pitch(1.45*breath)
    pose['head'] = pose['head'] @ Quaternion(Z, math.radians(tilt))
    for side, sign in [('r', 1), ('l', -1)]:
        pose['wing_root_'+side] = Quaternion(Z, math.radians(sign*1.8*breath))
    for j in range(8):
        sway = curve(t-.12*j/7, [(0, -.42), (.24, 1), (.39, .86), (.71, -1), (.86, -.88)])
        curl = curve(t-.01*j, [(0, 0), (.34, 0), (.44, .78), (.50, 1), (.63, .27), (.72, 0)])
        yaw = Quaternion(Y, math.radians((1.25+.09*j)*sway))
        pose[f'tail_{j+1:02}'] = yaw @ pitch(-max(0, j-3)*2.1*curl)
    return pose


LOOPS = {
    'flap_body': (flap, 60, 1),
    'flap_climb_body': (lambda t: flap(t, climb=True), 60, 1),
    'glide': (glide, 30, 8),
    'walk': (walk, 60, 1),
    'stand': (stand, 30, 10),
}


def bake(rig, nodes, parents, rotations, name):
    pose_at, fps, duration = LOOPS[name]
    frames = fps*duration
    samples = [pose_at(f / frames) for f in range(frames)]
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
    return fps, tracks


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
