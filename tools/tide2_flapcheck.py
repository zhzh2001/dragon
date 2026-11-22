"""Measure body motion during a wing-only stroke on an exported dragon."""
import json
import math
import sys
from pathlib import Path

import bpy
import numpy as np
from mathutils import Quaternion, Vector, kdtree

ROOT = Path(__file__).resolve().parents[1]


def positions(mesh):
    data = np.empty(3 * len(mesh.vertices), dtype=np.float32)
    mesh.vertices.foreach_get('co', data)
    return data.reshape(-1, 3)


def wing_mask(mesh, rest, rim):
    # UV landmarks identify the shoulder rim after the rigger levels the wings.
    # Join copies across texture seams by position before walking from each tip.
    points, inverse = np.unique(rest, axis=0, return_inverse=True)
    inverse = inverse.ravel()
    tree = kdtree.KDTree(len(rim))
    for i, (u, v) in enumerate(rim):
        tree.insert((u, v, 0), i)
    tree.balance()
    boundary = set()
    found = set()
    for loop, uv in zip(mesh.loops, mesh.uv_layers.active.data):
        _, i, distance = tree.find((*uv.uv, 0))
        if distance < 0.000002:
            boundary.add(inverse[loop.vertex_index])
            found.add(i)
    if len(found) != len(rim):
        raise ValueError(f'Found {len(found)} of {len(rim)} shoulder landmarks')
    neighbours = [[] for _ in points]
    for edge in mesh.edges:
        a, b = inverse[list(edge.vertices)]
        neighbours[a].append(b)
        neighbours[b].append(a)
    tips = [np.argmin(points[:, 0]), np.argmax(points[:, 0])]
    seen = set(tips)
    for i in tips:
        for j in neighbours[i]:
            if j not in seen and j not in boundary:
                seen.add(j)
                tips.append(j)
    return np.isin(inverse, list(seen))


def check(name, regions):
    args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    path = Path(args[0]) if args else ROOT / 'assets' / f'{name}.glb'
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(path))
    body = next(o for o in bpy.context.scene.objects if o.type == 'MESH')
    rig = body.find_armature()
    mesh = body.data
    rest = positions(mesh)
    rim = json.loads((ROOT / 'artifacts' / name / 'flap-weights' / 'shoulder-rim.json').read_text())
    wing = wing_mask(mesh, rest, rim)
    landmarks = [rest[:, 1].argmin(), rest[:, 1].argmax(), rest[:, 2].argmin()]
    if wing[landmarks].any():
        raise ValueError('The shoulder rim did not separate the tail, snout and feet from the wing')
    span = 19.0
    for line in (ROOT / 'assets' / f'{name}.glb.rig.cfg').read_text().splitlines():
        fields = line.split()
        if fields[:1] == ['model_span_m'] and float(fields[1]) > 0:
            span = float(fields[1])
    scale = span / np.ptp(rest[:, 0])
    indices = np.flatnonzero(~wing)
    x, y, z = rest[indices].T
    selections = {'body': np.arange(len(indices))}
    for label, region in regions.items():
        selections[label] = np.flatnonzero(region(x, y, z))
    inputs = []
    for bone in rig.pose.bones:
        if bone.name.startswith('wing_'):
            shoulder = bone.name.startswith('wing_root_')
            axis = Vector((0, 1, 0) if shoulder else (0, 0, 1))
            local = bone.bone.matrix_local.to_quaternion().inverted() @ axis
            sign = 1 if bone.name.endswith('_l') else -1
            inputs.append((bone, local, sign, shoulder))

    # Compare all pairs. The largest travel can occur between two interior poses.
    samples = []
    peak_squared = np.zeros(len(indices))
    graph = bpy.context.evaluated_depsgraph_get()
    for t in np.linspace(0, 1, 41):
        for bone, axis, sign, shoulder in inputs:
            degrees = 72 - 197 * t if shoulder else 30 * (1 - t)
            bone.rotation_quaternion = Quaternion(axis, math.radians(sign * degrees))
        bpy.context.view_layer.update()
        posed = positions(body.evaluated_get(graph).data)[indices]
        for earlier in samples:
            delta = posed - earlier
            np.maximum(peak_squared, np.einsum('ij,ij->i', delta, delta), out=peak_squared)
        samples.append(posed)

    travel = scale * np.sqrt(peak_squared)
    print(f'{name}: {len(indices)} body vertices, {wing.sum()} wing vertices, '
          f'{len(inputs)} posed bones, {scale:.5f} m/unit')
    for label, selected in selections.items():
        i = selected[travel[selected].argmax()]
        index = indices[i]
        vertex = mesh.vertices[int(index)]
        weights = {body.vertex_groups[g.group].name: round(g.weight, 6)
                   for g in vertex.groups if g.weight > 0}
        xyz = tuple(round(float(v), 6) for v in rest[index])
        print(f'{label}: {travel[i]:.8f} m, vertex {index}, '
              f'XYZ {xyz}, weights {weights}, '
              f'{(travel[selected] > .02).sum()} over 2 cm')
    return float(travel.max())


if __name__ == '__main__':
    worst = check('tide2', {
        'left cheek fin': lambda x, y, z: (x < -.045) & (y > .19) & (z > .30),
        'left foreleg fin blade': lambda x, y, z: (x < -.070) & (y > .085) & (y < .16) & (z > .091) & (z < .155),
        'left foreleg near fin': lambda x, y, z: (x < -.055) & (y > .07) & (y < .19) & (z < .18),
        'left haunch': lambda x, y, z: (x < -.035) & (y < 0) & (y > -.13) & (z > .105),
    })
    if worst > .02:
        raise SystemExit(1)
