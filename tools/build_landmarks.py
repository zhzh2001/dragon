"""Build and inspect the valley landmarks with Blender. See tools/landmarks.md."""
import argparse
import json
import math
import os
from pathlib import Path
import sys
import traceback

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import bpy
import numpy as np
from mathutils import Vector
import build_rockkit as kit

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets/props/landmarks'
ART = ROOT / 'artifacts/landmarks'
CACHE = ROOT / 'build-landmarks-cache'
RAW = ROOT / 'assets/tripo-raw'
kit.OUT, kit.ART, kit.TMP = str(OUT), str(ART), str(CACHE / 'maps')

# Source stem, glTF size axis, metres, LOD0 triangle limit.
PIECES = {
    'keep_ruin': ('tripo-ruin-keep-01', 0, 25, 24000),
    'tower_ruin': ('tripo-ruin-tower-02', 1, 23, 24000),
    'bridge': ('tripo-bridge-03', 0, 40, 24000),
    'arch_ruin': ('tripo-arch-04', 1, 14, 24000),
    'stone_circle': ('tripo-standing-stones-05', 2, 27, 24000),
    'monolith': ('tripo-monolith-06', 1, 12, 12000),
    'cairn': ('tripo-cairn-15', 1, 6, 12000),
    'dragon_bones': ('tripo-bones-16', 0, 33, 24000),
    'snag': ('tripo-snag-01', 1, 11, 12000),
    'log': ('tripo-log-01', 0, 10, 12000),
}


REJECTED = {
    'dragon_bones': 'The second pass kept the rib gaps but turned bone ends into pointed sheets.',
    'log': 'The second pass reduced the rounded branch stubs to spikes.',
    'snag': 'The second pass left two branch clumps floating beside the trunk.',
    'bridge': 'LOD2 smeared the masonry on both abutments after two passes.',
    'keep_ruin': 'The second pass distorted the window frame and lost part of the rear arch.',
}


def accepted():
    return [name for name in PIECES if name not in REJECTED]


def fit_bounds(obj, lo, hi):
    old_lo, old_hi = kit.bounds(obj)
    scale = (hi - lo) / (old_hi - old_lo)
    kit.transform(obj, scale=scale, move=lo - old_lo * scale)


def bury_bridge_feet(obj):
    """Lower the lowest course at either abutment if decimation lifted it."""
    vertices = obj.data.vertices
    coords = np.empty((len(vertices), 3), dtype=np.float32)
    vertices.foreach_get('co', coords.ravel())
    for sign in (-1, 1):
        outward = coords[:, 0] * sign
        foot = outward >= 15.2
        drop = max(0.0, float(coords[foot, 2].min()) + 0.15)
        horizontal = np.clip((outward - 12) / 3.2, 0, 1)
        vertical = np.clip(1.5 - coords[:, 2], 0, 1)
        coords[:, 2] -= drop * horizontal * vertical
    vertices.foreach_set('co', coords.ravel())
    obj.data.update()


def finish_export(path, name):
    """Supply a tangent at vertices where a collapsed UV triangle has none."""
    doc, blob = kit.read_glb(path)
    binary = bytearray(blob)
    attributes = doc['meshes'][0]['primitives'][0]['attributes']
    tangents = kit.accessor(doc, binary, attributes['TANGENT'])
    normals = kit.accessor(doc, binary, attributes['NORMAL'])
    direction = tangents[:, :3]
    dot = np.sum(direction * normals, axis=1)
    bad = (np.abs(np.linalg.norm(direction, axis=1) - 1) > 0.02) | (np.abs(dot) > 0.03)
    if bad.any():
        normal = normals[bad]
        tangent = direction[bad] - dot[bad, None] * normal
        missing = np.linalg.norm(tangent, axis=1) < 1e-5
        axis = np.eye(3)[np.abs(normal[missing]).argmin(axis=1)]
        tangent[missing] = np.cross(normal[missing], axis)
        direction[bad] = tangent / np.linalg.norm(tangent, axis=1)[:, None]
        acc = doc['accessors'][attributes['TANGENT']]
        if 'min' in acc:
            acc['min'] = tangents.min(axis=0).tolist()
            acc['max'] = tangents.max(axis=0).tolist()
    doc['nodes'][0]['name'] = doc['meshes'][0]['name'] = Path(path).stem
    doc['asset']['generator'] = 'tools/build_landmarks.py'
    doc['materials'][0]['name'] = name
    kit.write_glb(path, doc, binary)


def fit_thin_coarse(points, indices, attributes, budget, name):
    """Allow the coarse rib and branch vertices to move during reduction."""
    import ctypes as ct
    from numpy.ctypeslib import ndpointer
    used, indices = np.unique(indices.ravel(), return_inverse=True)
    pos = np.ascontiguousarray(points[used], dtype=np.float32)
    attrs = np.ascontiguousarray(attributes[used], dtype=np.float32)
    faces = np.ascontiguousarray(indices, dtype=np.uint32)
    cross_weight = 4 if name == 'dragon_bones' else 2
    scale = np.array([cross_weight, 1, cross_weight], dtype=np.float32)
    weights = np.array([.01, .01, .01, 0, 0], dtype=np.float32)
    pos *= scale
    floats = ndpointer(dtype=np.float32, flags='C_CONTIGUOUS')
    uints = ndpointer(dtype=np.uint32, flags='C_CONTIGUOUS')
    size = ct.c_size_t
    fn = ct.CDLL(str(meshopt_library())).meshopt_simplifyWithUpdate
    fn.argtypes = [uints, size, floats, size, size, floats, size, floats, size, ct.c_void_p,
                   size, ct.c_float, ct.c_uint, ct.POINTER(ct.c_float)]
    fn.restype = size
    count = fn(faces, len(faces), pos, len(pos), 12, attrs, 20, weights, 5, None,
               budget * 3, 1e10, 32, None)
    pos /= scale
    attrs[:, :3] /= np.maximum(np.linalg.norm(attrs[:, :3], axis=1, keepdims=True), 1e-8)
    return pos, attrs, faces[:count].reshape(-1, 3)


def meshopt_library():
    return Path(bpy.app.binary_path).resolve().parents[1] / 'Resources/lib/libmeshoptimizer.dylib'


def simplify(points, indices, attributes, budget):
    """Reduce triangles with Blender's bundled meshoptimizer and retain source UVs."""
    import ctypes as ct
    from numpy.ctypeslib import ndpointer
    fn = ct.CDLL(str(meshopt_library())).meshopt_simplifyWithAttributes
    floats = ndpointer(dtype=np.float32, flags='C_CONTIGUOUS')
    uints = ndpointer(dtype=np.uint32, flags='C_CONTIGUOUS')
    size = ct.c_size_t
    fn.argtypes = [uints, uints, size, floats, size, size, floats, size, floats, size,
                   ct.c_void_p, size, ct.c_float, ct.c_uint, ct.POINTER(ct.c_float)]
    fn.restype = size
    source = np.ascontiguousarray(indices, dtype=np.uint32).ravel()
    result = np.empty_like(source)
    weights = np.array([.03, .03, .03, .85, .85], dtype=np.float32)
    # Permissive mode can remove a seam while preserving its surviving UVs.
    count = fn(result, source, len(source), points, len(points), 12, attributes, 20,
               weights, 5, None, budget * 3, 1e10, 32, None)
    return result[:count].reshape(-1, 3)


def surface_mesh(name, points, attributes, faces):
    # Removing UV seams during reduction can leave coincident triangles.
    used, indices = np.unique(faces, return_inverse=True)
    _, by_position = np.unique(points[used], axis=0, return_inverse=True)
    keyed = np.sort(by_position[indices.reshape(-1, 3)], axis=1)
    _, unique = np.unique(keyed, axis=0, return_index=True)
    faces = faces[np.sort(unique)]
    a, b, c = np.moveaxis(points[faces], 1, 0)
    faces = faces[np.linalg.norm(np.cross(b - a, c - a), axis=1) > 1e-14]
    used, indices = np.unique(faces, return_inverse=True)
    triangles = indices.reshape(-1, 3)
    pos = points[used][:, [0, 2, 1]].copy()
    pos[:, 1] *= -1
    normals = attributes[used][:, [0, 2, 1]].copy()
    normals[:, 1] *= -1
    # Keep the broad face towards +Z in glTF. Lay the trunk on its two supports.
    if name in ('bridge', 'arch_ruin', 'keep_ruin', 'monolith'):
        pos[:, :2] = np.column_stack((-pos[:, 1], pos[:, 0]))
        normals[:, :2] = np.column_stack((-normals[:, 1], normals[:, 0]))
    if name == 'log':
        a = math.radians(-5.5)
        for data in (pos, normals):
            x, z = data[:, 0].copy(), data[:, 2].copy()
            data[:, 0] = math.cos(a) * x + math.sin(a) * z
            data[:, 2] = -math.sin(a) * x + math.cos(a) * z
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(pos.tolist(), [], triangles.tolist(), shade_flat=False)
    mesh.update()
    mesh.normals_split_custom_set_from_vertices(normals.tolist())
    layer = mesh.uv_layers.new(name='Tripo atlas')
    uv = attributes[used, 3:5].copy()
    uv[:, 1] = 1 - uv[:, 1]
    layer.data.foreach_set('uv', uv[triangles.ravel()].ravel())
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    return obj


def source_maps(name, doc, binary, points, attributes, triangles):
    """Transfer the original atlas, including its OpenGL tangent normal map."""
    mat = doc['materials'][0]
    pbr = mat['pbrMetallicRoughness']
    maps = {}
    for key, info in [('base', pbr['baseColorTexture']), ('orm', pbr['metallicRoughnessTexture']),
                      ('normal', mat['normalTexture'])]:
        blob, mime = kit.image_blob(doc, binary, doc['textures'][info['index']]['source'])
        path = CACHE / ('read-source' + ('.png' if mime == 'image/png' else '.jpg'))
        path.write_bytes(blob)
        pixels = kit.load_pixels(str(path), 'Non-Color')[:, :, :3]
        if key == 'base':
            pixels = kit.srgb_to_linear(pixels)
        maps[key] = kit.downsample(pixels, pixels.shape[0] // 2048)
    base = maps['base']
    if name == 'tower_ruin':
        r, g, b = base.transpose(2, 0, 1)
        blue = (b > r * 1.16) & (b > g * 1.04)
        base[blue] = (base[blue] @ kit.LUMA)[:, None] * (0.33, 0.31, 0.28)
    maps['orm'][:, :, 0] = 1
    maps['orm'][:, :, 2] = 0
    normal = maps['normal'] * 2 - 1
    normal[normal[:, :, 2] <= 0] = (0, 0, 1)
    normal /= np.maximum(np.linalg.norm(normal, axis=2, keepdims=True), 1e-8)
    maps['normal'] = normal

    # Fit directional light to the textured high mesh, weighted by visible surface area.
    sample = triangles[::max(1, len(triangles) // 300000)]
    vertices = points[sample]
    area = np.linalg.norm(np.cross(vertices[:, 1] - vertices[:, 0], vertices[:, 2] - vertices[:, 0]), axis=1)
    coords = attributes[sample, 3:5].mean(axis=1)
    normals = attributes[sample, :3].mean(axis=1)
    normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-8)
    pix = np.clip((coords * [2048, -2048] + [0, 2048]).astype(int), 0, 2047)
    rgb = base[pix[:, 1], pix[:, 0]]
    weight = np.sqrt(area)
    design = np.column_stack((np.ones(len(sample)), normals))
    coeff = np.linalg.lstsq(design * weight[:, None], np.log(np.maximum(rgb @ kit.LUMA, 1e-4)) * weight, rcond=None)[0][1:]
    # Vertex samples in the original atlas provide local surface directions.
    vertex = np.unique(triangles)
    uv = attributes[vertex, 3:5]
    cell = np.clip((uv * [1024, -1024] + [0, 1024]).astype(int), 0, 1023)
    index = cell[:, 1] * 1024 + cell[:, 0]
    count = np.bincount(index, minlength=1024**2).reshape(1024, 1024)
    light = np.bincount(index, weights=attributes[vertex, :3] @ coeff, minlength=1024**2).reshape(count.shape)
    light /= np.maximum(count, 1)
    light, _ = kit.dilate_fill(light[:, :, None].astype(np.float32), count > 0, 12)
    # Interpolate the correction field between adjacent samples in texture space.
    img = bpy.data.images.new('source light', 1024, 1024, float_buffer=True)
    img.colorspace_settings.name = 'Non-Color'
    rgba = np.ones((1024, 1024, 4), dtype=np.float32)
    rgba[:, :, :3] = np.exp(-np.clip(light, -1.5, 1.5))
    img.pixels.foreach_set(rgba.ravel())
    img.scale(2048, 2048)
    base *= kit.np_from_image(img)[:, :, :1]
    bpy.data.images.remove(img)
    restored = base[pix[:, 1], pix[:, 0]]
    base *= np.average(rgb, axis=0, weights=area) / np.average(restored, axis=0, weights=area)
    print('SOURCE LIGHT', name, coeff, flush=True)
    return maps


def conform_mesh(obj, src, weight):
    """Subdivide the shared atlas and fit new vertices to the source surface."""
    kit.activate(obj)
    bpy.ops.object.duplicate()
    fine = bpy.context.object
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.subdivide(number_cuts=1)
    bpy.ops.object.mode_set(mode='OBJECT')
    mod = fine.modifiers.new('Surface fit', 'SHRINKWRAP')
    mod.target = src
    group = fine.vertex_groups.new(name='Fit weight')
    group.add(list(range(len(fine.data.vertices))), weight, 'REPLACE')
    mod.vertex_group = group.name
    kit.apply_modifier(fine, mod)
    return fine


def bake_texture(obj, src, node, size, reach, normal):
    image = bpy.data.images.new('transfer', size, size, alpha=True, float_buffer=True)
    image.colorspace_settings.name = 'Non-Color'
    node.image = image
    node.id_data.nodes.active = node
    kit.activate(obj, [src])
    bpy.ops.object.bake(type='NORMAL' if normal else 'EMIT', normal_space='TANGENT',
                        use_selected_to_active=True, cage_extrusion=reach, max_ray_distance=reach*3,
                        margin_type='EXTEND', margin=8, use_clear=True)
    data = kit.np_from_image(image)[..., :3].copy()
    bpy.data.images.remove(image)
    return data


def sample_atlas(image, coords):
    h, w = image.shape[:2]
    x = np.clip(coords[..., 0] * w - .5, 0, w - 1)
    y = np.clip(coords[..., 1] * h - .5, 0, h - 1)
    x0, y0 = x.astype(int), y.astype(int)
    x1, y1 = np.minimum(x0+1,w-1), np.minimum(y0+1,h-1)
    tx, ty = (x-x0)[..., None], (y-y0)[..., None]
    return ((1-ty)*((1-tx)*image[y0,x0]+tx*image[y0,x1]) +
            ty*((1-tx)*image[y1,x0]+tx*image[y1,x1])).astype(np.float32)


def transfer_level(obj, src, shader, emit, output, maps, stem, size):
    material = bpy.data.materials.new('Bake receiver')
    material.use_nodes = True
    obj.data.materials.clear()
    obj.data.materials.append(material)
    target = material.node_tree.nodes.new('ShaderNodeTexImage')
    links = shader.id_data.links
    links.new(emit.outputs[0], output.inputs['Surface'])
    diagonal = np.linalg.norm(np.subtract(*kit.bounds(obj)))
    coords = bake_texture(obj, src, target, size, diagonal * .008, False)
    hit = coords[..., 2] > .5
    second = bake_texture(obj, src, target, size, diagonal * .035, False)
    coords[~hit] = second[~hit]
    hit |= second[..., 2] > .5
    base = sample_atlas(maps['base'], coords)
    orm = sample_atlas(maps['orm'], coords)
    base, filled = kit.dilate_fill(base, hit, 8)
    base[~filled] = np.mean(base[hit], axis=0)
    orm[..., 0] = 1
    orm[..., 1] = np.where(hit, orm[..., 1], np.median(orm[hit, 1]))
    orm[..., 2] = 0
    links.new(shader.outputs[0], output.inputs['Surface'])
    normals = np.zeros_like(base)
    valid = np.zeros(hit.shape, dtype=bool)
    for factor in (.002, .008, .035):
        trial = bake_texture(obj, src, target, size, diagonal * factor, True) * 2 - 1
        take = hit & ~valid & (trial[..., 2] > 0) & (np.abs(np.linalg.norm(trial, axis=2)-1) < .02)
        normals[take] = trial[take]
        valid |= take
    normals, filled = kit.dilate_fill(normals, valid, 16)
    unresolved = int((hit & ~filled).sum())
    print('NORMAL TRANSFER', stem, unresolved, 'unresolved of', int(hit.sum()), flush=True)
    kit.check(unresolved < hit.sum() * .005, f'{stem}: normal transfer missed too much surface')
    # At isolated misses, the geometric normal supplies the local surface direction.
    normals[~filled] = (0, 0, 1)
    normals /= np.maximum(np.linalg.norm(normals, axis=2, keepdims=True), 1e-8)
    prefix = str(CACHE / 'maps' / stem)
    images = (prefix+'_base.jpg', prefix+'_normal.png', prefix+'_orm.jpg')
    kit.write_jpeg(images[0], kit.to_u8(kit.linear_to_srgb(base)), 90)
    encoded = kit.to_u8(normals * .5 + .5).astype(np.int32)
    encoded[..., :2] = np.minimum((encoded[..., :2] + 1) // 2 * 2, 254)
    encoded[..., 2] = np.minimum((encoded[..., 2] + 2) // 4 * 4, 252)
    kit.write_png(images[1], encoded.astype(np.uint8))
    kit.write_jpeg(images[2], kit.to_u8(orm), 88, packed=True)
    return images


def build_shared_atlas(name, points, attrs, original, maps):
    """Project all three levels into charts drawn on the coarsest geometry."""
    source, axis, target, budget = PIECES[name]
    sc = bpy.context.scene
    kit.use_gpu(sc)
    sc.cycles.samples = 1
    src = kit.import_scan(str(RAW / (source + '.glb')))
    src.rotation_mode = 'XYZ'
    if name in ('keep_ruin', 'bridge', 'arch_ruin', 'monolith'):
        kit.transform(src, rot=(0, 0, 90))
    if name == 'log':
        kit.transform(src, rot=(0, -5.5, 0))
    if name == 'dragon_bones':
        kit.cut(src, (0, 0, -.080), (0, 0, -1), False)
    tree = src.data.materials[0].node_tree
    shader = next(n for n in tree.nodes if n.type == 'BSDF_PRINCIPLED')
    output = next(n for n in tree.nodes if n.type == 'OUTPUT_MATERIAL')
    uv = tree.nodes.new('ShaderNodeTexCoord')
    add = tree.nodes.new('ShaderNodeVectorMath')
    add.operation = 'ADD'
    add.inputs[1].default_value = (0, 0, 1)
    tree.links.new(uv.outputs['UV'], add.inputs[0])
    emit = tree.nodes.new('ShaderNodeEmission')
    tree.links.new(add.outputs[0], emit.inputs[0])
    coarse_budget = int(.0615 * budget)
    if name in ('dragon_bones', 'snag'):
        coarse = surface_mesh(name, *fit_thin_coarse(points, original, attrs, coarse_budget, name))
    else:
        coarse = surface_mesh(name, points, attrs, simplify(points, original, attrs, coarse_budget))
    # Weld UV seams before subdivision. This keeps chart boundaries watertight.
    kit.weld(coarse)
    for layer in list(coarse.data.uv_layers):
        coarse.data.uv_layers.remove(layer)
    coarse.data.uv_layers.new(name='Shared atlas')
    kit.activate(coarse)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(65), island_margin=.004)
    bpy.ops.object.mode_set(mode='OBJECT')
    weight = .42 if name in ('dragon_bones', 'snag', 'log') else .78
    middle = conform_mesh(coarse, src, weight)
    fine = conform_mesh(middle, src, weight)
    lo, hi = kit.bounds(fine)
    size = (hi-lo) * (target / (hi-lo)[(0,2,1)[axis]])
    depth = .3 if name == 'dragon_bones' else size[2] * .03
    lo = np.array([-size[0]/2, -size[1]/2, -depth])
    for lod, obj in enumerate((fine, middle, coarse)):
        # Refit shading normals after moving vertices to the source surface.
        for face in obj.data.polygons:
            face.use_smooth = True
        for edge in obj.data.edges:
            edge.use_edge_sharp = False
        obj.data.normals_split_custom_set([(0, 0, 0)] * len(obj.data.loops))
        stem = name + kit.LOD_SUFFIX[lod]
        images = transfer_level(obj, src, shader, emit, output, maps, stem, kit.LOD_MAPS[lod])
        fit_bounds(obj, lo, lo+size)
        if name == 'bridge':
            bury_bridge_feet(obj)
        obj.data.validate(clean_customdata=False)
        path = str(OUT / (stem+'.glb'))
        kit.export_glb(obj, path, stem, images)
        finish_export(path, name)
    print('BUILT', name, flush=True)


def build(name):
    kit.reset_scene()
    source, axis, target, budget = PIECES[name]
    doc, binary = kit.read_glb(str(RAW / (source + '.glb')))
    prim = doc['meshes'][0]['primitives'][0]
    points = np.ascontiguousarray(kit.accessor(doc, binary, prim['attributes']['POSITION']), dtype=np.float32)
    triangles = kit.accessor(doc, binary, prim['indices']).reshape(-1, 3)
    attrs = np.ascontiguousarray(np.column_stack([kit.accessor(doc, binary, prim['attributes'][key])
                                                  for key in ('NORMAL', 'TEXCOORD_0')]), dtype=np.float32)
    if name == 'dragon_bones':
        # Trim at source Y=-0.080. The ground hides the uncapped cut.
        triangles = triangles[points[triangles, 1].max(axis=1) > -.080]
        points = points.copy()
        points[:, 1] = np.maximum(points[:, 1], -.080)
    kit.TRIPO_PIECES = [dict(name=n) for n in PIECES]
    (CACHE / 'maps').mkdir(parents=True, exist_ok=True)
    maps = source_maps(name, doc, binary, points, attrs, triangles)
    original = simplify(points, triangles, attrs, int(.985 * budget))
    if name in ('keep_ruin', 'arch_ruin', 'tower_ruin', 'dragon_bones', 'snag', 'log'):
        return build_shared_atlas(name, points, attrs, original, maps)
    images = kit.write_maps(maps, name)
    del maps
    for lod, ratio in enumerate(kit.LOD_RATIOS):
        faces = original if lod == 0 else simplify(points, original, attrs, int(len(original) * ratio))
        obj = surface_mesh(name, points, attrs, faces)
        if lod == 0:
            low, high = kit.bounds(obj)
            span = high - low
            size = span * (target / span[(0, 2, 1)[axis]])
            depth = .3 if name == 'dragon_bones' else size[2] * .03
            lo = np.array([-size[0]/2, -size[1]/2, -depth])
            hi = lo + size
        fit_bounds(obj, lo, hi)
        if name == 'bridge':
            bury_bridge_feet(obj)
        stem = name + kit.LOD_SUFFIX[lod]
        path = str(OUT / (stem + '.glb'))
        kit.export_glb(obj, path, stem, images[lod])
        finish_export(path, name)
    print('BUILT', name, flush=True)


# --------------------------------------------------------------- review renders

# Azimuths follow Blender's XY plane. Elevations expose rims and ground cuts.
LANDMARK_VIEWS = {
    "keep_ruin": ((-76.0, 12.0), (105.0, 24.0)),
    "tower_ruin": ((-55.0, 12.0), (120.0, 31.0)),
    "bridge": ((-76.0, 12.0), (125.0, 20.0)),
    "arch_ruin": ((-82.0, 10.0), (105.0, 15.0)),
    "stone_circle": ((-55.0, 28.0), (125.0, 18.0)),
    "monolith": ((-78.0, 12.0), (104.0, 16.0)),
    "cairn": ((-55.0, 16.0), (125.0, 24.0)),
    "dragon_bones": ((-70.0, 23.0), (110.0, 30.0)),
    "snag": ((-55.0, 12.0), (125.0, 16.0)),
    "log": ((-70.0, 22.0), (110.0, 28.0)),
}


def landmark_render_scene(name):
    sc, sun, cam = kit.render_setup(samples=32)
    sc.cycles.max_bounces = 6
    sc.cycles.diffuse_bounces = 3
    sc.cycles.glossy_bounces = 3
    sc.cycles.adaptive_threshold = 0.04
    cam.data.lens = 50.0
    cam.data.sensor_width = 36.0
    cam.data.sensor_fit = "HORIZONTAL"
    ground = kit.plain_material("landmark_ground", (0.155, 0.165, 0.115))
    kit.add_plane("ground", (4000, 4000), ground)
    path = OUT / (name + '.glb')
    base = name.split('_lod')[0]
    if not path.exists() and base in REJECTED:
        path = CACHE / 'rejected' / base / (name + '.glb')
    obj = kit.import_scan(str(path))
    return sc, sun, cam, obj


def landmark_view_vector(azimuth, elevation):
    a, e = math.radians(azimuth), math.radians(elevation)
    return np.array((math.cos(a) * math.cos(e),
                     math.sin(a) * math.cos(e), math.sin(e)))


def landmark_frame(cam, lo, hi, azimuth, elevation, width, height):
    """Fit the bounding box with room for the ground and nearby shadows."""
    target = (lo + hi) * 0.5
    target[2] = max(0.0, hi[2]) * 0.43
    direction = landmark_view_vector(azimuth, elevation)
    right = np.cross(np.array((0.0, 0.0, 1.0)), direction)
    right /= np.linalg.norm(right)
    up = np.cross(direction, right)
    corners = np.array([(x, y, z) for x in (lo[0], hi[0])
                        for y in (lo[1], hi[1]) for z in (lo[2], hi[2])])
    offsets = corners - target
    half_w, half_h = kit.view_half(cam, width, height)
    depth = offsets @ direction
    horizontal = np.abs(offsets @ right) / (half_w * 0.78)
    vertical = np.abs(offsets @ up) / (half_h * 0.78)
    distance = float(np.max(depth + np.maximum(horizontal, vertical)))
    return target + direction * distance, target


def landmark_caption(cam, text, sc, width, height):
    label = kit.add_label(cam, text, sc, width, height)
    label.data.size *= 1.35
    return label


def render(names):
    """Render two near views and one fixed-distance view for each piece."""
    ART.mkdir(parents=True, exist_ok=True)
    for name in names:
        sc, sun, cam, obj = landmark_render_scene(name)
        lo, hi = kit.bounds(obj)
        views = LANDMARK_VIEWS[name]
        # Keep the same lighting across both views of a piece.
        kit.aim_sun(sun, views[0][0] + 55.0)
        label = landmark_caption(cam, name, sc, 1920, 1080)
        for index, (azimuth, elevation) in enumerate(views, 1):
            eye, target = landmark_frame(cam, lo, hi, azimuth, elevation, 1920, 1080)
            kit.aim_camera(cam, eye, target)
            label.data.body = f"{name}  |  near {index}"
            kit.render_tile(sc, str(ART / f"{name}-near{index}.png"), 1920, 1080)
        render_far(name)


def render_far(name):
    sc, sun, cam, obj = landmark_render_scene(name + '_lod2')
    lo, hi = kit.bounds(obj)
    azimuth = LANDMARK_VIEWS[name][0][0]
    kit.aim_sun(sun, azimuth + 55.0)
    target = (lo + hi) * 0.5
    target[2] = max(0.0, hi[2]) * 0.43
    eye = target + landmark_view_vector(azimuth, 8.0) * 250.0
    kit.aim_camera(cam, eye, target)
    landmark_caption(cam, f"{name}  |  250 m  |  LOD2  |  50 mm lens", sc, 1920, 1080)
    kit.render_tile(sc, str(ART / f"{name}-250m.png"), 1920, 1080)


def render_lods():
    """Compare each piece at one camera pose across all three detail levels."""
    ART.mkdir(parents=True, exist_ok=True)
    CACHE.mkdir(parents=True, exist_ok=True)
    rows = []
    for base in ("keep_ruin", "arch_ruin"):
        if base in REJECTED:
            diagnostic = CACHE / 'rejected' / base
            if not all((diagnostic / (base + suffix + '.glb')).exists() for suffix in kit.LOD_SUFFIX):
                build_rejected(base)
        tiles = []
        eye = target = None
        azimuth, elevation = LANDMARK_VIEWS[base][0]
        for level, suffix in enumerate(("", "_lod1", "_lod2")):
            name = base + suffix
            sc, sun, cam, obj = landmark_render_scene(name)
            if level == 0:
                lo, hi = kit.bounds(obj)
                eye, target = landmark_frame(cam, lo, hi, azimuth, elevation, 640, 540)
            kit.aim_camera(cam, eye, target)
            kit.aim_sun(sun, azimuth + 55.0)
            triangles = kit.tri_count(obj)
            landmark_caption(cam, f"{base}  LOD{level}\n{triangles:,} triangles", sc, 640, 540)
            path = CACHE / f"render-{name}-lod.png"
            tiles.append(kit.render_tile(sc, str(path), 640, 540))
        rows.append(np.concatenate(tiles, axis=1))
    # Blender image buffers store rows from bottom to top.
    sheet = np.concatenate(rows[::-1], axis=0)
    kit.write_png(str(ART / "lods.png"), kit.to_u8(sheet))


# Verification reads exported GLBs. Scratch image decoding stays in CACHE.
def _landmark_geometry(name, lod=0):
    suffix = ('', '_lod1', '_lod2')[lod]
    doc, binary = kit.read_glb(str(OUT / (name + suffix + '.glb')))
    primitive = doc['meshes'][0]['primitives'][0]
    pos = kit.accessor(doc, binary, primitive['attributes']['POSITION']).astype(float)
    idx = kit.accessor(doc, binary, primitive['indices']).astype(np.int64).reshape(-1, 3)
    return doc, binary, primitive, pos, idx


def _landmark_file(name, lod):
    suffix = ('', '_lod1', '_lod2')[lod]
    path = OUT / (name + suffix + '.glb')
    doc, binary = kit.read_glb(str(path))
    kit.check(len(doc.get('meshes', [])) == 1 and len(doc['meshes'][0]['primitives']) == 1,
              f'{name}: expected one mesh with one primitive')
    kit.check(len(doc.get('nodes', [])) == 1 and len(doc.get('materials', [])) == 1,
              f'{name}: expected one node and one material')
    node = doc['nodes'][0]
    kit.check(not any(key in node for key in ('translation', 'rotation', 'scale', 'matrix', 'skin', 'children')),
              f'{name}: node contained a transform, skin, or child')
    kit.check(not any(doc.get(key) for key in ('skins', 'animations', 'cameras', 'extensionsRequired')),
              f'{name}: asset contained unsupported scene data or required extensions')
    primitive = doc['meshes'][0]['primitives'][0]
    kit.check(primitive.get('mode', 4) == 4 and not primitive.get('targets'),
              f'{name}: expected triangles without morph targets')
    attributes = primitive['attributes']
    for attribute in ('POSITION', 'NORMAL', 'TANGENT', 'TEXCOORD_0'):
        kit.check(attribute in attributes, f'{name}: missing {attribute}')
    kit.check(node.get('mesh') == 0 and primitive.get('material') == 0
              and doc['scenes'][doc.get('scene', 0)]['nodes'] == [0], f'{name}: invalid scene references')
    arrays = {key: kit.accessor(doc, binary, attributes[key]).astype(float)
              for key in ('POSITION', 'NORMAL', 'TANGENT', 'TEXCOORD_0')}
    pos, normal, tangent, uv = (arrays[key] for key in ('POSITION', 'NORMAL', 'TANGENT', 'TEXCOORD_0'))
    for key, array in arrays.items():
        kit.check(len(array) == len(pos) and np.isfinite(array).all(), f'{name}: invalid {key} values')
    idx = kit.accessor(doc, binary, primitive['indices']).astype(np.int64).reshape(-1, 3)
    kit.check(len(idx) > 0 and idx.min() >= 0 and idx.max() < len(pos), f'{name}: invalid triangle indices')
    area = np.linalg.norm(np.cross(pos[idx[:, 1]] - pos[idx[:, 0]], pos[idx[:, 2]] - pos[idx[:, 0]]), axis=1)
    kit.check(area.min() > 1e-12, f'{name}: a triangle had zero area')
    _, merged = np.unique(pos, axis=0, return_inverse=True)
    faces = np.sort(merged[idx], axis=1)
    kit.check(len(np.unique(faces, axis=0)) == len(faces), f'{name}: duplicate geometric triangles remained')
    kit.check(tangent.shape[1] == 4 and np.max(np.abs(np.abs(tangent[:, 3]) - 1)) < .001,
              f'{name}: tangents lacked valid handedness')
    kit.check(np.max(np.abs(np.linalg.norm(normal, axis=1) - 1)) < .02,
              f'{name}: normals were not unit length')
    kit.check(np.max(np.abs(np.sum(normal * tangent[:, :3], axis=1))) < .03,
              f'{name}: tangents were not perpendicular to normals')
    bad_tangent = float((np.abs(np.linalg.norm(tangent[:, :3], axis=1) - 1) > .02).mean())
    kit.check(bad_tangent == 0, f'{name}: {bad_tangent:.2%} of tangents were not unit length')
    kit.check(uv.min() >= -.0001 and uv.max() <= 1.0001, f'{name}: UVs left the atlas')
    material = doc['materials'][0]
    pbr = material.get('pbrMetallicRoughness', {})
    for key, holder in (('baseColorTexture', pbr), ('metallicRoughnessTexture', pbr),
                        ('normalTexture', material), ('occlusionTexture', material)):
        kit.check(key in holder, f'{name}: missing {key}')
    kit.check(material['occlusionTexture']['index'] == pbr['metallicRoughnessTexture']['index'],
              f'{name}: occlusion and roughness did not share a texture')
    kit.check(len(doc.get('images', [])) == 3, f'{name}: expected three embedded images')
    kit.check(material.get('alphaMode', 'OPAQUE') == 'OPAQUE'
              and not any(material.get('emissiveFactor', [0, 0, 0])), f'{name}: material was transparent or emissive')
    pixels = {}
    sources = set()
    for label, texture, mime in (('base', pbr['baseColorTexture'], 'image/jpeg'),
                                ('normal', material['normalTexture'], 'image/png'),
                                ('orm', pbr['metallicRoughnessTexture'], 'image/jpeg')):
        source = doc['textures'][texture['index']]['source']
        sources.add(source)
        kit.check(texture.get('texCoord', 0) == 0 and not texture.get('extensions'),
                  f'{name}: {label} used a UV transform or another UV set')
        image = doc['images'][source]
        kit.check('bufferView' in image and 'uri' not in image, f'{name}: {label} was not embedded')
        blob, actual_mime = kit.image_blob(doc, binary, source)
        kit.check(actual_mime == mime, f'{name}: {label} image type was wrong')
        resolution = (2048, 1024, 512)[lod]
        kit.check(kit.image_size(blob, actual_mime) == (resolution, resolution),
                  f'{name}: LOD{lod} {label} map size was wrong')
        pixels[label] = kit.decode(blob, actual_mime)
        kit.check(np.isfinite(pixels[label]).all(), f'{name}: {label} contained non-finite pixels')
    kit.check(len(sources) == 3, f'{name}: map slots reused an image')
    n = pixels['normal'] * 2 - 1
    kit.check(n[..., 2].min() > 0 and np.abs(np.linalg.norm(n, axis=2) - 1).max() < .035,
              f'{name}: tangent normal map contained inward or non-unit vectors')
    blue = pixels['orm'][..., 2]
    kit.check(blue.mean() <= 3 / 255 and np.percentile(blue, 99.9) <= 32 / 255,
              f'{name}: metallic channel was not zero within JPEG error')
    # Preserve source roughness. Its value has no rock-kit lower bound.
    albedo = kit.srgb_to_linear(pixels['base'])
    mean = _landmark_albedo(albedo, uv, idx, area) * pbr.get('baseColorFactor', [1, 1, 1, 1])[:3]
    h, w = albedo.shape[:2]
    x = np.clip((uv[:, 0] * w).astype(int), 0, w - 1)
    y = np.clip(((1 - uv[:, 1]) * h).astype(int), 0, h - 1)
    luminance = albedo[y, x] @ kit.LUMA
    weight = np.zeros(len(pos))
    for corner in range(3):
        np.add.at(weight, idx[:, corner], area)
    sw = np.sqrt(weight)
    design = np.column_stack((np.ones(len(pos)), normal)) * sw[:, None]
    coefficients, *_ = np.linalg.lstsq(design, np.log(np.maximum(luminance, 1e-4)) * sw, rcond=None)
    return dict(name=name + suffix, pos=pos, idx=idx, lo=pos.min(axis=0), hi=pos.max(axis=0),
                size=np.ptp(pos, axis=0), tris=len(idx), bytes=path.stat().st_size,
                mean=mean, light=float(np.linalg.norm(coefficients[1:])))


def _bridge_deck(pos, idx):
    """Intersect every triangle with Z=0 and find the upper surface."""
    segments = []
    for triangle in pos[idx]:
        normal = np.cross(triangle[1] - triangle[0], triangle[2] - triangle[0])
        if normal[1] <= .35 * np.linalg.norm(normal):
            continue
        points = []
        for a, b in zip(triangle, np.roll(triangle, -1, axis=0)):
            if abs(a[2]) < 1e-8:
                points.append(a[:2])
            if a[2] * b[2] < 0:
                points.append((a + (b - a) * (-a[2] / (b[2] - a[2])))[:2])
        if len(points) < 2:
            continue
        points = np.unique(np.asarray(points), axis=0)
        points = points[np.argsort(points[:, 0])]
        if len(points) >= 2 and points[-1, 0] - points[0, 0] > 1e-8:
            segments.append([points[0], points[-1]])
    kit.check(bool(segments), 'bridge: no surface crossed the deck centre line Z=0')
    segments = np.asarray(segments)
    left, right = float(segments[:, 0, 0].min()), float(segments[:, 1, 0].max())
    # Merge projected intervals to detect a break in the deck surface.
    reach = left
    for segment in sorted(segments, key=lambda value: value[0, 0]):
        kit.check(segment[0, 0] <= reach + 0.30, 'bridge: deck centre-line surface contained a gap greater than 0.30 m')
        reach = max(reach, float(segment[1, 0]))
    def surface(x):
        active = segments[(segments[:, 0, 0] <= x + 1e-7) & (segments[:, 1, 0] >= x - 1e-7)]
        t = (x - active[:, 0, 0]) / (active[:, 1, 0] - active[:, 0, 0])
        return float(np.max(active[:, 0, 1] + t * (active[:, 1, 1] - active[:, 0, 1])))
    # A stone in the vertical end wall can project past the last deck stone.
    # Follow the top course to its lip, allowing the steps between deck stones.
    abutments = []
    xs = np.unique(segments[:, :, 0])
    for side in (1, -1):
        for x in xs[::side]:
            height = surface(x)
            if height >= surface(x + side * 0.4) - 0.55:
                abutments.append([float(x), height, 0.0])
                break
    # A piecewise linear surface reaches its maximum at a segment endpoint.
    height = float(segments[:, :, 1].max())
    kit.check(right - left >= 0.85 * np.ptp(pos[:, 0]), 'bridge: centre-line surface missed an end')
    return abutments, height


def _landmark_albedo(pixels, uv, idx, area):
    """Sample seven points per triangle and weight them by surface area."""
    # glTF starts at the top. Blender's decoded pixels start at the bottom.
    bary = np.array([[1/3, 1/3, 1/3], [.6, .2, .2], [.2, .6, .2], [.2, .2, .6],
                     [.1, .45, .45], [.45, .1, .45], [.45, .45, .1]])
    samples = np.einsum('sk,tkc->tsc', bary, uv[idx])
    h, w = pixels.shape[:2]
    x = np.clip(samples[..., 0] * w - .5, 0, w - 1)
    y = np.clip((1 - samples[..., 1]) * h - .5, 0, h - 1)
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    x1, y1 = np.minimum(x0 + 1, w - 1), np.minimum(y0 + 1, h - 1)
    tx, ty = (x - x0)[..., None], (y - y0)[..., None]
    colour = ((1 - ty) * ((1 - tx) * pixels[y0, x0] + tx * pixels[y0, x1])
              + ty * ((1 - tx) * pixels[y1, x0] + tx * pixels[y1, x1])).mean(axis=1)
    return np.average(colour, axis=0, weights=area)


def _enclosed_holes(mask):
    """Return projected empty regions that do not touch the image edge."""
    from collections import deque
    seen = mask.copy()
    regions = []
    height, width = mask.shape
    for y, x in np.argwhere(~mask):
        if seen[y, x]:
            continue
        queue = deque([(int(y), int(x))])
        seen[y, x] = True
        region, border = [], False
        while queue:
            py, px = queue.popleft()
            region.append((py, px))
            border |= py in (0, height - 1) or px in (0, width - 1)
            for ny, nx in ((py - 1, px), (py + 1, px), (py, px - 1), (py, px + 1)):
                if 0 <= ny < height and 0 <= nx < width and not seen[ny, nx]:
                    seen[ny, nx] = True
                    queue.append((ny, nx))
        if not border and len(region) >= 12:
            regions.append(np.asarray(region))
    return regions


def _tower_windows(level):
    """Measure both recessed windows against the masonry below their sills."""
    from mathutils.bvhtree import BVHTree
    tree = BVHTree.FromPolygons(level['pos'].tolist(), level['idx'].tolist(), all_triangles=True)
    depths = []
    for azimuth in (120, 292):
        angle = math.radians(azimuth)
        direction = Vector((math.cos(angle), 0, math.sin(angle)))
        distances = []
        for height in (8.5, 11.5):
            eye = direction * 30 + Vector((0, height, 0))
            _, _, face, distance = tree.ray_cast(eye, -direction)
            kit.check(face is not None, 'tower_ruin: window probe missed the wall')
            distances.append(distance)
        depths.append(distances[1] - distances[0])
    return np.array(depths)


def _landmark_shapes(name, levels):
    first = levels[0]
    overlaps, holes = [], 0
    # Padding keeps the raster edge outside the silhouette.
    lo = first['lo'] - first['size'].max() * .02
    hi = first['hi'] + first['size'].max() * .02
    reference = {axes: kit.silhouette(first['pos'], first['idx'], axes, lo, hi, res=256)
                 for axes in ((0, 1), (2, 1), (0, 2))}
    for lod in (1, 2):
        other = levels[lod]
        drift = max(np.abs(other['lo'] - first['lo']).max(), np.abs(other['hi'] - first['hi']).max())
        kit.check(drift <= first['size'].max() * (0.03 if lod == 1 else 0.06),
                  f'{name}: LOD{lod} bounds drifted too far')
        worst = 1.0
        for axes, a in reference.items():
            b = kit.silhouette(other['pos'], other['idx'], axes, lo, hi, res=256)
            worst = min(worst, float((a & b).sum() / max(1, (a | b).sum())))
            if lod == 1 and axes[1] == 1 and name in ('keep_ruin', 'tower_ruin', 'arch_ruin', 'bridge'):
                for region in _enclosed_holes(a):
                    retained = 1 - float(b[region[:, 0], region[:, 1]].mean())
                    kit.check(retained >= .70, f'{name}: LOD1 closed a projected hole ({retained:.1%} remained)')
                    holes += 1
                if name == 'arch_ruin' and axes != (0, 2):
                    # An open arch can connect to the image background below its feet.
                    probe = np.zeros_like(a)
                    for row in range(a.shape[0]):
                        world_y = lo[1] + row / 255 * max(hi[axes[0]] - lo[axes[0]], hi[1] - lo[1])
                        if not .15 * first['size'][1] < world_y < .60 * first['size'][1]:
                            continue
                        filled = np.flatnonzero(a[row])
                        if len(filled) > 1:
                            probe[row, filled[0]:filled[-1] + 1] = ~a[row, filled[0]:filled[-1] + 1]
                    if probe.sum() >= 40:
                        retained = 1 - float(b[probe].mean())
                        kit.check(retained >= .75, f'{name}: LOD1 closed the open arch ({retained:.1%} remained)')
                        holes += 1
        kit.check(worst >= (.95 if lod == 1 else .90), f'{name}: LOD{lod} silhouette overlap was {worst:.3f}')
        overlaps.append(worst)
    if name == 'tower_ruin':
        near_depth = _tower_windows(first)
        mid_depth = _tower_windows(levels[1])
        kit.check(np.all(near_depth > .75) and np.all(mid_depth > near_depth * .75),
                  'tower_ruin: LOD1 filled a recessed window')
        holes += len(near_depth)
    if name == 'arch_ruin':
        kit.check(holes > 0, 'arch_ruin: no projected arch opening remained at LOD0')
    return overlaps, holes


def write_metadata(names):
    entries = []
    for name in names:
        _, _, _, pos, _ = _landmark_geometry(name, 2)
        pos = np.unique(pos, axis=0)
        _, _, _, full, full_idx = _landmark_geometry(name, 0)
        size = np.ptp(full, axis=0)
        footprint = kit.footprint_area(pos)
        kit.check(footprint > 0, f'{name}: placement footprint had zero area')
        for band in (.035, .05, .075, .10, .125, .15, .20, .25):
            base = pos[pos[:, 1] <= pos[:, 1].min() + size[1] * band]
            coverage = kit.footprint_area(base) / footprint
            if len(base) >= 8 and coverage >= .25:
                break
        kit.check(len(base) >= 8 and coverage >= .25, f'{name}: placement base had insufficient coverage')
        points = kit.farthest_points(base, 26)
        entry = dict(name=name, kind='landmark', size=size.tolist(), source=PIECES[name][0] + '.glb',
                     placement='upright_yaw_only', base_band_fraction=band, base_hull_coverage=coverage,
                     check_hull_coverage=kit.footprint_area(points) / footprint, check_points=points.tolist())
        if name == 'bridge':
            entry['abutments'], entry['deck_height'] = _bridge_deck(full, full_idx)
        entries.append(entry)
    metadata = dict(version=1, axes='glTF: X right, Y up, Z face', units='metres',
                    placement_rule='Keep Y vertical. Rotate around Y for placement.',
                    ground_translation_rule='After placement, lower Y until every check point lies below terrain.',
                    check_points_rule='Use at most 26 LOD2 vertices from the lower surface.', pieces=entries)
    (OUT / 'pieces.json').write_text(json.dumps(metadata, indent=2) + '\n')


def verify(names):
    kit.check(bool(names), 'No landmarks were selected for verification')
    old_out, old_tmp = kit.OUT, kit.TMP
    kit.OUT, kit.TMP = str(OUT), str(CACHE / 'verify-images')
    lines, reports, failures = [], {}, []
    try:
        metadata = json.loads((OUT / 'pieces.json').read_text())
        entries = {item['name']: item for item in metadata['pieces']}
        kit.check(len(entries) == len(metadata['pieces']), 'Placement metadata contained duplicate names')
        kit.check(set(entries) == set(accepted()), 'Placement metadata did not list the accepted pieces')
        for name in names:
            try:
                levels = [_landmark_file(name, lod) for lod in range(3)]
                first = levels[0]
                _, axis, target, budget = PIECES[name]
                kit.check(first['tris'] <= budget, f'{name}: LOD0 exceeded {budget} triangles')
                ratios = [level['tris'] / first['tris'] for level in levels[1:]]
                kit.check(.20 <= ratios[0] <= .30 and .045 <= ratios[1] <= .08,
                          f'{name}: LOD ratios were {ratios}, expected about 25% and 6%')
                kit.check(abs(first['size'][axis] - target) <= max(.1, target * .01), f'{name}: size missed its target')
                centre = (first['lo'] + first['hi']) / 2
                kit.check(np.abs(centre[[0, 2]]).max() <= .03, f'{name}: origin missed the horizontal bounds centre')
                burial = .3 if name == 'dragon_bones' else .03 * first['size'][1]
                kit.check(abs(first['lo'][1] + burial) <= max(.03, burial * .08), f'{name}: base burial missed {burial:.3f} m')
                means = []
                for lod, level in enumerate(levels):
                    pos, mean = level['pos'], level['mean']
                    means.append(mean.tolist())
                    limit = .60 if name == 'dragon_bones' else .45 if name not in ('snag', 'log') else None
                    if limit is not None:
                        kit.check(float(mean @ kit.LUMA) <= limit,
                                  f'{name}: LOD{lod} area-weighted mean albedo exceeded {limit}')
                    if name == 'bridge':
                        for end in (pos[:, 0] <= level['lo'][0] + level['size'][0] * .12,
                                    pos[:, 0] >= level['hi'][0] - level['size'][0] * .12):
                            kit.check(pos[end, 1].min() <= 0, f'bridge: LOD{lod} had an end foot above ground')
                overlaps, holes = _landmark_shapes(name, levels)
                item = entries[name]
                kit.check(item['kind'] == 'landmark' and item['source'] == PIECES[name][0] + '.glb',
                          f'{name}: placement kind or source was wrong')
                kit.check(np.allclose(item['size'], first['size'], atol=1e-5), f'{name}: placement size was stale')
                points = np.asarray(item['check_points'])
                kit.check(points.ndim == 2 and points.shape[1] == 3 and 3 <= len(points) <= 26,
                          f'{name}: placement check point count or shape was invalid')
                kit.check(all(np.any(np.all(levels[2]['pos'] == point, axis=1)) for point in points),
                          f'{name}: placement check point was not an LOD2 vertex')
                if name == 'bridge':
                    abutments, deck_height = _bridge_deck(first['pos'], first['idx'])
                    kit.check(np.allclose(item['abutments'], abutments, atol=1e-5)
                              and abs(item['deck_height'] - deck_height) < 1e-5, 'bridge: deck metadata was stale')
                reports[name] = dict(size=first['size'].tolist(), triangles=[level['tris'] for level in levels],
                                     mean_linear_albedo=means, silhouette_overlap=overlaps, projected_holes_checked=holes,
                                     bytes=[level['bytes'] for level in levels], base_y=float(first['lo'][1]),
                                     directional_light_diagnostic=float(first['light']))
                lines.append(f"VERIFY {name}: PASS; triangles {reports[name]['triangles']}; "
                             f"size {np.round(first['size'], 3).tolist()}; albedo {np.round(means[0], 4).tolist()}; "
                             f"silhouettes {np.round(overlaps, 3).tolist()}; holes {holes}")
            except (kit.KitError, KeyError, ValueError, IndexError) as error:
                failures.append(f'VERIFY {name}: FAIL; {error}')
        allowed = {'pieces.json'} | {name + suffix + '.glb' for name in entries for suffix in ('', '_lod1', '_lod2')}
        extra = [str(path.relative_to(OUT)) for path in OUT.rglob('*') if path.is_file() and str(path.relative_to(OUT)) not in allowed]
        if extra:
            failures.append(f'VERIFY directory: unexpected files {extra}')
        total = sum(path.stat().st_size for path in OUT.rglob('*') if path.is_file())
        if total > 110_000_000:
            failures.append(f'VERIFY directory: {total} bytes exceeded 110000000')
        lines += failures
        lines.append(f'VERIFY directory: {total} bytes / 110000000; {len(failures)} failures')
        report = '\n'.join(lines) + '\n'
        CACHE.mkdir(parents=True, exist_ok=True)
        (CACHE / 'verify.txt').write_text(report)
        (CACHE / 'verify.json').write_text(json.dumps(reports, indent=2) + '\n')
        print(report, flush=True)
        kit.check(not failures, f'Landmark verification failed: {len(failures)} failures. Read build-landmarks-cache/verify.txt.')
        return reports
    finally:
        kit.OUT, kit.TMP = old_out, old_tmp


def build_rejected(name):
    """Rebuild review geometry outside the placement kit."""
    global OUT
    previous = OUT
    try:
        OUT = CACHE / 'rejected' / name
        OUT.mkdir(parents=True, exist_ok=True)
        build(name)
    finally:
        OUT = previous


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--only', help='Comma-separated piece names')
    action = parser.add_mutually_exclusive_group()
    for flag in ('verify-only', 'render', 'lods', 'metadata'):
        action.add_argument('--' + flag, action='store_true')
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    names = args.only.split(',') if args.only else accepted()
    for name in names:
        kit.check(name in PIECES, f'Unknown piece {name}')
    rejected = [name for name in names if name in REJECTED]
    if rejected:
        kit.check(len(names) == 1 and args.only and not (args.verify_only or args.metadata or args.lods),
                  'Select one rejected piece to reproduce its review in the cache.')
        global OUT
        OUT = CACHE / 'rejected' / names[0]
        kit.OUT = str(OUT)
    for folder in (OUT, ART, CACHE):
        kit.check(folder.resolve().is_relative_to(ROOT), f'Output path left the worktree: {folder}')
        folder.mkdir(parents=True, exist_ok=True)
    if args.verify_only:
        verify(names)
    elif args.render:
        render(names)
        if not args.only:
            render_lods()
    elif args.lods:
        render_lods()
    elif args.metadata:
        write_metadata(accepted())
    else:
        for name in names:
            build(name)
        if not args.only:
            write_metadata(names)
            verify(names)


if __name__ == '__main__':
    try:
        main()
    except Exception:
        traceback.print_exc()
        print('LANDMARKS FAILED', flush=True)
        sys.stderr.flush()
        os._exit(1)
