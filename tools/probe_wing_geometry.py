"""Measure a rigged dragon's wings straight out of its .glb, and simulate the fold.

    python3 tools/probe_wing_geometry.py                       # the four shipped assets
    python3 tools/probe_wing_geometry.py assets/stormsail.glb  # one file
    python3 tools/probe_wing_geometry.py --rig-cfg assets/embercrest.glb.rig.cfg assets/embercrest.glb

Diagnosis tool for the "folded wings cross" complaint. It reads the GLB
directly (numpy only -- no Blender, no pygltflib) and, per asset and per side:

  1. re-derives the wing chain with the SAME rules as `anim::map_dragon_joints`
     (name substrings, helper exclusions, side from the subtree's mean X,
     main-line descent, peer branches), so the chain it reports is the chain
     the engine drives;
  2. rebuilds the bind pose from the file's inverse bind matrices, as the
     loader does (docs/ANIMATION.md: nothing can disagree about inverse(IBM));
  3. reports bone directions, a least-squares wing plane through the wing
     joints and one through the membrane vertices, and the angle between each
     plane's normal and body up -- the number that says whether rotating the
     fold about `Vec3::unit_y()` is a defensible approximation;
  4. reports how far the skinned membrane reaches from every wing bone;
  5. replays `DragonRig::drive_wings` (progress, fold scales, outboard_decay,
     normalize, the Y-then-Z composition in `rotate_joint`) for the studio's
     Dive and Grounded states -- neither of which has a beat, so the replay
     leaves out everything the engine keys to flap phase or velocity (stroke
     plane, recovery flex and droop, feathering twist, body heave; see
     docs/ANIMATION.md "A wingbeat is not a wave") and stays exact for the
     static poses it probes -- about four fold axes -- A: body Y as shipped,
     B: the wing-joint plane normal, D: the membrane-vertex plane normal,
     C: the anatomical hinge (bone in x bone out) at the elbow and wrist --
     skins the mesh with each result, and counts the membrane vertices that
     end up inside the torso, across the midline or inside the other wing;
  6. runs the same metrics over a handful of candidate .rig.cfg profiles at
     full tuck, so a recommended profile is measured rather than guessed;
  7. checks whether each fitted joint sits in the wing's leading edge or out in
     the membrane, which is the "is the skeleton itself wrong" question.

Two things it flags on the way, because they change what the numbers mean:
joints below the mapped chain that the engine never drives (the mapper's
main-line descent stops before a leaf bone, so a two-bone finger loses its
outer bone), and helper joints that bind at the model origin (dragon.glb),
which are kept out of every fit.

Every position is printed in the ENGINE BODY FRAME (forward -Z, right +X, up
+Y) in metres at the engine's 19 m wingspan unless the label says model
space. A model that faces +Z in its own space is yawed 180 about Y first, the
same rule `DragonRig::init` applies (head forward of the tail's last bone).

Nothing here writes to the asset or to `src/`. The output is evidence for
someone deciding what axis and what profile the engine should use.
"""
import argparse
import json
import math
import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ASSETS = [
    'assets/stormsail.glb',
    'assets/embercrest.glb',
    'assets/dragon.glb',
    'assets/alt/prowler.glb',
]
WINGSPAN_M = 19.0  # app.cpp: asset_.scale = 19 / extent.x

# ------------------------------------------------------------------ RigTuning defaults
# Copied from src/anim/dragon_rig.h. A .rig.cfg overrides these per model, exactly
# as load_rig_tuning does (key value per line).
RIG_DEFAULTS = dict(
    flap_shoulder_deg=52.0, wing_phase_lag=0.26, wing_phase_delay=0.0,
    wing_downstroke_fraction=0.40, outboard_decay=0.68,
    tuck_sweep_deg=88.0, tuck_fold_deg=52.0, tuck_droop_deg=22.0, brake_flare_deg=30.0,
    wing_elbow_fold_scale=1.0, wing_wrist_fold_scale=1.0, wing_finger_fold_scale=1.0,
    wing_flap_fold_deg=0.0, wing_flap_limit_deg=0.0, wing_recovery_fold_deg=0.0,
    wing_recovery_extend_phase=0.0, upstroke_fold_deg=24.0,
    speed_sweep_deg=30.0, speed_fold_deg=14.0, sweep_speed_start=45.0, sweep_speed_full=95.0,
    load_forward_sweep_deg=4.0, wing_load_flex_deg=0.0,
)


def load_rig_cfg(tuning, path):
    """Apply a flat `key value` profile the way anim::load_rig_tuning does."""
    applied = 0
    for line in Path(path).read_text().splitlines():
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) < 2 or parts[0] not in tuning:
            continue
        try:
            tuning[parts[0]] = float(parts[1])
            applied += 1
        except ValueError:
            pass
    return applied


# ------------------------------------------------------------------ GLB parsing
COMPONENT_DTYPE = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16,
                   5125: np.uint32, 5126: np.float32}
TYPE_COUNT = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


class Glb:
    def __init__(self, path):
        data = Path(path).read_bytes()
        magic, version, length = struct.unpack_from('<III', data, 0)
        assert magic == 0x46546C67, f'{path}: not a GLB'
        offset = 12
        self.json = None
        self.bin = None
        while offset < length:
            chunk_length, chunk_type = struct.unpack_from('<II', data, offset)
            chunk = data[offset + 8: offset + 8 + chunk_length]
            if chunk_type == 0x4E4F534A:
                self.json = json.loads(chunk.decode('utf-8'))
            elif chunk_type == 0x004E4942 and self.bin is None:
                self.bin = chunk
            offset += 8 + chunk_length
        assert self.json is not None and self.bin is not None

    def accessor(self, index):
        acc = self.json['accessors'][index]
        view = self.json['bufferViews'][acc['bufferView']]
        dtype = np.dtype(COMPONENT_DTYPE[acc['componentType']])
        count = acc['count']
        width = TYPE_COUNT[acc['type']]
        start = view.get('byteOffset', 0) + acc.get('byteOffset', 0)
        stride = view.get('byteStride', 0)
        elem = dtype.itemsize * width
        if stride and stride != elem:
            rows = np.frombuffer(self.bin, dtype=np.uint8, count=stride * (count - 1) + elem,
                                 offset=start)
            idx = (np.arange(count)[:, None] * stride + np.arange(elem)[None, :])
            out = rows[idx].copy().view(dtype).reshape(count, width)
        else:
            out = np.frombuffer(self.bin, dtype=dtype, count=count * width,
                                offset=start).reshape(count, width)
        out = out.astype(np.float64) if acc['componentType'] == 5126 else out
        if acc.get('normalized') and acc['componentType'] != 5126:
            out = out.astype(np.float64) / np.iinfo(dtype).max
        return out


# ------------------------------------------------------------------ math (engine conventions)
def quat_from_axis_angle(axis, angle):
    n = axis / max(np.linalg.norm(axis), 1e-12)
    h = 0.5 * angle
    s = math.sin(h)
    return np.array([n[0] * s, n[1] * s, n[2] * s, math.cos(h)])


def quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return np.array([aw * bx + ax * bw + ay * bz - az * by,
                     aw * by - ax * bz + ay * bw + az * bx,
                     aw * bz + ax * by - ay * bx + az * bw,
                     aw * bw - ax * bx - ay * by - az * bz])


def quat_conj(q):
    return np.array([-q[0], -q[1], -q[2], q[3]])


def quat_rotate(q, v):
    u = q[:3]
    t = 2.0 * np.cross(u, v)
    return v + t * q[3] + np.cross(u, t)


def quat_to_mat3(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def quat_from_mat3(m):
    """Same branch structure as core::quat_from_matrix (columns normalised first)."""
    cols = [m[:, i] / max(np.linalg.norm(m[:, i]), 1e-12) for i in range(3)]
    x, y, z = cols  # x = first column etc; m[r][c] -> col c component r
    xx, xy, xz = x
    yx, yy, yz = y
    zx, zy, zz = z
    trace = xx + yy + zz
    if trace > 0:
        s = math.sqrt(trace + 1.0) * 2.0
        q = [(yz - zy) / s, (zx - xz) / s, (xy - yx) / s, 0.25 * s]
    elif xx > yy and xx > zz:
        s = math.sqrt(1.0 + xx - yy - zz) * 2.0
        q = [0.25 * s, (yx + xy) / s, (zx + xz) / s, (yz - zy) / s]
    elif yy > zz:
        s = math.sqrt(1.0 + yy - xx - zz) * 2.0
        q = [(yx + xy) / s, 0.25 * s, (zy + yz) / s, (zx - xz) / s]
    else:
        s = math.sqrt(1.0 + zz - xx - yy) * 2.0
        q = [(zx + xz) / s, (zy + yz) / s, 0.25 * s, (xy - yx) / s]
    q = np.array(q)
    return q / np.linalg.norm(q)


def decompose(m):
    """Mat4 -> (translation, rotation quat, scale), like core::decompose."""
    t = m[:3, 3].copy()
    scale = np.array([np.linalg.norm(m[:3, i]) for i in range(3)])
    r = m[:3, :3] / np.where(scale > 1e-12, scale, 1.0)
    return t, quat_from_mat3(r), scale


def compose(t, q, s):
    m = np.eye(4)
    m[:3, :3] = quat_to_mat3(q) * s[None, :]
    m[:3, 3] = t
    return m


def unit(v):
    n = np.linalg.norm(v)
    return v / n if n > 1e-12 else v


def angle_deg(a, b):
    c = np.clip(np.dot(unit(a), unit(b)), -1.0, 1.0)
    return math.degrees(math.acos(c))


def fit_plane(points, weights=None):
    """Least-squares plane. Returns (centroid, unit normal, rms residual)."""
    p = np.asarray(points, dtype=np.float64)
    if weights is None:
        weights = np.ones(len(p))
    w = weights / max(weights.sum(), 1e-12)
    c = (p * w[:, None]).sum(0)
    d = (p - c) * np.sqrt(w)[:, None]
    _, s, vt = np.linalg.svd(d, full_matrices=False)
    n = vt[-1]
    residual = (p - c) @ n
    rms = math.sqrt(float((w * residual * residual).sum()))
    return c, n, rms


def convex_hull_2d(pts):
    """Andrew's monotone chain; pts is (n,2). Returns hull vertices CCW."""
    pts = np.unique(np.round(pts, 6), axis=0)
    if len(pts) < 3:
        return pts
    pts = pts[np.lexsort((pts[:, 1], pts[:, 0]))]

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower = []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    upper = []
    for p in pts[::-1]:
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return np.array(lower[:-1] + upper[:-1])


def inside_convex_depth(hull, pts):
    """Signed distance of each point to a CCW convex polygon (positive = inside).

    The depth is the distance to the nearest edge line, which for a point
    inside a convex polygon is how far it would have to move to get out."""
    if len(hull) < 3:
        return np.full(len(pts), -np.inf)
    depth = np.full(len(pts), np.inf)
    for i in range(len(hull)):
        a = hull[i]
        b = hull[(i + 1) % len(hull)]
        e = b - a
        n = np.array([-e[1], e[0]])
        n /= max(np.linalg.norm(n), 1e-12)  # inward for CCW
        d = (pts - a) @ n
        depth = np.minimum(depth, d)
    return depth


# ------------------------------------------------------------------ skeleton
class Skeleton:
    """Joints in parent-before-child order, bind from inverse(IBM), like the loader."""

    def __init__(self, glb):
        j = glb.json
        skin = j['skins'][0]
        joints_nodes = skin['joints']
        ibm = glb.accessor(skin['inverseBindMatrices']).reshape(-1, 4, 4)
        # glTF matrices are column-major: element k is column k//4, row k%4.
        ibm = np.transpose(ibm, (0, 2, 1))
        bind_world = np.array([np.linalg.inv(m) for m in ibm])
        node_to_joint = {n: i for i, n in enumerate(joints_nodes)}
        parent_node = {}
        for ni, node in enumerate(j['nodes']):
            for c in node.get('children', []):
                parent_node[c] = ni
        gltf_parent = []
        for n in joints_nodes:
            p = parent_node.get(n)
            gltf_parent.append(node_to_joint.get(p, -1) if p is not None else -1)

        # Emit in dependency order (the engine's order), keep the glTF index too.
        order = []
        emitted = [False] * len(joints_nodes)
        while len(order) < len(joints_nodes):
            progressed = False
            for i in range(len(joints_nodes)):
                if emitted[i]:
                    continue
                p = gltf_parent[i]
                if p >= 0 and not emitted[p]:
                    continue
                order.append(i)
                emitted[i] = True
                progressed = True
            assert progressed, 'joint hierarchy has a cycle'
        self.gltf_to_skel = {g: s for s, g in enumerate(order)}
        self.count = len(order)
        self.names = []
        self.parent = []
        self.local_bind = []  # (t, q, s)
        self.world_bind = []
        self.inverse_bind = []
        for g in order:
            node = j['nodes'][joints_nodes[g]]
            name = node.get('name', f'joint_{g}')
            pg = gltf_parent[g]
            parent = self.gltf_to_skel[pg] if pg >= 0 else -1
            local = (np.linalg.inv(bind_world[pg]) @ bind_world[g]) if pg >= 0 else bind_world[g]
            self.names.append(name)
            self.parent.append(parent)
            self.local_bind.append(decompose(local))
            self.world_bind.append(bind_world[g])
            self.inverse_bind.append(ibm[g])
        self.world_bind = np.array(self.world_bind)
        self.inverse_bind = np.array(self.inverse_bind)
        self.children = [[] for _ in range(self.count)]
        for i, p in enumerate(self.parent):
            if p >= 0:
                self.children[p].append(i)
        # DragonRig::init: parent_bind_inverse_ = conj(accumulated bind rotation of parent).
        world_rot = [None] * self.count
        self.parent_bind_inverse = [None] * self.count
        for i in range(self.count):
            q = self.local_bind[i][1]
            p = self.parent[i]
            world_rot[i] = unit(q) if p < 0 else unit(quat_mul(world_rot[p], q))
            self.parent_bind_inverse[i] = (np.array([0, 0, 0, 1.0]) if p < 0
                                           else quat_conj(world_rot[p]))
        # Sanity: the hierarchy of locals must reproduce inverse(IBM).
        rebuilt = self.world_from_locals(self.local_bind)
        self.bind_reconstruction_error = float(np.abs(rebuilt - self.world_bind).max())

    def bind_pos(self, i):
        return self.world_bind[i][:3, 3]

    def degenerate(self, i):
        """A joint whose bind position is the origin: an IBM of identity, a helper."""
        return float(self.bind_pos(i) @ self.bind_pos(i)) < 1e-4

    def world_from_locals(self, locals_):
        world = np.zeros((self.count, 4, 4))
        for i in range(self.count):
            m = compose(*locals_[i])
            p = self.parent[i]
            world[i] = m if p < 0 else world[p] @ m
        return world

    def subtree(self, root):
        out = []
        stack = [root]
        while stack:
            c = stack.pop()
            out.append(c)
            stack.extend(self.children[c])
        return out

    def depth(self, i):
        d = 0
        while i >= 0:
            i = self.parent[i]
            d += 1
        return d


# ------------------------------------------------------------------ map_dragon_joints, replicated
HELPER_NAMES = ['_end', 'ik', 'pole', 'cont', 'target', 'chain', 'roll', 'fly', 'muscle', 'kneecap']


def collect(sk, include, exclude=()):
    found = []
    for i, name in enumerate(sk.names):
        low = name.lower()
        if any(s in low for s in include) and not any(s in low for s in exclude):
            found.append(i)
    return found


def order_chain(sk, members):
    if not members:
        return []
    mem = set(members)
    base = next((m for m in members if sk.parent[m] not in mem), None)
    if base is None:
        return []
    chain = []
    cur = base
    while cur is not None:
        chain.append(cur)
        cur = next((m for m in members if sk.parent[m] == cur), None)
    return chain


def trim_stub_base(sk, chain):
    while len(chain) >= 3:
        total = sum(np.linalg.norm(sk.bind_pos(chain[i]) - sk.bind_pos(chain[i - 1]))
                    for i in range(1, len(chain)))
        mean = total / (len(chain) - 1)
        first = np.linalg.norm(sk.bind_pos(chain[1]) - sk.bind_pos(chain[0]))
        if first < 0.25 * mean:
            chain.pop(0)
        else:
            break
    return chain


def subtree_mean_x(sk, root):
    s = sk.subtree(root)
    return float(np.mean([sk.bind_pos(i)[0] for i in s]))


def significant_children(sk, joint):
    return [c for c in sk.children[joint] if len(sk.subtree(c)) >= 2]


def branches_of(sk, joint):
    ch = significant_children(sk, joint)
    largest = max((len(sk.subtree(c)) for c in ch), default=0)
    return [c for c in ch if len(sk.subtree(c)) * 3 >= largest]


def descend_main(sk, start, stop_at=()):
    chain = []
    cur = start
    while cur >= 0:
        if cur in stop_at and chain:
            break
        chain.append(cur)
        nxt = branches_of(sk, cur)
        if len(nxt) != 1:
            break
        cur = nxt[0]
    return chain


def map_dragon_joints(sk):
    j = {'wing_root': [[], []], 'wing_fingers': [[], []], 'leg': [[], []],
         'front_leg': [[], []], 'foot_roots': []}
    j['root'] = next(i for i in range(sk.count) if sk.parent[i] < 0)
    chest = collect(sk, ['breast', 'chest', 'spine', 'torso'])
    j['chest'] = chest[-1] if chest else j['root']
    j['neck'] = trim_stub_base(sk, order_chain(sk, collect(sk, ['neck'], ['skin', 'ik_', '_end'])))
    head = collect(sk, ['head'], ['ik', '_end', 'target'])
    j['head'] = head[0] if head else -1
    j['tail'] = trim_stub_base(sk, order_chain(sk, collect(sk, ['tail'], ['cont', '_end'])))

    wing_candidates = collect(sk, ['w_c', 'wing', 'shoulder'], HELPER_NAMES)
    for side in range(2):
        best, best_depth = -1, 0
        for c in wing_candidates:
            if (subtree_mean_x(sk, c) > 0.0) != (side == 0):
                continue
            d = sk.depth(c)
            if best < 0 or d < best_depth:
                best, best_depth = c, d
        if best < 0:
            continue
        j['wing_root'][side] = descend_main(sk, best)
        branch_point = j['wing_root'][side][-1]
        for fb in branches_of(sk, branch_point):
            j['wing_fingers'][side].append(descend_main(sk, fb))
        if not j['wing_fingers'][side] and len(j['wing_root'][side]) > 1:
            j['wing_fingers'][side].append([j['wing_root'][side][-1]])
            j['wing_root'][side].pop()

    foot_all = collect(sk, ['hand', 'food', 'foot', 'fuss', 'paw'], HELPER_NAMES)
    feet = []
    for c in foot_all:
        in_wing = False
        for side in range(2):
            base = j['wing_root'][side][0] if j['wing_root'][side] else -1
            p = c
            while p >= 0 and base >= 0:
                if p == base:
                    in_wing = True
                p = sk.parent[p]
        if not in_wing:
            feet.append(c)
    leg_candidates = collect(sk, ['oberschenkel', 'thigh', 'upperleg', 'hip', 'femur'], HELPER_NAMES)
    for side in range(2):
        for c in leg_candidates:
            if (subtree_mean_x(sk, c) > 0.0) != (side == 0):
                continue
            chain = descend_main(sk, c, feet)
            if len(chain) > len(j['leg'][side]):
                j['leg'][side] = chain
    arm_candidates = collect(sk, ['upper_arm', 'oberarm', 'foreleg'], ['_end'])
    for side in range(2):
        for c in arm_candidates:
            if (subtree_mean_x(sk, c) > 0.0) != (side == 0):
                continue
            chain = descend_main(sk, c, feet)
            while chain and '_end_' in sk.names[chain[-1]]:
                chain.pop()
            j['front_leg'][side] = chain
            break
    for c in feet:
        nested = False
        p = sk.parent[c]
        while p >= 0:
            if p in feet:
                nested = True
            p = sk.parent[p]
        if not nested:
            j['foot_roots'].append(c)
    return j


# ------------------------------------------------------------------ the fold, replicated
def rotate_joint_two_axes(sk, locals_, joint, axis_a, angle_a, axis_b, angle_b):
    """DragonRig::rotate_joint(joint, axis_a, angle_a, axis_b, angle_b), from bind.

    Both body-space axes are carried into the parent's BIND frame by
    parent_bind_inverse_, and the local rotation becomes A * B * bind."""
    to_parent = sk.parent_bind_inverse[joint]
    a = quat_from_axis_angle(quat_rotate(to_parent, axis_a), angle_a)
    b = quat_from_axis_angle(quat_rotate(to_parent, axis_b), angle_b)
    t, q, s = sk.local_bind[joint]
    locals_[joint] = (t, unit(quat_mul(quat_mul(a, b), q)), s)


def smoothstep(a, b, x):
    t = min(max((x - a) / (b - a), 0.0), 1.0)
    return t * t * (3 - 2 * t)


def drive_wings(sk, joints, tuning, state, fold_axis_for, model_forward_z, side_filter=None):
    """Replays DragonRig::drive_wings for one FlightState and returns posed locals.

    `state`: dict with wing_tuck, airspeed, wing_angle (radians), wing_brake.
    `fold_axis_for(side, joint)`: body-space axis for the sweep+fold rotation
    (the engine passes Vec3::unit_y()). The flap stays about body Z.
    load_smoothed_ is taken as zero (no g excess in the states probed), the
    phase-delay articulation is off (no beat in a tuck or on the ground), and
    flutter is left out because it is a +-2 degree oscillation, not posture."""
    locals_ = list(sk.local_bind)
    tuck = state['wing_tuck']
    flare = state.get('wing_brake', 0.0)
    aft = model_forward_z
    speed_factor = smoothstep(tuning['sweep_speed_start'], tuning['sweep_speed_full'],
                              state['airspeed'])
    speed_share = speed_factor * (1.0 - tuck)
    sweep_deg = tuning['tuck_sweep_deg'] * tuck + tuning['speed_sweep_deg'] * speed_share
    fold_deg = tuning['tuck_fold_deg'] * tuck + tuning['speed_fold_deg'] * speed_share
    droop_deg = tuning['tuck_droop_deg'] * (
        tuck + speed_share * tuning['speed_sweep_deg'] / max(tuning['tuck_sweep_deg'], 1.0))
    flap_angle = state['wing_angle']
    if tuning['wing_flap_limit_deg'] > 0:
        lim = math.radians(tuning['wing_flap_limit_deg'])
        flap_angle = min(max(flap_angle, -lim), lim)
    non_flap_base = -math.radians(droop_deg)
    base = flap_angle + non_flap_base
    upstroke = smoothstep(math.radians(20.0), math.radians(50.0), base)
    upstroke_fold = math.radians(tuning['upstroke_fold_deg']) * upstroke
    flap_fold = math.radians(tuning['wing_flap_fold_deg']) * upstroke
    body_z = np.array([0.0, 0.0, 1.0])

    summary = {}
    for side in range(2):
        if side_filter is not None and side != side_filter:
            continue
        sign = 1.0 if side == 0 else -1.0
        root = joints['wing_root'][side]
        fingers = joints['wing_fingers'][side]
        root_len = len(root)
        longest_finger = max([len(f) for f in fingers] + [1])
        decay_total = sum(tuning['outboard_decay'] ** k for k in range(root_len + longest_finger))
        normalize = 1.0 / decay_total if decay_total > 1e-4 else 1.0
        shoulder_cut = 0.65 * upstroke

        def flap_share_at(k):
            return tuning['outboard_decay'] ** k * ((1.0 - shoulder_cut) if k < 2 else 1.0)

        flap_total = sum(flap_share_at(k) for k in range(root_len + longest_finger))
        flap_normalize = 1.0 / flap_total if flap_total > 1e-4 else 1.0

        per_joint = []
        depth = 0
        finger_index = -1

        def apply(joint, index_in_chain, chain_length):
            nonlocal depth
            lag = 1.0 - min(tuning['wing_phase_lag'] * depth, 0.8)
            local_base = base
            if tuning['wing_flap_limit_deg'] > 0:
                lim = math.radians(tuning['wing_flap_limit_deg'])
                cyclic = min(max(local_base - non_flap_base, -lim), lim)
                local_base = cyclic + non_flap_base
            flap = (local_base * sign) * flap_share_at(depth) * flap_normalize * lag
            progress = index_in_chain / (chain_length - 1) if chain_length > 1 else 1.0
            fold_progress = progress
            if finger_index >= 0:
                fold_progress *= max(tuning['wing_finger_fold_scale'], 0.0)
            elif index_in_chain > 0:
                is_wrist = root_len >= 3 and index_in_chain + 1 == root_len
                scale = tuning['wing_wrist_fold_scale'] if is_wrist else tuning['wing_elbow_fold_scale']
                fold_progress *= max(scale, 0.0)
            sweep = math.radians(sweep_deg) * sign * aft * (0.4 + 0.6 * progress) * normalize
            fold = (math.radians(fold_deg) + upstroke_fold * progress + flap_fold) * \
                fold_progress * sign * aft * normalize
            flare_angle = math.radians(tuning['brake_flare_deg']) * flare * sign * normalize
            axis = fold_axis_for(side, joint)
            rotate_joint_two_axes(sk, locals_, joint, axis, sweep + fold, body_z,
                                  flap - flare_angle)
            per_joint.append((sk.names[joint], math.degrees(sweep), math.degrees(fold),
                              math.degrees(flap)))
            depth += 1

        for i, jn in enumerate(root):
            apply(jn, i, len(root))
        root_depth = depth
        for finger in fingers:
            depth = root_depth
            finger_index += 1
            for i, jn in enumerate(finger):
                apply(jn, i, len(finger))
        summary[side] = dict(sweep_deg=sweep_deg, fold_deg=fold_deg, droop_deg=droop_deg,
                             normalize=normalize, per_joint=per_joint)
    return locals_, summary


def skin(sk, world, positions, joint_ids, weights):
    """Linear blend skinning with skinning[j] = world_posed[j] * inverseBind[j]."""
    skinning = np.einsum('jab,jbc->jac', world, sk.inverse_bind)
    out = np.zeros_like(positions)
    hom = np.concatenate([positions, np.ones((len(positions), 1))], axis=1)
    for k in range(joint_ids.shape[1]):
        m = skinning[joint_ids[:, k]]  # (n,4,4)
        out += weights[:, k:k + 1] * np.einsum('nab,nb->na', m, hom)[:, :3]
    return out


# ------------------------------------------------------------------ mesh
def load_mesh(glb, sk):
    """All skinned primitives, JOINTS_0 remapped to skeleton order, weights normalised."""
    j = glb.json
    skin_index = 0
    mesh_indices = [n['mesh'] for n in j['nodes'] if 'mesh' in n and n.get('skin', -1) == skin_index]
    if not mesh_indices:
        mesh_indices = [n['mesh'] for n in j['nodes'] if 'mesh' in n]
    pos, jid, wts = [], [], []
    skin_joints = j['skins'][skin_index]['joints']
    remap = np.array([sk.gltf_to_skel[g] for g in range(len(skin_joints))])
    for mi in dict.fromkeys(mesh_indices):
        for prim in j['meshes'][mi]['primitives']:
            a = prim['attributes']
            if 'JOINTS_0' not in a:
                continue
            p = glb.accessor(a['POSITION'])
            ji = glb.accessor(a['JOINTS_0']).astype(np.int64)
            w = glb.accessor(a['WEIGHTS_0']).astype(np.float64)
            ji = np.clip(ji, 0, len(remap) - 1)
            pos.append(p)
            jid.append(remap[ji])
            wts.append(w)
    positions = np.concatenate(pos)
    joint_ids = np.concatenate(jid)
    weights = np.concatenate(wts)
    total = weights.sum(1, keepdims=True)
    weights = np.where(total > 1e-8, weights / np.where(total > 1e-8, total, 1.0), weights)
    return positions, joint_ids, weights


def per_joint_weight(joint_ids, weights, count):
    """Dense (n, joints) weight matrix -- fine at these sizes."""
    dense = np.zeros((len(joint_ids), count))
    for k in range(joint_ids.shape[1]):
        np.add.at(dense, (np.arange(len(joint_ids)), joint_ids[:, k]), weights[:, k])
    return dense


# ------------------------------------------------------------------ reporting helpers
def fmt(v, scale=1.0):
    return '(' + ' '.join(f'{x * scale:7.3f}' for x in v) + ')'


def hr(title):
    print()
    print('=' * 100)
    print(title)
    print('=' * 100)


def sub(title):
    print()
    print('--- ' + title)


def probe(path, rig_cfg=None, verbose=True):
    hr(f'ASSET {path}')
    glb = Glb(path)
    sk = Skeleton(glb)
    print(f'{sk.count} joints; bind hierarchy reproduces inverse(IBM) to '
          f'{sk.bind_reconstruction_error:.2e} (model units)')
    joints = map_dragon_joints(sk)
    positions, joint_ids, weights = load_mesh(glb, sk)
    bounds_min, bounds_max = positions.min(0), positions.max(0)
    extent = bounds_max - bounds_min
    metres = WINGSPAN_M / extent[0] if extent[0] > 0.1 else 1.0
    print(f'{len(positions)} skinned vertices; model bounds min {fmt(bounds_min)} max {fmt(bounds_max)}')
    print(f'engine scale {metres:.4f} m per model unit (19 m over X extent {extent[0]:.3f})')

    # ---- facing (DragonRig::init rule) and the body frame
    aft_joint = joints['tail'][-1] if joints['tail'] else (joints['neck'][0] if joints['neck'] else -1)
    model_forward_z = -1.0
    if joints['head'] >= 0 and aft_joint >= 0:
        head_z = sk.bind_pos(joints['head'])[2]
        aft_z = sk.bind_pos(aft_joint)[2]
        model_forward_z = 1.0 if head_z > aft_z else -1.0
        print(f'facing: head z {head_z:.3f}, tail-end z {aft_z:.3f} -> model faces '
              f'{"+Z (engine yaws it 180)" if model_forward_z > 0 else "-Z (engine frame)"}')
    body_of = (lambda v: np.array([-v[0], v[1], -v[2]])) if model_forward_z > 0 else (lambda v: np.array(v))

    def body_pts(a):
        a = np.asarray(a)
        return a * np.array([-1, 1, -1]) if model_forward_z > 0 else a

    print('mapped rig (this script): neck %d, tail %d, wing root %d/%d, fingers %d/%d, legs %d/%d, '
          'front legs %d/%d, feet %d, jaw n/a' % (
              len(joints['neck']), len(joints['tail']),
              len(joints['wing_root'][0]), len(joints['wing_root'][1]),
              len(joints['wing_fingers'][0]), len(joints['wing_fingers'][1]),
              len(joints['leg'][0]), len(joints['leg'][1]),
              len(joints['front_leg'][0]), len(joints['front_leg'][1]), len(joints['foot_roots'])))
    print('   compare against the engine: ./build/dragon --headless --model %s --frames 2 2>&1 | grep "mapped rig"'
          % path)

    # ---- weights
    dense = per_joint_weight(joint_ids, weights, sk.count)
    dominant = dense.argmax(1)
    dominant_w = dense.max(1)

    wing_subtree = [set(), set()]
    for side in range(2):
        if joints['wing_root'][side]:
            wing_subtree[side] = set(sk.subtree(joints['wing_root'][side][0]))
    leg_like = set()
    for side in range(2):
        for chain in (joints['leg'][side], joints['front_leg'][side]):
            for jn in chain:
                leg_like |= set(sk.subtree(jn))
    for f in joints['foot_roots']:
        leg_like |= set(sk.subtree(f))
    wing_weight = [dense[:, sorted(wing_subtree[s])].sum(1) if wing_subtree[s] else np.zeros(len(dense))
                   for s in range(2)]
    torso_mask = np.ones(len(positions), bool)
    for s in range(2):
        torso_mask &= wing_weight[s] < 0.5
    torso_mask &= ~np.isin(dominant, sorted(leg_like))
    torso_pts_b = body_pts(positions[torso_mask]) * metres
    print(f'torso (non-wing, non-leg dominant) vertices: {torso_mask.sum()}; body-frame bounds '
          f'{fmt(torso_pts_b.min(0))} .. {fmt(torso_pts_b.max(0))} m')

    # Slab hulls of the torso along body forward (z), for the intersection test.
    slab = 0.25  # metres
    zs = torso_pts_b[:, 2]
    z0, z1 = zs.min(), zs.max()
    n_slabs = max(int(math.ceil((z1 - z0) / slab)), 1)
    hulls = []
    for si in range(n_slabs):
        lo = z0 + si * slab
        m = (zs >= lo) & (zs < lo + slab)
        if m.sum() >= 3:
            hulls.append(convex_hull_2d(torso_pts_b[m][:, :2]))
        else:
            hulls.append(None)

    def torso_penetration(pts_b):
        """Depth inside the torso slab hull at each point's z; -inf when outside."""
        depth = np.full(len(pts_b), -np.inf)
        idx = np.floor((pts_b[:, 2] - z0) / slab).astype(int)
        for si in range(n_slabs):
            m = idx == si
            if not m.any() or hulls[si] is None:
                continue
            depth[m] = inside_convex_depth(hulls[si], pts_b[m][:, :2])
        return depth

    results = {'path': path, 'sides': {}}
    side_names = {0: 'side0 (model +X)', 1: 'side1 (model -X)'}
    engine_side = {0: 'body LEFT (-X)' if model_forward_z > 0 else 'body RIGHT (+X)',
                   1: 'body RIGHT (+X)' if model_forward_z > 0 else 'body LEFT (-X)'}

    # ---- per-side geometry
    plane_normals = {}
    hinge_normals = {}
    wing_joint_lists = {}
    undriven_lists = {}
    tip_joints = {}
    for side in range(2):
        root = joints['wing_root'][side]
        fingers = joints['wing_fingers'][side]
        if not root:
            print(f'{side_names[side]}: no wing mapped')
            continue
        sub(f'{side_names[side]} = {engine_side[side]}: wing chain')
        driven = list(root) + [jn for f in fingers for jn in f]
        # Joints below the mapped chain that the engine never rotates. The
        # mapper's descend_main only follows children whose subtree has two or
        # more joints, so a finger whose outer bone is a leaf ends one bone
        # early: that leaf rides rigidly on its parent. They still carry skin
        # weights and they are where the wing actually ends, so the geometry
        # below includes them, flagged.
        undriven = []
        for i in driven:
            for d in sk.subtree(i):
                if d not in driven and d not in undriven:
                    undriven.append(d)
        all_wing = driven + undriven
        wing_joint_lists[side] = driven
        undriven_lists[side] = undriven
        # Tip joint per finger: the descendant of the chain's last joint that
        # lies furthest from the finger's base, skipping helpers bound at the
        # origin (dragon.glb's *_end leaves and one finger end are such).
        tip_joint = []
        for f in fingers:
            base = sk.bind_pos(f[0])
            best, best_d = f[-1], -1.0
            for d in sk.subtree(f[0]):
                if sk.degenerate(d):
                    continue
                dd = float(np.linalg.norm(sk.bind_pos(d) - base))
                if dd > best_d:
                    best, best_d = d, dd
            tip_joint.append(best)
        tip_joints[side] = tip_joint
        degenerate_wing = [i for i in all_wing if sk.degenerate(i)]
        if degenerate_wing:
            print(f'  DEGENERATE: {len(degenerate_wing)} wing joint(s) bind at the model origin and are left out of '
                  f'the plane fit, hinges and tips: {", ".join(sk.names[i] for i in degenerate_wing)}')
        print('  shared arm : ' + ' -> '.join(sk.names[i] for i in root))
        for fi, f in enumerate(fingers):
            extra = ''
            if tip_joint[fi] != f[-1]:
                extra = f'   (then undriven: {sk.names[tip_joint[fi]]})'
            print(f'  finger {fi}   : ' + ' -> '.join(sk.names[i] for i in f) + extra)
        if undriven:
            und_w = int(((dominant_w > 0.5) & np.isin(dominant, undriven)).sum())
            print(f'  MAPPER: {len(undriven)} wing joint(s) below the mapped chains are never driven '
                  f'({", ".join(sk.names[i] for i in undriven)}); {und_w} vertices are skinned mostly to them')
        print('  joint                        parent                   model-space pos         body-space pos (m)')
        for i in all_wing:
            p = sk.parent[i]
            flag = '' if i in driven else '   [undriven leaf]'
            if sk.degenerate(i):
                flag += '   [DEGENERATE: binds at the origin]'
            print(f'  {sk.names[i]:28s} {sk.names[p] if p >= 0 else "-":24s} {fmt(sk.bind_pos(i))}  '
                  f'{fmt(body_of(sk.bind_pos(i)), metres)}{flag}')

        sub(f'{side_names[side]}: segment directions (unit), model space and body space')
        print('  segment                                          model dir              body dir              length m')
        seg_dirs = {}
        for i in all_wing:
            p = sk.parent[i]
            if p < 0:
                continue
            d = sk.bind_pos(i) - sk.bind_pos(p)
            length = np.linalg.norm(d) * metres
            seg_dirs[i] = unit(d)
            print(f'  {sk.names[p]:22s} -> {sk.names[i]:22s} {fmt(unit(d))} {fmt(body_of(unit(d)))} {length:7.3f}')
        # The segment from each finger's last joint to the skinned tip.
        finger_tips = []
        for fi, f in enumerate(fingers):
            last = tip_joint[fi]
            m = dominant == last
            if m.sum() == 0:
                finger_tips.append(None)
                continue
            d = seg_dirs.get(last, unit(sk.bind_pos(last) - sk.bind_pos(sk.parent[last])))
            proj = (positions[m] - sk.bind_pos(last)) @ d
            tip = positions[m][proj.argmax()]
            finger_tips.append(tip)
            print(f'  finger {fi} tip (furthest dominant vertex along {sk.names[last]}): '
                  f'model {fmt(tip)}  body {fmt(body_of(tip), metres)}  {proj.max() * metres:.3f} m past the joint')

        # ---- wing plane
        sub(f'{side_names[side]}: wing plane')
        fit_joints = [i for i in all_wing if not sk.degenerate(i)]
        joint_pts = np.array([sk.bind_pos(i) for i in fit_joints])
        c_j, n_j, rms_j = fit_plane(joint_pts)
        wing_m = wing_weight[side] > 0.5
        c_v, n_v, rms_v = fit_plane(positions[wing_m], wing_weight[side][wing_m])
        span = np.ptp(joint_pts[:, 0])
        for label, n in (('joints', n_j), ('membrane', n_v)):
            pass
        if n_j[1] < 0:
            n_j = -n_j
        if n_v[1] < 0:
            n_v = -n_v
        ang_j = angle_deg(n_j, [0, 1, 0])
        ang_v = angle_deg(n_v, [0, 1, 0])
        n_j_b = body_of(n_j)
        n_v_b = body_of(n_v)
        print(f'  plane through the {len(fit_joints)} wing joints : normal body {fmt(n_j_b)}  '
              f'RMS residual {rms_j * metres:.3f} m ({100 * rms_j / max(span, 1e-9):.1f}% of the joint span {span * metres:.2f} m)')
        print(f'  plane through {wing_m.sum()} membrane verts (w>0.5): normal body {fmt(n_v_b)}  '
              f'RMS residual {rms_v * metres:.3f} m')
        print(f'  ANGLE(plane normal, body +Y): joints {ang_j:.1f} deg, membrane {ang_v:.1f} deg;   '
              f'joints-vs-membrane normals differ by {angle_deg(n_j, n_v):.1f} deg')
        # Decompose the tilt: about body Z (dihedral/droop, the wing raised or
        # lowered) and about body X (incidence, leading edge up or down).
        tilt_roll = math.degrees(math.atan2(-n_j_b[0], n_j_b[1]))
        tilt_pitch = math.degrees(math.atan2(n_j_b[2], n_j_b[1]))
        print(f'  tilt of the joint plane: about body Z (dihedral, + = tip up on the +X side) {tilt_roll:+.1f} deg, '
              f'about body X (incidence, + = leading edge up) {tilt_pitch:+.1f} deg')
        plane_normals[side] = (n_j, n_v)

        # Per-joint anatomical hinge: normal of the plane spanned by the bone in
        # and the bone out, oriented like the wing plane normal. Only defined
        # where the joint has a parent bone and a child bone.
        hinge = {}
        print('  per-joint hinge normal = unit(cross(bone in, bone out)), body space; angle to body +Y; angle to wing-joint plane normal')
        for i in all_wing:
            p = sk.parent[i]
            outs = [c for c in sk.children[i] if c in all_wing and not sk.degenerate(c)]
            if p < 0 or not outs or sk.degenerate(i) or sk.degenerate(p):
                continue
            d_in = unit(sk.bind_pos(i) - sk.bind_pos(p))
            d_out = unit(np.mean([sk.bind_pos(c) for c in outs], axis=0) - sk.bind_pos(i))
            cr = np.cross(d_in, d_out)
            bend = math.degrees(math.asin(min(np.linalg.norm(cr), 1.0)))
            if np.linalg.norm(cr) < 1e-3:
                print(f'    {sk.names[i]:26s} bones in/out collinear ({bend:.1f} deg bend): hinge undefined, use plane normal')
                hinge[i] = n_j
                continue
            h = unit(cr)
            if np.dot(h, n_j) < 0:
                h = -h
            hinge[i] = h
            print(f'    {sk.names[i]:26s} body {fmt(body_of(h))}  bend {bend:5.1f} deg  to +Y {angle_deg(h, [0, 1, 0]):5.1f} deg  '
                  f'to plane normal {angle_deg(h, n_j):5.1f} deg')
        hinge_normals[side] = hinge

        # ---- membrane extent per bone
        sub(f'{side_names[side]}: membrane extent per wing bone (vertices with weight > 0.5 to that bone)')
        print('  bone                         verts   body-frame bbox min (m)          bbox max (m)             '
              'max perp dist from bone line (m)  centroid off-line (m)  aft reach past bone (m)')
        for i in all_wing:
            m = (dominant == i) & (dominant_w > 0.5)
            if m.sum() == 0:
                print(f'  {sk.names[i]:28s}     0   (no dominant vertices)')
                continue
            pts = positions[m]
            p = sk.parent[i]
            a = sk.bind_pos(p) if p >= 0 else sk.bind_pos(i)
            b = sk.bind_pos(i)
            # For the arm bones the segment is parent->joint; a joint's weights
            # in this rigger sit along the segment it heads, so measure to both.
            outs = sk.children[i]
            if outs:
                b2 = np.mean([sk.bind_pos(c) for c in outs], axis=0)
                seg_a, seg_b = b, b2
            else:
                seg_a, seg_b = a, b
            d = seg_b - seg_a
            L2 = max(float(d @ d), 1e-12)
            t = np.clip(((pts - seg_a) @ d) / L2, 0, 1)
            foot = seg_a + t[:, None] * d
            perp = np.linalg.norm(pts - foot, axis=1)
            cen = pts.mean(0)
            cen_t = np.clip(((cen - seg_a) @ d) / L2, 0, 1)
            cen_off = np.linalg.norm(cen - (seg_a + cen_t * d))
            # Aft reach: body +Z relative to the bone segment's own aft-most point.
            pb = body_pts(pts)
            seg_aft = max(body_of(seg_a)[2], body_of(seg_b)[2])
            aft_reach = pb[:, 2].max() - seg_aft
            bb_lo, bb_hi = pb.min(0) * metres, pb.max(0) * metres
            flag = '' if i in driven else '  [undriven]'
            print(f'  {sk.names[i]:28s} {m.sum():5d}   {fmt(bb_lo)} {fmt(bb_hi)}   '
                  f'{perp.max() * metres:6.3f}   {cen_off * metres:6.3f}   {aft_reach * metres:6.3f}{flag}')

        # ---- is the joint in the leading edge or mid-membrane
        sub(f'{side_names[side]}: joint placement vs the mesh (leading edge and local thickness)')
        wing_pts = positions[wing_m]
        wing_pts_b = body_pts(wing_pts)
        # Leading edge: at each span station (|body x| bin), the forward-most
        # (min body z) wing vertex. The arm bones of a real wing run along it.
        print('  joint                        |x| m   leading edge z at that station (m)   joint z (m)   aft of leading edge (m)   chord there (m)')
        for i in root + [f[0] for f in fingers] + [t for t in tip_joint if t not in root]:
            jb = body_of(sk.bind_pos(i)) * metres
            station = abs(jb[0])
            m = np.abs(np.abs(wing_pts_b[:, 0] * metres) - station) < 0.15
            if m.sum() < 5:
                print(f'  {sk.names[i]:28s} {station:5.2f}   (no wing vertices at this station)')
                continue
            le = wing_pts_b[m][:, 2].min() * metres
            te = wing_pts_b[m][:, 2].max() * metres
            print(f'  {sk.names[i]:28s} {station:5.2f}   {le:8.3f}                          {jb[2]:8.3f}     {jb[2] - le:8.3f}                 {te - le:6.3f}')
        print('  local mesh thickness around each joint (PCA of all vertices within r; sqrt of smallest/largest variance)')
        r_probe = 0.35 / metres  # 35 cm at engine scale, in model units
        print('  joint                        verts in r   thickness m   width m   thick/width   nearest vertex m')
        for i in all_wing:
            jp = sk.bind_pos(i)
            dist = np.linalg.norm(positions - jp, axis=1)
            m = dist < r_probe
            nearest = dist.min() * metres
            if m.sum() < 8:
                print(f'  {sk.names[i]:28s} {m.sum():10d}   (too few)                                {nearest:6.3f}')
                continue
            pts = positions[m] - positions[m].mean(0)
            ev = np.sort(np.linalg.eigvalsh(pts.T @ pts / len(pts)))
            thick, width = math.sqrt(max(ev[0], 0)) * metres, math.sqrt(max(ev[2], 0)) * metres
            print(f'  {sk.names[i]:28s} {m.sum():10d}   {thick:9.3f}   {width:7.3f}   {thick / max(width, 1e-9):9.3f}   {nearest:6.3f}')
        results['sides'][side] = dict(angle_joint_plane=ang_j, angle_membrane_plane=ang_v,
                                      rms_joint_plane_m=rms_j * metres, tilt_roll=tilt_roll,
                                      tilt_pitch=tilt_pitch)

    # ------------------------------------------------------------ fold simulation
    tuning = dict(RIG_DEFAULTS)
    cfg_path = rig_cfg if rig_cfg else path + '.rig.cfg'
    if Path(cfg_path).exists():
        n = load_rig_cfg(tuning, cfg_path)
        print(f'\nrig profile: {cfg_path} ({n} values applied)')
    else:
        print(f'\nrig profile: none at {cfg_path}; RigTuning defaults')
    print('  ' + ', '.join(f'{k}={tuning[k]:g}' for k in (
        'tuck_sweep_deg', 'tuck_fold_deg', 'tuck_droop_deg', 'outboard_decay', 'wing_phase_lag',
        'wing_elbow_fold_scale', 'wing_wrist_fold_scale', 'wing_finger_fold_scale',
        'speed_sweep_deg', 'speed_fold_deg', 'upstroke_fold_deg', 'wing_flap_limit_deg')))

    # The studio's states (src/game/studio.cpp): Dive holds the tuck at 95 m/s;
    # its second half releases it and speed alone shapes the wing; Grounded is
    # the same full tuck at rest with an 18 degree wing angle.
    states = {
        'DIVE tucked (tuck 1, 95 m/s, wing 9 deg)': dict(wing_tuck=1.0, airspeed=95.0, wing_angle=math.radians(9.0)),
        'DIVE released (tuck 0, 95 m/s, wing 9 deg)': dict(wing_tuck=0.0, airspeed=95.0, wing_angle=math.radians(9.0)),
        'GROUNDED (tuck 1, 0 m/s, wing 18 deg)': dict(wing_tuck=1.0, airspeed=0.0, wing_angle=math.radians(18.0)),
    }
    y_axis = np.array([0.0, 1.0, 0.0])

    def axis_unit_y(side, joint):
        return y_axis

    def axis_plane(side, joint):
        return plane_normals[side][0]

    def axis_membrane(side, joint):
        return plane_normals[side][1]

    def axis_hinge(side, joint):
        root = joints['wing_root'][side]
        if joint in root[1:]:
            return hinge_normals[side].get(joint, plane_normals[side][0])
        return plane_normals[side][0]

    # Note on frames: the engine passes body-space axes and this asset's model
    # space differs from body space only by a yaw of 180, which leaves Y alone
    # and flips X and Z. The plane normal is computed in model space here and
    # used in model space, which is what `rotate_joint` would see after the
    # engine re-expressed a body-space axis into model space.
    variants = [('A: body +Y (engine today)', axis_unit_y),
                ('B: wing-joint plane normal per side', axis_plane),
                ('D: membrane-vertex plane normal per side', axis_membrane),
                ('C: anatomical hinge at elbow and wrist (cross of bone in, bone out), plane normal elsewhere', axis_hinge)]

    bind_world = sk.world_bind
    for state_name, state in states.items():
        hr(f'FOLD SIMULATION: {state_name}   [{path}]')
        variant_tips = {}
        for vname, axis_fn in variants:
            sub(vname)
            locals_, summary = drive_wings(sk, joints, tuning, state, axis_fn, model_forward_z)
            world = sk.world_from_locals(locals_)
            skinned = skin(sk, world, positions, joint_ids, weights)
            for side in sorted(summary):
                s = summary[side]
                if vname.startswith('A'):
                    print(f'  {side_names[side]}: sweep {s["sweep_deg"]:.1f} fold {s["fold_deg"]:.1f} droop {s["droop_deg"]:.1f} deg, '
                          f'normalize {s["normalize"]:.3f}; per joint (sweep, fold, flap deg): ' +
                          ', '.join(f'{n}({sw:+.0f},{fo:+.0f},{fl:+.0f})' for n, sw, fo, fl in s['per_joint']))
                all_wing = wing_joint_lists[side] + undriven_lists[side]
                wing_m = wing_weight[side] > 0.5
                folded_b = body_pts(skinned[wing_m]) * metres
                bind_b = body_pts(positions[wing_m]) * metres
                # Tips: the deepest joint of each finger, driven or not.
                tips = []
                for t in tip_joints[side]:
                    tips.append(body_of(world[t][:3, 3]) * metres)
                variant_tips[(vname, side)] = tips
                tips_txt = '  '.join(fmt(t) for t in tips)
                print(f'  {side_names[side]} finger tip joints after fold, body m: {tips_txt}')
                if vname.startswith('A'):
                    bind_tips = '  '.join(fmt(body_of(sk.bind_pos(t)) * metres) for t in tip_joints[side])
                    print(f'  {side_names[side]} finger tip joints at bind,       body m: {bind_tips}')
                # Sanity: a folded chain must keep its bone lengths.
                for i in all_wing:
                    p = sk.parent[i]
                    if p >= 0:
                        l0 = np.linalg.norm(sk.bind_pos(i) - sk.bind_pos(p))
                        l1 = np.linalg.norm(world[i][:3, 3] - world[p][:3, 3])
                        assert abs(l0 - l1) < 1e-6 * max(1.0, l0) + 1e-9, (sk.names[i], l0, l1)
                # Planarity of the folded wing joints and membrane
                jp = np.array([world[i][:3, 3] for i in all_wing if not sk.degenerate(i)])
                _, nf, rms_f = fit_plane(jp)
                _, nfm, rms_fm = fit_plane(skinned[wing_m], wing_weight[side][wing_m])
                _, _, rms_bind_m = fit_plane(positions[wing_m], wing_weight[side][wing_m])
                print(f'  {side_names[side]} planarity: joints RMS {rms_f * metres:.3f} m (bind {results["sides"][side]["rms_joint_plane_m"]:.3f}); '
                      f'membrane RMS {rms_fm * metres:.3f} m (bind {rms_bind_m * metres:.3f})')
                # Out-of-plane motion of each segment relative to the bind wing plane
                n_bind = plane_normals[side][0]
                worst = 0.0
                worst_name = ''
                for i in all_wing:
                    p = sk.parent[i]
                    if p < 0 or p not in all_wing or sk.degenerate(i) or sk.degenerate(p):
                        continue
                    d0 = unit(sk.bind_pos(i) - sk.bind_pos(p))
                    d1 = unit(world[i][:3, 3] - world[p][:3, 3])
                    # Angle each segment makes with the bind plane, before and after.
                    a0 = math.degrees(math.asin(np.clip(d0 @ n_bind, -1, 1)))
                    a1 = math.degrees(math.asin(np.clip(d1 @ n_bind, -1, 1)))
                    if abs(a1 - a0) > abs(worst):
                        worst, worst_name = a1 - a0, sk.names[i]
                print(f'  {side_names[side]} largest change in a segment\'s angle to the BIND wing plane: {worst:+.1f} deg at {worst_name}')
                # Intersections
                depth = torso_penetration(folded_b)
                inside = depth > 0
                mid_x = folded_b[:, 0]
                own_sign = np.sign(bind_b[:, 0].mean())
                crossed = (mid_x * own_sign) < 0
                print(f'  {side_names[side]} membrane verts INSIDE the torso hull: {inside.sum()} of {len(folded_b)} '
                      f'({100 * inside.mean():.1f}%), max depth {depth[inside].max() if inside.any() else 0:.3f} m, '
                      f'mean depth {depth[inside].mean() if inside.any() else 0:.3f} m;  '
                      f'across the midline: {crossed.sum()} ({100 * crossed.mean():.1f}%), furthest {abs(mid_x[crossed]).max() if crossed.any() else 0:.3f} m')
                if inside.any():
                    dom_in = dominant[wing_m][inside]
                    counts = {}
                    for d in dom_in:
                        counts[d] = counts.get(d, 0) + 1
                    top = sorted(counts.items(), key=lambda kv: -kv[1])[:4]
                    print(f'  {side_names[side]}   intruding verts by dominant bone: ' +
                          ', '.join(f'{sk.names[k]} {v}' for k, v in top))
                # Bind-pose baseline for the same test, so a torso-hugging bind does not read as a fold bug.
                depth0 = torso_penetration(bind_b)
                print(f'  {side_names[side]}   (at bind, for reference: {int((depth0 > 0).sum())} inside the torso hull, '
                      f'{int(((bind_b[:, 0] * own_sign) < 0).sum())} across the midline)')
                # Finger order around the wrist, projected on the folded plane
                fingers = joints['wing_fingers'][side]
                if len(fingers) >= 2:
                    wrist = joints['wing_root'][side][-1]
                    r_bind = bind_world[wrist][:3, :3]
                    r_bind = r_bind / np.linalg.norm(r_bind, axis=0, keepdims=True)
                    r_pose = world[wrist][:3, :3]
                    r_pose = r_pose / np.linalg.norm(r_pose, axis=0, keepdims=True)
                    n_carried = unit(r_pose @ r_bind.T @ n_bind)

                    def fan(pos_fn, normal):
                        w = pos_fn(wrist)
                        vecs = [pos_fn(t) - w for t in tip_joints[side]]
                        e1 = unit(vecs[0] - (vecs[0] @ normal) * normal)
                        e2 = np.cross(normal, e1)
                        angles = [math.degrees(math.atan2(v @ e2, v @ e1)) for v in vecs]
                        signs = [np.sign(np.cross(vecs[k], vecs[k + 1]) @ normal) for k in range(len(vecs) - 1)]
                        return angles, signs
                    o0, s0 = fan(lambda i: sk.bind_pos(i), n_bind)
                    o1, s1 = fan(lambda i: world[i][:3, 3], n_carried)
                    same = s0 == s1
                    print(f'  {side_names[side]} finger fan about the wrist, in the plane carried with the wrist (deg): '
                          f'bind {[round(a) for a in o0]} -> folded {[round(a) for a in o1]}; '
                          f'adjacent order {"preserved" if same else "CHANGED (fingers cross)"}')
            # Cross-wing overlap: side0 folded membrane inside side1's folded membrane AABB and vice versa
            if all(s in summary for s in (0, 1)):
                fb = [body_pts(skinned[wing_weight[s] > 0.5]) * metres for s in range(2)]
                for s in range(2):
                    o = 1 - s
                    lo, hi = fb[o].min(0), fb[o].max(0)
                    m = np.all((fb[s] >= lo) & (fb[s] <= hi), axis=1)
                    print(f'  {side_names[s]} membrane verts inside the OTHER wing\'s folded AABB: {m.sum()} ({100 * m.mean():.1f}%)')
        # Tip differences between variants
        sub('tip displacement between variants (finger-end joints, body m)')
        for side in range(2):
            if (variants[0][0], side) not in variant_tips:
                continue
            a = variant_tips[(variants[0][0], side)]
            for vname, _ in variants[1:]:
                b = variant_tips[(vname, side)]
                d = [np.linalg.norm(x - y) for x, y in zip(a, b)]
                print(f'  {side_names[side]} {vname[:2]} vs A: per finger ' + ', '.join(f'{x:.2f}' for x in d) + ' m')
    # ------------------------------------------------------------ profile candidates
    # The same intersection metrics for the full tuck under a handful of
    # profiles, about body Y (A) and about the wing plane normal (B). This is
    # what a recommended .rig.cfg has to be justified from.
    hr(f'PROFILE CANDIDATES at full tuck (tuck 1, 0 m/s, wing 18 deg = GROUNDED)   [{path}]')
    candidates = [
        ('defaults 88/52/22 scales 1/1/1', {}),
        ('embercrest cfg 42/24/12 scales .55/1/1.1', dict(tuck_sweep_deg=42, tuck_fold_deg=24, tuck_droop_deg=12,
                                                         wing_elbow_fold_scale=0.55, wing_wrist_fold_scale=1.0,
                                                         wing_finger_fold_scale=1.1)),
        ('70/40/12 scales 1/1/1', dict(tuck_sweep_deg=70, tuck_fold_deg=40, tuck_droop_deg=12)),
        ('60/36/10 scales .6/1/1.2', dict(tuck_sweep_deg=60, tuck_fold_deg=36, tuck_droop_deg=10,
                                          wing_elbow_fold_scale=0.6, wing_wrist_fold_scale=1.0,
                                          wing_finger_fold_scale=1.2)),
        ('50/30/8 scales .6/1/1.2', dict(tuck_sweep_deg=50, tuck_fold_deg=30, tuck_droop_deg=8,
                                         wing_elbow_fold_scale=0.6, wing_wrist_fold_scale=1.0,
                                         wing_finger_fold_scale=1.2)),
        ('50/30/0 scales .6/1/1.2 (no droop)', dict(tuck_sweep_deg=50, tuck_fold_deg=30, tuck_droop_deg=0,
                                                    wing_elbow_fold_scale=0.6, wing_wrist_fold_scale=1.0,
                                                    wing_finger_fold_scale=1.2)),
        ('42/24/12 scales .4/1/1.3', dict(tuck_sweep_deg=42, tuck_fold_deg=24, tuck_droop_deg=12,
                                          wing_elbow_fold_scale=0.4, wing_wrist_fold_scale=1.0,
                                          wing_finger_fold_scale=1.3)),
        ('42/24/6 scales .55/1/1.1 (half droop)', dict(tuck_sweep_deg=42, tuck_fold_deg=24, tuck_droop_deg=6,
                                                       wing_elbow_fold_scale=0.55, wing_wrist_fold_scale=1.0,
                                                       wing_finger_fold_scale=1.1)),
        ('46/28/8 scales .5/1/1.2', dict(tuck_sweep_deg=46, tuck_fold_deg=28, tuck_droop_deg=8,
                                         wing_elbow_fold_scale=0.5, wing_wrist_fold_scale=1.0,
                                         wing_finger_fold_scale=1.2)),
        ('60/36/10 scales .6/1/1.2 decay .55', dict(tuck_sweep_deg=60, tuck_fold_deg=36, tuck_droop_deg=10,
                                                    wing_elbow_fold_scale=0.6, wing_wrist_fold_scale=1.0,
                                                    wing_finger_fold_scale=1.2, outboard_decay=0.55)),
    ]
    state = states['GROUNDED (tuck 1, 0 m/s, wing 18 deg)']
    print(f'{"profile":44s} {"axis":4s} {"torso%":>7s} {"maxdep":>7s} {"midln%":>7s} {"xwing%":>7s} '
          f'{"tip|x|min":>9s} {"tip|x|max":>9s} {"tipy min":>8s} {"seg out-of-plane":>16s} {"memRMS":>7s} {"tip rot":>8s}')
    for cname, over in candidates:
        t2 = dict(tuning)
        t2.update(over)
        for vlabel, axis_fn in (('A', axis_unit_y), ('B', axis_plane), ('D', axis_membrane)):
            locals_, summary = drive_wings(sk, joints, t2, state, axis_fn, model_forward_z)
            world = sk.world_from_locals(locals_)
            skinned = skin(sk, world, positions, joint_ids, weights)
            torso_pct, maxdep, mid_pct, xw_pct, worst, mem_rms = [], [], [], [], [], []
            tip_x, tip_y = [], []
            fb = {}
            for side in sorted(summary):
                wing_m = wing_weight[side] > 0.5
                fb[side] = body_pts(skinned[wing_m]) * metres
            for side in sorted(summary):
                wing_m = wing_weight[side] > 0.5
                folded_b = fb[side]
                depth = torso_penetration(folded_b)
                inside = depth > 0
                torso_pct.append(100 * inside.mean())
                maxdep.append(depth[inside].max() if inside.any() else 0.0)
                own = np.sign((body_pts(positions[wing_m]) * metres)[:, 0].mean())
                mid_pct.append(100 * ((folded_b[:, 0] * own) < 0).mean())
                other = 1 - side
                if other in fb:
                    lo, hi = fb[other].min(0), fb[other].max(0)
                    xw_pct.append(100 * np.all((folded_b >= lo) & (folded_b <= hi), axis=1).mean())
                n_bind = plane_normals[side][0]
                all_w = wing_joint_lists[side] + undriven_lists[side]
                mem_rms.append(fit_plane(skinned[wing_m], wing_weight[side][wing_m])[2] * metres)
                w_ = 0.0
                for i in all_w:
                    pp = sk.parent[i]
                    if pp < 0 or pp not in all_w or sk.degenerate(i) or sk.degenerate(pp):
                        continue
                    d0 = unit(sk.bind_pos(i) - sk.bind_pos(pp))
                    d1 = unit(world[i][:3, 3] - world[pp][:3, 3])
                    a0 = math.degrees(math.asin(np.clip(d0 @ n_bind, -1, 1)))
                    a1 = math.degrees(math.asin(np.clip(d1 @ n_bind, -1, 1)))
                    w_ = max(w_, abs(a1 - a0))
                worst.append(w_)
                for t in tip_joints[side]:
                    tb = body_of(world[t][:3, 3]) * metres
                    tip_x.append(abs(tb[0]))
                    tip_y.append(tb[1])
            # Total in-plane rotation reaching the tip (sum of sweep+fold down the chain).
            s0 = summary[min(summary)]
            tip_rot = sum(abs(sw) + abs(fo) for _, sw, fo, _ in s0['per_joint'][:len(joints['wing_root'][min(summary)]) + 1])
            print(f'{cname:44s} {vlabel:4s} {np.mean(torso_pct):7.1f} {max(maxdep):7.2f} {np.mean(mid_pct):7.1f} '
                  f'{np.mean(xw_pct) if xw_pct else 0:7.1f} {min(tip_x):9.2f} {max(tip_x):9.2f} {min(tip_y):8.2f} '
                  f'{max(worst):16.1f} {np.mean(mem_rms):7.2f} {tip_rot:8.0f}')
    print('  torso% = wing membrane verts inside the torso slab hull; maxdep = deepest, m; midln% = across the body midline;')
    print("  xwing% = inside the other wing's folded AABB; tip|x| = finger tip distance from the midline, m (torso half-width above);")
    print("  seg out-of-plane = largest change of any wing segment's angle to the bind wing plane, deg; memRMS = RMS distance of the folded membrane")
    print("  vertices from their own best-fit plane, m (a rigidly folded sheet stays near its bind value); tip rot = in-plane rotation reaching the first finger joint, deg")
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('assets', nargs='*', help='.glb files (default: the four shipped models)')
    ap.add_argument('--rig-cfg', help='rig profile to apply instead of <asset>.rig.cfg')
    args = ap.parse_args()
    assets = args.assets or [str(ROOT / a) for a in DEFAULT_ASSETS]
    np.set_printoptions(precision=3, suppress=True)
    summary = []
    for a in assets:
        if not Path(a).exists():
            print(f'missing: {a}', file=sys.stderr)
            continue
        summary.append(probe(a, args.rig_cfg))
    hr('SUMMARY: angle between the bind wing plane normal and body +Y (the fold axis the engine uses)')
    print(f'{"asset":42s} {"side":6s} {"joints plane":>13s} {"membrane plane":>15s} {"dihedral":>9s} {"incidence":>10s}')
    for r in summary:
        for side, s in sorted(r['sides'].items()):
            print(f'{Path(r["path"]).name:42s} {side:<6d} {s["angle_joint_plane"]:13.1f} {s["angle_membrane_plane"]:15.1f} '
                  f'{s["tilt_roll"]:+9.1f} {s["tilt_pitch"]:+10.1f}')


if __name__ == '__main__':
    main()
