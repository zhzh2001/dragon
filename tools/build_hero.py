"""Build the hero dragon's static mesh from three generated sculpts.

    blender -b --factory-startup --python tools/build_hero.py
    blender -b --factory-startup --python tools/build_hero.py -- --verify-only
    blender -b --factory-startup --python tools/build_hero.py -- --render
    blender -b --factory-startup --python tools/build_hero.py -- --stage low2,join
    blender -b --factory-startup --python tools/build_hero.py -- --landmarks

Version 4. The torso, neck, legs and tail come from a body sculpt that was
generated without wings (HERO_BODY names it). The wings come from the first
whole-body sculpt, as in version 2, and that sculpt sets the frame. The head
comes from the head sculpt. HERO_WING=v3 takes both wings from a sculpt of
one wing through the `wing` stage instead; that wing is not the default
because it tears in the engine.

The default run executes every stage, then verifies. Each stage reads the
cache of the stage before it from build-hero-cache/ and writes its own, so
`--stage` re-runs only what changed. The record is tools/hero-build.md.

Blender axes are used inside this file: +Z up, the dragon's nose at +Y, the
wings along X. The glTF exporter turns that into +Y up and forward at -Z.
"""

import json
import math
import os
import struct
import sys
import time
import zlib

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(ROOT, "build-hero-cache")
ART = os.path.join(ROOT, "artifacts", "hero-build")
BODY_IN = os.path.join(ROOT, "assets", "embercrest-cand-hero-hunyuan.glb")
HEAD_IN = os.path.join(ROOT, "assets", "embercrest-cand-hero-head-hunyuan.glb")
# The wingless body sculpt. HERO_BODY names another one; see "Swapping the body" in tools/hero-build.md.
BODY2_IN = os.environ.get("HERO_BODY") or os.path.join(ROOT, "assets", "embercrest-cand-hero-body-hunyuan.glb")
WING2_IN = os.path.join(ROOT, "assets", "embercrest-cand-hero-wing-hunyuan.glb")
# Which wings the build uses. "v2" (the default) cuts them out of the first whole-body sculpt and
# unions them with the body. "v3" takes them from the wing sculpt through the `wing` stage; in the
# engine that wing looks torn mid-beat and fails five rig checks (tools/hero-build.md).
# "v5" takes them from the second wing sculpt, whose plates were drawn along the skeleton's own
# wing bones, through the `wing5` stage: one membrane sheet with an offset second face, and the
# sculpt's bones over it (tools/hero-build.md, "Version 5").
WING_SOURCE = os.environ.get("HERO_WING", "v2")
if WING_SOURCE not in ("v2", "v3", "v5"):
    raise SystemExit("HERO_WING must be v2, v3 or v5")
WING_V3 = WING_SOURCE == "v3"
WING_V5 = WING_SOURCE == "v5"
WING5_IN = os.path.join(ROOT, "assets", "embercrest-cand-hero-wing2-hunyuan.glb")
SKELETON = os.path.join(ROOT, "tools", "skeletons", "embercrest-hero.json")
OUT_GLB = os.path.join(ROOT, "assets", "embercrest-hero-static.glb")
OUT_THICK = os.path.join(ROOT, "assets", "embercrest-hero-membrane-thickness.png")
DESIGN = os.path.join(ROOT, "docs", "concept", "showcase", "hero-dragon-design.png")

WINGSPAN = 19.0
T0 = time.time()


def log(*a):
    print(f"[hero {time.time() - T0:6.1f}s]", *a, flush=True)


class HeroError(Exception):
    pass


def cache(name):
    os.makedirs(CACHE, exist_ok=True)
    return os.path.join(CACHE, name)


# -------------------------------------------------------------- image tools

def np_from_image(img):
    w, h = img.size
    a = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(a)
    return a.reshape(h, w, 4)


def srgb_to_linear(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def to_u8(a):
    return np.clip(np.rint(a * 255.0), 0, 255).astype(np.uint8)


def png_bytes(top_first):
    """Encode an (h, w, c) uint8 array (c = 1 or 3) as PNG."""
    if top_first.ndim == 2:
        top_first = top_first[..., None]
    a = top_first.astype(np.int16)
    h, w, c = a.shape
    left = np.zeros_like(a)
    left[:, c == 0:] = 0
    left[:, 1:] = a[:, :-1]
    up = np.zeros_like(a)
    up[1:] = a[:-1]
    cands = [(a - x).astype(np.uint8) for x in (0, left, up, (left + up) >> 1)]
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

    ctype = 0 if c == 1 else 2
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, ctype, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows.tobytes(), 6)) + chunk(b"IEND", b""))


def write_png(path, bottom_first_u8):
    with open(path, "wb") as f:
        f.write(png_bytes(bottom_first_u8[::-1]))


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
    if factor == 1:
        return a
    h, w, c = a.shape
    return a.reshape(h // factor, factor, w // factor, factor, c).mean(axis=(1, 3))


def dilate_fill(a, valid, iterations):
    """Grow the valid texels outward so a mip or a bilinear tap past an
    island's edge reads the island's colour."""
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


# --------------------------------------------------------------- scene tools

def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    return setup_cycles()


def setup_cycles():
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.use_denoising = False
    try:
        prefs = bpy.context.preferences.addons["cycles"].preferences
        prefs.compute_device_type = "METAL" if sys.platform == "darwin" else "OPTIX"
        prefs.get_devices()
        for dv in prefs.devices:
            dv.use = True
        sc.cycles.device = "GPU"
    except Exception as e:
        log("no GPU device:", e)
    return sc


def open_blend(name):
    bpy.ops.wm.open_mainfile(filepath=cache(name))
    return setup_cycles()


def save_blend(name):
    bpy.ops.wm.save_as_mainfile(filepath=cache(name), compress=False)
    log("saved", name)


def append_object(blend, obj_name):
    path = cache(blend)
    with bpy.data.libraries.load(path, link=False) as (src, dst):
        if obj_name not in src.objects:
            raise HeroError(f"{blend} has no object {obj_name}")
        dst.objects = [obj_name]
    obj = dst.objects[0]
    bpy.context.scene.collection.objects.link(obj)
    return obj


def activate(obj, others=()):
    if bpy.context.object and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    bpy.ops.object.select_all(action="DESELECT")
    for o in others:
        o.select_set(True)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def coords(obj):
    n = len(obj.data.vertices)
    co = np.empty(n * 3, dtype=np.float64)
    obj.data.vertices.foreach_get("co", co)
    return co.reshape(n, 3)


def set_coords(obj, co):
    obj.data.vertices.foreach_set("co", np.asarray(co, dtype=np.float64).ravel())
    obj.data.update()


def tri_count(obj):
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


def apply_modifier(obj, mod):
    activate(obj)
    bpy.ops.object.modifier_apply(modifier=mod.name)


def duplicate(obj, name):
    new = obj.copy()
    new.data = obj.data.copy()
    new.name = name
    new.data.name = name
    bpy.context.scene.collection.objects.link(new)
    return new


def import_scan(path, name):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    new = [o for o in bpy.data.objects if o not in before]
    meshes = [o for o in new if o.type == "MESH"]
    if not meshes:
        raise HeroError(f"{path}: no mesh")
    activate(meshes[0], meshes)
    if len(meshes) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    for o in new:
        if o != obj and o.name in bpy.data.objects:
            bpy.data.objects.remove(o)
    obj.name = name
    obj.data.name = name
    return obj


def bvh_of(obj):
    me = obj.data
    me.calc_loop_triangles()
    co = [v.co.copy() for v in me.vertices]
    tris = [tuple(t.vertices) for t in me.loop_triangles]
    return BVHTree.FromPolygons(co, tris)


# ------------------------------------------------------------------- rendering

SUN_ELEVATION = 30.0


def render_setup(samples=64, sky=0.55):
    """A sun at 30 degrees and a sky light, in the open scene."""
    sc = setup_cycles()
    sc.cycles.samples = samples
    sc.cycles.use_denoising = True
    sc.cycles.use_adaptive_sampling = True
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGB"
    sc.render.film_transparent = False
    for vt, look in (("AgX", "AgX - Medium High Contrast"), ("Filmic", "Medium High Contrast")):
        try:
            sc.view_settings.view_transform = vt
            sc.view_settings.look = look
            break
        except TypeError:
            continue
    world = bpy.data.worlds.new("sky")
    world.use_nodes = True
    nt = world.node_tree
    bg = next(n for n in nt.nodes if n.type == "BACKGROUND")
    out = next(n for n in nt.nodes if n.type == "OUTPUT_WORLD")
    # The camera sees a flat grey. The light is a blue sky.
    bg.inputs[0].default_value = (0.42, 0.58, 0.95, 1.0)
    bg.inputs[1].default_value = sky
    grey = nt.nodes.new("ShaderNodeBackground")
    grey.inputs[0].default_value = (0.36, 0.38, 0.40, 1.0)
    grey.inputs[1].default_value = 1.0
    lp = nt.nodes.new("ShaderNodeLightPath")
    mix = nt.nodes.new("ShaderNodeMixShader")
    nt.links.new(lp.outputs["Is Camera Ray"], mix.inputs[0])
    nt.links.new(bg.outputs[0], mix.inputs[1])
    nt.links.new(grey.outputs[0], mix.inputs[2])
    nt.links.new(mix.outputs[0], out.inputs["Surface"])
    sc.world = world
    sun_data = bpy.data.lights.new("sun", "SUN")
    sun_data.energy = 4.2
    sun_data.color = (1.0, 0.92, 0.80)
    sun_data.angle = math.radians(1.5)
    sun = bpy.data.objects.new("sun", sun_data)
    sc.collection.objects.link(sun)
    cam_data = bpy.data.cameras.new("cam")
    cam_data.clip_start = 0.01
    cam_data.clip_end = 500
    cam = bpy.data.objects.new("cam", cam_data)
    sc.collection.objects.link(cam)
    sc.camera = cam
    aim_sun(sun, 60.0)
    return sc, sun, cam


def aim_sun(sun, azimuth_deg, elevation=SUN_ELEVATION):
    """Azimuth is where the light comes FROM, in degrees from +X toward +Y."""
    a, e = math.radians(azimuth_deg), math.radians(elevation)
    to_sun = Vector((math.cos(a) * math.cos(e), math.sin(a) * math.cos(e), math.sin(e)))
    sun.rotation_euler = to_sun.to_track_quat("Z", "Y").to_euler()


def aim_camera(cam, eye, target, ortho=None, lens=50.0, up_hint=None):
    cam.location = eye
    d = Vector(target) - Vector(eye)
    q = d.to_track_quat("-Z", "Y")
    cam.rotation_euler = q.to_euler()
    if ortho:
        cam.data.type = "ORTHO"
        cam.data.ortho_scale = ortho
    else:
        cam.data.type = "PERSP"
        cam.data.lens = lens


def render_to(sc, path, w, h):
    sc.render.resolution_x = w
    sc.render.resolution_y = h
    sc.render.resolution_percentage = 100
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)


def ortho_shot(sc, cam, path, centre, direction, width, w=1400, h=1000, dist=40.0):
    """An orthographic view looking along `direction` at `centre`, `width`
    metres across the frame."""
    c = Vector(centre)
    d = Vector(direction).normalized()
    aim_camera(cam, c - d * dist, c, ortho=width)
    render_to(sc, path, w, h)


# ---------------------------------------------------------------- stage: import

def stage_import():
    """Read each sculpt once and keep it as a .blend."""
    for src, name in ((BODY_IN, "body_src"), (HEAD_IN, "head_src"), (BODY2_IN, "body2_src")) + (
            ((WING2_IN, "wing2_src"),) if WING_V3 else ()) + (((WING5_IN, "wing5_src"),) if WING_V5 else ()):
        reset_scene()
        obj = import_scan(src, name)
        co = coords(obj)
        log(name, "verts", len(co), "tris", tri_count(obj), "min", co.min(0).round(3), "max", co.max(0).round(3))
        for m in obj.data.materials:
            for n in m.node_tree.nodes:
                if n.type == "TEX_IMAGE" and n.image:
                    log("  image", n.image.name, tuple(n.image.size), n.image.colorspace_settings.name,
                        [l.to_socket.name for o in n.outputs for l in o.links])
        save_blend(name + "_raw.blend")


# ------------------------------------------------------------------ stage: wing
#
# Version 3 takes both wings from a sculpt of one right wing, generated alone.
# The sculpt lies flat: X runs out along the wing, Y forward, Z up. All work
# on it is done in "plan": a 1600 px square over the sculpt seen from above,
# px to the right (out along the wing) and py down (toward the trailing edge).
# The bone lines below were read off a picture of the sculpt's thickness in
# that frame, and `w2_snap` then moves them onto the ridge of each bone.
#
# The sculpt is fitted to version 2's wing skeleton by a warp: each sculpt
# bone line maps to a line of the skeleton, a point near a bone moves rigidly
# with it, and the membrane between bones blends their motions.

W2_N = 1600
W2_K = 1.2049 / W2_N                # sculpt units per plan px
W2_C = (0.0257, -0.0551)            # sculpt X, Y at the centre of the plan
W2_SCALE = 8.86                     # metres per sculpt unit, across a bone and through the sheet
W2_TRIS = 4400                      # one wing, reduced, before the cuts along the bones
W2_THIN = 0.35                      # the membrane loses this share of its thickness
W2_OUTER_BILLOW = 0.3               # share of its billow the panel between fingers 1 and 2 keeps
W2_SHEET_GAP = 0.0045               # sculpt units: the reduced sheet's two faces stay this far apart (40 mm)
W2_BONE_T = 0.0115                  # sculpt units: thicker than this is bone, the membrane is 0.0073
W2_POWER_RADIUS = 3.0               # px: softens the blend weight right on a bone line

# Version 2's wing skeleton, right wing, build frame (X out, Y forward, Z up),
# from tools/skeletons/embercrest-hero.json. `root` and the two flank points
# are inside the body. `tip` is version 2's wing tip, where the claw ends.
W2_TARGET = {
    "root": (0.30, 1.12, 2.75), "shoulder": (1.15, 0.88, 3.68), "elbow": (2.62, 0.03, 4.12),
    "wrist": (3.43, 0.58, 5.68), "hub": (3.66, 0.55, 5.72),
    "f1a": (6.31, 0.40, 5.64), "f1b": (7.95, -0.07, 5.40), "tip": (9.50, -1.02, 4.52),
    "f2a": (5.85, -0.37, 5.16), "f2b": (8.10, -1.40, 3.75),
    "f3a": (4.74, -0.63, 4.84), "f3b": (5.71, -1.73, 3.05),
    "spur_a": (2.76, -0.43, 4.02), "spur_b": (2.87, -1.26, 2.74),
    "flank_a": (0.50, 0.50, 3.22), "flank_b": (0.62, -0.30, 3.02),
    "arm_back": (1.60, -1.00, 3.30),          # level with the arm and behind it: keeps the arm's flat side up
    "trail_a": (7.55, -0.72, 4.80), "trail_b": (8.65, -1.22, 4.18),     # the trailing edge between fingers 1 and 2
}
# Bone lines: name, plan line, skeleton points it maps to, whether the line
# snaps to the sculpt's ridge, and what lies on the line's left (seen from
# above, walking along it) as skeleton points with a sign. A line that starts
# with "hub" or "elbow" starts where the forearm or the arm carries its own
# first point, pulled 40% toward the joint.
W2_CHAINS = [
    ("arm", [(45, 618), (120, 620), (200, 625), (300, 647), (370, 665)], ("root", "shoulder", "elbow"), False, ("arm_back",), -1),
    ("fore", [(370, 665), (480, 580), (600, 487), (672, 430)], ("elbow", "wrist", "hub"), True, ("spur_b", "f3b"), -1),
    ("spur", [(367, 700), (350, 780), (320, 855), (277, 940)], ("elbow", "spur_a", "spur_b"), True, ("f3b",), 1),
    ("f3a", [(682, 465), (680, 650), (672, 880)], ("hub", "f3a"), True, ("f2b",), 1),
    ("f3b", [(672, 880), (640, 1060), (605, 1190)], ("f3a", "f3b"), True, ("f2b",), 1),
    ("f2a", [(700, 458), (800, 520), (880, 610), (930, 690), (947, 750)], ("hub", "f2a"), True, ("tip",), 1),
    ("f2b", [(947, 750), (1062, 908), (1040, 950), (1010, 1020), (995, 1100), (975, 1180), (958, 1245)],
     ("f2a", "f2b"), True, ("tip",), 1),
    ("f1a", [(703, 440), (900, 425), (1150, 412)], ("hub", "f1a"), True, ("f2b",), -1),
    ("f1b", [(1150, 412), (1340, 520)], ("f1a", "f1b"), True, ("f2b",), -1),
    ("f1c", [(1340, 520), (1500, 680)], ("f1b", "tip"), True, ("f2b",), -1),
    ("flank", [(60, 692), (185, 735)], ("flank_a", "flank_b"), False, ("elbow",), 1),
    # The short finger has no bone in the skeleton. Its line holds the trailing edge between fingers 1 and 2.
    ("trail", [(1262, 912), (1330, 1040)], ("trail_a", "trail_b"), False, ("f2b",), -1),
]
W2_BRIDGE = ((947, 750), (1062, 908))      # no bone here in the sculpt: one is swept in
# The claw at the end of each finger: its plan line. It maps to a straight
# continuation of the finger, so a claw that turns aside in the sculpt is
# turned back into line.
W2_CLAWS = {"f1c": ("claw_f1", [(1500, 680), (1555, 665)]), "f2b": ("claw_f2", [(958, 1245), (925, 1305)]),
            "f3b": ("claw_f3", [(605, 1190), (570, 1255)]), "spur": ("claw_spur", [(277, 940), (255, 965)])}
# Side pieces: name, plan line from base to tip, the bone line whose last
# segment carries it, and a flag that is not used.
W2_FOLLOW = [
    ("thumb_a", [(660, 400), (650, 370), (640, 320)], "fore", False),
    ("thumb_b", [(690, 405), (700, 385), (730, 372)], "fore", False),
    ("alula", [(1150, 412), (1110, 320), (1085, 295)], "f1a", False),
]
# Horn by plan: (line, where along it the horn starts in px).
W2_HORN_START = {"thumb_a": 28, "thumb_b": 20, "alula": 92, "claw_f1": 0, "claw_f2": 0, "claw_f3": 0, "claw_spur": 0}
# The sculpt has one more claw, 0.1 m long, on the web in front of the elbow at plan (345, 455). It stays
# membrane: as horn it follows one bone while the web under it blends two, and the fold tears it off.
# Bones that stop in the middle of the membrane, and the cut that takes the
# short finger's point off the trailing edge: (line, radius px).
W2_STUBS = [
    ([(1068, 568), (947, 750), (872, 900)], 27),
    ([(1277, 650), (1200, 740), (1145, 800), (1075, 890), (1066, 902)], 30),
    ([(1262, 912), (1300, 980), (1330, 1040)], 26),
]
W2_STUB_KEEP = ((947, 750), 15, 12)     # the knuckle where finger 2 turns stays: centre, radius, feather
W2_CUSP_CUT = ((1250, 972), (1317, 935))


def w2_px(xy):
    xy = np.asarray(xy, dtype=np.float64)
    return np.stack([(xy[..., 0] - W2_C[0]) / W2_K + W2_N / 2, W2_N / 2 - (xy[..., 1] - W2_C[1]) / W2_K], axis=-1)


def w2_xy(px):
    px = np.asarray(px, dtype=np.float64)
    return np.stack([W2_C[0] + (px[..., 0] - W2_N / 2) * W2_K, W2_C[1] + (W2_N / 2 - px[..., 1]) * W2_K], axis=-1)


def w2_sample(raster, px):
    """Bilinear sample of a plan raster at px positions (cell centres at +0.5)."""
    x = np.clip(px[..., 0] - 0.5, 0, W2_N - 1.001)
    y = np.clip(px[..., 1] - 0.5, 0, W2_N - 1.001)
    x0, y0 = x.astype(int), y.astype(int)
    fx, fy = x - x0, y - y0
    return (raster[y0, x0] * (1 - fx) * (1 - fy) + raster[y0, x0 + 1] * fx * (1 - fy)
            + raster[y0 + 1, x0] * (1 - fx) * fy + raster[y0 + 1, x0 + 1] * fx * fy)


def w2_segment(p, a, b):
    """Along (0 to 1), signed distance to the left, and distance, of points p from segment a-b, in px."""
    a, b = np.asarray(a, float), np.asarray(b, float)
    d = b - a
    length = float(np.hypot(*d))
    u = d / length
    left = np.array([u[1], -u[0]])          # py runs down, so this is the left seen from above
    rel = p - a
    t = (rel @ u) / length
    s = rel @ left
    near = rel - np.clip(t, 0, 1)[..., None] * d
    return t, s, np.hypot(near[..., 0], near[..., 1])


def w2_line_distance(p, line):
    return np.min([w2_segment(p, line[i], line[i + 1])[2] for i in range(len(line) - 1)], axis=0)


def w2_resample(line, step):
    line = np.asarray(line, float)
    seg = np.hypot(*np.diff(line, axis=0).T)
    cum = np.r_[0, np.cumsum(seg)]
    n = max(1, int(round(cum[-1] / step)))
    at = np.linspace(0, cum[-1], n + 1)
    return np.stack([np.interp(at, cum, line[:, 0]), np.interp(at, cum, line[:, 1])], axis=1)


def w2_fill(raster, valid, grow, relax):
    """Fill the cells that are not valid from the valid ones round them, smoothly."""
    a, v = dilate_fill(raster[..., None], valid, grow)
    a = a[..., 0]
    a[~v] = np.nanmean(raster[valid])
    hole = ~valid
    for _ in range(relax):
        avg = (np.roll(a, 1, 0) + np.roll(a, -1, 0) + np.roll(a, 1, 1) + np.roll(a, -1, 1)) / 4
        a[hole] = avg[hole]
    return a


def w2_blur(a, passes):
    for _ in range(passes):
        a = (a * 2 + np.roll(a, 1, 0) + np.roll(a, -1, 0) + np.roll(a, 1, 1) + np.roll(a, -1, 1)) / 6
    return a


def w2_rasters(obj):
    """The sculpt's upper and lower surface over the plan, by rays from above and below."""
    tree = bvh_of(obj)
    zup = np.full((W2_N, W2_N), np.nan)
    zlo = np.full((W2_N, W2_N), np.nan)
    up, down = Vector((0, 0, 1)), Vector((0, 0, -1))
    centres = w2_xy(np.stack(np.meshgrid(np.arange(W2_N) + 0.5, np.arange(W2_N) + 0.5), axis=-1))
    for j in range(W2_N):
        for i in range(W2_N):
            x, y = centres[j, i]
            hit = tree.ray_cast(Vector((x, y, 1.0)), down)
            if hit[0] is not None:
                zup[j, i] = hit[0].z
                zlo[j, i] = tree.ray_cast(Vector((x, y, -1.0)), up)[0].z
    return zup, zlo


def w2_snap(line, thick, fixed_ends=True):
    """Move a bone line onto the ridge of the sculpt's thickness."""
    pts = w2_resample(line, 40.0)
    for _ in range(3):
        new = pts.copy()
        for i in range(len(pts)):
            if fixed_ends and i in (0, len(pts) - 1):
                continue
            d = pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]
            n = np.array([d[1], -d[0]]) / np.hypot(*d)
            off = np.arange(-16, 17)
            w = np.maximum(w2_sample(thick, pts[i] + off[:, None] * n) - W2_BONE_T, 0)
            if w.sum() > 1e-6:
                new[i] = pts[i] + n * float((w * off).sum() / w.sum())
        pts = new
    return pts


def w2_measure_lines(thick):
    """Snap every bone line, and give each claw line as it is."""
    lines = {}
    for name, line, keys, snap, _, _ in W2_CHAINS:
        if name == "f2b":
            # The first segment is the bridge: nothing to snap to there.
            lines[name] = np.concatenate([np.asarray(line[:1], float), w2_snap(line[1:], thick, fixed_ends=False)])
        else:
            lines[name] = w2_snap(line, thick) if snap else w2_resample(line, 40.0)
    # Lines that meet share the point where they meet.
    for a, b in (("f3a", "f3b"), ("f2a", "f2b"), ("f1a", "f1b"), ("f1b", "f1c")):
        lines[b][0] = lines[a][-1]
    lines["fore"][0] = lines["arm"][-1]
    for name, line, parent, _ in W2_FOLLOW:
        lines[name] = np.asarray(line, float)
    for parent, (name, line) in W2_CLAWS.items():
        lines[name] = np.asarray(line, float)
        lines[name][0] = lines[parent][-1]
    return lines


def w2_edit_rasters(zup, zlo, lines):
    """The sculpt's two surfaces with the stub bones gone, a bone swept across
    the gap in finger 2, and the membrane thinned. Returns the new surfaces
    and the plan masks the maps stage paints with."""
    have = ~np.isnan(zup)
    thick = np.where(have, zup - zlo, 0.0)
    bone = w2_blur((thick > W2_BONE_T).astype(np.float32), 6) > 0.02      # bones, grown by a few px
    sheet = have & ~bone
    mem_up = w2_fill(np.where(sheet, zup, 0.0), sheet, 90, 250)
    mem_lo = w2_fill(np.where(sheet, zlo, 0.0), sheet, 90, 250)
    grid = np.stack(np.meshgrid(np.arange(W2_N) + 0.5, np.arange(W2_N) + 0.5), axis=-1)
    # Stubs: the surface drops to the membrane's level.
    stub = np.zeros((W2_N, W2_N))
    for line, radius in W2_STUBS:
        d = w2_line_distance(grid, line)
        stub = np.maximum(stub, 1.0 - smoothstep((d - radius) / 8.0))
    centre, keep_r, feather = W2_STUB_KEEP
    stub *= smoothstep((np.hypot(grid[..., 0] - centre[0], grid[..., 1] - centre[1]) - keep_r) / feather)
    new_up = np.where(have, zup + stub * (mem_up - zup), np.nan)
    new_lo = np.where(have, zlo + stub * (mem_lo - zlo), np.nan)
    # The bridge: finger 2's section just past the gap, swept back across it.
    a, b = np.asarray(W2_BRIDGE[0], float), np.asarray(W2_BRIDGE[1], float)
    after = lines["f2b"]
    offs = np.arange(-30, 31)
    prof_up, prof_lo = np.zeros(len(offs)), np.zeros(len(offs))
    stations = w2_resample(after[1:], 8.0)[4:12]
    for i in range(len(stations) - 1):
        d = stations[i + 1] - stations[i]
        n = np.array([d[1], -d[0]]) / np.hypot(*d)
        at = stations[i] + offs[:, None] * n
        prof_up += w2_sample(np.nan_to_num(zup), at) - w2_sample(mem_up, at)
        prof_lo += w2_sample(np.nan_to_num(zlo), at) - w2_sample(mem_lo, at)
    prof_up = np.maximum(prof_up / (len(stations) - 1), 0)
    prof_lo = np.minimum(prof_lo / (len(stations) - 1), 0)
    prof_up, prof_lo = (prof_up + prof_up[::-1]) / 2, (prof_lo + prof_lo[::-1]) / 2
    t, s, d = w2_segment(grid, a, b)
    length = float(np.hypot(*(b - a)))
    inside = (t > -6 / length) & (t < 1 + 6 / length) & (np.abs(s) <= 30)
    bulge_up = np.interp(s, offs, prof_up) * inside
    bulge_lo = np.interp(s, offs, prof_lo) * inside
    new_up = np.where(have & inside, np.maximum(new_up, mem_up + bulge_up), new_up)
    new_lo = np.where(have & inside, np.minimum(new_lo, mem_lo + bulge_lo), new_lo)
    bridge = inside & (np.abs(s) <= 14) & (t > 0) & (t < 1)
    # The panel between fingers 1 and 2 is three times as wide in the sculpt as in the
    # skeleton. Its billow would become steep folds, so it is laid flatter first.
    poly = np.concatenate([lines["f2a"], lines["f2b"], lines["claw_f2"][-1:], [(1335, 1085)], lines["claw_f1"][::-1],
                           lines["f1c"][::-1], lines["f1b"][::-1], lines["f1a"][::-1]])
    inside_poly = np.zeros((W2_N, W2_N), bool)
    gx, gy = grid[..., 0], grid[..., 1]
    for i in range(len(poly)):
        (x0, y0), (x1, y1) = poly[i], poly[(i + 1) % len(poly)]
        if y0 != y1:
            cross = ((y0 > gy) != (y1 > gy)) & (gx < x0 + (gy - y0) * (x1 - x0) / (y1 - y0))
            inside_poly ^= cross
    flat = w2_blur(inside_poly.astype(np.float64), 12) * (1.0 - W2_OUTER_BILLOW)
    mid = (new_up + new_lo) / 2
    bones_now = w2_blur(((new_up - new_lo) > W2_BONE_T).astype(np.float32), 2) > 0.5
    level = w2_fill(np.where(have & bones_now, mid, 0.0), have & bones_now, 400, 300)
    drop = np.where(have, flat * (level - mid), 0.0) * (~bones_now)
    new_up, new_lo = new_up + drop, new_lo + drop
    # Thin the membrane about its middle; bones keep their thickness.
    mid = (new_up + new_lo) / 2
    new_thick = np.where(have, new_up - new_lo, 0.0)
    # Past the outline the thickness carries on, or a claw's steep side would count as thin.
    boneness = w2_blur(smoothstep((w2_fill(new_thick, have, 14, 0) - 0.0095) / 0.008), 3)
    f = 1.0 - W2_THIN * (1.0 - boneness)
    new_up = mid + (new_up - mid) * f
    new_lo = mid + (new_lo - mid) * f
    log("wing rasters: membrane", round(float(np.median(thick[sheet])) * W2_SCALE * 1000), "mm thick, thinned to",
        round(float(np.median((new_up - new_lo)[sheet])) * W2_SCALE * 1000), "mm; stub cells", int((stub > 0.5).sum()),
        "bridge cells", int(bridge.sum()), "bridge bone", round(float(prof_up.max() - prof_lo.min()) * W2_SCALE * 1000), "mm thick")
    return new_up, new_lo, stub, bridge


class WingWarp:
    """The map from the sculpt to the build frame, right wing."""

    def __init__(self, lines, mid, shorten=0.0):
        """`lines` are the bone lines in plan px; `mid` is the plan raster of
        the sheet's middle height, in sculpt units."""
        self.segs = []          # (a px, b px, z at a, z at b, target A, target B, L, N)
        self.owner = []         # the segment whose motion each segment follows
        tgt = {k: np.array(v, float) for k, v in W2_TARGET.items()}
        index = {}

        def add_chain(name, line, targets, left, sign):
            line = np.asarray(line, float)
            seg = np.hypot(*np.diff(line, axis=0).T)
            cum = np.r_[0, np.cumsum(seg)] / seg.sum()
            tl = np.array(targets)
            tseg = np.linalg.norm(np.diff(tl, axis=0), axis=1)
            tcum = np.r_[0, np.cumsum(tseg)] / tseg.sum()
            # Every corner of the target line becomes a corner of the sculpt line.
            for f in tcum[1:-1]:
                if np.abs(cum - f).min() > 0.02:
                    k = int(np.searchsorted(cum, f))
                    w = (f - cum[k - 1]) / (cum[k] - cum[k - 1])
                    line = np.insert(line, k, line[k - 1] + w * (line[k] - line[k - 1]), axis=0)
                    cum = np.insert(cum, k, f)
                else:
                    cum[np.abs(cum - f).argmin()] = f
            pts = np.stack([np.interp(cum, tcum, tl[:, c]) for c in range(3)], axis=1)
            ref = np.mean([tgt[k] for k in left], axis=0)
            first = len(self.segs)
            for i in range(len(line) - 1):
                direction = pts[i + 1] - pts[i]
                direction /= np.linalg.norm(direction)
                side = sign * (ref - (pts[i] + pts[i + 1]) / 2)
                normal = np.cross(direction, side)
                normal /= np.linalg.norm(normal)
                lateral = np.cross(normal, direction)
                z = w2_sample(mid, line[i:i + 2])
                self.owner.append(len(self.segs))
                self.segs.append((line[i], line[i + 1], float(z[0]), float(z[1]), pts[i], pts[i + 1], lateral, normal))
            index[name] = (first, len(self.segs) - 1)

        def carried(name, p):
            """Where the last segment of a chain puts a plan point at the sheet's middle."""
            z = float(w2_sample(mid, np.asarray(p, float)[None])[0])
            return self.one(index[name][1], np.asarray(p, float)[None], np.array([z]))[0]

        for name, _, keys, _, left, sign in W2_CHAINS:
            line = lines[name]
            targets = [tgt[k] for k in keys]
            if keys[0] == "hub" and name != "fore":
                targets[0] = tgt["hub"] + 0.6 * (carried("fore", line[0]) - tgt["hub"])
            if name == "spur":
                targets[0] = tgt["elbow"] + 0.6 * (carried("arm", line[0]) - tgt["elbow"])
            claw = W2_CLAWS.get(name)
            length = 0.0
            if claw:
                length = float(np.hypot(*(lines[claw[0]][-1] - lines[claw[0]][0]))) * W2_K * W2_SCALE
            if name == "f1c":
                # The claw ends at version 2's wing tip, so the finger ends a claw's length short of it.
                reach = targets[1] - targets[0]
                targets[1] = targets[0] + reach * (1.0 - (length + shorten) / np.linalg.norm(reach))
            add_chain(name, line, targets, left, sign)
            if claw:
                along = targets[-1] - targets[-2]
                along /= np.linalg.norm(along)
                add_chain(claw[0], lines[claw[0]], [targets[-1], targets[-1] + along * length], left, sign)
        self.bones = len(self.segs)
        for name, _, parent, _ in W2_FOLLOW:
            line = lines[name]
            for i in range(len(line) - 1):
                self.segs.append((line[i], line[i + 1]) + self.segs[index[parent][1]][2:])
                self.owner.append(index[parent][1])
        self.index = index

    def one(self, i, px, z):
        a, b, za, zb, ta, tb, lateral, normal = self.segs[self.owner[i]]
        t, s, _ = w2_segment(px, a, b)
        h = z - (za + np.clip(t, 0, 1) * (zb - za))
        return (ta + t[:, None] * (tb - ta) + (s * W2_K * W2_SCALE)[:, None] * lateral
                + (h * W2_SCALE)[:, None] * normal)

    def __call__(self, co):
        """Sculpt coordinates to the build frame."""
        out = np.zeros((len(co), 3))
        for lo in range(0, len(co), 200000):
            c = co[lo:lo + 200000]
            px = w2_px(c[:, :2])
            acc = np.zeros((len(c), 3))
            total = np.zeros(len(c))
            for i, seg in enumerate(self.segs):
                d = w2_segment(px, seg[0], seg[1])[2]
                w = 1.0 / (d * d + W2_POWER_RADIUS ** 2) ** 2
                acc += w[:, None] * self.one(i, px, c[:, 2])
                total += w
            out[lo:lo + 200000] = acc / total[:, None]
        return out


def w2_remap(co, rasters):
    """Move sculpt vertices from the old surfaces to the edited ones."""
    zup, zlo, new_up, new_lo = rasters
    have = ~np.isnan(zup)
    old_mid = w2_fill(np.where(have, (zup + zlo) / 2, 0.0), have, 12, 0)
    new_mid = w2_fill(np.where(have, (new_up + new_lo) / 2, 0.0), have, 12, 0)
    old_t = np.where(have, zup - zlo, 0.0)
    ratio = np.where(old_t > 2e-3, np.where(have, new_up - new_lo, 0.0) / np.maximum(old_t, 2e-3), 1.0)
    ratio = np.clip(ratio, 0.2, 3.0)
    px = w2_px(co[:, :2])
    m0, m1, r = w2_sample(old_mid, px), w2_sample(new_mid, px), w2_sample(ratio, px)
    out = co.copy()
    out[:, 2] = m1 + (co[:, 2] - m0) * r
    return out, (co[:, 2] > m0)


def w2_corridors(lines, thick):
    """Each bone as a strip in plan: (name, centre points, left half width, right half width, horn start px)."""
    groups = [("arm", ["arm", "fore"]), ("spur", ["spur", "claw_spur"]), ("f3", ["f3a", "f3b", "claw_f3"]),
              ("f2", ["f2a", "f2b", "claw_f2"]), ("f1", ["f1a", "f1b", "f1c", "claw_f1"]),
              ("thumb_a", ["thumb_a"]), ("thumb_b", ["thumb_b"]), ("alula", ["alula"])]
    out = []
    for name, parts in groups:
        pts, horn_at, run = [], None, 0.0
        for part in parts:
            p = w2_resample(lines[part], 45.0)
            if part in W2_HORN_START:
                horn_at = run + W2_HORN_START[part]
            run += float(np.hypot(*np.diff(p, axis=0).T).sum())
            pts.append(p if not pts else p[1:])
        pts = np.concatenate(pts)
        tang = np.gradient(pts, axis=0)
        tang /= np.hypot(tang[:, 0], tang[:, 1])[:, None]
        left = np.stack([tang[:, 1], -tang[:, 0]], axis=1)
        widths = []
        for sgn in (1, -1):
            w = np.zeros(len(pts))
            for i in range(len(pts)):
                steps = np.arange(1, 75)
                t = w2_sample(thick, pts[i] + sgn * steps[:, None] * left[i])
                out_at = np.flatnonzero(t < W2_BONE_T)
                w[i] = (steps[out_at[0]] if len(out_at) else 74) + 1.5
            w[1:-1] = np.maximum(w[1:-1], np.minimum(w[:-2], w[2:]))      # no notch at one sample
            widths.append(np.clip(w, 5, 75))
        out.append((name, pts, left, widths[0], widths[1], horn_at))
    return out


def w2_classify(bm, corridors):
    """Cut the reduced wing along the edge of every bone strip, in plan, and
    mark each face hide (0), membrane (1) or horn (2) by its material index."""
    def px_of(v):
        return w2_px(np.array([v.co.x, v.co.y]))

    def cut(p0, p1, reach):
        """Cut along the plan line p0-p1 the faces whose centre lies near it."""
        a, b = w2_xy(p0), w2_xy(p1)
        d = b - a
        n = Vector((d[1], -d[0], 0)).normalized()
        mid = (np.asarray(p0) + np.asarray(p1)) / 2
        r = (float(np.hypot(*(np.asarray(p1) - np.asarray(p0)))) / 2 + reach) * W2_K
        mx, my = w2_xy(mid)
        faces = [f for f in bm.faces if (f.calc_center_median().x - mx) ** 2 + (f.calc_center_median().y - my) ** 2 < r * r]
        if faces:
            bmesh.ops.bisect_plane(bm, geom=face_geom(faces), dist=1e-7, plane_co=(a[0], a[1], 0), plane_no=n)

    for name, pts, left, wl, wr, horn_at in corridors:
        run = np.r_[0, np.cumsum(np.hypot(*np.diff(pts, axis=0).T))]
        for i in range(len(pts) - 1):
            cut(pts[i] + wl[i] * left[i], pts[i + 1] + wl[i + 1] * left[i + 1], 6)
            cut(pts[i] - wr[i] * left[i], pts[i + 1] - wr[i + 1] * left[i + 1], 6)
        if horn_at is not None:
            k = int(np.searchsorted(run, horn_at))
            k = min(max(k, 1), len(pts) - 1)
            w = (horn_at - run[k - 1]) / (run[k] - run[k - 1])
            base = pts[k - 1] + w * (pts[k] - pts[k - 1])
            cut(base + 40 * left[k], base - 40 * left[k], 4)
    bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 3])
    centres = w2_px(np.array([[f.calc_center_median().x, f.calc_center_median().y] for f in bm.faces]))
    kind = np.ones(len(centres), int)
    for name, pts, left, wl, wr, horn_at in corridors:
        run = np.r_[0, np.cumsum(np.hypot(*np.diff(pts, axis=0).T))]
        for i in range(len(pts) - 1):
            t, s, _ = w2_segment(centres, pts[i], pts[i + 1])
            tc = np.clip(t, 0, 1)
            # The strip's piece is the quad between the two cut lines. Its ends are the shared
            # edges of its neighbours, so a bend leaves no wedge between two pieces.
            quad = [pts[i] + wl[i] * left[i], pts[i + 1] + wl[i + 1] * left[i + 1],
                    pts[i + 1] - wr[i + 1] * left[i + 1], pts[i] - wr[i] * left[i]]
            turn = [(quad[(k + 1) % 4][0] - quad[k][0]) * (centres[:, 1] - quad[k][1])
                    - (quad[(k + 1) % 4][1] - quad[k][1]) * (centres[:, 0] - quad[k][0]) for k in range(4)]
            inside = np.all(np.array(turn) >= -1e-9, axis=0) | np.all(np.array(turn) <= 1e-9, axis=0)
            along = run[i] + tc * (run[i + 1] - run[i])
            horn = inside & (horn_at is not None) & (along >= (horn_at if horn_at is not None else 0))
            kind[inside & ~horn & (kind != 2)] = 0
            kind[horn] = 2
        # A claw's tip hangs past its line's end.
        if horn_at is not None:
            past = np.hypot(centres[:, 0] - pts[-1][0], centres[:, 1] - pts[-1][1]) < max(wl[-1], wr[-1]) + 4
            kind[past] = 2
    for f, k in zip(bm.faces, kind):
        f.material_index = int(k)
    return [int((kind == k).sum()) for k in range(3)]


def stage_wing():
    """The wing sculpt, edited, fitted to version 2's skeleton and reduced.
    Writes wing2.blend with `wing_src` (both wings, full detail, for the bake)
    and `wing_low` (both wings, reduced, faces marked hide, membrane or horn)."""
    open_blend("wing2_src_raw.blend")
    src = bpy.data.objects["wing2_src"]
    if src.data.has_custom_normals:
        activate(src)
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
    co = coords(src)
    zup, zlo = w2_rasters(src)
    have = ~np.isnan(zup)
    thick = np.where(have, zup - zlo, 0.0)
    lines = w2_measure_lines(thick)
    new_up, new_lo, stub, bridge = w2_edit_rasters(zup, zlo, lines)
    new_thick = np.where(have, new_up - new_lo, 0.0)
    np.savez(cache("wing2_plan.npz"), stub=stub.astype(np.float16), bridge=bridge,
             thick=new_thick.astype(np.float32), have=have, **{"line_" + k: v for k, v in lines.items()})
    edited, upper = w2_remap(co, (zup, zlo, new_up, new_lo))
    mid = w2_fill(np.where(have, (new_up + new_lo) / 2, 0.0), have, 12, 0)
    warp = WingWarp(lines, mid)
    # The claw at the tip turns outward, so it reaches past the finger's line. Shorten the
    # finger until the wing ends at version 2's half span.
    claws = edited[w2_line_distance(w2_px(edited[:, :2]), lines["claw_f1"]) < 30]
    shorten = 0.0
    for _ in range(4):
        over = float(warp(claws)[:, 0].max()) - WINGSPAN / 2
        shorten += over / 0.8
        warp = WingWarp(lines, mid, shorten)
    log("wing: finger 1 shortened by", round(shorten, 3), "m for the claw")
    set_coords(src, edited)

    # The reduced wing, made flat and warped afterwards.
    low = duplicate(src, "wing_low")
    strip_layers(low)
    weld(low)
    topo_report(low, "wing welded")
    decimate_to(low, W2_TRIS)
    topo_report(low, "wing decimated")
    bm = bmesh.new()
    bm.from_mesh(low.data)
    # The short finger's point comes off the trailing edge.
    p0, p1 = w2_xy(W2_CUSP_CUT[0]), w2_xy(W2_CUSP_CUT[1])
    d = p1 - p0
    n = Vector((-d[1], d[0], 0)).normalized()
    if n.y > 0:
        n = -n                               # toward the trailing edge
    mid_px = (np.array(W2_CUSP_CUT[0]) + np.array(W2_CUSP_CUT[1])) / 2
    box = [f for f in bm.faces if np.hypot(*(w2_px(np.array(f.calc_center_median()[:2])) - mid_px - (25, 70))) < 160]
    bmesh.ops.bisect_plane(bm, geom=face_geom(box), dist=1e-7, plane_co=(p0[0], p0[1], 0), plane_no=n, clear_outer=True)
    edges = [e for e in bm.edges if e.is_boundary]
    log("wing: boundary edges after the cut", len(edges))
    bmesh.ops.holes_fill(bm, edges=edges, sides=0)
    bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 3])
    counts = w2_classify(bm, w2_corridors(lines, new_thick))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    # The reduced sheet's two faces have different triangles, so one can poke through the
    # other, and the pale underside then shows on top. Push each face's vertices clear of
    # the other face.
    moved = 0
    for _ in range(3):
        bm.normal_update()
        for want_up in (True, False):
            other = [f for f in bm.faces if (f.normal.z > 0) != want_up]
            tree = BVHTree.FromPolygons([v.co.copy() for v in bm.verts], [[v.index for v in f.verts] for f in other])
            way = Vector((0, 0, -1.0 if want_up else 1.0))
            for v in bm.verts:
                faces = v.link_faces
                if not faces or any(f.material_index != 1 or (f.normal.z > 0) != want_up for f in faces):
                    continue
                hit = tree.ray_cast(v.co - way * 0.02, way, 0.02 + W2_SHEET_GAP)
                if hit[0] is not None:
                    v.co -= way * (0.02 + W2_SHEET_GAP - hit[3])
                    moved += 1
        bm.verts.index_update()
    log("wing: membrane vertices pushed clear of the other face", moved)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    # Which face of the wing each triangle is on: 1 above, 0 below. The maps stage grades by it.
    upper_layer = bm.faces.layers.float.new("wing_upper")
    for f in bm.faces:
        f[upper_layer] = 1.0 if f.normal.z > 0 else 0.0
    bm.to_mesh(low.data)
    bm.free()
    log("wing faces: hide", counts[0], "membrane", counts[1], "horn", counts[2])
    for name in ("from_wing", "from_wing_membrane", "from_wing_horn"):
        low.data.materials.append(bpy.data.materials.new(name))
    topo_report(low, "wing cut and marked")

    # Both into the build frame. The plan position rides along on the full sculpt.
    px = w2_px(edited[:, :2]) / W2_N
    plan = src.data.attributes.new("plan", "FLOAT_VECTOR", "POINT")
    plan.data.foreach_set("vector", np.stack([px[:, 0], px[:, 1], upper.astype(float)], axis=1).ravel())
    hi = warp(edited)
    lo = warp(coords(low))
    span = max(hi[:, 0].max(), lo[:, 0].max())
    for c in (hi, lo):
        c[:, 0] *= 1.0 + (WINGSPAN / 2 / span - 1.0) * smoothstep((c[:, 0] - 4.0) / 4.0)
    log("wing tip at X", round(float(span), 3), "set to", WINGSPAN / 2)
    set_coords(src, hi)
    set_coords(low, lo)
    for line in ("arm", "fore", "f1a", "f1b", "f1c", "f2a", "f2b", "f3a", "f3b", "spur"):
        a, b = warp.index[line]
        log("  bone line", line, "runs", np.round(warp.segs[a][4], 2).tolist(), "to", np.round(warp.segs[b][5], 2).tolist())
    for obj in (src, low):
        other = duplicate(obj, "wing_low_left" if obj is low else "wing_src_left")
        c = coords(other)
        c[:, 0] *= -1
        set_coords(other, c)
        activate(other)
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.mesh.flip_normals()
        bpy.ops.object.mode_set(mode="OBJECT")
        if obj is src:
            activate(obj, [other])
            bpy.ops.object.join()
        for o in (obj, other) if obj is low else (obj,):
            for p in o.data.polygons:
                p.use_smooth = True
    src.name = src.data.name = "wing_src"
    topo_report(low, "right wing")
    save_blend("wing2.blend")


# ----------------------------------------------------------------- stage: wing5
#
# Version 5 takes both wings from the second wing sculpt. Its plates were drawn
# along a flat projection of the skeleton's own wing bones, so its arm, three
# fingers and rib are the skeleton's, with no stub bones.
#
# The sculpt lies nearly flat. The stage turns it into its own plane (the
# vertices' principal axes) and works in "plan": a picture of it seen from
# above, 800 px across, px to the right (out along the wing) and py down
# (toward the trailing edge). Every plan number below is on that 800 px grid.
#
# The fit is piecewise. Each membrane panel is a polygon of the two bone lines
# that bound it and its trailing edge, and mean value coordinates carry the
# polygon onto the same bones in 3D, so a panel follows its own two bones and
# nothing else. Near a bone a point moves rigidly with the bone instead.
#
# The membrane is rebuilt as one sheet: a constrained Delaunay triangulation
# in plan, mapped, then copied along its normals to make the second face. The
# two faces have the same triangles and a rim joins them, so skinning cannot
# push one through the other. The sculpt's bones stay as real geometry over
# the sheet.

W5_N = 1200                         # raster cells across the plan
W5_SHEET = 0.020                    # m between the sheet's two faces
W5_EDGE = 16.0                      # px between sheet vertices: about 0.17 m
W5_BONE_T = 0.0080                  # sculpt units: thicker than this is bone; the membrane is 0.006
W5_BILLOW = 0.6                     # share of the sculpt's billow the sheet keeps
W5_BONE_TRIS = 7000                 # one wing's bones and claws, reduced: hide near version 4's 104k
W5_REACH = 2.2                      # the rigid bone map fades out between r0 and this times r0
# Bone lines: chain -> (r0 in px, [(px, py, skeleton point or None), ...]). A
# point with a name lands on that skeleton point; the others are spread along
# the chain by length. The points were read off the sculpt's thickness and
# are moved onto the ridge of the bone by `w5_snap`.
W5_HUB = (292.0, 56.0)
W5_CHAINS = {
    "arm": (24, [(2, 192, "start"), (60, 196, None), (120, 206, "shoulder"), (176, 234, "elbow")]),
    "fore": (11, [(176, 234, "elbow"), (230, 142, None), (284, 52, "wrist"), (292, 56, "hub")]),
    "f1": (8, [(292, 56, "hub"), (400, 60, None), (520, 72, "f1a"), (650, 125, "f1b"), (712, 182, None), (772, 245, "f1c")]),
    "f2": (8, [(292, 56, "hub"), (380, 122, None), (470, 192, "f2a"), (550, 282, None), (630, 378, "f2b")]),
    "f3": (8, [(292, 56, "hub"), (335, 140, None), (373, 225, "f3a"), (400, 320, None), (415, 460, "f3b")]),
    "spur": (8, [(176, 234, "elbow"), (170, 282, "spa"), (160, 360, None), (150, 445, "spb")]),
}
# The claw at each chain's end: plan tip. It maps straight on along the chain's last segment.
W5_CLAWS = {"f1": (790, 277), "f2": (642, 415), "f3": (408, 493), "spur": (128, 482)}
# The two thumb claws above the wrist ride rigidly on the forearm's last segment.
W5_THUMB = ((283, 0), (350, 46))    # plan box
W5_SNAP = {"arm": False}
# One stub the brief did not expect: a short rib from the wrist knob, 1 m long, between the forearm
# and finger 3. It is flattened into the membrane: (start, end, half width px), and finger 3's line
# keeps this far clear of it.
W5_STUB = ((296.0, 72.0), (318.0, 172.0), 7.0, 8.0)            # the upper arm is a wide flat band: its line is not snapped
# Points of the skeleton that are not bone ends, build frame. `start` is where
# the sculpt's arm stump ends, inside the body on the line from the elbow
# through the shoulder. The membrane's inner edge is laid on the flank
# between flank_a and flank_b, inside body B (`tools/hero-build.md`).
W5_START_DEPTH = 0.90                # the arm's stump ends this far inside the body, past the shoulder
W5_FLANK = {"flank_a": (0.50, 0.50, 3.22), "flank_b": (0.62, -0.30, 3.02)}
# The membrane's colour, linear, measured on version 4's membrane (version 2's
# wing): geometric mean of the texels on faces that face up, and down. Version 4's
# wing bones and wing claws likewise.
W5_COLOUR = {"upper": (0.529, 0.112, 0.029), "lower": (0.492, 0.108, 0.030),
             "bone": (0.138, 0.027, 0.010), "claw": (0.107, 0.031, 0.015)}
# The root panel (body to rib) is not fitted from the sculpt: its sculpt panel turns back along the
# flank and the fit folds. It is a ruled patch P(s, t) = A(t) + s (B(t) - A(t)) between the wing's
# edge B (the upper arm from the shoulder to the elbow, then the rib to its tip) and an attachment
# line A on body B's flank, which cannot fold. W5_ROOT_PATH is A as (Y, Z) stations; its X is the
# flank's, read by rays from the side, minus W5_ROOT_SINK. Above Z 3.2 the flank is smooth; below it
# the haunch bulges to X 1.4 between Y -0.2 and -1.0.
W5_ROOT_PATH = ((1.00, 3.40), (0.40, 3.35), (-0.40, 3.45), (-0.80, 3.40), (-1.20, 3.25))
W5_ROOT_SINK = 0.025                # m: the attachment line lies this far inside the flank
W5_ROOT_SAG = 0.05                  # m: the patch sags this far at its middle, as a sine across it
W5_ROOT_SCALLOP = 0.35              # m: depth of the trailing edge's scallop between the flank and the rib's tip
W5_ROOT_AP = 0.85                   # share of the sculpt's inner edge, from the stump, that is A in plan
W5_CONTRAST = 1.15                  # the washed sculpt's veins are faint: a little more contrast in log colour


def w5_targets():
    """The right wing's skeleton points in the build frame (X out, Y forward, Z up)."""
    with open(SKELETON) as f:
        spec = json.load(f)
    b = {x[0]: x for x in spec["bones"]}

    def build(p):
        return np.array([p[0], -p[2], p[1]], float)

    t = {"shoulder": build(b["wing_root_r"][1]), "elbow": build(b["wing_root_r"][2]),
         "wrist": build(b["wing_arm_r"][2]), "hub": build(b["wing_wrist_r"][2]),
         "f1a": build(b["wing_finger_1a_r"][2]), "f1b": build(b["wing_finger_1b_r"][2]),
         "f1c": build(b["wing_finger_1c_r"][2]), "f2a": build(b["wing_finger_2a_r"][2]),
         "f2b": build(b["wing_finger_2b_r"][2]), "f3a": build(b["wing_finger_3a_r"][2]),
         "f3b": build(b["wing_finger_3b_r"][2]), "spa": build(b["wing_spur_a_r"][2]),
         "spb": build(b["wing_spur_b_r"][2])}
    d = t["shoulder"] - t["elbow"]
    t["start"] = t["shoulder"] + d / np.linalg.norm(d) * W5_START_DEPTH
    for k, v in W5_FLANK.items():
        t[k] = np.array(v, float)
    return t


class W5Plan:
    """The sculpt's own plane and the 800 px plan over it."""

    def __init__(self, co):
        self.c0 = co.mean(0)
        r = np.linalg.svd((co - self.c0)[::20], full_matrices=False)[2].copy()
        if r[2, 2] < 0:
            r[2] *= -1
        if r[0, 0] < 0:
            r[0] *= -1
        r[1] = np.cross(r[2], r[0])
        self.R = r
        rc = self.flat(co)
        self.lo = rc.min(0)[:2] - 0.01
        self.hi = rc.max(0)[:2] + 0.01
        self.k = float(max(self.hi - self.lo)) / 800.0       # sculpt units per px

    def flat(self, co):
        return (co - self.c0) @ self.R.T

    def px(self, xy):
        xy = np.asarray(xy, float)
        return np.stack([(xy[..., 0] - self.lo[0]) / self.k, (self.hi[1] - xy[..., 1]) / self.k], axis=-1)

    def xy(self, px):
        px = np.asarray(px, float)
        return np.stack([self.lo[0] + px[..., 0] * self.k, self.hi[1] - px[..., 1] * self.k], axis=-1)


def w5_sample(raster, px):
    """Bilinear sample of a W5_N raster at 800 px plan positions."""
    return w2_sample_n(raster, np.asarray(px, float) * (W5_N / 800.0))


def w2_sample_n(raster, cells):
    n = raster.shape[0]
    x = np.clip(cells[..., 0] - 0.5, 0, n - 1.001)
    y = np.clip(cells[..., 1] - 0.5, 0, n - 1.001)
    x0, y0 = x.astype(int), y.astype(int)
    fx, fy = x - x0, y - y0
    return (raster[y0, x0] * (1 - fx) * (1 - fy) + raster[y0, x0 + 1] * fx * (1 - fy)
            + raster[y0 + 1, x0] * (1 - fx) * fy + raster[y0 + 1, x0 + 1] * fx * fy)


def w5_rasters(obj, plan):
    """The flat sculpt's upper and lower surface over the plan, by rays from above and below."""
    tree = bvh_of(obj)
    zup = np.full((W5_N, W5_N), np.nan)
    zlo = np.full((W5_N, W5_N), np.nan)
    up, down = Vector((0, 0, 1)), Vector((0, 0, -1))
    g = (np.arange(W5_N) + 0.5) * (800.0 / W5_N)
    for j in range(W5_N):
        y = plan.hi[1] - g[j] * plan.k
        for i in range(W5_N):
            x = plan.lo[0] + g[i] * plan.k
            hit = tree.ray_cast(Vector((x, y, 1.0)), down)
            if hit[0] is not None:
                zup[j, i] = hit[0].z
                zlo[j, i] = tree.ray_cast(Vector((x, y, -1.0)), up)[0].z
    return zup, zlo


def w5_snap(p, thick, direction):
    """Move one plan point across `direction` onto the ridge of the bone's thickness."""
    d = np.asarray(direction, float)
    n = np.array([d[1], -d[0]]) / np.hypot(*d)
    off = np.arange(-14, 15)
    w = np.maximum(w5_sample(thick, np.asarray(p, float) + off[:, None] * n) - W5_BONE_T, 0)
    return np.asarray(p, float) + (n * float((w * off).sum() / w.sum()) if w.sum() > 1e-6 else 0)


def w5_lines(thick, tgt):
    """Each chain as plan points and their 3D targets, every 10 px, after the snap."""
    out = {}
    for name, (r0, pts) in W5_CHAINS.items():
        p = np.array([q[:2] for q in pts], float)
        if W5_SNAP.get(name, True):
            for i in range(1, len(p)):
                if pts[i][2] in ("hub", "elbow"):
                    continue
                d = p[min(i + 1, len(p) - 1)] - p[i - 1]
                p[i] = w5_snap(p[i], thick, d)
        # Knots carry their skeleton point; the rest are spread by length between knots.
        seg = np.hypot(*np.diff(p, axis=0).T)
        cum = np.r_[0, np.cumsum(seg)]
        knots = [i for i, q in enumerate(pts) if q[2]]
        n = max(2, int(round(cum[-1] / 10.0)) + 1)
        at = np.unique(np.concatenate([np.linspace(0, cum[-1], n), cum[knots]]))
        line = np.stack([np.interp(at, cum, p[:, 0]), np.interp(at, cum, p[:, 1])], axis=1)
        t3 = np.stack([np.interp(at, cum[knots], [tgt[pts[i][2]][c] for i in knots]) for c in range(3)], axis=1)
        out[name] = (line, t3, r0)
    # Lines that meet share their point exactly.
    hub = np.array(W5_HUB)
    for name in ("f1", "f2", "f3"):
        out[name][0][0] = hub
    out["fore"][0][-1] = hub
    out["spur"][0][0] = out["arm"][0][-1]
    out["fore"][0][0] = out["arm"][0][-1]
    return out


def w5_curve(points, frac):
    """Points along a polyline at fractions of its length."""
    points = np.asarray(points, float)
    cum = np.r_[0, np.cumsum(np.linalg.norm(np.diff(points, axis=0), axis=1))]
    f = np.clip(np.asarray(frac, float), 0, 1) * cum[-1]
    return np.stack([np.interp(f, cum, points[:, c]) for c in range(points.shape[1])], axis=-1)


class W5Root:
    """The root panel as a ruled patch, in 3D and in the sculpt's plan."""

    def __init__(self, wmap, lines, body, rib_plan, rib3):
        arm_line, arm_t3, _ = lines["arm"]
        k = int(np.argmin(np.linalg.norm(arm_t3 - wmap.tgt["shoulder"], axis=1)))
        # B: the arm from the shoulder to the elbow, resampled about every W5_EDGE, then the outer
        # sheet's own vertices along the rib, so the two sheets share them.
        alen = float(np.linalg.norm(np.diff(arm_t3[k:], axis=0), axis=1).sum())
        f = np.linspace(0, 1, max(2, int(round(alen / 0.17))) + 1)[:-1]
        self.n_arm = len(f)
        self.Bp = np.vstack([w5_curve(arm_line[k:], f), rib_plan])
        self.B3 = np.vstack([w5_curve(arm_t3[k:], f), rib3])
        cum = np.r_[0, np.cumsum(np.linalg.norm(np.diff(self.B3, axis=0), axis=1))]
        self.VB = float(cum[-1])
        self.tB = cum / cum[-1]
        # A: stations on the flank of body B.
        tree = bvh_of(body)
        path = w5_curve(np.array(W5_ROOT_PATH), np.linspace(0, 1, 41))
        xs = []
        for y, z in path:
            hit = tree.ray_cast(Vector((3.0, y, z)), Vector((-1.0, 0, 0)))
            xs.append(hit[0].x if hit[0] is not None else 0.6)
        xs = np.array(xs)
        for _ in range(6):
            xs[1:-1] = (xs[:-2] + 2 * xs[1:-1] + xs[2:]) / 4
        self.A3 = np.c_[xs - W5_ROOT_SINK, path]
        # A in plan: the sculpt's inner edge from the arm's stump down, its first W5_ROOT_AP.
        edge = wmap.root_edge[0][::-1]
        self.Ap = w5_curve(edge, np.linspace(0, W5_ROOT_AP, 41))
        log("wing5 root: A from", np.round(self.A3[0], 2).tolist(), "to", np.round(self.A3[-1], 2).tolist(),
            "B", round(self.VB, 2), "m long,", len(self.B3), "samples")
        # The inverse of the plan patch, by a grid and a tree.
        ss, tt = np.meshgrid(np.linspace(-0.05, 1.05, 111), np.linspace(-0.1, 1.15, 251))
        self.grid_st = np.c_[ss.ravel(), tt.ravel()]
        gp = self.plan_point(self.grid_st[:, 0], self.grid_st[:, 1])
        self.kd = KDTree(len(gp))
        for i, q in enumerate(gp):
            self.kd.insert((q[0], q[1], 0.0), i)
        self.kd.balance()

    def _at(self, curve, tcurve, t):
        t = np.clip(t, 0, 1)
        return np.stack([np.interp(t, tcurve, curve[:, c]) for c in range(curve.shape[1])], axis=-1)

    def plan_point(self, s, t):
        a = w5_curve(self.Ap, t)
        b = self._at(self.Bp, self.tB, t)
        return a + np.asarray(s)[:, None] * (b - a)

    def point(self, s, t):
        a = w5_curve(self.A3, t)
        b = self._at(self.B3, self.tB, t)
        s = np.asarray(s, float)
        out = a + s[:, None] * (b - a)
        out[:, 2] -= W5_ROOT_SAG * np.sin(np.pi * np.clip(s, 0, 1))
        return out

    def normal(self, s, t, e=1e-3):
        dt = self.point(s, t + e) - self.point(s, t - e)
        ds = self.point(s + e, t) - self.point(s - e, t)
        n = np.cross(dt, ds)
        n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
        n[n[:, 2] < 0] *= -1
        return n

    def st_of_plan(self, p):
        st = np.array([self.grid_st[self.kd.find((q[0], q[1], 0.0))[1]] for q in p])
        return np.clip(st[:, 0], 0, 1), np.clip(st[:, 1], 0, 1)


def w5_root_sheet(root):
    """The root patch as a triangulation of (u, v): v along B in metres, u across, u = s L(v)."""
    from mathutils.geometry import delaunay_2d_cdt
    h = 0.17
    vB = root.tB * root.VB
    L = np.linalg.norm(root.B3 - w5_curve(root.A3, root.tB), axis=1)
    Lv = lambda v: np.interp(v, vB, L)
    ring = [np.c_[L, vB]]                                  # B, front to tip
    n = max(2, int(np.ceil(L[-1] / h)))
    u = np.linspace(L[-1], 0, n + 1)[1:-1]
    ring.append(np.c_[u, root.VB - W5_ROOT_SCALLOP * np.sin(np.pi * u / L[-1])])
    n = max(2, int(np.ceil(root.VB / h)))
    ring.append(np.c_[np.zeros(n + 1), np.linspace(root.VB, 0, n + 1)])     # A, tip to front
    n = max(2, int(np.ceil(L[0] / h)))
    ring.append(np.c_[np.linspace(0, L[0], n + 1)[1:-1], np.zeros(n - 1)])
    ring = np.concatenate(ring)
    nb = len(ring)
    xs = np.arange(0, L.max(), h)
    ys = np.arange(0, root.VB, h * math.sqrt(3) / 2)
    grid = np.array([(x + (h / 2 if j % 2 else 0), y) for j, y in enumerate(ys) for x in xs])
    grid = grid[w5_inside(grid, ring)]
    near = np.min([w2_segment(grid, ring[i], ring[(i + 1) % nb])[2] for i in range(nb)], axis=0)
    grid = grid[near > 0.55 * h]
    verts = [Vector(p) for p in ring] + [Vector(p) for p in grid]
    res = delaunay_2d_cdt(verts, [(i, (i + 1) % nb) for i in range(nb)], [list(range(nb))], 1, 1e-6)
    uv = np.array([tuple(v) for v in res[0]])
    tris = np.array([f for f in res[2] if len(f) == 3])
    tris = tris[w5_inside(uv[tris].mean(1), ring)]
    t = uv[:, 1] / root.VB
    sv = np.clip(uv[:, 0] / np.maximum(Lv(uv[:, 1]), 1e-6), 0, 1)
    co = root.point(sv, t)
    # Vertices on B: which sample each is.
    on_b = np.full(len(uv), -1)
    for i, q in enumerate(np.c_[L, vB]):
        d = np.hypot(uv[:, 0] - q[0], uv[:, 1] - q[1])
        j = int(d.argmin())
        if d[j] < 1e-5:
            on_b[j] = i
            co[j] = root.B3[i]
    return co, tris, sv, on_b


def w5_stub(p, lines):
    """Plan points on the stub rib, and not on finger 3."""
    a, b, half, clear = W5_STUB
    on = w2_segment(p, np.array(a), np.array(b))[2] < half
    f3 = lines["f3"][0]
    away = np.min([w2_segment(p, f3[i], f3[i + 1])[2] for i in range(len(f3) - 1)], axis=0) > clear
    return on & away & (p[:, 1] > a[1] + 4)


def w5_mvc(points, poly):
    """Mean value coordinates of plan points in a closed polygon (V, 2). Returns (M, V)."""
    s = poly[None, :, :] - points[:, None, :]
    r = np.maximum(np.hypot(s[..., 0], s[..., 1]), 1e-6)
    s1 = np.roll(s, -1, axis=1)
    r1 = np.roll(r, -1, axis=1)
    cross = s[..., 0] * s1[..., 1] - s[..., 1] * s1[..., 0]
    dot = (s * s1).sum(-1)
    tan_half = cross / np.maximum(r * r1 + dot, 1e-9)
    w = (np.roll(tan_half, 1, axis=1) + tan_half) / r
    return w / w.sum(1, keepdims=True)


def w5_inside(points, poly):
    inside = np.zeros(len(points), bool)
    gx, gy = points[:, 0], points[:, 1]
    for i in range(len(poly)):
        (x0, y0), (x1, y1) = poly[i], poly[(i + 1) % len(poly)]
        if y0 != y1:
            inside ^= ((y0 > gy) != (y1 > gy)) & (gx < x0 + (gy - y0) * (x1 - x0) / (y1 - y0))
    return inside


class W5Map:
    """Plan position and sculpt height to the build frame, right wing."""

    def __init__(self, plan, lines, mid, have, tgt, scale):
        self.plan, self.lines, self.mid, self.scale = plan, lines, mid, scale
        self.up = np.array([0.0, 0.0, 1.0])
        L = {k: v for k, v in lines.items()}
        rev = lambda a: a[::-1]
        # The sculpt's trailing edge between two finger ends, and the inner edge of the root panel.
        self.edges = {}
        for a, b in (("f1", "f2"), ("f2", "f3"), ("f3", "spur")):
            pa, pb = L[a][0][-1], L[b][0][-1]
            ta, tb = L[a][1][-1], L[b][1][-1]
            n = max(2, int(np.hypot(*(pb - pa)) / 10.0))
            f = np.linspace(0, 1, n + 1)[1:-1]
            self.edges[a + b] = (pa + f[:, None] * (pb - pa), ta + f[:, None] * (tb - ta))
        # Root panel: the sculpt's inner edge, from the rib's end up to the arm's stump, found row by row.
        rows = np.arange(L["spur"][0][-1][1] - 6, L["arm"][0][0][1] + 24, -10.0)
        edge = []
        for py in rows:
            cells = w5_sample(have.astype(np.float32), np.stack([np.arange(0, 400.0), np.full(400, py)], axis=1))
            xs = np.flatnonzero(cells > 0.5)
            edge.append((float(xs[0]) if len(xs) else 0.0, py))
        edge = np.array(edge + [tuple(L["arm"][0][0] + (0, 18))])
        # Its targets: the trailing part from the rib's end to flank_b, then the flank to flank_a and the stump.
        path = np.array([L["spur"][1][-1], tgt["flank_b"], tgt["flank_a"], tgt["start"]])
        plen = np.r_[0, np.cumsum(np.linalg.norm(np.diff(path, axis=0), axis=1))]
        elen = np.r_[0, np.cumsum(np.hypot(*np.diff(np.vstack([L["spur"][0][-1], edge]), axis=0).T))]
        f = elen[1:] / elen[-1] * plen[-1]
        self.root_edge = (edge, np.stack([np.interp(f, plen, path[:, c]) for c in range(3)], axis=1))
        # Panels: closed plan polygons and the 3D point of every corner.
        def poly(parts):
            p = np.concatenate([q[0] for q in parts])
            t = np.concatenate([q[1] for q in parts])
            keep = np.r_[True, np.hypot(*np.diff(p, axis=0).T) > 1e-6]
            p, t = p[keep], t[keep]
            if np.hypot(*(p[-1] - p[0])) < 1e-6:
                p, t = p[:-1], t[:-1]
            return p, t
        self.panels = {
            "root": poly([L["arm"], (L["spur"][0][1:], L["spur"][1][1:]), self.root_edge]),
            "spur_f3": poly([L["fore"], (L["f3"][0][1:], L["f3"][1][1:]), self.edges["f3spur"],
                             (rev(L["spur"][0])[:-1], rev(L["spur"][1])[:-1])]),
            "f3_f2": poly([L["f2"], self.edges["f2f3"], (rev(L["f3"][0])[:-1], rev(L["f3"][1])[:-1])]),
            "f2_f1": poly([L["f1"], self.edges["f1f2"], (rev(L["f2"][0])[:-1], rev(L["f2"][1])[:-1])]),
        }
        # Rigid segments: every bone line, each claw, and the thumb piece.
        self.segs = []          # (a, b, za, zb, A, B, r0, chain)
        for name, (line, t3, r0) in L.items():
            z = w5_sample(mid, line)
            for i in range(len(line) - 1):
                self.segs.append([line[i], line[i + 1], z[i], z[i + 1], t3[i], t3[i + 1], r0, name])
            if name in W5_CLAWS:
                tip = np.array(W5_CLAWS[name], float)
                d3 = t3[-1] - t3[-2]
                d3 /= np.linalg.norm(d3)
                length = np.hypot(*(tip - line[-1])) * plan.k * scale
                zt = float(w5_sample(mid, tip[None])[0])
                self.segs.append([line[-1], tip, z[-1], zt, t3[-1], t3[-1] + d3 * length, r0, name + "_claw"])
        self.claw_shorten = 0.0
        # Each segment's frame. Its normal is the panel surface's beside it.
        for s in self.segs:
            d = s[5] - s[4]
            d = d / np.linalg.norm(d)
            mp = (s[0] + s[1]) / 2
            u = (s[1] - s[0]) / np.hypot(*(s[1] - s[0]))
            left = np.array([u[1], -u[0]])
            ns = []
            for sgn in (1, -1):
                q = (mp + sgn * (s[6] + 6) * left)[None]
                n = self.panel_normal(q)
                if n is not None:
                    ns.append(n[0])
            n = np.mean(ns, axis=0) if ns else self.up
            n = n - (n @ d) * d
            n /= np.linalg.norm(n)
            s.append(d)
            s.append(n)
            s.append(np.cross(n, d))        # the 3D left, matching the plan's left seen from above

    def which_panel(self, p):
        out = np.full(len(p), -1)
        for k, (poly, _) in enumerate(self.panels.values()):
            out[(out < 0) & w5_inside(p, poly)] = k
        return out

    def panel_point(self, p, k):
        poly, t3 = list(self.panels.values())[k]
        return w5_mvc(p, poly) @ t3

    def panel_normal(self, p, eps=2.0):
        k = self.which_panel(p)
        if (k < 0).any():
            return None
        out = np.zeros((len(p), 3))
        for kk in np.unique(k):
            sel = k == kk
            q = p[sel]
            dx = self.panel_point(q + (eps, 0), kk) - self.panel_point(q - (eps, 0), kk)
            dy = self.panel_point(q - (0, eps), kk) - self.panel_point(q + (0, eps), kk)
            n = np.cross(dx, dy)
            out[sel] = n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
        return out

    def bone_distance(self, p):
        """Distance to the nearest bone segment in units of that segment's r0, and the segment."""
        best = np.full(len(p), np.inf)
        which = np.zeros(len(p), int)
        for i, s in enumerate(self.segs):
            d = w2_segment(p, s[0], s[1])[2] / s[6]
            closer = d < best
            best[closer], which[closer] = d[closer], i
        return best, which

    def rigid(self, p, z, only=None):
        acc = np.zeros((len(p), 3))
        total = np.zeros(len(p))
        for i, s in enumerate(self.segs):
            if only is not None and i not in only:
                continue
            t, sd, d = w2_segment(p, s[0], s[1])
            w = 1.0 / (d * d + 9.0) ** 2
            zl = s[2] + np.clip(t, 0, 1) * (s[3] - s[2])
            q = (s[4] + t[:, None] * (s[5] - s[4]) + (sd * self.plan.k * self.scale)[:, None] * s[10]
                 + ((z - zl) * self.scale)[:, None] * s[9])
            acc += w[:, None] * q
            total += w
        return acc / total[:, None]

    def __call__(self, p, z, billow=W5_BILLOW, chunk=20000):
        out = np.zeros((len(p), 3))
        thumb = [i for i, s in enumerate(self.segs) if s[7] == "fore"][-1:]
        for lo in range(0, len(p), chunk):
            q, zz = p[lo:lo + chunk], z[lo:lo + chunk]
            m = w5_sample(self.mid, q)
            k = self.which_panel(q)
            dist, _ = self.bone_distance(q)
            w = 1.0 - smoothstep((dist - 1.0) / (W5_REACH - 1.0))
            w[k < 0] = 1.0
            res = np.zeros((len(q), 3))
            need = w > 0
            if need.any():
                res[need] = self.rigid(q[need], zz[need])
            box = (q[:, 0] > W5_THUMB[0][0]) & (q[:, 0] < W5_THUMB[1][0]) & (q[:, 1] > W5_THUMB[0][1]) \
                & (q[:, 1] < W5_THUMB[1][1]) & (q[:, 1] < 46 + 0.0 * q[:, 0])
            if box.any():
                res[box] = self.rigid(q[box], zz[box], only=thumb)
                w[box] = 1.0
            for kk in np.unique(k[k >= 0]):
                sel = (k == kk) & (w < 1.0)
                if not sel.any():
                    continue
                if kk == 0 and getattr(self, "root", None) is not None:
                    sr, tr = self.root.st_of_plan(q[sel])
                    pan = self.root.point(sr, tr) + ((zz[sel] - m[sel]) * self.scale)[:, None] * self.root.normal(sr, tr)
                    res[sel] = w[sel, None] * res[sel] + (1 - w[sel, None]) * pan
                    continue
                poly, t3 = list(self.panels.values())[kk]
                base = w5_mvc(q[sel], poly)
                pt = base @ t3
                hb = m[sel] - base @ w5_sample(self.mid, poly)
                n = self.panel_normal(q[sel])
                n[(n @ self.up) < 0] *= -1
                pan = pt + ((billow * hb + (zz[sel] - m[sel])) * self.scale)[:, None] * n
                res[sel] = w[sel, None] * res[sel] + (1 - w[sel, None]) * pan
            out[lo:lo + chunk] = res
        return out


def w5_sheet(wmap):
    """The membrane's mid-surface as a plan triangulation: (plan points, triangles, boundary loop)."""
    from mathutils.geometry import delaunay_2d_cdt
    L = wmap.lines

    def resample(p, step):
        p = np.asarray(p, float)
        seg = np.hypot(*np.diff(p, axis=0).T)
        cum = np.r_[0, np.cumsum(seg)]
        n = max(1, int(round(cum[-1] / step)))
        at = np.linspace(0, cum[-1], n + 1)
        return np.stack([np.interp(at, cum, p[:, 0]), np.interp(at, cum, p[:, 1])], axis=1)

    root_edge = wmap.root_edge[0]
    outline = [resample(L["arm"][0], W5_EDGE), resample(L["fore"][0], W5_EDGE)[1:], resample(L["f1"][0], W5_EDGE)[1:]]
    # The trailing edge between two finger ends: the sculpt's own scallop, found along the chord.
    have = wmap.have
    for a, b in (("f1", "f2"), ("f2", "f3"), ("f3", "spur")):
        pa, pb = L[a][0][-1], L[b][0][-1]
        chord = resample(np.array([pa, pb]), W5_EDGE)
        u = (pb - pa) / np.hypot(*(pb - pa))
        inward = np.array([-u[1], u[0]])
        if (W5_HUB - (pa + pb) / 2) @ inward < 0:
            inward = -inward
        scal = []
        for c in chord[1:-1]:
            steps = np.arange(0, 200.0, 1.0)
            pts = c + steps[:, None] * inward
            h = w5_sample(have.astype(np.float32), pts) > 0.5
            first = np.flatnonzero(h)
            scal.append(pts[first[0] + 2] if len(first) else c)
        outline.append(np.vstack([scal, [pb]]) if scal else np.array([pb]))
    # The root panel is built apart (W5Root): the outline runs back up the rib to the elbow.
    rib = resample(L["spur"][0], W5_EDGE)
    outline[0] = outline[0][-1:]                  # the arm's line is the root panel's: keep only the elbow
    outline.append(rib[::-1][1:-1])
    ring = np.concatenate(outline)
    keep = np.r_[True, np.hypot(*np.diff(ring, axis=0).T) > 3.0]
    ring = ring[keep]
    if np.hypot(*(ring[-1] - ring[0])) < 3.0:
        ring = ring[:-1]
    # Inner bone lines as constraints, so a row of vertices lies on each bone.
    inner = [resample(L[n][0], W5_EDGE) for n in ("f2", "f3")]
    verts = [tuple(p) for p in ring]
    nb = len(ring)
    edges = [(i, (i + 1) % nb) for i in range(nb)]
    for line in inner:
        start = len(verts)
        for p in line:
            verts.append(tuple(p))
        edges += [(start + i, start + i + 1) for i in range(len(line) - 1)]
    # Interior points on a triangular grid, clear of every line.
    h = W5_EDGE
    xs = np.arange(ring[:, 0].min(), ring[:, 0].max(), h)
    ys = np.arange(ring[:, 1].min(), ring[:, 1].max(), h * math.sqrt(3) / 2)
    grid = np.array([(x + (h / 2 if j % 2 else 0), y) for j, y in enumerate(ys) for x in xs])
    grid = grid[w5_inside(grid, ring)]
    lines = [ring] + inner
    near = np.full(len(grid), np.inf)
    for line in lines:
        closed = line is ring
        for i in range(len(line) - (0 if closed else 1)):
            near = np.minimum(near, w2_segment(grid, line[i], line[(i + 1) % len(line)])[2])
    grid = grid[near > 0.55 * h]
    verts += [tuple(p) for p in grid]
    res = delaunay_2d_cdt([Vector(v) for v in verts], edges, [list(range(nb))], 1, 1e-4)
    co = np.array([tuple(v) for v in res[0]])
    tris = [f for f in res[2] if len(f) == 3]
    tris = np.array(tris)
    cen = co[tris].mean(1)
    tris = tris[w5_inside(cen, ring)]
    used = np.unique(tris)
    remap = np.full(len(co), -1)
    remap[used] = np.arange(len(used))
    out = co[used]
    rib_ids = []
    for q in rib:
        d = np.hypot(out[:, 0] - q[0], out[:, 1] - q[1])
        rib_ids.append(int(d.argmin()))
        assert d.min() < 1e-3, "a rib vertex is missing from the sheet"
    return out, remap[tris], np.array(rib_ids)


def stage_wing5():
    """The second wing sculpt fitted panel by panel to the skeleton's wing bones.
    Writes wing5.blend: `wing_src` (both wings in full detail, for the bake),
    `wing_low` and `wing_low_left` (the bones and claws, reduced), and
    `wing_sheet` and `wing_sheet_left` (the membrane, one sheet with two faces).
    Reads one thing from the body: the flank of body B (src2.blend), where the root panel attaches."""
    open_blend("wing5_src_raw.blend")
    src = bpy.data.objects["wing5_src"]
    if src.data.has_custom_normals:
        activate(src)
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
    plan = W5Plan(coords(src))
    flat = plan.flat(coords(src))
    set_coords(src, flat)
    zup, zlo = w5_rasters(src, plan)
    have = ~np.isnan(zup)
    thick = np.where(have, zup - zlo, 0.0)
    mid = w2_fill(np.where(have, (zup + zlo) / 2, 0.0), have, 40, 60)
    tgt = w5_targets()
    lines = w5_lines(thick, tgt)
    # Metres per sculpt unit across a bone: the skeleton's bone length over the sculpt's.
    sk = sum(np.linalg.norm(np.diff(lines[n][1], axis=0), axis=1).sum() for n in lines)
    sc = sum(np.hypot(*np.diff(lines[n][0], axis=0).T).sum() for n in lines) * plan.k
    scale = float(sk / sc)
    log("wing5: plan", round(plan.k * 800, 4), "units across; scale", round(scale, 3), "m per unit; membrane",
        round(float(np.median(thick[have & (thick < W5_BONE_T)])) * scale * 1000), "mm thick")
    # Thin the membrane about its middle to the sheet's thickness. Bones keep theirs.
    px_all = plan.px(flat[:, :2])
    t_v = w5_sample(np.where(have, thick, 0.0), px_all)
    m_v = w5_sample(mid, px_all)
    bone_v = smoothstep((t_v - 0.0068) / 0.004)
    bone_v *= ~w5_stub(px_all, lines)
    sheet_half = W5_SHEET / 2 / scale
    f = np.where(t_v > 1e-4, np.minimum(1.0, sheet_half / np.maximum(t_v / 2, 1e-6)), 1.0)
    f = f + (1.0 - f) * bone_v
    flat[:, 2] = m_v + (flat[:, 2] - m_v) * f
    set_coords(src, flat)
    wmap = W5Map(plan, lines, mid, have, tgt, scale)
    wmap.tgt = tgt
    wmap.have = have
    # Panel widths, plan against skeleton: the gap between the two bones' ends, and their angle at the root.
    for name, (poly, t3) in wmap.panels.items():
        log("  panel", name, "corners", len(poly))
    for a, b in (("f1", "f2"), ("f2", "f3"), ("f3", "spur")):
        pa, pb = lines[a][0][-1], lines[b][0][-1]
        ta, tb = lines[a][1][-1], lines[b][1][-1]
        log("  panel %s-%s: trailing gap in the sculpt %.2f m, in the skeleton %.2f m (%.0f%%)" % (
            a, b, np.hypot(*(pa - pb)) * plan.k * scale, np.linalg.norm(ta - tb),
            100 * (np.hypot(*(pa - pb)) * plan.k * scale / np.linalg.norm(ta - tb) - 1)))

    # The bones: the sculpt with the membrane taken out, reduced while flat.
    low = duplicate(src, "wing_low")
    strip_layers(low)
    weld(low)
    bm = bmesh.new()
    bm.from_mesh(low.data)
    cen = np.array([f.calc_center_median()[:] for f in bm.faces])
    cp = plan.px(cen[:, :2])
    tf = w5_sample(np.where(have, thick, 0.0), cp)
    dist, which = wmap.bone_distance(cp)
    claw = np.zeros(len(cp), bool)
    for name, tip in W5_CLAWS.items():
        base = lines[name][0][-1]
        t, s, d = w2_segment(cp, base, np.array(tip, float))
        claw |= (t > -0.15) & (d < 16)
    thumb = (cp[:, 0] > W5_THUMB[0][0]) & (cp[:, 0] < W5_THUMB[1][0]) & (cp[:, 1] < W5_THUMB[1][1])
    keep = ((tf > W5_BONE_T) | claw | thumb) & ~w5_stub(cp, lines)
    bmesh.ops.delete(bm, geom=[f for f, k in zip(bm.faces, keep) if not k], context="FACES")
    loose = [v for v in bm.verts if not v.link_faces]
    bmesh.ops.delete(bm, geom=loose, context="VERTS")
    # Islands of a few faces are scraps of the membrane's rim.
    seen, drop = set(), []
    for fc in bm.faces:
        if fc in seen:
            continue
        stack, comp = [fc], [fc]
        seen.add(fc)
        while stack:
            a = stack.pop()
            for e in a.edges:
                for g in e.link_faces:
                    if g not in seen:
                        seen.add(g)
                        stack.append(g)
                        comp.append(g)
        if len(comp) < 400:
            drop += comp
    bmesh.ops.delete(bm, geom=drop, context="FACES")
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
    bm.to_mesh(low.data)
    bm.free()
    topo_report(low, "wing5 bones cut out")
    decimate_to(low, W5_BONE_TRIS)
    topo_report(low, "wing5 bones decimated")
    bm = bmesh.new()
    bm.from_mesh(low.data)
    # The bones' open rims sink to the sheet's middle, inside the sheet.
    rim = [v for v in bm.verts if v.is_boundary]
    for v in rim:
        v.co.z = float(w5_sample(mid, plan.px(np.array([[v.co.x, v.co.y]])))[0])
    cen = np.array([f.calc_center_median()[:] for f in bm.faces])
    cp = plan.px(cen[:, :2])
    horn = np.zeros(len(cp), bool)
    for name, tip in W5_CLAWS.items():
        base = lines[name][0][-1]
        t, s, d = w2_segment(cp, base, np.array(tip, float))
        horn |= (t > 0.05) & (d < 16)
    horn |= (cp[:, 0] > W5_THUMB[0][0] + 4) & (cp[:, 0] < W5_THUMB[1][0]) & (cp[:, 1] < 38)
    for fc, h_ in zip(bm.faces, horn):
        fc.material_index = 2 if h_ else 0
        fc.smooth = True
    bm.to_mesh(low.data)
    bm.free()
    log("wing5 bones: rim vertices", len(rim), "claw faces", int(horn.sum()), "of", len(horn))
    low.data.materials.clear()
    for name in ("from_wing", "from_wing_membrane", "from_wing_horn"):
        low.data.materials.append(bpy.data.materials.new(name))
    low.data.polygons.foreach_set("material_index", np.where(horn, 2, 0).astype(np.int32))

    # The sheet.
    sp, stri, rib_ids = w5_sheet(wmap)
    smid = w5_sample(mid, sp)
    s3 = wmap(sp, smid)
    # The root panel: a ruled patch between the flank and the arm and rib, welded on the rib's vertices.
    body = append_object("src2.blend", "body2_src")
    root = W5Root(wmap, lines, body, sp[rib_ids], s3[rib_ids])
    bpy.data.objects.remove(body)
    wmap.root = root
    rco, rtri, rs, on_b = w5_root_sheet(root)
    rmap = np.full(len(rco), -1)
    for j, i in enumerate(on_b):
        if i >= root.n_arm:
            rmap[j] = rib_ids[i - root.n_arm]
    new = rmap < 0
    rmap[new] = len(s3) + np.arange(int(new.sum()))
    sheet_s = np.r_[np.full(len(s3), -1.0), rs[new]]
    s3 = np.vstack([s3, rco[new]])
    stri = np.vstack([stri, rmap[rtri]])
    log("wing5 root patch: vertices", int(new.sum()), "triangles", len(rtri), "welded on the rib", int((~new).sum()))
    sheet_me = bpy.data.meshes.new("wing_sheet")
    sheet_me.from_pydata(s3.tolist(), [], stri.tolist())
    sheet = bpy.data.objects.new("wing_sheet", sheet_me)
    bpy.context.collection.objects.link(sheet)
    bm = bmesh.new()
    bm.from_mesh(sheet_me)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.normal_update()
    if np.mean([f.normal.z for f in bm.faces]) < 0:
        bmesh.ops.reverse_faces(bm, faces=bm.faces)
        bm.normal_update()
    nv = len(bm.verts)
    nrm = np.array([v.normal[:] for v in bm.verts])
    pos = np.array([v.co[:] for v in bm.verts])
    upper_faces = [[v.index for v in f.verts] for f in bm.faces]
    boundary = [e for e in bm.edges if e.is_boundary]
    # Each boundary edge in the upper face's winding order.
    bedges = []
    for e in boundary:
        f = e.link_faces[0]
        vs = [v.index for v in f.verts]
        a, b = e.verts[0].index, e.verts[1].index
        i = vs.index(a)
        bedges.append((a, b) if vs[(i + 1) % 3] == b else (b, a))
    bm.free()
    half = W5_SHEET / 2
    co = np.concatenate([pos + nrm * half, pos - nrm * half])
    faces = [tuple(f) for f in upper_faces] + [tuple(nv + i for i in reversed(f)) for f in upper_faces]
    for a, b in bedges:
        faces.append((b, a, nv + a))
        faces.append((b, nv + a, nv + b))
    n_face = len(upper_faces)
    sheet_me = bpy.data.meshes.new("wing_sheet2")
    sheet_me.from_pydata(co.tolist(), [], faces)
    sheet.data = sheet_me
    sheet.data.name = "wing_sheet"
    for name in ("from_wing", "from_wing_membrane", "from_wing_horn"):
        sheet_me.materials.append(bpy.data.materials.new(name))
    sheet_me.polygons.foreach_set("material_index", np.ones(len(faces), np.int32))
    up = sheet_me.attributes.new("wing_upper", "FLOAT", "FACE")
    up.data.foreach_set("value", np.r_[np.ones(n_face), np.zeros(n_face), np.full(2 * len(bedges), 0.5)].astype(np.float32))
    for p in sheet_me.polygons:
        p.use_smooth = True
    sa = sheet_me.attributes.new("sheet_s", "FLOAT", "POINT")
    sa.data.foreach_set("value", np.r_[sheet_s, sheet_s].astype(np.float32))
    # Folds at rest: neighbouring triangles of one face that turn past 60 degrees.
    fn = np.cross(co[np.array(upper_faces)[:, 1]] - co[np.array(upper_faces)[:, 0]],
                  co[np.array(upper_faces)[:, 2]] - co[np.array(upper_faces)[:, 0]])
    fn /= np.linalg.norm(fn, axis=1, keepdims=True)
    pairs = {}
    for fi, f in enumerate(upper_faces):
        for a_, b_ in ((f[0], f[1]), (f[1], f[2]), (f[2], f[0])):
            pairs.setdefault((min(a_, b_), max(a_, b_)), []).append(fi)
    folds = [(v[0], v[1]) for v in pairs.values() if len(v) == 2 and fn[v[0]] @ fn[v[1]] < 0.5]
    in_root = [x for x in folds if any(sheet_s[i] >= 0 for i in upper_faces[x[0]])]
    log("wing5 sheet folds past 60 degrees at rest:", len(folds), "of them in the root patch:", len(in_root))
    el = np.concatenate([[np.linalg.norm(s3[t[i]] - s3[t[(i + 1) % 3]]) for i in range(3)] for t in stri])
    log("wing5 sheet: vertices a face", nv, "triangles a face", n_face, "rim", 2 * len(bedges),
        "edge length m: median", round(float(np.median(el)), 3), "5%", round(float(np.percentile(el, 5)), 3),
        "95%", round(float(np.percentile(el, 95)), 3), "max", round(float(el.max()), 3))
    topo_report(sheet, "wing5 sheet")

    # Into the build frame.
    lc = coords(low)
    lo3 = wmap(plan.px(lc[:, :2]), lc[:, 2])
    hi3 = wmap(px_all, flat[:, 2])
    # The tip claw must end at version 2's half span: the claw and the outer finger move in along the finger.
    span = max(float(lo3[:, 0].max()), float(hi3[:, 0].max()))
    over = span - WINGSPAN / 2
    log("wing5: tip at X", round(span, 3), "before the span fit")
    if abs(over) > 1e-4:
        for c in (lo3, hi3, ):
            c[:, 0] -= over * smoothstep((c[:, 0] - 7.6) / (span - 7.6))
        sco = coords(sheet)
        sco[:, 0] -= over * smoothstep((sco[:, 0] - 7.6) / (span - 7.6))
        set_coords(sheet, sco)
    set_coords(low, lo3)
    set_coords(src, hi3)
    plan_attr = src.data.attributes.new("plan", "FLOAT_VECTOR", "POINT")
    plan_attr.data.foreach_set("vector", np.stack([px_all[:, 0] / 800, px_all[:, 1] / 800, np.zeros(len(px_all))], 1).ravel())
    for p in low.data.polygons:
        p.use_smooth = True
    for name, (line, t3, r0) in lines.items():
        log("  bone line", name, "from", np.round(t3[0], 2).tolist(), "to", np.round(t3[-1], 2).tolist())
    np.savez(cache("wing5_plan.npz"), have=have, thick=thick.astype(np.float32), mid=mid.astype(np.float32),
             **{"line_" + k: v[0] for k, v in lines.items()}, **{"target_" + k: v[1] for k, v in lines.items()})
    # The left wing is the mirror.
    for obj in (src, low, sheet):
        other = duplicate(obj, obj.name + "_left")
        c = coords(other)
        c[:, 0] *= -1
        set_coords(other, c)
        activate(other)
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.mesh.flip_normals()
        bpy.ops.object.mode_set(mode="OBJECT")
        if obj is src:
            activate(obj, [other])
            bpy.ops.object.join()
    src.name = src.data.name = "wing_src"
    for o in list(bpy.data.objects):
        if o.name.endswith("_left") and o.name.startswith("wing_low"):
            o.name = o.data.name = "wing_low_left"
        if o.name.endswith("_left") and o.name.startswith("wing_sheet"):
            o.name = o.data.name = "wing_sheet_left"
    log("wing5 objects", sorted(o.name for o in bpy.data.objects))
    save_blend("wing5.blend")


# ----------------------------------------------------------------- stage: align

# The sculpts come out of the generator with the nose at -Y, one unit long.
BODY_SCALE = WINGSPAN / 1.1730      # the body sculpt's X extent is 1.1730 units
HEAD_SCALE = 3.0                    # metres per unit: the head sculpt's horns then lie on the body's
BODY_SNOUT = (4.495, 4.60)          # Y, Z of the body sculpt's snout tip in metres
# The head sculpt's neck is 1.5 m deep and 1.33 m wide where it ends. The
# body's neck is 1.1 m by 0.95 m there. Below the jaw the head sculpt is
# scaled about the neck axis, more with depth, until its lower rim is
# inside the body's neck.
TAPER_TOP, TAPER_BOTTOM = 4.40, 3.58
TAPER_SCALE = 0.62
TAPER_AXIS_TOP, TAPER_AXIS_BOTTOM = 2.50, 2.40    # the neck axis in Y
NECK_MATCH_KEEP = 0.6               # share of the head sculpt's own colour kept at the top of the head
NECK_CUT_Z = 4.0                    # the body sculpt ends here, inside the head sculpt's neck
NECK_BINS = 6                       # directions round the neck in which the seam's colours are matched
NECK_BIN_CLAMP = 0.5                # the largest log gain a direction may ask for


def smoothstep(x):
    x = np.clip(x, 0.0, 1.0)
    return x * x * (3 - 2 * x)


def stage_align():
    """Both sculpts in the build frame: metres, nose at +Y, feet at Z=0."""
    open_blend("body_src_raw.blend")
    body = bpy.data.objects["body_src"]
    co = coords(body) * np.array([-1.0, -1.0, 1.0]) * BODY_SCALE
    co[:, 0] -= (co[:, 0].min() + co[:, 0].max()) / 2
    co[:, 2] -= co[:, 2].min()
    set_coords(body, co)
    log("body bounds", co.min(0).round(3), co.max(0).round(3))

    head = append_object("head_src_raw.blend", "head_src")
    hc = coords(head) * np.array([-1.0, -1.0, 1.0])
    tip = hc[hc[:, 1].argmax()]
    hc = hc * HEAD_SCALE + np.array([-np.median(hc[:, 0]) * HEAD_SCALE,
                                     BODY_SNOUT[0] - tip[1] * HEAD_SCALE,
                                     BODY_SNOUT[1] - tip[2] * HEAD_SCALE])
    w = smoothstep((TAPER_TOP - hc[:, 2]) / (TAPER_TOP - TAPER_BOTTOM))
    # ease in, then nearly linear: the collar becomes a cone
    k = 1.0 + (TAPER_SCALE - 1.0) * w
    axis = TAPER_AXIS_TOP + (TAPER_AXIS_BOTTOM - TAPER_AXIS_TOP) * w
    hc[:, 0] *= k
    hc[:, 1] = axis + (hc[:, 1] - TAPER_AXIS_TOP) * k
    set_coords(head, hc)
    log("head bounds", hc.min(0).round(3), hc.max(0).round(3))
    save_blend("src_aligned.blend")


# ------------------------------------------------------------------- stage: low

HEAD_TRIS = 31000
BODY_TRIS = 68000


def weld(obj, dist=1e-5):
    activate(obj)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.remove_doubles(threshold=dist)
    bpy.ops.object.mode_set(mode="OBJECT")


def topo_report(obj, tag):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    nb = sum(1 for e in bm.edges if e.is_boundary)
    nm = sum(1 for e in bm.edges if not e.is_manifold and not e.is_boundary)
    bm.free()
    log(tag, "verts", len(obj.data.vertices), "tris", tri_count(obj), "boundary edges", nb, "non-manifold", nm)
    return nb, nm


def strip_layers(obj):
    me = obj.data
    while len(me.uv_layers):
        me.uv_layers.remove(me.uv_layers[0])
    me.materials.clear()


def decimate_to(obj, tris):
    n = tri_count(obj)
    mod = obj.modifiers.new("dec", "DECIMATE")
    mod.ratio = tris / n
    mod.use_collapse_triangulate = True
    apply_modifier(obj, mod)


def cut_neck(body):
    """The body loses everything above the neck cut: its own head."""
    bm = bmesh.new()
    bm.from_mesh(body.data)
    back = FIT.get("neck_box_y", 1.5)
    box = [f for f in bm.faces if all(v.co.y > back and abs(v.co.x) < 1.4 and v.co.z > NECK_CUT_Z - 0.5
                                      for v in f.verts)]
    geom = set(box)
    for f in box:
        geom.update(f.edges)
        geom.update(f.verts)
    bmesh.ops.bisect_plane(bm, geom=list(geom), dist=1e-6, plane_co=(0, 0, NECK_CUT_Z),
                           plane_no=(0, 0, 1), clear_outer=False, clear_inner=False)
    kill = [v for v in bm.verts if v.co.z > NECK_CUT_Z + 1e-5 and v.co.y > back and abs(v.co.x) < 1.4]
    bmesh.ops.delete(bm, geom=kill, context="VERTS")
    edges = [e for e in bm.edges if e.is_boundary and all(abs(v.co.z - NECK_CUT_Z) < 1e-4 for v in e.verts)]
    log("neck cut: boundary edges", len(edges))
    bmesh.ops.holes_fill(bm, edges=edges, sides=0)
    bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 3])
    bm.to_mesh(body.data)
    bm.free()
    topo_report(body, "body cut")


def stage_low_parts():
    """Reduce the head sculpt. It is reduced in the first build's taper, so
    the head's triangles are the same in every build; `low2` moves them."""
    open_blend("src_aligned.blend")
    head = duplicate(bpy.data.objects["head_src"], "head_low")
    for o in (bpy.data.objects["body_src"], bpy.data.objects["head_src"]):
        bpy.data.objects.remove(o)
    strip_layers(head)
    weld(head)
    topo_report(head, "head welded")
    decimate_to(head, HEAD_TRIS)
    topo_report(head, "head decimated")
    save_blend("low_parts.blend")


# ------------------------------------------------------- stages: align2, low2
#
# Version 2 takes the torso, neck, legs and tail from a second body sculpt
# that was generated without wings. The first body sculpt gives the wings
# only. The second sculpt goes into the first one's frame, so the first
# sculpt's wings stay where they were.

BODY2_TRIS = 52000
WING_TRIS = 20000
TAIL_WARP_Y = -1.5                  # behind this the new tail is fitted to the old tail
TAPER2_AXIS_BOTTOM = 2.67           # the head sculpt's collar now tapers about an axis further forward
NECK2_TOP_SCALE = 0.80              # the new neck narrows to this at the cut, so its cap stays inside the head
NECK2_AXIS_Y = 2.62
HIND_WIDEN = 0.15                   # body A: the hind legs move out this far, to the first sculpt's stance
# How each wingless body sculpt goes into the frame. Body A (version 2) has four straight legs,
# and is scaled and placed by its feet. Body B (version 4) stands with bent legs, lower, and
# carries its neck forward, so its feet say nothing about its scale. Its numbers were fitted by
# eye on side silhouettes of both bodies (tools/hero-build.md, "Version 4"):
#   scale, y_shift   body A's scale, and the generator's own Y: the two torsos then coincide in plan
#   z_scale          body B's back is 0.25 m lower; 1.07 puts its back and its belly on body A's
#   neck_*           the neck leans 45 degrees forward where the head sculpt's collar stands at 75.
#                    Past a band across the neck's base everything turns about the pivot.
#   hind_widen       0: body B already stands wider than the first sculpt. Body A's move also left
#                    a step across the tail root, because its box ended at Y -1.7 with no fade.
#   hind_narrow      the hind feet come in this far (see stage_align2)
#   neck_box_y       the neck cut's box reaches back to here: the turned head's horns end at Y 1.4
#   tail_lift_y      the tail's lift starts behind the hind feet, which reach back to Y -1.75
BODY_FIT = {
    "embercrest-cand-hero-body-hunyuan.glb": dict(hind_widen=HIND_WIDEN, tail_lift_y=TAIL_WARP_Y),
    "embercrest-cand-hero-body2-hunyuan.glb": dict(
        scale=13.037, y_shift=0.0, z_scale=1.07, neck_pivot=(2.3, 3.1), neck_dir_deg=45.0, neck_band=1.4,
        neck_turn_deg=30.0, neck_shift=(0.2, 0.0), hind_widen=0.0, hind_narrow=0.35, tail_lift_y=-1.8, neck_box_y=0.8),
}
BODY_KEY = os.path.basename(BODY2_IN)
if BODY_KEY not in BODY_FIT:
    raise SystemExit("no BODY_FIT entry for " + BODY_KEY + ": measure the body first (tools/hero-build.md)")
FIT = BODY_FIT[BODY_KEY]
BODY_B = BODY_KEY == "embercrest-cand-hero-body2-hunyuan.glb"
# The wing piece of the first sculpt: outside WING_XC, and near the body only
# above WING_FLOOR and between WING_Y. Past WING_FREE_X everything is wing.
WING_XC = 0.30
WING_FREE_X = 1.75
WING_FLOOR = 2.70
WING_Y = (-1.30, 1.95)
WING_SINK = 0.45                    # the root's top slopes down this far toward the spine
WING_SINK_X = (0.40, 0.90)
WING_TUCK_X = 0.30                  # what lies under the root is drawn in toward this half width
WING_TUCK_KEEP = 0.15
WING_TUCK_Y = (-1.5, 0.3, 1.0, 1.5, 1.9)
WING_TUCK_Z = (3.05, 3.05, 3.25, 3.45, 3.60)
WING_TUCK_DEPTH = 0.35
SLIVER_FACES = 150                  # an island of one sculpt smaller than this, inside another, is a sliver
WING_SEAM_SMOOTH = (2, 4)           # rings and passes of smoothing along the wing's seam


def retaper_head(co):
    """Move head sculpt points from the first build's taper to version 2's.
    Only the collar below TAPER_TOP moves."""
    co = np.array(co, dtype=np.float64)
    w = smoothstep((TAPER_TOP - co[:, 2]) / (TAPER_TOP - TAPER_BOTTOM))
    k = 1.0 + (TAPER_SCALE - 1.0) * w
    axis1 = TAPER_AXIS_TOP + (TAPER_AXIS_BOTTOM - TAPER_AXIS_TOP) * w
    axis2 = TAPER_AXIS_TOP + (TAPER2_AXIS_BOTTOM - TAPER_AXIS_TOP) * w
    co[:, 1] += axis2 - axis1
    return co


def wing_region(co, eps=0.0):
    a = np.abs(co[:, 0])
    near = (co[:, 2] > WING_FLOOR - eps) & (co[:, 1] > WING_Y[0] - eps) & (co[:, 1] < WING_Y[1] + eps)
    return (a > WING_XC - eps) & ((a > WING_FREE_X) | near)


def wing_deform(co):
    """Make the wing's root dive into the new body. The top of the root
    slopes down toward the spine. What lies under the root (the first
    sculpt's own flank) is drawn in under the new body's surface."""
    co = np.array(co, dtype=np.float64)
    a = np.abs(co[:, 0])
    sgn = np.where(co[:, 0] < 0, -1.0, 1.0)
    co[:, 2] -= WING_SINK * (1.0 - smoothstep((a - WING_SINK_X[0]) / (WING_SINK_X[1] - WING_SINK_X[0])))
    zs = np.interp(co[:, 1], WING_TUCK_Y, WING_TUCK_Z)
    g = smoothstep((co[:, 2] - (zs - WING_TUCK_DEPTH)) / WING_TUCK_DEPTH)
    near = 1.0 - smoothstep((a - 1.1) / 0.4)
    t = (1.0 - g) * near * (1.0 - WING_TUCK_KEEP)
    co[:, 0] = sgn * (a + (WING_TUCK_X - a) * t)
    return co


def belly_line(co, ys):
    """The lowest point of the centre strip in each Y bin."""
    strip = co[np.abs(co[:, 0]) < 0.15]
    out = np.full(len(ys), np.nan)
    for i, y in enumerate(ys):
        s = strip[np.abs(strip[:, 1] - y) < 0.125, 2]
        if len(s):
            out[i] = s.min()
    return out


def foot_centres(co):
    """Y of the fore feet and of the hind feet, and the mean half width of each pair."""
    low = co[(co[:, 2] < 0.25) & (np.abs(co[:, 0]) < 2.2)]
    fore, hind = low[low[:, 1] > 1.0], low[low[:, 1] <= 1.0]
    return (fore[:, 1].mean(), hind[:, 1].mean(), np.abs(fore[:, 0]).mean(), np.abs(hind[:, 0]).mean())


def stage_align2():
    """The second body sculpt in the first one's frame, the head sculpt in
    its new taper, and the first sculpt's wings as a bake source."""
    open_blend("src_aligned.blend")
    old = bpy.data.objects["body_src"]
    head = bpy.data.objects["head_src"]
    oc = coords(old)
    b2 = append_object("body2_src_raw.blend", "body2_src")
    c = coords(b2) * np.array([-1.0, -1.0, 1.0])
    c[:, 0] -= (c[:, 0].min() + c[:, 0].max()) / 2
    c[:, 2] -= c[:, 2].min()
    # Scale and place by the feet: the same distance from fore feet to hind feet.
    unit = c[c[:, 2] < 0.02]
    split = (unit[:, 1].min() + unit[:, 1].max()) / 2
    uf, uh = unit[unit[:, 1] > split][:, 1].mean(), unit[unit[:, 1] <= split][:, 1].mean()
    of, oh, _, _ = foot_centres(oc)
    if "scale" in FIT:
        scale, shift = FIT["scale"], FIT["y_shift"]
    else:
        scale = (of - oh) / (uf - uh)
        shift = of - uf * scale
    c *= scale
    c[:, 1] += shift
    log("body2 scale", round(float(scale), 4), "Y shift", round(float(shift), 4))
    if "z_scale" in FIT:
        c[:, 2] *= FIT["z_scale"]
    if "neck_turn_deg" in FIT:
        # Stand the neck up under the head sculpt's collar.
        pivot = np.array(FIT["neck_pivot"])
        d = np.array([math.cos(math.radians(FIT["neck_dir_deg"])), math.sin(math.radians(FIT["neck_dir_deg"]))])
        rel = c[:, 1:3] - pivot
        t = smoothstep((rel @ d) / FIT["neck_band"] + 0.5) * (c[:, 1] > 0.8)
        ang = math.radians(FIT["neck_turn_deg"]) * t
        cs, sn = np.cos(ang), np.sin(ang)
        c[:, 1] = pivot[0] + rel[:, 0] * cs - rel[:, 1] * sn + FIT["neck_shift"][0] * t
        c[:, 2] = pivot[1] + rel[:, 0] * sn + rel[:, 1] * cs + FIT["neck_shift"][1] * t
    # The tail: the same length as the old tail, and its belly on the old belly.
    tip_new, tip_old = c[:, 1].min(), oc[:, 1].min()
    k = (tip_old - TAIL_WARP_Y) / (tip_new - TAIL_WARP_Y)
    tail = c[:, 1] < TAIL_WARP_Y
    c[tail, 1] = TAIL_WARP_Y + (c[tail, 1] - TAIL_WARP_Y) * k
    ys = np.arange(tip_old + 0.5, TAIL_WARP_Y + 0.01, 0.25)
    dz = belly_line(oc, ys) - belly_line(c, ys)
    dz = np.convolve(np.pad(dz, 2, mode="edge"), np.ones(5) / 5, mode="valid")
    lift = np.interp(c[:, 1], ys, dz) * smoothstep((FIT["tail_lift_y"] - c[:, 1]) / 1.0)
    c[:, 2] += lift
    log("tail: Y scale", round(float(k), 4), "lift", round(float(dz.min()), 3), "to", round(float(dz.max()), 3))
    # The neck narrows a little toward the cut, so the head sculpt's collar covers it.
    col = (c[:, 1] > 1.5) & (np.abs(c[:, 0]) < 1.4) & (c[:, 2] > 3.4)
    kk = 1.0 + (NECK2_TOP_SCALE - 1.0) * smoothstep((c[col, 2] - 3.55) / 0.45)
    c[col, 0] *= kk
    c[col, 1] = NECK2_AXIS_Y + (c[col, 1] - NECK2_AXIS_Y) * kk
    # The hind legs stand as wide as the old ones.
    leg = (c[:, 1] > -1.7) & (c[:, 1] < 0.9) & (c[:, 2] < 2.5)
    a = np.abs(c[leg, 0])
    c[leg, 0] += np.sign(c[leg, 0]) * FIT["hind_widen"] * smoothstep((a - 0.30) / 0.25) * (1.0 - smoothstep((c[leg, 2] - 1.9) / 0.6))
    if "hind_narrow" in FIT:
        # Body B's hind legs splay: the feet stand 1.7 m from the centre plane under hips at 0.8.
        # Lean each leg in below the thigh. The move grows from nothing at Z 2.4 to the full
        # amount at the ground, and only outside the body's own half width, so no step is left.
        f = np.clip((2.4 - c[:, 2]) / 2.4, 0.0, 1.0) ** 1.5
        w = smoothstep((np.abs(c[:, 0]) - 0.55) / 0.3) * (1.0 - smoothstep((c[:, 1] + 0.1) / 0.4))
        c[:, 0] -= np.sign(c[:, 0]) * FIT["hind_narrow"] * f * w
    c[:, 2] -= c[:, 2].min()
    set_coords(b2, c)
    log("body2 bounds", c.min(0).round(3), c.max(0).round(3))

    set_coords(head, retaper_head(coords(head)))

    if WING_V3 or WING_V5:
        # The first sculpt only set the frame. The version 3 and 5 wings take nothing else from it.
        bpy.data.objects.remove(old)
        save_blend("src2.blend")
        return
    # The wings: drop the rest of the first sculpt, then move the root.
    keep = wing_region(oc)
    old.data.vertices.foreach_set("select", ~keep)
    activate(old)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_mode(type="VERT")
    bpy.ops.object.mode_set(mode="OBJECT")
    old.data.vertices.foreach_set("select", ~keep)
    old.data.polygons.foreach_set("select", np.zeros(len(old.data.polygons), bool))
    old.data.edges.foreach_set("select", np.zeros(len(old.data.edges), bool))
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.delete(type="VERT")
    bpy.ops.object.mode_set(mode="OBJECT")
    set_coords(old, wing_deform(coords(old)))
    old.name = "wing_src"
    old.data.name = "wing_src"
    log("wing source: triangles", tri_count(old))
    save_blend("src2.blend")


def low2_wings_v2():
    """Version 2's wings: both cut out of the first sculpt as one capped piece, then reduced."""
    wing = append_object("src_aligned.blend", "body_src")
    wing.name = wing.data.name = "wing_low"
    strip_layers(wing)
    weld(wing)
    co = coords(wing)
    drop = ~wing_region(co, eps=0.25)
    wing.data.vertices.foreach_set("select", drop)
    activate(wing)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_mode(type="VERT")
    bpy.ops.object.mode_set(mode="OBJECT")
    wing.data.vertices.foreach_set("select", drop)
    wing.data.polygons.foreach_set("select", np.zeros(len(wing.data.polygons), bool))
    wing.data.edges.foreach_set("select", np.zeros(len(wing.data.edges), bool))
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.delete(type="VERT")
    bpy.ops.object.mode_set(mode="OBJECT")
    bm = bmesh.new()
    bm.from_mesh(wing.data)

    def near_faces():
        return [f for f in bm.faces if all(abs(v.co.x) < WING_FREE_X for v in f.verts)]

    for side in (1, -1):
        bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], dist=1e-6,
                               plane_co=(side * WING_XC, 0, 0), plane_no=(1, 0, 0))
    for pco, pno in (((0, 0, WING_FLOOR), (0, 0, 1)), ((0, WING_Y[0], 0), (0, 1, 0)), ((0, WING_Y[1], 0), (0, 1, 0))):
        bmesh.ops.bisect_plane(bm, geom=face_geom(near_faces()), dist=1e-6, plane_co=pco, plane_no=pno)
    e = 1e-5
    kill = [v for v in bm.verts
            if not (abs(v.co.x) > WING_XC - e and (abs(v.co.x) > WING_FREE_X or (
                v.co.z > WING_FLOOR - e and WING_Y[0] - e < v.co.y < WING_Y[1] + e)))]
    bmesh.ops.delete(bm, geom=kill, context="VERTS")
    edges = [e_ for e_ in bm.edges if e_.is_boundary]
    log("wing cut: boundary edges", len(edges))
    bmesh.ops.holes_fill(bm, edges=edges, sides=0)
    bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 3])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(wing.data)
    bm.free()
    set_coords(wing, wing_deform(coords(wing)))
    topo_report(wing, "wings cut")
    decimate_to(wing, WING_TRIS)
    topo_report(wing, "wings decimated")


def stage_low2():
    """Reduce the new body and the wings, and move the reduced head into the new taper."""
    reset_scene()
    body = append_object("src2.blend", "body2_src")
    body.name = body.data.name = "body_low"
    strip_layers(body)
    weld(body)
    topo_report(body, "body2 welded")
    cut_neck(body)
    decimate_to(body, BODY2_TRIS)
    topo_report(body, "body2 decimated")

    if WING_V3:
        for name in ("wing_low", "wing_low_left"):
            topo_report(append_object("wing2.blend", name), name + " from the wing stage")
    elif WING_V5:
        for name in ("wing_low", "wing_low_left", "wing_sheet", "wing_sheet_left"):
            topo_report(append_object("wing5.blend", name), name + " from the wing5 stage")
    else:
        low2_wings_v2()

    head = append_object("low_parts.blend", "head_low")
    set_coords(head, retaper_head(coords(head)))
    save_blend("low2.blend")


def stage_join():
    """One closed surface: the union of the reduced body, head and wings."""
    open_blend("low2.blend")
    body = bpy.data.objects["body_low"]
    head = bpy.data.objects["head_low"]
    wings = [bpy.data.objects[n] for n in (("wing_low", "wing_low_left") if WING_V3 else
                                           ("wing_low", "wing_low_left", "wing_sheet", "wing_sheet_left") if WING_V5
                                           else ("wing_low",))]
    SHEET_WING = WING_V3 or WING_V5
    # Material slots carry the origin of every face through the union.
    # The version 3 wing brings three slots of its own: from_wing (its hide), from_wing_membrane and
    # from_wing_horn. Version 2's wing is one closed piece with one slot, and is unioned like the head.
    for obj, name in ((body, "from_body"), (head, "from_head")) + (() if SHEET_WING else ((wings[0], "from_wing"),)):
        obj.data.materials.clear()
        obj.data.materials.append(bpy.data.materials.new(name))
    if SHEET_WING:
        for i, m in enumerate(wings[0].data.materials):
            m.name = m.name.split(".")[0]
            for w in wings[1:]:
                w.data.materials[i] = m
    for other in ((head,) if SHEET_WING else (wings[0], head)):
        before = body.data.copy()
        span = coords(body)[:, 1].max() - coords(body)[:, 1].min()
        mod = body.modifiers.new("union", "BOOLEAN")
        mod.operation = "UNION"
        mod.object = other
        mod.solver = "EXACT"
        mod.material_mode = "TRANSFER"
        apply_modifier(body, mod)
        if coords(body)[:, 1].max() - coords(body)[:, 1].min() < span - 0.5:
            # The exact solver lost the body: with body B it returns one wing alone. The manifold
            # solver joins them, once the wing piece's ten edges with four faces are collapsed.
            log("the exact union lost the body; using the manifold solver")
            body.data = before
            bm = bmesh.new()
            bm.from_mesh(other.data)
            for _ in range(3):
                bad = [e for e in bm.edges if not e.is_manifold]
                if not bad:
                    break
                bmesh.ops.collapse(bm, edges=bad)
                bmesh.ops.dissolve_degenerate(bm, dist=1e-6, edges=bm.edges)
            bm.to_mesh(other.data)
            bm.free()
            mod = body.modifiers.new("union", "BOOLEAN")
            mod.operation = "UNION"
            mod.object = other
            mod.solver = "MANIFOLD"
            trees = [BVHTree.FromPolygons([v.co[:] for v in m.vertices], [p.vertices[:] for p in m.polygons])
                     for m in (before, other.data)]
            apply_modifier(body, mod)
            # That solver does not carry the operand's material. A face is the wing's where the
            # wing piece's surface is nearer than the body's.
            wing_mat = other.data.materials[0]
            if wing_mat.name not in [m.name for m in body.data.materials]:
                body.data.materials.append(wing_mat)
            slot = [m.name for m in body.data.materials].index(wing_mat.name)
            n = 0
            for poly in body.data.polygons:
                if trees[1].find_nearest(poly.center)[3] < trees[0].find_nearest(poly.center)[3]:
                    poly.material_index = slot
                    n += 1
            log("faces given to", other.data.materials[0].name, n)
        bpy.data.objects.remove(other)
        topo_report(body, "after a union")
    body.name = "low"
    body.data.name = "low"
    names = [m.name for m in body.data.materials]
    wing_slots = {i for i, n in enumerate(names) if n.startswith("from_wing")}
    origin = {i: ("wing" if i in wing_slots else n) for i, n in enumerate(names)}
    bm = bmesh.new()
    bm.from_mesh(body.data)
    bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 3])
    # Drop closed pockets the union left inside: keep the largest shell.
    seen = set()
    shells = []
    for v in bm.verts:
        if v in seen:
            continue
        stack, shell = [v], []
        seen.add(v)
        while stack:
            a = stack.pop()
            shell.append(a)
            for e in a.link_edges:
                b = e.other_vert(a)
                if b not in seen:
                    seen.add(b)
                    stack.append(b)
        shells.append(shell)
    shells.sort(key=len, reverse=True)
    log("shells", [len(s) for s in shells[:8]])
    for s in shells[1:]:
        bmesh.ops.delete(bm, geom=s, context="VERTS")
    # Where two sculpts cross at a shallow angle, slivers of one poke through
    # the other. Cut each small island off and close the hole it leaves.
    seen, islands = set(), []
    for f in bm.faces:
        if f in seen:
            continue
        stack, comp = [f], [f]
        seen.add(f)
        while stack:
            a = stack.pop()
            for e in a.edges:
                for g in e.link_faces:
                    if origin[g.material_index] == origin[a.material_index] and g not in seen:
                        seen.add(g)
                        stack.append(g)
                        comp.append(g)
        if len(comp) < SLIVER_FACES:
            islands.append(comp)
    dropped = 0
    for comp in islands:
        own = origin[comp[0].material_index]
        around = [g.material_index for f in comp for e in f.edges for g in e.link_faces if origin[g.material_index] != own]
        if not around:
            continue
        other = max(set(around), key=around.count)
        rim = [e for f in comp for e in f.edges if any(origin[g.material_index] != own for g in e.link_faces)]
        bmesh.ops.delete(bm, geom=comp, context="FACES")
        rim = [e for e in rim if e.is_valid and e.is_boundary]
        made = bmesh.ops.holes_fill(bm, edges=rim, sides=0)["faces"]
        for g in made:
            g.material_index = other
        dropped += 1
    bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 3])
    log("slivers cut off:", dropped, "of", len(islands), "small islands")
    if not SHEET_WING:
        # Soften the crease where each wing's root comes out of the body.
        ring = set()
        for e in bm.edges:
            if len(e.link_faces) == 2 and (e.link_faces[0].material_index in wing_slots) != (
                    e.link_faces[1].material_index in wing_slots):
                ring.update(e.verts)
        seam = len(ring)
        for _ in range(WING_SEAM_SMOOTH[0]):
            ring |= {e.other_vert(v) for v in ring for e in v.link_edges}
        for _ in range(WING_SEAM_SMOOTH[1]):
            bmesh.ops.smooth_vert(bm, verts=list(ring), factor=0.5, use_axis_x=True, use_axis_y=True, use_axis_z=True)
        log("wing seam: vertices on it", seam, "smoothed", len(ring))
    bm.to_mesh(body.data)
    bm.free()
    # The wings are not unioned. A reduced membrane is a sheet 40 mm thick whose two faces
    # touch here and there, and the exact solver drops such a mesh or, told to repair it,
    # tears the sheet into two thousand pieces. Each wing keeps its own closed surface: its
    # arm and the root of its membrane end inside the body.
    if SHEET_WING:
        activate(body, wings)
        bpy.ops.object.join()
    names = [m.name for m in body.data.materials]
    nb, nm = topo_report(body, "joined")
    for label in ("from_head", "from_wing") + (("from_wing_membrane", "from_wing_horn") if SHEET_WING else ()):
        n = sum(1 for p in body.data.polygons if names[p.material_index] == label)
        log("triangles", label, n, "share", round(n / len(body.data.polygons), 3))
    save_blend("joined.blend")


# ------------------------------------------------------------------ stage: head
#
# Everything here was read off renders of the head sculpt in the build frame.

# The lip line seen from the side, (Y, Z) from the mouth corner to the snout.
# The sculpt has an overbite: the upper lip ends in a shelf 15 to 45 mm wide
# and the lower jaw's wall starts at the shelf's inner edge. The cut runs
# 8 mm under the shelf, on the lower jaw's wall, so it never grazes the shelf.
LIP = [(3.40, 4.580), (3.60, 4.580), (3.70, 4.578), (3.76, 4.578), (3.79, 4.572), (3.82, 4.564),
       (3.85, 4.552), (3.88, 4.540), (3.91, 4.532), (3.94, 4.528), (3.97, 4.524), (4.00, 4.520),
       (4.045, 4.516), (4.09, 4.512), (4.12, 4.508), (4.27, 4.508), (4.365, 4.495), (4.41, 4.475),
       (4.43, 4.465), (4.60, 4.465)]
MOUTH_CORNER_Y = 3.60
JAW_HINGE = (3.42, 4.64)            # Y, Z: the axis is parallel to X
JAW_OPEN_DEG = 35.0
LIP_LOOPS = (0.018, 0.045)          # supporting loops this far above and below the lip
WARP_OFFSET = 100.0

# The eyes: a point on the painted eye, the outward normal there, the long
# axis of the opening (toward the rear corner).
EYE_SURFACE = {1: (0.314, 3.722, 4.836), -1: (-0.334, 3.722, 4.836)}
EYE_NORMAL = (0.78, 0.55, 0.28)     # X is mirrored for the left eye
EYE_LONG = (0.534, -0.826, 0.18)
EYE_R = 0.14                        # the eyeball
EYE_CORNEA = 0.008                  # the bulge over the iris
EYE_CORNEA_DEG = 26.0
EYE_GAP = 0.004                     # between the cornea's top and the lids
EYE_OPEN = (0.104, 0.037, 0.050)    # half length, upper half height, lower half height
# The patch of real loops ends on this oval: half length, then the half
# heights above and below. Above, it stops under the brow ridge's overhang.
EYE_OUTER = (0.165, 0.070, 0.112)
EYE_RINGS = (0.0, 0.10, 0.26, 0.55, 1.0)
EYE_SPOKES = 36
EYE_SOCKET_MARGIN = 0.004           # the socket is this much wider than the lid's edge, and under the lid rings
EYE_RING_MARGIN = 0.008             # the lid rings stay this far outside the lid's inner edge
EYE_FACE_MARGIN = 0.014             # other hide vertices stay this far outside, so no long edge sags into the ball
EYE_VIEW_DROP = 0.35                # the lid patch is laid out as seen from this far below the normal


# The sculpt has two fangs a side. They hang from the upper lip, outside the
# lower jaw's wall and fused to it. Below the cut sheet, what lies between
# these Y and outside the wall is fang. It stays on the upper jaw, and the
# cut leaves the sheet there to run down the seam between fang and wall.
FANG_Y = (4.262, 4.398)
HEAD_CENTRE_X = -0.0115
JAW_WALL = ((4.27, 0.1265), (4.42, 0.055))    # Y, half width of the lower jaw under the shelf
FANG_MARGIN = 0.008


def lip_z(y):
    ly = np.array([p[0] for p in LIP])
    lz = np.array([p[1] for p in LIP])
    return np.interp(y, ly, lz)


def is_fang(co):
    """A point below the lip sheet that belongs to a sculpted fang."""
    if not FANG_Y[0] < co.y < FANG_Y[1]:
        return False
    wall = np.interp(co.y, [JAW_WALL[0][0], JAW_WALL[1][0]], [JAW_WALL[0][1], JAW_WALL[1][1]])
    return abs(co.x - HEAD_CENTRE_X) > wall + FANG_MARGIN


def jaw_weight(y, s, lower):
    """How much a point follows the lower jaw. `s` is the height above the
    lip sheet. The slit ends at the corner, so both lips carry 0.5 there."""
    d = y - MOUTH_CORNER_Y
    L = float(smoothstep((d + 0.25) / 0.5))
    if lower:
        return L * float(1.0 - smoothstep((-s - 0.30) / 0.25))
    win = float(1.0 - smoothstep(s / 0.15))
    if d <= 0:
        return L * win
    return 0.5 * float(1.0 - smoothstep(d / 0.2)) * win


def zipper(a, b):
    """Triangles between two polylines that run the same way. Each entry is
    (vertex, position)."""
    tris = []
    i = j = 0
    while i < len(a) - 1 or j < len(b) - 1:
        if j == len(b) - 1:
            step_a = True
        elif i == len(a) - 1:
            step_a = False
        else:
            step_a = (a[i + 1][1] - b[j][1]).length < (a[i][1] - b[j + 1][1]).length
        if step_a:
            tris.append((a[i][0], a[i + 1][0], b[j][0]))
            i += 1
        else:
            tris.append((a[i][0], b[j + 1][0], b[j][0]))
            j += 1
    return tris


def face_geom(faces):
    geom = set(faces)
    for f in faces:
        geom.update(f.edges)
        geom.update(f.verts)
    return list(geom)


def in_head_box(co):
    return co.y > 3.2 and abs(co.x) < 0.8 and 4.0 < co.z < 5.3


def cut_lips(bm):
    """Cut the lip line open, with supporting loops beside it. Returns the
    upper lip's vertices from the left corner round the snout to the right
    corner, and the jaw weight layer."""
    jaw = bm.verts.layers.float.new("jaw")
    W = WARP_OFFSET
    # Flatten the lip sheet: in the warped space it is the plane z = -W.
    for v in bm.verts:
        if in_head_box(v.co):
            v.co.z -= float(lip_z(v.co.y)) + W

    def zone(lo_y, span):
        return [f for f in bm.faces
                if all(v.co.z < -W + span and v.co.z > -W - span and v.co.y > lo_y for v in f.verts)]

    # A loop round the muzzle at the corner, where the slit ends.
    bmesh.ops.bisect_plane(bm, geom=face_geom(zone(MOUTH_CORNER_Y - 0.15, 0.2)), dist=1e-6,
                           plane_co=(0, MOUTH_CORNER_Y, 0), plane_no=(0, 1, 0))
    for off in (0.0,) + tuple(LIP_LOOPS) + tuple(-x for x in LIP_LOOPS):
        bmesh.ops.bisect_plane(bm, geom=face_geom(zone(MOUTH_CORNER_Y - 1e-5, 0.25)), dist=1e-6,
                               plane_co=(0, 0, -W + off), plane_no=(0, 0, 1))
    bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 3])
    def face_upper(f):
        """Upper jaw: above the sheet, or a fang below it."""
        c = f.calc_center_median()
        return c.z + W > 0 or is_fang(c)

    def in_mouth(v):
        return v.co.y > MOUTH_CORNER_Y - 1e-5 and abs(v.co.z + W) < 0.3 and abs(v.co.x) < 0.8

    cut = [e for e in bm.edges
           if len(e.link_faces) == 2 and all(in_mouth(v) for v in e.verts)
           and face_upper(e.link_faces[0]) != face_upper(e.link_faces[1])]
    # Keep the one long cut that runs from corner to corner.
    comp = {}
    for e in cut:
        for v in e.verts:
            comp.setdefault(v, []).append(e)
    seen, groups = set(), []
    for e in cut:
        if e in seen:
            continue
        stack, group = [e], []
        seen.add(e)
        while stack:
            a = stack.pop()
            group.append(a)
            for v in a.verts:
                for b in comp[v]:
                    if b not in seen:
                        seen.add(b)
                        stack.append(b)
        groups.append(group)
    groups.sort(key=len, reverse=True)
    for g in groups[:6]:
        ys = [v.co.y for e in g for v in e.verts]
        xs = [v.co.x for e in g for v in e.verts]
        log("  cut", len(g), "edges, y", round(min(ys), 3), round(max(ys), 3), "x", round(min(xs), 3), round(max(xs), 3))
    cut = groups[0]
    upper_faces = {f for e in cut for f in e.link_faces if face_upper(f)}
    bmesh.ops.split_edges(bm, edges=cut)
    bm.verts.ensure_lookup_table()
    bm.edges.ensure_lookup_table()

    slit = [e for e in bm.edges if e.is_boundary]
    upper = [e for e in slit if e.link_faces[0] in upper_faces]
    lower = [e for e in slit if e.link_faces[0] not in upper_faces]
    log("slit edges", len(slit), "upper", len(upper), "lower", len(lower))
    lower_verts = {v for e in lower for v in e.verts}
    upper_verts = {v for e in upper for v in e.verts}
    corners = lower_verts & upper_verts
    lower_verts -= corners

    # Jaw weights, still in the warped space.
    for v in bm.verts:
        z = v.co.z + W
        if not (abs(z) < 1.2 and v.co.y > 3.0 and abs(v.co.x) < 0.8):
            continue
        if v in lower_verts:
            low_side = True
        elif v in upper_verts:
            low_side = False
        else:
            low_side = z < 0 and not is_fang(v.co)
        v[jaw] = jaw_weight(v.co.y, max(z, 0.0) if (z < 0 and not low_side) else z, low_side)

    # Order the upper chain.
    link = {}
    for e in upper:
        a, b = e.verts
        link.setdefault(a, []).append(b)
        link.setdefault(b, []).append(a)
    ends = [v for v, n in link.items() if len(n) == 1]
    if len(ends) != 2:
        raise HeroError(f"the upper lip is not one open chain: {len(ends)} ends")
    start = min(ends, key=lambda v: v.co.x)
    chain, prev = [start], None
    while True:
        nxt = [n for n in link[chain[-1]] if n is not prev]
        if not nxt:
            break
        prev = chain[-1]
        chain.append(nxt[0])
    if len(chain) != len(link):
        rest = [v for v in link if v not in set(chain)]
        for v in rest[:60]:
            log("  stray", v.co.to_tuple(3), len(link[v]))
        raise HeroError(f"the upper lip chain has {len(chain)} of {len(link)} vertices")
    low_link = {}
    for e in lower:
        a, b = e.verts
        low_link.setdefault(a, []).append(b)
        low_link.setdefault(b, []).append(a)
    low_chain, prev = [start], None
    while True:
        nxt = [n for n in low_link[low_chain[-1]] if n is not prev]
        if not nxt:
            break
        prev = low_chain[-1]
        low_chain.append(nxt[0])

    # Where the cut ran down a fang's seam, the fang has an open back and the
    # jaw wall has a notch of the same outline. Close both with a face each.
    # The lips then run straight past, and the mouth interior starts there.
    twin = {}
    for v in lower_verts | corners:
        twin[tuple(round(c, 5) for c in v.co)] = v

    def close_dips(seq):
        out, caps, i = [seq[0]], 0, 1
        while i < len(seq):
            if i < len(seq) - 1 and seq[i].co.z + W < -0.004:
                j = i
                while j < len(seq) - 1 and seq[j].co.z + W < -0.004:
                    j += 1
                poly = seq[i - 1:j + 1]
                made = []
                for group in (poly, [twin.get(tuple(round(c, 5) for c in v.co)) for v in poly]):
                    if len(group) >= 3 and all(v is not None for v in group) and len(set(group)) == len(group):
                        try:
                            made.append(bm.faces.new(group))
                        except ValueError:
                            pass
                bmesh.ops.triangulate(bm, faces=made)
                caps += 1
                out.append(seq[j])
                i = j + 1
            else:
                out.append(seq[i])
                i += 1
        return out, caps

    chain, n_caps = close_dips(chain)
    log("fang seams closed:", n_caps, "lip chain now", len(chain))

    for v in bm.verts:
        if v.co.z < -W / 2:
            v.co.z += W
            v.co.z += float(lip_z(v.co.y))
    bmesh.ops.dissolve_degenerate(bm, dist=1e-5, edges=bm.edges)
    chain = [v for v in chain if v.is_valid]
    low_chain = [v for v in low_chain if v.is_valid]
    return chain, low_chain, jaw


def eye_frame(side):
    n = Vector((EYE_NORMAL[0] * side, EYE_NORMAL[1], EYE_NORMAL[2])).normalized()
    a1 = Vector((EYE_LONG[0] * side, EYE_LONG[1], EYE_LONG[2]))
    a1 = (a1 - n * a1.dot(n)).normalized()
    a2 = n.cross(a1)
    if a2.z < 0:
        a2 = -a2
    s0 = Vector(EYE_SURFACE[side])
    return s0, n, a1, a2, s0 - n * EYE_R


def eye_opening(theta, scale=1.0):
    a, bu, bl = EYE_OPEN
    x, y = math.cos(theta), math.sin(theta)
    b = bu if y > 0 else bl
    p = 1.75
    return scale / (abs(x / a) ** p + abs(y / b) ** p) ** (1 / p)


def eye_patch(bm, bvh, side, head_slot):
    """Replace the hide round one eye with rings of real loops and an open
    lid. Nothing of the hide stays inside the eyeball's reach."""
    s0, n, a1, a2, c = eye_frame(side)
    gaze = n.copy()
    clear = EYE_R + EYE_CORNEA + EYE_GAP
    A = EYE_OUTER[0]

    def oval(u, w):
        return (u / A) ** 2 + (w / (EYE_OUTER[1] if w > 0 else EYE_OUTER[2])) ** 2

    def plane(co):
        d = co - s0
        return d.dot(a1), d.dot(a2), d.dot(n)

    # The brow ridge hangs over the eye. Seen from a little below the eye's
    # own normal, the lid is in view up to the fold under the ridge. The
    # patch replaces what that view shows inside the oval, and nothing behind.
    n = (n - a2 * EYE_VIEW_DROP).normalized()
    a2 = n.cross(a1)
    if a2.z < 0:
        a2 = -a2
    bm.faces.ensure_lookup_table()
    view = BVHTree.FromBMesh(bm)
    hit_faces = set()
    steps = 150
    for iu in range(-steps, steps + 1):
        for iw in range(-steps, steps + 1):
            u, w = A * iu / steps, EYE_OUTER[2] * iw / steps
            if oval(u, w) >= 0.94:
                continue
            hit = view.ray_cast(s0 + a1 * u + a2 * w + n * 0.3, -n)
            if hit[0] is not None:
                hit_faces.add(hit[2])
    doomed = [bm.faces[i] for i in hit_faces]
    bvh = view
    touched = {v for f in doomed for v in f.verts}
    bmesh.ops.delete(bm, geom=doomed, context="FACES")

    def near(v):
        return v in touched and v.is_valid

    # A vertex where the rim touches itself would make two loops. Widen the
    # hole there until the rim is one simple loop.
    for _ in range(12):
        touched = {v for v in touched if v.is_valid}
        edge = [e for e in bm.edges if e.is_boundary and all(v in touched for v in e.verts)]
        link = {}
        for e in edge:
            a, b = e.verts
            link.setdefault(a, []).append(b)
            link.setdefault(b, []).append(a)
        bad = [v for v, x in link.items() if len(x) != 2]
        if not bad:
            # more than one loop: keep the longest, open up the others
            left, loops = set(link), []
            while left:
                a = left.pop()
                group, stack = [a], [a]
                while stack:
                    for b in link[stack.pop()]:
                        if b in left:
                            left.discard(b)
                            group.append(b)
                            stack.append(b)
                loops.append(group)
            # the rim is the loop that spans the most of the oval
            def span(g):
                us = [plane(v.co)[0] for v in g]
                ws = [plane(v.co)[1] for v in g]
                return (max(us) - min(us)) * (max(ws) - min(ws))
            loops.sort(key=span, reverse=True)
            bad = [v for g in loops[1:] for v in g]
        if not bad:
            break
        log("   rim pinch", len(bad), [(tuple(round(c, 3) for c in plane(v.co)), len(link[v])) for v in bad[:4]])
        more = {f for v in bad for f in v.link_faces}
        touched.update(v for f in more for v in f.verts)
        doomed += list(more)
        bmesh.ops.delete(bm, geom=list(more), context="FACES")
    loose = [v for v in bm.verts if not v.link_faces]
    touched.difference_update(loose)
    bmesh.ops.delete(bm, geom=loose, context="VERTS")
    if any(len(x) != 2 for x in link.values()):
        raise HeroError(f"eye {side}: the hole's rim is not one loop")
    start = next(iter(link))
    loop, prev = [start], None
    while True:
        nxt = [x for x in link[loop[-1]] if x is not prev]
        prev = loop[-1]
        if nxt[0] is start:
            break
        loop.append(nxt[0])
    if len(loop) != len(link):
        raise HeroError(f"eye {side}: the hole's rim has {len(link) - len(loop)} stray vertices")
    ang = [math.atan2(plane(v.co)[1], plane(v.co)[0]) for v in loop]
    area = sum(plane(loop[i].co)[0] * plane(loop[(i + 1) % len(loop)].co)[1]
               - plane(loop[(i + 1) % len(loop)].co)[0] * plane(loop[i].co)[1] for i in range(len(loop)))
    if area < 0:
        loop.reverse()
        ang.reverse()
    k = min(range(len(loop)), key=lambda i: abs(ang[i]))
    loop = loop[k:] + loop[:k]

    rings = []
    thetas = [2 * math.pi * i / EYE_SPOKES for i in range(EYE_SPOKES)]
    # The lid's inner edge, on the sphere the eyeball can never reach.
    inner = []
    for th in thetas:
        r = eye_opening(th, 0.90)
        p = s0 + a1 * (r * math.cos(th)) + a2 * (r * math.sin(th))
        inner.append(bm.verts.new(c + (p - c).normalized() * clear))
    rings.append(inner)
    for t in EYE_RINGS:
        ring = []
        for th in thetas:
            ro = 1.0 / math.sqrt(oval(math.cos(th), math.sin(th)))
            r = eye_opening(th) * (1 - t) + ro * t
            p = s0 + a1 * (r * math.cos(th)) + a2 * (r * math.sin(th))
            hit = bvh.ray_cast(p + n * 0.3, -n)
            q = hit[0] if hit[0] is not None else p
            if (q - c).length < clear + EYE_RING_MARGIN:
                q = c + (q - c).normalized() * (clear + EYE_RING_MARGIN)
            ring.append(bm.verts.new(q))
        rings.append(ring)
    # Take the steps out of each ring: a ray can land on a scale's far side.
    for ring in rings[1:-1]:
        for _ in range(2):
            pos = [v.co.copy() for v in ring]
            for i, v in enumerate(ring):
                q = pos[i] * 0.5 + (pos[i - 1] + pos[(i + 1) % len(ring)]) * 0.25
                if (q - c).length < clear + EYE_RING_MARGIN:
                    q = c + (q - c).normalized() * (clear + EYE_RING_MARGIN)
                v.co = q
    new_faces = []
    # The socket: a cup behind the lid's edge that wraps the ball. Without
    # it a grazing view between lid and ball looks into the hollow head.
    socket = []
    prev = rings[0]
    for t in (0.07, 0.15, 0.25, 0.37, 0.5, 0.65, 0.82):
        ring = [bm.verts.new(c + (v.co - c).normalized().slerp(-gaze, t) * (clear + EYE_SOCKET_MARGIN))
                for v in rings[0]]
        for i in range(EYE_SPOKES):
            j = (i + 1) % EYE_SPOKES
            new_faces.append(bm.faces.new((prev[i], ring[j], prev[j])))
            new_faces.append(bm.faces.new((prev[i], ring[i], ring[j])))
        socket += ring
        prev = ring
    pole = bm.verts.new(c - gaze * (clear + EYE_SOCKET_MARGIN))
    socket.append(pole)
    for i in range(EYE_SPOKES):
        new_faces.append(bm.faces.new((prev[i], pole, prev[(i + 1) % EYE_SPOKES])))
    for a, b in zip(rings[:-1], rings[1:]):
        for i in range(EYE_SPOKES):
            j = (i + 1) % EYE_SPOKES
            new_faces.append(bm.faces.new((a[i], a[j], b[j])))
            new_faces.append(bm.faces.new((a[i], b[j], b[i])))
    # Join the outer ring to the hole's rim. Both run round the eye, so the
    # join walks them by angle. Walking by distance can run away along one
    # of them and fan triangles across the eye.
    outer = list(rings[-1]) + [rings[-1][0]]
    rim = loop + [loop[0]]
    ta = thetas + [2 * math.pi]
    prev_ang = math.atan2(plane(loop[0].co)[1], plane(loop[0].co)[0])
    turned = prev_ang                 # the ring starts at angle 0, the rim near it
    tb = [turned]
    for v in loop[1:]:
        a_ = math.atan2(plane(v.co)[1], plane(v.co)[0])
        step = (a_ - prev_ang + math.pi) % (2 * math.pi) - math.pi
        turned += step
        prev_ang = a_
        tb.append(max(tb[-1], turned))
    tb.append(max(tb[-1], 2 * math.pi))
    i = j = 0
    while i < len(outer) - 1 or j < len(rim) - 1:
        if j == len(rim) - 1 or (i < len(outer) - 1 and ta[i + 1] <= tb[j + 1]):
            tri = (outer[i], rim[j], outer[i + 1])
            i += 1
        else:
            tri = (outer[i], rim[j], rim[j + 1])
            j += 1
        if len(set(tri)) == 3:
            new_faces.append(bm.faces.new(tri))
    for f in new_faces:
        f.material_index = head_slot
        f.smooth = True
    pushed = 0
    ringset = {v for r in rings for v in r} | set(socket)
    for v in bm.verts:
        d = v.co - c
        if d.length < clear + EYE_FACE_MARGIN and v not in ringset:
            v.co = c + d.normalized() * (clear + EYE_FACE_MARGIN)
            pushed += 1
    # A long triangle can cut through the ball's reach with all three corners
    # outside it. Lift the corners of any such face until its nearest point
    # is clear. The lid's inner edge is already where it must be.
    from mathutils.geometry import closest_point_on_tri
    inner_set = set(rings[0]) | set(socket)
    lifted = 0
    for _ in range(3):
        again = False
        for f in bm.faces:
            if len(f.verts) != 3 or min((v.co - c).length for v in f.verts) > clear + 0.15:
                continue
            p = closest_point_on_tri(c, f.verts[0].co, f.verts[1].co, f.verts[2].co)
            # a face on the inner edge touches the limit by design
            limit = clear - 0.0012 if any(v in inner_set for v in f.verts) else clear + 0.0015
            if (p - c).length < limit:
                if lifted < 6:
                    log("   lift", [(round(((v.co - c).length - clear) * 1000, 1),
                                     "inner" if v in inner_set else ("ring" if v in ringset else "hide"))
                                    for v in f.verts], "closest", round(((p - c).length - clear) * 1000, 2),
                        "edges mm", [round(e.calc_length() * 1000) for e in f.edges])
                for v in f.verts:
                    if v not in inner_set:
                        v.co = c + (v.co - c).normalized() * ((v.co - c).length + 0.004)
                again = True
                lifted += 1
        if not again:
            break
    nearest = min((closest_point_on_tri(c, f.verts[0].co, f.verts[1].co, f.verts[2].co) - c).length
                  for f in bm.faces if len(f.verts) == 3 and min((v.co - c).length for v in f.verts) < clear + 0.15)
    log(f"eye {side}: removed {len(doomed)} faces, rim {len(loop)}, pushed {pushed} hide vertices off the ball, "
        f"lifted {lifted} faces; the hide's nearest point is {(nearest - EYE_R - EYE_CORNEA) * 1000:.1f} mm "
        f"outside the cornea's reach")
    return [[v for v in r] for r in rings]


def make_eyeball(name, side):
    s0, n, a1, a2, c = eye_frame(side)
    up = Vector((0, 0, 1))
    e2 = (up - n * up.dot(n)).normalized()
    e1 = e2.cross(n)
    alphas = [0, 4, 8, 12, 16, 20, 24, 28, 34, 42, 52, 64, 78, 95, 115, 140, 162, 180]
    seg = 32
    verts, uvs = [], []
    index = {}
    for j, al in enumerate(alphas):
        a = math.radians(al)
        r = EYE_R
        if al < EYE_CORNEA_DEG:
            r += EYE_CORNEA * math.cos(math.pi / 2 * al / EYE_CORNEA_DEG) ** 2
        rho = 0.47 * al / 78.0 if al <= 78 else 0.47 + 0.025 * (al - 78) / 102.0
        count = 1 if al in (0, 180) else seg
        for i in range(count):
            ph = 2 * math.pi * i / seg
            d = n * math.cos(a) + (e1 * math.cos(ph) + e2 * math.sin(ph)) * math.sin(a)
            index[(j, i)] = len(verts)
            verts.append(c + d * r)
            uvs.append((0.5 + rho * math.cos(ph), 0.5 + rho * math.sin(ph)))
    faces = []
    last = len(alphas) - 1
    for j in range(last):
        for i in range(seg):
            k = (i + 1) % seg
            if j == 0:
                faces.append((index[(0, 0)], index[(1, i)], index[(1, k)]))
            elif j == last - 1:
                faces.append((index[(j, i)], index[(last, 0)], index[(j, k)]))
            else:
                faces.append((index[(j, i)], index[(j + 1, i)], index[(j + 1, k)]))
                faces.append((index[(j, i)], index[(j + 1, k)], index[(j, k)]))
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in verts], [], faces)
    layer = me.uv_layers.new(name="UVMap")
    for loop in me.loops:
        layer.data[loop.index].uv = uvs[loop.vertex_index]
    for p in me.polygons:
        p.use_smooth = True
    obj = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(obj)
    # outward normals
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()
    return obj


MOUTH_ROWS = (0.10, 0.30, 0.58)     # how far each row of the gums lies from the lip toward the middle
PALATE_MAX, FLOOR_MAX = 0.085, 0.055
KIND_GUM, KIND_TONGUE, KIND_TOOTH = 0.0, 1.0, 2.0


def build_mouth(chain_co, bvh):
    """The mouth interior as one mesh: the upper and lower gums are sheets
    that start on the lip's own vertices and dome into the head, a throat
    closes the back, and the teeth and the tongue sit inside."""
    n = len(chain_co)
    ia = max(range(n), key=lambda i: chain_co[i].y)
    apex_y = chain_co[ia].y
    xm = (chain_co[0].x + chain_co[-1].x) / 2
    verts, faces, attrs = [], [], []     # attrs: (kind, depth, lower, weight_y)

    def add(p, kind, depth, lower, wy=None):
        verts.append(Vector(p))
        attrs.append((kind, depth, lower, p[1] if wy is None else wy))
        return len(verts) - 1

    def medial(p):
        return Vector((xm, min(p.y, apex_y - 0.06), float(lip_z(p.y))))

    def room(p, direction):
        o = p + (medial(p) - p) * 0.6
        hit = bvh.ray_cast(o, Vector((0, 0, direction)))
        return hit[3] if hit[0] is not None else 0.04

    def prof(u):
        return math.sin(math.pi / 2 * min(1.0, u * 1.45))

    def inside(p, margin):
        loc, nor, _, dist = bvh.find_nearest(p)
        return (p - loc).dot(nor) < 0 and dist > margin

    def gum_point(p, u, sign, height):
        """A point of the gum sheet, kept inside the closed head."""
        to_mid = medial(p) - p
        for _ in range(5):
            flat = p + to_mid * u
            if inside(flat, 0.004):
                break
            u = min(0.9, u + 0.08)
        lift = height * prof(u)
        q = flat + Vector((0, 0, sign * lift))
        for _ in range(6):
            if inside(q, 0.006):
                break
            lift *= 0.6
            q = flat + Vector((0, 0, sign * lift))
        return q

    if not inside(Vector((0.0, 3.9, 4.7)), 0.0) or inside(Vector((0.0, 3.9, 5.6)), 0.0):
        raise HeroError("the inside test reads the head's normals the wrong way round")
    halves = {}
    for sign, cap, lower in ((1.0, PALATE_MAX, False), (-1.0, FLOOR_MAX, True)):
        h = [min(cap, 0.5 * room(p, sign)) for p in chain_co]
        # smooth the heights along the lip
        for _ in range(6):
            h = [h[max(i - 1, 0)] * 0.25 + h[i] * 0.5 + h[min(i + 1, n - 1)] * 0.25 for i in range(n)]
        rows = [[add(p, KIND_GUM, 0.0, lower) for p in chain_co]]
        for u in MOUTH_ROWS:
            row = []
            for i, p in enumerate(chain_co):
                t = 1.0 - abs(i - ia) / max(ia, n - 1 - ia)
                q = gum_point(p, u, sign, h[i])
                row.append(add(q, KIND_GUM, 0.10 + 0.5 * u + 0.25 * u * (1 - t), lower))
            rows.append(row)
        for a, b in zip(rows[:-1], rows[1:]):
            for i in range(n - 1):
                faces.append((a[i], a[i + 1], b[i + 1], sign))
                faces.append((a[i], b[i + 1], b[i], sign))
        inner = rows[-1]
        left = [(inner[i], verts[inner[i]]) for i in range(0, ia + 1)]
        right = [(inner[i], verts[inner[i]]) for i in range(n - 1, ia, -1)]
        for tri in zipper(left, right):
            faces.append((tri[0], tri[1], tri[2], sign))
        border = [r[0] for r in rows] + [r[-1] for r in reversed(rows)]
        halves[lower] = dict(rows=rows, h=h, border=border)

    # The throat: the lens between the two rear borders, drawn back and shut.
    ring0 = halves[False]["border"] + list(reversed(halves[True]["border"]))
    lower_flags = [False] * len(halves[False]["border"]) + [True] * len(halves[True]["border"])
    c0 = sum((verts[i] for i in ring0), Vector()) / len(ring0)
    prev = ring0
    for m, (sc, back) in enumerate(((0.9, 0.09), (0.7, 0.19), (0.42, 0.28))):
        cm = c0 + Vector((0, -back, 0.015 * (m + 1)))
        ring = [add(cm + (verts[i] - c0) * sc, KIND_GUM, 0.82 + 0.06 * m, lw)
                for i, lw in zip(ring0, lower_flags)]
        for k in range(len(ring)):
            k2 = (k + 1) % len(ring)
            faces.append((prev[k], prev[k2], ring[k2], 0.0))
            faces.append((prev[k], ring[k2], ring[k], 0.0))
        prev = ring
    centre = add(c0 + Vector((0, -0.33, 0.05)), KIND_GUM, 1.0, False)
    for k in range(len(prev)):
        faces.append((prev[k], prev[(k + 1) % len(prev)], centre, 0.0))
    throat_centre = c0 + Vector((0, -0.15, 0.02))

    # Teeth: cones along the first row of each gum.
    def tooth(base, direction, length, radius, lower, lean):
        d = Vector(direction).normalized()
        side_a = d.cross(Vector((0, 1, 0)))
        if side_a.length < 1e-3:
            side_a = Vector((1, 0, 0))
        side_a.normalize()
        side_b = d.cross(side_a)
        ringsv = []
        for frac, rr in ((-0.25, 1.0), (0.0, 1.0), (0.45, 0.72), (0.8, 0.38)):
            pts = []
            for k in range(6):
                a = math.pi / 3 * k
                p = base + d * (length * frac) + lean * (length * max(frac, 0) ** 2 * 0.35) \
                    + (side_a * math.cos(a) + side_b * math.sin(a)) * radius * rr
                pts.append(add(p, KIND_TOOTH, max(frac, 0.0), lower, base.y))
            ringsv.append(pts)
        tip = add(base + d * length + lean * (length * 0.35), KIND_TOOTH, 1.0, lower, base.y)
        for a, b in zip(ringsv[:-1], ringsv[1:]):
            for k in range(6):
                k2 = (k + 1) % 6
                faces.append((a[k], a[k2], b[k2], 9.0))
                faces.append((a[k], b[k2], b[k], 9.0))
        for k in range(6):
            faces.append((ringsv[-1][k], ringsv[-1][(k + 1) % 6], tip, 9.0))

    def fits(base, d, length, lean):
        return all(inside(base + Vector(d) * (length * f) + lean * (length * f * f * 0.35), 0.002 + 0.006 * (1 - f))
                   for f in (0.5, 0.8, 1.0))

    n_teeth = 0
    lengths = []
    for lower in (False, True):
        pts = chain_co
        sign = -1.0 if lower else 1.0
        acc = [0.0]
        for a, b in zip(pts[:-1], pts[1:]):
            acc.append(acc[-1] + (b - a).length)
        total = acc[-1]
        spacing = 0.052
        count = int((total - 0.14) / spacing)
        for k in range(count):
            s = 0.07 + (k + (0.5 if lower else 0.0)) * (total - 0.14) / count
            if s > total - 0.07:
                continue
            i = max(1, min(n - 1, int(np.searchsorted(acc, s))))
            f = (s - acc[i - 1]) / max(acc[i] - acc[i - 1], 1e-9)
            p = pts[i - 1].lerp(pts[i], f)
            t = 1.0 - abs(s / total - 0.5) * 2.0      # 0 at the corners, 1 at the snout
            inward = (medial(p) - p)
            inward.z = 0
            reach = max(inward.length, 1e-6)
            inward.normalize()
            # the gum line: 30 mm inside the lip, on the gum sheet
            p = gum_point(p, min(0.55, (0.036 if lower else 0.028) / reach), sign, halves[lower]["h"][i])
            jitter = 0.85 + 0.3 * ((k * 7919) % 13) / 13.0
            if lower:
                length = (0.024 + 0.026 * t) * jitter
                base, d, radius = p, (0, 0, 1), 0.009 + 0.004 * t
            else:
                length = (0.036 + 0.036 * t) * jitter
                base, d, radius = p, (0, 0, -1), 0.011 + 0.005 * t
            lean = inward * 0.3
            for _ in range(8):              # a tooth must not show through the closed jaw
                if fits(base, d, length, lean):
                    break
                length *= 0.85
                base = base + inward * 0.004
                lean = inward * min(1.2, lean.length + 0.15)
            else:
                continue
            tooth(base, d, length, min(radius, length * 0.33), lower, lean)
            lengths.append(length)
            n_teeth += 1

    # The tongue lies on the floor.
    lo = halves[True]
    y0, y1 = MOUTH_CORNER_Y + 0.02, apex_y - 0.10
    stations = 12
    prev = None
    left_i = list(range(0, ia + 1))
    ys = [chain_co[i].y for i in left_i]
    for k in range(stations + 1):
        f = k / stations
        y = y0 + (y1 - y0) * f
        i = left_i[int(np.argmin([abs(v - y) for v in ys]))]
        half = abs(chain_co[i].x - xm)
        wdt = half * 0.55 * (1.0 - 0.75 * f ** 3)
        thick = 0.014 * (1.0 - 0.6 * f ** 3)
        zc = float(lip_z(chain_co[i].y)) - lo["h"][i] * 0.75 + thick
        if k == stations:
            wdt *= 0.35
        ring = []
        for m in range(8):
            a = 2 * math.pi * m / 8
            groove = 0.004 * (1 - f) if m == 2 else 0.0
            ring.append(add((xm + wdt * math.cos(a), y, zc + thick * math.sin(a) - groove),
                            KIND_TONGUE, f, True))
        if prev is None:
            cidx = add((xm, y - 0.02, zc), KIND_TONGUE, 0.0, True)
            for m in range(8):
                faces.append((ring[m], cidx, ring[(m + 1) % 8], 9.0))
        else:
            for m in range(8):
                m2 = (m + 1) % 8
                faces.append((prev[m], prev[m2], ring[m2], 9.0))
                faces.append((prev[m], ring[m2], ring[m], 9.0))
        prev = ring
    tip = add((xm, y1 + 0.02, zc), KIND_TONGUE, 1.0, True)
    for m in range(8):
        faces.append((prev[m], prev[(m + 1) % 8], tip, 9.0))

    me = bpy.data.meshes.new("mouth")
    # The palate's top comes within a few millimetres of an eyeball's reach, and which side of
    # the limit it lands on changes with the neck's triangles. Push it clear.
    reach = EYE_R + EYE_CORNEA + 0.003
    pushed = 0
    for side in (1, -1):
        c = eye_frame(side)[4]
        for i, v in enumerate(verts):
            d = Vector(v) - c
            if d.length < reach:
                verts[i] = tuple(c + d.normalized() * reach)
                pushed += 1
    log("mouth vertices pushed clear of the eyeballs:", pushed)
    me.from_pydata([tuple(v) for v in verts], [], [f[:3] for f in faces])
    obj = bpy.data.objects.new("mouth", me)
    bpy.context.scene.collection.objects.link(obj)
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.faces.ensure_lookup_table()
    # Closed parts (teeth, tongue) face outward. The sheets face the cavity.
    solid = [bm.faces[i] for i, f in enumerate(faces) if f[3] == 9.0]
    bmesh.ops.recalc_face_normals(bm, faces=solid)
    for i, f in enumerate(faces):
        face = bm.faces[i]
        sign = f[3]
        if sign in (1.0, -1.0):
            if face.normal.z * sign > 0:       # the palate looks down, the floor up
                face.normal_flip()
        elif sign == 0.0:
            ctr = face.calc_center_median()
            if face.normal.dot(throat_centre - ctr) < 0:
                face.normal_flip()
        face.smooth = True
    bm.to_mesh(me)
    bm.free()
    for name, col in (("kind", 0), ("depth", 1), ("jaw", None)):
        a = me.attributes.new(name, "FLOAT", "POINT")
        if col is not None:
            a.data.foreach_set("value", [x[col] for x in attrs])
        else:
            a.data.foreach_set("value", [jaw_weight(x[3], 0.0, x[2]) for x in attrs])
    log("mouth: verts", len(verts), "tris", len(faces), "teeth", n_teeth,
        "tooth length mm", round(min(lengths) * 1000), "to", round(max(lengths) * 1000))
    return obj


def open_jaw(obj, degrees=JAW_OPEN_DEG):
    """Rotate the lower jaw open by the stored weights. For renders and the
    verifier only: the file ships closed."""
    co = coords(obj)
    w = np.empty(len(co), dtype=np.float32)
    obj.data.attributes["jaw"].data.foreach_get("value", w)
    a = -np.radians(degrees) * w
    y = co[:, 1] - JAW_HINGE[0]
    z = co[:, 2] - JAW_HINGE[1]
    co[:, 1] = JAW_HINGE[0] + y * np.cos(a) - z * np.sin(a)
    co[:, 2] = JAW_HINGE[1] + y * np.sin(a) + z * np.cos(a)
    set_coords(obj, co)


def stage_head():
    """Lips, eyelids, eyeballs and the mouth interior."""
    open_blend("joined.blend")
    low = bpy.data.objects["low"]
    bvh = bvh_of(low)
    head_slot = [m.name for m in low.data.materials].index("from_head")
    bm = bmesh.new()
    bm.from_mesh(low.data)
    chain, low_chain, jaw = cut_lips(bm)
    chain_co = [v.co.copy() for v in chain]
    log("lip chain", len(chain), "from", chain_co[0].to_tuple(3), "to", chain_co[-1].to_tuple(3),
        "lower chain", len(low_chain))
    for side in (1, -1):
        eye_patch(bm, bvh, side, head_slot)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    for f in bm.faces:
        f.smooth = True
    bm.to_mesh(low.data)
    bm.free()
    topo_report(low, "hide with lips and lids")
    make_eyeball("eye_r", 1)
    make_eyeball("eye_l", -1)
    build_mouth(chain_co, bvh)
    save_blend("head.blend")


# ----------------------------------------------------------------- stage: parts

# Joint loops. Each joint gets three parallel edge loops, cut with planes
# across the limb. Positions were read off orthographic renders.
# Wing joints: (name, X, Y, direction X, direction Y, radius), right wing; the
# left wing mirrors X. The planes are vertical.
WING_JOINTS_V2 = [
    ("wing_shoulder", 1.25, 1.10, 0.90, -0.45, 0.75),
    ("wing_elbow", 2.64, 0.45, 1.00, 0.00, 0.60),
    ("wing_wrist", 3.43, 0.71, 1.00, 0.00, 0.60),
    ("finger1_a", 6.31, 0.45, 0.97, -0.20, 0.24),
    ("finger1_b", 7.95, -0.07, 0.92, -0.38, 0.22),
    ("finger2_a", 5.85, -0.37, 0.90, -0.44, 0.22),
    ("finger3_a", 4.74, -0.63, 0.60, -0.80, 0.22),
    ("finger4_a", 2.74, -0.43, 0.10, -1.00, 0.22),
]
# With the version 3 wing the loops are at the rig's joints.
WING_JOINTS_V3 = [
    ("wing_shoulder", 1.15, 0.88, 0.87, -0.50, 0.75),
    ("wing_elbow", 2.62, 0.03, 0.98, -0.20, 0.60),
    ("wing_wrist", 3.43, 0.58, 0.83, 0.56, 0.60),
    ("finger1_a", 6.31, 0.40, 0.98, -0.18, 0.24),
    ("finger1_b", 7.95, -0.07, 0.90, -0.43, 0.22),
    ("finger2_a", 5.85, -0.37, 0.92, -0.40, 0.22),
    ("finger3_a", 4.74, -0.63, 0.68, -0.73, 0.22),
    ("finger4_a", 2.76, -0.43, 0.20, -0.98, 0.22),
]
WING_JOINTS = WING_JOINTS_V3 if (WING_V3 or WING_V5) else WING_JOINTS_V2
# Leg joints: (name, Y, Z, direction Y, direction Z, radius). The planes
# contain the X axis. Both sides are cut.
LEG_JOINTS_A = [
    ("hind_hip", -0.50, 2.30, 0.30, -1.00, 0.75),
    ("hind_knee", -0.33, 1.85, -0.50, -0.87, 0.60),
    ("hind_hock", -0.98, 1.10, -0.50, -0.86, 0.45),
    ("hind_foot", -0.78, 0.45, 0.00, -1.00, 0.45),
    ("fore_shoulder", 1.85, 2.30, -0.20, -1.00, 0.65),
    ("fore_elbow", 1.82, 1.90, 0.10, -1.00, 0.50),
    ("fore_wrist", 2.04, 0.95, 0.30, -1.00, 0.42),
    ("fore_foot", 2.20, 0.40, 0.00, -1.00, 0.45),
]
# Body B's legs are bent, so each plane's normal bisects the two segments that meet at the joint.
# Centres: section midpoints of the forearm, the paw and the metatarsus; the elbow, the knee and
# the hock read off side silhouettes of the placed sculpt (tools/hero-rig.md has the table).
LEG_JOINTS_B = [
    ("hind_hip", -0.60, 2.90, 0.26, -0.96, 0.75),
    ("hind_knee", -0.35, 1.80, -0.42, -0.91, 0.60),
    ("hind_hock", -1.40, 1.25, -0.43, -0.90, 0.45),
    ("hind_foot", -1.20, 0.35, 0.78, -0.63, 0.45),
    ("fore_shoulder", 2.35, 2.75, -0.66, -0.75, 0.65),
    ("fore_elbow", 1.70, 1.95, -0.14, -0.99, 0.50),
    ("fore_wrist", 2.20, 0.80, 0.50, -0.86, 0.42),
    ("fore_foot", 2.55, 0.28, 0.89, -0.46, 0.45),
]
LEG_JOINTS = LEG_JOINTS_B if BODY_B else LEG_JOINTS_A
# The first build's leg joints, on the first body sculpt. The record's table compares them.
LEG_JOINTS_V1 = {"hind_hip": (-0.05, 2.25), "hind_knee": (0.00, 1.83), "hind_hock": (-0.96, 1.43),
                 "hind_foot": (-0.84, 0.45), "fore_shoulder": (1.84, 2.30), "fore_elbow": (1.72, 1.92),
                 "fore_wrist": (2.04, 0.98), "fore_foot": (2.25, 0.40)}
# Y ranges of the fore and hind claws, under TOE_CLAW_Z. Body B's paws reach further forward.
TOE_CLAWS = ((2.98, 9.0), (-0.62, 0.25)) if BODY_B else ((2.50, 9.0), (-0.42, 0.25))
TOE_CLAW_Z = 0.40 if BODY_B else 0.36
JOINT_LOOP_SPACING = 0.10
MEMBRANE_MAX_THICKNESS = 0.135      # the sculpt's membrane is 70 to 130 mm thick, its bones 150 mm and up
MEMBRANE_MAX_EDGE = 0.60 if WING_V3 else 0.45    # longer wing edges are split in two
MEMBRANE_THIN = 0.30                # each side of the membrane moves in by this share of the thickness
WING_MIN_X = 1.3
# Claws by position: (X, Y, radius) of the wing's claws, right wing.
WING_CLAWS = [(9.42, -0.98, 0.26), (8.14, -1.45, 0.22), (5.71, -1.76, 0.22), (2.83, -1.30, 0.22), (3.12, 0.92, 0.30)]
HORN_GROW_SATURATION = 0.66
HORN_SATURATION = 0.45              # the sculpt's horns are grey-brown, its frill is red-brown


def source_colours(obj):
    """The base colour of a sculpt at each of its vertices."""
    me = obj.data
    img = next(n.image for n in me.materials[0].node_tree.nodes
               if n.type == "TEX_IMAGE" and n.outputs[0].links
               and n.outputs[0].links[0].to_socket.name == "Base Color")
    px = np_from_image(img)
    h, w = px.shape[:2]
    nl = len(me.loops)
    uv = np.empty(nl * 2, np.float32)
    me.uv_layers[0].data.foreach_get("uv", uv)
    uv = uv.reshape(nl, 2)
    lv = np.empty(nl, np.int32)
    me.loops.foreach_get("vertex_index", lv)
    c = px[np.clip((uv[:, 1] * h).astype(int), 0, h - 1), np.clip((uv[:, 0] * w).astype(int), 0, w - 1), :3]
    vc = np.zeros((len(me.vertices), 3), np.float32)
    vc[lv] = c
    return vc


def cut_joint_loops(bm, skip_slot=None):
    made = 0
    for name, x, y, dx, dy, r in WING_JOINTS:
        for side in (1, -1):
            c = Vector((x * side, y, 0))
            n = Vector((dx * side, dy, 0)).normalized()
            for k in ((-0.5, 0.5) if name.startswith("finger") else (-1, 0, 1)):
                pc = c + n * (k * JOINT_LOOP_SPACING)
                faces = [f for f in bm.faces if f.material_index != skip_slot
                         and all((Vector((v.co.x, v.co.y, 0)) - c).length < r and v.co.z > 3.0 for v in f.verts)]
                if faces:
                    bmesh.ops.bisect_plane(bm, geom=face_geom(faces), dist=1e-5, plane_co=pc, plane_no=n)
                    made += 1
    for name, y, z, dy, dz, r in LEG_JOINTS:
        c = Vector((0, y, z))
        n = Vector((0, dy, dz)).normalized()
        for k in (-1, 0, 1):
            pc = c + n * (k * JOINT_LOOP_SPACING)
            faces = [f for f in bm.faces
                     if all((Vector((0, v.co.y, v.co.z)) - c).length < r and 0.25 < abs(v.co.x) < 1.75
                            for v in f.verts)]
            if faces:
                bmesh.ops.bisect_plane(bm, geom=face_geom(faces), dist=1e-5, plane_co=pc, plane_no=n)
                made += 1
    bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 3])
    log("joint loops cut:", made, "planes, faces now", len(bm.faces))


def majority(bm, flag, passes, region=None):
    """Smooth a per-face flag: a face takes the value of most of its neighbours."""
    for _ in range(passes):
        new = dict(flag)
        for f in (region if region is not None else bm.faces):
            near = [flag[e_f] for e in f.edges for e_f in e.link_faces if e_f is not f]
            if near and sum(near) * 2 > len(near):
                new[f] = True
            elif near and sum(near) == 0:
                new[f] = False
        flag = new
    return flag


def drop_small(bm, flag, min_faces):
    """Clear islands of flagged faces smaller than min_faces."""
    seen = set()
    for f in bm.faces:
        if not flag[f] or f in seen:
            continue
        island, stack = [f], [f]
        seen.add(f)
        while stack:
            a = stack.pop()
            for e in a.edges:
                for b in e.link_faces:
                    if flag[b] and b not in seen:
                        seen.add(b)
                        island.append(b)
                        stack.append(b)
        if len(island) < min_faces:
            for a in island:
                flag[a] = False
    return flag


def stage_parts():
    """Joint loops, then split the hide into hide, membrane and horn."""
    open_blend("head.blend")
    low = bpy.data.objects["low"]
    head_slot = [m.name for m in low.data.materials].index("from_head")
    slot_names = [m.name for m in low.data.materials]
    wing_slots = {i for i, n in enumerate(slot_names) if n.startswith("from_wing")}
    memb_slot = slot_names.index("from_wing_membrane") if (WING_V3 or WING_V5) else -1
    claw_slot = slot_names.index("from_wing_horn") if (WING_V3 or WING_V5) else -1
    head_src = append_object("src_aligned.blend", "head_src")
    hcol = source_colours(head_src)
    hco = coords(head_src)
    sel = np.nonzero(hco[:, 2] > 4.9)[0]
    kd = KDTree(len(sel))
    for i in sel:
        kd.insert(hco[i], int(i))
    kd.balance()
    bpy.data.objects.remove(head_src)

    bm = bmesh.new()
    bm.from_mesh(low.data)
    src_layer = bm.faces.layers.int.new("from_head")     # made first: a new layer moves the faces in memory
    wing_layer = bm.faces.layers.int.new("from_wing")
    # The decimation leaves the flat membrane with edges over a metre long.
    # Split them, so the membrane can fold.
    split = 0
    for _ in range(3):
        # Version 5's membrane is one sheet whose two faces share their triangles: it is not touched.
        long_edges = [e for e in bm.edges if e.calc_length() > MEMBRANE_MAX_EDGE
                      and all(f.material_index in wing_slots and not (WING_V5 and f.material_index == memb_slot)
                              for f in e.link_faces)]
        if not long_edges:
            break
        split += len(long_edges)
        bmesh.ops.subdivide_edges(bm, edges=long_edges, cuts=1)
        bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 3])
    log("wing edges split:", split)
    thick_layer = bm.verts.layers.float.new("thick")
    cut_joint_loops(bm, skip_slot=memb_slot if WING_V5 else None)
    # A loop cut can leave a wing vertex 2 mm from another. The rig's fold stretches such an edge forty times.
    if WING_V3 or WING_V5:
        close = [v for v in bm.verts if v.link_faces and all(f.material_index in wing_slots for f in v.link_faces)
                 and not any(f.material_index == memb_slot for f in v.link_faces)]
        before = len(bm.verts)
        bmesh.ops.remove_doubles(bm, verts=close, dist=0.006)
        log("wing vertices merged within 6 mm:", before - len(bm.verts))
    bmesh.ops.dissolve_degenerate(bm, dist=2e-4, edges=bm.edges)
    bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 3])
    bm.verts.ensure_lookup_table()
    bm.faces.ensure_lookup_table()
    bm.normal_update()
    tree = BVHTree.FromBMesh(bm)

    # Membrane: the wing stage marked it, and cut the mesh along its border.
    # The thickness of the wing at each vertex feeds the thickness map.
    for v in bm.verts:
        t = 9.0
        if abs(v.co.x) > WING_MIN_X - 0.3 and v.co.z > 2.0:
            hit = tree.ray_cast(v.co - v.normal * 0.004, -v.normal)
            if hit[0] is not None:
                t = hit[3] + 0.004
        v[thick_layer] = t

    def near_claw(c):
        return any((abs(c.x) - x) ** 2 + (c.y - y) ** 2 < r * r for x, y, r in WING_CLAWS)

    if WING_V3 or WING_V5:
        memb = {f: f.material_index == memb_slot for f in bm.faces}
    else:
        # Version 2's wing: the membrane is the thin sheet of the wing piece.
        memb = {}
        for f in bm.faces:
            c = f.calc_center_median()
            thin = sum(1 for v in f.verts if v[thick_layer] < MEMBRANE_MAX_THICKNESS)
            memb[f] = (f.material_index in wing_slots and thin >= 2 and abs(c.x) > WING_MIN_X and c.z > 2.0
                       and not near_claw(c))
        memb = majority(bm, memb, 3)
        memb = drop_small(bm, memb, 60)
        inv = drop_small(bm, {f: not memb[f] for f in bm.faces}, 60)
        memb = {f: not inv[f] for f in bm.faces}

    # Horns: the head sculpt's grey-brown parts above the frill. Claws: by position.
    horn, sat_of = {}, {}
    for f in bm.faces:
        c = f.calc_center_median()
        is_horn, sat = False, 9.0
        if f.material_index == head_slot and c.z > 5.05 and c.y < 3.62:
            col = np.mean([hcol[kd.find(v.co)[1]] for v in f.verts], axis=0)
            mx = float(col.max())
            sat = (mx - float(col.min())) / max(mx, 1e-4)
            is_horn = sat < HORN_SATURATION
        horn[f] = is_horn
        sat_of[f] = sat
    horn = majority(bm, horn, 2)
    horn = drop_small(bm, horn, 60)
    # Grow each seed over the horn's smooth tube. A horn meets the head in a
    # crease, and the growth stops there.
    front = [f for f in bm.faces if horn[f]]
    while front:
        nxt = []
        for f in front:
            for e in f.edges:
                for g in e.link_faces:
                    if horn[g] or sat_of[g] > HORN_GROW_SATURATION:
                        continue
                    if len(e.link_faces) == 2 and not e.is_convex and e.calc_face_angle(0.0) > math.radians(28):
                        continue
                    horn[g] = True
                    nxt.append(g)
        front = nxt
    horn = majority(bm, horn, 2)
    inv = drop_small(bm, {f: not horn[f] for f in bm.faces}, 150)
    horn = {f: not inv[f] for f in bm.faces}
    n_horn = sum(horn.values())
    for f in bm.faces:
        c = f.calc_center_median()
        if f.material_index == head_slot:
            continue
        toe = c.z < TOE_CLAW_Z and abs(c.x) < 2.0 and f.material_index not in wing_slots and any(
            lo < c.y < hi for lo, hi in TOE_CLAWS)
        if toe or f.material_index == claw_slot or (
                not (WING_V3 or WING_V5) and f.material_index in wing_slots and near_claw(c) and c.z > 2.0 and not memb[f]):
            horn[f] = True
    log("faces: membrane", sum(memb.values()), "horn", n_horn, "claw", sum(horn.values()) - n_horn)

    if not (WING_V3 or WING_V5):
        # Thin the membrane: both faces of the sheet move toward its middle.
        moved = 0
        for v in bm.verts:
            if v.link_faces and all(memb[f] for f in v.link_faces) and v[thick_layer] < MEMBRANE_MAX_THICKNESS:
                v.co -= v.normal * (v[thick_layer] * MEMBRANE_THIN)
                moved += 1
        log("membrane vertices thinned:", moved)

    for f in bm.faces:
        f[src_layer] = 1 if f.material_index == head_slot else 0
        f[wing_layer] = 1 if f.material_index in wing_slots else 0
        f.material_index = 1 if memb[f] else (2 if horn[f] else 0)
        f.smooth = True
    bm.normal_update()
    bm.to_mesh(low.data)
    bm.free()
    idx = np.empty(len(low.data.polygons), np.int32)
    low.data.polygons.foreach_get("material_index", idx)
    low.data.materials.clear()           # this resets every face to slot 0
    for name in ("hide", "membrane", "horn"):
        low.data.materials.append(bpy.data.materials.new(name))
    low.data.polygons.foreach_set("material_index", idx)
    low.data.update()
    topo_report(low, "parts")
    log("triangles: hide", int((idx == 0).sum()), "membrane", int((idx == 1).sum()), "horn", int((idx == 2).sum()))
    save_blend("parts.blend")


# -------------------------------------------------------------------- stage: uv

MAP_SIZE = {"hide": 4096, "membrane": 2048, "horn": 1024, "eye": 512, "mouth": 1024}
UV_ANGLE = 76.0
MEMBRANE_UV_ANGLE = 88.0
HEAD_UV_SHARE = 0.30                # of the hide atlas's used area
WING_UV_SCALE = 1.0 if (WING_V3 or WING_V5) else 0.8    # the wing's arm and fingers get shorter texels than the body


def uv_array(me):
    n = len(me.loops)
    uv = np.empty(n * 2, dtype=np.float64)
    me.uv_layers[0].data.foreach_get("uv", uv)
    return uv.reshape(n, 2)


def uv_areas(me):
    """UV area of every triangle."""
    uv = uv_array(me)
    start = np.empty(len(me.polygons), np.int32)
    me.polygons.foreach_get("loop_start", start)
    a, b, c = uv[start], uv[start + 1], uv[start + 2]
    return 0.5 * np.abs((b[:, 0] - a[:, 0]) * (c[:, 1] - a[:, 1]) - (c[:, 0] - a[:, 0]) * (b[:, 1] - a[:, 1]))


def unwrap(obj, margin, head_share=None, angle=UV_ANGLE):
    me = obj.data
    while len(me.uv_layers):
        me.uv_layers.remove(me.uv_layers[0])
    me.uv_layers.new(name="UVMap")
    activate(obj)
    from_head = np.zeros(len(me.polygons), np.int32)
    if "from_head" in me.attributes:
        me.attributes["from_head"].data.foreach_get("value", from_head)
    groups = [from_head == 0, from_head == 1] if head_share else [np.ones(len(me.polygons), bool)]
    for mask in groups:
        if not mask.any():
            continue
        bpy.ops.object.mode_set(mode="OBJECT")
        me.polygons.foreach_set("select", mask)
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_mode(type="FACE")
        bpy.ops.uv.smart_project(angle_limit=math.radians(angle), island_margin=0.0, area_weight=0.0,
                                 correct_aspect=True, scale_to_bounds=False)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.average_islands_scale()
    bpy.ops.object.mode_set(mode="OBJECT")
    if head_share:
        area = uv_areas(me)
        a = area[from_head == 1].sum() / area.sum()
        k = math.sqrt(head_share / (1 - head_share) * (1 - a) / a)
        # Islands never mix the two groups, so scaling the head's corners
        # scales its islands. The pack sorts out the overlap.
        uv = uv_array(me)
        loop_total = np.empty(len(me.polygons), np.int32)
        me.polygons.foreach_get("loop_total", loop_total)
        loop_head = np.repeat(from_head == 1, loop_total)
        uv[loop_head] *= k
        if "from_wing" in me.attributes:
            from_wing = np.zeros(len(me.polygons), np.int32)
            me.attributes["from_wing"].data.foreach_get("value", from_wing)
            uv[np.repeat(from_wing == 1, loop_total)] *= WING_UV_SCALE
        me.uv_layers[0].data.foreach_set("uv", uv.ravel())
        log(obj.name, "head surface share of UV before", round(float(a), 3), "scale", round(k, 2))
    activate(obj)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.select_all(action="SELECT")
    # An added margin packs tighter than a fraction of the islands' size: 46% of the atlas against 38%.
    bpy.ops.uv.pack_islands(rotate=True, rotate_method="ANY", margin=margin, margin_method="ADD",
                            shape_method="CONCAVE")
    bpy.ops.object.mode_set(mode="OBJECT")
    bm = bmesh.new()
    bm.from_mesh(me)
    uvl = bm.loops.layers.uv.verify()
    islands = 0
    seen = set()
    for f in bm.faces:
        if f in seen:
            continue
        islands += 1
        stack = [f]
        seen.add(f)
        while stack:
            a = stack.pop()
            for l in a.loops:
                for r in l.link_loops:
                    g = r.face
                    if g not in seen and (r.link_loop_next[uvl].uv - l[uvl].uv).length < 1e-6 \
                            and (r[uvl].uv - l.link_loop_next[uvl].uv).length < 1e-6:
                        seen.add(g)
                        stack.append(g)
    bm.free()
    log(obj.name, "UV islands", islands)
    area = uv_areas(me)
    log(obj.name, "atlas coverage", round(float(area.sum()), 3),
        "head share", round(float(area[from_head == 1].sum() / max(area.sum(), 1e-9)), 3))


def stage_uv():
    """One object per material, each with its own atlas."""
    open_blend("parts.blend")
    low = bpy.data.objects["low"]
    me = low.data
    # Keep the shading continuous across the cuts between the parts.
    nrm = np.empty(len(me.vertices) * 3, dtype=np.float32)
    me.vertices.foreach_get("normal", nrm)
    me.normals_split_custom_set_from_vertices(nrm.reshape(-1, 3).tolist())
    activate(low)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.separate(type="MATERIAL")
    bpy.ops.object.mode_set(mode="OBJECT")
    for o in [o for o in bpy.data.objects if o.type == "MESH" and o.name.startswith("low")]:
        o.name = o.data.materials[0].name
        o.data.name = o.name
    for name in ("hide", "membrane", "horn"):
        obj = bpy.data.objects[name]
        # The membrane is two faces of one sheet: a wide angle keeps each face in a few islands.
        unwrap(obj, 0.0004 if name == "hide" else 0.0015, HEAD_UV_SHARE if name == "hide" else None,
               MEMBRANE_UV_ANGLE if name == "membrane" and (WING_V3 or WING_V5) else UV_ANGLE)
        topo_report(obj, name)

    # The membrane's thickness: the sculpt's own, and thicker near a bone.
    memb, hide = bpy.data.objects["membrane"], bpy.data.objects["hide"]
    hco = coords(hide)
    wing = hco[np.abs(hco[:, 0]) > 1.0]
    kd = KDTree(len(wing))
    for i, p in enumerate(wing):
        kd.insert(p, i)
    kd.balance()
    mco = coords(memb)
    t = np.empty(len(mco), np.float32)
    memb.data.attributes["thick"].data.foreach_get("value", t)
    shared = set(map(tuple, np.round(hco, 5)))
    val = np.empty(len(mco), np.float32)
    for i, p in enumerate(mco):
        # the nearest hide vertex that is not on the membrane's own border
        d = min((x[2] for x in kd.find_n(p, 12) if x[2] > 1e-4), default=1.0)
        val[i] = 0.45 * min(t[i] / 0.16, 1.0) + 0.55 * math.exp(-d / 0.45)
    a = memb.data.attributes.new("thickmap", "FLOAT", "POINT")
    a.data.foreach_set("value", val)
    log("membrane thickness map value", round(float(val.min()), 3), "to", round(float(val.max()), 3))
    save_blend("uv.blend")


# ------------------------------------------------------------------ stage: bake

LUMA = np.array([0.2126, 0.7152, 0.0722])
# The wing's colour moves toward the new body's. Share of the gain at the
# root and far out along the wing, per material.
if WING_V5:
    WING_GRADE = {"hide": (1.0, 0.0), "membrane": (0.0, 0.0), "horn": (0.0, 0.0)}
    WING_GRADE_NEAR = 0.9
    WING_GRADE_FADE = 1.0
elif WING_V3:
    WING_GRADE = {"hide": (1.0, 0.0), "membrane": (0.5, 0.0), "horn": (0.0, 0.0)}
    WING_GRADE_NEAR = 0.9           # half width inside which the gain is full
    WING_GRADE_FADE = 1.0
else:
    WING_GRADE = {"hide": (1.0, 0.6), "membrane": (1.0, 0.0), "horn": (0.0, 0.0)}
    WING_GRADE_NEAR = 1.6
    WING_GRADE_FADE = 2.4
ROUGH_SPREAD = 0.6
ROUGH = {"hide": (0.60, 0.50, 0.70), "membrane": (0.60, 0.55, 0.65), "horn": (0.30, 0.22, 0.38)}


def source_material(obj):
    """A material on a sculpt that can show one of its maps as emission, or
    its normal map through a surface shader."""
    old = obj.data.materials[0]
    imgs = {}
    for n in old.node_tree.nodes:
        if n.type == "TEX_IMAGE" and n.image:
            for o in n.outputs:
                for l in o.links:
                    if l.to_socket.name == "Base Color":
                        imgs["diff"] = n.image
                    elif l.to_node.type == "NORMAL_MAP":
                        imgs["normal"] = n.image
                    elif l.to_node.type in ("SEPARATE_COLOR", "SEPRGB", "SEPARATE_RGB") or l.to_socket.name in ("Roughness", "Metallic", "Color"):
                        imgs.setdefault("orm", n.image)
    for key in ("diff", "normal", "orm"):
        if key not in imgs:
            raise HeroError(f"{obj.name}: no {key} map found")
    m = bpy.data.materials.new(obj.name + "_bake")
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    emit = nt.nodes.new("ShaderNodeEmission")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.name = "tex"
    nor = nt.nodes.new("ShaderNodeTexImage")
    nor.image = imgs["normal"]
    nor.image.colorspace_settings.name = "Non-Color"
    nmap = nt.nodes.new("ShaderNodeNormalMap")
    nt.links.new(nor.outputs["Color"], nmap.inputs["Color"])
    nt.links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])
    nt.links.new(tex.outputs["Color"], emit.inputs["Color"])
    attr = nt.nodes.new("ShaderNodeAttribute")
    attr.name = "plan"
    attr.attribute_name = "plan"
    obj.data.materials.clear()
    obj.data.materials.append(m)
    return m, imgs


def source_mode(m, imgs, mode):
    nt = m.node_tree
    out = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL")
    emit = next(n for n in nt.nodes if n.type == "EMISSION")
    bsdf = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    for l in list(out.inputs["Surface"].links):
        nt.links.remove(l)
    if mode == "normal":
        nt.links.new(bsdf.outputs[0], out.inputs["Surface"])
        return
    nt.links.new(emit.outputs[0], out.inputs["Surface"])
    for l in list(emit.inputs["Color"].links):
        nt.links.remove(l)
    if mode == "plan":
        nt.links.new(nt.nodes["plan"].outputs["Vector"], emit.inputs["Color"])
        return
    nt.links.new(nt.nodes["tex"].outputs["Color"], emit.inputs["Color"])
    nt.nodes["tex"].image = imgs[mode]
    imgs[mode].colorspace_settings.name = "sRGB" if mode == "diff" else "Non-Color"


def target_material(obj):
    m = obj.data.materials[0]
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    emit = nt.nodes.new("ShaderNodeEmission")
    attr = nt.nodes.new("ShaderNodeAttribute")
    attr.name = "attr"
    geo = nt.nodes.new("ShaderNodeNewGeometry")
    geo.name = "geo"
    node = nt.nodes.new("ShaderNodeTexImage")
    node.name = "bake_target"
    nt.links.new(emit.outputs[0], out.inputs["Surface"])
    return m


def bake_image(sc, target, kind, size, source=None, extrusion=0.03, attribute=None, samples=4):
    """Bake one map onto `target`. Returns (h, w, 4) float32, bottom row first."""
    m = target.data.materials[0]
    nt = m.node_tree
    node = nt.nodes["bake_target"]
    img = bpy.data.images.new("bake_" + kind, size, size, alpha=True, float_buffer=True)
    img.colorspace_settings.name = "Linear Rec.709" if kind == "diff" else "Non-Color"
    node.image = img
    nt.nodes.active = node
    emit = next(n for n in nt.nodes if n.type == "EMISSION")
    for l in list(emit.inputs["Color"].links):
        nt.links.remove(l)
    sc.cycles.samples = samples
    sc.render.bake.margin = 0
    if source is not None:
        activate(target, [source])
        bpy.ops.object.bake(type="NORMAL" if kind == "normal" else "EMIT", normal_space="TANGENT",
                            use_selected_to_active=True, cage_extrusion=extrusion,
                            max_ray_distance=extrusion * 2.6, margin=0, use_clear=True)
    else:
        activate(target)
        if kind == "object_normal":
            bpy.ops.object.bake(type="NORMAL", normal_space="OBJECT", use_selected_to_active=False,
                                margin=0, use_clear=True)
        elif kind == "ao":
            bpy.ops.object.bake(type="AO", use_selected_to_active=False, margin=0, use_clear=True)
        else:
            if kind == "position":
                nt.links.new(nt.nodes["geo"].outputs["Position"], emit.inputs["Color"])
            else:
                nt.nodes["attr"].attribute_name = attribute
                nt.links.new(nt.nodes["attr"].outputs["Fac"], emit.inputs["Color"])
            bpy.ops.object.bake(type="EMIT", use_selected_to_active=False, margin=0, use_clear=True)
    a = np_from_image(img).copy()
    bpy.data.images.remove(img)
    return a


def delight(colour, normals, valid):
    """Fit log luminance = a + b . n over the texels and divide the b term
    out. A baked sun or sky shows as a b that is not zero."""
    lum = np.maximum(colour @ LUMA.astype(np.float32), 1e-4)
    idx = np.flatnonzero(valid.ravel())
    if len(idx) < 1000:
        return colour, [0, 0, 0]
    idx = idx[::max(1, len(idx) // 400000)]
    n = normals.reshape(-1, 3)[idx]
    y = np.log(lum.ravel()[idx])
    A = np.concatenate([np.ones((len(idx), 1), np.float32), n], axis=1)
    coef, *_ = np.linalg.lstsq(A, y, rcond=None)
    b = coef[1:]
    out = colour * np.exp(-(normals @ b.astype(np.float32)))[..., None]
    return out, [round(float(x), 3) for x in b]


def finish_maps(name, base, normal, rough, ao, valid, groups=()):
    """Dilate, clamp and write one material's three maps. Each group of
    texels (one per sculpt) is centred on the material's mean roughness, so
    no step in gloss shows where two sculpts meet."""
    mean_r, lo, hi = ROUGH[name]
    r = rough.copy()
    for g in (groups or (valid,)):
        if g.any():
            r[g] = mean_r + (r[g] - r[g].mean()) * ROUGH_SPREAD
    r = np.clip(r, lo, hi)
    orm = np.stack([ao, r, np.zeros_like(r)], axis=-1)
    pad = 12 if base.shape[0] >= 4096 else 8
    base, v2 = dilate_fill(base, valid, pad)
    base[~v2] = base[valid].mean(axis=0)
    orm, v2 = dilate_fill(orm, valid, pad)
    orm[~v2] = orm[valid].mean(axis=0)
    normal = normal.copy()
    normal[~valid] = (0.0, 0.0, 1.0)
    normal, _ = dilate_fill(normal, valid, pad)
    normal /= np.maximum(np.linalg.norm(normal, axis=2, keepdims=True), 1e-6)
    stem = cache("map_" + name)
    write_jpeg(stem + "_base.jpg", to_u8(linear_to_srgb(base)), 92)
    write_jpeg(stem + "_orm.jpg", to_u8(orm), 90)
    write_jpeg(stem + "_normal.jpg", to_u8(normal * 0.5 + 0.5), 95)
    log(name, "maps written; mean albedo", base[valid].mean(axis=0).round(3), "roughness",
        round(float(r[valid].min()), 2), "to", round(float(r[valid].max()), 2))


def stage_bake():
    """Bake the sculpts onto the three hide parts."""
    open_blend("uv.blend")
    sc = bpy.context.scene
    body_src = append_object("src2.blend", "body2_src")
    head_src = append_object("src2.blend", "head_src")
    wing_src = append_object("wing5.blend" if WING_V5 else "wing2.blend" if WING_V3 else "src2.blend", "wing_src")
    src = {}
    for o in (body_src, head_src, wing_src):
        src[o.name] = (o,) + source_material(o)
    stats = {}
    for name in ("horn", "membrane", "hide"):
        obj = bpy.data.objects[name]
        size = MAP_SIZE[name]
        target_material(obj)
        extrusion = 0.07 if name == "membrane" else 0.035
        mask = bake_image(sc, obj, "mask", size, attribute="from_head")
        valid = mask[..., 3] > 0.5
        head = valid & (mask[..., 0] > 0.5)
        wing = valid & (bake_image(sc, obj, "mask", size, attribute="from_wing")[..., 0] > 0.5)
        pos = bake_image(sc, obj, "position", size)[..., :3]
        onrm = bake_image(sc, obj, "object_normal", size)[..., :3] * 2.0 - 1.0
        onrm /= np.maximum(np.linalg.norm(onrm, axis=2, keepdims=True), 1e-6)
        out = {}
        for kind in ("diff", "orm", "normal"):
            acc = np.zeros((size, size, 3), np.float32)
            for sname, region in (("body2_src", valid & ~head & ~wing), ("head_src", head), ("wing_src", wing)):
                if not region.any():
                    continue
                so, sm, simgs = src[sname]
                source_mode(sm, simgs, kind)
                a = bake_image(sc, obj, kind, size, source=so, extrusion=extrusion,
                               samples=1 if kind != "normal" else 4)
                hit = a[..., 3] > 0.5
                if kind == "diff":
                    out.setdefault("hit", np.zeros((size, size), bool))
                    out["hit"] |= region & hit
                acc[region] = a[..., :3][region]
                log(name, kind, "from", sname, "texels", int(region.sum()), "missed", int((region & ~hit).sum()))
            out[kind] = acc
        # Where each wing texel lies on the wing sculpt's plan, and on which face.
        plan = np.zeros((size, size, 3), np.float32)
        if wing.any() and WING_V3:
            so, sm, simgs = src["wing_src"]
            source_mode(sm, simgs, "plan")
            a = bake_image(sc, obj, "plan", size, source=so, extrusion=extrusion, samples=1)
            a, _ = dilate_fill(a[..., :3], a[..., 3] > 0.5, 24)
            plan[wing] = a[wing]
            # The face of the wing comes from the reduced mesh: in a fold a ray can reach the other face.
            plan[..., 2] = bake_image(sc, obj, "mask", size, attribute="wing_upper")[..., 0]
        if wing.any() and WING_V5 and name == "membrane":
            plan[..., 2] = bake_image(sc, obj, "mask", size, attribute="wing_upper")[..., 0]
        ao = None
        if name == "hide":
            ao = bake_image(sc, obj, "ao", size // 2, samples=12)[..., 0]
        thick = None
        if name == "membrane":
            thick = bake_image(sc, obj, "thick", size, attribute="thickmap")[..., 0]
        np.savez(cache("raw_" + name + ".npz"), diff=out["diff"].astype(np.float16),
                 rough=out["orm"][..., 1].astype(np.float16), normal=out["normal"].astype(np.float16),
                 pos=pos.astype(np.float16), onrm=onrm.astype(np.float16), hit=out["hit"], valid=valid,
                 head=head, wing=wing, plan=plan.astype(np.float32), ao=np.zeros(1) if ao is None else ao.astype(np.float16),
                 thick=np.zeros(1) if thick is None else thick.astype(np.float16))
        log("raw bake saved for", name)


# The wing's colours, linear, read off the two plates the wing sculpt was made
# from (artifacts/showcase-study/hero-wing-plates): a saturated orange-red
# membrane with dark red-brown bones above, a paler salmon below. The sculpt's
# own colour is one pale pink on both faces.
WING_COLOUR = {(True, "membrane"): (0.60, 0.105, 0.022), (True, "bone"): (0.19, 0.045, 0.020),
               (False, "membrane"): (0.68, 0.27, 0.17), (False, "bone"): (0.46, 0.17, 0.10)}
WING_CONTRAST = {True: 1.35, False: 1.0}


def grade_wing(name, base, tn, wing, plan, onrm, stats):
    """Paint the wing in plan: the stub bones' colour goes, the swept bone
    gets a bone's colour, and each face is graded to its plate."""
    marks = np.load(cache("wing2_plan.npz"))
    px = np.clip((plan[..., :2] * W2_N).astype(int), 0, W2_N - 1)
    upper = plan[..., 2] > 0.5
    stub = marks["stub"].astype(np.float32)[px[..., 1], px[..., 0]] * wing
    bridge = marks["bridge"][px[..., 1], px[..., 0]] & wing
    bone = smoothstep((marks["thick"][px[..., 1], px[..., 0]] - 0.0095) / 0.006) * (1.0 - stub)
    if name == "horn":
        return base, tn
    logc = np.log(np.maximum(base, 1e-4))
    # A texel shows the upper colour by how far its surface faces up in the bind pose. Where the
    # fit folds the sheet over, or the underside wraps the leading edge, what is seen from above
    # is then the upper colour, not a pale patch.
    facing = smoothstep((onrm[..., 2] + 0.2) / 0.4)
    out = np.zeros_like(base)
    for face in (True, False):
        sel = wing & (upper == face)
        sheet, rib = sel & (bone < 0.15) & (stub < 0.05), sel & (bone > 0.85) & ~bridge
        if sheet.sum() < 500 and rib.sum() < 500:
            continue
        mean_sheet = logc[sheet].mean(axis=0) if sheet.sum() >= 500 else logc[rib].mean(axis=0)
        mean_rib = logc[rib].mean(axis=0) if rib.sum() >= 500 else mean_sheet
        # A stub's texels lose the bone's colour: they take the sheet's mean and keep a third of their own detail.
        detail = logc - (mean_sheet + (mean_rib - mean_sheet) * np.maximum(bone, stub)[..., None])
        detail = detail * (1.0 - 0.67 * stub[..., None])
        detail[bridge] *= 0.3
        b = np.where(bridge, 1.0, bone)[..., None]
        # The detail is measured against this face's own means. The colour it is laid on is the upper
        # one or the lower one by which way the texel faces.
        for shown, weight in ((True, facing), (False, 1.0 - facing)):
            target = np.log(np.array(WING_COLOUR[(shown, "membrane")], np.float32)) * (1 - b) \
                + np.log(np.array(WING_COLOUR[(shown, "bone")], np.float32)) * b
            graded = np.exp(target + WING_CONTRAST[shown] * detail)
            out[sel] += (graded * weight[..., None])[sel]
        stats[f"{name}_wing_{'upper' if face else 'lower'}_source_mean"] = [round(float(x), 3) for x in np.exp(mean_sheet)]
    flat = (stub > 0.3)[..., None]
    tn = np.where(flat, tn * (1 - stub[..., None]) + np.array([0, 0, 1], np.float32) * stub[..., None], tn)
    log(name, "wing graded: stub texels", int((stub > 0.3).sum()), "bridge texels", int(bridge.sum()),
        "upper texels", int((wing & upper).sum()), "lower", int((wing & ~upper).sum()))
    out[~wing] = base[~wing]
    return np.clip(out, 0.0, 1.0), tn


def grade_wing5(name, base, wing, plan, stats):
    """Version 5: each face of the membrane, the bones and the claws move to version 4's colour.
    The shift is one log gain per part, so the sculpt's own veins and rings keep their contrast."""
    logc = np.log(np.maximum(base, 1e-4))
    out = base.copy()
    if name == "membrane":
        upper = plan[..., 2] > 0.25
        parts = (("upper", wing & upper), ("lower", wing & ~upper))
    else:
        parts = ((("bone" if name == "hide" else "claw"), wing),)
    for key, sel in parts:
        if sel.sum() < 500:
            continue
        mean = logc[sel].mean(axis=0)
        k = W5_CONTRAST if name == "membrane" else 1.0
        target = np.log(np.array(W5_COLOUR[key], np.float32))
        out[sel] = np.exp(target + k * (logc[sel] - mean))
        stats[f"{name}_wing5_{key}"] = {"source": [round(float(x), 4) for x in np.exp(mean)],
                                        "target": list(W5_COLOUR[key]), "texels": int(sel.sum())}
        log(name, "wing5", key, "source mean", np.exp(mean).round(4), "to", W5_COLOUR[key])
    return np.clip(out, 0.0, 1.0)


W5_ROOT_TEXTURE_X = (2.25, 2.60)    # m: the root patch's texels are replaced inside the first, faded out by the second


def smooth_root5(base, tn, wing, plan, pos):
    """The root patch's bake source is the sculpt's root panel carried onto the patch, and where that panel
    reaches past the patch it collapses and bakes streaks. Its texels take each face's target colour with a
    gentle variation in space instead, and a flat normal. The fade ends before the rib."""
    x = np.abs(pos[..., 0])
    w = (1.0 - smoothstep((x - W5_ROOT_TEXTURE_X[0]) / (W5_ROOT_TEXTURE_X[1] - W5_ROOT_TEXTURE_X[0]))) * wing
    upper = plan[..., 2] > 0.25
    y, z = pos[..., 1], pos[..., 2]
    vary = (np.sin(x * 3.1 + y * 1.7) * np.sin(y * 4.3 - z * 2.1) + 0.5 * np.sin(x * 7.9 - y * 6.2 + z * 3.3)) / 1.5
    out = base.copy()
    for key, sel in (("upper", upper), ("lower", ~upper)):
        col = np.exp(np.log(np.array(W5_COLOUR[key], np.float32))[None, None, :] + 0.10 * vary[..., None])
        out = np.where(sel[..., None], base * (1 - w[..., None]) + col * w[..., None], out)
    tn = tn * (1 - w[..., None]) + np.array([0, 0, 1], np.float32) * w[..., None]
    log("membrane root patch texels replaced:", int((w > 0.99).sum()), "faded:", int(((w > 0) & (w <= 0.99)).sum()))
    return out.astype(np.float32), tn


def stage_maps():
    """Grade the raw bakes and write each part's three maps."""
    reset_scene()
    stats = {}
    wing_gain = np.ones(3, np.float32)
    for name in ("hide", "horn", "membrane"):
        raw = np.load(cache("raw_" + name + ".npz"))
        valid, head, hit, wing = raw["valid"], raw["head"], raw["hit"], raw["wing"]
        body = valid & ~head & ~wing
        size = valid.shape[0]
        pos = raw["pos"].astype(np.float32)
        onrm = raw["onrm"].astype(np.float32)
        # Texels the rays missed take their neighbours' values.
        base, grown = dilate_fill(raw["diff"].astype(np.float32), hit, 6)
        rough, _ = dilate_fill(raw["rough"].astype(np.float32)[..., None], hit, 6)
        tn = raw["normal"].astype(np.float32) * 2.0 - 1.0
        tn[~hit] = (0, 0, 1)
        # Where the mesh was moved off the sculpt (the lids, the eye sockets)
        # whole faces miss. They take the colour of the nearest baked texel
        # in space, not in the atlas.
        lost = valid & ~grown
        if lost.any():
            lp = pos[lost]
            lo, hi = lp.min(axis=0) - 0.06, lp.max(axis=0) + 0.06
            cand = np.flatnonzero((hit & np.all((pos > lo) & (pos < hi), axis=2)).ravel())
            cand = cand[::max(1, len(cand) // 250000)]
            flat_pos = pos.reshape(-1, 3)
            kd = KDTree(len(cand))
            for k, i in enumerate(cand):
                kd.insert(flat_pos[i], k)
            kd.balance()
            src = np.array([cand[kd.find(p_)[1]] for p_ in lp])
            base[lost] = base.reshape(-1, 3)[src]
            rough[lost] = rough.reshape(-1, 1)[src]
            log(name, "texels filled from the nearest baked texel in space:", int(lost.sum()))
        base = np.maximum(base, 1e-4)
        # Baked light. The fit finds how luminance follows the surface
        # direction. Only the X term is removed: the dragon is symmetric in
        # X, so an X term can only be light. The Y and Z terms are the
        # design's pale belly and throat, and they stay.
        for label, region in (("body", body), ("head", head)) + (() if (WING_V3 or WING_V5) else (("wing", wing),)):
            if region.sum() > 1000:
                _, b = delight(base, onrm, region)
                stats[f"{name}_{label}_light_xyz"] = b
                base[region] = (base * np.exp(-(onrm[..., 0] * b[0]))[..., None])[region]
                log(name, label, "luminance against direction (x, y, z):", b, "- removed x")
        if wing.any() and WING_V3:
            base, tn = grade_wing(name, base, tn, wing, raw["plan"], onrm, stats)
        if wing.any() and WING_V5:
            base = grade_wing5(name, base, wing, raw["plan"], stats)
            if name == "membrane":
                base, tn = smooth_root5(base, tn, wing, raw["plan"], pos)
        if name == "hide":
            # The head sculpt's neck takes the body sculpt's colour where
            # they meet: one gain for the dark back, one for the pale front.
            z, y = pos[..., 2], pos[..., 1]
            gains = []
            for lo_y, hi_y in ((0.0, 2.25), (2.75, 9.0)):
                hb = head & (z < 4.2) & (y > lo_y) & (y < min(hi_y, 3.3))
                bb = body & (z > 3.3) & (z < 3.95) & (y > max(lo_y, 1.7)) & (y < hi_y) \
                    & (np.abs(pos[..., 0]) < 0.7)
                g = np.exp(np.log(base[bb]).mean(axis=0) - np.log(base[hb]).mean(axis=0))
                gains.append(np.clip(g, 0.7, 1.45))
            stats["neck_gain_back_front"] = [[round(float(c), 3) for c in g] for g in gains]
            log("neck colour gains (back, front):", stats["neck_gain_back_front"])
            f = smoothstep((y - 2.3) / 0.4)[..., None]
            gain = gains[0] * (1 - f) + gains[1] * f
            w = (1.0 - NECK_MATCH_KEEP * smoothstep((z - 4.0) / 0.9))[..., None]
            base[head] = (base * gain ** w)[head]
            # Then both sides of the seam move half way to each other, by
            # direction round the neck: the back, the flanks and the throat
            # each have their own colour.
            ang = np.abs(np.arctan2(pos[..., 0], y - NECK2_AXIS_Y))
            edges = np.linspace(0.0, np.pi, NECK_BINS + 1)
            hsel = head & (z > 3.6) & (z < 4.15)
            bsel = body & (z > 3.35) & (z < 3.95) & (y > 1.5) & (np.abs(pos[..., 0]) < 0.9)
            diff = np.zeros((NECK_BINS, 3), np.float32)
            for i in range(NECK_BINS):
                hb = hsel & (ang >= edges[i]) & (ang < edges[i + 1])
                bb = bsel & (ang >= edges[i]) & (ang < edges[i + 1])
                if hb.sum() > 500 and bb.sum() > 500:
                    diff[i] = np.clip(np.log(base[bb]).mean(axis=0) - np.log(base[hb]).mean(axis=0),
                                      -NECK_BIN_CLAMP, NECK_BIN_CLAMP)
            stats["neck_bin_gain"] = [[round(float(c), 3) for c in np.exp(d)] for d in diff]
            log("neck gains by direction, throat to nape:", stats["neck_bin_gain"])
            centres = (edges[:-1] + edges[1:]) / 2
            d_at = np.stack([np.interp(ang, centres, diff[:, c]) for c in range(3)], axis=-1)
            wh = (1.0 - smoothstep((z - 3.9) / 0.6))[..., None]
            wb = (smoothstep((z - 2.9) / 0.6) * smoothstep((y - 1.4) / 0.5))[..., None]
            base[head] = (base * np.exp(0.5 * d_at * wh))[head]
            base[body] = (base * np.exp(-0.5 * d_at * wb))[body]
            # The sculpt has an eye painted where the lids now open. Paint the
            # lids over it with the colour of the skin round the eye.
            for side in (1, -1):
                s0, n, a1, a2, c = eye_frame(side)
                n = (n - a2 * EYE_VIEW_DROP).normalized()
                a2 = n.cross(a1)
                if a2.z < 0:
                    a2 = -a2
                d = pos - np.array(s0, dtype=np.float32)
                u, w_, hgt = d @ np.array(a1, np.float32), d @ np.array(a2, np.float32), d @ np.array(n, np.float32)
                near = head & (np.abs(hgt) < 0.09) & (u * u + w_ * w_ < 0.3 ** 2)
                r = np.sqrt(u * u + w_ * w_)
                cs, sn = u / np.maximum(r, 1e-6), w_ / np.maximum(r, 1e-6)
                bb_ = np.where(sn > 0, EYE_OPEN[1], EYE_OPEN[2])
                open_r = (np.abs(cs / EYE_OPEN[0]) ** 1.75 + np.abs(sn / bb_) ** 1.75) ** (-1 / 1.75)
                q = r / np.maximum(open_r, 1e-6)
                ring = near & (q > 1.45) & (q < 1.9)
                lid = np.median(base[ring], axis=0) * np.array([0.50, 0.40, 0.36], np.float32)
                # also any texel near the eye that still has the painted eye's
                # amber, its white highlight or its black pupil
                rr, gg, bl = base[..., 0], base[..., 1], base[..., 2]
                painted = near & (q < 3.2) & (((rr > 0.22) & (gg > 0.36 * rr) & (bl < 0.3 * rr) & (gg > 0.1))
                                              | (base.min(axis=2) > 0.28) | (base.max(axis=2) < 0.012))
                socket = head & (hgt < -0.02) & (hgt > -0.45) & (r < 0.2) & (np.linalg.norm(
                    pos - np.array(c, dtype=np.float32), axis=2) < EYE_R + EYE_CORNEA + EYE_GAP + EYE_SOCKET_MARGIN + 0.004)
                wgt = np.maximum(np.maximum(smoothstep((1.36 - q) / 0.22) * near, painted.astype(np.float32)),
                                 socket.astype(np.float32))[..., None]
                shade = (0.55 + 0.45 * np.clip((q - 0.9) / 0.45, 0, 1))[..., None]
                base = base * (1 - wgt) + lid * shade * wgt
                tn = tn * (1 - wgt) + np.array([0, 0, 1], np.float32) * wgt
                rough = rough * (1 - wgt) + float(rough[ring].mean()) * wgt
                stats[f"lid_colour_{side}"] = [round(float(x), 3) for x in lid]
            # The wing arm takes the new body's colour: one gain, measured
            # where the wing's root meets the body.
            ax = np.abs(pos[..., 0])
            near_w = wing & (ax < WING_GRADE_NEAR) & (pos[..., 2] > 2.9)
            near_b = body & (ax > 0.3) & (ax < 1.2) & (pos[..., 2] > 2.9) & (pos[..., 1] > -0.3) & (pos[..., 1] < 1.9)
            wing_gain = np.clip(np.exp(np.log(base[near_b]).mean(axis=0) - np.log(base[near_w]).mean(axis=0)),
                                0.5, 2.0).astype(np.float32)
            stats["wing_gain"] = [round(float(c), 3) for c in wing_gain]
            log("wing colour gain:", stats["wing_gain"])
            ao = raw["ao"].astype(np.float32)
            ao = np.repeat(np.repeat(ao, 2, axis=0), 2, axis=1)
            ao = np.clip(0.35 + 0.65 * ao, 0, 1)
        else:
            ao = np.ones((size, size), np.float32)
        if wing.any():
            ax = np.abs(pos[..., 0])
            full, far = WING_GRADE[name]
            w = (far + (full - far) * (1.0 - smoothstep((ax - WING_GRADE_NEAR) / WING_GRADE_FADE)))[..., None]
            base[wing] = (base * wing_gain ** w)[wing]
        base = np.clip(base, 0.0, 1.0)
        finish_maps(name, base, tn, rough[..., 0], ao, valid, groups=(body, head, wing))
        if name == "membrane":
            tv, v2 = dilate_fill(raw["thick"].astype(np.float32)[..., None], valid, 8)
            tv[~v2] = tv[valid].mean()
            write_png(OUT_THICK, to_u8(tv[..., 0]))
            log("thickness map written", OUT_THICK)
    with open(cache("bake_stats.json"), "w") as f:
        json.dump(stats, f, indent=1)


# ---------------------------------------------------------------- stage: export

EYE_IRIS_DEG = 23.0
EYE_UV_DEG = 78.0                   # this polar angle maps to radius 0.47 in UV


def eye_maps():
    """Paint the eye: amber iris, vertical slit pupil, dark rim. The UV
    centre is the pupil, +V is up."""
    size = MAP_SIZE["eye"]
    ax = (np.arange(size) + 0.5) / size - 0.5
    x, y = np.meshgrid(ax, ax)
    rho = np.sqrt(x * x + y * y)
    theta = np.arctan2(y, x)
    ri = 0.47 * EYE_IRIS_DEG / EYE_UV_DEG
    t = np.clip(rho / ri, 0, 1)
    rng = np.random.default_rng(7)
    # radial fibres: noise that depends on the angle, stretched along the radius
    ang = rng.normal(size=720)
    k = np.exp(-0.5 * (np.arange(-6, 7) / 2.0) ** 2)
    ang = np.convolve(np.concatenate([ang[-6:], ang, ang[:6]]), k / k.sum(), mode="valid")
    fibre = ang[((theta + np.pi) / (2 * np.pi) * 720).astype(int) % 720]
    fine = rng.normal(size=1440)
    fibre2 = fine[((theta + np.pi) / (2 * np.pi) * 1440).astype(int) % 1440]
    inner = np.array([1.00, 0.80, 0.22])
    mid = np.array([0.96, 0.55, 0.07])
    outer = np.array([0.62, 0.24, 0.04])
    iris = np.where((t < 0.5)[..., None], inner + (mid - inner) * (t / 0.5)[..., None],
                    mid + (outer - mid) * ((t - 0.5) / 0.5)[..., None])
    iris = iris * (1.0 + 0.22 * fibre[..., None] * (0.3 + 0.7 * t[..., None]) + 0.06 * fibre2[..., None])
    sclera_t = np.clip((rho - ri) / (0.47 - ri), 0, 1)[..., None]
    sclera = np.array([0.50, 0.22, 0.07]) * (1 - sclera_t) + np.array([0.20, 0.07, 0.04]) * sclera_t
    blend = smoothstep((rho - ri + 0.004) / 0.008)[..., None]
    col = iris * (1 - blend) + sclera * blend
    limbus = np.exp(-0.5 * ((rho - ri) / 0.011) ** 2)[..., None]
    col = col * (1 - 0.72 * limbus)
    # the slit: a lens with pointed ends
    ay, axw = ri * 0.86, ri * 0.20
    half = axw * np.clip(1 - (y / ay) ** 2, 0, 1) ** 0.75
    pupil = (1 - smoothstep((np.abs(x) - half) / 0.004 + 0.5)) * (np.abs(y) < ay)
    col = col * (1 - pupil[..., None]) + np.array([0.012, 0.008, 0.006]) * pupil[..., None]
    base = np.clip(col, 0, 1)                       # these are sRGB values
    orm = np.zeros((size, size, 3))
    orm[..., 0] = 1.0
    orm[..., 1] = 0.05 + 0.10 * smoothstep((rho - ri * 1.25) / 0.05)    # the cornea is 0.05
    nrm = np.zeros((size, size, 3))
    nrm[..., :2] = 0.5
    nrm[..., 2] = 1.0
    stem = cache("map_eye")
    write_jpeg(stem + "_base.jpg", to_u8(base), 95)
    write_jpeg(stem + "_orm.jpg", to_u8(orm), 92)
    write_jpeg(stem + "_normal.jpg", to_u8(nrm), 95)


MOUTH_GUM = [(0.0, (0.16, 0.035, 0.03)), (0.12, (0.42, 0.06, 0.06)), (0.45, (0.36, 0.045, 0.05)),
             (0.80, (0.07, 0.008, 0.012)), (1.0, (0.004, 0.001, 0.001))]
MOUTH_TONGUE = [(0.0, (0.25, 0.035, 0.04)), (1.0, (0.50, 0.10, 0.10))]
MOUTH_TOOTH = [(0.0, (0.30, 0.17, 0.07)), (0.3, (0.68, 0.56, 0.36)), (1.0, (0.80, 0.74, 0.58))]
MOUTH_ROUGH = (0.16, 0.24, 0.20)    # gums, tongue, teeth: wet
MOUTH_RELIEF_MM = 5.0               # the depth of the palate's ridges in the normal map


def mouth_relief(sc, mouth, size):
    """A height for the mouth's surface, and the tangent-space normal it
    gives. Ridges across the palate and gums, papillae on the tongue, faint
    grain on the teeth. Returns (normal, height), both bottom row first."""
    nt = mouth.data.materials[0].node_tree
    out = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL")
    emit = next(n for n in nt.nodes if n.type == "EMISSION")
    co = nt.nodes.new("ShaderNodeTexCoord")
    kind = nt.nodes.new("ShaderNodeAttribute")
    kind.attribute_name = "kind"

    def math(op, a, b):
        n = nt.nodes.new("ShaderNodeMath")
        n.operation = op
        for i, v in enumerate((a, b)):
            if isinstance(v, (int, float)):
                n.inputs[i].default_value = v
            else:
                nt.links.new(v, n.inputs[i])
        return n.outputs[0]

    wave = nt.nodes.new("ShaderNodeTexWave")
    wave.wave_type = "BANDS"
    wave.bands_direction = "Y"
    wave.inputs["Scale"].default_value = 9.0
    wave.inputs["Distortion"].default_value = 2.5
    wave.inputs["Detail"].default_value = 2.0
    wave.inputs["Detail Scale"].default_value = 3.0
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 70.0
    noise.inputs["Detail"].default_value = 4.0
    vor = nt.nodes.new("ShaderNodeTexVoronoi")
    vor.inputs["Scale"].default_value = 160.0
    grain = nt.nodes.new("ShaderNodeTexNoise")
    grain.inputs["Scale"].default_value = 300.0
    for n in (wave, noise, vor, grain):
        nt.links.new(co.outputs["Object"], n.inputs["Vector"])
    gum_h = math("ADD", math("MULTIPLY", wave.outputs["Fac"], 0.7), math("MULTIPLY", noise.outputs["Fac"], 0.3))
    tongue_h = math("ADD", math("MULTIPLY", math("MINIMUM", math("MULTIPLY", vor.outputs["Distance"], 110.0), 1.0), 0.35),
                    math("MULTIPLY", noise.outputs["Fac"], 0.25))
    tooth_h = math("MULTIPLY", grain.outputs["Fac"], 0.06)
    is_gum = math("LESS_THAN", kind.outputs["Fac"], 0.5)
    is_tooth = math("GREATER_THAN", kind.outputs["Fac"], 1.5)
    is_tongue = math("SUBTRACT", math("SUBTRACT", 1.0, is_gum), is_tooth)
    height = math("ADD", math("ADD", math("MULTIPLY", gum_h, is_gum), math("MULTIPLY", tongue_h, is_tongue)),
                  math("MULTIPLY", tooth_h, is_tooth))
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 1.0
    bump.inputs["Distance"].default_value = MOUTH_RELIEF_MM / 1000.0
    nt.links.new(height, bump.inputs["Height"])
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    node = nt.nodes["bake_target"]

    def bake(kind_name, bake_type, colourspace):
        img = bpy.data.images.new("bake_" + kind_name, size, size, alpha=True, float_buffer=True)
        img.colorspace_settings.name = colourspace
        node.image = img
        nt.nodes.active = node
        activate(mouth)
        sc.cycles.samples = 4
        if bake_type == "NORMAL":
            bpy.ops.object.bake(type="NORMAL", normal_space="TANGENT", use_selected_to_active=False, margin=0,
                                use_clear=True)
        else:
            bpy.ops.object.bake(type="EMIT", use_selected_to_active=False, margin=0, use_clear=True)
        a = np_from_image(img).copy()
        bpy.data.images.remove(img)
        return a

    for l in list(emit.inputs["Color"].links):
        nt.links.remove(l)
    nt.links.new(height, emit.inputs["Color"])
    h = bake("height", "EMIT", "Non-Color")[..., 0]
    for l in list(out.inputs["Surface"].links):
        nt.links.remove(l)
    nt.links.new(bsdf.outputs[0], out.inputs["Surface"])
    nrm = bake("relief", "NORMAL", "Non-Color")[..., :3]
    for l in list(out.inputs["Surface"].links):
        nt.links.remove(l)
    nt.links.new(emit.outputs[0], out.inputs["Surface"])
    return nrm, h


def mouth_maps(sc, mouth):
    """The mouth's colours come from its vertex attributes: what a vertex
    is (gum, tongue, tooth) and how deep it lies."""
    me = mouth.data
    activate(mouth)
    me.uv_layers.new(name="UVMap")
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(70), island_margin=0.004)
    bpy.ops.object.mode_set(mode="OBJECT")
    m = bpy.data.materials.new("mouth")
    me.materials.clear()
    me.materials.append(m)
    target_material(mouth)
    size = MAP_SIZE["mouth"]
    kind = bake_image(sc, mouth, "kind", size, attribute="kind")
    depth = bake_image(sc, mouth, "depth", size, attribute="depth")[..., 0]
    valid = kind[..., 3] > 0.5
    k = np.rint(kind[..., 0]).astype(int)
    base = np.zeros((size, size, 3), np.float32)
    rough = np.zeros((size, size), np.float32)
    for idx, ramp in enumerate((MOUTH_GUM, MOUTH_TONGUE, MOUTH_TOOTH)):
        xs = [r[0] for r in ramp]
        col = np.stack([np.interp(depth, xs, [r[1][c] for r in ramp]) for c in range(3)], axis=-1)
        sel = k == idx
        base[sel] = col[sel]
        rough[sel] = MOUTH_ROUGH[idx]
    # Relief: the creases are darker and a little rougher than the crests.
    nrm, height = mouth_relief(sc, mouth, size)
    crest = np.clip(height / max(float(np.percentile(height[valid & (k == 0)], 98)), 1e-4), 0.0, 1.0)
    soft = k != 2
    base[soft] *= (0.62 + 0.55 * crest[soft])[:, None]
    rough[soft] += 0.10 * (1.0 - crest[soft])
    ao = np.clip(1.0 - 0.6 * depth * (k == 0), 0.3, 1.0).astype(np.float32)
    orm = np.stack([ao, rough, np.zeros_like(rough)], axis=-1)
    base, v2 = dilate_fill(base, valid, 8)
    base[~v2] = (0.2, 0.03, 0.03)
    orm, v2 = dilate_fill(orm, valid, 8)
    orm[~v2] = (1.0, 0.35, 0.0)
    nrm = nrm * 2.0 - 1.0
    # A sliver triangle has no tangent frame and bakes a wild normal.
    nrm[(~valid) | (nrm[..., 2] < 0.45)] = (0.0, 0.0, 1.0)
    nrm, _ = dilate_fill(nrm, valid, 8)
    nrm /= np.maximum(np.linalg.norm(nrm, axis=2, keepdims=True), 1e-6)
    nrm = nrm * 0.5 + 0.5
    log("mouth relief: normal map tilt up to", round(float(np.degrees(np.arccos(np.clip(
        (nrm[valid][:, 2] * 2 - 1).min(), -1, 1)))), 1), "degrees")
    stem = cache("map_mouth")
    write_jpeg(stem + "_base.jpg", to_u8(linear_to_srgb(base)), 92)
    write_jpeg(stem + "_orm.jpg", to_u8(orm), 90)
    write_jpeg(stem + "_normal.jpg", to_u8(nrm), 95)


def read_glb(path):
    with open(path, "rb") as f:
        blob = f.read()
    magic, version, length = struct.unpack("<4sII", blob[:12])
    if magic != b"glTF" or version != 2 or length != len(blob):
        raise HeroError(f"{path}: not a glTF 2 binary")
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


MATERIALS = ("hide", "membrane", "horn", "eye", "mouth")
EXPORT_OBJECTS = (("hide", "hide"), ("membrane", "membrane"), ("horn", "horn"),
                  ("eye_l", "eye"), ("eye_r", "eye"), ("mouth", "mouth"))


def stage_export():
    """Paint the eye and the mouth, then write the file."""
    open_blend("uv.blend")
    sc = bpy.context.scene
    eye_maps()
    mouth_maps(sc, bpy.data.objects["mouth"])
    mats = {name: bpy.data.materials.new("hero_" + name) for name in MATERIALS}
    objs = []
    for oname, mname in EXPORT_OBJECTS:
        obj = bpy.data.objects[oname]
        me = obj.data
        me.materials.clear()
        me.materials.append(mats[mname])
        for p in me.polygons:
            p.use_smooth = True
        keep = {"jaw": "_JAW"} if oname in ("hide", "mouth") else {"sheet_s": "_SHEET_S"} if oname == "membrane" else {}
        for a in list(me.attributes):
            if a.is_internal or a.is_required or a.name in ("UVMap", "custom_normal", "sharp_face", "material_index"):
                continue
            if a.name in keep:
                a.name = keep[a.name]
            elif a.domain in ("POINT", "FACE") and a.data_type in ("FLOAT", "INT", "FLOAT_COLOR", "BYTE_COLOR"):
                me.attributes.remove(a)
        objs.append(obj)
    for o in list(bpy.data.objects):
        if o not in objs:
            bpy.data.objects.remove(o)
    activate(objs[0], objs)
    os.makedirs(os.path.dirname(OUT_GLB), exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=OUT_GLB, export_format="GLB", use_selection=True,
                              export_yup=True, export_apply=True, export_tangents=True,
                              export_normals=True, export_texcoords=True, export_attributes=True,
                              export_materials="PLACEHOLDER", export_animations=False,
                              export_skins=False, export_morph=False, export_cameras=False,
                              export_lights=False, export_extras=False)
    doc, binary = read_glb(OUT_GLB)
    binary = bytearray(binary)
    order = list(MATERIALS)
    of_object = dict(EXPORT_OBJECTS)
    for mesh in doc["meshes"]:
        if len(mesh["primitives"]) != 1:
            raise HeroError(f"{mesh['name']}: expected one primitive")
        mesh["primitives"][0]["material"] = order.index(of_object[mesh["name"]])
    doc["images"], doc["textures"], new_mats = [], [], []
    doc["samplers"] = [dict(magFilter=9729, minFilter=9987, wrapS=10497, wrapT=10497)]
    for name in order:
        tex = {}
        for label in ("base", "normal", "orm"):
            path = cache(f"map_{name}_{label}.jpg")
            with open(path, "rb") as f:
                data = f.read()
            binary += b"\0" * (-len(binary) % 4)
            doc["bufferViews"].append(dict(buffer=0, byteOffset=len(binary), byteLength=len(data)))
            binary += data
            doc["images"].append(dict(name=f"{name}_{label}", bufferView=len(doc["bufferViews"]) - 1,
                                      mimeType="image/jpeg"))
            doc["textures"].append(dict(sampler=0, source=len(doc["images"]) - 1))
            tex[label] = len(doc["textures"]) - 1
        new_mats.append(dict(
            name=name,
            pbrMetallicRoughness=dict(baseColorTexture=dict(index=tex["base"]),
                                      metallicRoughnessTexture=dict(index=tex["orm"]),
                                      metallicFactor=1.0, roughnessFactor=1.0),
            normalTexture=dict(index=tex["normal"], scale=1.0),
            occlusionTexture=dict(index=tex["orm"], strength=1.0)))
    doc["materials"] = new_mats
    binary += b"\0" * (-len(binary) % 4)
    doc["buffers"] = [dict(byteLength=len(binary))]
    doc["asset"]["generator"] = "tools/build_hero.py"
    write_glb(OUT_GLB, doc, binary)
    log("wrote", OUT_GLB, round(os.path.getsize(OUT_GLB) / 1e6, 1), "MB; materials", order)


# ------------------------------------------------------------------ the renders

def load_png(path):
    img = bpy.data.images.load(path, check_existing=False)
    a = np_from_image(img)[..., :3].copy()
    bpy.data.images.remove(img)
    return a                                  # bottom row first, stored values


def resize(a, w, h):
    ys = np.clip((np.arange(h) + 0.5) * a.shape[0] / h, 0, a.shape[0] - 1).astype(int)
    xs = np.clip((np.arange(w) + 0.5) * a.shape[1] / w, 0, a.shape[1] - 1).astype(int)
    return a[ys][:, xs]


def import_hero():
    """The delivered file, loaded the way any tool would load it."""
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=OUT_GLB)
    objs = {o.name: o for o in bpy.data.objects if o not in before and o.type == "MESH"}
    for name, _ in EXPORT_OBJECTS:
        if name not in objs:
            raise HeroError(f"the file has no mesh {name}")
    return objs


def jaw_attribute(obj):
    for name in ("_JAW", "_jaw", "JAW", "jaw"):
        if name in obj.data.attributes:
            return name
    return None


def open_jaw_imported(objs, degrees=JAW_OPEN_DEG):
    for name in ("hide", "mouth"):
        obj = objs[name]
        attr = jaw_attribute(obj)
        if attr is None:
            raise HeroError(f"{name}: the jaw weights did not survive the round trip")
        if attr != "jaw":
            obj.data.attributes[attr].name = "jaw"
        open_jaw(obj, degrees)


class Sheet:
    """Tiles rendered one by one and joined into one 1920x1080 image."""

    def __init__(self, sc, sun, cam, name, cols, rows=1):
        self.sc, self.sun, self.cam, self.name = sc, sun, cam, name
        self.w, self.h = 1920 // cols, 1080 // rows
        self.cols, self.rows, self.tiles = cols, rows, []

    def shot(self, eye, target, ortho=None, lens=50.0, sun_az=None):
        if sun_az is None:
            d = Vector(eye) - Vector(target)
            sun_az = math.degrees(math.atan2(d.y, d.x)) + 38.0
        aim_sun(self.sun, sun_az)
        aim_camera(self.cam, eye, target, ortho=ortho, lens=lens)
        path = cache(f"tile_{self.name}_{len(self.tiles)}.png")
        render_to(self.sc, path, self.w, self.h)
        self.tiles.append(load_png(path))

    def add(self, image):
        """A picture from elsewhere, fitted to the tile's width without stretching."""
        h = min(self.h, int(round(image.shape[0] * self.w / image.shape[1])))
        tile = np.full((self.h, self.w, 3), 0.25, np.float32)
        y0 = (self.h - h) // 2
        tile[y0:y0 + h] = resize(image, self.w, h)
        self.tiles.append(tile)

    def save(self):
        rows = []
        for r in range(self.rows):
            row = self.tiles[r * self.cols:(r + 1) * self.cols]
            while len(row) < self.cols:
                row.append(np.full((self.h, self.w, 3), 0.25, np.float32))
            rows.append(np.concatenate(row, axis=1))
        full = np.concatenate(rows[::-1], axis=0)
        os.makedirs(ART, exist_ok=True)
        write_png(os.path.join(ART, self.name + ".png"), to_u8(full))
        log("render", self.name)


def add_wire(obj, thickness):
    w = obj.modifiers.new("wire", "WIREFRAME")
    w.thickness = thickness
    w.use_even_offset = False       # an even offset turns a sliver triangle into a long spike
    w.use_replace = False
    w.material_offset = 1
    m = bpy.data.materials.new("wire")
    m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    b.inputs["Base Color"].default_value = (0.01, 0.01, 0.01, 1)
    obj.data.materials.append(m)


def render_all(only=None):
    reset_scene()
    sc, sun, cam = render_setup(samples=96)
    objs = import_hero()
    hero = list(objs.values())

    def want(name):
        return only is None or name in only

    if want("turnaround"):
        s = Sheet(sc, sun, cam, "turnaround", 3, 2)
        s.shot((40, -2.6, 3.3), (0, -2.6, 3.3), ortho=15.0)
        s.shot((0, 40, 3.6), (0, 0, 3.6), ortho=20.0)
        s.shot((0, -40, 3.6), (0, 0, 3.6), ortho=20.0)
        s.shot((0, -2.59, 40), (0, -2.6, 3), ortho=20.0, sun_az=20)
        s.shot((17, 17, 9), (0, -1.2, 3.4), lens=62)
        s.shot((-15, -19, 8), (0, -1.5, 3.4), lens=62)
        s.save()
    if want("head"):
        s = Sheet(sc, sun, cam, "head", 2)
        s.shot((9, 3.25, 4.85), (0, 3.25, 4.85), lens=120)
        s.shot((5.2, 8.6, 5.3), (0, 3.3, 4.8), lens=120)
        s.save()
    if want("eye"):
        s0, n, a1, a2, c = eye_frame(1)
        s = Sheet(sc, sun, cam, "eye", 1)
        view = (n + Vector((0, 0.25, -0.05))).normalized()
        s.shot(tuple(s0 + view * 3.0), tuple(s0), lens=250, sun_az=58)
        s.save()
    if want("seam"):
        s = Sheet(sc, sun, cam, "seam", 2)
        s.shot((7.5, 2.4, 4.3), (0, 2.4, 3.9), lens=110)
        s.shot((-4.5, 8.0, 4.6), (0, 2.5, 3.9), lens=110)
        s.save()
    if want("body"):
        s = Sheet(sc, sun, cam, "body", 3)
        s.shot((0.0, -0.81, 10.5), (0, -0.8, 3.5), lens=85, sun_az=20)
        s.shot((7.5, 3.4, 2.4), (0.6, 1.6, 1.9), lens=62, sun_az=40)
        s.shot((5.2, -8.2, 5.6), (0, -4.6, 2.2), lens=95, sun_az=-35)
        s.save()
    if want("graft"):
        s = Sheet(sc, sun, cam, "graft", 3)
        s.shot((3.2, 3.6, 7.2), (0.9, 0.8, 3.5), lens=70, sun_az=30)
        s.shot((4.2, 5.2, 3.0), (0.9, 1.0, 3.4), lens=75, sun_az=95)
        s.shot((3.6, -4.2, 5.4), (0.9, 0.3, 3.3), lens=75, sun_az=-30)
        s.save()
    if want("compare"):
        s = Sheet(sc, sun, cam, "compare", 3)
        design = load_png(DESIGN)
        h, w = design.shape[:2]
        s.add(design[int(h * 0.62):int(h * 0.985), int(w * 0.668):int(w * 0.985)])
        eye, target = (7.5, 7.6, 5.0), (0, 3.05, 4.75)
        s.shot(eye, target, lens=85, sun_az=75)
        for o in hero:
            o.hide_render = True
        sculpt = append_object("src2.blend", "head_src")
        s.shot(eye, target, lens=85, sun_az=75)
        bpy.data.objects.remove(sculpt)
        for o in hero:
            o.hide_render = False
        s.save()
    if want("wire"):
        add_wire(objs["hide"], 0.0025)
        add_wire(objs["membrane"], 0.004)
        add_wire(objs["horn"], 0.0025)
        s = Sheet(sc, sun, cam, "wire", 2)
        s.shot((6.5, 6.2, 5.2), (0, 3.4, 4.85), lens=120)
        s.shot((3.9, 0.3, 14), (3.9, 0.31, 4.6), lens=80, sun_az=30)
        s.save()
        for name in ("hide", "membrane", "horn"):
            objs[name].modifiers.clear()
    if want("mouth"):
        open_jaw_imported(objs)
        s = Sheet(sc, sun, cam, "mouth", 2)
        s.shot((4.6, 8.0, 4.4), (0, 3.75, 4.5), lens=115)
        s.shot((1.6, 9.4, 4.25), (0, 3.9, 4.45), lens=125, sun_az=80)
        s.save()


# ------------------------------------------------------------------ the verifier

TRI_BUDGET = (80000, 120000)
# Where the soles' centres must be, fore and hind, build frame. Body A: the first body sculpt's.
# Body B: its own, measured on the placed sculpt; its paws stand 0.49 m further forward and its
# hind feet 0.34 m further back, because its legs are bent.
OLD_FEET_Y = (2.828, -0.875) if BODY_B else (2.334, -0.538)
OLD_TAIL_TIP_Y = -9.734
SIZE_LIMIT = 60 * 1000 * 1000
HEAD_TRI_SHARE = 0.25
EYE_TURN_DEG = 25.0


def image_size(blob, mime):
    if mime == "image/png":
        return struct.unpack(">II", blob[16:24])
    i = 2
    while i < len(blob):
        if blob[i] != 0xFF:
            raise HeroError("bad JPEG stream")
        marker = blob[i + 1]
        length = struct.unpack(">H", blob[i + 2:i + 4])[0]
        if 0xC0 <= marker <= 0xCF and marker not in (0xC4, 0xC8, 0xCC):
            hgt, wid = struct.unpack(">HH", blob[i + 5:i + 9])
            return wid, hgt
        i += 2 + length
    raise HeroError("JPEG without a frame header")


def accessor(doc, binary, index):
    acc = doc["accessors"][index]
    view = doc["bufferViews"][acc["bufferView"]]
    dtype = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}[acc["componentType"]]
    width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[acc["type"]]
    if view.get("byteStride") not in (None, np.dtype(dtype).itemsize * width):
        raise HeroError("interleaved buffer views are not expected")
    off = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    a = np.frombuffer(binary, dtype=dtype, count=acc["count"] * width, offset=off)
    return a.reshape(acc["count"], width) if width > 1 else a


def to_build_frame(p):
    """glTF (X, Y up, -Z forward) to this file's frame (X, Y forward, Z up)."""
    return np.stack([p[:, 0], -p[:, 2], p[:, 1]], axis=1).astype(np.float64)


def rotate_about(points, centre, axis, degrees):
    m = np.array(Matrix.Rotation(math.radians(degrees), 3, Vector(axis)))
    return (points - centre) @ m.T + centre


def verify():
    fails, notes = [], []

    def check(cond, message):
        (notes if cond else fails).append(("ok   " if cond else "FAIL ") + message)

    if not os.path.exists(OUT_GLB):
        raise HeroError(f"{OUT_GLB} does not exist")
    size = os.path.getsize(OUT_GLB)
    check(size <= SIZE_LIMIT, f"file size {size / 1e6:.1f} MB, limit {SIZE_LIMIT / 1e6:.0f} MB")
    doc, binary = read_glb(OUT_GLB)
    check("KHR_draco_mesh_compression" not in doc.get("extensionsUsed", []), "no Draco")
    check(not doc.get("skins"), "no skin: the file is unrigged")

    mats = [m["name"] for m in doc["materials"]]
    check(mats == list(MATERIALS), f"five materials {mats}")
    for m in doc["materials"]:
        pbr = m.get("pbrMetallicRoughness", {})
        ok = all(k in pbr for k in ("baseColorTexture", "metallicRoughnessTexture")) and "normalTexture" in m \
            and "occlusionTexture" in m
        check(ok, f"{m['name']}: base colour, normal and occlusion-roughness-metallic maps")
        if not ok:
            continue
        check(m["occlusionTexture"]["index"] == pbr["metallicRoughnessTexture"]["index"],
              f"{m['name']}: occlusion and roughness share one packed map")
        check("baseColorFactor" not in pbr, f"{m['name']}: no baseColorFactor, the colour is in the texture")
        for tex in (pbr["baseColorTexture"], m["normalTexture"], pbr["metallicRoughnessTexture"]):
            img = doc["images"][doc["textures"][tex["index"]]["source"]]
            view = doc["bufferViews"][img["bufferView"]]
            blob = binary[view.get("byteOffset", 0):view.get("byteOffset", 0) + view["byteLength"]]
            w, h = image_size(blob, img["mimeType"])
            check((w, h) == (MAP_SIZE[m["name"]],) * 2, f"{img['name']}: {w}x{h}")

    geo = {}
    for node in doc["nodes"]:
        if "mesh" not in node:
            continue
        check(all(k not in node for k in ("rotation", "scale", "matrix")) and
              all(abs(x) < 1e-6 for x in node.get("translation", [0, 0, 0])), f"{node['name']}: node has no transform")
        mesh = doc["meshes"][node["mesh"]]
        prim = mesh["primitives"][0]
        check(len(mesh["primitives"]) == 1 and prim.get("mode", 4) == 4 and "indices" in prim,
              f"{mesh['name']}: one indexed triangle primitive")
        attrs = prim["attributes"]
        check(all(k in attrs for k in ("POSITION", "NORMAL", "TANGENT", "TEXCOORD_0")),
              f"{mesh['name']}: POSITION, NORMAL, TANGENT, TEXCOORD_0")
        pos = to_build_frame(accessor(doc, binary, attrs["POSITION"]))
        idx = accessor(doc, binary, prim["indices"]).astype(np.int64).reshape(-1, 3)
        tan = accessor(doc, binary, attrs["TANGENT"])
        check(bool(np.all(np.abs(np.abs(tan[:, 3]) - 1) < 1e-3)) and
              float(np.mean(np.abs(np.linalg.norm(tan[:, :3], axis=1) - 1) < 1e-2)) > 0.995,
              f"{mesh['name']}: tangents are unit vectors with a sign")
        jaw = accessor(doc, binary, attrs["_JAW"]) if "_JAW" in attrs else None
        geo[mesh["name"]] = dict(pos=pos, idx=idx, material=mats[prim["material"]], jaw=jaw)
    want = dict(EXPORT_OBJECTS)
    check(sorted(geo) == sorted(want), f"meshes {sorted(geo)}")
    for name, g in geo.items():
        check(g["material"] == want.get(name), f"{name} uses material {g['material']}")
    if fails:
        for line in notes + fails:
            print("[verify]", line)
        raise HeroError(f"{len(fails)} checks failed")

    skin = ("hide", "membrane", "horn")
    tris = {n: len(geo[n]["idx"]) for n in geo}
    total = sum(tris[n] for n in skin)
    check(TRI_BUDGET[0] <= total <= TRI_BUDGET[1],
          f"hide {tris['hide']} + membrane {tris['membrane']} + horn {tris['horn']} = {total} triangles, "
          f"budget {TRI_BUDGET[0]} to {TRI_BUDGET[1]}")
    head_tris = 0
    for n in skin:
        c = geo[n]["pos"][geo[n]["idx"]].mean(axis=1)
        head_tris += int(((c[:, 1] > 1.6) & (c[:, 2] > 3.584) & (np.abs(c[:, 0]) < 1.3)).sum())
    check(head_tris >= HEAD_TRI_SHARE * total, f"head and collar: {head_tris} triangles, {head_tris / total:.1%} of the dragon")

    allp = np.concatenate([g["pos"] for g in geo.values()])
    span = allp[:, 0].max() - allp[:, 0].min()
    check(abs(span - WINGSPAN) < 0.02, f"wingspan {span:.3f} m")
    check(abs(allp[:, 2].min()) < 0.01, f"feet at Y = {allp[:, 2].min():.4f}")
    eye_y = geo["eye_r"]["pos"][:, 1].mean()
    check(eye_y > 3.0 and allp[:, 1].min() < -9.0 and geo["eye_r"]["pos"][:, 0].mean() > 0,
          f"forward is -Z: the eyes are {eye_y:.2f} m ahead of the origin, the tail tip {-allp[:, 1].min():.2f} m behind")

    # The frame is the first build's: the rig was fitted there.
    low = allp[(allp[:, 2] < 0.25) & (np.abs(allp[:, 0]) < 2.2)]
    fore_y, hind_y = low[low[:, 1] > 1.0][:, 1].mean(), low[low[:, 1] <= 1.0][:, 1].mean()
    check(abs(fore_y - OLD_FEET_Y[0]) < 0.1 and abs(hind_y - OLD_FEET_Y[1]) < 0.1,
          f"feet where the first build's were: fore {-fore_y:.2f}, hind {-hind_y:.2f} in Z "
          f"(first build {-OLD_FEET_Y[0]:.2f}, {-OLD_FEET_Y[1]:.2f})")
    check(abs(allp[:, 1].min() - OLD_TAIL_TIP_Y) < 0.1, f"tail tip at Z {-allp[:, 1].min():.2f} (first build {-OLD_TAIL_TIP_Y:.2f})")

    # Eyes.
    others = [n for n in geo if not n.startswith("eye")]
    occ_pos = np.concatenate([geo[n]["pos"] for n in others])
    clear = EYE_R + EYE_CORNEA
    hide_tree = BVHTree.FromPolygons([Vector(p) for p in geo["hide"]["pos"]], [tuple(int(i) for i in t) for t in geo["hide"]["idx"]])
    for name, side in (("eye_r", 1), ("eye_l", -1)):
        s0, n, a1, a2, c = eye_frame(side)
        cen = np.array(c)
        d = np.linalg.norm(geo[name]["pos"] - cen, axis=1)
        check(d.min() > EYE_R - 1e-4 and d.max() < clear + 1e-4,
              f"{name}: a sphere of radius {EYE_R * 1000:.0f} mm, cornea bulge {(d.max() - EYE_R) * 1000:.1f} mm")
        nearest = np.linalg.norm(occ_pos - cen, axis=1).min()
        check(nearest > clear + 5e-4,
              f"{name}: nothing else comes within the ball's reach (nearest vertex {(nearest - clear) * 1000:.1f} mm outside it)")
        worst = 9.0
        for axis in (a2, a1):
            for deg in (-EYE_TURN_DEG, EYE_TURN_DEG):
                turned = rotate_about(geo[name]["pos"], cen, axis, deg)
                for p in turned[::3]:
                    worst = min(worst, hide_tree.find_nearest(Vector(p))[3])
        check(worst > 5e-4, f"{name}: turned {EYE_TURN_DEG:.0f} degrees each way about two axes, "
                            f"the ball stays {worst * 1000:.1f} mm clear of the lids")
        hit = hide_tree.ray_cast(s0 + n * 0.5, -n)
        check(hit[0] is None or hit[3] > 0.5, f"{name}: the lids are open over the pupil")

    # Mouth.
    occ_v, occ_t, base = [], [], 0
    for nme in ("hide", "horn", "membrane", "eye_l", "eye_r"):
        occ_v += [Vector(p) for p in geo[nme]["pos"]]
        occ_t += [tuple(int(i) + base for i in t) for t in geo[nme]["idx"]]
        base += len(geo[nme]["pos"])
    tree = BVHTree.FromPolygons(occ_v, occ_t)
    mouth = geo["mouth"]["pos"]
    inner = np.array([tree.find_nearest(Vector(p))[3] > 0.001 for p in mouth])
    centre = Vector((0, 3.95, 4.6))
    views = []
    for el in (-60, -30, 0, 30, 60):
        for az in range(0, 360, 30):
            e, a = math.radians(el), math.radians(az)
            out = Vector((math.cos(a) * math.cos(e), math.sin(a) * math.cos(e), math.sin(e)))
            # a viewpoint must be in the open air, not inside the chest
            if tree.ray_cast(centre + out * 4.0, out)[0] is None:
                views.append(centre + out * 4.0)

    def seen(points, occluder, report=0):
        count = 0
        for p in points:
            p = Vector(p)
            for v in views:
                d = p - v
                dist = d.length
                hit = occluder.ray_cast(v, d / dist, dist - 0.0015)
                if hit[0] is None:
                    count += 1
                    if count <= report:
                        print("[verify]      seen at", p.to_tuple(3), "from", (v - centre).normalized().to_tuple(2))
                    break
        return count

    shown = seen(mouth[inner], tree, report=12)
    check(shown == 0, f"mouth closed: {shown} of {int(inner.sum())} interior vertices can be seen from {len(views)} directions")
    check(geo["hide"]["jaw"] is not None and geo["mouth"]["jaw"] is not None, "hide and mouth carry the _JAW weights")
    if geo["hide"]["jaw"] is not None and geo["mouth"]["jaw"] is not None:
        def swing(pos, w):
            a = -np.radians(JAW_OPEN_DEG) * w
            out = pos.copy()
            y, z = pos[:, 1] - JAW_HINGE[0], pos[:, 2] - JAW_HINGE[1]
            out[:, 1] = JAW_HINGE[0] + y * np.cos(a) - z * np.sin(a)
            out[:, 2] = JAW_HINGE[1] + y * np.sin(a) + z * np.cos(a)
            return out
        hide_open = swing(geo["hide"]["pos"], geo["hide"]["jaw"])
        mouth_open = swing(mouth, geo["mouth"]["jaw"])
        open_tree = BVHTree.FromPolygons([Vector(p) for p in hide_open], [tuple(int(i) for i in t) for t in geo["hide"]["idx"]])
        shown = seen(mouth_open[inner][::4], open_tree)
        check(shown > 100, f"jaw open {JAW_OPEN_DEG:.0f} degrees: {shown} of {int(inner.sum()) // 4 + 1} sampled interior vertices show")
        drop = float((mouth - mouth_open)[:, 2].max())
        check(drop > 0.3, f"jaw open: the chin drops {drop:.2f} m")

    check(os.path.exists(OUT_THICK), "membrane thickness map exists")
    if os.path.exists(OUT_THICK):
        with open(OUT_THICK, "rb") as f:
            head = f.read(26)
        w, h = struct.unpack(">II", head[16:24])
        check((w, h) == (MAP_SIZE["membrane"],) * 2 and head[25] == 0, f"thickness map {w}x{h}, one channel")

    for line in notes + fails:
        print("[verify]", line)
    if fails:
        raise HeroError(f"{len(fails)} checks failed")
    print(f"[verify] all {len(notes)} checks passed")


# ---------------------------------------------------------------- the landmarks

def landmarks():
    """Measure the same landmarks on the first body sculpt and on version 2's
    sources, and write them as a table in the file's coordinates (glTF: +Y
    up, -Z forward). The rig was fitted to the first; this is its refit."""
    reset_scene()
    old = coords(append_object("src_aligned.blend", "body_src"))
    new = coords(append_object("src2.blend", "body2_src"))
    wing = coords(append_object("wing5.blend" if WING_V5 else "wing2.blend" if WING_V3 else "src2.blend", "wing_src"))
    rows = []

    def gltf(p):
        return (p[0], p[2], -p[1])

    def add(name, a, b):
        a, b = gltf(a), gltf(b)
        rows.append((name, a, b, tuple(y - x for x, y in zip(a, b))))

    def foot(co, fore):
        low = co[(co[:, 2] < 0.25) & (co[:, 0] > 0) & (co[:, 0] < 2.2) & ((co[:, 1] > 1.0) == fore)]
        return low.mean(axis=0)

    def leg_x(co, y, z, fore):
        y0, y1 = (1.2, 3.1) if fore else (-1.7, 0.9)
        sl = co[(np.abs(co[:, 2] - z) < 0.03) & (co[:, 1] > y0) & (co[:, 1] < y1) & (co[:, 0] > 0.30) & (co[:, 0] < 1.75)
                & (np.abs(co[:, 1] - y) < 0.45)]
        return (sl[:, 0].min() + sl[:, 0].max()) / 2 if len(sl) else float("nan")

    def centre(co, y):
        st = co[(np.abs(co[:, 0]) < 0.15) & (np.abs(co[:, 1] - y) < 0.125)]
        return np.array([0.0, y, st[:, 2].min()])

    new_joint = {j[0]: (j[1], j[2]) for j in LEG_JOINTS}
    for name in ("fore_shoulder", "fore_elbow", "fore_wrist", "fore_foot", "hind_hip", "hind_knee", "hind_hock",
                 "hind_foot"):
        fore = name.startswith("fore")
        (oy, oz), (ny, nz) = LEG_JOINTS_V1[name], new_joint[name]
        add(name.replace("_", " ") + " joint", (leg_x(old, oy, oz, fore), oy, oz), (leg_x(new, ny, nz, fore), ny, nz))
    add("fore foot, centre of the sole", foot(old, True), foot(new, True))
    add("hind foot, centre of the sole", foot(old, False), foot(new, False))
    for name, x, y, _, _, _ in WING_JOINTS[:3]:
        z_old = old[(np.abs(old[:, 0] - x) < 0.1) & (np.abs(old[:, 1] - y) < 0.1) & (old[:, 2] > 2.9)][:, 2]
        z_new = wing[(np.abs(wing[:, 0] - x) < 0.1) & (np.abs(wing[:, 1] - y) < 0.1) & (wing[:, 2] > 2.9)][:, 2]
        add(name.replace("_", " "), (x, y, (z_old.min() + z_old.max()) / 2), (x, y, (z_new.min() + z_new.max()) / 2))
    tip_o, tip_n = wing[wing[:, 0].argmax()], wing[wing[:, 0].argmax()]
    add("wing tip", old[old[:, 0].argmax()], tip_n)
    add("tail root, belly", centre(old, -1.5), centre(new, -1.5))
    add("tail middle, belly", centre(old, -5.5), centre(new, -5.5))
    add("tail tip", old[old[:, 1].argmin()], new[new[:, 1].argmin()])
    add("belly between the legs", centre(old, 0.7), centre(new, 0.7))
    lines = ["| Landmark (right side) | Old X, Y, Z | New X, Y, Z | Difference |", "|---|---|---|---|"]
    for name, a, b, d in rows:
        f = lambda t: ", ".join(f"{v:.2f}" for v in t)
        lines.append(f"| {name} | {f(a)} | {f(b)} | {', '.join(f'{v:+.2f}' for v in d)} |")
    text = "\n".join(lines)
    with open(cache("landmarks.md"), "w") as fh:
        fh.write(text + "\n")
    print(text)


STAGES = [("import", stage_import), ("wing", stage_wing), ("align", stage_align), ("low_parts", stage_low_parts),
          ("align2", stage_align2), ("wing5", stage_wing5), ("low2", stage_low2), ("join", stage_join), ("head", stage_head), ("parts", stage_parts), ("uv", stage_uv),
          ("bake", stage_bake), ("maps", stage_maps), ("export", stage_export)]


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if "--verify-only" in argv:
        verify()
        return
    if "--landmarks" in argv:
        landmarks()
        return
    if "--render" in argv:
        i = argv.index("--render")
        render_all(argv[i + 1].split(",") if len(argv) > i + 1 and not argv[i + 1].startswith("--") else None)
        return
    if "--stage" in argv:
        want = argv[argv.index("--stage") + 1].split(",")
        table = dict(STAGES)
        for s in want:
            log("stage", s)
            table[s]()
        return
    for name, fn in STAGES:
        if name == "wing" and not WING_V3:
            continue
        if name == "wing5" and not WING_V5:
            continue
        log("stage", name)
        fn()
    verify()


if __name__ == "__main__":
    try:
        main()
    except Exception:
        import traceback
        traceback.print_exc()
        sys.exit(1)
