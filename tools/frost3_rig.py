"""Bind a measured round-three mesh in its original silhouette."""
import json
import math
import sys
from pathlib import Path

import bmesh
import bpy
import numpy as np
from mathutils import Vector

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
from repair_gltf_tangents import repair_tangents
from stone2_jaw import lower_surface
from storm2_membrane import Membrane
from tide2_shoulder import limit_wing_heat, smooth

ROOT = TOOLS.parent


def assign(body, v, weights):
    for group in list(g.group for g in v.groups):
        body.vertex_groups[group].remove([v.index])
    for name, weight in weights.items():
        if weight > 0:
            body.vertex_groups[name].add([v.index], weight, 'REPLACE')


def dorsal_ice(body, surface, bones):
    # Each raised blade has a narrow base. Trace each blade above the back.
    mesh = body.data
    line = [(-.516,.169),(-.42,.174),(-.34,.181),(-.26,.194),(-.19,.212),
            (-.12,.237),(-.06,.255),(-.015,.262),(.07,.266),(.13,.257),(.166,.247)]
    ys, zs = np.array(line).T
    # The two rows merge along the tail. Follow the rake to each blade's own base.
    centers = [(n,(a[1]+b[1])*.5) for n,a,b,_ in bones if n.startswith('tail_') or n in ('root','chest')]
    for v in mesh.vertices:
        x,y,z = v.co
        if v.index in surface or not ys[0] < y < ys[-1] or abs(x) > .045:
            continue
        height = z - np.interp(y,ys,zs)
        blend = smooth((height+.006)/.009) * smooth((.045-abs(x))/.014)
        if blend <= 0:
            continue
        station = y + 1.75 * max(0.,height)
        owner = min(centers, key=lambda item: abs(item[1]-station))[0]
        weights = {body.vertex_groups[g.group].name:g.weight*(1-blend) for g in v.groups}
        weights[owner] = weights.get(owner,0.) + blend
        assign(body,v,weights)


def build(name):
    body_spec = json.loads((TOOLS/'skeletons'/f'{name}.json').read_text())
    bones = body_spec['bones']
    output = ROOT/'artifacts'/name
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(ROOT/'assets'/f'{name}-cand-oneshot.glb'), merge_vertices=True)
    body = next(o for o in bpy.context.scene.objects if o.type == 'MESH')
    body.name = name
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    body.rotation_mode = 'XYZ'
    body.rotation_euler.z = math.pi
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
    mesh = body.data
    coords = np.empty((len(mesh.vertices),3), dtype=np.float32)
    mesh.vertices.foreach_get('co', coords.ravel())
    bounds = body_spec['reference_bounds']
    assert np.allclose(coords.min(0), bounds['lo'], atol=1e-5)
    assert np.allclose(coords.max(0), bounds['hi'], atol=1e-5)
    # Exact position keys weld the UV seams before decimation. Corner UVs survive the weld.
    _, first, inverse = np.unique(coords, axis=0, return_index=True, return_inverse=True)
    targets = first[inverse.ravel()]
    duplicates = np.flatnonzero(targets != np.arange(len(coords)))
    if len(duplicates):
        bm = bmesh.new()
        bm.from_mesh(mesh)
        bm.verts.ensure_lookup_table()
        weld = {bm.verts[int(i)]:bm.verts[int(targets[i])] for i in duplicates}
        bmesh.ops.weld_verts(bm, targetmap=weld)
        bm.to_mesh(mesh)
        bm.free()
    target = 80000
    modifier = body.modifiers.new('Silhouette', 'DECIMATE')
    modifier.ratio = target / len(mesh.polygons)
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    if len(mesh.polygons) == target - 1:
        # Split one boundary triangle without changing its surface or UVs.
        bm = bmesh.new()
        bm.from_mesh(mesh)
        edge = max((e for e in bm.edges if e.is_boundary), key=lambda e: e.calc_length())
        face = edge.link_faces[0]
        opposite = next(v for v in face.verts if v not in edge.verts[:])
        _, midpoint = bmesh.utils.edge_split(edge, edge.verts[0], .5)
        bmesh.utils.face_split(face, midpoint, opposite)
        bm.to_mesh(mesh)
        bm.free()
    assert len(mesh.polygons) == target, len(mesh.polygons)
    for face in mesh.polygons:
        face.use_smooth = True

    data = bpy.data.armatures.new(name)
    rig = bpy.data.objects.new(name + '_Rig', data)
    bpy.context.collection.objects.link(rig)
    bpy.ops.object.select_all(action='DESELECT')
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode='EDIT')
    for label, start, end, parent in bones:
        bone = data.edit_bones.new(label)
        bone.head, bone.tail = start, end
        if parent:
            bone.parent = data.edit_bones[parent]
    bpy.ops.object.mode_set(mode='OBJECT')
    body.select_set(True)
    bpy.ops.object.parent_set(type='ARMATURE_AUTO')
    assert all(any(g.weight > 0 for g in v.groups) for v in mesh.vertices), 'Heat bind left empty weights'

    wing = body_spec['wing_field']
    lateral, floor, rise, slope = wing['cut']
    def distance(co):
        return abs(co.x) - lateral + slope * max(0., min(rise, co.z - floor))
    surface = limit_wing_heat(body, distance, wing['width'])
    # Record the fixed shoulder vertices by UV so the exported skin can be checked independently.
    rim = set()
    for e in mesh.edges:
        a, b = e.vertices
        if (a in surface) != (b in surface):
            rim.add(b if a in surface else a)
    landmarks = sorted({tuple(mesh.uv_layers.active.data[l.index].uv)
                        for l in mesh.loops if l.vertex_index in rim})
    folder = output/'flap-weights'
    folder.mkdir(exist_ok=True, parents=True)
    (folder/'shoulder-rim.json').write_text(json.dumps(landmarks)+'\n')

    hub = Vector(wing['hub'])
    ends = {n:Vector(b) for n,a,b,parent in bones}
    membrane = Membrane({**body_spec,'wing_bind_roll_axis':ends['wing_finger_1b_r']-hub})
    arm = [(n,Vector(a),Vector(b)) for n,a,b,parent in bones
           if n in ('wing_root_r','wing_arm_r','wing_wrist_r')]
    for i, share in surface.items():
        v = mesh.vertices[i]
        x,y,z = v.co
        side = 'l' if x < 0 else 'r'
        co = Vector((abs(x),y,z))
        along, height = membrane.project(co)
        fan = smooth((along-.012)/.060)
        weights = {}
        for n,a,b in arm:
            ab = b-a
            t = max(0.,min(1.,(co-a).dot(ab)/ab.length_squared))
            weights[n[:-1]+side] = 1./((co-a-ab*t).length_squared+.00012)**1.6
        total = sum(weights.values())
        weights = {n:w/total*(1-fan) for n,w in weights.items()}
        if fan:
            for n,w in membrane.weights(along,height,side).items():
                weights[n] = w*fan
        combined = {body.vertex_groups[g.group].name:g.weight*(1-share) for g in v.groups}
        for n,w in weights.items():
            combined[n] = combined.get(n,0.) + share*w
        assign(body,v,combined)

    if name == 'frost3':
        dorsal_ice(body, surface, bones)
    mouth = body_spec['jaw_mask']
    jaw_y,jaw_z = np.array(mouth['line']).T
    def split(y):
        return np.interp(y,jaw_y,jaw_z)
    lower = lower_surface(mesh, mouth['separate_y'], split)
    last = [n for n,a,b,parent in bones if n.startswith('tail_')]
    for v in mesh.vertices:
        x,y,z = v.co
        start,end = body_spec['tail_tip']
        if y < end:
            blend = smooth((y-start)/(end-start))
            assign(body,v,{last[-1]:1-blend,last[-2]:blend})
        if v.index in surface:
            continue
        # The swept crown projects behind the head pivot. Keep its blades on the skull.
        if name == 'frost3' and y > .152 and z > .287:
            blend = smooth((z-.287)/.031)
            weights = {body.vertex_groups[g.group].name:g.weight*(1-blend) for g in v.groups}
            weights['head'] = weights.get('head',0.)+blend
            assign(body,v,weights)
        if y < mouth['hinge'][0] or z < mouth['floor']:
            continue
        share = smooth((y-mouth['hinge'][0]) / (mouth['hinge'][1]-mouth['hinge'][0]))
        if not lower.get(v.index,z<split(y)):
            share = 0.
        assign(body,v,{'jaw':share,'head':1-share})
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.vertex_group_limit_total(limit=4)
    bpy.ops.object.vertex_group_normalize_all(lock_active=False)
    for m in mesh.materials:
        m.use_backface_culling = False
    out = ROOT/'assets'/f'{name}-raw.glb'
    bpy.ops.export_scene.gltf(filepath=str(out), export_format='GLB', use_selection=True,
                              export_animations=False, export_skins=True, export_def_bones=True,
                              export_yup=True, export_tangents=True)
    repair_tangents(out)
    print(f'{name}: {len(mesh.polygons)} triangles, {len(bones)} joints, {len(surface)} wing vertices, {len(rim)} shoulder landmarks')


if __name__ == '__main__':
    build('frost3')
