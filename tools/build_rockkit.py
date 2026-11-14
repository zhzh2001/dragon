"""Build the rock and cliff kit from Poly Haven CC0 scans.

    blender -b --factory-startup --python tools/build_rockkit.py
    blender -b --factory-startup --python tools/build_rockkit.py -- --verify-only
    blender -b --factory-startup --python tools/build_rockkit.py -- --render
    blender -b --factory-startup --python tools/build_rockkit.py -- --only cliff_a,boulder_b

The default run downloads the scans into build-rockkit-cache/, builds the
eight pieces at three levels of detail into assets/props/rockkit/, and then
verifies them. `--verify-only` checks the files on disk. `--render` writes the
three inspection renders to artifacts/rockkit/ from the files on disk.
The record is tools/rockkit.md.

Every step for one piece:
  1. Import the scan, orient it, cut it, scale it to game size. A cliff also
     gets its side and top borders bent back so they end inside the slope.
  2. Decimate a copy to the LOD0 budget and unwrap it into a new atlas.
  3. Bake base colour, the scan's occlusion/roughness map and a tangent-space
     normal map from the scan (mesh plus its own normal map) onto LOD0.
  4. Remove the directional light term from the base colour, then grade it to
     the kit's one rock colour.
  5. Decimate LOD0 to LOD1 and LOD2, export, and embed the maps.
"""

import json
import math
import os
import struct
import sys
import time
import urllib.request
import zlib

import bmesh
import bpy
import numpy as np
from mathutils import Euler, Vector, kdtree

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(ROOT, "build-rockkit-cache")
OUT = os.path.join(ROOT, "assets", "props", "rockkit")
ART = os.path.join(ROOT, "artifacts", "rockkit")
TMP = os.path.join(CACHE, "work")
USER_AGENT = "rockkit-build/1.0"
SCAN_RES = "4k"

# The kit's rock colour: mean linear albedo of every piece. It sits a little
# under the engine's palette Rock (0.26, 0.23, 0.20), because a scan's mean
# includes its dark cracks.
TARGET_ALBEDO = np.array([0.232, 0.208, 0.180], dtype=np.float64)
TINT_KEEP = 0.32          # share of the scan's own hue that survives the grade
TARGET_LOG_SD = 0.45      # spread of log luminance every piece is pulled toward
TARGET_ROUGH = 0.86
ALBEDO_TOLERANCE = 0.08
# The slope of log luminance against surface direction, measured at the
# vertices. The build removes it per texel, so a small rest remains. The
# scans measured 0.25 to 0.70 before.
LIGHT_LIMIT = 0.15
SIZE_LIMIT = 80 * 1000 * 1000
LOD_RATIOS = (1.0, 0.25, 0.0625)
LOD_SUFFIX = ("", "_lod1", "_lod2")
LOD_MAPS = (2048, 1024, 512)
REF_SLOPE_DEG = 60.0      # the slope a cliff's rim is shaped for
RIM_BURY = 2.0            # metres the rim ends behind that slope

# rot: degrees, Blender XYZ euler, applied to the scan first. In Blender the
#   piece faces -Y and is Z-up, which exports as +Z forward, Y-up.
# fit: (axis, metres) the uniform scale makes that Blender axis this long.
# cuts: planes in the oriented scan's normalised bounds (0..1 on each axis);
#   everything on the far side of the normal is removed.
# squash: per-axis scale applied before the fit.
# upright: a cliff scan is turned to face -Y and tipped until its mean face
#   stands at this angle from the ground.
SINK = ((0.5, 0.5, 0.10), (0, 0, -1))      # cut the lowest tenth off, flat base
PIECES = [
    dict(name="boulder_a", kind="boulder", asset="boulder_01", fit=(1, 5.4), cuts=[SINK]),
    dict(name="boulder_b", kind="boulder", asset="namaqualand_boulder_03", fit=(1, 4.4), cuts=[SINK]),
    dict(name="boulder_c", kind="boulder", asset="namaqualand_boulder_04", fit=(0, 3.0),
         rot=(0, 0, 40), cuts=[SINK]),
    dict(name="slab_a", kind="slab", asset="namaqualand_boulder_05", fit=(0, 7.0),
         squash=(1.0, 1.1, 0.72), cuts=[((0.5, 0.5, 0.12), (0, 0, -1))]),
    dict(name="slab_b", kind="slab", asset="rock_09", fit=(1, 8.6), rot=(9, 0, 0),
         cuts=[((0.5, 0.5, 0.14), (0, 0, -1))]),
    dict(name="cliff_a", kind="cliff", asset="rock_face_01", fit=(0, 32.0), upright=72.0, squash=(1.0, 1.0, 0.80),
         cuts=[((0.26, 0.5, 0.5), (-1, 0, 0))]),   # the scan's left quarter is a ledge over a gap
    dict(name="cliff_b", kind="cliff", asset="rock_face_02", fit=(0, 28.0), upright=74.0),
    dict(name="cliff_c", kind="cliff", asset="mountainside", fit=(0, 33.0), upright=72.0,
         squash=(1.0, 1.0, 0.68)),
]
BUDGET = {"boulder": 8000, "slab": 8000, "cliff": 20000}


def log(*a):
    print("[rockkit]", *a, flush=True)


class KitError(Exception):
    pass


# ----------------------------------------------------------------- download

def fetch(url, path):
    if os.path.exists(path) and os.path.getsize(path) > 0:
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    log("download", url)
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=600) as r, open(path + ".part", "wb") as f:
        while True:
            block = r.read(1 << 20)
            if not block:
                break
            f.write(block)
    os.replace(path + ".part", path)


def download(asset):
    """Fetch one Poly Haven model as glTF. Returns the paths that matter."""
    d = os.path.join(CACHE, asset)
    listing = os.path.join(d, "files.json")
    fetch("https://api.polyhaven.com/files/" + asset, listing)
    with open(listing) as f:
        entry = json.load(f)["gltf"][SCAN_RES]["gltf"]
    main = os.path.join(d, os.path.basename(entry["url"]))
    fetch(entry["url"], main)
    maps = {}
    for rel, inc in entry["include"].items():
        p = os.path.join(d, rel)
        fetch(inc["url"], p)
        for key in ("diff", "nor_gl", "arm"):
            if f"_{key}_" in os.path.basename(rel):
                maps[key] = p
    for key in ("diff", "nor_gl", "arm"):
        if key not in maps:
            raise KitError(f"{asset}: the scan has no {key} map")
    return main, maps


# -------------------------------------------------------------- image tools

def np_from_image(img):
    w, h = img.size
    a = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(a)
    return a.reshape(h, w, 4)


def load_pixels(path, colorspace="Non-Color"):
    """Stored values of an image file, 0..1, bottom row first."""
    img = bpy.data.images.load(path, check_existing=False)
    img.colorspace_settings.name = colorspace
    a = np_from_image(img).copy()
    bpy.data.images.remove(img)
    return a


def srgb_to_linear(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def to_u8(a):
    return np.clip(np.rint(a * 255.0), 0, 255).astype(np.uint8)


def png_bytes(rgb_top_first):
    """Encode an (h, w, 3) uint8 array as PNG, each row with its best filter."""
    a = rgb_top_first.astype(np.int16)
    h, w, c = a.shape
    left = np.zeros_like(a)
    left[:, 1:] = a[:, :-1]
    up = np.zeros_like(a)
    up[1:] = a[:-1]
    ul = np.zeros_like(a)
    ul[1:, 1:] = a[:-1, :-1]
    p = left + up - ul
    pa, pb, pc = np.abs(p - left), np.abs(p - up), np.abs(p - ul)
    paeth = np.where((pa <= pb) & (pa <= pc), left, np.where(pb <= pc, up, ul))
    cands = [(a - x).astype(np.uint8) for x in (0, left, up, (left + up) >> 1, paeth)]
    cost = np.stack([np.abs(x.view(np.int8).astype(np.int32)).sum(axis=(1, 2)) for x in cands])
    best = cost.argmin(axis=0)
    rows = np.empty((h, 1 + w * c), dtype=np.uint8)
    rows[:, 0] = best
    for k, x in enumerate(cands):
        sel = best == k
        rows[sel, 1:] = x[sel].reshape(-1, w * c)

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows.tobytes(), 9)) + chunk(b"IEND", b""))


def write_png(path, rgb_bottom_first_u8):
    with open(path, "wb") as f:
        f.write(png_bytes(rgb_bottom_first_u8[::-1]))


def write_jpeg(path, rgb_bottom_first_u8, quality):
    h, w, _ = rgb_bottom_first_u8.shape
    img = bpy.data.images.new("jpeg_out", w, h, alpha=False, float_buffer=False)
    img.colorspace_settings.name = "Non-Color"
    rgba = np.ones((h, w, 4), dtype=np.float32)
    rgba[..., :3] = rgb_bottom_first_u8.astype(np.float32) / 255.0
    img.pixels.foreach_set(rgba.ravel())
    img.filepath_raw = path
    img.file_format = "JPEG"
    img.save(quality=quality)
    bpy.data.images.remove(img)


def downsample(a, factor):
    h, w, c = a.shape
    return a.reshape(h // factor, factor, w // factor, factor, c).mean(axis=(1, 3))


def dilate_fill(a, valid, iterations):
    """Grow the valid texels outward so a mip or a bilinear tap past an
    island's edge reads rock, then give what is left the mean."""
    a = a.copy()
    valid = valid.copy()
    for _ in range(iterations):
        if valid.all():
            break
        acc = np.zeros_like(a)
        cnt = np.zeros(valid.shape, dtype=np.float32)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            v = np.roll(valid, (dy, dx), axis=(0, 1))
            acc += np.roll(a, (dy, dx), axis=(0, 1)) * v[..., None]
            cnt += v
        grow = (~valid) & (cnt > 0)
        a[grow] = acc[grow] / cnt[grow][:, None]
        valid |= grow
    return a, valid


def image_size(blob, mime):
    if mime == "image/png":
        return struct.unpack(">II", blob[16:24])
    i = 2
    while i < len(blob):
        if blob[i] != 0xFF:
            raise KitError("bad JPEG stream")
        marker = blob[i + 1]
        length = struct.unpack(">H", blob[i + 2:i + 4])[0]
        if 0xC0 <= marker <= 0xCF and marker not in (0xC4, 0xC8, 0xCC):
            hgt, wid = struct.unpack(">HH", blob[i + 5:i + 9])
            return wid, hgt
        i += 2 + length
    raise KitError("JPEG without a frame header")


# --------------------------------------------------------------- scene tools

def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.use_denoising = False
    return sc


def use_gpu(sc):
    try:
        prefs = bpy.context.preferences.addons["cycles"].preferences
        prefs.compute_device_type = "METAL" if sys.platform == "darwin" else "OPTIX"
        prefs.get_devices()
        for dv in prefs.devices:
            dv.use = True
        sc.cycles.device = "GPU"
    except Exception as e:  # a machine without a GPU renders on the CPU
        log("no GPU device:", e)


def activate(obj, others=()):
    bpy.ops.object.select_all(action="DESELECT")
    for o in others:
        o.select_set(True)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def bounds(obj):
    n = len(obj.data.vertices)
    co = np.empty(n * 3, dtype=np.float64)
    obj.data.vertices.foreach_get("co", co)
    co = co.reshape(n, 3)
    return co.min(axis=0), co.max(axis=0)


def tri_count(obj):
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


def apply_modifier(obj, mod):
    activate(obj)
    bpy.ops.object.modifier_apply(modifier=mod.name)


def import_scan(path):
    bpy.ops.import_scene.gltf(filepath=path)
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        raise KitError(f"{path}: no mesh")
    activate(meshes[0], meshes)
    if len(meshes) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    for o in list(bpy.context.scene.objects):
        if o != obj:
            bpy.data.objects.remove(o)
    return obj


def transform(obj, rot=None, scale=None, move=None):
    if rot is not None:
        obj.rotation_euler = Euler([math.radians(a) for a in rot], "XYZ")
    if scale is not None:
        obj.scale = scale
    if move is not None:
        obj.location = move
    activate(obj)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)


def keep_largest_islands(obj, min_share=0.02):
    """Drop loose flakes: islands under a share of the largest one's faces."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.faces.ensure_lookup_table()
    seen = set()
    islands = []
    for f in bm.faces:
        if f.index in seen:
            continue
        stack, isl = [f], []
        seen.add(f.index)
        while stack:
            g = stack.pop()
            isl.append(g)
            for e in g.edges:
                for o in e.link_faces:
                    if o.index not in seen:
                        seen.add(o.index)
                        stack.append(o)
        islands.append(isl)
    big = max(len(i) for i in islands)
    doomed = [f for isl in islands if len(isl) < big * min_share for f in isl]
    if doomed:
        bmesh.ops.delete(bm, geom=doomed, context="FACES")
    bm.to_mesh(obj.data)
    bm.free()
    return len(islands), len(doomed)


def cut(obj, co, no, cap):
    """Remove what lies on the +no side of the plane. `cap` closes the hole
    and gives the new faces material slot 1."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    geom = bm.verts[:] + bm.edges[:] + bm.faces[:]
    res = bmesh.ops.bisect_plane(bm, geom=geom, plane_co=Vector(co), plane_no=Vector(no),
                                 clear_outer=True, clear_inner=False, dist=1e-6)
    if cap:
        edges = [e for e in res["geom_cut"] if isinstance(e, bmesh.types.BMEdge)]
        filled = bmesh.ops.holes_fill(bm, edges=edges, sides=0)
        for f in filled["faces"]:
            f.material_index = 1
        bmesh.ops.triangulate(bm, faces=filled["faces"])
    bm.to_mesh(obj.data)
    bm.free()


def bend_cliff_borders(obj, piece):
    """Shape a wall segment so that it emerges from a slope.

    The piece faces -Y. A reference slope of REF_SLOPE_DEG passes through the
    wall's top. The whole rim is pulled back to end RIM_BURY metres behind
    that plane, so a segment set into a slope shows no free edge and no open
    back, and two segments side by side meet in a gully. The middle of the
    face does not move.
    """
    me = obj.data
    n = len(me.vertices)
    co = np.empty(n * 3, dtype=np.float64)
    me.vertices.foreach_get("co", co)
    co = co.reshape(n, 3)
    lo, hi = co.min(axis=0), co.max(axis=0)
    width, height = hi[0] - lo[0], hi[2] - lo[2]
    u = (co[:, 0] - lo[0]) / width
    t = (co[:, 2] - lo[2]) / height
    cot = 1.0 / math.tan(math.radians(REF_SLOPE_DEG))
    # Anchor the plane on the face near the top, where rock meets slope.
    y_top = np.median(co[t > 0.8][:, 1])
    y_plane = y_top - (hi[2] - co[:, 2]) * cot

    def smooth(x):
        x = np.clip(x, 0.0, 1.0)
        return x * x * (3 - 2 * x)

    # Distance of every vertex from the scan's outer edge, in the wall's own
    # plane. The outer edge is the longest open loop; small holes are not it.
    bm = bmesh.new()
    bm.from_mesh(me)
    parent = {}

    def find(a):
        while parent.setdefault(a, a) != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for e in bm.edges:
        if e.is_boundary:
            parent[find(e.verts[0].index)] = find(e.verts[1].index)
    bm.free()
    groups = {}
    for v in parent:
        groups.setdefault(find(v), []).append(v)
    rim = max(groups.values(), key=len)
    tree = kdtree.KDTree(len(rim))
    for k, vi in enumerate(rim):
        tree.insert((co[vi, 0], 0.0, co[vi, 2]), k)
    tree.balance()
    dist = np.array([tree.find((c[0], 0.0, c[2]))[2] for c in co])
    band = piece.get("rim_band", 0.22) * min(width, height)
    w = 1.0 - smooth(dist / band)
    target = np.maximum(co[:, 1], y_plane + RIM_BURY)
    co[:, 1] += (target - co[:, 1]) * w
    me.vertices.foreach_set("co", co.ravel())
    me.update()


def slope_anchor(co):
    """Where the reference slope sits in a cliff piece: the plane is
    y = anchor + z / tan(REF_SLOPE_DEG) in Blender axes. Found from the rim,
    which the build left RIM_BURY behind it."""
    cot = 1.0 / math.tan(math.radians(REF_SLOPE_DEG))
    return float(np.percentile(co[:, 1] - co[:, 2] * cot, 99.5)) - RIM_BURY


def weld(obj):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    lo, hi = bounds(obj)
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=float(np.linalg.norm(hi - lo)) * 2e-6)
    bm.to_mesh(obj.data)
    bm.free()


def stand_upright(obj, lean_deg):
    """Turn a wall scan to face -Y, then tip it to stand at lean_deg."""
    me = obj.data
    n = len(me.polygons)
    nrm = np.empty(n * 3)
    area = np.empty(n)
    me.polygons.foreach_get("normal", nrm)
    me.polygons.foreach_get("area", area)
    m = (nrm.reshape(n, 3) * area[:, None]).sum(axis=0)
    yaw = math.degrees(math.atan2(-m[0], -m[1]))     # brings the normal's x to 0
    transform(obj, rot=(0, 0, yaw))
    elevation = math.degrees(math.atan2(m[2], math.hypot(m[0], m[1])))
    tip = elevation - (90.0 - lean_deg)
    transform(obj, rot=(tip, 0, 0))
    return yaw, tip


def prepare_source(obj, piece):
    """Orient, cut, scale and seat the scan. Returns the game-size scan."""
    weld(obj)
    # Two empty slots: 0 for the scan, 1 for the underside of a cut. Clearing
    # the slots later would reset every face to slot 0 and lose the mark.
    obj.data.materials.clear()
    obj.data.materials.append(None)
    obj.data.materials.append(None)
    transform(obj, rot=piece.get("rot", (0, 0, 0)))
    if "upright" in piece:
        log(piece["name"], "yaw, tip:", stand_upright(obj, piece["upright"]))
    if "squash" in piece:
        transform(obj, scale=piece["squash"])
    cap = piece["kind"] != "cliff"
    for frac, no in piece.get("cuts", ()):
        lo, hi = bounds(obj)
        cut(obj, lo + (hi - lo) * np.array(frac), no, cap and no[2] < -0.5)
    islands, dropped = keep_largest_islands(obj)
    lo, hi = bounds(obj)
    axis, metres = piece["fit"]
    s = metres / (hi[axis] - lo[axis])
    transform(obj, scale=(s, s, s))
    if piece["kind"] == "cliff":
        bend_cliff_borders(obj, piece)
    lo, hi = bounds(obj)
    transform(obj, move=(-(lo[0] + hi[0]) / 2, -(lo[1] + hi[1]) / 2, -lo[2]))
    for p in obj.data.polygons:
        p.use_smooth = True
    return s, islands, dropped


# -------------------------------------------------------------------- baking

def source_materials(obj, maps):
    """Slot 0 shows the scan's maps, slot 1 is the plain underside of a cut."""
    mats = []
    for slot in range(2):
        m = bpy.data.materials.new(f"scan_{slot}")
        m.use_nodes = True
        nt = m.node_tree
        nt.nodes.clear()
        out = nt.nodes.new("ShaderNodeOutputMaterial")
        emit = nt.nodes.new("ShaderNodeEmission")
        bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
        if slot == 0:
            tex = nt.nodes.new("ShaderNodeTexImage")
            tex.name = "tex"
            nor = nt.nodes.new("ShaderNodeTexImage")
            nor.image = bpy.data.images.load(maps["nor_gl"], check_existing=True)
            nor.image.colorspace_settings.name = "Non-Color"
            nmap = nt.nodes.new("ShaderNodeNormalMap")
            nt.links.new(nor.outputs["Color"], nmap.inputs["Color"])
            nt.links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])
            nt.links.new(tex.outputs["Color"], emit.inputs["Color"])
        obj.data.materials[slot] = m
        mats.append(m)
    return mats


def set_source_mode(mats, mode, maps, cap_colour):
    for slot, m in enumerate(mats):
        nt = m.node_tree
        out = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL")
        emit = next(n for n in nt.nodes if n.type == "EMISSION")
        bsdf = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
        for l in list(out.inputs["Surface"].links):
            nt.links.remove(l)
        if mode == "normal":
            nt.links.new(bsdf.outputs[0], out.inputs["Surface"])
            continue
        nt.links.new(emit.outputs[0], out.inputs["Surface"])
        if slot == 0:
            tex = nt.nodes["tex"]
            tex.image = bpy.data.images.load(maps[mode], check_existing=True)
            tex.image.colorspace_settings.name = "sRGB" if mode == "diff" else "Non-Color"
        else:
            emit.inputs["Color"].default_value = (*cap_colour[mode], 1.0)


def make_lod0(src, budget):
    activate(src)
    bpy.ops.object.duplicate()
    low = bpy.context.view_layer.objects.active
    low.name = "lod0"
    n = tri_count(low)
    if n > budget:
        mod = low.modifiers.new("dec", "DECIMATE")
        mod.ratio = budget * 0.985 / n
        mod.use_collapse_triangulate = True
        apply_modifier(low, mod)
    mod = low.modifiers.new("tri", "TRIANGULATE")
    apply_modifier(low, mod)
    while len(low.data.uv_layers):
        low.data.uv_layers.remove(low.data.uv_layers[0])
    return low


def unwrap(low):
    """A new atlas. The islands are found on a smoothed copy, because a
    projection of the rough mesh shatters into thousands of chips."""
    activate(low)
    bpy.ops.object.duplicate()
    proxy = bpy.context.view_layer.objects.active
    mod = proxy.modifiers.new("smooth", "SMOOTH")
    mod.factor = 0.8
    mod.iterations = 40
    apply_modifier(proxy, mod)
    proxy.data.uv_layers.new(name="UVMap")
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(72), island_margin=0.0,
                             area_weight=0.0, correct_aspect=True, scale_to_bounds=False)
    bpy.ops.object.mode_set(mode="OBJECT")
    n = len(proxy.data.loops)
    uv = np.empty(n * 2, dtype=np.float32)
    proxy.data.uv_layers[0].data.foreach_get("uv", uv)
    bpy.data.objects.remove(proxy)
    if len(low.data.loops) != n:
        raise KitError("the smoothed copy lost its topology")
    layer = low.data.uv_layers.new(name="UVMap")
    layer.data.foreach_set("uv", uv)
    activate(low)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.average_islands_scale()
    bpy.ops.object.mode_set(mode="OBJECT")
    shrink_cap_uvs(low)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.pack_islands(rotate=True, margin=0.004)
    bpy.ops.object.mode_set(mode="OBJECT")


def shrink_cap_uvs(low, factor=0.3):
    """The flat underside of a cut carries no scan detail and is not seen.
    Give it a tenth of the texels it would take."""
    me = low.data
    cap_loops = [li for p in me.polygons if p.material_index == 1 for li in p.loop_indices]
    if not cap_loops:
        return
    n = len(me.loops)
    uv = np.empty(n * 2, dtype=np.float32)
    me.uv_layers[0].data.foreach_get("uv", uv)
    uv = uv.reshape(n, 2)
    c = uv[cap_loops].mean(axis=0)
    uv[cap_loops] = c + (uv[cap_loops] - c) * factor
    me.uv_layers[0].data.foreach_set("uv", uv.ravel())


def bake_pass(sc, src, low, kind, size, extrusion):
    mat = low.data.materials[0]
    nt = mat.node_tree
    node = nt.nodes["bake_target"]
    img = bpy.data.images.new("bake_" + kind, size, size, alpha=True, float_buffer=True)
    img.colorspace_settings.name = "Non-Color" if kind != "diff" else "Linear Rec.709"
    node.image = img
    nt.nodes.active = node
    if kind == "object_normal":
        activate(low)
        sc.cycles.samples = 2
        bpy.ops.object.bake(type="NORMAL", normal_space="OBJECT", use_selected_to_active=False,
                            margin=0, use_clear=True)
    else:
        activate(low, [src])
        sc.cycles.samples = 8
        bpy.ops.object.bake(type="NORMAL" if kind == "normal" else "EMIT",
                            normal_space="TANGENT", use_selected_to_active=True,
                            cage_extrusion=extrusion, max_ray_distance=extrusion * 2.5,
                            margin=0, use_clear=True)
    a = np_from_image(img)[..., :3].astype(np.float64)
    bpy.data.images.remove(img)
    return a


def light_fit(lum, normals, valid):
    """Least squares fit of log luminance against the surface direction:
    log L = a + b . n. A baked sun or sky shows as a b that is not zero."""
    n = normals[valid]
    y = np.log(np.maximum(lum[valid], 1e-4))
    step = max(1, len(y) // 400000)
    A = np.concatenate([np.ones((len(y[::step]), 1)), n[::step]], axis=1)
    coef, *_ = np.linalg.lstsq(A, y[::step], rcond=None)
    return coef[1:]


LUMA = np.array([0.2126, 0.7152, 0.0722])


def grade(diff, normals, valid):
    """Delight, then pull the piece to the kit's colour. Linear in and out."""
    c = np.maximum(diff, 1e-4)
    lum = c @ LUMA
    b_before = light_fit(lum, normals, valid)
    c = c * np.exp(-(normals @ b_before))[..., None]
    lum = c @ LUMA
    b_after = light_fit(lum, normals, valid)
    # One hue: keep a share of the scan's own colour variation.
    tint = TARGET_ALBEDO / (TARGET_ALBEDO @ LUMA)
    mean_rgb = c[valid].mean(axis=0)
    own = c / mean_rgb * (mean_rgb @ LUMA)       # the scan with its cast removed
    c = (own * TINT_KEEP + lum[..., None] * (1 - TINT_KEEP)) * tint
    # One contrast.
    lum = np.maximum(c @ LUMA, 1e-4)
    ll = np.log(lum)
    m, sd = ll[valid].mean(), ll[valid].std()
    k = float(np.clip(TARGET_LOG_SD / sd, 0.75, 1.5))
    c = c * np.exp((ll - m) * (k - 1.0))[..., None]
    # One mean.
    for _ in range(4):
        c = np.clip(c * (TARGET_ALBEDO / c[valid].mean(axis=0)), 0.012, 0.85)
    return c, dict(light_before=[round(float(x), 4) for x in b_before],
                   light_after=[round(float(x), 4) for x in b_after],
                   contrast_gain=round(k, 3), log_sd_before=round(float(sd), 3))


def bake_maps(sc, src, low, maps, mats, size, name):
    lo, hi = bounds(low)
    extrusion = float(np.linalg.norm(hi - lo)) * 0.012
    m = bpy.data.materials.new("bake_target")
    m.use_nodes = True
    node = m.node_tree.nodes.new("ShaderNodeTexImage")
    node.name = "bake_target"
    low.data.materials[0] = m
    low.data.materials[1] = m          # slot 1: the faces of a cut's underside
    cap = dict(diff=(0.20, 0.18, 0.16), arm=(0.85, 0.95, 0.0))
    out = {}
    for kind in ("diff", "arm", "normal"):
        set_source_mode(mats, kind, maps, cap)
        out[kind] = bake_pass(sc, src, low, kind, size, extrusion)
    out["object_normal"] = bake_pass(sc, src, low, "object_normal", size, extrusion)
    low.data.materials.clear()
    for p in low.data.polygons:
        p.material_index = 0

    valid = out["diff"].max(axis=2) > 1e-6
    coverage = float(valid.mean())
    normals = out["object_normal"] * 2.0 - 1.0
    normals /= np.maximum(np.linalg.norm(normals, axis=2, keepdims=True), 1e-6)
    # The flat underside of a cut is one made-up colour facing down. It must
    # not steer the light fit or the grade. Its texels are the ones that
    # baked to exactly that colour.
    cap = valid & (np.abs(out["diff"] - np.array(cap["diff"])).max(axis=2) < 2e-3)
    rock = valid & ~cap
    base, stats = grade(out["diff"], normals, rock)
    base[cap] = TARGET_ALBEDO * 0.8
    stats["atlas_coverage"] = round(coverage, 3)

    tn = out["normal"] * 2.0 - 1.0
    tn /= np.maximum(np.linalg.norm(tn, axis=2, keepdims=True), 1e-6)
    tn[~valid] = (0.0, 0.0, 1.0)
    orm = out["arm"].copy()
    rough = orm[..., 1]
    orm[..., 1] = np.clip(rough + (TARGET_ROUGH - rough[valid].mean()), 0.45, 1.0)
    orm[..., 0] = np.clip(orm[..., 0], 0.0, 1.0)
    orm[..., 2] = 0.0

    base, _ = dilate_fill(base, valid, 24)
    base[~_] = base[valid].mean(axis=0)
    orm, v2 = dilate_fill(orm, valid, 24)
    orm[~v2] = orm[valid].mean(axis=0)
    tn, _ = dilate_fill(tn, valid, 24)
    tn /= np.maximum(np.linalg.norm(tn, axis=2, keepdims=True), 1e-6)
    stats["mean_albedo"] = [round(float(x), 4) for x in base[valid].mean(axis=0)]
    return dict(base=base, normal=tn, orm=orm), stats


def write_maps(maps, name):
    """Write the three maps at each LOD's size. Returns the file paths."""
    os.makedirs(TMP, exist_ok=True)
    paths = []
    full = maps["base"].shape[0]
    for lod, size in enumerate(LOD_MAPS):
        f = full // size
        base = downsample(maps["base"], f)
        orm = downsample(maps["orm"], f)
        nrm = downsample(maps["normal"], f)
        nrm /= np.maximum(np.linalg.norm(nrm, axis=2, keepdims=True), 1e-6)
        stem = os.path.join(TMP, name + LOD_SUFFIX[lod])
        write_jpeg(stem + "_base.jpg", to_u8(linear_to_srgb(base)), 90)
        write_jpeg(stem + "_orm.jpg", to_u8(orm), 88)
        # 7 bits for X and Y, 6 for Z: under one degree of error, and the PNG
        # is a fifth smaller, which is what fits the kit in its 80 MB.
        n8 = to_u8(nrm * 0.5 + 0.5).astype(np.int32)
        n8[..., :2] = np.minimum((n8[..., :2] + 1) // 2 * 2, 254)
        n8[..., 2] = np.minimum((n8[..., 2] + 2) // 4 * 4, 252)
        write_png(stem + "_normal.png", n8.astype(np.uint8))
        paths.append((stem + "_base.jpg", stem + "_normal.png", stem + "_orm.jpg"))
    return paths


# -------------------------------------------------------------------- export

def read_glb(path):
    with open(path, "rb") as f:
        blob = f.read()
    magic, version, length = struct.unpack("<4sII", blob[:12])
    if magic != b"glTF" or version != 2 or length != len(blob):
        raise KitError(f"{path}: not a glTF 2 binary")
    off, doc, binary = 12, None, b""
    while off < length:
        clen, ctype = struct.unpack("<II", blob[off:off + 8])
        body = blob[off + 8:off + 8 + clen]
        if ctype == 0x4E4F534A:
            doc = json.loads(body)
        elif ctype == 0x004E4942:
            binary = body
        off += 8 + clen
    return doc, binary


def write_glb(path, doc, binary):
    js = json.dumps(doc, separators=(",", ":")).encode()
    js += b" " * (-len(js) % 4)
    binary = bytes(binary) + b"\0" * (-len(binary) % 4)
    total = 12 + 8 + len(js) + 8 + len(binary)
    with open(path, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(binary), 0x004E4942) + binary)


def export_glb(obj, path, name, images):
    """Blender writes the geometry and its tangents. The material and the
    three maps are then written here, byte for byte as built, so the
    exporter cannot re-encode them or split the packed map."""
    activate(obj)
    obj.name = name
    obj.data.name = name
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True,
                              export_yup=True, export_apply=True, export_tangents=True,
                              export_normals=True, export_texcoords=True,
                              export_materials="PLACEHOLDER", export_animations=False,
                              export_skins=False, export_morph=False, export_cameras=False,
                              export_lights=False, export_extras=False)
    doc, binary = read_glb(path)
    binary = bytearray(binary)
    doc["images"], doc["textures"] = [], []
    for i, (p, label) in enumerate(zip(images, ("basecolor", "normal", "orm"))):
        with open(p, "rb") as f:
            data = f.read()
        binary += b"\0" * (-len(binary) % 4)
        doc["bufferViews"].append(dict(buffer=0, byteOffset=len(binary), byteLength=len(data)))
        binary += data
        doc["images"].append(dict(name=f"{name}_{label}", bufferView=len(doc["bufferViews"]) - 1,
                                  mimeType="image/png" if p.endswith(".png") else "image/jpeg"))
        doc["textures"].append(dict(sampler=0, source=i))
    doc["samplers"] = [dict(magFilter=9729, minFilter=9987, wrapS=10497, wrapT=10497)]
    doc["materials"] = [dict(
        name="rockkit_" + name.split("_lod")[0],
        pbrMetallicRoughness=dict(baseColorTexture=dict(index=0),
                                  metallicRoughnessTexture=dict(index=2),
                                  metallicFactor=1.0, roughnessFactor=1.0),
        normalTexture=dict(index=1, scale=1.0),
        occlusionTexture=dict(index=2, strength=1.0))]
    for mesh in doc["meshes"]:
        for prim in mesh["primitives"]:
            prim["material"] = 0
    binary += b"\0" * (-len(binary) % 4)
    doc["buffers"] = [dict(byteLength=len(binary))]
    doc["asset"]["generator"] = "tools/build_rockkit.py"
    write_glb(path, doc, binary)


def make_lod(low, ratio):
    activate(low)
    bpy.ops.object.duplicate()
    o = bpy.context.view_layer.objects.active
    mod = o.modifiers.new("dec", "DECIMATE")
    mod.ratio = ratio
    mod.use_collapse_triangulate = True
    apply_modifier(o, mod)
    return o


def build_piece(piece):
    t0 = time.time()
    name = piece["name"]
    sc = reset_scene()
    use_gpu(sc)
    path, maps = download(piece["asset"])
    src = import_scan(path)
    scan_tris = tri_count(src)
    scale, islands, dropped = prepare_source(src, piece)
    mats = source_materials(src, maps)
    low = make_lod0(src, BUDGET[piece["kind"]])
    unwrap(low)
    for p in low.data.polygons:
        p.use_smooth = True
    baked, stats = bake_maps(sc, src, low, maps, mats, LOD_MAPS[0], name)
    images = write_maps(baked, name)
    os.makedirs(OUT, exist_ok=True)
    tris = []
    for lod, ratio in enumerate(LOD_RATIOS):
        obj = low if lod == 0 else make_lod(low, ratio)
        tris.append(tri_count(obj))
        export_glb(obj, os.path.join(OUT, name + LOD_SUFFIX[lod] + ".glb"),
                   name + LOD_SUFFIX[lod], images[lod])
    lo, hi = bounds(low)
    stats.update(name=name, asset=piece["asset"], scan_triangles=scan_tris,
                 scale=round(scale, 3), triangles=tris, islands_dropped=dropped,
                 size_blender_xyz=[round(float(x), 2) for x in hi - lo],
                 seconds=round(time.time() - t0, 1))
    log("BUILT", json.dumps(stats))
    return stats


# -------------------------------------------------------------- verification

COMPONENTS = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


def accessor(doc, binary, index):
    acc = doc["accessors"][index]
    view = doc["bufferViews"][acc["bufferView"]]
    code, size = COMPONENTS[acc["componentType"]]
    width = WIDTH[acc["type"]]
    start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    stride = view.get("byteStride", size * width)
    if stride != size * width:
        raise KitError("interleaved buffers are not expected")
    a = np.frombuffer(binary, dtype=np.dtype("<" + code), count=acc["count"] * width, offset=start)
    return a.reshape(acc["count"], width) if width > 1 else a


def image_blob(doc, binary, index):
    img = doc["images"][index]
    view = doc["bufferViews"][img["bufferView"]]
    start = view.get("byteOffset", 0)
    return binary[start:start + view["byteLength"]], img["mimeType"]


def decode(blob, mime):
    os.makedirs(TMP, exist_ok=True)
    p = os.path.join(TMP, "verify_tmp" + (".png" if mime == "image/png" else ".jpg"))
    with open(p, "wb") as f:
        f.write(blob)
    a = load_pixels(p)
    os.remove(p)
    return a[..., :3].astype(np.float64)


def silhouette(pos, tri, axes, lo, hi, res=192):
    """Filled outline of the mesh seen along one axis, as a boolean grid."""
    span = float(max(hi[axes[0]] - lo[axes[0]], hi[axes[1]] - lo[axes[1]]))
    p = (pos[:, axes] - lo[list(axes)]) / span * (res - 1)
    grid = np.zeros((res, res), dtype=bool)
    t = p[tri]                                    # (n, 3, 2)
    mn = np.clip(np.floor(t.min(axis=1)).astype(int), 0, res - 1)
    mx = np.clip(np.ceil(t.max(axis=1)).astype(int), 0, res - 1)
    for k in range(len(t)):
        x0, y0 = mn[k]
        x1, y1 = mx[k]
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y0, y1 + 1))
        a, b, c = t[k]
        d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(d) < 1e-9:
            continue
        w0 = ((b[1] - c[1]) * (xs - c[0]) + (c[0] - b[0]) * (ys - c[1])) / d
        w1 = ((c[1] - a[1]) * (xs - c[0]) + (a[0] - c[0]) * (ys - c[1])) / d
        inside = (w0 >= -0.02) & (w1 >= -0.02) & (w0 + w1 <= 1.02)
        grid[y0:y1 + 1, x0:x1 + 1] |= inside
    return grid


def check(cond, message):
    if not cond:
        raise KitError(message)


def verify_file(piece, lod):
    name = piece["name"] + LOD_SUFFIX[lod]
    path = os.path.join(OUT, name + ".glb")
    check(os.path.exists(path), f"{name}: file missing")
    doc, binary = read_glb(path)
    check(len(doc.get("meshes", [])) == 1 and len(doc["meshes"][0]["primitives"]) == 1,
          f"{name}: expected one mesh with one primitive")
    check(len(doc.get("materials", [])) == 1, f"{name}: expected one material")
    check(len(doc.get("nodes", [])) == 1, f"{name}: expected one node")
    node = doc["nodes"][0]
    for key in ("translation", "rotation", "scale", "matrix"):
        check(key not in node, f"{name}: node has a {key}; transforms must be applied")
    for key in ("skins", "animations", "cameras"):
        check(not doc.get(key), f"{name}: unexpected {key}")
    prim = doc["meshes"][0]["primitives"][0]
    check(prim.get("mode", 4) == 4, f"{name}: not a triangle list")
    for attr in ("POSITION", "NORMAL", "TANGENT", "TEXCOORD_0"):
        check(attr in prim["attributes"], f"{name}: no {attr}")
    pos = accessor(doc, binary, prim["attributes"]["POSITION"]).astype(np.float64)
    nrm = accessor(doc, binary, prim["attributes"]["NORMAL"]).astype(np.float64)
    tan = accessor(doc, binary, prim["attributes"]["TANGENT"]).astype(np.float64)
    uv = accessor(doc, binary, prim["attributes"]["TEXCOORD_0"]).astype(np.float64)
    idx = accessor(doc, binary, prim["indices"]).astype(np.int64).reshape(-1, 3)
    check(tan.shape[1] == 4 and np.all(np.abs(np.abs(tan[:, 3]) - 1) < 1e-3),
          f"{name}: tangents need a +-1 handedness")
    # A triangle with no UV area has no tangent; the exporter writes zero.
    bad_tan = float((np.abs(np.linalg.norm(tan[:, :3], axis=1) - 1) > 0.02).mean())
    check(bad_tan < 0.005, f"{name}: {bad_tan:.2%} of tangents are not unit length")
    check(uv.min() >= -1e-4 and uv.max() <= 1 + 1e-4, f"{name}: UVs leave the 0..1 square")
    tris = len(idx)
    lo, hi = pos.min(axis=0), pos.max(axis=0)
    size = hi - lo

    mat = doc["materials"][0]
    pbr = mat.get("pbrMetallicRoughness", {})
    for key, holder in (("baseColorTexture", pbr), ("metallicRoughnessTexture", pbr),
                        ("normalTexture", mat), ("occlusionTexture", mat)):
        check(key in holder, f"{name}: material has no {key}")
    check(mat["occlusionTexture"]["index"] == pbr["metallicRoughnessTexture"]["index"],
          f"{name}: occlusion and roughness are not one packed map")
    check(len(doc["images"]) == 3, f"{name}: expected three images")
    tex = lambda t: doc["textures"][t["index"]]["source"]
    sizes, pixels = {}, {}
    for label, t in (("base", pbr["baseColorTexture"]), ("normal", mat["normalTexture"]),
                     ("orm", mat["occlusionTexture"])):
        blob, mime = image_blob(doc, binary, tex(t))
        sizes[label] = image_size(blob, mime)
        if label == "normal":
            check(mime == "image/png", f"{name}: the normal map must be PNG")
        want = LOD_MAPS[lod]
        check(sizes[label] == (want, want), f"{name}: {label} map is {sizes[label]}, expected {want}")
        pixels[label] = decode(blob, mime)
    n = pixels["normal"] * 2 - 1
    check(np.abs(np.linalg.norm(n, axis=2) - 1).mean() < 0.02, f"{name}: normal map is not unit length")
    check(n[..., 2].mean() > 0.80 and abs(n[..., 0].mean()) < 0.05 and abs(n[..., 1].mean()) < 0.05,
          f"{name}: normal map is not tangent space")
    # JPEG chroma ringing leaves a few stray counts in blue; 0 is written.
    blue = pixels["orm"][..., 2]
    check(blue.mean() <= 3 / 255 and np.percentile(blue, 99.9) <= 32 / 255,
          f"{name}: metallic channel is not 0")
    check(0.5 < pixels["orm"][..., 1].mean() <= 1.0, f"{name}: roughness mean out of range")
    albedo = srgb_to_linear(pixels["base"])
    mean_albedo = albedo.reshape(-1, 3).mean(axis=0)

    # A baked sun shows as base colour that follows the surface direction.
    h, w, _ = albedo.shape
    px = np.clip((uv[:, 0] * w).astype(int), 0, w - 1)
    py = np.clip(((1.0 - uv[:, 1]) * h).astype(int), 0, h - 1)
    lum = albedo[py, px] @ LUMA
    seen = pos[:, 1] > 0.03 * size[1]          # not the flat underside of a cut
    # Weight each vertex by the surface it stands for, as the build's fit
    # weights by texel.
    tri_area = np.linalg.norm(np.cross(pos[idx[:, 1]] - pos[idx[:, 0]], pos[idx[:, 2]] - pos[idx[:, 0]]), axis=1)
    weight = np.zeros(len(pos))
    for k in range(3):
        np.add.at(weight, idx[:, k], tri_area)
    sw = np.sqrt(weight[seen])[:, None]
    A = np.concatenate([np.ones((int(seen.sum()), 1)), nrm[seen]], axis=1) * sw
    coef, *_ = np.linalg.lstsq(A, np.log(np.maximum(lum[seen], 1e-4)) * sw[:, 0], rcond=None)
    light = float(np.linalg.norm(coef[1:]))

    return dict(name=name, tris=tris, lo=lo, hi=hi, size=size, sizes=sizes, pos=pos, idx=idx,
                mean_albedo=mean_albedo, light=light, bytes=os.path.getsize(path))


def verify():
    reset_scene()
    lines, means = [], {}
    total = 0
    for piece in PIECES:
        kind = piece["kind"]
        info = [verify_file(piece, lod) for lod in range(3)]
        a = info[0]
        name = piece["name"]
        x, y, z = a["size"]
        check(a["tris"] <= BUDGET[kind], f"{name}: {a['tris']} triangles, budget {BUDGET[kind]}")
        r1, r2 = info[1]["tris"] / a["tris"], info[2]["tris"] / a["tris"]
        check(0.20 <= r1 <= 0.30, f"{name}: LOD1 is {r1:.1%} of LOD0, expected about 25%")
        check(0.045 <= r2 <= 0.08, f"{name}: LOD2 is {r2:.1%} of LOD0, expected about 6%")
        if kind == "boulder":
            across = max(x, z)
            check(2.0 <= across <= 6.0 and min(x, z) >= 2.0, f"{name}: {x:.2f} x {z:.2f} m across, expected 2 to 6")
        elif kind == "slab":
            check(4.0 <= max(x, z) <= 10.0, f"{name}: {max(x, z):.2f} m long, expected 4 to 10")
            check(y <= 0.5 * max(x, z), f"{name}: {y:.2f} m high is not a slab")
        else:
            check(20.0 <= x <= 40.0, f"{name}: {x:.2f} m wide, expected 20 to 40")
            check(15.0 <= y <= 30.0, f"{name}: {y:.2f} m high, expected 15 to 30")
        check(abs(a["lo"][1]) < 0.01, f"{name}: base is at y={a['lo'][1]:.3f}, expected 0")
        cx, cz = (a["lo"][0] + a["hi"][0]) / 2, (a["lo"][2] + a["hi"][2]) / 2
        check(abs(cx) < 0.01 and abs(cz) < 0.01, f"{name}: origin is ({cx:.3f}, {cz:.3f}) off the base centre")
        ious = []
        for lod in (1, 2):
            b = info[lod]
            drift = float(np.abs(np.concatenate([b["lo"] - a["lo"], b["hi"] - a["hi"]])).max())
            limit = (0.03, 0.06)[lod - 1] * float(a["size"].max())
            check(drift <= limit, f"{b['name']}: bounds drift {drift:.2f} m from LOD0, limit {limit:.2f}")
            worst = 1.0
            for axes in ((0, 1), (2, 1), (0, 2)):   # front, side, top
                s0 = silhouette(a["pos"], a["idx"], axes, a["lo"], a["hi"])
                s1 = silhouette(b["pos"], b["idx"], axes, a["lo"], a["hi"])
                worst = min(worst, float((s0 & s1).sum() / max(1, (s0 | s1).sum())))
            floor = (0.95, 0.90)[lod - 1]
            check(worst >= floor, f"{b['name']}: silhouette overlap {worst:.3f}, floor {floor}")
            ious.append(worst)
        check(a["light"] < LIGHT_LIMIT, f"{name}: base colour follows the surface direction ({a['light']:.3f}); baked light")
        means[name] = a["mean_albedo"]
        total += sum(i["bytes"] for i in info)
        lines.append(f"VERIFY {name}: PASS\n"
                     f"  glTF Y-up size X/Y/Z metres: ({x:.2f}, {y:.2f}, {z:.2f}); base y={a['lo'][1]:.3f}; origin offset ({cx:.3f}, {cz:.3f})\n"
                     f"  triangles LOD0/1/2: {a['tris']}/{info[1]['tris']}/{info[2]['tris']} ({r1:.1%}, {r2:.1%}); budget {BUDGET[kind]}\n"
                     f"  silhouette overlap with LOD0 (worst of front, side, top): LOD1 {ious[0]:.3f}, LOD2 {ious[1]:.3f}\n"
                     f"  maps base/normal/ORM: {LOD_MAPS[0]}/{LOD_MAPS[1]}/{LOD_MAPS[2]} px per LOD; normal PNG; ORM packed, metallic 0; tangents: PASS\n"
                     f"  mean linear albedo: ({a['mean_albedo'][0]:.4f}, {a['mean_albedo'][1]:.4f}, {a['mean_albedo'][2]:.4f}); directional light term {a['light']:.3f} (limit {LIGHT_LIMIT})\n"
                     f"  bytes LOD0/1/2: {info[0]['bytes']}/{info[1]['bytes']}/{info[2]['bytes']}")
    kit = np.mean(list(means.values()), axis=0)
    kit_lum = float(kit @ LUMA)
    worst = 0.0
    for name, m in means.items():
        dev = max(float(np.abs(m / kit - 1).max()), abs(float(m @ LUMA) / kit_lum - 1))
        worst = max(worst, dev)
        check(dev <= ALBEDO_TOLERANCE, f"{name}: mean albedo is {dev:.1%} from the kit mean, limit 8%")
    on_disk = sum(os.path.getsize(os.path.join(OUT, f)) for f in os.listdir(OUT))
    extra = sorted(set(os.listdir(OUT)) - {p["name"] + s + ".glb" for p in PIECES for s in LOD_SUFFIX})
    check(not extra, f"unexpected files in the kit directory: {extra}")
    check(on_disk <= SIZE_LIMIT, f"kit is {on_disk / 1e6:.1f} MB, limit 80 MB")
    for l in lines:
        print(l)
    print(f"VERIFY kit: PASS\n"
          f"  kit mean linear albedo: ({kit[0]:.4f}, {kit[1]:.4f}, {kit[2]:.4f}); worst piece deviation {worst:.2%} (limit 8%)\n"
          f"  directory size: {on_disk / 1e6:.2f} MB of 80 MB, 24 files", flush=True)


# -------------------------------------------------------------------- renders

SUN_ELEVATION = 20.0


def render_setup(samples=96):
    sc = reset_scene()
    use_gpu(sc)
    sc.cycles.samples = samples
    sc.cycles.use_denoising = True
    sc.cycles.use_adaptive_sampling = True
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGB"
    for vt, look in (("AgX", "AgX - Medium High Contrast"), ("Filmic", "Medium High Contrast")):
        try:
            sc.view_settings.view_transform = vt
            sc.view_settings.look = look
            break
        except TypeError:
            continue
    world = bpy.data.worlds.new("sky")
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs[0].default_value = (0.42, 0.58, 0.95, 1.0)   # the sky light
    bg.inputs[1].default_value = 0.55
    sc.world = world
    sun_data = bpy.data.lights.new("sun", "SUN")
    sun_data.energy = 4.2
    sun_data.color = (1.0, 0.90, 0.76)
    sun_data.angle = math.radians(1.2)
    sun = bpy.data.objects.new("sun", sun_data)
    sc.collection.objects.link(sun)
    cam_data = bpy.data.cameras.new("cam")
    cam_data.clip_start = 0.1
    cam_data.clip_end = 5000
    cam = bpy.data.objects.new("cam", cam_data)
    sc.collection.objects.link(cam)
    sc.camera = cam
    return sc, sun, cam


def aim_sun(sun, azimuth_deg):
    """Azimuth is where the light comes FROM, in degrees from +X toward +Y."""
    a, e = math.radians(azimuth_deg), math.radians(SUN_ELEVATION)
    to_sun = Vector((math.cos(a) * math.cos(e), math.sin(a) * math.cos(e), math.sin(e)))
    sun.rotation_euler = to_sun.to_track_quat("Z", "Y").to_euler()


def aim_camera(cam, eye, target):
    cam.location = eye
    cam.rotation_euler = (Vector(target) - Vector(eye)).to_track_quat("-Z", "Y").to_euler()


def import_piece(name):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=os.path.join(OUT, name + ".glb"))
    new = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
    obj = new[0]
    activate(obj)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return obj


def plain_material(name, colour, roughness=0.9):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    b.inputs["Base Color"].default_value = (*colour, 1.0)
    b.inputs["Roughness"].default_value = roughness
    return m


def add_plane(name, size, material, location=(0, 0, 0), rotation=(0, 0, 0)):
    bpy.ops.mesh.primitive_plane_add(size=1, location=location, rotation=rotation)
    o = bpy.context.view_layer.objects.active
    o.name = name
    o.scale = (size[0], size[1], 1)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    o.data.materials.append(material)
    return o


def view_half(cam, w, h):
    """Half width and half height of the view at distance 1."""
    half = 18.0 / cam.data.lens
    return (half, half * h / w) if w >= h else (half * w / h, half)


def add_label(cam, text, sc, w, h):
    """A caption in the lower left corner of the frame, lit by nothing."""
    curve = bpy.data.curves.new("label", "FONT")
    curve.body = text
    hw, hh = view_half(cam, w, h)
    curve.size = hh * 2 * 15.0 / h              # 15 pixel capitals
    curve.space_line = 1.25
    o = bpy.data.objects.new("label", curve)
    sc.collection.objects.link(o)
    m = bpy.data.materials.new("label")
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = (1, 1, 1, 1)
    em.inputs["Strength"].default_value = 1.6
    nt.links.new(em.outputs[0], out.inputs["Surface"])
    curve.materials.append(m)
    o.parent = cam
    lines = text.count("\n")
    o.location = (-hw * 0.95, -hh * 0.92 + lines * curve.size * 1.25, -1.0)
    o.visible_shadow = False
    return o


def render_tile(sc, path, w, h):
    sc.render.resolution_x, sc.render.resolution_y = w, h
    sc.render.resolution_percentage = 100
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    return load_pixels(path, "sRGB")[..., :3]


def tile_shot(names, labels, azimuths, w, h, prefix, elevation=9.0, lens=58.0, zoom=0.84):
    """One piece (or several LODs of it) alone on neutral ground, from each azimuth."""
    tiles = []
    for name, label in zip(names, labels):
        sc, sun, cam = render_setup()
        cam.data.lens = lens
        add_plane("ground", (4000, 4000), plain_material("ground", (0.20, 0.20, 0.20)))
        obj = import_piece(name)
        lo, hi = bounds(obj)
        centre = (lo + hi) / 2
        radius = float(np.linalg.norm(hi - lo)) / 2
        sc.render.resolution_x, sc.render.resolution_y = w, h
        cam.data.sensor_fit = "AUTO"
        dist = radius / min(view_half(cam, w, h)) * zoom
        text = add_label(cam, label, sc, w, h)
        for az in azimuths:
            a, e = math.radians(az), math.radians(elevation)
            eye = centre + np.array([math.cos(a) * math.cos(e), math.sin(a) * math.cos(e), math.sin(e)]) * dist
            eye[2] = max(eye[2], 0.4)
            aim_camera(cam, eye, centre)
            aim_sun(sun, az + 62.0)       # raking light from the camera's right
            text.hide_render = az != azimuths[0]
            tiles.append(render_tile(sc, os.path.join(TMP, f"{prefix}_{name}_{int(az) % 360}.png"), w, h))
    return tiles


def assemble(rows, path):
    """rows: lists of (h, w, 3) tiles, bottom row first inside each tile."""
    sheet = np.concatenate([np.concatenate(r, axis=1) for r in rows[::-1]], axis=0)
    os.makedirs(ART, exist_ok=True)
    write_png(path, to_u8(sheet))
    log("wrote", path, sheet.shape)


def size_text(name):
    doc, binary = read_glb(os.path.join(OUT, name + ".glb"))
    prim = doc["meshes"][0]["primitives"][0]
    acc = doc["accessors"][prim["attributes"]["POSITION"]]
    s = np.array(acc["max"]) - np.array(acc["min"])
    tris = doc["accessors"][prim["indices"]]["count"] // 3
    return s, tris


def render_sheet():
    cliffs = [p["name"] for p in PIECES if p["kind"] == "cliff"]
    small = [p["name"] for p in PIECES if p["kind"] != "cliff"]

    def labels(names, sep="   "):
        out = []
        for n in names:
            s, t = size_text(n)
            out.append(f"{n}{sep}{s[0]:.1f} x {s[1]:.1f} x {s[2]:.1f} m   {t} tris")
        return out

    top = tile_shot(cliffs, labels(cliffs, "\n"), (-62.0, -140.0), 320, 400, "sheet", elevation=5.0, zoom=0.74)
    low = tile_shot(small, labels(small), (-60.0, 135.0), 384, 340, "sheet", elevation=11.0)
    row1 = [low[i * 2] for i in range(5)]
    row2 = [low[i * 2 + 1] for i in range(5)]
    assemble([top, row1, row2], os.path.join(ART, "sheet.png"))


def render_lods():
    rows = []
    for base, az, el in (("boulder_a", -60.0, 11.0), ("cliff_a", -68.0, 6.0)):
        names = [base + s for s in LOD_SUFFIX]
        labels = []
        for n in names:
            s, t = size_text(n)
            labels.append(f"{n}   {t} triangles")
        rows.append(tile_shot(names, labels, (az,), 640, 540, "lods", elevation=el))
    assemble(rows, os.path.join(ART, "lods.png"))


def noise_material(name, colours, scale, bump, patch=None):
    """Render staging only, not part of the kit: a surface of two colours
    mixed by noise, with an optional third in patches, and a bump."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    b = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    coord = nt.nodes.new("ShaderNodeTexCoord")

    def noise(sc_, detail):
        n = nt.nodes.new("ShaderNodeTexNoise")
        n.inputs["Scale"].default_value = sc_
        n.inputs["Detail"].default_value = detail
        n.inputs["Roughness"].default_value = 0.62
        nt.links.new(coord.outputs["Object"], n.inputs["Vector"])
        return n

    def ramp(src, lo, hi, c0, c1):
        r = nt.nodes.new("ShaderNodeValToRGB")
        r.color_ramp.elements[0].position = lo
        r.color_ramp.elements[0].color = (*c0, 1)
        r.color_ramp.elements[1].position = hi
        r.color_ramp.elements[1].color = (*c1, 1)
        nt.links.new(src.outputs["Fac"], r.inputs["Fac"])
        return r

    base = ramp(noise(scale, 7), 0.38, 0.64, colours[0], colours[1])
    colour = base.outputs["Color"]
    if patch is not None:
        mask = ramp(noise(patch[1], 9), patch[2], patch[2] + 0.06, (0, 0, 0), (1, 1, 1))
        mix = nt.nodes.new("ShaderNodeMixRGB")
        mix.inputs[2].default_value = (*patch[0], 1)
        nt.links.new(mask.outputs["Color"], mix.inputs[0])
        nt.links.new(colour, mix.inputs[1])
        colour = mix.outputs["Color"]
    fine = noise(scale * 22, 6)
    shade = nt.nodes.new("ShaderNodeMixRGB")
    shade.blend_type = "MULTIPLY"
    shade.inputs[0].default_value = 0.7
    nt.links.new(colour, shade.inputs[1])
    nt.links.new(ramp(fine, 0.25, 0.75, (0.45, 0.45, 0.45), (1, 1, 1)).outputs["Color"], shade.inputs[2])
    nt.links.new(shade.outputs["Color"], b.inputs["Base Color"])
    bm_ = nt.nodes.new("ShaderNodeBump")
    bm_.inputs["Strength"].default_value = 1.0
    bm_.inputs["Distance"].default_value = bump
    nt.links.new(fine.outputs["Fac"], bm_.inputs["Height"])
    nt.links.new(bm_.outputs["Normal"], b.inputs["Normal"])
    b.inputs["Roughness"].default_value = 0.95
    return m


WALL = dict(
    slope_deg=60.0, width=120.0, slope_length=120.0,
    # name, x centre, height of the piece's base above the valley floor
    cliffs=[("cliff_b", -23.5, 17.5), ("cliff_a", 1.0, 14.5), ("cliff_c", 28.0, 16.5)],
    talus_deg=33.0, scree=5200, seed=11,
)


def talus_crest(x):
    """Height where the scree fan meets the wall, along the band."""
    xs = [c[1] for c in WALL["cliffs"]]
    zs = [c[2] + 4.5 for c in WALL["cliffs"]]
    h = float(np.interp(x, xs, zs))
    h += 1.6 * math.sin(x * 0.21) + 1.0 * math.sin(x * 0.53 + 1.0)     # cones under the gullies
    fade = min(1.0, max(0.0, (WALL["width"] / 2 + 2.0 - abs(x)) / 16.0))  # the fan thins past the band
    return max(0.6, h * (0.25 + 0.75 * fade * fade * (3 - 2 * fade)))


def talus_point(x, t, tan_s, tan_t):
    """A point on the fan: t=0 at the crest, 1 at the toe. Concave, as scree is."""
    h = talus_crest(x)
    run = h / tan_t * 1.12
    z = h * (1 - t) ** 1.35
    return np.array([x, h / tan_s - run * t, z])


def render_wall():
    sc, sun, cam = render_setup(samples=192)
    rng = np.random.default_rng(WALL["seed"])
    a = math.radians(WALL["slope_deg"])
    tan_s, tan_t = math.tan(a), math.tan(math.radians(WALL["talus_deg"]))
    rock = tuple(TARGET_ALBEDO)
    turf = noise_material("slope", ((0.060, 0.085, 0.030), (0.150, 0.135, 0.060)), 0.045, 0.35,
                          patch=(tuple(TARGET_ALBEDO * 0.82), 0.045, 0.50))
    # The slope rises toward +Y from its foot at y=0. The camera is at -Y.
    length = WALL["slope_length"]
    add_plane("slope", (WALL["width"], length), turf,
              location=(0, math.cos(a) * length / 2, math.sin(a) * length / 2), rotation=(a, 0, 0))
    add_plane("floor", (WALL["width"], 420), turf, location=(0, -210, 0))

    pieces = {}
    for p in PIECES:
        obj = import_piece(p["name"])
        obj.location = (0, 0, -2000)          # the instance source, out of sight
        pieces[p["name"]] = obj
    middles = []
    for name, x, base_z in WALL["cliffs"]:
        src = pieces[name]
        n = len(src.data.vertices)
        co = np.empty(n * 3)
        src.data.vertices.foreach_get("co", co)
        co = co.reshape(n, 3)
        o = bpy.data.objects.new("wall_" + name, src.data)
        sc.collection.objects.link(o)
        # The piece's reference slope lies on the slope plane.
        o.location = (x, base_z / tan_s - slope_anchor(co), base_z)
        middles.append(base_z + co[:, 2].max() / 2)

    # The fan the scree lies on.
    cols, rows = 161, 30
    bm = bmesh.new()
    grid = []
    for i in range(cols):
        x = -WALL["width"] / 2 + WALL["width"] * i / (cols - 1)
        col = []
        for j in range(rows):
            t = j / (rows - 1) * 1.04 - 0.04       # starts a little inside the wall
            pt = talus_point(x, max(t, 0.0), tan_s, tan_t)
            if t < 0:
                pt += np.array([0.0, 2.5, 1.0])
            pt[2] += rng.normal(0, 0.10) - (0.06 if j == rows - 1 else 0.0)
            col.append(bm.verts.new(pt))
        grid.append(col)
    for i in range(cols - 1):
        for j in range(rows - 1):
            bm.faces.new((grid[i][j], grid[i][j + 1], grid[i + 1][j + 1], grid[i + 1][j]))
    me = bpy.data.meshes.new("talus")
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = True
    talus = bpy.data.objects.new("talus", me)
    sc.collection.objects.link(talus)
    me.materials.append(noise_material("gravel", (tuple(TARGET_ALBEDO * 0.75), tuple(TARGET_ALBEDO * 1.1)),
                                       0.5, 0.25))

    # Scree: the kit's boulders and slabs. Gravity sorts a fan: fines at the
    # crest, blocks at the toe, a few that rolled out onto the floor.
    small = [p["name"] for p in PIECES if p["kind"] != "cliff"]
    across = {}
    for name in small:
        lo, hi = bounds(pieces[name])
        across[name] = max(hi[0] - lo[0], hi[1] - lo[1])
    for i in range(WALL["scree"]):
        x = float(rng.uniform(-WALL["width"] / 2 + 1, WALL["width"] / 2 - 1))
        t = float(rng.uniform(0, 1)) ** 0.8
        runout = t > 0.93 and float(rng.uniform()) < 0.5
        want = 0.4 + 0.5 * float(rng.uniform()) ** 2 + 2.4 * t ** 2.2 * float(rng.uniform(0.25, 1.0))
        if float(rng.uniform()) < 0.025:
            want += float(rng.uniform(1.5, 3.2))
        pt = talus_point(x, min(t, 1.0), tan_s, tan_t)
        if runout:
            pt[1] -= float(rng.uniform(0, 1)) ** 2 * 22.0
            pt[2] = 0.0
        name = small[int(rng.integers(len(small)))]
        s = want / across[name]
        o = bpy.data.objects.new(f"scree_{i}", pieces[name].data)
        sc.collection.objects.link(o)
        o.scale = (s, s, s)
        dip = 0.0 if runout else -math.atan(1.35 * tan_t * (1 - t) ** 0.35) * 0.8
        o.rotation_euler = (float(rng.normal(0, 0.25)) + dip, float(rng.normal(0, 0.25)),
                            float(rng.uniform(0, 2 * math.pi)))
        o.location = (pt[0], pt[1], pt[2] - 0.22 * want)

    z_mid = float(np.mean(middles))
    y_face = z_mid / tan_s - 4.0
    cam.data.lens = 74.0
    aim_camera(cam, (0.0, y_face - 250.0, z_mid), (0.0, y_face, z_mid))
    aim_sun(sun, -90.0 + 56.0)
    sc.render.resolution_x, sc.render.resolution_y = 1920, 1080
    os.makedirs(ART, exist_ok=True)
    sc.render.filepath = os.path.join(ART, "wall.png")
    bpy.ops.render.render(write_still=True)
    log("wrote", sc.render.filepath, "camera height", round(z_mid, 2), "distance to the face 250")


def render_all(which):
    os.makedirs(TMP, exist_ok=True)
    if "sheet" in which:
        render_sheet()
    if "lods" in which:
        render_lods()
    if "wall" in which:
        render_wall()


# ----------------------------------------------------------------------- main

def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    only = None
    which = ("sheet", "lods", "wall")
    for i, a in enumerate(argv):
        if a == "--only":
            only = argv[i + 1].split(",")
        if a == "--shots":
            which = argv[i + 1].split(",")
    if "--verify-only" in argv:
        verify()
        return
    if "--render" in argv:
        render_all(which)
        return
    stats = []
    for piece in PIECES:
        if only and piece["name"] not in only:
            continue
        stats.append(build_piece(piece))
    os.makedirs(TMP, exist_ok=True)
    record = os.path.join(TMP, "build_stats.json")
    old = {}
    if os.path.exists(record):
        with open(record) as f:
            old = {s["name"]: s for s in json.load(f)}
    old.update({s["name"]: s for s in stats})
    with open(record, "w") as f:
        json.dump([old[p["name"]] for p in PIECES if p["name"] in old], f, indent=1)
    if not only:
        verify()


if __name__ == "__main__":
    try:
        main()
    except Exception:
        import traceback
        traceback.print_exc()
        print("ROCKKIT FAILED", flush=True)
        sys.exit(1)
