"""Build and inspect the measured Embercrest hero skin without changing source bytes."""
import os
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
# Keep delegated builds in their configured cache. The old fixed path escaped the task's write scope.
CACHE = Path(os.environ.get('HERO_RIG_CACHE', ROOT / 'build-hero-rig-cache'))
CACHE.mkdir(exist_ok=True)
sys.pycache_prefix = str(CACHE / 'pycache')
sys.dont_write_bytecode = True
os.environ['TMPDIR'] = str(CACHE)
import json
import struct
import math
import hashlib
import shutil
import argparse
import tempfile
tempfile.tempdir = str(CACHE)
import numpy as np
import bpy
from mathutils import Vector, Matrix, Quaternion
from mathutils.kdtree import KDTree
from mathutils.bvhtree import BVHTree
bpy.context.preferences.filepaths.temporary_directory = str(CACHE)
SOURCE = ROOT / 'assets/embercrest-hero-static.glb'
OUTPUT = ROOT / 'assets/embercrest-hero.glb'
SPEC = ROOT / 'tools/skeletons/embercrest-hero.json'
# A caller can contain reports without overwriting unrelated inspection artifacts.
ART = Path(os.environ.get('HERO_RIG_ART', ROOT / 'artifacts/hero-rig'))
SHEET_SPEC = json.loads(SPEC.read_text())
DTYPES = {5126: '<f4', 5125: '<u4', 5123: '<u2', 5121: 'u1'}
WIDTHS = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


def read_glb(path):
    data = path.read_bytes()
    magic, version, size = struct.unpack_from('<4sII', data)
    assert magic == b'glTF' and version == 2 and size == len(data)
    pos = 12
    doc = binary = None
    while pos < len(data):
        n, kind = struct.unpack_from('<I4s', data, pos)
        chunk = data[pos + 8:pos + 8 + n]
        if kind == b'JSON':
            doc = json.loads(chunk)
        elif kind == b'BIN\x00':
            binary = chunk
        pos += n + 8
    return doc, binary


def accessor(doc, binary, index):
    a = doc['accessors'][index]
    v = doc['bufferViews'][a['bufferView']]
    dtype = np.dtype(DTYPES[a['componentType']])
    width = WIDTHS[a['type']]
    offset = v.get('byteOffset', 0) + a.get('byteOffset', 0)
    stride = v.get('byteStride', width * dtype.itemsize)
    out = np.ndarray((a['count'], width), dtype=dtype, buffer=binary,
                     offset=offset, strides=(stride, dtype.itemsize)).copy()
    return out[:, 0] if width == 1 else out


def geometry(doc, binary):
    out = {}
    for mesh in doc['meshes']:
        p = mesh['primitives'][0]
        out[mesh['name']] = {k: accessor(doc, binary, a) for k, a in p['attributes'].items()}
        out[mesh['name']]['tri'] = accessor(doc, binary, p['indices']).reshape(-1, 3)
    return out


def unique_mesh(g):
    p, inverse = np.unique(np.round(g['POSITION'], 6), axis=0, return_inverse=True)
    tri = inverse[g['tri']]
    return p, tri, inverse


def components(p, tri):
    parent = np.arange(len(p))
    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    for a, b, c in tri:
        ra, rb, rc = root(a), root(b), root(c)
        parent[rb] = ra
        parent[rc] = ra
    ids = np.array([root(i) for i in range(len(p))])
    return [np.flatnonzero(ids == k) for k in np.unique(ids)]


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def blender_point(p):
    return Vector((float(p[0]), -float(p[2]), float(p[1])))


def gltf_points(p):
    return p[:, [0, 2, 1]] * np.array([1, 1, -1])


def eye_measure(g, hide):
    """Recover the ball centre and six lid rings from exported geometry."""
    result = {}
    p, tri, inverse = unique_mesh(hide)
    adjacency = [set() for _ in p]
    for a, b, c in tri:
        adjacency[a].update((b, c))
        adjacency[b].update((a, c))
        adjacency[c].update((a, b))
    for side, sign in [('l', -1), ('r', 1)]:
        q, _, _ = unique_mesh(g['eye_' + side])
        gaze = np.array([sign * .78, .28, -.55])
        gaze /= np.linalg.norm(gaze)
        initial = np.array([sign * .214 - .01, 4.797, -3.645])
        rear = (q - initial) @ gaze < 0
        a = np.c_[2 * q[rear], np.ones(rear.sum())].astype(float)
        fit = np.linalg.lstsq(a, (q[rear].astype(float) ** 2).sum(1), rcond=None)[0]
        centre = fit[:3]
        radii = np.linalg.norm(p - centre, axis=1)
        inner = set(np.flatnonzero(abs(radii - .152) < .00002))
        socket = set(np.flatnonzero(abs(radii - .156) < .00002))
        rings, seen = [inner], inner | socket
        for _ in range(5):
            ring = set.union(*(adjacency[i] for i in rings[-1])) - seen
            rings.append(ring)
            seen |= ring
        assert [len(r) for r in rings] == [36] * 6, 'Lid topology did not match six rings.'
        # Fit the long axis from the measured inner ring.
        edge = p[sorted(inner)]
        _, _, vt = np.linalg.svd(edge - edge.mean(0))
        long = vt[0]
        up = np.cross(gaze, long)
        if up[1] < 0:
            long = -long
            up = -up
        up /= np.linalg.norm(up)
        normal = np.cross(long, up)
        normal /= np.linalg.norm(normal)
        result[side] = dict(centre=centre, gaze=gaze, long=long, up=up, normal=normal,
                            rings=[np.flatnonzero(np.isin(inverse, list(r))) for r in rings],
                            unique_rings=[sorted(r) for r in rings],
                            socket=np.flatnonzero(np.isin(inverse, list(socket))))
    return result


def segment_distance(p, a, b):
    delta = b - a
    t = np.clip((p - a) @ delta / (delta @ delta), 0, 1)
    return np.linalg.norm(p - a - t[:, None] * delta, axis=1)


def top_four(score):
    """Keep four influences a row and stay continuous: subtract the fifth score.

    A row whose leading scores tie has no mass left after the subtraction. That
    row goes whole to its first leading bone. The first rig divided by zero here.
    """
    score = np.asarray(score, float)
    if score.shape[1] > 4:
        cutoff = -np.partition(-score, 4, axis=1)[:, 4:5]
        w = np.maximum(0, score - cutoff)
    else:
        w = score.copy()
    total = w.sum(1)
    tied = total < 1e-9 * np.maximum(score.max(1), 1e-30)
    w[tied] = 0
    w[tied, score[tied].argmax(1)] = 1
    total[tied] = 1
    return w / total[:, None]


def field(p, bones, names, radius=.22):
    d = np.array([segment_distance(p, np.array(bones[n][1]), np.array(bones[n][2])) for n in names]).T
    w = top_four(1 / (d * d + radius * radius) ** 1.5)
    out = np.zeros((len(p), len(bones)), np.float32)
    out[:, [list(bones).index(n) for n in names]] = w
    return out


def near_field(p, bones, names, radius):
    """Weights from how much farther each bone is than the nearest one.

    Distance to the spine does not flatten this field, so a flank two metres
    wide still follows the one or two bones beside it.
    """
    d = np.array([segment_distance(p, np.array(bones[n][1]), np.array(bones[n][2])) for n in names]).T
    d = d - d.min(1, keepdims=True)
    w = top_four(1 / (d * d + radius * radius) ** 3)
    out = np.zeros((len(p), len(bones)), np.float32)
    out[:, [list(bones).index(n) for n in names]] = w
    return out


def relax(n, edges, stiffness, hold, target, shut=None, tolerance=1e-7, limit=6000):
    """Solve for the smoothest weights that stay near `target` where `hold` is large.

    Minimises sum over edges of stiffness * |w_i - w_j|^2 plus sum over vertices
    of hold * |w_i - target_i|^2, by conjugate gradients. Each row of the answer
    is a weighted mean of target rows, so it is not negative. `shut` marks
    (vertex, bone) pairs that are pulled to zero: a bone's weight then fades out
    smoothly at the edge of the region where it is allowed. Rows are normalised.
    """
    both = np.concatenate([edges, edges[:, ::-1]])
    kk = np.concatenate([stiffness, stiffness])
    order = np.argsort(both[:, 0], kind='stable')
    both, kk = both[order], kk[order]
    start = np.searchsorted(both[:, 0], np.arange(n))
    empty = np.diff(np.r_[start, len(both)]) == 0
    assert not empty.any(), 'A wing vertex has no edge.'
    used = np.flatnonzero(target.any(0))
    row = np.add.reduceat(kk, start)
    diagonal = np.repeat((hold + row)[:, None], len(used), axis=1)
    b = hold[:, None] * target[:, used].astype(float)
    if shut is not None:
        diagonal += 1e3 * row[:, None] * shut[:, used]
        b[shut[:, used]] = 0

    def apply(x):
        return diagonal * x - np.add.reduceat(kk[:, None] * x[both[:, 1]], start, axis=0)

    x = target[:, used].astype(float).copy()
    if shut is not None:
        x[shut[:, used]] = 0
    r = b - apply(x)
    z = r / diagonal
    d = z.copy()
    rz = (r * z).sum(0)
    scale = np.sqrt((b * b).sum(0)) + 1e-30
    for step in range(limit):
        ad = apply(d)
        alpha = rz / np.maximum((d * ad).sum(0), 1e-300)
        x += alpha * d
        r -= alpha * ad
        if (np.sqrt((r * r).sum(0)) / scale).max() < tolerance:
            break
        z = r / diagonal
        rz_new = (r * z).sum(0)
        d = z + (rz_new / np.maximum(rz, 1e-300)) * d
        rz = rz_new
    out = np.zeros(target.shape)
    out[:, used] = np.clip(x, 0, None)
    return out / out.sum(1, keepdims=True), step + 1


WING_HOLD = (.3, 1.5)
SUPPORT_PASSES = 2
MIRROR_BLEND = (.02, .06)
MIRROR_TURNS = 8
FOLD_STRETCH = 1.4
FOLD_ROUNDS = 40
FOLD_DEGREES = 80
MEMBRANE_THICKNESS = .12
MEMBRANE_RIM = .06


def fold_matrices(bones, names, side, sign, degrees=None):
    """Joint matrices of the test fold: the elbow turns the forearm forward, the wrist turns the hand back."""
    degrees = FOLD_DEGREES if degrees is None else degrees

    def turn(angle, pivot):
        m = np.eye(4)
        m[:3, :3] = np.array(Quaternion(Vector(wing_hinge(side)), math.radians(angle)).to_matrix())
        m[:3, 3] = np.array(pivot) - m[:3, :3] @ np.array(pivot)
        return m

    elbow = turn(sign * degrees, bones['wing_arm_' + side][1])
    wrist = (elbow @ np.r_[bones['wing_wrist_' + side][1], 1])[:3]
    hand = turn(-sign * degrees, wrist) @ elbow
    out = np.repeat(np.eye(4)[None], len(names), axis=0)
    for i, name in enumerate(names):
        if name == 'wing_arm_' + side:
            out[i] = elbow
        elif name.endswith('_' + side) and name.startswith(('wing_wrist', 'wing_finger')):
            out[i] = hand
    return out


# Mesh version 5: the membrane is one sheet with an offset second face. Its vertices are weighted
# by the panel they lie in, not by the relaxed solve, and the two faces carry identical rows.
SHEET_GAP = (.012, .030)            # m: an offset copy lies this far from its twin
SHEET_EPS = .05                     # m: keeps the score finite on a bone
SHEET_FLANK_X = .55                 # m: the membrane's inner edge lies on the flank here
SHEET_SEGMENT_RADIUS = .20          # m: segments of one chain blend across a knuckle over about this
SHEET_CHAINS = {'arm': ('wing_root', 'wing_arm', 'wing_wrist'), 'f1': ('wing_finger_1a', 'wing_finger_1b', 'wing_finger_1c'),
                'f2': ('wing_finger_2a', 'wing_finger_2b'), 'f3': ('wing_finger_3a', 'wing_finger_3b'),
                'spur': ('wing_spur_a', 'wing_spur_b')}


def sheet_twins(points):
    """For each point, the index of its offset twin, or -1. The twin is the only other point this close."""
    tree = KDTree(len(points))
    for i, q in enumerate(points):
        tree.insert(q, i)
    tree.balance()
    twin = np.full(len(points), -1)
    for i, q in enumerate(points):
        near = [(d, k) for _, k, d in tree.find_range(q, SHEET_GAP[1]) if k != i and d >= SHEET_GAP[0]]
        if len(near) == 1:
            twin[i] = near[0][1]
    return twin


def sheet_weights(points, bones, names, side, body, chains=None):
    """Panel weights: the two nearest bone chains share a vertex by its distance to each, linearly
    across the panel (a third chain is subtracted, so a swap of second and third is not a step).
    The body is a chain whose distance is the vertex's reach past the flank. Inside a chain the
    weight goes to its segments by a near field."""
    score, parts = [], []
    for chain, stems in SHEET_CHAINS.items():
        if chains is not None and chain not in chains:
            continue
        bn = [s + '_' + side for s in stems]
        d = np.array([segment_distance(points, np.array(bones[b][1]), np.array(bones[b][2])) for b in bn]).T
        score.append(1 / (d.min(1) + SHEET_EPS))
        rel = d - d.min(1, keepdims=True)
        w = 1 / (rel * rel + SHEET_SEGMENT_RADIUS ** 2) ** 3
        w /= w.sum(1, keepdims=True)
        dense = np.zeros((len(points), len(names)))
        for k, b in enumerate(bn):
            dense[:, names.index(b)] = w[:, k]
        # The rib's outer bone only marks where the rib runs: its weight goes to the inner one.
        if chain == 'spur':
            dense[:, names.index('wing_spur_a_' + side)] += dense[:, names.index('wing_spur_b_' + side)]
            dense[:, names.index('wing_spur_b_' + side)] = 0
        parts.append(dense)
    if body is not None:
        score.append(1 / (np.maximum(np.abs(points[:, 0]) - SHEET_FLANK_X, 0) + SHEET_EPS))
        parts.append(body)
    score = np.array(score).T
    third = -np.partition(-score, 2, axis=1)[:, 2:3] if score.shape[1] > 2 else np.zeros((len(score), 1))
    share = np.maximum(score - third, 0)
    share /= share.sum(1, keepdims=True)
    return four(sum(share[:, k, None] * parts[k] for k in range(len(parts))))


def four(w):
    """At most four influences a row, without a step: subtract the fifth, then normalise."""
    w = top_four(w)
    w[np.argsort(np.argsort(-w, axis=1), axis=1) >= 4] = 0
    return w / w.sum(1, keepdims=True)


class Wing:
    """One wing's hide and membrane as one surface, and the weights solved on it.

    The field gives each hide vertex a target. A vertex on a bone's shaft holds
    its target: the next bone is much farther than its own. A vertex near a
    joint, on the flare under the upper arm or on the membrane holds nothing and
    takes the smoothest blend its neighbours allow. A fold then shears every
    panel evenly. The body side of the wing root holds the body's field.
    """

    def __init__(self, hide, membrane, body, wing, mask, pins, bones, names, side, sign):
        self.side, self.sign, self.bones, self.names = side, sign, bones, names
        hp = hide['POSITION'].astype(float)
        mp = membrane['POSITION'].astype(float)
        self.hid = hid = np.flatnonzero(mask > 0)
        self.mid = mid = np.flatnonzero(mp[:, 0] * sign > 0)
        points = np.concatenate([hp[hid], mp[mid]])
        self.u, self.inverse = u, inverse = np.unique(np.round(points, 5), axis=0, return_inverse=True)
        self.n = n = len(u)
        local = np.full(len(hp), -1)
        local[hid] = inverse[:len(hid)]
        ht = hide['tri'][np.all(local[hide['tri']] >= 0, axis=1)]
        mlocal = np.full(len(mp), -1)
        mlocal[mid] = inverse[len(hid):]
        mt = membrane['tri'][np.all(mlocal[membrane['tri']] >= 0, axis=1)]
        self.hide_tri, self.web_tri = local[ht], mlocal[mt]
        self.tri = tri = np.concatenate([self.hide_tri, self.web_tri])
        # Cotangent stiffness: the answer then depends on the surface, not on how it was triangulated.
        pairs, cots = [], []
        for a, b, c in [(0, 1, 2), (1, 2, 0), (2, 0, 1)]:
            e1 = u[tri[:, a]] - u[tri[:, c]]
            e2 = u[tri[:, b]] - u[tri[:, c]]
            area = np.linalg.norm(np.cross(e1, e2), axis=1)
            pairs.append(np.sort(tri[:, [a, b]], axis=1))
            cots.append(.5 * (e1 * e2).sum(1) / np.maximum(area, 1e-12))
        pairs, cots = np.concatenate(pairs), np.concatenate(cots)
        edges, slot = np.unique(pairs, axis=0, return_inverse=True)
        stiffness = np.zeros(len(edges))
        np.add.at(stiffness, slot.ravel(), cots)
        live = edges[:, 0] != edges[:, 1]
        edges, stiffness = edges[live], np.maximum(stiffness[live], .02)
        # The membrane is a thin shell with two faces. Tie each vertex to the vertices just
        # beside it and to the one across it, or the two faces solve apart.
        shell = np.unique(inverse[len(hid):])
        tree = KDTree(len(shell))
        for k, i in enumerate(shell):
            tree.insert(u[i], k)
        tree.balance()
        facing = np.zeros((n, 3))
        np.add.at(facing, inverse[len(hid):], membrane['NORMAL'][mid].astype(float))
        facing /= np.maximum(np.linalg.norm(facing, axis=1, keepdims=True), 1e-9)
        ties = []
        for i in shell:
            near = [(d, shell[k]) for _, k, d in tree.find_range(u[i], MEMBRANE_THICKNESS) if shell[k] != i]
            ties += [(i, k) for d, k in near if d < MEMBRANE_RIM and k > i]
            across = [(d, k) for d, k in near if facing[i] @ facing[k] < -.3]
            if across:
                ties.append((i, min(across)[1]))
        ties = np.array(ties)
        edges = np.concatenate([edges, ties])
        stiffness = np.concatenate([stiffness, np.full(len(ties), 20.)])
        degree = np.zeros(n)
        np.add.at(degree, edges[:, 0], stiffness)
        np.add.at(degree, edges[:, 1], stiffness)
        wn = [b for b in names if b.startswith('wing') and b.endswith('_' + side)]
        # How surely a vertex belongs where the field puts it: its second bone must be much
        # farther than its first. That holds on a shaft and fails at a joint, on the flare and
        # on the membrane.
        d = np.sort(np.array([segment_distance(u, np.array(bones[b][1]), np.array(bones[b][2])) for b in wn]).T, axis=1)
        shaft = smoothstep(*WING_HOLD, (d[:, 1] - d[:, 0]) / (d[:, 0] + .05))
        m = np.ones(n)
        self.is_hide = is_hide = np.zeros(n, bool)
        rows = inverse[:len(hid)]
        m[rows] = mask[hid]
        is_hide[rows] = True
        lonely = np.flatnonzero(degree == 0)
        keep = np.flatnonzero(degree > 0)
        degree[lonely] = 1
        # Two pulls on a hide vertex: the body's field where the mask is low, its own bone on a shaft.
        to_body = np.where(is_hide, 10 * (1 - m) ** 2, 0)
        to_bone = np.where(is_hide, m * shaft, 0)
        target = np.zeros((n, body.shape[1]))
        target[rows] = body[hid] * to_body[rows, None] + wing[hid] * to_bone[rows, None]
        self.hold = total = to_body + to_bone
        target[total > 0] /= total[total > 0, None]
        target[total == 0, names.index('wing_root_' + side)] = 1
        # The hide under a claw follows the claw's bone and nothing else, or the claw lifts off.
        self.pinned = np.unique(rows[pins[hid] >= 0])
        self.pin_target = np.zeros((len(self.pinned), body.shape[1]))
        owner = np.full(n, -1)
        owner[rows] = np.maximum(owner[rows], pins[hid])
        self.pin_target[np.arange(len(self.pinned)), owner[self.pinned]] = 1
        target[self.pinned] = self.pin_target
        total[self.pinned] = 5
        # The spur is one rigid rib. Its outer bone only marks where the rib runs.
        outer, inner = names.index('wing_spur_b_' + side), names.index('wing_spur_a_' + side)
        target[:, inner] += target[:, outer]
        target[:, outer] = 0
        if len(lonely):
            edges = np.concatenate([edges, np.c_[lonely, np.full(len(lonely), keep[0])]])
            stiffness = np.concatenate([stiffness, np.full(len(lonely), 1e-6)])
        solved, steps = relax(n, edges, stiffness, degree * (total + 1e-9), target)
        # Four influences a vertex. Solve again with every bone shut outside the vertices where
        # it is one of the leading four, so no weight is cut off at a step.
        for _ in range(SUPPORT_PASSES):
            rank = np.argsort(np.argsort(-solved, axis=1), axis=1)
            solved, more = relax(n, edges, stiffness, degree * (total + 1e-9), target, shut=rank >= 4)
            steps += more
        # One sheet, not two. The upper face takes its weights from the closest point of the
        # lower face, so the two faces move as one and the rim between them keeps its length.
        anchors = np.array([q for b in wn for q in bones[b][1:3]])
        up = np.linalg.svd(anchors - anchors.mean(0))[2][-1]
        up = up if up[1] > 0 else -up
        upper = ~is_hide & (facing @ up > .3)
        lower_tri = self.web_tri[~upper[self.web_tri].any(1)]
        sheet = BVHTree.FromPolygons(u.tolist(), lower_tri.tolist(), all_triangles=True)
        copy = solved.copy()
        for i in np.flatnonzero(upper):
            location, _, face, distance = sheet.find_nearest(Vector(u[i]), MEMBRANE_THICKNESS * 2)
            if location is not None:
                copy[i] = barycentric(u[lower_tri[face]], location) @ solved[lower_tri[face]]
        self.solved = four(copy)
        self.solved[self.pinned] = self.pin_target
        # Mesh version 5: a membrane whose vertices nearly all have an offset twin is one sheet.
        local = sheet_twins(u[shell])
        has = local >= 0
        self.sheet = has.mean() > .8
        if self.sheet:
            # Both twins take the weights at their middle, so their rows are identical.
            mid_point = np.where(has[:, None], (u[shell] + u[shell[np.maximum(local, 0)]]) / 2, u[shell])
            from_body = smooth_field(mid_point, SHEET_SPEC, parts=True)[3]
            self.sheet_rows = shell
            self.sheet_w = sheet_weights(mid_point, bones, names, side, from_body)
            # Mesh version 5's root panel is a ruled patch from the flank (s = 0) to the arm and the
            # rib (s = 1). Its weights run linearly in s from the nearest body vertex's to the arm's and rib's.
            if '_SHEET_S' in membrane:
                sv = np.full(n, -1.0)
                sv[inverse[len(hid):]] = membrane['_SHEET_S'][mid].astype(float)
                root = sv[shell] >= 0
                if root.any():
                    q = mid_point[root]
                    wb = sheet_weights(q, bones, names, side, None, chains=('arm', 'spur'))
                    flank = np.flatnonzero((mask < .02) & (hp[:, 0] * sign > .3))
                    kd = KDTree(len(flank))
                    for k, i in enumerate(flank):
                        kd.insert(hp[i], k)
                    kd.balance()
                    wa = np.array([body[flank[kd.find(Vector(x))[1]]] for x in q], float)
                    # Nearest body vertices differ across the sculpt. Average mirrored samples before fixing the seam.
                    other=np.flatnonzero((mask<.02)&(hp[:,0]*sign<-.3))
                    other_tree=KDTree(len(other))
                    for k,i in enumerate(other):other_tree.insert(hp[i],k)
                    other_tree.balance()
                    mirror=np.array([names.index(n[:-2]+('_r' if n.endswith('_l') else '_l')) if n.endswith(('_l','_r')) else i for i,n in enumerate(names)])
                    opposite=np.array([body[other[other_tree.find(Vector(x*np.array([-1,1,1])))[1]]] for x in q],float)
                    wa=.5*(wa+opposite[:,mirror])
                    s_ = sv[shell][root][:, None]
                    self.sheet_w[root] = four(s_ * wb + (1 - s_) * wa)
                    # Fold relaxation moved the flank boundary away from the body. Pin both faces to the body row.
                    edge_value=np.zeros(n)
                    if '_SHEET_EDGE' in membrane:
                        edge_value[inverse[len(hid):]]=membrane['_SHEET_EDGE'][mid]
                    boundary=edge_value[shell[root]]>.5
                    fixed = shell[root][boundary]
                    self.pinned = np.r_[self.pinned, fixed]
                    self.pin_target = np.vstack([self.pin_target, four(wa[boundary])])
                    print('WING', side, 'root patch:', int(root.sum()), 'vertices from the flank to the arm and rib', flush=True)
            self.solved[shell] = self.sheet_w
            # Twins by surface row: the fold relaxing below moves a pair together.
            self.pairs = np.unique(np.sort(np.c_[shell[has], shell[local[has]]], axis=1), axis=0)
            print('WING', side, 'sheet membrane:', int(has.sum()), 'of', len(shell), 'vertices have a twin', flush=True)
        web = np.unique(np.sort(np.concatenate([self.web_tri[:, [0, 1]], self.web_tri[:, [1, 2]], self.web_tri[:, [2, 0]]]), axis=1), axis=0)
        self.web = web[np.linalg.norm(u[web[:, 0]] - u[web[:, 1]], axis=1) > 1e-3]
        self.loose = self.web_tri[~is_hide[self.web_tri].any(1)]
        print('WING', side, n, 'surface vertices;', int((total > .5).sum()), 'held;', len(ties), 'ties in the membrane;',
              int(upper.sum()), 'upper vertices follow the lower face;', steps, 'solver steps', flush=True)

    def fold(self):
        """Fold the wing as the verifier does and relax what the fold breaks.

        Each free end of a membrane edge that stretches past FOLD_STRETCH moves
        halfway to the other end. Each free corner of a membrane triangle that
        cuts the arm moves halfway to the weights of the arm triangle it cuts,
        so it travels with that part of the arm, as it does at rest.
        Returns (rounds, longest edge ratio, cutting triangles left).
        """
        u, n, web = self.u, self.n, self.web
        pose = fold_matrices(self.bones, self.names, self.side, self.sign)
        length = np.linalg.norm(u[web[:, 0]] - u[web[:, 1]], axis=1)
        thin = np.array([('finger' in b) or ('spur' in b) for b in self.names])
        # Mesh version 3 does not union the wings with the body: the membrane's root ends inside
        # the flank. A triangle that cuts the arm or the body at rest is that root, and is left alone.
        rest_tri = self.hide_tri[~thin[self.solved.argmax(1)][self.hide_tri].any(1)]
        at_rest = {k for _, k in BVHTree.FromPolygons(u.tolist(), rest_tri.tolist(), all_triangles=True).overlap(
            BVHTree.FromPolygons(u.tolist(), self.loose.tolist(), all_triangles=True))}
        for rounds in range(FOLD_ROUNDS + 1):
            solved = self.solved
            posed = np.einsum('vj,jab,vb->va', solved, pose, np.c_[u, np.ones(n)])[:, :3]
            ratio = np.linalg.norm(posed[web[:, 0]] - posed[web[:, 1]], axis=1) / length
            tight = web[ratio > FOLD_STRETCH]
            arm_tri = self.hide_tri[~thin[solved.argmax(1)][self.hide_tri].any(1)]
            cuts = BVHTree.FromPolygons(posed.tolist(), arm_tri.tolist(), all_triangles=True).overlap(
                BVHTree.FromPolygons(posed.tolist(), self.loose.tolist(), all_triangles=True))
            cuts = [(i, k) for i, k in cuts if k not in at_rest]
            if (not len(tight) and not cuts) or rounds == FOLD_ROUNDS:
                return rounds, float(ratio.max()), len(cuts)
            pull = np.zeros_like(solved)
            pulls = np.zeros(n)
            for a, b in [(0, 1), (1, 0)]:
                np.add.at(pull, tight[:, a], solved[tight[:, b]] - solved[tight[:, a]])
                np.add.at(pulls, tight[:, a], 1)
            for i, k in cuts:
                for v in self.loose[k]:
                    pull[v] += solved[arm_tri[i]].mean(0) - solved[v]
                    pulls[v] += 1
            free = (pulls > 0) & (self.hold < .5)
            solved = solved.copy()
            solved[free] += .5 * pull[free] / pulls[free, None]
            self.solved = four(solved)
            self.solved[self.pinned] = self.pin_target
            self.tie_twins()

    def sample(self, points, reach):
        """Weights at the closest surface point to each query, and the distance to it."""
        if not hasattr(self, 'surface'):
            self.surface = BVHTree.FromPolygons(self.u.tolist(), self.tri.tolist(), all_triangles=True)
        out = np.zeros((len(points), self.solved.shape[1]))
        distance = np.full(len(points), np.inf)
        for i, q in enumerate(points):
            location, _, face, d = self.surface.find_nearest(Vector(q), reach)
            if location is not None:
                out[i] = barycentric(self.u[self.tri[face]], location) @ self.solved[self.tri[face]]
                distance[i] = d
        return out, distance

    def tie_twins(self):
        """A sheet's two faces carry one row: each pair takes the mean of its two rows."""
        if self.sheet:
            a, b = self.pairs[:, 0], self.pairs[:, 1]
            mean = four((self.solved[a] + self.solved[b]) / 2)
            self.solved[a] = mean
            self.solved[b] = mean

    def result(self):
        self.tie_twins()
        k = len(self.hid)
        return self.hid, self.solved[self.inverse[:k]], self.mid, self.solved[self.inverse[k:]]


def barycentric(corners, point):
    b = np.linalg.lstsq(np.c_[corners, np.ones(3)].T, np.r_[np.array(point), 1], rcond=None)[0]
    b = np.clip(b, 0, None)
    return b / b.sum()


def mirror_wings(left, right, names):
    """Make the two wings agree where their surfaces agree.

    The two sides of the sculpt differ, so the two solves differ. Each vertex
    takes the mean of its own weights and the other wing's weights at its
    mirrored position, and the other wing then copies the result. The share fades to nothing between MIRROR_BLEND[0] and
    MIRROR_BLEND[1] metres from the other wing's surface. Returns the largest
    change made.
    """
    swap = np.array([names.index(n[:-2] + ('_r' if n.endswith('_l') else '_l')) if n.endswith(('_l', '_r')) else i for i, n in enumerate(names)])
    flip = np.array([-1., 1, 1])
    change = 0.
    # The left wing moves halfway to the right one. The right wing then takes the new left one.
    for own, other, most in [(left, right, .5), (right, left, 1.)]:
        theirs, distance = other.sample(own.u * flip, MIRROR_BLEND[1])
        share = most * (1 - smoothstep(*MIRROR_BLEND, np.where(np.isfinite(distance), distance, MIRROR_BLEND[1])))
        blended = own.solved * (1 - share[:, None]) + theirs[:, swap] * share[:, None]
        change = max(change, float(abs(blended - own.solved).max()))
        own.solved = four(blended)
        own.solved[own.pinned] = own.pin_target
        own.tie_twins()
    return change


def compact_weights(w):
    w = top_four(w)
    order = np.argsort(-w, axis=1)[:, :4]
    values = np.take_along_axis(w, order, axis=1).astype('<f4')
    values /= values.sum(1, keepdims=True)
    assert np.isfinite(values).all() and (values >= 0).all()
    return order.astype('<u2'), values


JOINT_BAND = .20


def limb_field(p, bones, names):
    """Blend adjacent limb segments across a 0.40 m joint band."""
    main = names[:4]
    heads = np.array([bones[n][1] for n in main])
    tails = np.array([bones[n][2] for n in main])
    lengths = np.linalg.norm(tails - heads, axis=1)
    distance = np.array([segment_distance(p, a, b) for a, b in zip(heads, tails)]).T
    nearest = distance.argmin(1)
    w = np.zeros((len(p), len(bones)), np.float32)
    indices = {n: list(bones).index(n) for n in names}
    for i, n in enumerate(main):
        ids = np.flatnonzero(nearest == i)
        along = (p[ids] - heads[i]) @ ((tails[i] - heads[i]) / lengths[i])
        band = min(JOINT_BAND, .45 * lengths[i])
        previous = 1 - smoothstep(-band, band, along) if i else np.zeros(len(ids))
        following = smoothstep(lengths[i] - band, lengths[i] + band, along) if i < 3 else np.zeros(len(ids))
        w[ids, indices[n]] = 1 - previous - following
        if i:
            w[ids, indices[main[i - 1]]] = previous
        if i < 3:
            w[ids, indices[main[i + 1]]] = following
    toes = [n for n in names if n.startswith('toe_')]
    ids = np.flatnonzero(p[:, 1] < .35)
    tw = field(p[ids], bones, toes, .055)
    blend = (1 - smoothstep(.18, .35, p[ids, 1]))[:, None]
    w[ids] = w[ids] * (1 - blend) + tw * blend
    return w


# The plane that ends the rigid head: through the head joint, facing forward and up.
HEAD_NORMAL = np.array([0., .4472, -.8944])
# Mesh version 4: the shoulder is at Y 2.75 and the hip at Y 2.90, so each mask fades out higher than on version 2
# (2.0 to 2.6 and 1.95 to 2.65), or the upper arm and the thigh would follow the spine.
LIMB = {True: dict(prefixes=('upper_arm_', 'forearm_', 'wrist_', 'hand_', 'toe_fore_'), radius=(.42, .58), height=(2.2, 2.9)),
        False: dict(prefixes=('thigh_', 'shin_', 'ankle_', 'foot_', 'toe_hind_'), radius=(.60, .90), height=(2.3, 3.1))}


def limb_names(names, side, fore):
    return [n for n in names if n.endswith('_' + side) and n.startswith(LIMB[fore]['prefixes'])]


def limb_mask(p, bones, ln, fore, sign):
    """One where a vertex belongs to this leg, zero on the belly and the other side."""
    upper = np.min([segment_distance(p, np.array(bones[n][1]), np.array(bones[n][2])) for n in ln[:3]], axis=0)
    lower = np.min([segment_distance(p, np.array(bones[n][1]), np.array(bones[n][2])) for n in ln[3:]], axis=0)
    r0, r1 = LIMB[fore]['radius']
    y0, y1 = LIMB[fore]['height']
    m = np.maximum(1 - smoothstep(r0, r1, upper), (1 - smoothstep(.40, .60, lower)) * (1 - smoothstep(.5, .8, p[:, 1])))
    return m * (1 - smoothstep(y0, y1, p[:, 1])) * (p[:, 0] * sign > 0)


def wing_mask(p, spec, sign):
    """One on the wing, zero on the body. Near the body the wing root blends in by height and station."""
    f = spec['wing_field']
    x, y, z = abs(p[:, 0]), p[:, 1], p[:, 2]
    root = smoothstep(*f['root_y'], y) * smoothstep(*f['root_z_front'], z) * (1 - smoothstep(*f['root_z_rear'], z))
    free = smoothstep(*f['free_x'], x)
    return smoothstep(*f['span_x'], x) * (root + (1 - root) * free) * smoothstep(2.2, 2.45, y) * (p[:, 0] * sign > 0)


def smooth_field(p, spec, parts=False):
    """The continuous weight field: a function of position alone, the same on both sides."""
    bones = {b[0]: b for b in spec['bones']}
    names = list(bones)
    body_names = ['root', 'chest', 'neck_01', 'neck_02', 'neck_03', 'head']
    body_names += [n for n in names if n.startswith('tail_')]
    w = near_field(p, bones, body_names, .35)
    head = smoothstep(-.25, .10, (p - np.array(bones['head'][1])) @ HEAD_NORMAL) * (abs(p[:, 0]) < 1.5)
    w *= (1 - head)[:, None]
    w[:, names.index('head')] += head
    limb_total = np.zeros(len(p))
    wing_total = np.zeros(len(p))
    wing_field = np.zeros_like(w)
    for side, sign in [('l', -1), ('r', 1)]:
        for fore in [True, False]:
            ln = limb_names(names, side, fore)
            m = limb_mask(p, bones, ln, fore, sign)
            ids = np.flatnonzero(m > 0)
            w[ids] = w[ids] * (1 - m[ids, None]) + limb_field(p[ids], bones, ln) * m[ids, None]
            limb_total += m
        wn = [n for n in names if n.startswith('wing') and n.endswith('_' + side)]
        m = wing_mask(p, spec, sign)
        # Inside free_x a point far from every wing bone belongs to the body: the membrane's root corner.
        reach = np.min([segment_distance(p, np.array(bones[n][1]), np.array(bones[n][2])) for n in wn], axis=0)
        near = 1 - smoothstep(*spec['wing_field']['bone_reach'], reach)
        free = smoothstep(*spec['wing_field']['free_x'], abs(p[:, 0]))
        m *= near + (1 - near) * free
        ids = np.flatnonzero(m > 0)
        wf = near_field(p[ids], bones, wn, spec['wing_field']['radius_m'])
        hub = np.array(bones['wing_wrist_' + side][1])
        share = 1 - smoothstep(*spec['wing_field']['wrist_share'], np.linalg.norm(p[ids] - hub, axis=1))
        wf *= 1 - share[:, None]
        wf[:, names.index('wing_wrist_' + side)] += share
        wing_field[ids] = wf
        wing_total += m
    blend = w * (1 - wing_total[:, None]) + wing_field * wing_total[:, None]
    return (blend, limb_total, wing_total, w, wing_field) if parts else blend


SPIKE_SMOOTHING = 80
SPIKE_MERGE = .04
SPIKE_PROMINENCE = .10
SPIKE_EXTENT = 1.5


def spike_segments(geo, eligible):
    """Split the hide into spikes and plates, and return those that stand out.

    Protrusion is the distance of a vertex outside a smoothed copy of the hide.
    Each vertex climbs to a peak of protrusion. Peaks closer than SPIKE_MERGE to
    their saddle merge. A segment counts as a spike if it rises SPIKE_PROMINENCE
    over its own base, spans less than SPIKE_EXTENT, and no vertex in it belongs
    to a leg, a wing, the jaw or a lid. Returns (export vertex ids, base ids, tip id).
    """
    p, tri, inverse = unique_mesh(geo)
    p = p.astype(float)
    n = len(p)
    edges = np.unique(np.sort(np.concatenate([tri[:, [0, 1]], tri[:, [1, 2]], tri[:, [2, 0]]]), axis=1), axis=0)
    edges = edges[edges[:, 0] != edges[:, 1]]
    both = np.concatenate([edges, edges[:, ::-1]])
    both = both[np.argsort(both[:, 0], kind='stable')]
    start = np.searchsorted(both[:, 0], np.arange(n + 1))
    degree = np.maximum(np.diff(start), 1)

    def average(values):
        total = np.zeros_like(values)
        np.add.at(total, both[:, 0], values[both[:, 1]])
        return total / (degree[:, None] if values.ndim == 2 else degree)

    q = p.copy()
    for _ in range(SPIKE_SMOOTHING):
        q = .5 * q + .5 * average(q)
    normal = np.zeros((n, 3))
    np.add.at(normal, inverse, geo['NORMAL'].astype(float))
    normal /= np.maximum(np.linalg.norm(normal, axis=1, keepdims=True), 1e-9)
    f = ((p - q) * normal).sum(1)
    for _ in range(2):
        f = .5 * f + .5 * average(f)
    label = np.full(n, -1)
    birth, parent = [], []

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for v in np.argsort(-f, kind='stable'):
        near = both[start[v]:start[v + 1], 1]
        near = near[label[near] >= 0]
        if not len(near):
            label[v] = len(birth)
            birth.append(f[v])
            parent.append(len(parent))
            continue
        groups = {find(label[u]) for u in near}
        best = max(groups, key=lambda c: (birth[c], -c))
        for c in groups:
            if c != best and birth[c] - f[v] < SPIKE_MERGE:
                parent[c] = best
        label[v] = find(label[near[f[near].argmax()]])
    label = np.array([find(a) for a in label])
    ok = np.ones(n, bool)
    np.logical_and.at(ok, inverse, eligible)
    order = np.argsort(label, kind='stable')
    bounds = np.flatnonzero(np.diff(label[order])) + 1
    export_order = np.argsort(inverse, kind='stable')
    export_start = np.searchsorted(inverse[export_order], np.arange(n + 1))
    out = []
    for ids in np.split(order, bounds):
        if len(ids) < 6 or not ok[ids].all():
            continue
        base_level = np.percentile(f[ids], 25)
        if f[ids].max() - np.percentile(f[ids], 10) < SPIKE_PROMINENCE or np.ptp(p[ids], axis=0).max() > SPIKE_EXTENT:
            continue
        expand = lambda sel: np.concatenate([export_order[export_start[i]:export_start[i + 1]] for i in sel])
        out.append((expand(ids), expand(ids[f[ids] <= base_level]), int(expand([ids[f[ids].argmax()]])[0])))
    return out


HEAD_PLANE = -.0115


def jaw_mask(geo):
    """The source `_JAW` mask, made the same on both sides of the head.

    The mask is 0 or 1 except on the ramp at each corner of the mouth, and the
    two ramps differ by up to 0.04. A ramp vertex with a mirror twin within
    MIRROR_REACH takes the mean of the two values.
    """
    jaw = geo['_JAW'].astype(float).copy()
    p = geo['POSITION'].astype(float)
    head = np.flatnonzero((p[:, 2] < -3.0) & (p[:, 1] > 4.0) & (abs(p[:, 0]) < 1.5))
    out = jaw.copy()
    # The two lips share positions at the corner. Keep each lip with its own side of 0.5.
    for ids in [head[jaw[head] <= .5], head[jaw[head] >= .5]]:
        if not len(ids):
            continue
        tree = KDTree(len(ids))
        for k, i in enumerate(ids):
            tree.insert(p[i], k)
        tree.balance()
        for i in ids:
            q = p[i] * np.array([-1., 1, 1]) + np.array([2 * HEAD_PLANE, 0, 0])
            twins = [jaw[ids[k]] for _, k, d in tree.find_range(q, MIRROR_REACH)]
            if twins and (jaw[i] not in (0., 1.) or min(twins) != max(twins) or twins[0] != jaw[i]):
                out[i] = .5 * (jaw[i] + np.mean(twins))
    return out.astype(np.float32)


def hide_overrides(geo, eyes):
    """Vertices whose weights come from the jaw mask and the lids, not from the field."""
    mask = geo['_JAW'] > 0
    for e in eyes.values():
        mask[np.concatenate(e['rings'])] = True
        mask[e['socket']] = True
    return mask


CLAW_GRIP = .06


def claw_owners(geo, bones, names):
    """Each horn piece and the one bone it follows: (export vertex ids, bone name)."""
    up, ut, ui = unique_mesh(geo)
    out = []
    for comp in components(up, ut):
        c = up[comp].mean(0)
        side = 'r' if c[0] > 0 else 'l'
        # The small thumb must use the wrist. Nearest-bone selection could assign its curved tip to a finger.
        if 3.0 < abs(c[0]) < 4.2 and c[1] > 5.45 and c[2] < -.55:
            owner = 'wing_wrist_' + side
        elif c[1] > 4.8 and abs(c[0]) < 1.5:
            owner = 'head'
        else:
            prefix = 'wing' if c[1] >= .6 else 'toe_fore_' if c[2] < -1 else 'toe_hind_'
            candidates = [n for n in names if n.startswith(prefix) and n.endswith('_' + side)]
            owner = min(candidates, key=lambda n: segment_distance(c[None], np.array(bones[n][1]), np.array(bones[n][2]))[0])
        if owner.startswith('wing_spur_b'):
            owner = owner.replace('spur_b', 'spur_a')
        out.append((np.flatnonzero(np.isin(ui, comp)), owner))
    return out


def make_weights(g, spec):
    bones = {b[0]: b for b in spec['bones']}
    names = list(bones)
    eyes = eye_measure(g, g['hide'])
    result = {}
    membrane_dense = None
    for mesh_name, geo in sorted(g.items(), key=lambda item: item[0] != 'hide'):
        p = geo['POSITION'].astype(float)
        w = np.zeros((len(p), len(names)), np.float32)
        if mesh_name.startswith('eye_'):
            w[:, names.index(mesh_name)] = 1
        elif mesh_name == 'mouth':
            jaw = jaw_mask(geo)
            w[:, names.index('head')] = 1 - jaw
            w[:, names.index('jaw')] = jaw
            up, ut, ui = unique_mesh(geo)
            for c in components(up, ut):
                ids = np.flatnonzero(np.isin(ui, c))
                if len(c) == 106:
                    tw = field(p[ids], bones, ['tongue_01', 'tongue_02', 'tongue_03'], .045)
                    w[ids] = tw
                elif len(c) == 25:
                    # A tooth is one rigid component, including its root.
                    lower = float(jaw[ids].mean()) > .5
                    w[ids] = 0
                    w[ids, names.index('jaw' if lower else 'head')] = 1
        else:
            w, limb, wing, body_part, wing_part = smooth_field(p, spec, parts=True)
            if mesh_name == 'membrane':
                w = membrane_dense
            if mesh_name == 'hide':
                membrane_dense = np.zeros((len(g['membrane']['POSITION']), len(names)), np.float32)
                # Hide vertices within CLAW_GRIP of a wing claw are pinned to the claw's bone.
                pins = np.full(len(p), -1)
                tree = KDTree(len(p))
                for i, co in enumerate(p):
                    tree.insert(co, i)
                tree.balance()
                for ids, owner in claw_owners(g['horn'], bones, names):
                    if owner.startswith('wing'):
                        for co in g['horn']['POSITION'][ids]:
                            pins[[i for _, i, _ in tree.find_range(co, CLAW_GRIP)]] = names.index(owner)
                wings = [Wing(geo, g['membrane'], body_part, wing_part, wing * (p[:, 0] * sign > 0), pins, bones, names, side, sign)
                         for side, sign in [('l', -1), ('r', 1)]]
                # Mirror, then relax what the test fold breaks. The relaxing is local, so a few turns settle.
                for turn in range(MIRROR_TURNS):
                    change = mirror_wings(*wings, names)
                    folds = [one.fold() for one in wings]
                    print('WINGS turn', turn, 'mirror change %.4f;' % change, 'fold rounds, longest edge, cuts:', [(a, round(b, 3), c) for a, b, c in folds], flush=True)
                for one in wings:
                    hid, hw, mid, mw = one.result()
                    w[hid] = hw
                    membrane_dense[mid] = mw
                centre = np.flatnonzero(membrane_dense.sum(1) == 0)
                membrane_dense[centre] = smooth_field(g['membrane']['POSITION'][centre].astype(float), spec)
                fixed = hide_overrides(geo, eyes)
                # Each spike follows the one bone that leads at its base.
                spikes = spike_segments(geo, (limb == 0) & (wing == 0) & ~fixed)
                for ids, base, _ in spikes:
                    owner = int(w[base].sum(0).argmax())
                    w[ids] = 0
                    w[ids, owner] = 1
                print('SPIKES', len(spikes), 'segments;', sum(len(s[0]) for s in spikes), 'vertices', flush=True)
                jaw = jaw_mask(geo)
                ids = np.flatnonzero(jaw > 0)
                w[ids] = 0
                w[ids, names.index('head')] = 1 - jaw[ids]
                w[ids, names.index('jaw')] = jaw[ids]
                for side, e in eyes.items():
                    centre = e['centre']
                    for ring, strength in zip(e['rings'], [1., .97, .84, .58, .25, .035]):
                        upper = (p[ring] - centre) @ e['up'] >= 0
                        w[ring] = 0
                        w[ring, names.index('head')] = 1 - strength
                        for sel, word in [(upper, 'upper'), (~upper, 'lower')]:
                            w[ring[sel], names.index('eyelid_' + word + '_' + side)] = strength
                    w[e['socket']] = 0
                    w[e['socket'], names.index('head')] = 1
            if mesh_name == 'horn':
                for ids, owner in claw_owners(geo, bones, names):
                    w[ids] = 0
                    w[ids, names.index(owner)] = 1
        result[mesh_name] = compact_weights(w)
        print('WEIGHTS', mesh_name, len(p), flush=True)
    return result


def append_accessor(doc, binary, array, kind, component):
    array = np.ascontiguousarray(array, dtype=DTYPES[component])
    binary.extend(b'\x00' * (-len(binary) % 4))
    vi = len(doc['bufferViews'])
    doc['bufferViews'].append(dict(buffer=0, byteOffset=len(binary), byteLength=array.nbytes))
    binary.extend(array.tobytes())
    ai = len(doc['accessors'])
    doc['accessors'].append(dict(bufferView=vi, componentType=component, count=len(array), type=kind))
    return ai


def build():
    doc, source_binary = read_glb(SOURCE)
    geo = geometry(doc, source_binary)
    spec = json.loads(SPEC.read_text())
    weights = make_weights(geo, spec)
    binary = bytearray(source_binary)
    names = [b[0] for b in spec['bones']]
    start = len(doc['nodes'])
    node_ids = {n: start + i for i, n in enumerate(names)}
    bone_map = {b[0]: b for b in spec['bones']}
    inverse = []
    for name, a, tail, parent in spec['bones']:
        local = np.array(a) - (np.array(bone_map[parent][1]) if parent else 0)
        node = dict(name=name, translation=local.tolist(), extras={'tail': tail})
        rotation = np.eye(3)
        if name.startswith('eyelid_'):
            eye = spec['eyes'][name[-1]]
            rotation = np.array([eye['long'], eye['up'], eye['normal']]).T
            q = Matrix(rotation.tolist()).to_quaternion()
            node['rotation'] = [q.x, q.y, q.z, q.w]
        children = [node_ids[b[0]] for b in spec['bones'] if b[3] == name]
        if children:
            node['children'] = children
        doc['nodes'].append(node)
        m = np.eye(4)
        m[:3, :3] = rotation
        m[:3, 3] = a
        inverse.append(np.linalg.inv(m).astype('<f4').T.flatten())
    ai = append_accessor(doc, binary, np.array(inverse), 'MAT4', 5126)
    doc['skins'] = [dict(name='Embercrest hero', joints=list(node_ids.values()),
                         skeleton=node_ids['root'], inverseBindMatrices=ai)]
    for node in doc['nodes'][:start]:
        if 'mesh' in node:
            node['skin'] = 0
    doc['scenes'][doc.get('scene', 0)]['nodes'].append(node_ids['root'])
    for mesh in doc['meshes']:
        j, w = weights[mesh['name']]
        attrs = mesh['primitives'][0]['attributes']
        attrs['JOINTS_0'] = append_accessor(doc, binary, j, 'VEC4', 5123)
        attrs['WEIGHTS_0'] = append_accessor(doc, binary, w, 'VEC4', 5126)
    doc['buffers'][0]['byteLength'] = len(binary)
    text = json.dumps(doc, separators=(',', ':')).encode()
    text += b' ' * (-len(text) % 4)
    binary.extend(b'\x00' * (-len(binary) % 4))
    data = struct.pack('<4sII', b'glTF', 2, 28 + len(text) + len(binary))
    data += struct.pack('<I4s', len(text), b'JSON') + text
    data += struct.pack('<I4s', len(binary), b'BIN\x00') + binary
    OUTPUT.write_bytes(data)
    for suffix in ['rig.cfg', 'flight.cfg']:
        # The sidecars start as Embercrest's. Later edits to them survive a rebuild.
        if not Path(str(OUTPUT) + '.' + suffix).exists():
            shutil.copyfile(ROOT / ('assets/embercrest.glb.' + suffix), Path(str(OUTPUT) + '.' + suffix))
    print('EXPORTED', OUTPUT.name, len(data), 'bytes', len(names), 'joints', flush=True)


def load_exported_scene():
    """Create Blender evaluation objects from the exported skin and accessors."""
    doc, binary = read_glb(OUTPUT)
    geo = geometry(doc, binary)
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.gltf(filepath=str(OUTPUT), merge_vertices=False)
    imported_armatures = sum(o.type == 'ARMATURE' for o in bpy.context.scene.objects)
    materials = {}
    for o in bpy.context.scene.objects:
        if o.type == 'MESH':
            materials[o.name] = list(o.data.materials)
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    skin = doc['skins'][0]
    nodes = [doc['nodes'][i] for i in skin['joints']]
    inverse = accessor(doc, binary, skin['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)
    binds = np.linalg.inv(inverse)
    arm = bpy.data.armatures.new('Hero skeleton')
    rig = bpy.data.objects.new('Hero armature', arm)
    bpy.context.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    parents = {child: i for i, node in enumerate(doc['nodes']) for child in node.get('children', [])}
    for i, (ni, node) in enumerate(zip(skin['joints'], nodes)):
        bone = arm.edit_bones.new(node['name'])
        conversion = np.array([[1,0,0,0],[0,0,-1,0],[0,1,0,0],[0,0,0,1]])
        bone.head = blender_point(binds[i, :3, 3])
        bone.tail = bone.head + Vector((0, 0, .1))
        bone.matrix = Matrix((conversion @ binds[i]).tolist())
        pi = parents.get(ni)
        if pi in skin['joints']:
            bone.parent = arm.edit_bones[doc['nodes'][pi]['name']]
    bpy.ops.object.mode_set(mode='OBJECT')
    rig.show_in_front = True
    objects = {}
    for name, a in geo.items():
        mesh = bpy.data.meshes.new(name)
        bp = a['POSITION'][:, [0, 2, 1]] * np.array([1, -1, 1])
        mesh.from_pydata(bp.tolist(), [], a['tri'].tolist())
        mesh.update()
        obj = bpy.data.objects.new(name, mesh)
        bpy.context.collection.objects.link(obj)
        for mat in materials[name]:
            mesh.materials.append(mat)
        uv = mesh.uv_layers.new(name='UVMap')
        loop_ids = np.empty(len(mesh.loops), np.int32)
        mesh.loops.foreach_get('vertex_index', loop_ids)
        texcoord = a['TEXCOORD_0'][loop_ids].copy()
        texcoord[:, 1] = 1 - texcoord[:, 1]
        uv.data.foreach_set('uv', texcoord.ravel())
        for face in mesh.polygons:
            face.use_smooth = True
        normals = a['NORMAL'][:, [0, 2, 1]] * np.array([1, -1, 1])
        mesh.normals_split_custom_set_from_vertices(normals.tolist())
        for node in nodes:
            obj.vertex_groups.new(name=node['name'])
        for v, (joints, weights) in enumerate(zip(a['JOINTS_0'], a['WEIGHTS_0'])):
            for j, w in zip(joints, weights):
                if w > 0:
                    obj.vertex_groups[int(j)].add([v], float(w), 'REPLACE')
        mod = obj.modifiers.new('Exported linear skin', 'ARMATURE')
        mod.object = rig
        mod.use_deform_preserve_volume = False
        objects[name] = obj
    bpy.context.view_layer.update()
    return doc, binary, geo, rig, objects, imported_armatures


def reset_pose(rig):
    for bone in rig.pose.bones:
        bone.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()


def rotate(rig, name, axis, degrees):
    pb = rig.pose.bones[name]
    pb.rotation_mode = 'QUATERNION'
    pb.rotation_quaternion = Quaternion(Vector(axis), math.radians(degrees))


def wing_hinge(side):
    """The fold axis: the body's up axis. Both hinges turn about it, in opposite senses.

    A hinge normal to the wing's own plane closes the wedge between the forearm
    and finger 3 by 80 degrees, and that wedge is 70 degrees wide: the finger
    would pass through the forearm. About the up axis the same wedge closes
    from 98 to 18 degrees in plan and nothing has to cross.
    """
    return (0., 1., 0.)


def set_pose(rig, label, eyes):
    reset_pose(rig)
    if label == 'jaw':
        rotate(rig, 'jaw', (1, 0, 0), -35)
    elif label == 'blink':
        for side, eye in eyes.items():
            for half in ['upper', 'lower']:
                bone = rig.pose.bones['eyelid_' + half + '_' + side]
                bone.scale = (1, .001, .001)
                bone.location = (0, -.0008 if half == 'upper' else .0008, .164)
    elif label == 'eyes':
        for side in ['l', 'r']:
            rotate(rig, 'eye_' + side, (0, 1, 0), 25)
    elif label == 'wingfold':
        # A Z fold: the forearm swings forward at the elbow, the hand swings back at the wrist.
        for side, sign in [('l', -1), ('r', 1)]:
            axis = wing_hinge(side)
            rotate(rig, 'wing_arm_' + side, axis, sign * FOLD_DEGREES)
            rotate(rig, 'wing_wrist_' + side, axis, -sign * FOLD_DEGREES)
    elif label == 'crouch':
        for side in ['l', 'r']:
            rotate(rig, 'forearm_' + side, (1, 0, 0), 60)
            # Mesh version 4 binds the knee bent 75 degrees. The check opens it by 60, which the leg can do.
            # Closing it 60 more (the -60 that suited version 2's straight leg) drives the shin into the
            # thigh and loses 17% of the volume.
            rotate(rig, 'shin_' + side, (1, 0, 0), 60)
    elif label == 'neck':
        rotate(rig, 'neck_02', (1, 0, 0), 30)
    bpy.context.view_layer.update()


def evaluated(objects):
    depsgraph = bpy.context.evaluated_depsgraph_get()
    out = {}
    for name, obj in objects.items():
        ev = obj.evaluated_get(depsgraph)
        mesh = ev.to_mesh()
        a = np.empty((len(mesh.vertices), 3), np.float32)
        mesh.vertices.foreach_get('co', a.ravel())
        out[name] = gltf_points(a)
        ev.to_mesh_clear()
    return out


MIRROR_REACH = .002
MEMBRANE_REACH = .004


def dense_weights(g, count):
    w = np.zeros((len(g['POSITION']), count), np.float32)
    rows = np.arange(len(w))[:, None]
    np.add.at(w, (rows, g['JOINTS_0']), g['WEIGHTS_0'])
    return w


def closed_volume(p, tri, rest):
    """Weld UV seams and cap each boundary loop separately."""
    _, first, inverse = np.unique(np.round(rest, 5), axis=0, return_index=True, return_inverse=True)
    q = p[first].astype(float)
    tri = inverse[tri]
    edges = np.concatenate([tri[:, [0, 1]], tri[:, [1, 2]], tri[:, [2, 0]]])
    _, inv, counts = np.unique(np.sort(edges, axis=1), axis=0, return_inverse=True, return_counts=True)
    boundary = edges[counts[inv] == 1]
    adjacency = {}
    for a, b in boundary:
        adjacency.setdefault(a, set()).add(b)
        adjacency.setdefault(b, set()).add(a)
    unseen = set(adjacency)
    loops = []
    while unseen:
        stack = [unseen.pop()]
        loop = set(stack)
        while stack:
            for b in adjacency[stack.pop()]:
                if b in unseen:
                    unseen.remove(b)
                    loop.add(b)
                    stack.append(b)
        loops.append(loop)
    q -= q[np.unique(tri)].mean(0)
    value = np.einsum('ij,ij->i', q[tri[:, 0]], np.cross(q[tri[:, 1]], q[tri[:, 2]])).sum() / 6
    for loop in loops:
        edge = boundary[np.isin(boundary[:, 0], list(loop))]
        centre = q[list(loop)].mean(0)
        value += np.einsum('ij,ij->i', q[edge[:, 1]], np.cross(q[edge[:, 0]], centre)).sum() / 6
    valid = not np.any(counts > 2)
    return abs(float(value)), valid, [len(s) for s in loops]


# The old gates did not measure the flank seam or require a wrist owner for the thumb.
# A nearby scale can face an interior point. Lateral crossings distinguish enclosure from a real seam gap.
def lateral_seam_gap(tree, point):
    side=1 if point[0]>0 else -1
    origin=Vector((side*1.6,float(point[1]),float(point[2])))
    direction=Vector((-side,0,0));inside=0
    for _ in range(32):
        hit=tree.ray_cast(origin,direction,3.2)
        if hit[0] is None:
            return 0.0 if inside else float('inf')
        if hit[0].x*side<=abs(point[0]):
            return 0.0 if inside else abs(point[0])-hit[0].x*side
        inside=max(0,inside+(1 if hit[1].x*side>0 else -1))
        origin=hit[0]+direction*.00001
    raise RuntimeError('The flank ray crossed too many surfaces. Inspect the attachment.')


def verify_wing_repairs(geo, rest, posed, names, check, label):
    tree=BVHTree.FromPolygons(posed['hide'].tolist(),geo['hide']['tri'].tolist(),all_triangles=True)
    m=geo['membrane']; sel=m['_SHEET_EDGE']>.5
    edges=np.unique(np.sort(np.concatenate([m['tri'][:,[0,1]],m['tri'][:,[1,2]],m['tri'][:,[2,0]]]),axis=1),axis=0)
    edges=edges[np.all(sel[edges],axis=1)]
    p=posed['membrane']
    samples=np.concatenate([p[sel]]+[p[edges[:,0]]*(1-t)+p[edges[:,1]]*t for t in np.linspace(.1,.9,9)])
    gap=max(lateral_seam_gap(tree,q) for q in samples)
    check(gap<.001,'root seam '+label,f'maximum lateral gap {gap*1000:.3f} mm across {len(samples)} samples')
    horn=geo['horn'];up,ut,ui=unique_mesh(horn)
    weights=dense_weights(horn,len(names))
    for side,sign in [('l',-1),('r',1)]:
        comps=[c for c in components(up,ut) if len(c)>10 and 3<up[c,0].mean()*sign<4.5 and up[c,1].mean()>5.2]
        check(len(comps)==1,'wrist_'+side+' count '+label,f'{len(comps)} connected claws')
        if len(comps)!=1:continue
        ids=np.flatnonzero(np.isin(ui,comps[0]));q=rest['horn'][ids]
        owner=names.index('wing_wrist_'+side)
        check(np.all(weights[ids,owner]==1),'wrist_'+side+' owner '+label,'all thumb vertices follow wing_wrist_'+side)
        a,b=int(q[:,0].argmin()),int(q[:,0].argmax());base=(q[a]+q[b])/2
        tip=int(np.linalg.norm(q-base,axis=1).argmax());length=float(np.linalg.norm(q[tip]-base))
        pq=posed['horn'][ids];pbase=(pq[a]+pq[b])/2;plength=float(np.linalg.norm(pq[tip]-pbase))
        check(abs(plength-length)<1e-5,'wrist_'+side+' rigid length '+label,f'{length:.4f} m rest, {plength:.4f} m posed')
        ring=np.abs(np.linalg.norm(q-base,axis=1)-np.ptp(q[:,0])/2)<1e-5
        # Measure cover along the posed wrist's forward direction, rather than a sign on an open shell.
        rotation=np.linalg.lstsq(q-q.mean(0),pq-pq.mean(0),rcond=None)[0]
        forward=np.array([0,0,-1.])@rotation
        forward/=np.linalg.norm(forward)
        gaps=[]
        for x in pq[ring]:
            hit=tree.ray_cast(Vector(x+.3*forward),Vector(-forward),.6)
            gaps.append(max(0,float((Vector(x)-hit[0]).dot(Vector(forward)))) if hit[0] is not None else float('inf'))
        gap=max(gaps)
        check(gap<.001,'wrist_'+side+' seated '+label,f'maximum base gap {gap*1000:.3f} mm')


def verify(doc, binary, geo, rig, objects, imported_armatures):
    checks = []
    def check(ok, name, detail):
        row = dict(pass_=bool(ok), name=name, detail=detail)
        checks.append(row)
        print(('PASS ' if ok else 'FAIL ') + name + ': ' + detail, flush=True)
    source_doc, source_bin = read_glb(SOURCE)
    source_geo = geometry(source_doc, source_bin)
    spec = json.loads(SPEC.read_text())
    skin = doc['skins'][0]
    names = [doc['nodes'][i]['name'] for i in skin['joints']]
    count = len(names)
    expected = [b[0] for b in json.loads((ROOT / 'tools/skeletons/winged-quadruped.json').read_text())['bones']]
    added = ['eye_' + s for s in ['l', 'r']] + ['eyelid_' + h + '_' + s for h in ['upper', 'lower'] for s in ['l', 'r']]
    added += ['tongue_01', 'tongue_02', 'tongue_03']
    check(set(expected + added) <= set(names) and len(names) == len(set(names)), 'bone names', f'{count} unique joints; all original and requested names exist')
    check(count <= 256 and len(doc['skins']) == 1 and imported_armatures == 1, 'armature contract', f'{count} joints, {len(doc["skins"])} skin, {imported_armatures} imported armature')
    skips = ['_end', 'ik', 'pole', 'cont', 'target', 'chain', 'roll', 'fly', 'muscle', 'kneecap']
    families = ['neck', 'tail', 'wing', 'shoulder', 'head', 'upper_arm', 'foreleg', 'thigh', 'hip', 'femur', 'hand', 'food', 'foot', 'fuss', 'paw', 'chest', 'spine', 'torso']
    bad = [n for n in added if any(s in n for s in skips + families)]
    check(not bad, 'new names avoid mapper aliases', str(bad))
    check(set(geo) == {'hide', 'membrane', 'horn', 'eye_l', 'eye_r', 'mouth'}, 'six meshes', ', '.join(geo))
    check(doc['materials'] == source_doc['materials'] and doc['images'] == source_doc['images'] and doc['textures'] == source_doc['textures'], 'material records unchanged', 'materials, image records, and texture records compared')
    check(binary[:len(source_bin)] == source_bin, 'source buffer unchanged', f'{len(source_bin)} original binary bytes compared, including every embedded image')
    for suffix in ['rig.cfg', 'flight.cfg']:
        def keys(path):
            rows = [line.split() for line in path.read_text().splitlines() if line.strip() and not line.startswith('#')]
            return {r[0]: r[1] for r in rows}
        ours = keys(Path(str(OUTPUT) + '.' + suffix))
        base = keys(ROOT / ('assets/embercrest.glb.' + suffix))
        changed = sorted(k for k in ours if base.get(k) != ours[k])
        check(set(base) <= set(ours), suffix + ' present', f'every Embercrest key kept; {len(changed)} values differ: {", ".join(changed) or "none"}')
    inv = accessor(doc, binary, skin['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)
    parents = {c: i for i, n in enumerate(doc['nodes']) for c in n.get('children', [])}
    world = {}
    def node_world(i):
        if i not in world:
            node = doc['nodes'][i]
            m = np.eye(4)
            m[:3, 3] = node.get('translation', [0, 0, 0])
            if 'rotation' in node:
                x, y, z, w = node['rotation']
                m[:3, :3] = np.array(Quaternion((w, x, y, z)).to_matrix())
            world[i] = node_world(parents[i]) @ m if i in parents else m
        return world[i]
    error = max(np.abs(node_world(ni) @ inv[j] - np.eye(4)).max() for j, ni in enumerate(skin['joints']))
    check(error < 1e-5, 'inverse binds reconcile', f'maximum matrix error {error:.9g}')
    nodes_by_name = {doc['nodes'][i]['name']: i for i in skin['joints']}
    fit_error = max(np.linalg.norm(node_world(nodes_by_name[b[0]])[:3, 3] - b[1]) for b in spec['bones'])
    parent_errors = [b[0] for b in spec['bones'] if b[3] and parents.get(nodes_by_name[b[0]]) != nodes_by_name[b[3]]]
    check(fit_error < 1e-5 and not parent_errors, 'measured skeleton hierarchy', f'maximum anchor error {fit_error:.9g} m; wrong parents {parent_errors}')
    for mesh, a in geo.items():
        prim = next(m for m in doc['meshes'] if m['name'] == mesh)['primitives'][0]
        same = all(np.array_equal(a[k], source_geo[mesh][k]) for k in source_geo[mesh])
        check(same and prim.get('mode', 4) == 4 and 'TANGENT' in a, mesh + ' source geometry', f'{len(a["tri"])} triangles; all source attributes and indices compared')
        w, j = a['WEIGHTS_0'], a['JOINTS_0']
        error = float(abs(w.sum(1) - 1).max())
        check(np.isfinite(w).all() and (w >= 0).all() and error < 1e-6 and j.max() < count and (w > 0).sum(1).max() <= 4,
              mesh + ' influences', f'max sum error {error:.9g}; unweighted {int((~(w.sum(1) > 0)).sum())}; maximum {int((w>0).sum(1).max())}')
    hw = dense_weights(geo['hide'], count)
    jaw_error = float(abs(hw[:, names.index('jaw')] - jaw_mask(geo['hide'])).max())
    jaw_shift = abs(hw[:, names.index('jaw')] - geo['hide']['_JAW'])
    check(jaw_error < 1e-6 and jaw_shift.max() < .03, 'hide jaw mask retained',
          f'the jaw weight is the source mask, with the two corner ramps averaged; {int((jaw_shift > 1e-6).sum())} vertices differ from the source, by at most {jaw_shift.max():.6f}')
    for mesh in ['horn', 'mouth']:
        a = geo[mesh]
        up, ut, ui = unique_mesh(a)
        weights = dense_weights(a, count)
        checked, wrong = 0, []
        for comp in components(up, ut):
            ids = np.flatnonzero(np.isin(ui, comp))
            if mesh == 'mouth' and len(comp) not in [25, 106]:
                continue
            checked += 1
            centroid = up[comp].mean(0)
            if mesh == 'mouth' and len(comp) == 106:
                allowed = [names.index('tongue_0' + str(i)) for i in [1,2,3]]
                ok = np.all(abs(weights[ids][:, allowed].sum(1) - 1) < 1e-6)
            else:
                owned = weights[ids].argmax(1)
                ok = len(set(owned)) == 1 and np.all(weights[ids, owned] == 1)
                owner = names[int(owned[0])]
                if mesh == 'horn':
                    if centroid[1] < .6:
                        ok &= owner.startswith('toe_')
                    elif centroid[1] > 4.8 and abs(centroid[0]) < 1.5:
                        ok &= owner == 'head'
                    else:
                        ok &= owner.startswith('wing_')
                else:
                    expected_owner = 'jaw' if a['_JAW'][ids].mean() > .5 else 'head'
                    ok &= owner == expected_owner
            if not ok:
                wrong.append(centroid.tolist())
        check(not wrong, mesh + ' component ownership', f'{checked} components checked; incorrect components {wrong}')
    eyes = eye_measure(geo, geo['hide'])
    for side, eye in eyes.items():
        a = geo['eye_' + side]
        w = dense_weights(a, count)
        check(np.all(w[:, names.index('eye_' + side)] == 1), 'eye_' + side + ' rigid', f'{len(w)} vertices at weight 1')
        check(len(eye['unique_rings']) == 6 and all(len(r) == 36 for r in eye['unique_rings']), 'lid_' + side + ' topology', 'six rings of 36 unique positions')
        w = dense_weights(geo['hide'], count)
        check(np.all(w[eye['socket'], names.index('head')] == 1), 'socket_' + side + ' fixed', f'{len(eye["socket"])} exported vertices follow head')
    # Mirror, part 1: the exported weights against the rig's field at the mirrored position.
    mirror = np.array([names.index(n[:-2] + ('_r' if n.endswith('_l') else '_l')) if n.endswith(('_l', '_r')) else i for i, n in enumerate(names)])
    assert names == [b[0] for b in spec['bones']]
    for mesh in ['hide']:
        a = geo[mesh]
        p = a['POSITION'].astype(float)
        exported = dense_weights(a, count)
        j, v = compact_weights(smooth_field(p, spec))
        own = dense_weights(dict(POSITION=p, JOINTS_0=j, WEIGHTS_0=v), count)
        plain = abs(exported - own).max(1) < 1e-6
        j, v = compact_weights(smooth_field(p * np.array([-1., 1, 1]), spec))
        other = dense_weights(dict(POSITION=p, JOINTS_0=j, WEIGHTS_0=v), count)[:, mirror]
        error = abs(exported - other).max(1)[plain]
        # The share that carries the field is a floor on what this check covers. On mesh version 4 the
        # head and neck spikes hold more of the hide's vertices and the share is 49.8%, so the floor is .45.
        check(error.max() <= .02 and plain.mean() > .45, mesh + ' mirrored field',
              f'exported weight against the field at the mirrored position; max {error.max():.6f}; RMS {np.sqrt(np.mean(error**2)):.6f}; {int(plain.sum())} of {len(p)} vertices carry the field, the rest are spikes, jaw and lids')
    # The membrane's weights come from its own triangles. Compare each left vertex with the
    # exported weights at the closest point of the right membrane, mirrored.
    a = geo['membrane']
    p = a['POSITION'].astype(float)
    w = dense_weights(a, count).astype(float)
    tree = BVHTree.FromPolygons(p.tolist(), a['tri'].tolist(), all_triangles=True)
    errors, far, where = [], 0, []
    for i in np.flatnonzero(p[:, 0] < -.04):
        q = p[i] * np.array([-1., 1, 1])
        location, _, face, distance = tree.find_nearest(Vector(q))
        if distance > MEMBRANE_REACH:
            far += 1
            continue
        t = a['tri'][face]
        b = np.linalg.lstsq(np.c_[p[t], np.ones(3)].T @ np.eye(3), np.r_[np.array(location), 1], rcond=None)[0]
        errors.append(abs(w[i] - (b @ w[t])[mirror]).max())
        where.append(p[i].round(3).tolist() + [round(float(distance), 4)])
    errors = np.array(errors)
    check(errors.max() <= .02 and len(errors) >= 300, 'membrane mirrored surface',
          f'left vertex against the right membrane at the mirrored point; max {errors.max():.6f}; RMS {np.sqrt(np.mean(errors**2)):.6f}; {len(errors)} vertices within {MEMBRANE_REACH*1000:.0f} mm of the mirrored surface; {far} farther')
    checks[-1]['over'] = int((errors > .02).sum())
    checks[-1]['worst'] = [where[i] + [round(float(errors[i]), 4)] for i in np.argsort(-errors)[:25]]
    # Mirror, part 2: exported weights where the two sides of the mesh coincide.
    hide_points = geo['hide']['POSITION'].astype(float)
    _, limb, wing = smooth_field(hide_points, spec, parts=True)[:3]
    spikes = spike_segments(geo['hide'], (limb == 0) & (wing == 0) & ~hide_overrides(geo['hide'], eyes))
    spike_ids = np.concatenate([s[0] for s in spikes])
    joint_parent = {names.index(b[0]): names.index(b[3]) for b in spec['bones'] if b[3]}
    for mesh in ['hide', 'membrane', 'horn', 'mouth']:
        a = geo[mesh]
        p, _, ui = unique_mesh(a)
        first = np.unique(ui, return_index=True)[1]
        w = dense_weights(a, count)[first]
        lower = (a['_JAW'][first] > .5) if '_JAW' in a else np.zeros(len(p), bool)
        rigid = (w.max(1) == 1) & np.isin(first, spike_ids) if mesh == 'hide' else np.zeros(len(p), bool)
        owners = [0, 0, 0]
        # In the mouth a tooth is rigid and the gum beside it is not. Compare like with like.
        part = np.zeros(len(p), np.int32)
        mixed = 0
        if mesh == 'mouth':
            for comp in components(p, unique_mesh(a)[1]):
                part[comp] = len(comp) if len(comp) in (25, 106) else 0
        # The head sculpt mirrors about X = -0.0115 and the body about X = 0.
        head_side = (p[:, 2] < -3.0) & (p[:, 1] > 4.0) & (abs(p[:, 0]) < 1.5)
        errors, distances, far = [], [], 0
        worst = None
        for cls in [False, True]:
            ids = np.flatnonzero(lower == cls)
            if not len(ids):
                continue
            tree = KDTree(len(ids))
            for k, i in enumerate(ids):
                tree.insert(p[i], k)
            tree.balance()
            for i in ids[p[ids, 0] < -.04]:
                centre_x = HEAD_PLANE if head_side[i] else 0.
                q = p[i].copy()
                q[0] = 2 * centre_x - q[0]
                _, k, distance = tree.find(q)
                if distance > MIRROR_REACH:
                    far += 1
                    continue
                if part[i] != part[ids[k]]:
                    mixed += 1
                    continue
                if rigid[i] or rigid[ids[k]]:
                    # A spike follows one bone. Its mirror image must lead with that bone or one joined to it.
                    one, two = int(w[i].argmax()), int(mirror[w[ids[k]].argmax()])
                    owners[0 if one == two else 1 if joint_parent.get(one) == two or joint_parent.get(two) == one else 2] += 1
                    continue
                errors.append(float(abs(w[i] - w[ids[k], mirror]).max()))
                distances.append(distance)
                if worst is None or errors[-1] > worst[0]:
                    worst = (errors[-1], int(i), int(ids[k]), distance)
        errors = np.array(errors) if errors else np.zeros(1)
        spikes_note = f'; {sum(owners)} more pairs have a spike on one side or both: {owners[0]} lead with the same bone, {owners[1]} with joined bones, {owners[2]} with bones apart' if mesh == 'hide' else ''
        if mesh == 'mouth':
            spikes_note = f'; {mixed} more pairs set a tooth against the gum and are not compared'
        check(errors.max() <= .02 and not owners[2], mesh + ' mirrored weights',
              f'max {errors.max():.6f}; RMS {np.sqrt(np.mean(errors**2)):.6f}; {len(distances)} pairs within {MIRROR_REACH*1000:.0f} mm of their mirror image; {far} left vertices have no such partner' + spikes_note)
        if worst:
            checks[-1]['worst_pair'] = dict(left=p[worst[1]].tolist(), right=p[worst[2]].tolist(), distance_m=worst[3],
                                           left_weights={names[j]: float(v) for j, v in enumerate(w[worst[1]]) if v > 0},
                                           right_weights={names[j]: float(v) for j, v in enumerate(w[worst[2]]) if v > 0})
    reset_pose(rig)
    rest = evaluated(objects)
    err = max(float(np.linalg.norm(rest[n] - a['POSITION'], axis=1).max()) for n, a in geo.items())
    check(err < 1e-5, 'Blender bind pose', f'max vertex displacement {err:.9g} m')
    if '_SHEET_EDGE' in geo['membrane']:
        verify_wing_repairs(geo,rest,rest,names,check,'bind')
    set_pose(rig, 'jaw', eyes)
    posed = evaluated(objects)
    hp = geo['hide']['POSITION']
    hinge = node_world(nodes_by_name['jaw'])[:3, 3]
    fixed = (geo['hide']['_JAW'] == 0) & (np.linalg.norm(hp - hinge, axis=1) > .4)
    motion = np.linalg.norm(posed['hide'][fixed] - rest['hide'][fixed], axis=1).max()
    check(motion < 1e-5, 'jaw fixed skull', f'35 degrees; {fixed.sum()} distant zero-mask vertices; max motion {motion:.9g} m')
    mouth = geo['mouth']
    p, t, ui = unique_mesh(mouth)
    lower = []
    for comp in components(p, t):
        ids = np.flatnonzero(np.isin(ui, comp))
        if len(comp) == 25 and dense_weights(mouth, count)[ids, names.index('jaw')].min() > .99:
            lower.extend(ids.tolist())
    upper_tris = geo['hide']['tri'][np.all(geo['hide']['_JAW'][geo['hide']['tri']] == 0, axis=1)]
    upper_tree = BVHTree.FromPolygons(posed['hide'].tolist(), upper_tris.tolist(), all_triangles=True)
    clearance = min(upper_tree.find_nearest(Vector(posed['mouth'][i]))[3] for i in lower)
    upper_y = np.interp(-rest['mouth'][lower, 2], [3.6,3.76,3.85,3.94,4,4.12,4.27,4.365,4.43], [4.58,4.578,4.552,4.528,4.52,4.508,4.508,4.495,4.465])
    vertical = float(np.min(upper_y - posed['mouth'][lower, 1]))
    check(clearance > .001 and vertical > 0, 'lower teeth clear upper lip', f'{len(lower)} vertices; nearest fixed hide {clearance:.6f} m; minimum vertical clearance {vertical:.6f} m')
    set_pose(rig, 'blink', eyes)
    posed = evaluated(objects)
    for side, e in eyes.items():
        inner = e['rings'][0]
        co = posed['hide'][inner]
        line = co @ e['up']
        gap = float(np.ptp(line))
        check(gap <= .005, 'blink_' + side + ' closure', f'inner-edge height spread {gap:.9f} m; threshold 0.005 m')
        # Test all ball vertices and triangle centres against the closed hide.
        tree = BVHTree.FromPolygons(posed['hide'].tolist(), geo['hide']['tri'].tolist(), all_triangles=True)
        ball = posed['eye_' + side]
        samples = np.concatenate([ball, ball[geo['eye_' + side]['tri']].mean(1)])
        exposed = 0
        tested = 0
        for x in [-.364, 0, .364]:
            for y in [-.364, 0, .364]:
                direction = e['normal'] + x * e['long'] + y * e['up']
                direction /= np.linalg.norm(direction)
                front = (samples - e['centre']) @ direction > 0
                for co in samples[front]:
                    hit = tree.ray_cast(Vector(co), Vector(direction), .6)
                    exposed += hit[0] is None
                    tested += 1
        nearest = min(tree.find_nearest(Vector(co))[3] for co in samples)
        check(exposed == 0, 'blink_' + side + ' ball coverage', f'{exposed}/{tested} rays exposed across nine directions; minimum hide distance {nearest:.6f} m')
    set_pose(rig, 'eyes', eyes)
    posed = evaluated(objects)
    for side, e in eyes.items():
        ids = np.concatenate(e['rings'])
        error = float(np.linalg.norm(posed['hide'][ids] - rest['hide'][ids], axis=1).max())
        movement = float(np.linalg.norm(posed['eye_' + side] - rest['eye_' + side], axis=1).max())
        check(error < 1e-5 and movement > .03, 'eye_' + side + ' independence', f'25 degrees; max lid motion {error:.9g} m; eye motion {movement:.6f} m')
    set_pose(rig, 'wingfold', eyes)
    posed = evaluated(objects)
    if '_SHEET_EDGE' in geo['membrane']:
        verify_wing_repairs(geo,rest,posed,names,check,'fold')
    def keys(points):
        return [tuple(k) for k in np.round(points / 1e-4).astype(np.int64).tolist()]
    hide_keys = keys(rest['hide'])
    membrane_keys = keys(rest['membrane'])
    # The arm and the body: every hide triangle that no finger or spur bone leads.
    lead = dense_weights(geo['hide'], count).argmax(1)
    thin = np.array([('finger' in n) or ('spur' in n) for n in names])
    solid_tri = geo['hide']['tri'][~thin[lead[geo['hide']['tri']]].any(1)]
    for side, sign in [('l', -1), ('r', 1)]:
        mt = geo['membrane']['tri']
        mt = mt[np.all(geo['membrane']['POSITION'][mt, 0] * sign > 0, axis=1)]
        edges = np.unique(np.sort(np.concatenate([mt[:, [0, 1]], mt[:, [1, 2]], mt[:, [2, 0]]]), axis=1), axis=0)
        l0 = np.linalg.norm(rest['membrane'][edges[:, 0]] - rest['membrane'][edges[:, 1]], axis=1)
        l1 = np.linalg.norm(posed['membrane'][edges[:, 0]] - posed['membrane'][edges[:, 1]], axis=1)
        keep = l0 > 1e-3
        ratio = l1[keep] / l0[keep]
        check(ratio.max() <= 1.6, 'wing_' + side + ' membrane stretch',
              f'elbow 80 and wrist 80 degrees; {int(keep.sum())} edges; longest {ratio.max():.4f} of rest; shortest {ratio.min():.4f}; {int((ratio > 1.3).sum())} over 1.3')
        top = np.argsort(-ratio)[:12]
        ends = edges[keep][top]
        checks[-1]['longest_edges'] = [dict(ratio=round(float(ratio[i]), 3), a=rest['membrane'][e[0]].round(3).tolist(), b=rest['membrane'][e[1]].round(3).tolist()) for i, e in zip(top, ends)]
        # Membrane triangles that cut the arm or the body. A triangle with a vertex on the hide is
        # the membrane's own attachment and is not counted; its neighbours are.
        every_hide_key = set(hide_keys)
        attached = np.array([any(membrane_keys[v] in every_hide_key for v in t) for t in mt])
        def cutting(points):
            a = BVHTree.FromPolygons(points['hide'].tolist(), solid_tri.tolist(), all_triangles=True)
            b = BVHTree.FromPolygons(points['membrane'].tolist(), mt.tolist(), all_triangles=True)
            found = set()
            for i, k in a.overlap(b):
                if not attached[k]:
                    found.add(k)
            return found
        before, after = cutting(rest), cutting(posed)
        fresh = after - before
        check(not fresh, 'wing_' + side + ' membrane intersections',
              f'{len(fresh)} membrane triangles cut the arm or the body when folded and not at rest; {len(after)} cut it folded; {len(before)} cut it at rest; {int((~attached).sum())} membrane triangles tested, {int(attached.sum())} attached to the hide; {len(solid_tri)} arm and body triangles')
        checks[-1]['fresh'] = [rest['membrane'][mt[k]].mean(0).round(3).tolist() for k in sorted(fresh)][:40]
    set_pose(rig, 'crouch', eyes)
    posed = evaluated(objects)
    for side, sign in [('l', -1), ('r', 1)]:
        for fore in [True, False]:
            p = rest['hide']
            ln = limb_names(names, 'l' if sign < 0 else 'r', fore)
            selected = dense_weights(geo['hide'], count)[:, [names.index(n) for n in ln]].sum(1) > .5
            tris = geo['hide']['tri'][np.all(selected[geo['hide']['tri']], axis=1)]
            v0, valid0, loops = closed_volume(p, tris, p)
            v1, valid1, _ = closed_volume(posed['hide'], tris, p)
            delta = abs(v1 / v0 - 1)
            check(delta < .15 and valid0 and valid1, ('fore_' if fore else 'hind_') + side + ' volume', f'60-degree elbow/knee; rest {v0:.6f} m3; posed {v1:.6f} m3; change {delta*100:.4f}%; simple boundary loops {loops}; valid {valid0 and valid1}')
    set_pose(rig, 'neck', eyes)
    posed = evaluated(objects)
    p = rest['hide'].astype(float)
    hw = dense_weights(geo['hide'], count)
    frill = [s for s in spikes if rest['hide'][s[2], 1] > 2.9 and rest['hide'][s[2], 2] < -1.5]
    rigid = [s for s in spikes if len(set(hw[s[0]].argmax(1))) == 1 and np.all(hw[s[0]].max(1) == 1)]
    check(len(rigid) == len(spikes), 'spike rigid ownership', f'{len(rigid)} of {len(spikes)} spikes and plates follow one bone at weight 1; {len(frill)} of them are on the head and neck')
    worst, longest, moved = 0., 0., 0
    owners = {}
    for ids, base, tip in frill:
        owner = names[int(hw[tip].argmax())]
        owners[owner] = owners.get(owner, 0) + 1
        l0 = np.linalg.norm(rest['hide'][tip] - rest['hide'][base].mean(0))
        l1 = np.linalg.norm(posed['hide'][tip] - posed['hide'][base].mean(0))
        worst = max(worst, abs(l1 / l0 - 1))
        longest = max(longest, l0)
        moved += np.linalg.norm(posed['hide'][tip] - rest['hide'][tip]) > .01
    tris = geo['hide']['tri']
    inside = np.zeros(len(p), np.int32)
    for k, s in enumerate(frill):
        inside[s[0]] = k + 1
    tris = tris[(inside[tris[:, 0]] > 0) & (inside[tris[:, 0]] == inside[tris[:, 1]]) & (inside[tris[:, 0]] == inside[tris[:, 2]])]
    edges = np.unique(np.sort(np.concatenate([tris[:, [0,1]],tris[:, [1,2]],tris[:, [2,0]]]), axis=1), axis=0)
    l0 = np.linalg.norm(rest['hide'][edges[:, 0]] - rest['hide'][edges[:, 1]], axis=1)
    l1 = np.linalg.norm(posed['hide'][edges[:, 0]] - posed['hide'][edges[:, 1]], axis=1)
    keep = l0 > 1e-3
    delta = abs(l1[keep] / l0[keep] - 1).max()
    check(worst < .01 and delta < .01 and moved > 0, 'frill spike lengths',
          f'neck_02 bent 30 degrees; {len(frill)} spikes, {moved} of them moved; base-to-tip length change at most {worst*100:.4f}%; longest spike {longest:.3f} m; {int(keep.sum())} edges inside spikes change at most {delta*100:.4f}%; owners {owners}')
    reset_pose(rig)
    ART.mkdir(exist_ok=True, parents=True)
    report = dict(source_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
                  output_sha256=hashlib.sha256(OUTPUT.read_bytes()).hexdigest(), checks=checks,
                  passed=sum(c['pass_'] for c in checks), failed=sum(not c['pass_'] for c in checks))
    (ART / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
    (ART / 'verification.txt').write_text('\n'.join(('PASS ' if c['pass_'] else 'FAIL ') + c['name'] + ': ' + c['detail'] for c in checks) + '\n')
    print('VERIFICATION', report['passed'], 'passed;', report['failed'], 'failed', flush=True)
    return report, eyes


def render_sheets(rig, objects, geo, eyes, report):
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = 24
    scene.cycles.use_denoising = True
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.world.color = (.22, .22, .22)
    scene.render.film_transparent = False
    target = Vector((0, 0, 3))
    for loc, power, size in [((9, 7, 14), 2600, 8), ((-8, 4, 10), 2200, 8), ((0, -9, 12), 3000, 7)]:
        data = bpy.data.lights.new('Rig inspection light', 'AREA')
        data.energy, data.size = power, size
        obj = bpy.data.objects.new(data.name, data)
        scene.collection.objects.link(obj)
        obj.location = loc
        obj.rotation_euler = (target - obj.location).to_track_quat('-Z', 'Y').to_euler()
    data = bpy.data.cameras.new('Rig inspection camera')
    cam = bpy.data.objects.new(data.name, data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    data.type = 'ORTHO'
    overlays = []
    emission = bpy.data.materials.new('Skeleton overlay')
    emission.use_nodes = True
    nodes = emission.node_tree.nodes
    nodes.clear()
    out = nodes.new('ShaderNodeOutputMaterial')
    shader = nodes.new('ShaderNodeEmission')
    shader.inputs['Color'].default_value = (.04, .8, 1., 1)
    shader.inputs['Strength'].default_value = 1.5
    emission.node_tree.links.new(shader.outputs[0], out.inputs[0])

    def remove_overlay():
        for obj in overlays:
            bpy.data.objects.remove(obj, do_unlink=True)
        overlays.clear()

    def camera(loc, aim, scale):
        cam.location = loc
        cam.rotation_euler = (Vector(aim) - cam.location).to_track_quat('-Z', 'Y').to_euler()
        data.ortho_scale = scale
        bpy.context.view_layer.update()

    def label(text, x, y, size=.035):
        curve = bpy.data.curves.new('Inspection label', 'FONT')
        curve.body = text
        curve.size = data.ortho_scale * size
        curve.extrude = 0
        obj = bpy.data.objects.new('Inspection label', curve)
        scene.collection.objects.link(obj)
        obj.rotation_euler = cam.rotation_euler
        obj.location = cam.matrix_world @ Vector((x * data.ortho_scale, y * data.ortho_scale, -1))
        curve.materials.append(emission)
        overlays.append(obj)

    def skeleton_overlay():
        spec = json.loads(SPEC.read_text())
        forward = cam.rotation_euler.to_quaternion() @ Vector((0, 0, -1))
        plane = cam.location + forward * 1.5
        def project(co):
            return co + forward * ((plane - co).dot(forward))
        for name, a, b, parent in spec['bones']:
            if name.startswith(('eye', 'tongue')):
                continue
            curve = bpy.data.curves.new('Bone overlay', 'CURVE')
            curve.dimensions = '3D'
            curve.bevel_depth = .014
            curve.bevel_resolution = 2
            spline = curve.splines.new('POLY')
            spline.points.add(1)
            for v, co in zip(spline.points, [a, b]):
                q = project(blender_point(co))
                v.co = (*q, 1)
            obj = bpy.data.objects.new(name + ' overlay', curve)
            scene.collection.objects.link(obj)
            curve.materials.append(emission)
            overlays.append(obj)

    def shot(tag, pose, loc, aim, scale, width=1920, height=1080, bones=False):
        remove_overlay()
        set_pose(rig, pose, eyes)
        scene.render.resolution_x = width
        scene.render.resolution_y = height
        camera(loc, aim, scale * max(1, height / width))
        if bones:
            skeleton_overlay()
        aspect = height / width
        horizontal = min(1, width / height)
        label(tag, -.46 * horizontal, min(1, aspect) * .46, .022 * horizontal)
        status = f'{report["failed"]} FAILED GATES' if report['failed'] else 'ALL GATES PASSED'
        label(status, -.46 * horizontal, -min(1, aspect) * .47, .015 * horizontal)
        path = CACHE / (tag.replace(' ', '-').replace('|', '-') + '.png')
        scene.render.filepath = str(path)
        bpy.ops.render.render(write_still=True)
        remove_overlay()
        return path

    def pair(paths, output):
        images, pixels = [], []
        for path in paths:
            im = bpy.data.images.load(str(path), check_existing=False)
            w, h = im.size
            a = np.empty(w * h * 4, np.float32)
            im.pixels.foreach_get(a)
            pixels.append(a.reshape(h, w, 4))
            images.append(im)
        a = np.concatenate(pixels, axis=1)
        out = bpy.data.images.new('Inspection sheet', width=a.shape[1], height=a.shape[0], alpha=True)
        out.pixels.foreach_set(a.ravel())
        out.filepath_raw = str(ART / output)
        out.file_format = 'PNG'
        out.save()
        bpy.data.images.remove(out)
        for im in images:
            bpy.data.images.remove(im)

    a = shot('BIND SIDE', 'rest', (24, -2.5, 3), (0, -2.5, 3), 16.5, 960, 1080, True)
    b = shot('BIND TOP', 'rest', (0, -2, 26), (0, -2, 0), 21, 960, 1080, True)
    pair([a, b], 'bones.png')
    a = shot('REST', 'rest', (5.8, 5, 4.9), (0, 3.65, 4.6), 2.1, 960, 1080)
    b = shot('JAW 35 DEGREES', 'jaw', (5.8, 5, 4.9), (0, 3.65, 4.6), 2.1, 960, 1080)
    pair([a, b], 'jaw.png')
    e = eyes['r']
    c = blender_point(e['centre'])
    look = blender_point(e['gaze'] - .35 * e['up'])
    a = shot('OPEN', 'rest', c + look * 3, c, .55, 960, 1080)
    b = shot('BLINK', 'blink', c + look * 3, c, .55, 960, 1080)
    pair([a, b], 'blink.png')
    path = shot('ELBOW 80 WRIST 80', 'wingfold', (16, 15, 14), (0, -1, 3.6), 19)
    shutil.copyfile(path, ART / 'wingfold.png')
    path = shot('FOUR LEGS BENT 60', 'crouch', (14, 8, 4.2), (0, .7, 1.5), 8)
    shutil.copyfile(path, ART / 'crouch.png')
    path = shot('NECK 30 DEGREES', 'neck', (15, 2, 5), (0, 2.6, 4.5), 6)
    shutil.copyfile(path, ART / 'neck.png')
    # Vertex colors display exported jaw and lid weights.
    obj = objects['hide']
    saved = list(obj.data.materials)
    attr = obj.data.color_attributes.new(name='Rig weights', type='FLOAT_COLOR', domain='POINT')
    skin_doc, _ = read_glb(OUTPUT)
    names = [skin_doc['nodes'][i]['name'] for i in skin_doc['skins'][0]['joints']]
    w = dense_weights(geo['hide'], len(names))
    colors = np.zeros((len(w), 4), np.float32)
    colors[:, :3] = (.055, .07, .12)
    colors[:, 0] += w[:, names.index('jaw')]
    for s in ['l', 'r']:
        colors[:, 1] += w[:, names.index('eyelid_upper_' + s)]
        colors[:, 2] += w[:, names.index('eyelid_lower_' + s)]
    colors[:, 3] = 1
    attr.data.foreach_set('color', colors.ravel())
    mat = bpy.data.materials.new('Exported weight colors')
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get('Principled BSDF')
    vertex = mat.node_tree.nodes.new('ShaderNodeVertexColor')
    vertex.layer_name = attr.name
    mat.node_tree.links.new(vertex.outputs['Color'], bsdf.inputs['Base Color'])
    bsdf.inputs['Roughness'].default_value = .8
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    path = shot('RED JAW  GREEN UPPER LID  BLUE LOWER LID', 'rest', (5.8, 5, 4.9), (0, 3.65, 4.65), 2.1)
    shutil.copyfile(path, ART / 'weights-head.png')
    obj.data.materials.clear()
    for mat in saved:
        obj.data.materials.append(mat)
    reset_pose(rig)
    print('RENDERED seven 1920x1080 inspection sheets', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify-only', action='store_true')
    parser.add_argument('--render', action='store_true')
    parser.add_argument('--no-render', action='store_true')
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    ART.mkdir(parents=True, exist_ok=True)
    if not args.verify_only and not args.render:
        build()
    loaded = load_exported_scene()
    report, eyes = verify(*loaded)
    if args.render or not (args.verify_only or args.no_render):
        render_sheets(loaded[3], loaded[4], loaded[2], eyes, report)
    if report['failed']:
        print(f'FAILED: {report["failed"]} verification gates. Output and renders were preserved.', flush=True)
        # Blender otherwise returns zero for a script exception without --python-exit-code.
        sys.stdout.flush()
        sys.stderr.flush()
        os._exit(1)


if __name__ == '__main__':
    try:
        main()
    except Exception:
        import traceback
        traceback.print_exc()
        sys.stdout.flush()
        sys.stderr.flush()
        os._exit(2)
