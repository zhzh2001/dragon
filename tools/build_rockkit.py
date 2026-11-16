"""Build the rock kit from Poly Haven scans and local Tripo meshes.

    blender -b --factory-startup --python tools/build_rockkit.py
    blender -b --factory-startup --python tools/build_rockkit.py -- --verify-only
    blender -b --factory-startup --python tools/build_rockkit.py -- --render
    blender -b --factory-startup --python tools/build_rockkit.py -- --only cliff_a,boulder_b

The default run downloads the scans into build-rockkit-cache/, builds the
available pieces at three levels of detail into assets/props/rockkit/, and then
verifies them. `--verify-only` checks the files on disk. `--render` writes the
three scan inspection renders to artifacts/rockkit/ from the files on disk.
Use --render --only NAME for individual front, side, top and close views.
Use --raw-faces --only NAME before choosing a Tripo wall orientation.
Use --metadata to regenerate placement data from the exported Tripo pieces.
Use --mounted --only NAME to inspect wall burial at 45 and 62 degrees.
Use --placed --only NAME to inspect flat-terrain burial from placement check points.
Use --repair-normals --only NAME to repeat the normal-only bake.
Install ffmpeg before encoding Tripo packed maps. Their JPEGs need full chroma resolution.
Use --probe-normals --only NAME to measure cage failures before repair.
Use --use-probes with repair only when the saved probes match the source and geometry.
Use --diagnostic to rebuild a rejected piece for inspection.
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
import subprocess
import shutil
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
SCAN_PIECES = list(PIECES)
TRIPO_PIECES = [
    dict(name="wall_a", kind="wall", asset="tripo-cliff-01.glb", fit=(0, 30), height=26,
         rejected="The close view retained a black cavity. The top showed a rectangular grid."),
    dict(name="wall_b", kind="wall", asset="tripo-cliff-03.glb", fit=(0, 22), height=34, rot=(0, 0, -90),
         rejected="The broad flat roof remained visible on 45-degree and 62-degree slopes."),
    dict(name="wall_c", kind="wall", asset="tripo-cliff-04.glb", fit=(0, 34), height=18, rot=(0, 0, -90),
         rejected="The roof remained broad and flat. The source showed regular masonry joints."),
    dict(name="tor_a", kind="crag", asset="tripo-cliff-05.glb", fit=(0, 22), height=24),
    dict(name="ridge_a", kind="crag", asset="tripo-ridge-01.glb", fit=(0, 40), height=16),
    dict(name="scree_a", kind="mound", asset="tripo-scree-01.glb", fit=(0, 20), height=7),
    dict(name="boulder_d", kind="boulder", asset="tripo-boulder-01.glb", fit=(0, 4.5)),
    dict(name="boulder_e", kind="boulder", asset="tripo-boulder-02.glb", fit=(0, 5)),
    dict(name="boulder_f", kind="boulder", asset="tripo-boulder-03.glb", fit=(1, 4)),
]
for piece in TRIPO_PIECES:
    piece.update(source="tripo", cuts=[((0.5, 0.5, 0.035), (0, 0, -1))])
PIECES += TRIPO_PIECES
BUDGET = {"boulder": 8000, "slab": 8000, "cliff": 20000, "wall": 20000,
          "crag": 20000, "mound": 12000}
TRIPO_ART = os.path.join(ART, "tripo")


def check_write_paths():
    """Reject output paths that resolve outside this worktree."""
    for path in (CACHE, OUT, ART, TMP, TRIPO_ART):
        if os.path.commonpath((ROOT, os.path.realpath(path))) != ROOT:
            raise KitError(f"Output path left the worktree: {path}")
        if os.path.isdir(path):
            for base, dirs, files in os.walk(path, followlinks=False):
                for name in dirs + files:
                    child = os.path.join(base, name)
                    if os.path.islink(child):
                        raise KitError(f"Output path contained a symlink: {child}")


def source_path(piece):
    return os.path.join(ROOT, "assets", "tripo-raw", piece["asset"])


def local_tripo(piece, normal_only=False):
    """Copy embedded maps into the local cache. The source stays read-only."""
    path = source_path(piece)
    doc, binary = read_glb(path)
    mat = doc["materials"][0]
    pbr = mat["pbrMetallicRoughness"]
    maps = {}
    os.makedirs(TMP, exist_ok=True)
    for key, texture in (("diff", pbr["baseColorTexture"]),
                         ("nor_gl", mat["normalTexture"]),
                         ("arm", pbr["metallicRoughnessTexture"])):
        if normal_only and key != "nor_gl":
            continue
        blob, mime = image_blob(doc, binary, doc["textures"][texture["index"]]["source"])
        target = os.path.join(TMP, piece["name"] + "_source_" + key +
                              (".png" if mime == "image/png" else ".jpg"))
        with open(target, "wb") as f:
            f.write(blob)
        maps[key] = target
    if normal_only:
        return path, maps
    arm = load_pixels(maps["arm"])[..., :3]
    arm[..., 0] = 1.0
    arm[..., 2] = 0.0
    maps["arm"] = os.path.join(TMP, piece["name"] + "_source_orm.png")
    write_png(maps["arm"], to_u8(arm))
    maps["constant_ao"] = True
    return path, maps


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


def write_jpeg(path, rgb_bottom_first_u8, quality, packed=False):
    h, w, _ = rgb_bottom_first_u8.shape
    if packed:
        # Separate channels need full chroma resolution to keep metallic blue zero.
        encoder = shutil.which("ffmpeg")
        check(encoder is not None, "Packed JPEG encoding needs ffmpeg. Install ffmpeg.")
        result = subprocess.run([encoder, "-v", "error", "-f", "rawvideo", "-pixel_format", "rgb24",
                                 "-video_size", f"{w}x{h}", "-i", "pipe:0", "-frames:v", "1",
                                 "-c:v", "mjpeg", "-pix_fmt", "yuvj444p", "-q:v", "2",
                                 "-f", "image2pipe", "pipe:1"],
                                input=rgb_bottom_first_u8[::-1].tobytes(), capture_output=True, check=True)
        with open(path, "wb") as f:
            f.write(result.stdout)
        return
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
    before = set(bpy.context.scene.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    meshes = [o for o in bpy.context.scene.objects if o not in before and o.type == "MESH"]
    if not meshes:
        raise KitError(f"{path}: no mesh")
    activate(meshes[0], meshes)
    if len(meshes) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    for o in list(bpy.context.scene.objects):
        if o != obj and o not in before:
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
    if piece.get("source") == "tripo":
        obj.rotation_mode = "XYZ"
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
    if "height" in piece:
        lo, hi = bounds(obj)
        transform(obj, scale=(1, 1, piece["height"] / (hi[2] - lo[2])))
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
    if maps.get("constant_ao"):
        trials = [bake_pass(sc, src, low, "normal", size, extrusion * factor) * 2 - 1
                  for factor in (0.1, 0.35)] + [tn]
        tn, stats["normal_projection"] = combine_normal_trials(trials, out["object_normal"].max(axis=2) > 1e-6)
        stats["normal_projection"]["cage_metres"] = [extrusion * factor for factor in (0.1, 0.35, 1.0)]
    tn /= np.maximum(np.linalg.norm(tn, axis=2, keepdims=True), 1e-6)
    tn[~valid] = (0.0, 0.0, 1.0)
    orm = out["arm"].copy()
    rough = orm[..., 1]
    orm[..., 1] = np.clip(rough + (TARGET_ROUGH - rough[valid].mean()), 0.45, 1.0)
    orm[..., 0] = 1.0 if maps.get("constant_ao") else np.clip(orm[..., 0], 0.0, 1.0)
    orm[..., 2] = 0.0

    base, _ = dilate_fill(base, valid, 24)
    base[~_] = base[valid].mean(axis=0)
    if maps.get("constant_ao"):
        base, residual = correct_mesh_light(low, base, normals, valid, rock)
        stats["mesh_light_after"] = residual
    orm, v2 = dilate_fill(orm, valid, 24)
    orm[~v2] = orm[valid].mean(axis=0)
    tn, _ = dilate_fill(tn, valid, 24)
    tn /= np.maximum(np.linalg.norm(tn, axis=2, keepdims=True), 1e-6)
    stats["mean_albedo"] = [round(float(x), 4) for x in base[valid].mean(axis=0)]
    low.data.materials.clear()
    for polygon in low.data.polygons:
        polygon.material_index = 0
    return dict(base=base, normal=tn, orm=orm), stats


def combine_normal_trials(trials, mask):
    """Prefer the nearest outward hit. Fill rejected projections from nearby valid texels."""
    combined = np.zeros((*mask.shape, 3), dtype=np.float64)
    good = np.zeros(mask.shape, dtype=bool)
    all_flat = mask.copy()
    contributions = []
    for normal in trials:
        flat = np.max(np.abs(normal - (0, 0, 1)), axis=2) < 1e-6
        unit = np.abs(np.linalg.norm(normal, axis=2) - 1) < 0.01
        take = mask & (normal[..., 2] > 0) & unit & ~flat & ~good
        combined[take] = normal[take]
        contributions.append(float(take[mask].mean()))
        good |= take
        all_flat &= flat
    combined[all_flat] = (0, 0, 1)
    good |= all_flat
    invalid = float((mask & ~good).sum() / mask.sum())
    combined, good = dilate_fill(combined, good, 24)
    unfilled = float((mask & ~good).sum() / mask.sum())
    check(unfilled < 0.0001, f"Normal projection left {unfilled:.2%} of the surface uncovered. Inspect the bake cage.")
    combined[~good] = (0, 0, 1)
    combined /= np.maximum(np.linalg.norm(combined, axis=2, keepdims=True), 1e-6)
    return combined, dict(cage_factors=[0.1, 0.35, 1.0], contribution_fractions=contributions,
                          flat_fraction=float(all_flat[mask].mean()), invalid_filled_fraction=invalid,
                          unfilled_surface_fraction=unfilled,
                          negative_z_fraction=float((combined[..., 2] < 0).mean()),
                          mean=combined.mean(axis=(0, 1)).tolist())


def correct_mesh_light(low, base, normals, valid, rock):
    """Remove residual light using the exported mesh's area-weighted UV samples."""
    mesh = low.data
    indices = np.array([loop.vertex_index for loop in mesh.loops])
    positions = np.array([v.co[:] for v in mesh.vertices])
    directions = np.array([n.vector[:] for n in mesh.corner_normals])
    uv = np.array([item.uv[:] for item in mesh.uv_layers[0].data])
    weight = np.array([p.area for p in mesh.polygons]).repeat(3)
    height = positions[:, 2].max() - positions[:, 2].min()
    seen = positions[indices, 2] > height * 0.03
    root_weight = np.sqrt(weight[seen])
    matrix = np.column_stack((np.ones(seen.sum()), directions[seen])) * root_weight[:, None]
    h, w, _ = base.shape
    x = np.clip((uv[:, 0] * w).astype(int), 0, w - 1)
    y = np.clip((uv[:, 1] * h).astype(int), 0, h - 1)
    field, _ = dilate_fill(normals, valid, 24)
    field /= np.maximum(np.linalg.norm(field, axis=2, keepdims=True), 1e-6)
    residuals = []
    for _ in range(5):
        luminance = base[y, x] @ LUMA
        coeff, *_ = np.linalg.lstsq(matrix, np.log(np.maximum(luminance[seen], 1e-4)) * root_weight, rcond=None)
        residuals.append(round(float(np.linalg.norm(coeff[1:])), 5))
        if residuals[-1] < 0.055:
            break
        base = np.clip(base * np.exp(-(field @ coeff[1:]))[..., None], 0.012, 0.85)
        base *= TARGET_ALBEDO / base[rock].mean(axis=0)
    return base, residuals


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
        write_jpeg(stem + "_orm.jpg", to_u8(orm), 88,
                   packed=name in {p["name"] for p in TRIPO_PIECES})
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


def make_lod(low, ratio, protect_outline=False):
    activate(low)
    bpy.ops.object.duplicate()
    o = bpy.context.view_layer.objects.active
    mod = o.modifiers.new("dec", "DECIMATE")
    mod.ratio = ratio
    mod.use_collapse_triangulate = True
    if protect_outline:
        # Keep the outer vertices in each narrow strip of all three projections.
        pos = np.array([v.co[:] for v in o.data.vertices])
        lo, hi = pos.min(axis=0), pos.max(axis=0)
        chosen = set()
        for axis in range(3):
            bucket = np.minimum(((pos[:, axis] - lo[axis]) / (hi[axis] - lo[axis]) * 160).astype(int), 159)
            for other in range(3):
                if axis == other:
                    continue
                for band in range(160):
                    ix = np.flatnonzero(bucket == band)
                    if len(ix):
                        chosen.update((int(ix[np.argmin(pos[ix, other])]), int(ix[np.argmax(pos[ix, other])])))
        group = o.vertex_groups.new(name="outline")
        group.add(list(range(len(pos))), 1.0, "REPLACE")
        group.add(list(chosen), 0.01, "REPLACE")
        mod.vertex_group = group.name
        mod.vertex_group_factor = 300
    apply_modifier(o, mod)
    return o


def build_piece(piece):
    t0 = time.time()
    name = piece["name"]
    sc = reset_scene()
    use_gpu(sc)
    path, maps = local_tripo(piece) if piece.get("source") == "tripo" else download(piece["asset"])
    src = import_scan(path)
    scan_tris = tri_count(src)
    scale, islands, dropped = prepare_source(src, piece)
    mats = source_materials(src, maps)
    low = make_lod0(src, BUDGET[piece["kind"]])
    if piece.get("source") == "tripo":
        repaired = low.data.validate(verbose=True, clean_customdata=False)
        log(name, "decimated mesh repairs:", repaired)
        lo, hi = bounds(low)
        shift = (-(lo[0] + hi[0]) / 2, -(lo[1] + hi[1]) / 2, -lo[2])
        transform(low, move=shift)
        transform(src, move=shift)
    unwrap(low)
    for p in low.data.polygons:
        p.use_smooth = True
    baked, stats = bake_maps(sc, src, low, maps, mats, LOD_MAPS[0], name)
    images = write_maps(baked, name)
    os.makedirs(OUT, exist_ok=True)
    tris = []
    for lod, ratio in enumerate(LOD_RATIOS):
        obj = low if lod == 0 else make_lod(low, ratio, protect_outline=name == "scree_a" and lod == 2)
        if lod and piece.get("source") == "tripo":
            repaired = obj.data.validate(verbose=True, clean_customdata=False)
            log(name, f"LOD{lod} mesh repairs:", repaired)
        tris.append(tri_count(obj))
        export_glb(obj, os.path.join(OUT, name + LOD_SUFFIX[lod] + ".glb"),
                   name + LOD_SUFFIX[lod], images[lod])
    lo, hi = bounds(low)
    stats.update(name=name, asset=piece["asset"], scan_triangles=scan_tris,
                 scale=round(scale, 3), triangles=tris, islands_dropped=dropped,
                 size_blender_xyz=[round(float(x), 2) for x in hi - lo],
                 seconds=round(time.time() - t0, 1))
    log("BUILT", json.dumps(stats))
    if piece.get("source") == "tripo":
        os.makedirs(TRIPO_ART, exist_ok=True)
        stats.update(source=piece["asset"], rotation_blender=piece.get("rot", (0, 0, 0)),
                     source_alignment_shift=[float(value) for value in shift],
                     occlusion="constant 1.0", full_high_mesh_normal_bake=True)
        with open(os.path.join(TRIPO_ART, name + "-build.json"), "w") as f:
            json.dump(stats, f, indent=2)
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
    for label, values in (("positions", pos), ("normals", nrm), ("tangents", tan), ("UVs", uv)):
        check(np.isfinite(values).all(), f"{name}: {label} contained non-finite values")
    check(idx.min() >= 0 and idx.max() < len(pos), f"{name}: triangle index left the vertex buffer")
    if piece.get("source") == "tripo":
        _, merged = np.unique(pos, axis=0, return_inverse=True)
        faces = np.sort(merged[idx], axis=1)
        check(len(np.unique(faces, axis=0)) == len(faces), f"{name}: duplicate geometric triangles remained")
        area = np.linalg.norm(np.cross(pos[idx[:, 1]] - pos[idx[:, 0]], pos[idx[:, 2]] - pos[idx[:, 0]]), axis=1)
        check(float(area.min()) > 1e-12, f"{name}: a triangle had zero area")
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
    check(abs(n[..., 0].mean()) < 0.05 and abs(n[..., 1].mean()) < 0.05,
          f"{name}: tangent normal directions had a large bias")
    if piece.get("source") == "tripo":
        check(float(n[..., 2].min()) > 0, f"{name}: a tangent normal pointed into the surface")
        check(float(np.abs(np.linalg.norm(n, axis=2) - 1).max()) < 0.035,
              f"{name}: a tangent normal exceeded the quantization error bound")
    else:
        check(n[..., 2].mean() > 0.80, f"{name}: normal map is not tangent space")
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
                mean_albedo=mean_albedo, light=light, bytes=os.path.getsize(path),
                normal_min_z=float(n[..., 2].min()), normal_mean_z=float(n[..., 2].mean()),
                normal_max_unit_error=float(np.abs(np.linalg.norm(n, axis=2) - 1).max()))


def verify(selected=None):
    reset_scene()
    lines, means = [], {}
    total = 0
    if selected is None:
        selected = [p for p in PIECES if not p.get("rejected")]
    check(bool(selected), "No pieces selected for verification")
    failures = []
    for piece in selected:
        try:
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
            elif piece.get("source") == "tripo":
                check(abs(x - piece["fit"][1]) < 0.1, f"{name}: width missed the target")
                check(abs(y - piece["height"]) < 0.1, f"{name}: height missed the target")
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
            if piece.get("source") == "tripo":
                check(float(np.abs(a["mean_albedo"] / TARGET_ALBEDO - 1).max()) <= ALBEDO_TOLERANCE,
                      f"{name}: albedo missed the original kit target")
            means[name] = a["mean_albedo"]
            total += sum(i["bytes"] for i in info)
            normal_line = ""
            if piece.get("source") == "tripo":
                normal_line = (f"  normal texels: outward; minimum Z {min(i['normal_min_z'] for i in info):.4f}; "
                               f"maximum unit error {max(i['normal_max_unit_error'] for i in info):.4f}; "
                               f"LOD0 mean Z {a['normal_mean_z']:.4f}\n")
            lines.append(f"VERIFY {name}: PASS\n"
                         f"  glTF Y-up size X/Y/Z metres: ({x:.2f}, {y:.2f}, {z:.2f}); base y={a['lo'][1]:.3f}; origin offset ({cx:.3f}, {cz:.3f})\n"
                         f"  triangles LOD0/1/2: {a['tris']}/{info[1]['tris']}/{info[2]['tris']} ({r1:.1%}, {r2:.1%}); budget {BUDGET[kind]}\n"
                         f"  silhouette overlap with LOD0 (worst of front, side, top): LOD1 {ious[0]:.3f}, LOD2 {ious[1]:.3f}\n"
                         f"  maps base/normal/ORM: {LOD_MAPS[0]}/{LOD_MAPS[1]}/{LOD_MAPS[2]} px per LOD; normal PNG; ORM packed, metallic 0; tangents: PASS\n"
                         f"{normal_line}  mean linear albedo: ({a['mean_albedo'][0]:.4f}, {a['mean_albedo'][1]:.4f}, {a['mean_albedo'][2]:.4f}); directional light term {a['light']:.3f} (limit {LIGHT_LIMIT})\n"
                         f"  bytes LOD0/1/2: {info[0]['bytes']}/{info[1]['bytes']}/{info[2]['bytes']}")
        except KitError as error:
            failures.append(f"VERIFY {piece['name']}: FAIL\n  {error}")
    if failures:
        report = "\n".join(lines + failures) + "\n"
        print(report, flush=True)
        os.makedirs(TRIPO_ART, exist_ok=True)
        with open(os.path.join(TRIPO_ART, "verify.txt"), "w") as f:
            f.write(report)
        raise KitError(f"Verification failed for {len(failures)} pieces. Read the per-piece results.")
    kit = np.mean(list(means.values()), axis=0)
    kit_lum = float(kit @ LUMA)
    worst = 0.0
    for name, m in means.items():
        dev = max(float(np.abs(m / kit - 1).max()), abs(float(m @ LUMA) / kit_lum - 1))
        worst = max(worst, dev)
        check(dev <= ALBEDO_TOLERANCE, f"{name}: mean albedo is {dev:.1%} from the kit mean, limit 8%")
    on_disk = sum(os.path.getsize(os.path.join(OUT, f)) for f in os.listdir(OUT))
    extra = sorted(set(os.listdir(OUT)) - {p["name"] + suffix + ".glb" for p in PIECES for suffix in LOD_SUFFIX} - {"pieces.json"})
    check(not extra, f"unexpected files in the kit directory: {extra}")
    scan_bytes = sum(os.path.getsize(os.path.join(OUT, p["name"] + suffix + ".glb"))
                     for p in SCAN_PIECES for suffix in LOD_SUFFIX)
    check(scan_bytes <= SIZE_LIMIT, "The original scan kit exceeded 80 MB")
    summary = (f"VERIFY selection: PASS\n"
               f"  selection mean linear albedo: ({kit[0]:.4f}, {kit[1]:.4f}, {kit[2]:.4f}); worst piece deviation {worst:.2%} (limit 8%)\n"
               f"  selected bytes: {total}; directory size: {on_disk / 1e6:.2f} MB; original scan kit: {scan_bytes / 1e6:.2f} MB of 80 MB")
    report = "\n".join(lines + [summary]) + "\n"
    print(report, flush=True)
    if any(p.get("source") == "tripo" for p in selected):
        os.makedirs(TRIPO_ART, exist_ok=True)
        with open(os.path.join(TRIPO_ART, "verify.txt"), "w") as f:
            f.write(report)



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
    cliffs = [p["name"] for p in SCAN_PIECES if p["kind"] == "cliff"]
    small = [p["name"] for p in SCAN_PIECES if p["kind"] != "cliff"]

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
    small = [p["name"] for p in SCAN_PIECES if p["kind"] != "cliff"]
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


def measure_alignment(selected):
    """Recover the source translation used when LOD0 was centred after decimation."""
    report = {}
    for piece in selected:
        reset_scene()
        src = import_scan(source_path(piece))
        prepare_source(src, piece)
        low = make_lod0(src, BUDGET[piece["kind"]])
        low.data.validate(clean_customdata=False)
        lo, hi = bounds(low)
        shift = np.array([-(lo[0] + hi[0]) / 2, -(lo[1] + hi[1]) / 2, -lo[2]])
        path = os.path.join(TRIPO_ART, piece["name"] + "-build.json")
        with open(path) as f:
            build_stats = json.load(f)
        build_stats["source_alignment_shift"] = shift.tolist()
        with open(path, "w") as f:
            json.dump(build_stats, f, indent=2)
        area = sum(p.area for p in low.data.polygons)
        texel = math.sqrt(area / (2048 * 2048 * build_stats["atlas_coverage"]))
        report[piece["name"]] = dict(source_alignment_shift=shift.tolist(),
                                     shift_metres=float(np.linalg.norm(shift)),
                                     average_surface_texel_metres=texel,
                                     shift_in_average_texels=float(np.linalg.norm(shift)) / texel,
                                     tight_cage_metres=float(np.linalg.norm(hi - lo)) * 0.0012)
        log("SOURCE ALIGNMENT", piece["name"], json.dumps(report[piece["name"]]))
        with open(os.path.join(TRIPO_ART, "source-alignment.json"), "w") as f:
            json.dump(report, f, indent=2)


def prepare_normal_bake(piece):
    """Load the full source and the exported LOD0 atlas for a normal-only bake."""
    sc = reset_scene()
    use_gpu(sc)
    path, maps = local_tripo(piece, normal_only=True)
    source_pixels = load_pixels(maps["nor_gl"])[::4, ::4, :3] * 2 - 1
    source_stats = dict(mean=source_pixels.mean(axis=(0, 1)).tolist(),
                        negative_z_fraction=float((source_pixels[..., 2] < 0).mean()),
                        mean_length=float(np.linalg.norm(source_pixels, axis=2).mean()))
    src = import_scan(path)
    prepare_source(src, piece)
    record = os.path.join(TRIPO_ART, piece["name"] + "-build.json")
    with open(record) as f:
        build_stats = json.load(f)
    if "source_alignment_shift" in build_stats:
        transform(src, move=build_stats["source_alignment_shift"])
    mats = source_materials(src, maps)
    set_source_mode(mats, "normal", maps, {})
    low = import_piece(piece["name"])
    low.data.materials.clear()
    target = bpy.data.materials.new("normal_probe")
    target.use_nodes = True
    target.node_tree.nodes.new("ShaderNodeTexImage").name = "bake_target"
    low.data.materials.append(target)
    lo, hi = bounds(low)
    extrusion = float(np.linalg.norm(hi - lo)) * 0.012
    object_map = bake_pass(sc, src, low, "object_normal", 2048, extrusion)
    mask = object_map.max(axis=2) > 1e-6
    return sc, src, low, mask, extrusion, source_stats


def probe_normals(selected):
    """Measure normal projection failures at several cage distances."""
    report = {}
    for piece in selected:
        sc, src, low, mask, extrusion, source_stats = prepare_normal_bake(piece)
        trials = []
        for factor in (1.0, 0.35, 0.1, 2.0):
            pixels = bake_pass(sc, src, low, "normal", 2048, extrusion * factor)
            normal = pixels * 2 - 1
            length = np.linalg.norm(normal, axis=2)
            missed = pixels.max(axis=2) < 1e-6
            hit = mask & ~missed
            trial = dict(cage_metres=extrusion * factor, factor=factor,
                         missing_fraction=float(missed[mask].mean()),
                         negative_z_fraction=float((normal[hit, 2] < 0).mean()),
                         mean=normal[hit].mean(axis=0).tolist(),
                         mean_unit_error=float(np.abs(length[hit] - 1).mean()))
            trials.append(trial)
            log("NORMAL PROBE", piece["name"], json.dumps(trial))
            np.savez_compressed(os.path.join(TMP, f"{piece['name']}-normal-probe-{factor}.npz"),
                                normal=normal.astype(np.float32), mask=mask, missed=missed)
        report[piece["name"]] = dict(source_normal=source_stats, trials=trials)
        with open(os.path.join(TRIPO_ART, "normal-probes.json"), "w") as f:
            json.dump(report, f, indent=2)


def replace_normal_map(piece, normal):
    """Replace each embedded normal map. Preserve geometry and the other encoded maps."""
    for lod, size in enumerate(LOD_MAPS):
        path = os.path.join(OUT, piece["name"] + LOD_SUFFIX[lod] + ".glb")
        doc, binary = read_glb(path)
        images = [image_blob(doc, binary, i)[0] for i in range(3)]
        nrm = downsample(normal, normal.shape[0] // size)
        nrm /= np.maximum(np.linalg.norm(nrm, axis=2, keepdims=True), 1e-6)
        n8 = to_u8(nrm * 0.5 + 0.5).astype(np.int32)
        n8[..., :2] = np.minimum((n8[..., :2] + 1) // 2 * 2, 254)
        n8[..., 2] = np.minimum((n8[..., 2] + 2) // 4 * 4, 252)
        images[1] = png_bytes(n8.astype(np.uint8)[::-1])
        views = [image["bufferView"] for image in doc["images"]]
        check(views == list(range(len(doc["bufferViews"]) - 3, len(doc["bufferViews"]))),
              "Normal replacement found an unexpected buffer layout. Rebuild the piece.")
        data = bytearray(binary[:doc["bufferViews"][views[0]]["byteOffset"]])
        for view, blob in zip(views, images):
            data += b"\0" * (-len(data) % 4)
            doc["bufferViews"][view] = dict(buffer=0, byteOffset=len(data), byteLength=len(blob))
            data += blob
        data += b"\0" * (-len(data) % 4)
        doc["buffers"][0]["byteLength"] = len(data)
        write_glb(path, doc, data)


def repair_normals(selected, use_probes=False):
    """Repair reversed projections without changing valid normal strength or geometry."""
    record = os.path.join(TRIPO_ART, "normal-repairs.json")
    report = {}
    if os.path.exists(record):
        with open(record) as f:
            report = json.load(f)
    for piece in selected:
        check(piece.get("source") == "tripo", "Normal repair only supports Tripo pieces. Select a Tripo name.")
        name = piece["name"]
        doc, binary = read_glb(os.path.join(OUT, name + ".glb"))
        before = decode(*image_blob(doc, binary, 1)) * 2 - 1
        original = dict(mean=before.mean(axis=(0, 1)).tolist(),
                        negative_z_fraction=float((before[..., 2] < 0).mean()))
        paths = [os.path.join(TMP, f"{name}-normal-probe-{factor}.npz") for factor in (0.1, 0.35, 1.0)]
        if use_probes and all(os.path.exists(path) for path in paths):
            arrays = [np.load(path) for path in paths]
            trials, mask = [a["normal"] for a in arrays], arrays[0]["mask"]
            size, _ = size_text(name)
            extrusion = float(np.linalg.norm(size)) * 0.012
        else:
            sc, src, low, mask, extrusion, source_stats = prepare_normal_bake(piece)
            trials = [bake_pass(sc, src, low, "normal", 2048, extrusion * factor) * 2 - 1
                      for factor in (0.1, 0.35, 1.0)]
        normal, stats = combine_normal_trials(trials, mask)
        with open(os.path.join(TRIPO_ART, name + "-build.json")) as f:
            alignment = json.load(f).get("source_alignment_shift", [0, 0, 0])
        first = report.get(name, {}).get("original_before", report.get(name, {}).get("before", original))
        stats.update(before=original, original_before=first, source_alignment_shift=alignment,
                     cage_metres=[extrusion * factor for factor in (0.1, 0.35, 1.0)])
        replace_normal_map(piece, normal)
        report[name] = stats
        log("REPAIRED NORMALS", name, json.dumps(stats))
        with open(os.path.join(TRIPO_ART, "normal-repairs.json"), "w") as f:
            json.dump(report, f, indent=2)


def farthest_points(points, count):
    """Spread check points over the candidate surface."""
    points = np.unique(points, axis=0)
    chosen = [int(np.argmin(points[:, 1]))]
    while len(chosen) < min(count, len(points)):
        distance = ((points[:, None] - points[chosen]) ** 2).sum(axis=2).min(axis=1)
        chosen.append(int(np.argmax(distance)))
    return points[chosen[:count]]


def footprint_area(points):
    """Measure the convex footprint in glTF X/Z."""
    points = sorted(set((float(p[0]), float(p[2])) for p in points))
    if len(points) < 3:
        return 0.0
    def cross(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
    halves = []
    for sequence in (points, points[::-1]):
        half = []
        for point in sequence:
            while len(half) >= 2 and cross(half[-2], half[-1], point) <= 0:
                half.pop()
            half.append(point)
        halves += half[:-1]
    return abs(sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(halves, halves[1:] + halves[:1]))) / 2


def write_metadata():
    """Regenerate metadata for all accepted Tripo pieces with three exported LOD files."""
    selected = [p for p in TRIPO_PIECES if not p.get("rejected")
                and all(os.path.exists(os.path.join(OUT, p["name"] + suffix + ".glb")) for suffix in LOD_SUFFIX)]
    entries = []
    measurements = {}
    for piece in selected:
        path = os.path.join(OUT, piece["name"] + "_lod2.glb")
        doc, binary = read_glb(path)
        prim = doc["meshes"][0]["primitives"][0]
        pos = np.unique(accessor(doc, binary, prim["attributes"]["POSITION"]), axis=0).astype(float)
        lo, hi = pos.min(axis=0), pos.max(axis=0)
        size, _ = size_text(piece["name"])
        full_area = footprint_area(pos)
        for band in (0.035, 0.05, 0.075, 0.10, 0.125, 0.15, 0.20, 0.25):
            base = pos[pos[:, 1] <= lo[1] + size[1] * band]
            coverage = footprint_area(base) / full_area
            if len(base) >= 8 and coverage >= 0.25:
                break
        check(len(base) >= 8 and coverage >= 0.25, f"{piece['name']}: the lower foot had insufficient coverage")
        points = farthest_points(base, 16 if piece["kind"] == "wall" else 26)
        check(len(points) >= 3 and footprint_area(points) > 0, f"{piece['name']}: base checks had no footprint")
        item = dict(name=piece["name"], kind=piece["kind"], size=size.tolist(),
                    source=piece["asset"], placement="upright_yaw_only",
                    base_band_fraction=band, base_hull_coverage=coverage,
                    check_hull_coverage=footprint_area(points) / full_area)
        if piece["kind"] == "wall":
            back = []
            for y in np.linspace(lo[1], hi[1], 11)[:-1]:
                band = pos[(pos[:, 1] >= y) & (pos[:, 1] <= y + size[1] / 10)]
                if len(band):
                    back.append(band[np.argmin(band[:, 2])])
            points = np.concatenate((points, np.array(back)))
            probes = []
            for slope in (45, 53.5, 62):
                angle = math.radians(slope)
                normal = np.array([0, math.cos(angle), math.sin(angle)])
                offset = float((points @ normal).max()) + 0.15
                centre = [0, 0, offset / normal[2]]
                signed = pos @ normal - offset
                probes.append(dict(slope_degrees=slope, point=centre, normal=normal.tolist(),
                                   exposed_vertex_fraction=float((signed > 0).mean()),
                                   maximum_relief=float(signed.max()),
                                   maximum_check_distance=float((points @ normal - offset).max())))
            item["mount_plane"] = dict(point=probes[1]["point"], normal=probes[1]["normal"],
                                      reference_slope_degrees=53.5, burial_metres=0.15)
            measurements[piece["name"]] = probes
        check(len(points) <= 26, f"{piece['name']}: too many placement check points")
        check(all(np.any(np.all(pos == point, axis=1)) for point in points),
              f"{piece['name']}: placement check left the LOD2 vertices")
        item["check_points"] = points.tolist()
        entries.append(item)
    metadata = dict(version=1, axes="glTF: X right, Y up, Z face", units="metres",
                    placement_rule="Keep Y vertical. Rotate around Y to face downhill. Do not align the mount normal by a 3D rotation.",
                    ground_translation_rule="After yaw and horizontal placement, lower Y until every check point is at least 0.15 m below terrain.",
                    wall_plane_rule="For slope angle a, set n=(0,cos(a),sin(a)). Set d=max(dot(n,p)) + 0.15 over check_points. Set point=(0,0,d/n.z).",
                    wall_translation_rule="Translate the yaw-rotated mounting point onto the terrain plane. Check points must remain inside terrain.",
                    check_points_rule="Expand the lower band until it has eight vertices and 25 percent of the horizontal hull. Walls also check each rear height band.",
                    pieces=entries)
    with open(os.path.join(OUT, "pieces.json"), "w") as f:
        json.dump(metadata, f, indent=2)
    with open(os.path.join(TRIPO_ART, "placement-measurements.json"), "w") as f:
        json.dump(measurements, f, indent=2)
    log("wrote placement metadata", len(entries), "pieces")


def render_mounted(selected):
    """Show vertical wall placement on both endpoint slopes."""
    with open(os.path.join(OUT, "pieces.json")) as f:
        entries = {p["name"]: p for p in json.load(f)["pieces"]}
    for piece in selected:
        if piece["kind"] != "wall":
            continue
        points = np.array(entries[piece["name"]]["check_points"])
        for slope in (45, 62):
            sc, sun, cam = render_setup(samples=32)
            obj = import_piece(piece["name"])
            lo, hi = bounds(obj)
            centre = (lo + hi) / 2
            angle = math.radians(slope)
            normal = np.array([0, math.cos(angle), math.sin(angle)])
            offset = float((points @ normal).max()) + 0.15
            add_plane("terrain", (300, 300), plain_material("terrain", (0.14, 0.17, 0.12)),
                      location=(0, -offset / normal[2], 0), rotation=(angle, 0, 0))
            cam.data.type = "ORTHO"
            cam.data.ortho_scale = float(max(hi - lo)) * 1.3
            for label, azimuth, elevation in (("front", -90, 0.18), ("right", -40, 0.18),
                                               ("left", -140, 0.18), ("above", -90, 1.4)):
                az = math.radians(azimuth)
                eye = centre + np.array([math.cos(az), math.sin(az), elevation]) * float(max(hi - lo)) * 2
                aim_camera(cam, eye, centre)
                aim_sun(sun, azimuth + 62)
                render_tile(sc, os.path.join(TRIPO_ART, f"{piece['name']}-mounted-{slope}-{label}.png"), 1024, 1024)


def render_placed(selected):
    """Inspect flat-terrain burial from the shipped check points."""
    with open(os.path.join(OUT, "pieces.json")) as f:
        entries = {p["name"]: p for p in json.load(f)["pieces"]}
    report = {}
    for piece in selected:
        name = piece["name"]
        points = np.array(entries[name]["check_points"])
        burial = float(points[:, 1].max()) + 0.15
        sc, sun, cam = render_setup(samples=32)
        obj = import_piece(name)
        transform(obj, move=(0, 0, -burial))
        lo, hi = bounds(obj)
        centre, size = (lo + hi) / 2, hi - lo
        add_plane("ground", (500, 500), plain_material("ground", (0.20, 0.20, 0.20)))
        cam.data.type = "ORTHO"
        cam.data.ortho_scale = float(max(size)) * 1.20
        aim_camera(cam, centre + np.array([0, -1, 0.08]) * float(max(size)) * 2, centre)
        aim_sun(sun, -28)
        render_tile(sc, os.path.join(TRIPO_ART, name + "-placed-flat-front.png"), 1024, 1024)
        report[name] = dict(terrain="flat", margin=0.15, translation_y=-burial,
                            highest_check_point_y=float(points[:, 1].max()) - burial)
    with open(os.path.join(TRIPO_ART, "placed-flat-measurements.json"), "w") as f:
        json.dump(report, f, indent=2)


def render_tripo(selected, raw=False):
    """Render cardinal faces and surface details under the kit's light."""
    os.makedirs(TRIPO_ART, exist_ok=True)
    for piece in selected:
        sc, sun, cam = render_setup(samples=32)
        obj = import_scan(source_path(piece)) if raw else import_piece(piece["name"])
        lo, hi = bounds(obj)
        if raw:
            span = float(max(hi - lo))
            transform(obj, scale=(20 / span,) * 3)
            lo, hi = bounds(obj)
            transform(obj, move=(-(lo[0]+hi[0])/2, -(lo[1]+hi[1])/2, -lo[2]))
            lo, hi = bounds(obj)
        centre = (lo + hi) / 2
        size = hi - lo
        add_plane("ground", (500, 500), plain_material("ground", (0.20, 0.20, 0.20)))
        cam.data.type = "ORTHO"
        cam.data.ortho_scale = float(max(size)) * 1.20
        views = [("front", (0,-1,0.08)), ("side", (1,0,0.08)), ("top", (0,-0.001,1))]
        if raw:
            views = [("plus_z", (0,-1,0.05)), ("plus_x", (1,0,0.05)),
                     ("minus_z", (0,1,0.05)), ("minus_x", (-1,0,0.05))]
        else:
            views.append(("close", (0,-1,0.08)))
        for label, direction in views:
            target = centre.copy()
            if label == "close":
                cam.data.ortho_scale = float(max(size)) * 0.29
                target[2] = size[2] * 0.56
            eye = target + np.array(direction) * float(max(size)) * 2
            aim_camera(cam, eye, target)
            aim_sun(sun, math.degrees(math.atan2(direction[1], direction[0])) + 62)
            path = os.path.join(TRIPO_ART, piece["name"] + ("-raw-" if raw else "-") + label + ".png")
            render_tile(sc, path, 1024, 1024)
        if not raw and piece["kind"] == "wall":
            cam.data.type = "PERSP"
            cam.data.lens = 46
            eye = centre.copy()
            eye[1] = lo[1] - 50
            aim_camera(cam, eye, centre)
            aim_sun(sun, -28)
            render_tile(sc, os.path.join(TRIPO_ART, piece["name"] + "-front-50m.png"), 1200, 1000)


# ----------------------------------------------------------------------- main

def main():
    check_write_paths()
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    only = None
    which = ("sheet", "lods", "wall")
    for i, a in enumerate(argv):
        if a == "--only":
            only = argv[i + 1].split(",")
        if a == "--shots":
            which = argv[i + 1].split(",")
    selected = [p for p in PIECES if not only or p["name"] in only]
    if only:
        check(set(only) <= {p["name"] for p in PIECES}, "Unknown piece in --only")
    reading_outputs = "--verify-only" in argv or "--render" in argv or "--metadata" in argv or "--mounted" in argv or "--placed" in argv
    if reading_outputs and not only:
        selected = [p for p in selected if not p.get("rejected")]
    if not reading_outputs:
        for piece in selected[:]:
            if piece.get("rejected") and "--diagnostic" not in argv:
                if only:
                    raise KitError(f"{piece['name']} failed visual review. Use --diagnostic to rebuild review evidence.")
                log("SKIP", piece["name"], piece["rejected"])
                selected.remove(piece)
                continue
            if piece.get("source") == "tripo" and not os.path.exists(source_path(piece)):
                log("SKIP", piece["name"], "missing source", piece["asset"])
                selected.remove(piece)
    if "--measure-alignment" in argv:
        measure_alignment(selected)
        return
    if "--repair-normals" in argv:
        repair_normals(selected, use_probes="--use-probes" in argv)
        return
    if "--probe-normals" in argv:
        probe_normals(selected)
        return
    if "--placed" in argv:
        render_placed(selected)
        return
    if "--mounted" in argv:
        render_mounted(selected)
        return
    if "--metadata" in argv:
        write_metadata()
        return
    if "--raw-faces" in argv:
        render_tripo([p for p in selected if p.get("source") == "tripo"], raw=True)
        return
    if "--verify-only" in argv:
        verify(selected)
        return
    if "--render" in argv:
        if only:
            render_tripo(selected)
        else:
            render_all(which)
        return
    stats = []
    for piece in selected:
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
