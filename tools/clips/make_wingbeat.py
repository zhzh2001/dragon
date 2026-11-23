"""Build the selected wingbeat in Blender, using its source armature and measured wing frame."""
import argparse
import json
import math
from pathlib import Path
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))

import bpy
from mathutils import Quaternion, Vector
import make_brake_bank as base
from check_clip import read_glb
from wingbeat.glb import write

X, Y, Z = base.AXES.values()


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


def measure_axes(rig, nodes, parents, rotations, worlds, source_bones):
    """Measure the child tail after a five-degree turn about each model axis."""
    base.reset_pose(rig)
    rows = {}
    for i, node in enumerate(nodes):
        name = node['name']
        if not name.startswith('wing_'):
            continue
        children = node.get('children', [])
        child = nodes[children[0]]['name'] if children else name
        bone = rig.pose.bones[name]
        parent = rotations[parents[i]]
        child_index = children[0] if children else i
        grandchildren = nodes[child_index].get('children', [])
        if grandchildren:
            endpoint = worlds[grandchildren[0]].translation
        else:
            # The glTF stores leaf heads. The fitted skeleton supplies their tail lengths.
            head, tail = source_bones[child]
            endpoint = worlds[child_index].translation + rotations[child_index] @ (Y*(tail-head).length)
        tip_local = rig.data.bones[child].matrix_local.inverted() @ (
            rig.matrix_world.inverted() @ (base.C @ endpoint))
        def tip():
            return rig.matrix_world @ rig.pose.bones[child].matrix @ tip_local
        start = tip()
        axes = {}
        for label, axis in base.AXES.items():
            local = parent.inverted() @ axis
            bone.rotation_quaternion = base.blender_basis(rig, name, parent, turn(local, 5))
            bpy.context.view_layer.update()
            end = tip()
            displacement = base.C.inverted() @ (end - start)
            axes[label] = {'parent_axis': [round(v, 8) for v in local],
                           'displacement': [round(v, 8) for v in displacement]}
            bone.rotation_quaternion = Quaternion()
            bpy.context.view_layer.update()
        rows[name] = {'point': child + '.tail', 'axes': axes}
    return rows


def level_rotation(points, share):
    """Fit the same bind plane that the engine levels at the shoulder."""
    wing = [p for n, p in points.items() if n.startswith('wing_') and n.endswith('_r') and p.length_squared >= 1e-4]
    centre = sum(wing, Vector()) / len(wing)
    offsets = [p - centre for p in wing]
    trace = sum(v.length_squared for v in offsets)
    normal = Y.copy()
    for _ in range(32):
        normal = (trace * normal - sum((d * d.dot(normal) for d in offsets), Vector())).normalized()
    if normal.y < 0:
        normal.negate()
    root = points['wing_root_r']
    axis = max((p - root for p in wing), key=lambda v: v.length_squared).normalized()
    n = normal - axis * normal.dot(axis)
    up = Y - axis * Y.dot(axis)
    angle = math.atan2(n.cross(up).dot(axis), n.dot(up))
    return Quaternion(axis, angle * share)


def stroke(phase, down):
    return curve(phase, [(0, 1), (.13*down, .95), (.44*down, .45), (.78*down, .07),
                         (down, 0), (down+.16*(1-down), .12), (down+.48*(1-down), .63),
                         (down+.80*(1-down), .96)])


def right_pose(phase, cfg, down, points, level, glide_offset=0):
    """Place wrist and tip heights independently so the hand follows the arm."""
    t = phase
    w = stroke(t, down)
    wrist_time = t-cfg['wrist_lag']/60
    tip_time = t-cfg['tip_lag']/60
    fold = curve(t, [(0, 0), (.19, .018), (.40, .035), (.50, .09), (.59, .63),
                     (.67, 1), (.77, .79), (.85, .35), (.95, .018)])
    sweep = cfg['sweep'] * curve(t, [(0, 0), (.16, .21), (.36, .81), (down, 1),
                                    (.65, -.05), (.74, -.45), (.90, -.08)])
    pitch = cfg['pitch'] * curve(t, [(0, 0), (.16, -.65), (.25, -1), (.45, -.35),
                                     (.53, .15), (.68, .9), (.82, .45), (.95, .07)])
    high, low = cfg['top'] + glide_offset, cfg['bottom'] + glide_offset
    def frame(roll, yaw, twist):
        return turn(Z, roll) @ turn(Y, yaw) @ turn(X, twist) @ level
    def at(stem):
        return points['wing_'+stem+'_r']
    def end_height(v, delay, high, low):
        value = stroke(delay, down)
        return (frame(high, 0, 0) @ v).y * value + (frame(low, 0, 0) @ v).y * (1-value)
    root = frame(low + (high-low)*w, sweep, 0)
    elbow_height = (root @ (at('arm') - at('root'))).y
    arm = frame(0, sweep + cfg['elbow']*fold, cfg.get('forearm_pitch', 0))
    segment = arm @ (at('wrist') - at('arm'))
    wrist_height = end_height(at('wrist')-at('root'), wrist_time, cfg.get('wrist_top', high-4), cfg.get('wrist_bottom', low+2))
    relative = (wrist_height-elbow_height)/math.hypot(segment.x, segment.y)
    if abs(relative) > 1:
        raise ValueError(f'Unreachable wrist at phase {t:.4f}: {relative:.4f}')
    roll = math.degrees(math.asin(relative) - math.atan2(segment.y, segment.x))
    arm = turn(Z, roll) @ arm
    palm_yaw = sweep + (cfg['elbow']-cfg['wrist'])*fold
    hand_roll = low+(high-low)*stroke(wrist_time, down)
    # The wyvern shares the turn into its level palm across both hinges.
    if 'palm_follow' in cfg:
        tip_roll = cfg['tip_bottom'] + (cfg['tip_top']-cfg['tip_bottom'])*stroke(tip_time, down)
        hand_roll += cfg['palm_follow']*(tip_roll-hand_roll)
    palm = frame(hand_roll, palm_yaw, pitch + cfg.get('palm_pitch', 0))
    pose = {'root': root, 'arm': arm, 'wrist': palm}
    knuckle_y = wrist_height + (palm @ (at('finger_1a')-at('wrist'))).y
    rays = sorted(int(n.split('_')[2][:-1]) for n in points if n.startswith('wing_finger_') and n.endswith('a_r'))
    for ray in rays:
        stem = 'finger_'+str(ray)
        a = at(stem+'a')
        tip = at(stem+'tip')
        fan = cfg['fan']*(ray-(len(rays)+1)/2)/(len(rays)-1)*fold
        finger = frame(cfg['tip_bottom']+(cfg['tip_top']-cfg['tip_bottom'])*stroke(tip_time, down),
                       palm_yaw+fan, pitch)
        goal = end_height(tip-at('root'), tip_time, cfg['tip_top'], cfg['tip_bottom'])
        v = finger @ (tip-a)
        relative = (goal-knuckle_y)/v.length
        if abs(relative) > 1:
            raise ValueError(f'Unreachable rib {ray} at phase {t:.4f}: {relative:.4f}')
        correction = math.asin(relative)-math.atan2(v.y, math.hypot(v.x, v.z))
        finger = Quaternion(Vector((-v.z, 0, v.x)).normalized(), correction) @ finger
        for segment in ('a','b','tip'):
            pose[stem+segment] = finger
    return {'wing_'+k: v for k, v in pose.items()}


def storm_foundation(points, rotations, nodes, bones, level, params):
    """Set an open palm over the retained shoulder and elbow rest rotations."""
    import numpy as np

    chain = ['wing_root', 'wing_arm', 'wing_wrist', 'wing_finger_1a', 'wing_finger_1b']
    chain_points = [points[n+'_r'] for n in chain]
    end = 2*chain_points[-1] - chain_points[-2]
    arms = [math.hypot(p.x-end.x, p.y-end.y) for p in chain_points]
    weights = [params['outboard_decay']**i * (1-min(.8, i*params['phase_lag'])) for i in range(5)]
    norm = sum(w*l/arms[0] for w,l in zip(weights,arms))
    angles = [params['wing_rest_deg']*w/norm for w in weights]
    joints = {n['name']: i for i,n in enumerate(nodes)}
    local, neutral, virtual, rolls = {}, {}, {}, {}
    for j, (name, parent) in enumerate(zip(chain[:3], [None]+chain[:2])):
        local[name] = turn(Z, angles[j]) @ (level if j == 0 else Quaternion())
        rolls[name] = angles[j]
        neutral[name] = (neutral[parent] if parent else Quaternion()) @ local[name]
        virtual[name+'_r'] = (virtual[parent+'_r'] + neutral[parent] @ (points[name+'_r'] - points[parent+'_r'])
                              if parent else points[name+'_r'])
    palm_origin = points['wing_finger_1a_r']
    vector = []
    rays = [n[:-3] for n in points if n.startswith('wing_finger_') and n.endswith('a_r')]
    for ray in rays:
        leaf = ray+'b_r'
        head, tail = bones[leaf]
        tip = points[leaf] + rotations[joints[leaf]] @ (Y*(tail-head).length)
        points[ray+'tip_r'] = tip
        vector.extend((points[leaf]-palm_origin, tip-palm_origin))
    # Use a single plane for the four spokes. This preserves the membrane between ribs.
    normal = Vector(np.linalg.eigh(np.array(vector).T @ np.array(vector))[1][:,0])
    if normal.y < 0:
        normal.negate()
    hand = normal.rotation_difference(Y)
    for ray in rays:
        for i, (suffix, parent) in enumerate([('a','wing_wrist'),('b',ray+'a'),('tip',ray+'b')]):
            name = ray+suffix
            neutral[name] = hand
            virtual[name+'_r'] = virtual[parent+'_r'] + neutral[parent] @ (points[name+'_r']-points[parent+'_r'])
            if i < 2:
                local[name] = turn(Z,angles[3+i])
                rolls[name] = angles[3+i]
    params['foundation_rolls_deg'] = rolls
    params['hand_xyzw'] = base.xyzw(hand)
    return virtual, local, neutral


def bake(rig, nodes, parents, rotations, points, params, level, name, foundation=None):
    cfg = params['climb' if name == 'flap_climb' else 'cruise']
    action = bpy.data.actions.new(name)
    rig.animation_data_create()
    rig.animation_data.action = action
    base.reset_pose(rig)
    tracks = {}
    for frame in range(61):
        if foundation:
            virtual, local, neutral = foundation
            # Pose keys use whole degrees relative to the measured frame-200 glide.
            pose = right_pose(frame%60 / 60, cfg, params['downstroke'], virtual,
                              Quaternion(), round(params['glide_offset_deg']))
            pose = {n: q @ neutral[n] for n, q in pose.items()}
        else:
            pose = right_pose(frame%60 / 60, cfg, params['downstroke'], points, level)
        for i, node in enumerate(nodes):
            joint = node['name']
            stem = joint[:-2]
            if stem not in pose:
                continue
            parent = parents[i]
            parent_pose = pose.get(nodes[parent]['name'][:-2], Quaternion() if foundation else level)
            delta = parent_pose.inverted() @ pose[stem]
            if foundation:
                delta = local[stem].inverted() @ delta
            if joint.endswith('_l'):
                delta.y, delta.z = -delta.y, -delta.z
            rest = base.quat(node.get('rotation', (0,0,0,1)))
            parent_q = rotations[parent]
            dq = parent_q.inverted() @ delta @ parent_q
            q = (dq @ rest).normalized()
            keys = tracks.setdefault(joint, [])
            if keys and sum(a*b for a,b in zip(base.xyzw(q), keys[-1])) < 0:
                q.negate()
            keys.append(base.xyzw(q))
            bone = rig.pose.bones[joint]
            bone.rotation_quaternion = base.blender_basis(rig, joint, parent_q, dq)
            bone.keyframe_insert('rotation_quaternion', frame=frame, group=joint)
    return tracks


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('model', choices=('tide2','storm3'))
    parser.add_argument('--measure', action='store_true')
    parser.add_argument('--axes-only', action='store_true')
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    model = args.model
    path = HERE/'wingbeat'/(model+'.json')
    params = json.loads(path.read_text())
    base.SOURCE = ROOT/'assets'/(model+'.glb')
    source, _ = read_glb(base.SOURCE)
    nodes, parents, worlds, rotations = base.source_rig(source)
    points = {node['name']: worlds[i].translation for i,node in enumerate(nodes)}
    rig, _ = base.import_mesh()
    level = level_rotation(points, params['level_share'])
    skeleton = json.loads((ROOT/'tools/skeletons'/(model+'.json')).read_text())
    bones = {name: (Vector(head), Vector(tail)) for name, head, tail, _ in skeleton['bones']}
    foundation = None
    if model == 'storm3':
        foundation = storm_foundation(points, rotations, nodes, bones, level, params)
    measure = args.axes_only or args.measure or 'axes' not in params
    if measure:
        params['level_xyzw'] = base.xyzw(level)
        params['axes'] = measure_axes(rig, nodes, parents, rotations, worlds, bones)
    if measure or foundation:
        path.write_text(json.dumps(params, indent=2)+'\n')
    if args.axes_only:
        return
    clips = {name: bake(rig, nodes, parents, rotations, points, params, level, name, foundation)
             for name in ('flap','flap_climb')}
    output = ROOT/'assets/clips'/(model+'.flight.glb')
    write(output, nodes, parents, clips)
    print('Wrote', output)


if __name__ == '__main__':
    main()
