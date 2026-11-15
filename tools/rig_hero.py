"""Build and inspect the measured Embercrest hero skin without changing source bytes."""
import os
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / 'build-hero-rig-cache'
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
ART = ROOT / 'artifacts/hero-rig'
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


def continuous_top_four(weights):
    """Copy the candidate rigger's continuous influence cutoff."""
    ordered = sorted(weights.items(), key=lambda item: item[1], reverse=True)
    cutoff = ordered[4][1] if len(ordered) > 4 else 0.
    kept = {n: max(0., w - cutoff) for n, w in ordered[:4]}
    total = sum(kept.values())
    if total < 1e-12:
        return {ordered[0][0]: 1.}
    return {n: w / total for n, w in kept.items() if w > 0}


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


def field(p, bones, names, radius=.22):
    d = np.array([segment_distance(p, np.array(bones[n][1]), np.array(bones[n][2])) for n in names]).T
    score = 1 / (d * d + radius * radius) ** 1.5
    order = np.argsort(-score, axis=1)[:, :5]
    chosen = np.take_along_axis(score, order, axis=1)
    cutoff = chosen[:, 4:5] if len(names) > 4 else 0
    w = np.maximum(0, chosen[:, :4] - cutoff)
    w /= w.sum(1, keepdims=True)
    out = np.zeros((len(p), len(bones)), np.float32)
    ids = [list(bones).index(n) for n in names]
    rows = np.arange(len(p))[:, None]
    out[rows, np.array(ids)[order[:, :4]]] = w
    return out


def compact_weights(w):
    order = np.argsort(-w, axis=1)[:, :5]
    values = np.take_along_axis(w, order, axis=1)
    values = np.maximum(0, values[:, :4] - values[:, 4:5])
    values /= values.sum(1, keepdims=True)
    return order[:, :4].astype('<u2'), values.astype('<f4')


def limb_field(p, bones, names):
    """Blend adjacent limb segments across a 0.30 m joint band."""
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
        previous = 1 - smoothstep(-.15, .15, along) if i else np.zeros(len(ids))
        following = smoothstep(lengths[i] - .15, lengths[i] + .15, along) if i < 3 else np.zeros(len(ids))
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


def make_weights(g, spec):
    bones = {b[0]: b for b in spec['bones']}
    names = list(bones)
    eyes = eye_measure(g, g['hide'])
    result = {}
    for mesh_name, geo in g.items():
        p = geo['POSITION'].astype(float)
        w = np.zeros((len(p), len(names)), np.float32)
        if mesh_name.startswith('eye_'):
            w[:, names.index(mesh_name)] = 1
        elif mesh_name == 'mouth':
            jaw = geo['_JAW']
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
            body_names = ['root', 'chest', 'neck_01', 'neck_02', 'neck_03', 'head']
            body_names += [n for n in names if n.startswith('tail_')]
            w = field(p, bones, body_names, .35)
            for side, sign in [('l', -1), ('r', 1)]:
                ips = p[:, 0] * sign > 0
                for fore in [True, False]:
                    prefixes = ('upper_arm_', 'forearm_', 'wrist_', 'hand_', 'toe_fore_') if fore else ('thigh_', 'shin_', 'ankle_', 'foot_', 'toe_hind_')
                    ln = [n for n in names if n.endswith('_' + side) and n.startswith(prefixes)]
                    region = ips & (p[:, 1] < 2.65) & ((p[:, 2] < -1.12) if fore else ((p[:, 2] > -1.12) & (p[:, 2] < 1.7)))
                    ids = np.flatnonzero(region)
                    lw = limb_field(p[ids], bones, ln)
                    blend = (1 - smoothstep(2.05, 2.65, p[ids, 1]))[:, None]
                    w[ids] = w[ids] * (1 - blend) + lw * blend
                wn = [n for n in names if n.startswith('wing') and n.endswith('_' + side)]
                region = ips & (p[:, 1] > 2.65) & (p[:, 2] > -1.75) & (abs(p[:, 0]) > .85)
                ids = np.flatnonzero(region)
                wf = field(p[ids], bones, wn, .408)
                hub = np.array(bones['wing_wrist_' + side][1])
                share = 1 - smoothstep(.24, .98, np.linalg.norm(p[ids] - hub, axis=1))
                wf *= 1 - share[:, None]
                wf[:, names.index('wing_wrist_' + side)] += share
                blend = smoothstep(.85, 1.5, abs(p[ids, 0]))[:, None]
                w[ids] = w[ids] * (1 - blend) + wf * blend
            if mesh_name == 'hide':
                # The dense head frill stays rigid with the skull.
                region = (p[:, 1] > 3.48) & (p[:, 2] < -1.78) & (abs(p[:, 0]) < 1.32)
                ids = np.flatnonzero(region)
                w[ids] = 0
                w[ids, names.index('head')] = 1
                jaw = geo['_JAW']
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
                up, ut, ui = unique_mesh(geo)
                for comp in components(up, ut):
                    ids = np.flatnonzero(np.isin(ui, comp))
                    c = up[comp].mean(0)
                    side = 'r' if c[0] > 0 else 'l'
                    if c[1] > 4.8 and abs(c[0]) < 1.5:
                        n = 'head'
                    elif c[1] < .6:
                        prefix = 'toe_fore_' if c[2] < -1 else 'toe_hind_'
                        candidates = [n for n in names if n.startswith(prefix) and n.endswith('_' + side)]
                        n = min(candidates, key=lambda n: segment_distance(c[None], np.array(bones[n][1]), np.array(bones[n][2]))[0])
                    else:
                        candidates = [n for n in names if n.startswith('wing') and n.endswith('_' + side)]
                        n = min(candidates, key=lambda n: segment_distance(c[None], np.array(bones[n][1]), np.array(bones[n][2]))[0])
                    w[ids] = 0
                    w[ids, names.index(n)] = 1
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
        for side, sign in [('l', -1), ('r', 1)]:
            rotate(rig, 'wing_arm_' + side, (0, 1, 0), sign * 80)
            rotate(rig, 'wing_wrist_' + side, (0, 1, 0), sign * 80)
    elif label == 'crouch':
        for side in ['l', 'r']:
            rotate(rig, 'forearm_' + side, (1, 0, 0), 60)
            rotate(rig, 'shin_' + side, (1, 0, 0), -60)
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
    valid = not np.any(counts > 2) and all(len(a) == 2 for a in adjacency.values())
    return abs(float(value)), valid, [len(s) for s in loops]


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
        check(Path(str(OUTPUT) + '.' + suffix).read_bytes() == (ROOT / ('assets/embercrest.glb.' + suffix)).read_bytes(), suffix + ' unchanged', 'byte comparison')
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
              mesh + ' influences', f'max sum error {error:.9g}; unweighted {int((w.sum(1)==0).sum())}; maximum {int((w>0).sum(1).max())}')
    hw = dense_weights(geo['hide'], count)
    jaw_error = float(abs(hw[:, names.index('jaw')] - geo['hide']['_JAW']).max())
    check(jaw_error < 1e-6, 'hide jaw mask retained', f'maximum exported weight difference {jaw_error:.9g}; source corner falloff retained')
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
    # Compare actual weights at nearest mirrored surface points.
    mirror = np.array([names.index(n[:-2] + ('_r' if n.endswith('_l') else '_l')) if n.endswith(('_l', '_r')) else i for i, n in enumerate(names)])
    for mesh in ['hide', 'membrane', 'horn', 'mouth']:
        a = geo[mesh]
        p, _, ui = unique_mesh(a)
        first = np.unique(ui, return_index=True)[1]
        w = dense_weights(a, count)[first]
        tree = KDTree(len(p))
        for i, co in enumerate(p):
            tree.insert(co, i)
        tree.balance()
        left = np.flatnonzero(p[:, 0] < -.03)
        centre_x = -.01 if mesh == 'mouth' else 0.
        errors, distances = [], []
        for i in left:
            q = p[i].copy()
            q[0] = 2 * centre_x - q[0]
            _, j, distance = tree.find(q)
            errors.append(float(abs(w[i] - w[j, mirror]).max()))
            distances.append(distance)
        errors, distances = np.array(errors), np.array(distances)
        check(errors.max() <= .02, mesh + ' mirrored weights', f'max {errors.max():.6f}; RMS {np.sqrt(np.mean(errors**2)):.6f}; nearest surface distance max {distances.max():.6f} m; {len(errors)} pairs')
        worst = int(left[int(errors.argmax())])
        query = p[worst].copy()
        query[0] = 2 * centre_x - query[0]
        _, other, distance = tree.find(query)
        checks[-1]['worst_pair'] = dict(left=p[worst].tolist(), right=p[other].tolist(), distance_m=distance,
                                       left_weights={names[j]:float(v) for j,v in enumerate(w[worst]) if v>0},
                                       right_weights={names[j]:float(v) for j,v in enumerate(w[other]) if v>0})
    reset_pose(rig)
    rest = evaluated(objects)
    err = max(float(np.linalg.norm(rest[n] - a['POSITION'], axis=1).max()) for n, a in geo.items())
    check(err < 1e-5, 'Blender bind pose', f'max vertex displacement {err:.9g} m')
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
    for side, sign in [('l', -1), ('r', 1)]:
        points = []
        for n in names:
            if n.startswith('wing_finger') and n.endswith('_' + side):
                b = rig.pose.bones[n]
                points.append(gltf_points(np.array([b.head]))[0])
        points = np.array(points)
        _, _, vt = np.linalg.svd(points - points.mean(0))
        normal = vt[-1]
        mem = posed['membrane'][geo['membrane']['POSITION'][:, 0] * sign > 0]
        deviation = abs((mem - points.mean(0)) @ normal)
        finger_error = float(abs((points - points.mean(0)) @ normal).max())
        check(deviation.max() <= .05, 'wing_' + side + ' membrane plane', f'elbow 80 and wrist 80 degrees; max {deviation.max():.6f} m; RMS {np.sqrt(np.mean(deviation**2)):.6f} m; finger nonplanarity {finger_error:.6f} m')
        # Count intersections between membrane triangles and arm-dominated hide triangles.
        hw = dense_weights(geo['hide'], count)
        arm_ids = [names.index('wing_root_' + side), names.index('wing_arm_' + side), names.index('wing_wrist_' + side)]
        ht = geo['hide']['tri']
        arm_tri = ht[np.all(hw[:, arm_ids].sum(1)[ht] > .85, axis=1)]
        mt = geo['membrane']['tri']
        mt = mt[np.all(geo['membrane']['POSITION'][mt, 0] * sign > 0, axis=1)]
        arm_tree = BVHTree.FromPolygons(posed['hide'].tolist(), arm_tri.tolist(), all_triangles=True)
        membrane_tree = BVHTree.FromPolygons(posed['membrane'].tolist(), mt.tolist(), all_triangles=True)
        overlap = arm_tree.overlap(membrane_tree)
        rest_arm = BVHTree.FromPolygons(rest['hide'].tolist(), arm_tri.tolist(), all_triangles=True)
        rest_mem = BVHTree.FromPolygons(rest['membrane'].tolist(), mt.tolist(), all_triangles=True)
        rest_overlap = rest_arm.overlap(rest_mem)
        check(not overlap, 'wing_' + side + ' arm intersections', f'{len(overlap)} folded intersecting triangle pairs; {len(rest_overlap)} already intersect at rest; {len(arm_tri)} arm triangles')
    set_pose(rig, 'crouch', eyes)
    posed = evaluated(objects)
    for side, sign in [('l', -1), ('r', 1)]:
        for fore in [True, False]:
            p = rest['hide']
            selected = (p[:, 0] * sign > .20) & (p[:, 1] < 2.55) & ((p[:, 2] < -1.12) if fore else ((p[:, 2] > -1.12) & (p[:, 2] < 1.7)))
            tris = geo['hide']['tri'][np.all(selected[geo['hide']['tri']], axis=1)]
            v0, valid0, loops = closed_volume(p, tris, p)
            v1, valid1, _ = closed_volume(posed['hide'], tris, p)
            delta = abs(v1 / v0 - 1)
            check(delta < .15 and valid0 and valid1, ('fore_' if fore else 'hind_') + side + ' volume', f'60-degree elbow/knee; rest {v0:.6f} m3; posed {v1:.6f} m3; change {delta*100:.4f}%; simple boundary loops {loops}; valid {valid0 and valid1}')
    set_pose(rig, 'neck', eyes)
    posed = evaluated(objects)
    p = rest['hide']
    frill = (p[:, 1] > 3.48) & (p[:, 2] > -3.50) & (p[:, 2] < -1.78) & (abs(p[:, 0]) < 1.32) & (geo['hide']['_JAW'] == 0)
    tris = geo['hide']['tri'][np.all(frill[geo['hide']['tri']], axis=1)]
    edges = np.unique(np.sort(np.concatenate([tris[:, [0,1]],tris[:, [1,2]],tris[:, [2,0]]]), axis=1), axis=0)
    l0 = np.linalg.norm(rest['hide'][edges[:, 0]] - rest['hide'][edges[:, 1]], axis=1)
    l1 = np.linalg.norm(posed['hide'][edges[:, 0]] - posed['hide'][edges[:, 1]], axis=1)
    keep = l0 > 1e-4
    delta = abs(l1[keep] / l0[keep] - 1).max()
    check(delta < .01, 'frill edge lengths', f'neck 30 degrees; {int(keep.sum())} edges over 0.1 mm; maximum length change {delta*100:.6f}%')
    check(np.all(dense_weights(geo['hide'], count)[frill, names.index('head')] == 1), 'frill rigid ownership', f'{int(frill.sum())} vertices in measured posterior head region')
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
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    ART.mkdir(parents=True, exist_ok=True)
    if not args.verify_only and not args.render:
        build()
    loaded = load_exported_scene()
    report, eyes = verify(*loaded)
    if args.render or not args.verify_only:
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
