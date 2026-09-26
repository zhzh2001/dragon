#!/usr/bin/env python3
"""Build assets/props/rocks.glb: six static rock variants for instanced scatter, then verify.

blender -b --factory-startup --python tools/build_rocks.py              # build + verify
blender -b --factory-startup --python tools/build_rocks.py -- --render  # ... and the contact sheet
blender -b --factory-startup --python tools/build_rocks.py -- --verify-only

Engine contract (the static glTF loader): one GLB, six mesh nodes named rock_0..rock_5
at identity, no skin, no armature, no animation, no camera or light, metres, Y-up.
Each mesh is one triangle primitive with POSITION, NORMAL and COLOR_0 (neutral grey,
0.35..0.75 linear, dark in the crevices, lighter on top), 300..1500 triangles, centred
on X/Z, its ground line at Y=0 and the geometry continuing 0.6 m below it so it sits
into a slope without a floating edge.

Every shape is generated in code: a displaced icosphere, cleaved by random planes
for the angular ones, decimated to budget, cut flat 0.6 m under the ground line,
then ambient occlusion and curvature are cast into the vertex colours. One seeded
random generator and Blender's deterministic noise: the GLB is byte-identical
across builds.
"""
import argparse
import json
import math
import os
from pathlib import Path
import random
import struct
import sys
import traceback

import bpy
import bmesh
from mathutils import Matrix, Vector, noise
from mathutils.bvhtree import BVHTree

ROOT = Path(__file__).resolve().parents[1]
DEPTH = 0.6                  # metres of rock below the ground line
UNDERCUT = 0.8               # how far the raw shape reaches under the cut, so the cap is wide
TRI_MIN, TRI_MAX = 300, 1500
COLOR_MIN, COLOR_MAX = 0.35, 0.75

# name, glTF size (X, Y up, Z), triangle target, shape recipe
VARIANTS = [
    ('rock_0', (4.0, 3.0, 4.0), 520, dict(kind='boulder', lumps=(0.16, 1.1), detail=(0.05, 3.5), cells=(0.10, 1.6), cleaves=5, cleave_frac=(0.7, 0.88), taper=0.10)),
    ('rock_1', (8.0, 5.0, 7.0), 900, dict(kind='boulder', lumps=(0.22, 0.9), detail=(0.06, 3.0), cells=(0.16, 1.3), cleaves=10, cleave_frac=(0.66, 0.86), taper=0.15)),
    ('rock_2', (9.0, 1.8, 6.0), 650, dict(kind='slab', lumps=(0.16, 1.3), detail=(0.06, 3.0), cells=(0.12, 1.5), cleaves=7, cleave_frac=(0.62, 0.85), taper=0.0)),
    ('rock_3', (4.0, 10.0, 4.0), 750, dict(kind='crag', lumps=(0.20, 1.2), detail=(0.06, 3.0), cells=(0.20, 1.4), cleaves=9, cleave_frac=(0.5, 0.8), taper=0.55)),
    ('rock_4', (7.0, 2.5, 7.0), 950, dict(kind='scree', lumps=(0.18, 1.4), detail=(0.06, 3.5), cells=(0.14, 1.8), cleaves=6, cleave_frac=(0.55, 0.8), taper=0.0)),
    ('rock_5', (16.0, 8.0, 12.0), 1400, dict(kind='outcrop', lumps=(0.24, 0.9), detail=(0.07, 2.6), cells=(0.16, 1.2), cleaves=5, cleave_frac=(0.72, 0.9), taper=0.35)),
]


def clear():
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    for collection in (bpy.data.meshes, bpy.data.materials, bpy.data.images,
                       bpy.data.cameras, bpy.data.lights, bpy.data.armatures):
        for item in list(collection):
            if not item.users:
                collection.remove(item)
    bpy.context.scene.unit_settings.system = 'METRIC'
    bpy.context.scene.unit_settings.scale_length = 1.0


def fbm(p, octaves=4, lacunarity=2.0, gain=0.5):
    total, amplitude, frequency, norm = 0.0, 1.0, 1.0, 0.0
    for _ in range(octaves):
        total += amplitude * noise.noise(p * frequency)
        norm += amplitude
        amplitude *= gain
        frequency *= lacunarity
    return total / norm


def blob(rng, subdivisions, recipe, stretch=(1.0, 1.0, 1.0)):
    """A unit icosphere, stretched, displaced radially by two scales of noise and tapered.

    `lumps` is the low-frequency (amplitude, frequency) that decides the silhouette,
    `detail` the finer one that breaks the surface. `taper` narrows the top so a
    shape sits like a mass rather than a balloon. Returns a bmesh.
    """
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=subdivisions, radius=1.0)
    offset = Vector((rng.uniform(-50, 50), rng.uniform(-50, 50), rng.uniform(-50, 50)))
    offset2 = Vector((rng.uniform(-50, 50), rng.uniform(-50, 50), rng.uniform(-50, 50)))
    offset3 = Vector((rng.uniform(-50, 50), rng.uniform(-50, 50), rng.uniform(-50, 50)))
    lump_amp, lump_freq = recipe['lumps']
    det_amp, det_freq = recipe['detail']
    cell_amp, cell_freq = recipe['cells']
    # A random shear so no two variants share the icosphere's symmetry.
    shear = Matrix.Identity(3)
    shear[0][2] = rng.uniform(-0.25, 0.25)
    shear[1][2] = rng.uniform(-0.25, 0.25)
    shear[0][1] = rng.uniform(-0.15, 0.15)
    for v in bm.verts:
        p = shear @ Vector((v.co.x * stretch[0], v.co.y * stretch[1], v.co.z * stretch[2]))
        d = lump_amp * fbm(p * lump_freq + offset, 3) + det_amp * fbm(p * det_freq + offset2, 3)
        # Cellular term: Chebyshev cells push out blocky, joint-bounded facets.
        f1 = noise.voronoi(p * cell_freq + offset3, distance_metric='CHEBYCHEV')[0][0]
        d += cell_amp * (0.5 - f1)
        r = 1.0 + d * 3.0
        v.co = p * r
    taper = recipe['taper']
    if taper:
        top = max(v.co.z for v in bm.verts)
        for v in bm.verts:
            t = max(0.0, v.co.z / top)
            k = 1.0 - taper * t * (0.4 + 0.6 * t)
            v.co.x *= k
            v.co.y *= k
    return bm


def cleave(bm, rng, count, frac, normal_bias=None):
    """Cut `count` random planes off the outside of the shape and cap each cut flat.

    Each plane sits at `frac` of the shape's extent along a random direction, so it
    slices a facet off the outside rather than through the core. `normal_bias` (a
    vector and a weight) pulls the cut directions, e.g. toward vertical for a shard.
    """
    for _ in range(count):
        d = Vector((rng.gauss(0, 1), rng.gauss(0, 1), rng.gauss(0, 1)))
        if normal_bias is not None:
            d += normal_bias[0] * normal_bias[1] * rng.choice((-1, 1))
        d.normalize()
        extent = max(v.co.dot(d) for v in bm.verts)
        co = d * extent * rng.uniform(*frac)
        result = bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:],
                                        dist=1e-5, plane_co=co, plane_no=d, clear_outer=True)
        edges = [e for e in result['geom_cut'] if isinstance(e, bmesh.types.BMEdge)]
        if edges:
            bmesh.ops.holes_fill(bm, edges=edges, sides=0)
    return bm


def shells(bm):
    """The connected pieces of a bmesh as vertex lists, largest first."""
    seen = set()
    shells = []
    for start in bm.verts:
        if start in seen:
            continue
        stack, shell = [start], []
        seen.add(start)
        while stack:
            v = stack.pop()
            shell.append(v)
            for e in v.link_edges:
                o = e.other_vert(v)
                if o not in seen:
                    seen.add(o)
                    stack.append(o)
        shells.append(shell)
    shells.sort(key=len, reverse=True)
    return shells


def largest_shell(bm):
    """Delete every connected piece but the largest: a deep cleave can sever a lump."""
    doomed = [v for shell in shells(bm)[1:] for v in shell]
    if doomed:
        bmesh.ops.delete(bm, geom=doomed, context='VERTS')
    return bm


def prune_slivers(bm, fraction=0.03):
    """Delete pieces smaller than `fraction` of the mesh: slivers the union or the decimation left behind."""
    doomed = [v for shell in shells(bm) if len(shell) < fraction * len(bm.verts) for v in shell]
    if doomed:
        bmesh.ops.delete(bm, geom=doomed, context='VERTS')
    return bm


def fit(bm, size, height_below):
    """Scale the shape's bounding box to `size` (Blender X, Y, Z-up) and put its top at size.z.

    The box height covers the visible height plus `height_below`, so after the
    ground cut the visible part is exactly the requested height.
    """
    lo = Vector((min(v.co[a] for v in bm.verts) for a in range(3)))
    hi = Vector((max(v.co[a] for v in bm.verts) for a in range(3)))
    target = Vector((size[0], size[1], size[2] + height_below))
    for v in bm.verts:
        for a in range(3):
            v.co[a] = (v.co[a] - lo[a]) / max(hi[a] - lo[a], 1e-9) * target[a]
        v.co.x -= size[0] / 2
        v.co.y -= size[1] / 2
        v.co.z -= height_below
    return bm


def shape(name, size_gltf, recipe, rng):
    """Build the raw shape for a variant in Blender coordinates (X, Y=-glTF Z, Z=up)."""
    sx, sy_up, sz = size_gltf
    size = (sx, sz, sy_up)
    kind = recipe['kind']
    if kind == 'boulder':
        bm = blob(rng, 5, recipe, stretch=(1.0, 0.9, 0.8))
        cleave(bm, rng, recipe['cleaves'], recipe['cleave_frac'])
    elif kind == 'slab':
        bm = blob(rng, 5, recipe, stretch=(1.0, 0.75, 0.35))
        # Two near-horizontal cuts make the flat top and a stepped ledge; the rest chip the rim.
        cleave(bm, rng, 2, (0.55, 0.75), normal_bias=(Vector((0, 0, 1)), 4.0))
        cleave(bm, rng, recipe['cleaves'], recipe['cleave_frac'], normal_bias=(Vector((0, 0, 1)), -0.6))
    elif kind == 'crag':
        bm = blob(rng, 5, recipe, stretch=(0.55, 0.45, 1.5))
        cleave(bm, rng, recipe['cleaves'], recipe['cleave_frac'], normal_bias=(Vector((0, 0, 1)), -1.2))
        # A broken tip: one steep cut takes the rounded crown off.
        cleave(bm, rng, 1, (0.82, 0.9), normal_bias=(Vector((0.6, 0.3, 1)), 5.0))
        # A standing stone leans a little; it does not stand plumb.
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Matrix.Rotation(math.radians(7), 3, 'Y'))
    elif kind == 'scree':
        bm = bmesh.new()
        stones = [((0.0, 0.0), 1.0, 0.7), ((1.25, 0.9), 0.85, 0.65), ((-1.15, 0.75), 0.7, 0.6),
                  ((0.3, -1.25), 0.8, 0.6), ((-0.85, -0.7), 0.6, 0.55)]
        for (x, y), r, rz in stones:
            stone = blob(rng, 4, recipe, stretch=(1.0, 0.9, 0.85))
            cleave(stone, rng, recipe['cleaves'], recipe['cleave_frac'])
            largest_shell(stone)
            bmesh.ops.rotate(stone, verts=stone.verts, cent=(0, 0, 0),
                             matrix=Matrix.Rotation(rng.uniform(0, math.tau), 3, 'Z'))
            # Each stone reaches below the cut so no fragment is left hanging above it.
            bmesh.ops.scale(stone, verts=stone.verts, vec=(r, r, r * rz))
            bmesh.ops.translate(stone, verts=stone.verts, vec=(x, y, r * rz * 0.2))
            stone_mesh = bpy.data.meshes.new('tmp_stone')
            stone.to_mesh(stone_mesh)
            stone.free()
            bm.from_mesh(stone_mesh)
            bpy.data.meshes.remove(stone_mesh)
        lo = min(v.co.z for v in bm.verts)
        bmesh.ops.translate(bm, verts=bm.verts, vec=(0, 0, -lo))
    elif kind == 'outcrop':
        # A main mass and a lower shoulder fused into it, so the skyline steps.
        bm = blob(rng, 6, recipe, stretch=(1.0, 0.7, 0.55))
        cleave(bm, rng, recipe['cleaves'], recipe['cleave_frac'], normal_bias=(Vector((0, 0, 1)), -0.3))
        largest_shell(bm)
        shoulder = blob(rng, 5, recipe, stretch=(0.7, 0.6, 0.4))
        cleave(shoulder, rng, 4, (0.7, 0.9))
        largest_shell(shoulder)
        bmesh.ops.translate(shoulder, verts=shoulder.verts, vec=(0.75, -0.2, -0.25))
        shoulder_mesh = bpy.data.meshes.new('tmp_shoulder')
        shoulder.to_mesh(shoulder_mesh)
        shoulder.free()
        bm.from_mesh(shoulder_mesh)
        bpy.data.meshes.remove(shoulder_mesh)
    else:
        raise ValueError(kind)
    if kind not in ('scree', 'outcrop'):
        largest_shell(bm)
    return fit(bm, size, DEPTH + UNDERCUT)


def to_object(bm, name):
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    return obj


def fuse_loose(obj):
    """Union every loose shell of the object into one surface (the scree stones, the outcrop's shoulder)."""
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.separate(type='LOOSE')
    bpy.ops.object.mode_set(mode='OBJECT')
    parts = [o for o in bpy.context.selected_objects if o is not obj]
    for part in parts:
        union = obj.modifiers.new('union', 'BOOLEAN')
        union.operation = 'UNION'
        union.solver = 'EXACT'
        union.object = part
        bpy.ops.object.select_all(action='DESELECT')
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=union.name)
        mesh = part.data
        bpy.data.objects.remove(part)
        bpy.data.meshes.remove(mesh)
    return len(parts) + 1


def decimate(obj, target):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    tri = obj.modifiers.new('tri', 'TRIANGULATE')
    bpy.ops.object.modifier_apply(modifier=tri.name)
    faces = len(obj.data.polygons)
    dec = obj.modifiers.new('decimate', 'DECIMATE')
    dec.decimate_type = 'COLLAPSE'
    dec.ratio = target / faces
    dec.use_collapse_triangulate = True
    bpy.ops.object.modifier_apply(modifier=dec.name)
    print(f'  {obj.name}: {faces} -> {len(obj.data.polygons)} triangles (target {target})', flush=True)


def ground_cut(obj):
    """Cut the shape flat at -DEPTH and cap it, recentre on X/Y, weld and clean up."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    result = bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], dist=1e-5,
                                    plane_co=(0, 0, -DEPTH), plane_no=(0, 0, -1), clear_outer=True)
    edges = [e for e in result['geom_cut'] if isinstance(e, bmesh.types.BMEdge)]
    bmesh.ops.holes_fill(bm, edges=edges, sides=0)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
    bmesh.ops.dissolve_degenerate(bm, edges=bm.edges, dist=1e-4)
    prune_slivers(bm)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    cx = (min(v.co.x for v in bm.verts) + max(v.co.x for v in bm.verts)) / 2
    cy = (min(v.co.y for v in bm.verts) + max(v.co.y for v in bm.verts)) / 2
    bmesh.ops.translate(bm, verts=bm.verts, vec=(-cx, -cy, 0))
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()


def hemisphere(n=32):
    """A fixed Fibonacci set of directions over the +Z hemisphere, with cosine weights."""
    dirs = []
    golden = math.pi * (3 - math.sqrt(5))
    for i in range(n):
        z = 1.0 - (i + 0.5) / n          # (0, 1]: never in the tangent plane
        r = math.sqrt(1 - z * z)
        a = golden * i
        dirs.append((Vector((r * math.cos(a), r * math.sin(a), z)), z))
    return dirs


def paint(obj, rng, size):
    """Bake occlusion, concavity and weathering into a per-corner vertex colour.

    Occlusion: 32 cosine-weighted rays per vertex against the mesh's own BVH out
    to 0.6 of the rock's size. Concavity: how much the neighbours sit above the
    tangent plane. Weathering: upward-facing surfaces are lighter, as lichen and
    rain leave them. Each face gets a small tint of its own so the facets read.
    Neutral grey; the engine tints by palette.
    """
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.normal_update()
    bm.verts.ensure_lookup_table()
    tree = BVHTree.FromBMesh(bm)
    reach = 0.6 * max(size)
    rays = hemisphere(32)
    value = {}
    for v in bm.verts:
        n = v.normal.normalized() if v.normal.length > 1e-9 else Vector((0, 0, 1))
        frame = n.to_track_quat('Z', 'Y').to_matrix()
        occluded, weight = 0.0, 0.0
        origin = v.co + n * 0.02
        for d, w in rays:
            hit = tree.ray_cast(origin, frame @ d, reach)
            if hit[0] is not None:
                occluded += w * (1.0 - hit[3] / reach)
            weight += w
        ao = occluded / weight
        concave = 0.0
        if v.link_edges:
            concave = sum(max(0.0, n.dot((e.other_vert(v).co - v.co).normalized())) for e in v.link_edges) / len(v.link_edges)
        top = max(0.0, n.z) ** 1.5
        grain = 0.5 + 0.5 * noise.noise(v.co * 1.7 + Vector((13.1, 7.7, 3.3)))
        value[v.index] = 0.55 - 0.45 * ao - 0.40 * concave + 0.10 * top + 0.14 * (grain - 0.5)
    layer = bm.loops.layers.float_color.new('Col')
    for f in bm.faces:
        tint = rng.uniform(-0.04, 0.04)
        for loop in f.loops:
            g = min(COLOR_MAX, max(COLOR_MIN, value[loop.vert.index] + tint))
            loop[layer] = (g, g, g, 1.0)
    # Smooth across gentle bends, sharp across the cleaved facets and the cut. The
    # boolean union stamps its operands' material indices on the result, so every
    # face is put back on slot 0 before the one shared material is attached.
    for f in bm.faces:
        f.smooth = True
        f.material_index = 0
    for e in bm.edges:
        e.smooth = e.is_manifold and e.calc_face_angle(0.0) < math.radians(28)
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()
    obj.data.color_attributes.active_color_index = obj.data.color_attributes.find('Col')
    obj.data.color_attributes.render_color_index = obj.data.color_attributes.active_color_index


def rock_material():
    mat = bpy.data.materials.new('rock')
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    bsdf = next(n for n in nodes if n.type == 'BSDF_PRINCIPLED')
    bsdf.inputs['Roughness'].default_value = 0.95
    attr = nodes.new('ShaderNodeVertexColor')
    attr.layer_name = 'Col'
    mat.node_tree.links.new(attr.outputs['Color'], bsdf.inputs['Base Color'])
    return mat


def build(path):
    clear()
    mat = rock_material()
    objects = []
    for index, (name, size, target, recipe) in enumerate(VARIANTS):
        rng = random.Random(9000 + index)
        obj = to_object(shape(name, size, recipe, rng), name)
        fuse_loose(obj)
        decimate(obj, target)
        ground_cut(obj)
        paint(obj, rng, size)
        obj.data.materials.clear()
        obj.data.materials.append(mat)
        objects.append(obj)
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects:
        obj.select_set(True)
    options = dict(filepath=str(path), export_format='GLB', use_selection=True, export_apply=True,
                   export_yup=True, export_animations=False, export_skins=False, export_normals=True,
                   export_tangents=False, export_texcoords=False, export_vertex_color='ACTIVE',
                   export_active_vertex_color_when_no_material=True, export_all_vertex_colors=False,
                   export_materials='EXPORT', export_cameras=False, export_lights=False, export_extras=False,
                   export_morph=False)
    known = bpy.ops.export_scene.gltf.get_rna_type().properties.keys()
    bpy.ops.export_scene.gltf(**{k: v for k, v in options.items() if k in known})
    return objects


def read_glb(path):
    data = Path(path).read_bytes()
    magic, version, length = struct.unpack_from('<III', data)
    assert magic == 0x46546c67 and version == 2 and length == len(data), 'Invalid GLB'
    jl, jt = struct.unpack_from('<II', data, 12)
    assert jt == 0x4e4f534a
    doc = json.loads(data[20:20 + jl])
    off = 20 + jl
    bl, bt = struct.unpack_from('<II', data, off)
    assert bt == 0x004e4942
    return doc, data[off + 8:off + 8 + bl]


def accessor(doc, blob, index):
    acc = doc['accessors'][index]
    view = doc['bufferViews'][acc['bufferView']]
    fmt = {5120: 'b', 5121: 'B', 5122: 'h', 5123: 'H', 5125: 'I', 5126: 'f'}[acc['componentType']]
    width = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}[acc['type']]
    size = struct.calcsize('<' + fmt * width)
    stride = view.get('byteStride', size)
    start = view.get('byteOffset', 0) + acc.get('byteOffset', 0)
    assert not acc.get('sparse'), 'Sparse accessor unsupported'
    rows = [struct.unpack_from('<' + fmt * width, blob, start + i * stride) for i in range(acc['count'])]
    if acc.get('normalized'):
        scale = {'B': 255.0, 'H': 65535.0, 'b': 127.0, 'h': 32767.0}[fmt]
        rows = [tuple(c / scale for c in r) for r in rows]
    return rows


def components(positions, idx):
    """Triangle count of each connected piece, largest first, welding the exporter's per-facet vertex splits by position."""
    key = {}
    canon = []
    for i, p in enumerate(positions):
        k = tuple(round(c, 4) for c in p)
        canon.append(key.setdefault(k, len(key)))
    parent = list(range(len(key)))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    for t in range(0, len(idx), 3):
        a, b, c = (canon[idx[t + j][0]] for j in range(3))
        parent[find(a)] = find(b)
        parent[find(b)] = find(c)
    sizes = {}
    for t in range(0, len(idx), 3):
        root = find(canon[idx[t][0]])
        sizes[root] = sizes.get(root, 0) + 1
    return sorted(sizes.values(), reverse=True)


def identity_node(node):
    identity = [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.]
    return (all(abs(a - b) < 1e-6 for a, b in zip(node.get('matrix', identity), identity))
            and all(abs(x) < 1e-6 for x in node.get('translation', [0, 0, 0]))
            and all(abs(a - b) < 1e-6 for a, b in zip(node.get('rotation', [0, 0, 0, 1]), [0, 0, 0, 1]))
            and all(abs(x - 1) < 1e-6 for x in node.get('scale', [1, 1, 1])))


def verify(path):
    """Read the GLB back byte by byte and check the contract; then round-trip it through Blender."""
    doc, blob = read_glb(path)
    assert not doc.get('skins'), 'Skins forbidden'
    assert not doc.get('animations'), 'Animations forbidden'
    assert not doc.get('cameras'), 'Cameras forbidden'
    assert not doc.get('extensions', {}).get('KHR_lights_punctual'), 'Lights forbidden'
    assert 'KHR_lights_punctual' not in doc.get('extensionsUsed', []), 'Lights forbidden'
    nodes = doc.get('nodes', [])
    mesh_nodes = [n for n in nodes if 'mesh' in n]
    assert len(nodes) == len(mesh_nodes) == 6, f'Expected exactly six mesh nodes, got {len(nodes)} nodes'
    assert [n['name'] for n in mesh_nodes] == [v[0] for v in VARIANTS], 'Node names or order wrong'
    assert all(identity_node(n) and not n.get('children') and 'skin' not in n for n in mesh_nodes), 'Non-identity node'
    assert sorted(doc['scenes'][doc.get('scene', 0)]['nodes']) == list(range(6)), 'Every node must be a scene root'
    assert len(doc['meshes']) == 6, 'Expected six meshes'
    report = []
    for node, (name, size, target, _) in zip(mesh_nodes, VARIANTS):
        mesh = doc['meshes'][node['mesh']]
        assert mesh['name'] == name, f'Mesh name {mesh["name"]} != {name}'
        assert len(mesh['primitives']) == 1, f'{name}: expected one primitive'
        prim = mesh['primitives'][0]
        assert prim.get('mode', 4) == 4 and not prim.get('targets'), f'{name}: triangles only, no morphs'
        attrs = prim['attributes']
        for key in ('POSITION', 'NORMAL', 'COLOR_0'):
            assert key in attrs, f'{name}: missing {key}'
        assert 'JOINTS_0' not in attrs and 'WEIGHTS_0' not in attrs, f'{name}: skinned attributes present'
        positions = accessor(doc, blob, attrs['POSITION'])
        normals = accessor(doc, blob, attrs['NORMAL'])
        colors = accessor(doc, blob, attrs['COLOR_0'])
        assert len(positions) == len(normals) == len(colors), f'{name}: attribute counts differ'
        assert all(math.isfinite(c) for p in positions for c in p), f'{name}: non-finite position'
        assert all(abs(math.sqrt(sum(c * c for c in n)) - 1) < 1e-2 for n in normals), f'{name}: unnormalised normal'
        idx = accessor(doc, blob, prim['indices'])
        assert len(idx) % 3 == 0 and all(0 <= i[0] < len(positions) for i in idx), f'{name}: bad indices'
        tris = len(idx) // 3
        assert TRI_MIN <= tris <= TRI_MAX, f'{name}: {tris} triangles outside {TRI_MIN}..{TRI_MAX}'
        lo = [min(p[a] for p in positions) for a in range(3)]
        hi = [max(p[a] for p in positions) for a in range(3)]
        dims = [hi[a] - lo[a] for a in range(3)]
        assert -1.0 <= lo[1] <= -0.3, f'{name}: base at Y={lo[1]:.3f}, expected -1.0..-0.3'
        assert abs(hi[1] - size[1]) <= 0.10 * size[1] + 0.05, f'{name}: top at Y={hi[1]:.3f}, expected {size[1]}'
        for a, label in ((0, 'X'), (2, 'Z')):
            assert abs(dims[a] - size[a]) <= 0.15 * size[a] + 0.05, f'{name}: {label} extent {dims[a]:.3f}, expected {size[a]}'
            assert abs(lo[a] + hi[a]) <= 0.1, f'{name}: not centred on {label}'
        greys = [c[0] for c in colors]
        assert all(len(c) in (3, 4) for c in colors), f'{name}: COLOR_0 must be VEC3 or VEC4'
        assert all(abs(c[0] - c[1]) < 0.01 and abs(c[1] - c[2]) < 0.01 for c in colors), f'{name}: colour is not neutral'
        assert all(COLOR_MIN - 0.01 <= g <= COLOR_MAX + 0.01 for g in greys), f'{name}: colour outside {COLOR_MIN}..{COLOR_MAX}'
        assert max(greys) - min(greys) > 0.15, f'{name}: vertex colour carries no shading'
        pieces = components(positions, idx)
        assert min(pieces) >= 0.03 * tris, f'{name}: a piece of {min(pieces)} triangles is a stray fragment'
        assert len(pieces) <= (5 if name == 'rock_4' else 1), f'{name}: {len(pieces)} separate pieces'
        assert len(doc['materials']) >= 1 and 'material' in prim, f'{name}: no material'
        assert not doc['materials'][prim['material']].get('pbrMetallicRoughness', {}).get('baseColorTexture'), 'No textures'
        report.append((name, dims, lo, hi, tris, len(positions), min(greys), max(greys), sum(greys) / len(greys)))
    assert not doc.get('images') and not doc.get('textures'), 'No textures on this file'

    # Full round trip through Blender's importer, independent of the accessor parse above.
    clear()
    bpy.ops.import_scene.gltf(filepath=str(path))
    imported = [o for o in bpy.context.scene.objects]
    assert len(imported) == 6 and all(o.type == 'MESH' for o in imported), 'Round trip: expected six mesh objects'
    by_name = {o.name: o for o in imported}
    for name, dims, lo, hi, tris, verts, gmin, gmax, gmean in report:
        obj = by_name[name]
        assert obj.parent is None and not obj.modifiers and not obj.data.shape_keys
        assert all(abs(obj.matrix_world[r][c] - (1 if r == c else 0)) < 1e-6 for r in range(4) for c in range(4))
        obj.data.calc_loop_triangles()
        assert len(obj.data.loop_triangles) == tris, f'{name}: round trip triangle count differs'
        assert len(obj.data.color_attributes) >= 1, f'{name}: round trip lost the vertex colour'
        assert obj.data.materials and obj.data.materials[0].node_tree, f'{name}: round trip lost the material'
        assert any(n.type in ('VERTEX_COLOR', 'ATTRIBUTE') for n in obj.data.materials[0].node_tree.nodes), \
            f'{name}: imported material does not sample COLOR_0'
        # Blender re-import is Z-up: undo only that conversion for the bounds.
        pts = [(v.co.x, v.co.z, -v.co.y) for v in obj.data.vertices]
        for a in range(3):
            assert abs(min(p[a] for p in pts) - lo[a]) < 1e-3 and abs(max(p[a] for p in pts) - hi[a]) < 1e-3, f'{name}: round trip bounds differ'
    print(f'VERIFY {Path(path).relative_to(ROOT)}: PASS', flush=True)
    print('  six mesh nodes rock_0..rock_5 at identity; no skins/animations/cameras/lights/textures; '
          'POSITION+NORMAL+COLOR_0; Blender round trip matches: PASS', flush=True)
    for name, dims, lo, hi, tris, verts, gmin, gmax, gmean in report:
        print(f'  {name}: size X/Y/Z=({dims[0]:.2f}, {hi[1]:.2f}, {dims[2]:.2f}) m, base Y={lo[1]:.2f}, '
              f'triangles={tris}, vertices={verts}, grey {gmin:.2f}..{gmax:.2f} (mean {gmean:.2f})', flush=True)
    return [by_name[v[0]] for v in VARIANTS]


def render(objects, out_dir):
    """Contact sheet of the six rocks in a row: a near three-quarter view and one from 250 m."""
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = 64
    scene.cycles.seed = 3
    scene.render.resolution_x, scene.render.resolution_y = 2000, 800
    scene.render.resolution_percentage = 100
    scene.view_settings.view_transform = 'AgX'
    scene.view_settings.exposure = -1.0
    scene.world.use_nodes = True
    bg = next(n for n in scene.world.node_tree.nodes if n.type == 'BACKGROUND')
    bg.inputs['Color'].default_value = (0.35, 0.40, 0.50, 1)
    bg.inputs['Strength'].default_value = 0.6
    bpy.ops.mesh.primitive_plane_add(size=4000, location=(0, 0, 0))
    floor = bpy.context.object
    mat = bpy.data.materials.new('ground')
    mat.use_nodes = True
    bsdf = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    bsdf.inputs['Base Color'].default_value = (0.16, 0.16, 0.16, 1)
    bsdf.inputs['Roughness'].default_value = 1.0
    floor.data.materials.append(mat)
    # Shade the shipped COLOR_0 directly, whatever the importer made of the material.
    shown = bpy.data.materials.new('rock_shown')
    shown.use_nodes = True
    bsdf = next(n for n in shown.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    bsdf.inputs['Roughness'].default_value = 1.0
    attr = shown.node_tree.nodes.new('ShaderNodeVertexColor')
    attr.layer_name = objects[0].data.color_attributes[0].name
    shown.node_tree.links.new(attr.outputs['Color'], bsdf.inputs['Base Color'])
    gap = 3.0
    widths = [v[1][0] for v in VARIANTS]
    total = sum(widths) + gap * (len(widths) - 1)
    x = -total / 2
    for obj, w in zip(objects, widths):
        obj.location = (x + w / 2, 0, 0)
        obj.data.materials.clear()
        obj.data.materials.append(shown)
        x += w + gap
    bpy.ops.object.light_add(type='SUN', location=(0, 0, 100))
    sun = bpy.context.object
    sun.rotation_euler = (math.radians(50), 0, math.radians(-35))
    sun.data.energy = 2.0
    sun.data.angle = math.radians(1.5)
    bpy.ops.object.camera_add()
    camera = bpy.context.object
    scene.camera = camera
    camera.data.lens = 50
    camera.data.clip_end = 5000
    target = Vector((0, 0, 2.5))
    out_dir.mkdir(parents=True, exist_ok=True)
    half_fov = math.atan(camera.data.sensor_width / 2 / camera.data.lens)
    near = (total / 2 + 4) / math.tan(half_fov) * 1.05
    for label, distance in (('', near), ('_far', 250)):
        camera.location = target + Vector((0.32, -0.85, 0.40)).normalized() * distance
        camera.rotation_euler = (target - camera.location).to_track_quat('-Z', 'Y').to_euler()
        scene.render.filepath = str(out_dir / f'rocks{label}.png')
        bpy.ops.render.render(write_still=True)
    # One rock at a time, each from the same three-quarter angle at 3.2x its longest side.
    scene.render.resolution_x, scene.render.resolution_y = 900, 700
    scene.cycles.samples = 48
    for obj, (name, size, _, _) in zip(objects, VARIANTS):
        for other in objects:
            other.hide_render = other is not obj
        centre = Vector(obj.location) + Vector((0, 0, size[1] * 0.45))
        camera.location = centre + Vector((0.55, -0.7, 0.45)).normalized() * 3.2 * max(size)
        camera.rotation_euler = (centre - camera.location).to_track_quat('-Z', 'Y').to_euler()
        scene.render.filepath = str(out_dir / f'{name}.png')
        bpy.ops.render.render(write_still=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--verify-only', action='store_true')
    parser.add_argument('--render', action='store_true')
    parser.add_argument('--output', type=Path, default=ROOT / 'assets' / 'props' / 'rocks.glb')
    parser.add_argument('--render-dir', type=Path, default=ROOT / 'artifacts' / 'rocks')
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.verify_only:
        build(args.output)
    objects = verify(args.output)
    if args.render:
        render(objects, args.render_dir)


if __name__ == '__main__':
    try:
        main()
    except BaseException:
        traceback.print_exc()
        sys.stdout.flush()
        sys.stderr.flush()
        # Blender otherwise returns success for Python errors without a CLI flag.
        os._exit(1)
