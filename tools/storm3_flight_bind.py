"""Bake Storm3's flight posture into both standing GLBs after the material repair.

Pass the standing input directory and a separate output directory after Blender's --.
Both directories use the names storm3-raw.glb and storm3.glb.
"""
import json
import math
import sys
from pathlib import Path

import bpy
import numpy as np
from mathutils import Matrix, Quaternion, Vector

TOOLS = Path(__file__).resolve().parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(TOOLS))
from hide.glbio import read_glb, write_glb
from hide.meshdata import accessor, joint_names, primitive
from tide2_flapcheck import positions


def smooth(t):
    t = np.clip(t, 0, 1)
    return t * t * (3 - 2 * t)


def widen_attachment(mesh, names, wing_field):
    # The trailing web spans the gap from the flank to the fourth rib.
    x, z, aft = mesh['POSITION'].T
    width = wing_field['width']
    lateral, floor, rise, slope = wing_field['cut']
    distance = abs(x) - lateral + slope * np.clip(z - floor, 0, rise)
    extra = .085 * smooth((.135 + aft) / .095) * smooth((.425 - z) / .095)
    old = smooth(distance / width)
    ratio = smooth(distance / (width + extra)) / np.maximum(old, 1e-12)
    joints, weights = mesh['JOINTS_0'].copy(), mesh['WEIGHTS_0'].copy()
    is_wing = np.array([name.startswith('wing_') for name in names])
    wing_slots = is_wing[joints]
    moving = (wing_slots * weights).sum(axis=1)
    rows = np.flatnonzero((moving > 0) & (old > 0) & (ratio < .99999))
    chest = names.index('chest')
    for i in rows:
        # Only the wing can release weight. The fixed side keeps its original skin.
        combined = dict(zip(joints[i][weights[i] > 0], weights[i][weights[i] > 0]))
        for joint in list(combined):
            if is_wing[joint]:
                combined[joint] *= ratio[i]
        combined[chest] = combined.get(chest, 0) + moving[i] * (1 - ratio[i])
        kept = sorted(combined.items(), key=lambda pair: -pair[1])[:4]
        joints[i] = 0
        weights[i] = 0
        total = sum(weight for _, weight in kept)
        for slot, (joint, weight) in enumerate(kept):
            joints[i, slot] = joint
            weights[i, slot] = weight / total
    return rows, joints, weights


def sculpt(path, mesh, names, spec):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(path))
    body = next(o for o in bpy.context.scene.objects if o.type == 'MESH')
    rig = body.find_armature()
    # The importer uses Blender coordinates: forward +Y and up +Z.
    original = mesh['POSITION'][:, [0, 2, 1]].copy()
    original[:, 1] *= -1
    imported = positions(body.data)
    unique, first, inverse = np.unique(original, axis=0, return_index=True, return_inverse=True)
    assert imported.shape == original.shape and np.allclose(imported, original, atol=3e-7, rtol=0)
    inverse = inverse.ravel()
    rows, joints, weights = widen_attachment(mesh, names, spec['wing_field'])
    changed = np.zeros(len(unique), dtype=bool)
    changed[inverse[rows]] = True
    for i in rows:
        vertex = body.data.vertices[int(i)]
        for group in [item.group for item in vertex.groups]:
            body.vertex_groups[group].remove([vertex.index])
        for joint, weight in zip(joints[i], weights[i]):
            if weight > 0:
                body.vertex_groups[names[joint]].add([vertex.index], weight, 'REPLACE')

    root = rig.pose.bones['root']
    assert (root.bone.head_local - Vector((0, .100, .212))).length < 1e-5
    assert (rig.pose.bones['head'].bone.head_local - Vector((0, .256, .526))).length < 1e-5
    # Each pitch is absolute around model X. Unlisted children follow their parent.
    absolute = spec['flight_bind_pitch_deg']
    pitch = {}
    for bone in [root, *root.children_recursive]:
        inherited = pitch[bone.parent.name] if bone.parent else 0
        pitch[bone.name] = absolute.get(bone.name, inherited)
        axis = bone.bone.matrix_local.to_quaternion().inverted() @ Vector((1, 0, 0))
        bone.rotation_quaternion = Quaternion(axis, math.radians(pitch[bone.name] - inherited))
    body.modifiers['Armature'].use_deform_preserve_volume = True
    bpy.context.view_layer.update()
    graph = bpy.context.evaluated_depsgraph_get()
    coords = positions(body.evaluated_get(graph).data)[first[inverse]]
    coords = coords[:, [0, 2, 1]].copy()
    coords[:, 2] *= -1

    to_gltf = Matrix(((1, 0, 0, 0), (0, 0, 1, 0), (0, -1, 0, 0), (0, 0, 0, 1)))
    deltas = []
    for name in names:
        bone = rig.pose.bones[name]
        delta = bone.matrix @ bone.bone.matrix_local.inverted()
        deltas.append(np.array(to_gltf @ delta @ to_gltf.transposed()))
    print(f'Widened the inner web on {changed.sum()} distinct points.')
    return coords, np.array(deltas), joints, weights


def turn_shading(mesh, coords):
    # Fit the rotation of each surface patch across all adjoining triangles and UV seams.
    points, first, inverse = np.unique(mesh['POSITION'], axis=0, return_index=True, return_inverse=True)
    inverse = inverse.ravel()
    triangles = inverse[mesh['indices']]
    posed = coords[first]
    covariance = np.zeros((len(points), 3, 3))
    for corner in range(3):
        a, b = triangles[:, corner], triangles[:, (corner + 1) % 3]
        edges = points[b] - points[a]
        new_edges = posed[b] - posed[a]
        product = new_edges[:, :, None] * edges[:, None, :]
        np.add.at(covariance, a, product)
        np.add.at(covariance, b, product)
    left, _, right = np.linalg.svd(covariance)
    left[:, :, 2] *= np.linalg.det(left @ right)[:, None]
    rotation = (left @ right)[inverse]
    normals = np.einsum('vij,vj->vi', rotation, mesh['NORMAL'])
    tangents = mesh['TANGENT'].copy()
    tangents[:, :3] = np.einsum('vij,vj->vi', rotation, tangents[:, :3])
    return normals, tangents


def pose_skin(doc, blob, deltas):
    skin = doc['skins'][0]
    matrices = accessor(doc, blob, skin['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)
    world = deltas @ np.linalg.inv(matrices)
    index = {node: slot for slot, node in enumerate(skin['joints'])}
    parents = {child: i for i, node in enumerate(doc['nodes']) for child in node.get('children', [])}
    for node, slot in index.items():
        parent = parents[node]
        local = np.linalg.solve(world[index[parent]], world[slot]) if parent in index else world[slot]
        translation, rotation, scale = Matrix(local).decompose()
        doc['nodes'][node].update(translation=list(translation), scale=list(scale),
                                  rotation=[rotation.x, rotation.y, rotation.z, rotation.w])
    matrices[:] = np.linalg.inv(world)


def build(source, target):
    assert source.resolve() != target.resolve()
    spec = json.loads((TOOLS / 'skeletons/storm3.json').read_text())
    raw = source / 'storm3-raw.glb'
    doc, blob = read_glb(raw)
    mesh = primitive(doc, blob)
    names = joint_names(doc)
    coords, deltas, joints, weights = sculpt(raw, mesh, names, spec)
    normals, tangents = turn_shading(mesh, coords)
    attributes = dict(POSITION=coords, NORMAL=normals, TANGENT=tangents,
                      JOINTS_0=joints, WEIGHTS_0=weights)
    for filename in ('storm3-raw.glb', 'storm3.glb'):
        doc, blob = read_glb(source / filename)
        streams = doc['meshes'][0]['primitives'][0]['attributes']
        blob = bytearray(blob)
        assert joint_names(doc) == names
        for name, values in attributes.items():
            buffer = accessor(doc, blob, streams[name])
            assert np.array_equal(buffer, mesh[name])
            buffer[:] = values
        bounds = doc['accessors'][streams['POSITION']]
        bounds.update(min=coords.min(0).tolist(), max=coords.max(0).tolist())
        pose_skin(doc, blob, deltas)
        write_glb(target / filename, doc, blob, {})
    print(f'Baked {len(names)} joints and {len(mesh["indices"])} triangles into {target}.')


if __name__ == '__main__':
    source, target = map(Path, sys.argv[sys.argv.index('--') + 1:])
    build(source.resolve(), target.resolve())
