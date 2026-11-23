"""Build the hero wingbeat with Blender and the model's original rest transforms."""
import json
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

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'assets/embercrest-hero.glb'
OUTPUT = ROOT / 'assets/clips/embercrest-hero.flight.glb'
ART = ROOT / 'artifacts/hero-flight'
CACHE = ROOT / 'build-clip-cache'


def probe(rig, nodes, parents, rotations):
    attacks.ART = CACHE
    report = attacks.measure_axes(rig, nodes, parents, rotations)
    report['joints'] = {n: row for n, row in report['joints'].items() if n.startswith('wing_')}
    (ART / 'wing-axes.json').write_text(json.dumps(report, indent=2) + '\n')


# The neutral right shoulder includes the rig's levelling twist at cruise.
LEVEL = base.quat((-.336819, -.078848, -.053912, .936712))
X, Y, Z = (Vector(v) for v in [(1, 0, 0), (0, 1, 0), (0, 0, 1)])


def turn(axis, degrees):
    return Quaternion(axis, math.radians(degrees))


def curve(t, knots):
    """Interpolate cyclic pose knots with monotone cubic Hermite tangents."""
    t %= 1
    pairs = [(p - 1, v) for p, v in knots[-2:]] + knots + [(p + 1, v) for p, v in knots[:2]]
    def tangent(i):
        x, y = pairs[i]
        a, b = pairs[i - 1]
        c, d = pairs[i + 1]
        left, right = (y - b) / (x - a), (d - y) / (c - x)
        return 2 * left * right / (left + right) if left * right > 0 else 0
    for i in range(1, len(pairs) - 2):
        a, v = pairs[i]
        b, w = pairs[i + 1]
        if a <= t <= b:
            u = (t - a) / (b - a)
            return (v + (w-v)*u*u*(3-2*u)
                    + (b-a)*u*(1-u)*((1-u)*tangent(i) - u*tangent(i+1)))
    raise ValueError(t)


def right_pose(t, climb):
    top, low = (65, -60) if climb else (55, -40)
    def stroke(delay):
        v = curve(t-delay, [(0, 1), (.065, .95), (.22, .45), (.39, .07),
                            (.50, 0), (.58, .12), (.74, .63), (.90, .96)])
        return low + (top-low)*v
    fold = curve(t, [(0, 0), (.19, .018), (.40, .035), (.50, .09),
                     (.59, .63), (.67, 1), (.77, .79), (.85, .35), (.95, .018)])
    sweep = curve(t, [(0, 0), (.16, .21), (.36, .81), (.50, 1),
                      (.65, -.05), (.74, -.45), (.90, -.08)])
    sweep *= 25 if climb else 15
    elbow, wrist = (45, 65) if climb else (35, 48)
    pitch = curve(t, [(0, 0), (.16, -9), (.26, -13), (.45, -5),
                      (.53, 2), (.68, 12), (.82, 6), (.95, 1)])
    lag = 4/3 if climb else 1
    def station(roll, yaw, twist, trim):
        return turn(Z, roll) @ turn(Y, yaw) @ turn(X, twist) @ turn(Z, trim) @ LEVEL

    root = station(stroke(0), sweep, pitch*.40, -30)
    arm = station(stroke(.092*lag)+4, sweep+elbow*fold, pitch*.72, -35)
    palm_yaw = sweep + (elbow-wrist)*fold
    palm = station(stroke(.11*lag)-2, palm_yaw, pitch, -6)
    pose = {'wing_root': root, 'wing_arm': arm, 'wing_wrist': palm}
    for ray, count in ((1, 3), (2, 2), (3, 2)):
        for j, segment in enumerate('abc'[:count]):
            delay = (.09 + .018*j + .005*(ray-1))*lag
            curl = curve(t, [(0, 1), (.18, 3), (.30, 6), (.45, 2), (.57, -2),
                             (.70, -14), (.82, -10), (.95, -1)])
            roll = stroke(delay * (.78 if climb else 1)) + curl*(.5 + j*.34) - 3
            yaw = palm_yaw + (ray-2)*(3.5 if climb else 7.5)*fold
            pose[f'wing_finger_{ray}{segment}'] = station(roll, yaw, pitch, 0)
    # The spur supports the trailing membrane between the elbow and the flank.
    pose['wing_spur_a'] = root @ turn(X, -4*fold - pitch*.16)
    return pose


def height_keys(t, climb, wrist, ray=0):
    # Heights refer to the shoulder. Meter-valued keys keep the reversal
    # visible when a folding forearm points toward the camera.
    if wrist:
        if climb:
            return curve(t, [(0, 2.58), (4/60, 2.72), (.14, 2.16), (.25, .23),
                             (.40, -1.35), (.50, -1.48), (34/60, -1.54),
                             (.67, -.38), (.80, 1.02), (.95, 2.52)])
        return curve(t, [(0, 2.42), (.05, 2.53), (.14, 2.03), (.25, .30),
                         (.40, -1.16), (.50, -1.43), (.55, -1.52),
                         (.67, -.16), (.80, 1.23), (.95, 2.38)])
    top = (7/60 if climb else 5/60)
    bottom = .5 + top
    heights = ([(7.20, -6.45), (5.86, -5.42), (3.53, -3.46)] if climb else
               [(6.80, -6.02), (5.45, -5.10), (3.28, -3.35)])
    high, low = heights[ray-1]
    weight = curve(t, [(0, .946), (top, 1), (.19, .78), (.25, .55),
                       (.40, .19), (.50, .045), (bottom, 0),
                       (.70, .24), (.82, .59), (.95, .88)])
    return low + (high-low)*weight


def match_height(pose, bones, offsets, origin_y, target):
    # Tilt the ray toward its keyed height. Keep its internal curvature.
    v = sum((pose[n] @ offsets[n] for n in bones), Vector())
    sine = (target-origin_y) / v.length
    if abs(sine) >= 1:
        raise ValueError(f'Height {target:.3f} exceeds {bones[0]} reach.')
    horizontal = math.hypot(v.x, v.z)
    correction = math.asin(sine) - math.atan2(v.y, horizontal)
    bend = Quaternion(Vector((-v.z, 0, v.x)).normalized(), correction)
    for n in bones:
        pose[n] = bend @ pose[n]


def place_tips(pose, t, climb, offsets):
    elbow_y = (pose['wing_root'] @ offsets['wing_root']).y
    wrist_y = height_keys(t, climb, True)
    match_height(pose, ['wing_arm'], offsets, elbow_y, wrist_y)
    knuckle_y = wrist_y + (pose['wing_wrist'] @ offsets['wing_wrist']).y
    for ray, segments in [(1, 'abc'), (2, 'ab'), (3, 'ab')]:
        bones = [f'wing_finger_{ray}{s}' for s in segments]
        match_height(pose, bones, offsets, knuckle_y, height_keys(t, climb, False, ray))


def bake(rig, nodes, parents, rotations, offsets, climb):
    tracks = {}
    action = bpy.data.actions.new('flap_climb' if climb else 'flap')
    rig.animation_data_create()
    rig.animation_data.action = action
    base.reset_pose(rig)
    for frame in range(61):
        t = frame % 60 / 60
        pose = right_pose(t, climb)
        place_tips(pose, t, climb, offsets)
        for i, node in enumerate(nodes):
            name = node['name']
            if name[:-2] not in pose:
                continue
            parent = parents[i]
            parent_abs = pose.get(nodes[parent]['name'][:-2], LEVEL)
            delta = parent_abs.inverted() @ pose[name[:-2]]
            if name.endswith('_l'):
                delta.y, delta.z = -delta.y, -delta.z
            rest = base.quat(node.get('rotation', (0, 0, 0, 1)))
            parent_q = rotations[parent]
            dq = parent_q.inverted() @ delta @ parent_q
            values = tracks.setdefault(name, [])
            q = (dq @ rest).normalized()
            if values and sum(a*b for a, b in zip(base.xyzw(q), values[-1])) < 0:
                q.negate()
            values.append(base.xyzw(q))
            pb = rig.pose.bones[name]
            pb.rotation_quaternion = base.blender_basis(rig, name, parent_q, dq)
            pb.keyframe_insert('rotation_quaternion', frame=frame, group=name)
    return tracks


if __name__ == '__main__':
    from hero_flight_glb import write
    base.SOURCE = SOURCE
    doc, _ = read_glb(SOURCE)
    nodes, parents, worlds, rotations = base.source_rig(doc)
    rig, meshes = base.import_mesh()
    CACHE.mkdir(exist_ok=True)
    ART.mkdir(parents=True, exist_ok=True)
    if '--probe-only' in sys.argv or not (ART/'wing-axes.json').exists():
        probe(rig, nodes, parents, rotations)
    if '--probe-only' not in sys.argv:
        source = {n['name']: n for n in doc['nodes']}
        offsets = {n['name'][:-2]: Vector(source[n['name']]['extras']['tail']) - worlds[i].translation
                   for i, n in enumerate(nodes) if n['name'].startswith('wing_') and n['name'].endswith('_r')}
        clips = {name: bake(rig, nodes, parents, rotations, offsets, name == 'flap_climb')
                 for name in ('flap', 'flap_climb')}
        write(OUTPUT, nodes, parents, clips)
        print('Wrote', OUTPUT, flush=True)
