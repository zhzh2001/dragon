"""Build the valley prey and inspect their exported skins in headless Blender."""

import argparse
import ctypes
import json
import math
from pathlib import Path
import struct
import sys

import bpy
import numpy as np

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import build_grazer as grazer
import build_rockkit as kit

CACHE = ROOT / "build-prey-cache"
OUT = ROOT / "assets/props"
DUR = grazer.DUR


def simplify_surface(pos, normal, faces):
    """Preserve skull ornaments, hoof tips and bound extremes during edge collapse."""
    from numpy.ctypeslib import ndpointer

    floats = ndpointer(dtype=np.float32, flags="C_CONTIGUOUS")
    uints = ndpointer(dtype=np.uint32, flags="C_CONTIGUOUS")
    size = ctypes.c_size_t
    library = Path(bpy.app.binary_path).resolve().parents[1] / "Resources/lib/libmeshoptimizer.dylib"
    simplify = ctypes.CDLL(str(library)).meshopt_simplifyWithAttributes
    simplify.argtypes = [
        uints,
        uints,
        size,
        floats,
        size,
        size,
        floats,
        size,
        floats,
        size,
        ctypes.c_void_p,
        size,
        ctypes.c_float,
        ctypes.c_uint,
        ctypes.POINTER(ctypes.c_float),
    ]
    simplify.restype = size
    height = (pos[:, 1] - pos[:, 1].min()) / np.ptp(pos[:, 1])
    region = np.flatnonzero((height > 0.62) | (height < 0.07))
    cells = np.floor(pos[region] * 22).astype(int)
    _, pinned = np.unique(cells, axis=0, return_index=True)
    locks = np.zeros(len(pos), dtype=np.uint8)
    locks[region[pinned]] = 1
    locks[pos.argmax(axis=0)] = locks[pos.argmin(axis=0)] = 1
    source = np.ascontiguousarray(faces, dtype=np.uint32).ravel()
    result = np.empty_like(source)
    count = simplify(
        result,
        source,
        len(source),
        pos,
        len(pos),
        12,
        normal,
        12,
        np.full(3, 0.08, dtype=np.float32),
        3,
        locks.ctypes.data,
        8900 * 3,
        1e10,
        32,
        None,
    )
    if count > 9000 * 3:
        raise ValueError(f"Reduction stopped at {count // 3} triangles.")
    used, index = np.unique(result[:count], return_inverse=True)
    points, first, weld = np.unique(pos[used], axis=0, return_index=True, return_inverse=True)
    tris = weld[index].reshape(-1, 3)
    _, unique = np.unique(np.sort(tris, axis=1), axis=0, return_index=True)
    return points, normal[used[first]], tris[np.sort(unique)]


def raw_surface(pos, faces):
    """Build a search surface for nearest-triangle colour transfer."""
    mesh = bpy.data.meshes.new("Source surface")
    mesh.vertices.add(len(pos))
    mesh.vertices.foreach_set("co", pos.ravel())
    mesh.loops.add(faces.size)
    mesh.loops.foreach_set("vertex_index", faces.astype(np.int32).ravel())
    mesh.polygons.add(len(faces))
    mesh.polygons.foreach_set("loop_start", np.arange(0, faces.size, 3, dtype=np.int32))
    mesh.update(calc_edges=True)
    obj = bpy.data.objects.new(mesh.name, mesh)
    bpy.context.collection.objects.link(obj)
    bpy.context.view_layer.update()
    return obj


def atlas(points, triangles):
    mesh = bpy.data.meshes.new("Prey atlas")
    mesh.from_pydata(points.tolist(), [], triangles.tolist())
    mesh.update()
    obj = bpy.data.objects.new(mesh.name, mesh)
    bpy.context.collection.objects.link(obj)
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(67), island_margin=0.003)
    bpy.ops.object.mode_set(mode="OBJECT")
    uv = np.empty((len(mesh.loops), 2), dtype=np.float32)
    mesh.uv_layers.active.data.foreach_get("uv", uv.ravel())
    uv[:, 1] = 1 - uv[:, 1]
    return uv.reshape(-1, 3, 2)


def rasterize(points, triangles, uv, size):
    """Interpolate a source-space location at each covered atlas texel."""
    locations = np.zeros((size, size, 3), np.float32)
    covered = np.zeros((size, size), bool)
    for tri, face_uv in zip(triangles, uv):
        a, b, c = face_uv * size - 0.5
        lower = np.maximum(np.ceil(np.minimum.reduce([a, b, c])).astype(int), 0)
        upper = np.minimum(np.floor(np.maximum.reduce([a, b, c])).astype(int), size - 1)
        x0, y0 = lower
        x1, y1 = upper + 1
        if x1 <= x0 or y1 <= y0:
            continue
        yy, xx = np.ogrid[y0:y1, x0:x1]
        e, f = b - a, c - a
        det = e[0] * f[1] - e[1] * f[0]
        if abs(det) < 1e-9:
            continue
        u = ((xx - a[0]) * f[1] - (yy - a[1]) * f[0]) / det
        v = (e[0] * (yy - a[1]) - e[1] * (xx - a[0])) / det
        inside = (u >= 0) & (v >= 0) & (u + v <= 1)
        p, q, r = points[tri]
        locations[y0:y1, x0:x1][inside] = p + u[inside, None] * (q - p) + v[inside, None] * (r - p)
        covered[y0:y1, x0:x1] |= inside
    return locations[covered], covered


def bake_colour(name, doc, binary, pos, normals, faces, source_uv, points, tris, uv):
    pbr = doc["materials"][0]["pbrMetallicRoughness"]
    texture = doc["textures"][pbr["baseColorTexture"]["index"]]["source"]
    blob, mime = kit.image_blob(doc, binary, texture)
    source_path = CACHE / (name + ("-source.jpg" if mime == "image/jpeg" else "-source.png"))
    source_path.write_bytes(blob)
    image = bpy.data.images.load(str(source_path), check_existing=False)
    image.colorspace_settings.name = "Non-Color"
    image.scale(2048, 2048)
    pixels = np.empty((2048, 2048, 4), np.float32)
    image.pixels.foreach_get(pixels.ravel())
    pixels[:, :, :3] = kit.srgb_to_linear(pixels[:, :, :3])
    bpy.data.images.remove(image)
    locations, covered = rasterize(points, tris, uv, 1024)
    source = raw_surface(pos, faces)
    surface = source.evaluated_get(bpy.context.evaluated_depsgraph_get())
    near = np.empty_like(locations)
    ids = np.empty(len(near), dtype=np.int32)
    for i, p in enumerate(locations):
        found, hit, _, ids[i] = surface.closest_point_on_mesh(p)
        if not found:
            raise ValueError("Colour transfer missed the source surface.")
        near[i] = hit
    vertices = faces[ids]
    a, b, c = pos[vertices].transpose(1, 0, 2)
    e, f, d = b - a, c - a, near - a

    def dot(p, q):
        return np.einsum("ij,ij->i", p, q)

    ee, ef, ff = dot(e, e), dot(e, f), dot(f, f)
    den = np.maximum(ee * ff - ef * ef, 1e-25)
    u = (ff * dot(d, e) - ef * dot(d, f)) / den
    v = (ee * dot(d, f) - ef * dot(d, e)) / den
    weights = np.clip(np.column_stack((1 - u - v, u, v)), 0, 1)
    weights /= weights.sum(axis=1, keepdims=True)
    sample = np.einsum("ij,ijk->ik", weights, source_uv[vertices])
    sample[:, 1] = 1 - sample[:, 1]
    xy = np.clip(sample * 2048 - 0.5, 0, 2046.999)
    x, y = xy.astype(int).T
    dx, dy = (xy - xy.astype(int)).T[:, :, None]
    rgb = (pixels[y, x, :3] * (1 - dx) + pixels[y, x + 1, :3] * dx) * (1 - dy)
    rgb += (pixels[y + 1, x, :3] * (1 - dx) + pixels[y + 1, x + 1, :3] * dx) * dy
    # Compare neighbouring coat samples to retain markings across different body regions.
    direction = np.einsum("ij,ijk->ik", weights, normals[vertices])
    direction /= np.maximum(np.linalg.norm(direction, axis=1, keepdims=True), 1e-8)
    cells = np.floor(near * 10).astype(int)
    _, group = np.unique(cells, axis=0, return_inverse=True)
    counts = np.bincount(group)

    def centered(values):
        return values - (np.bincount(group, weights=values) / counts)[group]

    design = np.column_stack([centered(col) for col in direction.T])
    luminance = np.log(np.maximum(rgb @ [0.2126, 0.7152, 0.0722], 0.008))
    coeff = np.linalg.lstsq(design, centered(luminance), rcond=None)[0]
    rgb *= np.exp(-np.clip(direction @ coeff, -0.5, 0.5))[:, None]
    baked = np.zeros((1024, 1024, 3), np.float32)
    baked[covered] = rgb
    baked, valid = kit.dilate_fill(baked, covered, 12)
    baked[~valid] = np.median(rgb, axis=0)
    rgba = np.ones((1024, 1024, 4), np.uint8) * 255
    rgba[:, :, :3] = np.rint(np.clip(kit.linear_to_srgb(baked), 0, 1) * 255).astype(np.uint8)
    return grazer.png(rgba), coeff


def prepare(name):
    CACHE.mkdir(exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    doc, binary = kit.read_glb(str(ROOT / "assets/tripo-raw" / f"tripo-{name}-01.glb"))
    prim = doc["meshes"][0]["primitives"][0]
    pos, normal, source_uv = [
        kit.accessor(doc, binary, prim["attributes"][key]) for key in ("POSITION", "NORMAL", "TEXCOORD_0")
    ]
    faces = kit.accessor(doc, binary, prim["indices"]).reshape(-1, 3)
    points, normals, tris = simplify_surface(pos, normal, faces)
    print(f"{name}: reduced {len(faces)} triangles to {len(tris)}.", flush=True)
    uv = atlas(points, tris)
    tex, coeff = bake_colour(name, doc, binary, pos, normal, faces, source_uv, points, tris, uv)
    np.savez(
        CACHE / f"{name}.npz",
        points=points,
        normals=normals,
        tris=tris,
        uv=uv,
        tex=np.frombuffer(tex, dtype=np.uint8),
        light=coeff,
    )
    print(f"{name}: baked colour. Directional coefficients {coeff}.", flush=True)


# These affine transforms come from the raw meshes, including their minimum hoof height.
TRANSFORMS = {
    "elk": (
        [[0, 0, -1], [0, 1, 0], [1, 0, 0]],
        [-0.00007724761962890625, -0.47857666015625, -0.13264923095703124],
        2.449333499439392,
    ),
    "bighorn": (
        [
            [-0.8192774038482568, 0, -0.5733973626933249],
            [0, 1, 0],
            [0.5733973626933249, 0, -0.8192774038482568],
        ],
        [0.03126970415575292, -0.489715576171875, -0.08687504571989513],
        1.6881689483696203,
    ),
    "boar": (
        [[0, 0, -1], [0, 1, 0], [1, 0, 0]],
        [-0.00038814544677734375, -0.270751953125, -0.0392669677734375],
        1.6334995014955136,
    ),
    "chamois": (
        [[1, 0, 0], [0, 1, 0], [0, 0, 1]],
        [0.00037860870361328125, -0.488311767578125, -0.0952871322631836],
        1.2276874082862406,
    ),
    "goat": (
        [
            [0.7458622171674406, 0, -0.6661002574703524],
            [0, 1, 0],
            [0.6661002574703524, 0, 0.7458622171674406],
        ],
        [-0.027487922704218185, -0.444976806640625, -0.10751779515292349],
        1.1311177703821973,
    ),
}
SPECS = {
    "elk": dict(name="elk", length=2.4, shoulder=1.54, mouth=(0, 1.365, 1.50)),
    "bighorn": dict(name="bighorn", length=1.7, shoulder=1.23, mouth=(-0.37, 1.18, 0.792)),
    "boar": dict(name="boar", length=1.6, shoulder=0.86, mouth=(0.04, 0.399, 0.835)),
    "chamois": dict(name="chamois", length=1.2, shoulder=0.92, mouth=(0.123, 0.85, 0.694)),
    "goat": dict(name="goat", length=1.4, shoulder=0.88, mouth=(0.107, 0.61, 0.774)),
}
# Root, two spine joints, chest, two neck joints, skull, and tail joints.
AXIAL = {
    "elk": [
        (0, 1.18, -0.54),
        (0, 1.20, -0.22),
        (0, 1.23, 0.12),
        (0, 1.26, 0.43),
        (0, 1.30, 0.68),
        (0, 1.45, 0.91),
        (0, 1.60, 1.14),
        (0, 1.35, -0.69),
        (0, 1.29, -0.79),
        (0, 1.22, -0.84),
    ],
    "bighorn": [
        (0, 0.97, -0.43),
        (0, 0.99, -0.19),
        (0, 1.00, 0.06),
        (-0.025, 1.01, 0.31),
        (-0.045, 1.12, 0.45),
        (-0.07, 1.25, 0.59),
        (-0.09, 1.36, 0.70),
        (0.02, 1.08, -0.56),
        (0.02, 1.03, -0.65),
        (0.02, 0.96, -0.69),
    ],
    "boar": [
        (0, 0.58, -0.37),
        (0, 0.59, -0.15),
        (0, 0.62, 0.08),
        (0, 0.63, 0.28),
        (0, 0.61, 0.42),
        (0, 0.56, 0.53),
        (0, 0.53, 0.64),
        (0, 0.66, -0.52),
        (0, 0.49, -0.59),
        (0, 0.30, -0.68),
    ],
    "chamois": [
        (0, 0.72, -0.28),
        (0, 0.72, -0.12),
        (0, 0.73, 0.07),
        (0, 0.74, 0.23),
        (0.015, 0.79, 0.35),
        (0.045, 0.88, 0.44),
        (0.09, 0.96, 0.54),
        (0, 0.80, -0.34),
        (0, 0.78, -0.41),
        (0, 0.78, -0.47),
    ],
    "goat": [
        (0, 0.63, -0.32),
        (0, 0.64, -0.13),
        (0, 0.65, 0.08),
        (0, 0.67, 0.28),
        (0.005, 0.73, 0.41),
        (0.01, 0.79, 0.52),
        (0.02, 0.79, 0.64),
        (0, 0.73, -0.43),
        (0.01, 0.72, -0.52),
        (0.02, 0.75, -0.58),
    ],
}
# Leg entries contain attachment, elbow or stifle, wrist or hock, and hoof heights.
LEGS = {
    "elk": dict(
        front=[(1.21, 0.55), (0.82, 0.54), (0.42, 0.56), (0.105, 0.58)],
        hind=[(1.14, -0.55), (0.86, -0.49), (0.59, -0.76), (0.105, -0.64)],
        width=(0.18, 0.24),
        belly=0.83,
    ),
    "bighorn": dict(
        front=[(0.99, 0.41), (0.65, 0.38), (0.34, 0.40), (0.075, 0.42)],
        hind=[(0.96, -0.42), (0.69, -0.34), (0.41, -0.56), (0.075, -0.48)],
        width=(0.16, 0.23),
        belly=0.64,
    ),
    "boar": dict(
        front=[(0.53, 0.32), (0.30, 0.30), (0.15, 0.34), (0.045, 0.35)],
        hind=[(0.52, -0.39), (0.31, -0.33), (0.19, -0.47), (0.045, -0.41)],
        width=(0.12, 0.15),
        belly=0.29,
    ),
    "chamois": dict(
        front=[(0.73, 0.25), (0.48, 0.23), (0.25, 0.24), (0.065, 0.25)],
        hind=[(0.72, -0.28), (0.52, -0.22), (0.34, -0.38), (0.075, -0.31)],
        width=(0.12, 0.135),
        belly=0.51,
    ),
    "goat": dict(
        front=[(0.67, 0.28), (0.44, 0.26), (0.24, 0.28), (0.065, 0.29)],
        hind=[(0.64, -0.32), (0.44, -0.26), (0.27, -0.43), (0.065, -0.35)],
        width=(0.145, 0.16),
        belly=0.42,
    ),
}


def canonicalize(name, raw_xyz):
    """Apply the fixed source transform to a full cloud or any subset."""
    matrix, origin, scale = TRANSFORMS[name]
    return (np.asarray(raw_xyz) @ np.asarray(matrix).T - origin) * scale


def _smooth(a, b, value):
    t = np.clip((value - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def skeleton(name, coords):
    """Place limb joints inside measured cross sections of each separate leg."""
    p = np.asarray(coords)
    spec = LEGS[name]
    h = SPECS[name]["shoulder"]
    names = [
        "root",
        "spine_01",
        "spine_02",
        "chest",
        "neck_01",
        "neck_02",
        "head",
        "tail_01",
        "tail_02",
        "tail_03",
    ]
    parents = [-1, 0, 1, 2, 3, 4, 5, 0, 7, 8]
    joints = [list(v) for v in AXIAL[name]]
    for side, sign in [("l", 1), ("r", -1)]:
        for kind, stems, par in [
            ("front", ("upper_arm", "forearm", "wrist", "hand"), 3),
            ("hind", ("thigh", "shin", "ankle", "foot"), 0),
        ]:
            front = kind == "front"
            rows = spec[kind]
            width = spec["width"][0 if front else 1]
            cloud = p[(sign * p[:, 0] > h * 0.025) & ((p[:, 2] > 0) if front else (p[:, 2] < 0))]
            sole = cloud[cloud[:, 1] < h * 0.07]
            hoofxz = np.median(sole[:, [0, 2]], axis=0)
            for k, (y, z) in enumerate(rows):
                x = sign * width
                if k > 0:
                    band = cloud[abs(cloud[:, 1] - y) < h * 0.025]
                    if len(band) > 3 and y < spec["belly"] * 0.96:
                        x, z = np.median(band[:, [0, 2]], axis=0)
                    else:
                        x = hoofxz[0]
                        z += hoofxz[1] - rows[-1][1]
                if k == 3:
                    x, z = hoofxz
                names.append(stems[k] + "_" + side)
                parents.append(par)
                joints.append([x, y, z])
                par = len(names) - 1
    return names, np.asarray(parents, dtype=int), np.asarray(joints, dtype=float)


def _chain_weights(p, chain, joints, count):
    """Blend adjacent joints along the nearest segment of an anatomical chain."""
    a = joints[chain[:-1]]
    b = joints[chain[1:]]
    d = b - a
    t = np.clip(np.einsum("vsi,si->vs", p[:, None, :] - a, d) / np.sum(d * d, axis=1), 0, 1)
    closest = a + t[:, :, None] * d
    distance = np.sum((p[:, None, :] - closest) ** 2, axis=2)
    seg = distance.argmin(axis=1)
    u = t[np.arange(len(p)), seg]
    weights = np.zeros((len(p), count))
    weights[np.arange(len(p)), np.asarray(chain)[seg]] = 1 - u
    weights[np.arange(len(p)), np.asarray(chain)[seg + 1]] += u
    return weights


def _limb_weights(p, ids, joints, count):
    """Keep each segment on its parent bone, with short blends around each joint."""
    w = np.zeros((len(p), count))
    w[:, ids[0]] = 1
    for previous, current in zip(ids[:-1], ids[1:]):
        length = np.linalg.norm(joints[current] - joints[previous])
        radius = max(0.012, length * 0.16)
        below = 1 - _smooth(joints[current, 1] - radius, joints[current, 1] + radius, p[:, 1])
        w[:, previous] -= below
        w[:, current] += below
    return np.maximum(w, 0)


def fit(name, coords, triangles):
    """Return names, parents, identity-bind joint positions, and normalized weights."""
    p = np.asarray(coords, dtype=float)
    names, parents, joints = skeleton(name, p)
    n = len(names)
    h = SPECS[name]["shoulder"]
    spec = LEGS[name]
    body = _chain_weights(p, list(range(7)), joints, n)
    weight = body.copy()
    locked = np.zeros(len(p), dtype=bool)
    for side, sign in [("l", 1), ("r", -1)]:
        for front, stems in [
            (True, ("upper_arm", "forearm", "wrist", "hand")),
            (False, ("thigh", "shin", "ankle", "foot")),
        ]:
            ids = [names.index(s + "_" + side) for s in stems]
            a = joints[ids[0]]
            limb = _limb_weights(p, ids, joints, n)
            sidegate = _smooth(h * 0.025, h * 0.12, sign * p[:, 0])
            attach = 1 - _smooth(spec["belly"] * 0.94, a[1] + h * 0.04, p[:, 1])
            zg = 1 - _smooth(h * 0.065, h * 0.19, abs(p[:, 2] - a[2]))
            upper = sidegate * attach * zg
            # Below the belly, each separated leg owns its entire cross section.
            lower = 1 - _smooth(spec["belly"] * 0.78, spec["belly"] * 0.98, p[:, 1])
            region = ((p[:, 2] > 0) if front else (p[:, 2] < 0)) & (sign * p[:, 0] > 0)
            amount = np.maximum(lower, upper) * region
            weight = weight * (1 - amount[:, None]) + limb * amount[:, None]
            sole = region & (p[:, 1] < joints[ids[-1], 1] * 0.80)
            weight[sole] = 0
            weight[sole, ids[-1]] = 1
            locked |= sole
    # A tail uses its own narrow rear region, separate from the hind legs.
    tail = joints[7:10]
    tail_weight = _chain_weights(p, [7, 8, 9], joints, n)
    tailz = 1 - _smooth(tail[0, 2] - 0.025 * h, tail[0, 2] + 0.055 * h, p[:, 2])
    tailx = 1 - _smooth(h * 0.035, h * 0.09, abs(p[:, 0] - tail[0, 0]))
    taily = _smooth(min(tail[:, 1]) - 0.10 * h, min(tail[:, 1]) - 0.035 * h, p[:, 1])
    tailmask = tailz * tailx * taily
    weight = weight * (1 - tailmask[:, None]) + tail_weight * tailmask[:, None]
    x, y, z = p.T
    tangent = joints[6] - joints[5]
    tangent /= np.linalg.norm(tangent)
    rigid = (p - joints[6]) @ tangent > -0.035 * h
    if name == "elk":
        rigid |= (y > 1.63) & (z > 0.48)
    elif name == "bighorn":
        rigid |= (
            ((y > 1.235) & (z > 0.22))
            | ((z > 0.69) & (y > 1.105))
            | ((y > 1.207) & (x < -0.18) & (z > 0.32) & (z < 0.48))
        )
    elif name == "chamois":
        rigid |= y > 0.96
    elif name == "goat":
        rigid |= ((y > 0.855) & (z > 0.35)) | (y > 0.90)
    weight[rigid] = 0
    weight[rigid, names.index("head")] = 1
    locked |= rigid
    # Diffuse attachment transitions across the welded surface, while retaining rigid endpoints.
    tri = np.asarray(triangles, dtype=int).reshape(-1, 3)
    if len(tri):
        _, first, inv = np.unique(np.round(p, 7), axis=0, return_index=True, return_inverse=True)
        wp = p[first]
        ww = weight[first]
        fixed = locked[first]
        faces = inv[tri]
        edges = np.unique(
            np.sort(np.concatenate((faces[:, [0, 1]], faces[:, [1, 2]], faces[:, [2, 0]])), axis=1), axis=0
        )
        edges = edges[edges[:, 0] != edges[:, 1]]
        src = np.r_[edges[:, 0], edges[:, 1]]
        dst = np.r_[edges[:, 1], edges[:, 0]]
        factor = 1 / np.maximum(np.linalg.norm(wp[src] - wp[dst], axis=1), h * 0.008)
        divisor = np.bincount(src, weights=factor, minlength=len(wp))
        for _ in range(5):
            accum = np.zeros_like(ww)
            np.add.at(accum, src, ww[dst] * factor[:, None])
            mean = accum / np.maximum(divisor[:, None], 1e-12)
            candidate = ww * 0.55 + mean * 0.45
            ww[~fixed] = candidate[~fixed]
        weight = ww[inv]
    top = np.argsort(weight, axis=1)[:, -4:]
    keep = np.zeros_like(weight)
    np.put_along_axis(keep, top, np.take_along_axis(weight, top, axis=1), axis=1)
    keep /= keep.sum(axis=1, keepdims=True)
    return names, parents, joints, keep


def planar(vector):
    return np.array([vector[2], vector[1]])


def direction(angle):
    return np.array([math.cos(angle), math.sin(angle)])


def angle(vector):
    return math.atan2(vector[1], vector[0])


def wrap(value):
    return (value + math.pi) % (2 * math.pi) - math.pi


def sagittal_world(rest, parents, angles):
    """Propagate pitch rotations in the plane through the spine and legs."""
    origin = np.empty_like(rest)
    pitch = np.empty(len(rest))
    for i, parent in enumerate(parents):
        if parent < 0:
            origin[i] = rest[i]
            pitch[i] = angles[i]
        else:
            pitch[i] = pitch[parent] + angles[i]
            origin[i] = origin[parent] + grazer.rx(pitch[parent]) @ (rest[i] - rest[parent])
    return origin, pitch


def leg_pose(rest, ids, pivot, parent_pitch, ankle, front, fold):
    """Solve three links to the hoof and counter-rotate its sole."""
    links = np.diff(rest[ids][:, [2, 1]], axis=0)
    lengths = np.linalg.norm(links, axis=1)
    rest_angles = np.array([angle(link) for link in links])
    rest_line = angle(planar(rest[ids[-1]] - rest[ids[0]]))
    reach = ankle - planar(pivot)
    line_angle = angle(reach)
    distance = min(np.linalg.norm(reach), lengths.sum() - 1e-6)
    # Select a distal angle that the two upper links can reach.
    l1, l2, l3 = lengths
    limit = math.acos(np.clip((distance**2 + l3**2 - (l1 + l2) ** 2) / (2 * distance * l3), -1, 1))
    bend = wrap(rest_angles[2] - rest_line) + (-1 if front else 1) * fold
    distal_angle = line_angle + np.clip(bend, -limit, limit)
    wrist = distance * direction(line_angle) - l3 * direction(distal_angle)
    radius = np.clip(np.linalg.norm(wrist), abs(l1 - l2) + 1e-7, l1 + l2 - 1e-7)
    elbow = math.acos(np.clip((radius**2 + l1**2 - l2**2) / (2 * radius * l1), -1, 1))
    # An elbow folds aft. A stifle folds toward the muzzle.
    first = angle(wrist) + (-elbow if front else elbow)
    second = angle(wrist - l1 * direction(first))
    absolute = [wrap(a - b) for a, b in zip(rest_angles, [first, second, distal_angle])]
    return [
        wrap(absolute[0] - parent_pitch),
        wrap(absolute[1] - absolute[0]),
        wrap(absolute[2] - absolute[1]),
        -absolute[2],
    ]


def set_body_height(angles, names, rest, dy):
    lift, pelvis = names.index("body_height"), names.index("pelvis")
    angles[lift] = math.asin(dy / (rest[lift, 2] - rest[pelvis, 2]))
    angles[pelvis] = -angles[lift]


def grazing_turn(name):
    return math.radians({"bighorn": 48, "chamois": -15, "goat": -20}.get(name, 0))


def turn_y(angle):
    c, s = math.cos(angle), math.sin(angle)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def grazing_pitch(spec, names, parents, rest):
    """Find the smallest shared bend that brings the mouth to the turf."""
    joints = [names.index(n) for n in ("spine_01", "spine_02", "chest", "neck_01", "neck_02", "head")]
    target = 0.046 if spec["name"] == "bighorn" else 0.012 * spec["length"]
    head = names.index("head")
    mouth = np.array(spec["mouth"]) - rest[head]
    # Distribute the reach along the back and both neck joints.
    profile = np.radians([15, 9, 10, 48, 33, -32])
    drop = {"elk": 0.13, "bighorn": 0.27, "boar": 0.06, "chamois": 0.25, "goat": 0.20}[spec["name"]]

    def pose(amount):
        angles = np.zeros(len(names))
        angles[joints] = amount * profile
        set_body_height(angles, names, rest, -amount * drop * spec["shoulder"])
        pos, pitch = sagittal_world(rest, parents, angles)
        # Turn the off-axis muzzle toward the grass as the neck lowers.
        neck = parents[head]
        turn = grazing_turn(spec["name"])
        neck_basis = grazer.rx(pitch[neck]) @ turn_y(0.3 * turn)
        head_origin = pos[neck] + neck_basis @ (rest[head] - rest[neck])
        head_basis = neck_basis @ grazer.rx(angles[head]) @ turn_y(0.7 * turn)
        height = (head_origin + head_basis @ mouth)[1]
        return height, angles

    prev = 0.0
    for amount in np.linspace(0.02, 1.9, 95):
        if pose(amount)[0] <= target:
            for _ in range(22):
                mid = (prev + amount) * 0.5
                if pose(mid)[0] > target:
                    prev = mid
                else:
                    amount = mid
            return pose(amount)[1]
        prev = amount
    raise ValueError(f"{spec['name']} could not reach the turf.")


def bake_clips(spec, names, parents, rest, points, weights):
    deep = grazing_pitch(spec, names, parents, rest)
    chains = []
    for side in ("l", "r"):
        for front, stems in [
            (True, ("upper_arm", "forearm", "wrist", "hand")),
            (False, ("thigh", "shin", "ankle", "foot")),
        ]:
            ids = [names.index(stem + "_" + side) for stem in stems]
            sole = points[weights[:, ids[-1]] > 0.55, 1].min()
            chains.append((side, front, ids, sole))
    clips = {}
    for clip, count in [("graze", 121), ("walk", 73), ("run", 67)]:
        keys = []
        for frame in range(count - 1):
            phase = frame / (count - 1)
            t = math.tau * phase
            pitch = np.zeros(len(names))
            yaw = np.zeros(len(names))
            if clip == "graze":
                lower = (1 - math.cos(t)) * 0.5
                pitch = deep * lower
                turn = grazing_turn(spec["name"]) * lower
                yaw[names.index("neck_02")] = 0.3 * turn
                yaw[names.index("head")] = 0.7 * turn + lower * 0.065 * math.sin(3 * t)
                if spec["name"] == "boar":
                    pitch[names.index("head")] -= lower * 0.04 * (1 + math.cos(5 * t))
                    yaw[names.index("neck_02")] = lower * 0.07 * math.sin(3 * t)
            elif clip == "walk":
                set_body_height(pitch, names, rest, -spec["shoulder"] * (0.033 + 0.009 * math.cos(2 * t)))
                pitch[names.index("chest")] = 0.022 * math.sin(2 * t)
                pitch[names.index("neck_01")] = 0.035 * math.sin(2 * t + 0.4)
                pitch[names.index("neck_02")] = 0.025 * math.sin(2 * t + 0.6)
            else:
                set_body_height(pitch, names, rest, -spec["shoulder"] * (0.043 + 0.027 * math.sin(2 * t)))
                pitch[names.index("spine_02")] = 0.048 * math.sin(t)
                pitch[names.index("chest")] = 0.064 * math.sin(t - 0.45)
                pitch[names.index("neck_01")] = -0.09 + 0.055 * math.sin(t - 0.7)
                pitch[names.index("neck_02")] = -0.08 + 0.04 * math.sin(t - 0.9)
                pitch[names.index("head")] = 0.04
            for i, name in enumerate(names):
                if name.startswith("tail_"):
                    segment = int(name[-2:])
                    yaw[i] = 0.11 * math.sin(t - 0.42 * segment)
                    if clip == "run":
                        pitch[i] = 0.10 + 0.055 * math.sin(t - 0.5 * segment)
            pos, cumulative = sagittal_world(rest, parents, pitch)
            for side, front, ids, sole in chains:
                leg_length = rest[ids[0], 1] - rest[ids[-1], 1]
                stride = lift = fold = 0.0
                if clip == "graze":
                    # A staggered fore stance leaves space for the lowering chest.
                    stride = leg_length * ((0.20 if side == "l" else -0.12) if front else 0.0)
                    fold = (0.75 if front else 0.15) * lower
                else:
                    if clip == "walk":
                        offset = (0.5 if side == "r" else 0) + (0.25 if front else 0)
                        duty, sweep, clearance = 0.66, 0.19, 0.13
                    else:
                        offset = (0.035 if side == "r" else 0) + (0.49 if front else 0)
                        duty, sweep, clearance = 0.28, 0.34, 0.24
                    ph = (phase - offset) % 1
                    if ph < duty:
                        stride = sweep * (1 - 2 * ph / duty)
                    else:
                        swing = (ph - duty) / (1 - duty)
                        stride = -sweep * math.cos(math.pi * swing)
                        lift = clearance * math.sin(math.pi * swing) ** 1.35
                        fold = (0.37 if clip == "walk" else 0.58) * math.sin(math.pi * swing)
                    stride *= leg_length
                    lift *= leg_length
                target = planar(rest[ids[-1]]) + [stride, lift + 0.003 - sole]
                pitch[ids] = leg_pose(
                    rest, ids, pos[ids[0]], cumulative[parents[ids[0]]], target, front, fold
                )
            # Compose local pitch and yaw into glTF quaternions in XYZW order.
            sx, cx = np.sin(pitch * 0.5), np.cos(pitch * 0.5)
            sy, cy = np.sin(yaw * 0.5), np.cos(yaw * 0.5)
            keys.append(np.column_stack((sx * cy, cx * sy, sx * sy, cx * cy)))
        keys.append(keys[0].copy())
        clips[clip] = np.array(keys, np.float32)
    return clips


def export(path, name, points, normals, triangles, uvs, tex, names, parents, rest, weights, clips):
    """Write indexed geometry and baked local rotations in the grazer coordinate system."""
    binary = bytearray()
    views, accessors = [], []

    def view(blob):
        binary.extend(b"\0" * (-len(binary) % 4))
        views.append(dict(buffer=0, byteOffset=len(binary), byteLength=len(blob)))
        binary.extend(blob)
        return len(views) - 1

    def accessor(values, kind, component=5126):
        data = np.asarray(values, dtype={5126: "<f4", 5123: "<u2"}[component])
        entry = dict(bufferView=view(data.tobytes()), componentType=component, count=len(data), type=kind)
        if kind != "MAT4":
            columns = data.reshape(len(data), -1)
            entry.update(min=columns.min(axis=0).tolist(), max=columns.max(axis=0).tolist())
        accessors.append(entry)
        return len(accessors) - 1

    seams, indices = np.unique(
        np.column_stack((triangles.ravel(), uvs.reshape(-1, 2))), axis=0, return_inverse=True
    )
    vi = seams[:, 0].astype(int)
    joints = np.argsort(-weights, axis=1)[:, :4]
    w = np.take_along_axis(weights, joints, axis=1)
    w /= w.sum(axis=1, keepdims=True)
    attrs = dict(
        POSITION=accessor(points[vi], "VEC3"),
        NORMAL=accessor(normals[vi], "VEC3"),
        TEXCOORD_0=accessor(seams[:, 1:], "VEC2"),
        JOINTS_0=accessor(joints[vi], "VEC4", 5123),
        WEIGHTS_0=accessor(w[vi], "VEC4"),
    )
    primitive = dict(attributes=attrs, indices=accessor(indices.ravel(), "SCALAR", 5123), material=0)
    nodes = []
    for i, parent in enumerate(parents):
        offset = rest[i] - rest[parent] if parent >= 0 else rest[i]
        node = dict(name=names[i], translation=offset.tolist())
        children = [j for j, p in enumerate(parents) if p == i]
        if children:
            node["children"] = children
        nodes.append(node)
    nodes.append(dict(name="prey_" + name, mesh=0, skin=0))
    animations = []
    for clip, keys in clips.items():
        time = accessor(np.linspace(0, DUR[clip], len(keys)), "SCALAR")
        samplers, channels = [], []
        for i in range(len(names)):
            if np.all(keys[:, i] == (0, 0, 0, 1)):
                continue
            samplers.append(dict(input=time, output=accessor(keys[:, i], "VEC4"), interpolation="LINEAR"))
            channels.append(dict(sampler=len(samplers) - 1, target=dict(node=i, path="rotation")))
        animations.append(dict(name=clip, samplers=samplers, channels=channels))
    inv = np.tile(np.eye(4), (len(names), 1, 1))
    inv[:, :3, 3] = -rest
    skin = dict(
        joints=list(range(len(names))),
        skeleton=0,
        inverseBindMatrices=accessor(inv.transpose(0, 2, 1).reshape(-1, 16), "MAT4"),
    )
    image = dict(mimeType="image/png", bufferView=view(tex))
    pbr = dict(baseColorTexture=dict(index=0), metallicFactor=0, roughnessFactor=0.88)
    doc = dict(
        asset=dict(version="2.0", generator="tools/build_prey.py"),
        scene=0,
        scenes=[dict(nodes=[0, len(names)])],
        nodes=nodes,
        meshes=[dict(name=name, primitives=[primitive])],
        skins=[skin],
        animations=animations,
        materials=[dict(name=name + " coat", pbrMetallicRoughness=pbr)],
        textures=[dict(source=0)],
        images=[image],
        accessors=accessors,
        bufferViews=views,
        buffers=[dict(byteLength=len(binary))],
    )
    kit.write_glb(str(path), doc, binary)


def build(name, cached):
    if not cached:
        prepare(name)
    data = np.load(CACHE / f"{name}.npz")
    points = canonicalize(name, data["points"])
    basis = canonicalize(name, np.eye(3)) - canonicalize(name, np.zeros((1, 3)))
    normals = data["normals"] @ np.linalg.inv(basis).T
    normals /= np.linalg.norm(normals, axis=1, keepdims=True)
    tris = data["tris"]
    names, parents, rest, weights = fit(name, points, tris)
    # Two opposing rotations lower the pelvis while the root stays fixed.
    # Both pairs of legs follow the pelvis during the crouch.
    pelvis = np.asarray(rest[0])
    lift = pelvis + [0, 0, SPECS[name]["length"] * 0.52]
    names = ["root", "body_height", "pelvis", *names[1:]]
    parents = [-1, 0, 1, *(parents[1:] + 2)]
    rest = np.vstack(([0, 0, 0], lift, rest))
    weights = np.pad(weights, ((0, 0), (2, 0)))
    clips = bake_clips(SPECS[name], names, parents, rest, points, weights)
    path = OUT / f"prey_{name}.glb"
    export(
        path,
        name,
        points,
        normals,
        tris,
        data["uv"],
        data["tex"].tobytes(),
        names,
        parents,
        rest,
        weights,
        clips,
    )


class PreyGLB:
    def __init__(self, path):
        raw = Path(path).read_bytes()
        assert len(raw) >= 28, "GLB header was truncated"
        magic, version, size = struct.unpack_from("<III", raw)
        assert (magic, version, size) == (0x46546C67, 2, len(raw)), "Invalid GLB header"
        chunks = []
        offset = 12
        while offset < len(raw):
            count, kind = struct.unpack_from("<II", raw, offset)
            assert count % 4 == 0 and offset + 8 + count <= len(raw)
            chunks.append((kind, raw[offset + 8 : offset + 8 + count]))
            offset += 8 + count
        assert [c[0] for c in chunks] == [0x4E4F534A, 0x004E4942]
        self.j = json.loads(chunks[0][1])
        self.b = chunks[1][1]
        assert self.j["asset"]["version"] == "2.0"
        assert len(self.j["buffers"]) == 1 and "uri" not in self.j["buffers"][0]
        assert 0 <= len(self.b) - self.j["buffers"][0]["byteLength"] <= 3

    def acc(self, index):
        a = self.j["accessors"][index]
        assert "sparse" not in a, "Sparse accessors are outside the prey export contract"
        view = self.j["bufferViews"][a["bufferView"]]
        assert view["buffer"] == 0
        dtype = np.dtype(
            {5120: "i1", 5121: "u1", 5122: "<i2", 5123: "<u2", 5125: "<u4", 5126: "<f4"}[a["componentType"]]
        )
        width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[a["type"]]
        start = view.get("byteOffset", 0) + a.get("byteOffset", 0)
        stride = view.get("byteStride", width * dtype.itemsize)
        end = start + (a["count"] - 1) * stride + width * dtype.itemsize
        assert a["count"] > 0 and stride >= width * dtype.itemsize
        assert end <= view.get("byteOffset", 0) + view["byteLength"] <= len(self.b)
        result = np.ndarray(
            (a["count"], width), dtype=dtype, buffer=self.b, offset=start, strides=(stride, dtype.itemsize)
        ).copy()
        if a.get("normalized", False):
            assert dtype.kind in "iu"
            result = result.astype(float) / np.iinfo(dtype).max
            if dtype.kind == "i":
                result = np.maximum(result, -1)
        return result


def prey_rotation(q):
    x, y, z, w = q
    return np.array(
        [
            [1 - 2 * y * y - 2 * z * z, 2 * x * y - 2 * z * w, 2 * x * z + 2 * y * w],
            [2 * x * y + 2 * z * w, 1 - 2 * x * x - 2 * z * z, 2 * y * z - 2 * x * w],
            [2 * x * z - 2 * y * w, 2 * y * z + 2 * x * w, 1 - 2 * x * x - 2 * y * y],
        ]
    )


def verify(path, spec):
    """Return metrics. Raise AssertionError when an exported contract check fails."""
    glb = PreyGLB(path)
    data = glb.j
    assert not data.get("cameras") and not data.get("extensions", {}).get("KHR_lights_punctual")
    assert len(data["meshes"]) == len(data["skins"]) == len(data["materials"]) == 1
    assert len(data["meshes"][0]["primitives"]) == 1
    primitive = data["meshes"][0]["primitives"][0]
    assert primitive.get("mode", 4) == 4 and primitive["material"] == 0
    assert not primitive.get("targets")
    attrs = primitive["attributes"]
    assert set(attrs) == {"POSITION", "NORMAL", "TEXCOORD_0", "JOINTS_0", "WEIGHTS_0"}
    position = glb.acc(attrs["POSITION"]).astype(float)
    weights = glb.acc(attrs["WEIGHTS_0"]).astype(float)
    joint_values = glb.acc(attrs["JOINTS_0"])
    assert joint_values.dtype.kind in "iu"
    joints = joint_values.astype(int)
    normals = glb.acc(attrs["NORMAL"])
    uv = glb.acc(attrs["TEXCOORD_0"])
    assert position.shape[1] == 3 and normals.shape == position.shape
    assert uv.shape == (len(position), 2)
    assert joints.shape == weights.shape == (len(position), 4)
    assert np.max(abs(np.linalg.norm(normals, axis=1) - 1)) < 0.01
    assert np.all(weights >= 0) and np.max(abs(weights.sum(1) - 1)) < 1e-5
    indices = glb.acc(primitive["indices"]).ravel()
    assert indices.dtype.kind in "iu" and len(indices) % 3 == 0
    assert indices.min() >= 0 and indices.max() < len(position)
    triangles = len(indices) // 3
    assert 0 < triangles <= 9000, ("triangle budget", triangles)
    for index in range(len(data["accessors"])):
        assert np.isfinite(glb.acc(index)).all(), ("nonfinite accessor", index)

    low, high = position.min(0), position.max(0)
    dimensions = high - low
    length = float(spec["length"])
    assert abs(dimensions[2] - length) < max(0.005, length * 0.01), dimensions
    assert abs(low[1]) < 0.015 and abs((low[0] + high[0]) / 2) < length * 0.05
    assert low[2] < 0 < high[2] and 0 < dimensions[0] < length
    assert dimensions[1] > length * 0.25

    skin = data["skins"][0]
    ids = skin["joints"]
    assert 24 <= len(ids) <= 40 and len(set(ids)) == len(ids)
    assert joints.min() >= 0 and joints.max() < len(ids)
    nodes = data["nodes"]
    names = [nodes[i]["name"] for i in ids]
    assert len(set(names)) == len(names)
    bone = dict(zip(names, ids))
    parent = {}
    for index, node in enumerate(nodes):
        assert "camera" not in node and not node.get("extensions", {}).get("KHR_lights_punctual")
        assert "matrix" not in node, "The exporter must write canonical TRS nodes"
        assert np.array_equal(node.get("scale", [1, 1, 1]), [1, 1, 1])
        assert abs(np.linalg.norm(node.get("rotation", [0, 0, 0, 1])) - 1) < 1e-5
        for child in node.get("children", []):
            assert child not in parent and 0 <= child < len(nodes)
            parent[child] = index
    assert {"root", "chest", "neck_01", "neck_02", "head", "tail_01"} <= set(names)
    assert bone["root"] not in parent
    assert parent[bone["head"]] == bone["neck_02"]
    assert parent[bone["neck_02"]] == bone["neck_01"]
    neck_names = {name for name in names if name.startswith("neck_")}
    assert neck_names == {"neck_01", "neck_02"}
    spine = []
    ancestor = parent[bone["neck_01"]]
    while True:
        assert ancestor in ids and ancestor not in spine
        spine.append(ancestor)
        if ancestor == bone["root"]:
            break
        ancestor = parent[ancestor]
    assert 3 <= len([i for i in spine if i not in (bone["root"], bone["body_height"])]) <= 4
    assert bone["chest"] in spine
    tail = sorted(name for name in names if name.startswith("tail_"))
    assert 1 <= len(tail) <= 3
    for index, name in enumerate(tail):
        assert name == "tail_%02d" % (index + 1)
        assert parent[bone[name]] == (bone[tail[index - 1]] if index else bone["pelvis"])
    if "jaw" in bone:
        assert parent[bone["jaw"]] == bone["head"]
    for side in ("l", "r"):
        for stems in (("upper_arm", "forearm", "wrist", "hand"), ("thigh", "shin", "ankle", "foot")):
            chain = [bone[stem + "_" + side] for stem in stems]
            assert parent[chain[0]] in spine
            for above, below in zip(chain, chain[1:]):
                assert parent[below] == above
    mesh_nodes = [i for i, node in enumerate(nodes) if "mesh" in node]
    assert len(mesh_nodes) == 1
    mesh_node = nodes[mesh_nodes[0]]
    assert mesh_node["mesh"] == 0 and mesh_node["skin"] == 0
    assert mesh_nodes[0] not in parent
    assert np.array_equal(mesh_node.get("translation", [0, 0, 0]), [0, 0, 0])
    assert np.array_equal(mesh_node.get("rotation", [0, 0, 0, 1]), [0, 0, 0, 1])
    scene_roots = data["scenes"][data.get("scene", 0)]["nodes"]
    assert set(scene_roots) == {bone["root"], mesh_nodes[0]}
    assert set(ids) | set(mesh_nodes) == set(range(len(nodes)))

    inverse_bind = glb.acc(skin["inverseBindMatrices"]).reshape(-1, 4, 4).transpose(0, 2, 1)
    assert len(inverse_bind) == len(ids)

    def worlds(rotations=None):
        rotations = rotations or {}
        matrices = {}
        active = set()

        def visit(index):
            if index in matrices:
                return matrices[index]
            assert index not in active, "The node hierarchy contained a cycle"
            active.add(index)
            node = nodes[index]
            local = np.eye(4)
            local[:3, :3] = prey_rotation(rotations.get(index, node.get("rotation", [0, 0, 0, 1])))
            local[:3, 3] = node.get("translation", [0, 0, 0])
            matrices[index] = visit(parent[index]) @ local if index in parent else local
            active.remove(index)
            return matrices[index]

        return np.array([visit(index) for index in ids])

    homogeneous = np.column_stack((position, np.ones(len(position))))

    def deform(world, mask=None):
        selected = slice(None) if mask is None else mask
        return np.einsum(
            "vk,vkij,vj->vi",
            weights[selected],
            (world @ inverse_bind)[joints[selected]],
            homogeneous[selected],
        )[:, :3]

    bind = worlds()
    bind_error = float(np.linalg.norm(deform(bind) - position, axis=1).max())
    assert bind_error < 1e-5, ("bind reproduction", bind_error)
    assert np.max(abs(bind @ inverse_bind - np.eye(4))) < 1e-5
    head_index = names.index("head")
    assert bind[head_index, 2, 3] > bind[names.index("root"), 2, 3]
    mouth = np.asarray(spec["mouth"], dtype=float)
    assert mouth.shape == (3,) and np.isfinite(mouth).all()
    assert mouth[2] > bind[head_index, 2, 3] and mouth[1] > 0
    mouth_local = np.linalg.solve(bind[head_index], np.r_[mouth, 1])

    material = data["materials"][0]
    assert "normalTexture" not in material and "occlusionTexture" not in material
    assert "emissiveTexture" not in material and not material.get("extensions")
    pbr = material["pbrMetallicRoughness"]
    assert 0.85 <= pbr["roughnessFactor"] <= 0.9 and pbr["metallicFactor"] == 0
    assert "metallicRoughnessTexture" not in pbr
    assert len(data["images"]) == len(data["textures"]) == 1
    assert pbr["baseColorTexture"]["index"] == 0 and data["textures"][0]["source"] == 0
    image = data["images"][0]
    assert image["mimeType"] == "image/png" and "uri" not in image
    view = data["bufferViews"][image["bufferView"]]
    start = view.get("byteOffset", 0)
    png = glb.b[start : start + view["byteLength"]]
    assert png[:8] == b"\x89PNG\r\n\x1a\n" and png[12:16] == b"IHDR"
    assert struct.unpack_from(">II", png, 16) == (1024, 1024)

    feet = {}
    sole_sets = {}
    for name in ("foot_l", "hand_l", "foot_r", "hand_r"):
        influence = (weights * (joints == names.index(name))).sum(1)
        mask = (influence > 0.4) & (position[:, 1] < length * 0.15)
        assert mask.sum() >= 4, ("missing weighted hoof", name)
        lowest = position[mask, 1].min()
        sole = mask & (position[:, 1] <= lowest + 0.012 * length)
        assert sole.sum() >= 3, ("missing weighted sole", name)
        feet[name] = mask
        sole_sets[name] = sole

    durations = {"graze": 4.0, "walk": 1.2, "run": 0.55}
    animations = data["animations"]
    assert len(animations) == 3 and {a["name"] for a in animations} == set(durations)
    scale = float(spec.get("contact_scale", 1.0))
    assert 0 < scale <= 1
    penetration_limit, contact_limit = 0.05 * scale, 0.065 * scale
    walk_offsets = {"foot_l": 0.0, "hand_l": 0.25, "foot_r": 0.5, "hand_r": 0.75}
    reports = {}
    all_feet = np.logical_or.reduce(list(feet.values()))
    selected_feet = {name: mask[all_feet] for name, mask in feet.items()}
    selected_soles = {name: mask[all_feet] for name, mask in sole_sets.items()}
    bind_feet = position[all_feet]
    for animation in animations:
        name = animation["name"]
        duration = durations[name]
        tracks = []
        targeted = set()
        assert animation["channels"] and animation["samplers"]
        for channel in animation["channels"]:
            target = channel["target"]
            assert target["path"] == "rotation" and target["node"] in ids
            assert target["node"] not in targeted
            targeted.add(target["node"])
            sampler = animation["samplers"][channel["sampler"]]
            assert sampler.get("interpolation", "LINEAR") == "LINEAR"
            times = glb.acc(sampler["input"]).ravel()
            values = glb.acc(sampler["output"])
            assert times[0] == 0 and len(times) >= 3 and np.all(np.diff(times) > 0)
            assert abs(times[-1] - duration) < 1e-6
            assert values.shape == (len(times), 4)
            assert values[0].tobytes() == values[-1].tobytes(), (name, "loop seam")
            assert np.max(abs(np.linalg.norm(values, axis=1) - 1)) < 1e-5
            tracks.append((target["node"], times, values))
        minimum, nearest, stance_gap, level_error = math.inf, -math.inf, -math.inf, 0.0
        mouth_heights = []
        flight_samples = 0
        foot_ranges = {key: [] for key in feet}
        for time in np.linspace(0, duration, 241):
            rotations = {}
            for index, times, values in tracks:
                segment = max(0, min(np.searchsorted(times, time, side="right") - 1, len(times) - 2))
                fraction = np.clip((time - times[segment]) / (times[segment + 1] - times[segment]), 0, 1)
                first, last = values[segment].astype(float), values[segment + 1].astype(float)
                dot = float(np.dot(first, last))
                if dot < 0:
                    last, dot = -last, -dot
                if dot > 0.9995:
                    q = (1 - fraction) * first + fraction * last
                else:
                    angle = math.acos(np.clip(dot, -1, 1))
                    q = (
                        math.sin((1 - fraction) * angle) * first + math.sin(fraction * angle) * last
                    ) / math.sin(angle)
                rotations[index] = q / np.linalg.norm(q)
            world = worlds(rotations)
            mesh = deform(world, all_feet)
            heights = {key: float(mesh[mask, 1].min()) for key, mask in selected_feet.items()}
            minimum = min(minimum, *heights.values())
            nearest = max(nearest, min(heights.values()))
            flight_samples += int(min(heights.values()) > 0.01 * scale)
            mouth_heights.append(float((world[head_index] @ mouth_local)[1]))
            for key, height in heights.items():
                foot_ranges[key].append(mesh[selected_soles[key]].mean(0))
                stance = name == "graze" or (
                    name == "walk" and (time / duration - walk_offsets[key]) % 1 < 0.66
                )
                if stance:
                    stance_gap = max(stance_gap, height)
                    # A level sole preserves each vertex's height relative to its neighbours.
                    mask = selected_soles[key]
                    displacement = mesh[mask, 1] - bind_feet[mask, 1]
                    level_error = max(level_error, float(np.ptp(displacement)))
        assert minimum >= -penetration_limit, (name, "sole penetration", minimum)
        if name != "run":
            assert nearest <= contact_limit, (name, "continuous support", nearest)
            assert stance_gap <= contact_limit, (name, "stance gap", stance_gap)
            assert level_error <= 0.025 * scale, (name, "sole tilt or distortion", level_error)
        else:
            assert flight_samples >= 2, ("run lacked a suspended phase", nearest)
        sweeps = {key: float(np.ptp(np.array(points)[:, 2])) for key, points in foot_ranges.items()}
        if name in ("walk", "run"):
            assert min(sweeps.values()) > 0.03 * length, (name, "missing leg sweep", sweeps)
        if name == "graze":
            assert max(mouth_heights) - min(mouth_heights) > 0.08 * length, (
                "missing grazing head motion",
                mouth_heights,
            )
        reports[name] = {
            "duration_s": duration,
            "sampled_poses": 241,
            "sole_min_m": minimum,
            "worst_nearest_sole_m": nearest,
            "stance_gap_m": stance_gap if name != "run" else None,
            "stance_sole_height_change_m": level_error if name != "run" else None,
            "flight_samples": flight_samples,
            "mouth_min_m": min(mouth_heights),
            "mouth_max_m": max(mouth_heights),
            "hoof_sweep_m": sweeps,
            "rotation_only": True,
            "exact_loop": True,
        }
    return {
        "name": spec["name"],
        "dimensions_m": dimensions.tolist(),
        "triangles": triangles,
        "bones": len(ids),
        "bind_max_error_m": bind_error,
        "mouth_bind_m": mouth.tolist(),
        "mouth_head_local_m": mouth_local[:3].tolist(),
        "penetration_limit_m": penetration_limit,
        "stance_gap_limit_m": contact_limit,
        "clips": reports,
        "passed": True,
    }


def render(path, spec):
    """Render the exported animal and write labelled evidence sheets."""
    import json
    import math
    import struct
    import sys
    import zlib
    from pathlib import Path

    import bpy
    import numpy as np
    from bpy_extras.object_utils import world_to_camera_view
    from mathutils import Vector

    root = Path(path).resolve().parents[2]
    out = root / "artifacts" / "prey" / spec["name"]
    cache = root / "build-prey-cache" / spec["name"] / "renders"
    out.mkdir(parents=True, exist_ok=True)
    cache.mkdir(parents=True, exist_ok=True)
    requested = spec.get("render_views")
    for arg in sys.argv:
        if arg.startswith("--views="):
            requested = arg.split("=", 1)[1].split(",")
    requested = set(requested) if requested is not None else None

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(path))
    scene = bpy.context.scene
    arm = next(o for o in scene.objects if o.type == "ARMATURE")
    mesh = next(o for o in scene.objects if o.type == "MESH")
    actions = {
        clip: next(
            a
            for a in bpy.data.actions
            if a.name == clip or a.name.startswith(clip + "_") or a.name.startswith(clip + ".")
        )
        for clip in ("graze", "walk", "run")
    }
    arm.animation_data_create()
    for track in arm.animation_data.nla_tracks:
        track.mute = True
    scene.render.engine = "CYCLES"
    kit.use_gpu(scene)
    scene.cycles.samples = int(spec.get("render_samples", 8))
    scene.cycles.use_denoising = True
    scene.render.use_persistent_data = True
    scene.render.image_settings.compression = 15
    scene.cycles.max_bounces = 3
    scene.cycles.diffuse_bounces = 2
    scene.cycles.glossy_bounces = 2
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "8"
    scene.render.film_transparent = False
    scene.view_settings.view_transform = "AgX"
    scene.view_settings.exposure = 0
    scene.view_settings.gamma = 1

    world = bpy.data.worlds.new("Prey evidence sky")
    world.use_nodes = True
    scene.world = world
    nodes = world.node_tree.nodes
    sky = nodes.new("ShaderNodeTexSky")
    sky.sky_type = "SINGLE_SCATTERING"
    sky.sun_elevation = math.radians(30)
    sky.sun_rotation = math.radians(-45)
    sky.sun_disc = False
    nodes.get("Background").inputs["Strength"].default_value = 0.065
    world.node_tree.links.new(sky.outputs["Color"], nodes.get("Background").inputs["Color"])
    sun_direction = Vector((0.612372, -0.612372, 0.5))
    bpy.ops.object.light_add(type="SUN")
    sun = bpy.context.object
    sun.name = "Sun at 30 degrees"
    sun.rotation_euler = (-sun_direction).to_track_quat("-Z", "Y").to_euler()
    sun.data.energy = 2.8
    sun.data.color = (1.0, 0.92, 0.81)
    sun.data.angle = math.radians(1.5)

    length = float(spec["length"])
    bpy.ops.mesh.primitive_plane_add(size=length * 200, location=(0, 0, -0.002))
    floor = bpy.context.object
    floor.name = "Neutral green-brown ground"
    ground = bpy.data.materials.new("Prey evidence ground")
    ground.use_nodes = True
    bsdf = ground.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (0.18, 0.20, 0.145, 1)
    bsdf.inputs["Roughness"].default_value = 1
    floor.data.materials.append(ground)
    bpy.ops.object.camera_add()
    camera = bpy.context.object
    camera.data.type = "ORTHO"
    camera.data.clip_start = 0.001
    camera.data.clip_end = length * 300
    scene.camera = camera

    # Blender imports glTF Y-up as Z-up, with forward along negative Y.
    mouth = Vector((spec["mouth"][0], -spec["mouth"][2], spec["mouth"][1]))
    head_bind = arm.matrix_world @ arm.data.bones["head"].matrix_local
    mouth_local = head_bind.inverted() @ mouth

    def pose(clip, phase):
        action = actions[clip]
        arm.animation_data.action = action
        if action.slots:
            arm.animation_data.action_slot = action.slots[0]
        start, end = action.frame_range
        frame = start + phase * (end - start)
        scene.frame_set(math.floor(frame), subframe=frame % 1)
        bpy.context.view_layer.update()

    def mouth_position():
        return arm.matrix_world @ arm.pose.bones["head"].matrix @ mouth_local

    def vertices():
        evaluated = mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
        evaluated_mesh = evaluated.to_mesh()
        points = [evaluated.matrix_world @ v.co for v in evaluated_mesh.vertices]
        evaluated.to_mesh_clear()
        return points

    def bounds(points):
        low = Vector(tuple(min(v[i] for v in points) for i in range(3)))
        high = Vector(tuple(max(v[i] for v in points) for i in range(3)))
        return [Vector((x, y, z)) for x in (low.x, high.x) for y in (low.y, high.y) for z in (low.z, high.z)]

    def fit(points, direction, width, height, margin=1.15):
        scene.render.resolution_x = width
        scene.render.resolution_y = height
        box = bounds(points)
        target = sum(box, Vector()) / len(box)
        camera.location = target + Vector(direction).normalized() * length * 8
        camera.rotation_euler = (target - camera.location).to_track_quat("-Z", "Y").to_euler()
        camera.data.ortho_scale = 1
        bpy.context.view_layer.update()
        projected = [world_to_camera_view(scene, camera, p) for p in box]
        extent = max(max(abs(p.x - 0.5) * 2, abs(p.y - 0.5) * 2) for p in projected)
        camera.data.ortho_scale = extent * margin
        bpy.context.view_layer.update()

    # This bitmap font keeps labels independent of system fonts and Pillow.
    glyph_rows = {
        "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
        "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
        "C": ["01111", "10000", "10000", "10000", "10000", "10000", "01111"],
        "D": ["11110", "10001", "10001", "10001", "10001", "10001", "11110"],
        "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
        "F": ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
        "G": ["01111", "10000", "10000", "10111", "10001", "10001", "01111"],
        "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
        "I": ["11111", "00100", "00100", "00100", "00100", "00100", "11111"],
        "J": ["00111", "00010", "00010", "00010", "10010", "10010", "01100"],
        "K": ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
        "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
        "M": ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
        "N": ["10001", "11001", "10101", "10011", "10001", "10001", "10001"],
        "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
        "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
        "Q": ["01110", "10001", "10001", "10001", "10101", "10010", "01101"],
        "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
        "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
        "T": ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
        "U": ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
        "V": ["10001", "10001", "10001", "10001", "10001", "01010", "00100"],
        "W": ["10001", "10001", "10001", "10101", "10101", "10101", "01010"],
        "X": ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
        "Y": ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
        "Z": ["11111", "00001", "00010", "00100", "01000", "10000", "11111"],
        "0": ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
        "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
        "2": ["01110", "10001", "00001", "00010", "00100", "01000", "11111"],
        "3": ["11110", "00001", "00001", "01110", "00001", "00001", "11110"],
        "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
        "5": ["11111", "10000", "10000", "11110", "00001", "00001", "11110"],
        "6": ["01110", "10000", "10000", "11110", "10001", "10001", "01110"],
        "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
        "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
        "9": ["01110", "10001", "10001", "01111", "00001", "00001", "01110"],
        "/": ["00001", "00010", "00010", "00100", "01000", "01000", "10000"],
        ".": ["00000", "00000", "00000", "00000", "00000", "00110", "00110"],
        "-": ["00000", "00000", "00000", "11111", "00000", "00000", "00000"],
        ":": ["00000", "00110", "00110", "00000", "00110", "00110", "00000"],
        "%": ["11001", "11010", "00010", "00100", "01000", "01011", "10011"],
    }

    def label(pixels, text, x=12, y=10, scale=2):
        for character in text.upper().replace("_", " "):
            rows = glyph_rows.get(character, ["00000"] * 7)
            for row, values in enumerate(rows):
                for col, value in enumerate(values):
                    if value == "1":
                        pixels[
                            y + row * scale : y + (row + 1) * scale,
                            x + col * scale : x + (col + 1) * scale,
                            :3,
                        ] = 238
            x += 6 * scale

    def write_evidence(target, pixels):
        height, width = pixels.shape[:2]

        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

        scanlines = b"".join(b"\0" + row.tobytes() for row in pixels)
        target.write_bytes(
            b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(scanlines, 3))
            + chunk(b"IEND", b"")
        )

    def capture(name):
        scene.render.filepath = str(cache / (name + ".png"))
        bpy.ops.render.render(write_still=True)
        image = bpy.data.images.load(scene.render.filepath, check_existing=False)
        image.colorspace_settings.name = "Non-Color"
        pixels = np.empty(len(image.pixels), dtype=np.float32)
        image.pixels.foreach_get(pixels)
        width, height = image.size
        pixels = np.clip(pixels.reshape(height, width, 4)[::-1] * 255 + 0.5, 0, 255).astype(np.uint8)
        bpy.data.images.remove(image)
        return pixels

    def wanted(name):
        return requested is None or name in requested

    for clip in ("graze", "walk", "run"):
        if not wanted(clip + "-sheet"):
            continue
        union = []
        for k in range(8):
            pose(clip, k / 8)
            union.extend(bounds(vertices()))
        fit(union, (1, 0, 0.045), 400, 420, margin=1.12)
        sheet = np.full((900, 1600, 4), (30, 34, 30, 255), dtype=np.uint8)
        for k in range(8):
            pose(clip, k / 8)
            pixels = capture(f"{clip}-{k:02d}")
            x, y = k % 4 * 400, k // 4 * 450
            sheet[y + 30 : y + 450, x : x + 400] = pixels
            label(sheet, f"{spec['name']} {clip} {k}/8", x + 10, y + 8)
        write_evidence(out / (clip + "-sheet.png"), sheet)

    # Find the lowest animated attachment instead of assuming a phase.
    lowest = (float("inf"), 0.0)
    for k in range(65):
        phase = k / 64
        pose("graze", phase)
        lowest = min(lowest, (mouth_position().z, phase))
    deep_phase = lowest[1]
    views = [
        ("three-quarter", "walk", 0.125, (1, -1.2, 0.48)),
        ("walk-front", "walk", 0.125, (0, -1, 0.10)),
        ("walk-rear", "walk", 0.625, (0, 1, 0.10)),
        ("graze-head", "graze", deep_phase, (1, -0.65, 0.24)),
        ("run-top", "run", 0.375, (0, -0.001, 1)),
        ("walk-feet", "walk", 0.125, (1, -0.65, 0.18)),
    ]
    for name, clip, phase, direction in views:
        if not wanted(name):
            continue
        pose(clip, phase)
        points = vertices()
        if name == "graze-head":
            mouth = mouth_position()
            head = arm.matrix_world @ arm.pose.bones["head"].head
            reach = max((head - mouth).length * 1.25, length * 0.18)
            points = [p for p in points if (p - mouth).length < reach]
            points.extend((mouth, head, Vector((mouth.x, mouth.y, 0))))
        elif name == "walk-feet":
            cutoff = max(p.z for p in points) * 0.45
            points = [p for p in points if p.z < cutoff]
        fit(points, direction, 1600, 900, margin=1.18)
        pixels = capture(name)
        pixels[:42] = (30, 34, 30, 255)
        label(pixels, f"{spec['name']} / {name} / {clip} phase {phase:.4f}", scale=3)
        write_evidence(out / (name + ".png"), pixels)

    metadata = {
        "source": str(Path(path).relative_to(root)),
        "renderer": "Cycles " + scene.cycles.device,
        "samples": scene.cycles.samples,
        "sun_elevation_degrees": 30,
        "sky": "Single scattering",
        "sheet_phases": [k / 8 for k in range(8)],
        "deep_graze_phase": deep_phase,
        "deep_graze_mouth_height": lowest[0],
        "detail_phases": {name: {"clip": clip, "phase": phase} for name, clip, phase, direction in views},
    }
    (cache / "render-evidence.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"Rendered {spec['name']}: {out}", flush=True)


def report_lines(results):
    dims = " x ".join(f"{v:.3f}" for v in results["dimensions_m"])
    lines = [
        f"{results['name']} PASS | {dims} m | {results['triangles']} triangles | "
        f"{results['bones']} bones | bind error {results['bind_max_error_m']:.2e} m"
    ]
    for name, clip in results["clips"].items():
        gap = clip["stance_gap_m"]
        gap_text = f"{gap * 100:.2f} cm" if gap is not None else "free flight"
        lines.append(
            f"  {name:5} {clip['duration_s']:.2f} s | min sole {clip['sole_min_m'] * 100:.2f} cm | "
            f"max nearest sole {clip['worst_nearest_sole_m'] * 100:.2f} cm | stance {gap_text}"
        )
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("name", choices=SPECS)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--verify-only", action="store_true")
    mode.add_argument("--render-only", action="store_true")
    mode.add_argument("--build-only", action="store_true")
    parser.add_argument("--cached", action="store_true", help="Reuse the reduced surface and baked texture.")
    parser.add_argument("--views", help="Render only the named comma-separated views.")
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1 :])
    spec = SPECS[args.name]
    path = OUT / f"prey_{args.name}.glb"
    CACHE.mkdir(exist_ok=True)
    if not (args.verify_only or args.render_only):
        build(args.name, args.cached)
    if not args.render_only:
        results = verify(path, spec)
        report = json.dumps(results, indent=2)
        (CACHE / f"{args.name}-verified.json").write_text(report + "\n")
        print("\n".join(report_lines(results)), flush=True)
    if not (args.verify_only or args.build_only):
        render(path, spec)


if __name__ == "__main__":
    main()
