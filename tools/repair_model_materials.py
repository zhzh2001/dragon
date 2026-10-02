#!/usr/bin/env python3
"""Repair the ORM and normal maps of a generated, rigged .glb, from its own mesh.

    tools/repair_model_materials.py assets/stormsail.glb -o assets/stormsail-mat.glb
    tools/repair_model_materials.py assets/x.glb -o out.glb --debug-dir /tmp/x   # diagnostics
    tools/repair_model_materials.py assets/x.glb --stats-only                   # measure, no write

Why this exists. The Hunyuan one-shot ships a base colour that is fine and two
data maps that are not: the packed occlusion-roughness-metallic has a
constant 1.0 in R (no ambient occlusion at all) and a near-constant G (one
gloss for horn, scale, membrane and claw alike), and the normal map is almost
flat. Lit by the engine's single GGX lobe (shaders/skinned.hlsl) that reads as
injection-moulded plastic. Nothing in the shader is wrong; the maps carry no
information for it to show.

What it does, and what it deliberately does not do:

  1. Bakes real ambient occlusion from the bind-pose mesh into ORM.R --
     cosine-weighted hemisphere rays against the mesh (trimesh + embree), with
     a distance falloff so the result is local crevice occlusion, not a
     lighting solution.
  2. Rebuilds roughness (ORM.G) from zones the mesh can actually locate:
       - keratin (horn, dorsal spike, claw, tooth): thin AND cylindrical
         geometry, measured by a ray through the surface and by the
         discrete curvature of the mesh. Semi-gloss.
       - membrane / fin: thin AND flat. Matte.
       - hide: everything else. Matte; the underside (bind-pose normal facing
         down, i.e. belly scutes) a little smoother.
       - wet mouth: jaw/head-owned texels that the bake found deeply
         occluded, which on a closed-mouth head is the inside of the mouth.
     plus variation that follows the surface: crevices (from the bake) are
     dustier and rougher, and the generator's own roughness residual is kept
     at reduced weight where it had any.
  3. Puts detail back into the normal map from the base colour's luminance
     high-pass -- the painted scales and membrane veins are in the colour but
     not in the normal -- composed over the original normal with a whiteout
     blend. Kept subtle: the failure mode in the other direction is a
     crawling, noisy surface. Tangent space, +Y up; the TANGENT attribute is
     untouched, and the green-channel convention is MEASURED from the mesh
     (cross(N,T)*w against dP/dV) rather than assumed.
  4. Leaves everything else byte-identical: geometry, skin, UVs, tangents,
     node tree, animations, the base-colour image, and which glTF slot each
     image sits in (so the engine's per-slot colour-space rule still holds).
     Only the two image payloads change. The output is re-read and every
     non-image bufferView is compared to the input.

  It does not invent a procedural scale field: the base colour already says
  where the scales are, and a field of its own would put scales on the
  membrane where the paint has veins. It does not separate eyes, teeth or
  nostrils: this asset has one primitive and one material, and nothing in
  the mesh or the skin names them.

Numpy, Pillow, scipy and trimesh (with embree) are required. Install them
into a venv, not the system python; the tool prints which ray engine it got.
"""
from __future__ import annotations

import argparse
import io
import json
import math
import struct
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

Image.MAX_IMAGE_PIXELS = None

# ------------------------------------------------------------------ GLB parsing
COMPONENT_DTYPE = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16,
                   5125: np.uint32, 5126: np.float32}
TYPE_COUNT = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
JSON_CHUNK = 0x4E4F534A
BIN_CHUNK = 0x004E4942


class Glb:
    def __init__(self, path):
        data = Path(path).read_bytes()
        magic, version, length = struct.unpack_from('<III', data, 0)
        assert magic == 0x46546C67, f'{path}: not a GLB'
        assert length == len(data), f'{path}: truncated'
        offset = 12
        self.json = None
        self.bin = None
        while offset < length:
            chunk_length, chunk_type = struct.unpack_from('<II', data, offset)
            chunk = data[offset + 8: offset + 8 + chunk_length]
            if chunk_type == JSON_CHUNK:
                self.json = json.loads(chunk.decode('utf-8'))
            elif chunk_type == BIN_CHUNK and self.bin is None:
                self.bin = bytes(chunk)
            offset += 8 + chunk_length
        assert self.json is not None and self.bin is not None
        assert len(self.json.get('buffers', [])) == 1, 'expected a single embedded buffer'

    def view_bytes(self, index):
        view = self.json['bufferViews'][index]
        start = view.get('byteOffset', 0)
        return self.bin[start: start + view['byteLength']]

    def accessor(self, index):
        acc = self.json['accessors'][index]
        assert 'sparse' not in acc
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
        if acc['componentType'] == 5126:
            return out.astype(np.float64)
        if acc.get('normalized'):
            return out.astype(np.float64) / np.iinfo(dtype).max
        return out.astype(np.int64)

    def image(self, index):
        info = self.json['images'][index]
        assert 'bufferView' in info, 'only embedded images are supported'
        return Image.open(io.BytesIO(self.view_bytes(info['bufferView'])))


def write_glb(glb, path, replaced_views):
    """Write `glb` with the bufferViews in `replaced_views` ({index: bytes}) swapped.

    Every other view's bytes are copied verbatim; only offsets move. Views are
    re-laid in their original order, each padded to 4 bytes.
    """
    doc = json.loads(json.dumps(glb.json))  # deep copy
    chunks = []
    offset = 0
    for index, view in enumerate(doc['bufferViews']):
        payload = replaced_views.get(index, glb.view_bytes(index))
        view['byteOffset'] = offset
        view['byteLength'] = len(payload)
        chunks.append(payload)
        offset += len(payload)
        pad = (-offset) % 4
        if pad:
            chunks.append(b'\0' * pad)
            offset += pad
    binary = b''.join(chunks)
    doc['buffers'][0]['byteLength'] = len(binary)
    text = json.dumps(doc, separators=(',', ':')).encode('utf-8')
    text += b' ' * ((-len(text)) % 4)
    total = 12 + 8 + len(text) + 8 + len(binary)
    with open(path, 'wb') as out:
        out.write(struct.pack('<III', 0x46546C67, 2, total))
        out.write(struct.pack('<II', len(text), JSON_CHUNK))
        out.write(text)
        out.write(struct.pack('<II', len(binary), BIN_CHUNK))
        out.write(binary)
    return doc


# ------------------------------------------------------------------ images
def to_float(img):
    return np.asarray(img.convert('RGB'), dtype=np.float32) / 255.0


def to_png_bytes(array):
    data = np.clip(np.rint(array * 255.0), 0, 255).astype(np.uint8)
    buf = io.BytesIO()
    Image.fromarray(data, 'RGB').save(buf, format='PNG', compress_level=6)
    return buf.getvalue()


def channel_stats(array, label):
    lines = []
    for c, name in enumerate('RGB'):
        ch = array[..., c]
        p5, p50, p95 = np.percentile(ch, [5, 50, 95])
        lines.append(f'  {label} {name}: mean {ch.mean():.3f} std {ch.std():.3f} '
                     f'p5 {p5:.3f} p50 {p50:.3f} p95 {p95:.3f}')
    return '\n'.join(lines)


def resize(array, size):
    """Bilinear resample of a float map (H, W) or (H, W, C) to size x size."""
    if array.ndim == 2:
        return np.asarray(Image.fromarray(array.astype(np.float32), 'F')
                          .resize((size, size), Image.BILINEAR))
    return np.stack([resize(array[..., c], size) for c in range(array.shape[-1])], -1)


def fill_nearest(array, mask):
    """Replace every texel outside `mask` with the value of the nearest texel inside.

    This is the UV-island dilation: the sampler and the mip chain read past
    island edges, and a hard edge against a background value becomes a seam.
    """
    _, (rows, cols) = ndimage.distance_transform_edt(~mask, return_indices=True)
    return array[rows, cols]


def masked_gaussian(values, mask, sigma):
    """Gaussian blur that only averages texels inside `mask` (normalized convolution)."""
    weight = ndimage.gaussian_filter(mask.astype(np.float32), sigma)
    blurred = ndimage.gaussian_filter(values * mask, sigma)
    return blurred / np.maximum(weight, 1e-4)


# ------------------------------------------------------------------ UV rasterisation
def rasterize(uv, faces, size):
    """Map every texel of a size x size image to (triangle index, barycentrics).

    Triangles are drawn filled plus a one-texel outline, so a texel whose
    centre falls just outside every triangle of a thin island still belongs
    to it. Barycentrics are computed analytically and clamped for those
    outline texels. Returns (tri, bary, mask); tri is -1 where nothing lands.
    """
    canvas = Image.new('I', (size, size), 0)
    draw = ImageDraw.Draw(canvas)
    pts = uv * size - 0.5  # texel centres sit at integer coordinates
    for index, (a, b, c) in enumerate(faces):
        poly = [tuple(pts[a]), tuple(pts[b]), tuple(pts[c])]
        draw.polygon(poly, fill=index + 1, outline=index + 1)
    tri = np.asarray(canvas, dtype=np.int64) - 1
    mask = tri >= 0
    rows, cols = np.nonzero(mask)
    t = tri[rows, cols]
    p = np.stack([cols, rows], -1).astype(np.float64)
    a, b, c = pts[faces[t, 0]], pts[faces[t, 1]], pts[faces[t, 2]]
    v0, v1, v2 = b - a, c - a, p - a
    d00 = (v0 * v0).sum(1); d01 = (v0 * v1).sum(1); d11 = (v1 * v1).sum(1)
    d20 = (v2 * v0).sum(1); d21 = (v2 * v1).sum(1)
    denom = d00 * d11 - d01 * d01
    denom = np.where(np.abs(denom) < 1e-12, 1e-12, denom)
    w1 = (d11 * d20 - d01 * d21) / denom
    w2 = (d00 * d21 - d01 * d20) / denom
    bary = np.stack([1.0 - w1 - w2, w1, w2], -1)
    bary = np.clip(bary, 0.0, 1.0)
    bary /= np.maximum(bary.sum(1, keepdims=True), 1e-9)
    return tri, (rows, cols), bary


def interpolate(attr, faces, tri, bary):
    return (attr[faces[tri, 0]] * bary[:, 0:1] + attr[faces[tri, 1]] * bary[:, 1:2]
            + attr[faces[tri, 2]] * bary[:, 2:3])


# ------------------------------------------------------------------ mesh measurements
GROUP_RULES = [  # first substring match wins; the names are the rigger's contract
    ('toe', 'toe'), ('wing_finger', 'finger'), ('wing_wrist', 'wing'), ('wing_arm', 'wing'),
    ('wing_root', 'wing'), ('jaw', 'jaw'), ('head', 'head'), ('neck', 'neck'), ('tail', 'tail'),
    ('thigh', 'leg'), ('shin', 'leg'), ('ankle', 'leg'), ('foot', 'leg'),
    ('upper_arm', 'leg'), ('forearm', 'leg'), ('hand', 'leg'), ('wrist', 'leg'),
    ('chest', 'body'), ('root', 'body'),
]
GROUPS = ['body', 'neck', 'head', 'jaw', 'tail', 'wing', 'finger', 'leg', 'toe', 'other']


def joint_groups(glb):
    joints = glb.json['skins'][0]['joints']
    names = [glb.json['nodes'][j].get('name', '') for j in joints]
    group_of = []
    for name in names:
        lowered = name.lower()
        for key, group in GROUP_RULES:
            if key in lowered:
                group_of.append(GROUPS.index(group))
                break
        else:
            group_of.append(GROUPS.index('other'))
    return np.array(group_of), names


def vertex_group_weights(joints, weights, group_of):
    out = np.zeros((len(joints), len(GROUPS)))
    for k in range(joints.shape[1]):
        np.add.at(out, (np.arange(len(joints)), group_of[joints[:, k]]), weights[:, k])
    return out


def vertex_curvature(positions, normals, faces, extent):
    """Dimensionless discrete curvature: extent / radius, averaged over edges.

    A cylinder of radius r gives |dn|/|dp| = 1/r, so a finger bone or horn
    reads as a large number and a membrane as a small one.
    """
    edges = np.concatenate([faces[:, [0, 1]], faces[:, [1, 2]], faces[:, [2, 0]]])
    dn = np.linalg.norm(normals[edges[:, 0]] - normals[edges[:, 1]], axis=1)
    dp = np.linalg.norm(positions[edges[:, 0]] - positions[edges[:, 1]], axis=1)
    k = dn / np.maximum(dp, 1e-9) * extent
    total = np.zeros(len(positions)); count = np.zeros(len(positions))
    np.add.at(total, edges[:, 0], k); np.add.at(count, edges[:, 0], 1)
    np.add.at(total, edges[:, 1], k); np.add.at(count, edges[:, 1], 1)
    return total / np.maximum(count, 1)


def cosine_hemisphere(normals, rng):
    """One cosine-weighted direction about each of `normals`."""
    u1 = rng.random(len(normals)); u2 = rng.random(len(normals))
    r = np.sqrt(u1); phi = 2.0 * math.pi * u2
    local = np.stack([r * np.cos(phi), r * np.sin(phi), np.sqrt(np.maximum(0.0, 1.0 - u1))], -1)
    helper = np.where(np.abs(normals[:, 0:1]) < 0.9, [[1.0, 0.0, 0.0]], [[0.0, 1.0, 0.0]])
    t = np.cross(helper, normals); t /= np.linalg.norm(t, axis=1, keepdims=True)
    b = np.cross(normals, t)
    return local[:, 0:1] * t + local[:, 1:2] * b + local[:, 2:3] * normals


def ray_distances(mesh, origins, directions, far):
    """Distance to the first hit for each ray, `far` where nothing is hit."""
    out = np.full(len(origins), far)
    locations, index_ray, _ = mesh.ray.intersects_location(origins, directions, multiple_hits=False)
    if len(index_ray):
        d = np.linalg.norm(locations - origins[index_ray], axis=1)
        # embree can report one ray twice on a shared edge; keep the nearest
        np.minimum.at(out, index_ray, d)
    return out


def bake_occlusion(mesh, positions, normals, extent, rays, radius, rng, log):
    """Hemisphere AO with linear distance falloff; 1 = open, 0 = fully enclosed."""
    origins = positions + normals * (3e-4 * extent)
    far = radius * extent
    occlusion = np.zeros(len(positions))
    batch = 1_000_000
    started = time.time()
    for r in range(rays):
        directions = cosine_hemisphere(normals, rng)
        for start in range(0, len(positions), batch):
            stop = min(start + batch, len(positions))
            d = ray_distances(mesh, origins[start:stop], directions[start:stop], far)
            occlusion[start:stop] += np.clip(1.0 - d / far, 0.0, 1.0)
        if r == 0 or (r + 1) % 16 == 0:
            log(f'    ao pass {r + 1}/{rays}  {time.time() - started:.0f}s')
    return 1.0 - occlusion / rays


def measure_thickness(mesh, positions, normals, extent):
    origins = positions - normals * (2e-4 * extent)
    far = extent
    return ray_distances(mesh, origins, -normals, far) / extent


def check_bitangent_convention(positions, normals, tangents, uv, faces):
    """Return +1 if cross(N,T)*w points toward decreasing glTF V (image up), else -1.

    That is the glTF (+Y up) convention. If a file ever disagrees, the derived
    detail's green channel has to flip with it, so this is measured per file.
    """
    p0, p1, p2 = positions[faces[:, 0]], positions[faces[:, 1]], positions[faces[:, 2]]
    t0, t1, t2 = uv[faces[:, 0]], uv[faces[:, 1]], uv[faces[:, 2]]
    e1, e2 = p1 - p0, p2 - p0
    d1, d2 = t1 - t0, t2 - t0
    det = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]
    ok = np.abs(det) > 1e-12
    inv = 1.0 / np.where(ok, det, 1.0)
    dpdv = (e2 * d1[:, 0:1] - e1 * d2[:, 0:1]) * inv[:, None]
    tf = (tangents[faces[:, 0]] + tangents[faces[:, 1]] + tangents[faces[:, 2]]) / 3.0
    nf = (normals[faces[:, 0]] + normals[faces[:, 1]] + normals[faces[:, 2]]) / 3.0
    bf = np.cross(nf, tf[:, :3]) * tf[:, 3:4]
    cos = (bf * dpdv).sum(1) / (np.linalg.norm(bf, axis=1) * np.linalg.norm(dpdv, axis=1) + 1e-20)
    fraction_down = float((cos[ok] > 0).mean())
    return (-1 if fraction_down > 0.5 else 1), fraction_down


# ------------------------------------------------------------------ the repair
def repair(args):
    log = lambda s: print(s, flush=True)
    src = Path(args.glb)
    glb = Glb(src)
    doc = glb.json
    assert len(doc['meshes']) == 1 and len(doc['meshes'][0]['primitives']) == 1, \
        'this tool expects one mesh with one primitive (one material)'
    assert len(doc['materials']) == 1
    prim = doc['meshes'][0]['primitives'][0]
    material = doc['materials'][0]
    attrs = prim['attributes']
    for needed in ('POSITION', 'NORMAL', 'TEXCOORD_0', 'TANGENT', 'JOINTS_0', 'WEIGHTS_0'):
        assert needed in attrs, f'missing {needed}'

    positions = glb.accessor(attrs['POSITION'])
    normals = glb.accessor(attrs['NORMAL'])
    normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-12)
    uv = glb.accessor(attrs['TEXCOORD_0'])
    tangents = glb.accessor(attrs['TANGENT'])
    joints = glb.accessor(attrs['JOINTS_0'])
    weights = glb.accessor(attrs['WEIGHTS_0'])
    faces = glb.accessor(prim['indices']).reshape(-1, 3)
    extent = float((positions.max(0) - positions.min(0)).max())
    log(f'{src}: {len(positions)} verts, {len(faces)} tris, extent {extent:.4f}')

    # Which image is in which slot. Slots are the colour-space contract.
    normal_image = doc['textures'][material['normalTexture']['index']]['source']
    orm_image = doc['textures'][material['pbrMetallicRoughness']['metallicRoughnessTexture']['index']]['source']
    base_image = doc['textures'][material['pbrMetallicRoughness']['baseColorTexture']['index']]['source']
    assert len({normal_image, orm_image, base_image}) == 3
    orig_normal = to_float(glb.image(normal_image))
    orig_orm = to_float(glb.image(orm_image))
    base = to_float(glb.image(base_image))
    size = base.shape[0]
    assert base.shape == orig_normal.shape == orig_orm.shape and base.shape[1] == size
    log('before:')
    log(channel_stats(orig_normal, 'normal'))
    log(channel_stats(orig_orm, 'orm'))

    sign_v, fraction_down = check_bitangent_convention(positions, normals, tangents, uv, faces)
    log(f'bitangent points toward image-{"up" if sign_v > 0 else "DOWN"} '
        f'(fraction of faces pointing down: {fraction_down:.3f})')

    if args.stats_only:
        return

    import trimesh
    mesh = trimesh.Trimesh(positions, faces, process=False)
    log(f'ray engine: {type(mesh.ray).__module__}')
    if 'embree' not in type(mesh.ray).__module__:
        log('WARNING: no embree; the bake will be very slow')

    # ---- bake-resolution geometry pass ------------------------------------
    B = args.bake_size
    log(f'rasterising UVs at {B}...')
    tri_b, (rows_b, cols_b), bary_b = rasterize(uv, faces, B)
    mask_b = tri_b >= 0
    log(f'  {mask_b.mean() * 100:.1f}% of texels covered')
    P = interpolate(positions, faces, tri_b[rows_b, cols_b], bary_b)
    N = interpolate(normals, faces, tri_b[rows_b, cols_b], bary_b)
    N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-12)

    group_of, joint_names = joint_groups(glb)
    counts = {GROUPS[g]: int((group_of == g).sum()) for g in set(group_of.tolist())}
    log(f'  joint groups: {counts}')
    vgroups = vertex_group_weights(joints, weights, group_of)
    G = interpolate(vgroups, faces, tri_b[rows_b, cols_b], bary_b)
    vcurv = vertex_curvature(positions, normals, faces, extent)
    curvature = interpolate(vcurv[:, None], faces, tri_b[rows_b, cols_b], bary_b)[:, 0]

    log('measuring thickness...')
    thickness = measure_thickness(mesh, P, N, extent)

    rng = np.random.default_rng(args.seed)
    log(f'baking occlusion: {len(P)} texels x {args.ao_rays} rays, radius {args.ao_radius} extent')
    ao = bake_occlusion(mesh, P, N, extent, args.ao_rays, args.ao_radius, rng, log)

    def to_map(values, fill=0.0):
        img = np.full((B, B), fill, dtype=np.float32)
        img[rows_b, cols_b] = values
        return img

    ao_map = to_map(ao, 1.0)
    # The bake is noisy at this ray count; a small blur inside the islands is
    # cheaper than four times the rays and does not blur across seams.
    ao_map = np.where(mask_b, masked_gaussian(ao_map, mask_b, 1.0), 1.0).astype(np.float32)

    # ---- zones ------------------------------------------------------------
    def group(name):
        return G[:, GROUPS.index(name)]

    thin = 1.0 - smoothstep(args.thin_below, args.thin_below * 1.8, thickness)
    flat = 1.0 - smoothstep(args.flat_curvature, args.flat_curvature * 2.0, curvature)
    wing_owned = np.clip(group('finger') + group('wing') + 0.5 * group('tail'), 0, 1)
    membrane = thin * flat * wing_owned
    # A rounded membrane edge is thin and curved; a keratin spike is thin,
    # curved and NOT surrounded by membrane. Erode keratin away from membrane.
    membrane_map = to_map(membrane)
    membrane_near = np.where(mask_b, masked_gaussian(membrane_map, mask_b, 4.0), 0.0)[rows_b, cols_b]
    keratin = thin * (1.0 - flat) * (1.0 - np.clip(membrane_near * 4.0, 0, 1))
    keratin = np.clip(keratin + thin * (1.0 - flat) * group('toe'), 0, 1)  # claws are always keratin
    belly = smoothstep(-0.15, -0.6, N[:, 1]) * np.clip(group('body') + group('neck') + group('tail') + group('leg'), 0, 1)
    mouth = smoothstep(0.45, 0.2, ao) * np.clip(group('jaw') + 0.6 * group('head'), 0, 1) * (1.0 - keratin)

    hide = args.rough_hide
    rough = np.full(len(P), hide)
    rough = rough + (args.rough_belly - hide) * belly
    rough = rough + (args.rough_membrane - rough) * membrane
    rough = rough + (args.rough_keratin - rough) * keratin
    rough = rough + (args.rough_wet - rough) * mouth
    # crevices collect dust: rougher where occluded, less so on keratin
    rough = rough + 0.20 * (1.0 - ao) * (1.0 - 0.6 * keratin)
    rough_map = to_map(rough, hide)
    zone_map = to_map(membrane * 1.0 + keratin * 2.0 + belly * 3.0 * (1 - membrane) * (1 - keratin) + mouth * 4.0)
    cover = mask_b.sum()
    log(f'  zone coverage: membrane {membrane.sum() / cover * 100:.1f}%  keratin {keratin.sum() / cover * 100:.1f}%  '
        f'belly {belly.sum() / cover * 100:.1f}%  mouth {mouth.sum() / cover * 100:.1f}%')
    log(f'  bake AO: mean {ao.mean():.3f} p5 {np.percentile(ao, 5):.3f} p50 {np.percentile(ao, 50):.3f}')

    # ---- is there lighting baked into the base colour? --------------------
    luma_full = (0.299 * base[..., 0] + 0.587 * base[..., 1] + 0.114 * base[..., 2]).astype(np.float32)
    luma_b = resize(luma_full, B)[rows_b, cols_b]
    r_ao = np.corrcoef(luma_b, ao)[0, 1]
    r_up = np.corrcoef(luma_b, N[:, 1])[0, 1]
    log(f'  base-colour luminance vs baked AO: r = {r_ao:+.3f};  vs bind-pose up-facing: r = {r_up:+.3f}')
    log('  (a clearly positive r means the generator already painted occlusion/skylight into the colour)')

    # ---- full-resolution maps ---------------------------------------------
    log(f'rasterising UVs at {size} for the island mask...')
    tri_f, (rows_f, cols_f), _ = rasterize(uv, faces, size)
    mask_f = tri_f >= 0

    ao_full = np.clip(resize(fill_nearest(ao_map, mask_b), size), 0, 1)
    ao_full = 1.0 - args.ao_strength * (1.0 - ao_full)
    ao_full = np.maximum(ao_full, args.ao_floor)

    rough_full = resize(fill_nearest(rough_map, mask_b), size)
    # keep the generator's own roughness variation, at reduced weight, where it had any
    orig_rough = orig_orm[..., 1]
    residual = orig_rough - masked_gaussian(orig_rough, mask_f, 12.0)
    rough_full = rough_full + args.keep_rough_residual * np.where(mask_f, residual, 0.0)
    # scale centres read brighter than the grooves between them; polish them a little
    hp_fine = luma_full - masked_gaussian(luma_full, mask_f, 3.0)
    rough_full = rough_full - 0.35 * np.clip(hp_fine, -0.15, 0.15)
    rough_full = np.clip(rough_full, 0.2, 0.95).astype(np.float32)
    rough_full = fill_nearest(rough_full, mask_f)

    # ---- detail normal from the base colour ---------------------------------
    log('deriving detail normal from base-colour luminance...')
    height = np.zeros_like(luma_full)
    for sigma, weight in ((1.5, 0.55), (6.0, 0.45)):
        height += weight * (luma_full - masked_gaussian(luma_full, mask_f, sigma))
    height = np.where(mask_f, height, 0.0)
    height = fill_nearest(height, mask_f)
    # Pigment boundaries are steps in luminance but not in height. Soft-clip
    # the height so a hard colour edge cannot become a cliff, and take the
    # gradient over more than one texel so it cannot alias into a spike.
    height_scale = max(float(np.percentile(np.abs(height[mask_f]), 90)), 1e-6)
    height = np.tanh(height / (2.0 * height_scale)) * (2.0 * height_scale)
    height = ndimage.gaussian_filter(height, 0.8)
    gy, gx = np.gradient(height)  # gy: along rows (image down), gx: along columns
    # +X right; +Y toward image-up (glTF), which is -rows -- unless this file measured otherwise
    nx = -gx
    ny = sign_v * gy
    # The high-pass is meaningless within a couple of texels of an island edge
    # (half its neighbourhood is another island), and on islands only a few
    # texels wide it is nothing but edge. Fade the detail out there.
    inside = ndimage.distance_transform_edt(mask_f).astype(np.float32)
    edge_fade = smoothstep(0.5, 3.5, inside)
    nx *= edge_fade; ny *= edge_fade
    slope = np.sqrt(nx * nx + ny * ny)
    scale = args.normal_strength / max(float(np.percentile(slope[mask_f & (inside > 3.5)], 95)), 1e-6)
    nx *= scale; ny *= scale
    # Soft knee on the slope: the bulk of the detail stays linear, the few
    # hard colour edges compress toward a ceiling instead of clipping flat.
    mag = np.sqrt(nx * nx + ny * ny)
    knee = 1.8 * args.normal_strength
    compressed = knee * np.tanh(mag / knee)
    ratio = compressed / np.maximum(mag, 1e-9)
    nx *= ratio; ny *= ratio
    nz = np.sqrt(np.maximum(1.0 - nx * nx - ny * ny, 0.05))
    detail = np.stack([nx, ny, nz], -1)
    if args.detail_membrane != 1.0:
        membrane_full = resize(fill_nearest(membrane_map, mask_b), size)
        damp = 1.0 - (1.0 - args.detail_membrane) * membrane_full
        detail[..., 0] *= damp; detail[..., 1] *= damp
        detail[..., 2] = np.sqrt(np.maximum(1.0 - detail[..., 0] ** 2 - detail[..., 1] ** 2, 0.05))

    original = orig_normal * 2.0 - 1.0
    original /= np.maximum(np.linalg.norm(original, axis=-1, keepdims=True), 1e-6)
    # whiteout blend
    blended = np.stack([
        original[..., 0] * detail[..., 2] + detail[..., 0] * original[..., 2],
        original[..., 1] * detail[..., 2] + detail[..., 1] * original[..., 2],
        original[..., 2] * detail[..., 2]], -1)
    blended /= np.maximum(np.linalg.norm(blended, axis=-1, keepdims=True), 1e-6)
    new_normal = (blended * 0.5 + 0.5).astype(np.float32)
    new_normal = fill_nearest(new_normal, mask_f)

    new_orm = np.stack([ao_full, rough_full, orig_orm[..., 2]], -1).astype(np.float32)

    log('after:')
    log(channel_stats(new_normal, 'normal'))
    log(channel_stats(new_orm, 'orm'))

    # ---- diagnostics ---------------------------------------------------------
    if args.debug_dir:
        dbg = Path(args.debug_dir); dbg.mkdir(parents=True, exist_ok=True)
        Image.fromarray((np.clip(ao_full, 0, 1) * 255).astype(np.uint8)).resize((1024, 1024)).save(dbg / 'ao.png')
        Image.fromarray((rough_full * 255).astype(np.uint8)).resize((1024, 1024)).save(dbg / 'roughness.png')
        Image.fromarray((new_normal * 255).astype(np.uint8)).resize((1024, 1024)).save(dbg / 'normal.png')
        Image.fromarray((np.clip(to_map(thickness, 0) / (args.thin_below * 4), 0, 1) * 255).astype(np.uint8)).save(dbg / 'thickness.png')
        Image.fromarray((np.clip(to_map(curvature, 0) / (args.flat_curvature * 4), 0, 1) * 255).astype(np.uint8)).save(dbg / 'curvature.png')
        palette = np.array([[0.5, 0.5, 0.5], [0.2, 0.4, 1.0], [1.0, 0.85, 0.2], [0.3, 0.8, 0.3], [1.0, 0.2, 0.2]])
        zone_rgb = np.zeros((B, B, 3), dtype=np.float32) + 0.5
        zid = np.clip(np.rint(zone_map), 0, 4).astype(int)
        zone_rgb[mask_b] = palette[zid[mask_b]]
        Image.fromarray((zone_rgb * 255).astype(np.uint8)).save(dbg / 'zones.png')
        # a GLB whose base colour IS the zone map, to view the zones on the model
        zone_full = resize(fill_nearest(zone_rgb, mask_b), size)
        zone_glb = dbg / (src.stem + '-zones.glb')
        views = {doc['images'][base_image]['bufferView']: to_png_bytes(np.clip(zone_full, 0, 1) ** 2.2),
                 doc['images'][orm_image]['bufferView']: to_png_bytes(np.stack([np.ones_like(ao_full), np.full_like(ao_full, 0.7), np.zeros_like(ao_full)], -1)),
                 doc['images'][normal_image]['bufferView']: to_png_bytes(np.tile(np.array([0.5, 0.5, 1.0], np.float32), (size, size, 1)))}
        write_glb(glb, zone_glb, views)
        log(f'  wrote diagnostics to {dbg}')

    # ---- write and verify ------------------------------------------------------
    out = Path(args.output)
    log(f'encoding PNGs and writing {out}...')
    replaced = {doc['images'][normal_image]['bufferView']: to_png_bytes(new_normal),
                doc['images'][orm_image]['bufferView']: to_png_bytes(new_orm)}
    write_glb(glb, out, replaced)

    check = Glb(out)
    untouched = [i for i in range(len(doc['bufferViews'])) if i not in replaced]
    for i in untouched:
        assert check.view_bytes(i) == glb.view_bytes(i), f'bufferView {i} changed'
    stripped = lambda d: json.dumps({k: v for k, v in d.items() if k not in ('bufferViews', 'buffers')}, sort_keys=True)
    assert stripped(check.json) == stripped(doc), 'JSON changed beyond buffer layout'
    for i, v in enumerate(doc['bufferViews']):
        for key in v:
            if key not in ('byteOffset', 'byteLength'):
                assert check.json['bufferViews'][i].get(key) == v[key]
    log(f'verified: {len(untouched)} bufferViews byte-identical, JSON identical apart from buffer layout')
    log(f'{out}: {out.stat().st_size / 1e6:.1f} MB')


def smoothstep(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('glb', help='rigged, textured .glb (one primitive, one material)')
    parser.add_argument('-o', '--output', help='where to write the repaired .glb (never in place)')
    parser.add_argument('--stats-only', action='store_true', help='print the channel statistics and exit')
    parser.add_argument('--debug-dir', help='write AO, roughness, zone and thickness maps plus a zone-coloured .glb here')
    parser.add_argument('--bake-size', type=int, default=2048, help='resolution of the geometry bake (AO, zones); upsampled to the texture size')
    parser.add_argument('--ao-rays', type=int, default=64)
    parser.add_argument('--ao-radius', type=float, default=0.12, help='ray reach as a fraction of the model extent; beyond it a hit does not occlude')
    parser.add_argument('--ao-strength', type=float, default=1.0)
    parser.add_argument('--ao-floor', type=float, default=0.12)
    parser.add_argument('--thin-below', type=float, default=0.006, help='thickness (fraction of extent) under which geometry counts as thin')
    parser.add_argument('--flat-curvature', type=float, default=45.0, help='extent/radius under which geometry counts as flat')
    parser.add_argument('--rough-hide', type=float, default=0.68)
    parser.add_argument('--rough-belly', type=float, default=0.58)
    parser.add_argument('--rough-membrane', type=float, default=0.80)
    parser.add_argument('--rough-keratin', type=float, default=0.32)
    parser.add_argument('--rough-wet', type=float, default=0.30)
    parser.add_argument('--keep-rough-residual', type=float, default=0.3, help="weight of the generator's own roughness variation")
    parser.add_argument('--normal-strength', type=float, default=0.26, help='95th-percentile tangent-plane slope of the derived detail')
    parser.add_argument('--detail-membrane', type=float, default=0.7, help='multiplier on detail-normal strength over membrane')
    parser.add_argument('--seed', type=int, default=7)
    args = parser.parse_args()
    if not args.stats_only:
        if not args.output:
            parser.error('-o/--output is required unless --stats-only')
        if Path(args.output).resolve() == Path(args.glb).resolve():
            parser.error('refusing to overwrite the input; write to a new path')
    repair(args)


if __name__ == '__main__':
    main()
