"""Build the hero dragon's static mesh from two generated sculpts.

    blender -b --factory-startup --python tools/build_hero.py
    blender -b --factory-startup --python tools/build_hero.py -- --verify-only
    blender -b --factory-startup --python tools/build_hero.py -- --render
    blender -b --factory-startup --python tools/build_hero.py -- --stage low,parts

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
    for src, name in ((BODY_IN, "body_src"), (HEAD_IN, "head_src")):
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


def stage_low_parts():
    """Cut the body sculpt's head off, and reduce each sculpt."""
    open_blend("src_aligned.blend")
    body = duplicate(bpy.data.objects["body_src"], "body_low")
    head = duplicate(bpy.data.objects["head_src"], "head_low")
    for o in (bpy.data.objects["body_src"], bpy.data.objects["head_src"]):
        bpy.data.objects.remove(o)
    for o in (body, head):
        strip_layers(o)
        weld(o)
        topo_report(o, o.name + " welded")

    # The body loses everything above the neck cut: its own head.
    bm = bmesh.new()
    bm.from_mesh(body.data)
    box = [f for f in bm.faces if all(v.co.y > 1.5 and abs(v.co.x) < 1.4 and v.co.z > NECK_CUT_Z - 0.5
                                      for v in f.verts)]
    geom = set(box)
    for f in box:
        geom.update(f.edges)
        geom.update(f.verts)
    r = bmesh.ops.bisect_plane(bm, geom=list(geom), dist=1e-6, plane_co=(0, 0, NECK_CUT_Z),
                               plane_no=(0, 0, 1), clear_outer=False, clear_inner=False)
    kill = [v for v in bm.verts if v.co.z > NECK_CUT_Z + 1e-5 and v.co.y > 1.5 and abs(v.co.x) < 1.4]
    bmesh.ops.delete(bm, geom=kill, context="VERTS")
    edges = [e for e in bm.edges if e.is_boundary and all(abs(v.co.z - NECK_CUT_Z) < 1e-4 for v in e.verts)]
    log("neck cut: boundary edges", len(edges))
    bmesh.ops.holes_fill(bm, edges=edges, sides=0)
    bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 3])
    bm.to_mesh(body.data)
    bm.free()
    topo_report(body, "body cut")

    decimate_to(body, BODY_TRIS)
    topo_report(body, "body decimated")
    decimate_to(head, HEAD_TRIS)
    topo_report(head, "head decimated")
    save_blend("low_parts.blend")


def stage_join():
    """One closed surface: the union of the two reduced sculpts."""
    open_blend("low_parts.blend")
    body = bpy.data.objects["body_low"]
    head = bpy.data.objects["head_low"]
    # Material slots carry the origin of every face through the union.
    for obj, name in ((body, "from_body"), (head, "from_head")):
        obj.data.materials.append(bpy.data.materials.new(name))
    mod = body.modifiers.new("union", "BOOLEAN")
    mod.operation = "UNION"
    mod.object = head
    mod.solver = "EXACT"
    mod.material_mode = "TRANSFER"
    apply_modifier(body, mod)
    bpy.data.objects.remove(head)
    body.name = "low"
    body.data.name = "low"
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
    bm.to_mesh(body.data)
    bm.free()
    nb, nm = topo_report(body, "joined")
    names = [m.name for m in body.data.materials]
    n_head = sum(1 for p in body.data.polygons if names[p.material_index] == "from_head")
    log("triangles from the head sculpt", n_head, "share", round(n_head / len(body.data.polygons), 3))
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
WING_JOINTS = [
    ("wing_shoulder", 1.25, 1.10, 0.90, -0.45, 0.75),
    ("wing_elbow", 2.64, 0.45, 1.00, 0.00, 0.60),
    ("wing_wrist", 3.43, 0.71, 1.00, 0.00, 0.60),
    ("finger1_a", 6.31, 0.45, 0.97, -0.20, 0.24),
    ("finger1_b", 7.95, -0.07, 0.92, -0.38, 0.22),
    ("finger2_a", 5.85, -0.37, 0.90, -0.44, 0.22),
    ("finger3_a", 4.74, -0.63, 0.60, -0.80, 0.22),
    ("finger4_a", 2.74, -0.43, 0.10, -1.00, 0.22),
]
# Leg joints: (name, Y, Z, direction Y, direction Z, radius). The planes
# contain the X axis. Both sides are cut.
LEG_JOINTS = [
    ("hind_hip", -0.05, 2.25, 0.30, -1.00, 0.75),
    ("hind_knee", 0.00, 1.83, -0.50, -0.87, 0.60),
    ("hind_hock", -0.96, 1.43, -0.50, -0.86, 0.45),
    ("hind_foot", -0.84, 0.45, 0.00, -1.00, 0.42),
    ("fore_shoulder", 1.84, 2.30, -0.20, -1.00, 0.60),
    ("fore_elbow", 1.72, 1.92, 0.10, -1.00, 0.50),
    ("fore_wrist", 2.04, 0.98, 0.30, -1.00, 0.42),
    ("fore_foot", 2.25, 0.40, 0.00, -1.00, 0.42),
]
JOINT_LOOP_SPACING = 0.10
MEMBRANE_MAX_THICKNESS = 0.135      # the sculpt's membrane is 70 to 130 mm thick, its bones 150 mm and up
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


def cut_joint_loops(bm):
    made = 0
    for name, x, y, dx, dy, r in WING_JOINTS:
        for side in (1, -1):
            c = Vector((x * side, y, 0))
            n = Vector((dx * side, dy, 0)).normalized()
            for k in ((-0.5, 0.5) if name.startswith("finger") else (-1, 0, 1)):
                pc = c + n * (k * JOINT_LOOP_SPACING)
                faces = [f for f in bm.faces
                         if all((Vector((v.co.x, v.co.y, 0)) - c).length < r and v.co.z > 3.0 for v in f.verts)]
                if faces:
                    bmesh.ops.bisect_plane(bm, geom=face_geom(faces), dist=1e-5, plane_co=pc, plane_no=n)
                    made += 1
    for name, y, z, dy, dz, r in LEG_JOINTS:
        c = Vector((0, y, z))
        n = Vector((0, dy, dz)).normalized()
        for k in (-1, 0, 1):
            pc = c + n * (k * JOINT_LOOP_SPACING)
            faces = [f for f in bm.faces
                     if all((Vector((0, v.co.y, v.co.z)) - c).length < r and 0.25 < abs(v.co.x) < 1.7
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
    thick_layer = bm.verts.layers.float.new("thick")
    cut_joint_loops(bm)
    bmesh.ops.dissolve_degenerate(bm, dist=2e-4, edges=bm.edges)
    bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 3])
    bm.verts.ensure_lookup_table()
    bm.faces.ensure_lookup_table()
    bm.normal_update()
    tree = BVHTree.FromBMesh(bm)

    # Membrane: the thin sheet of the wing.
    down = Vector((0, 0, -1))
    for v in bm.verts:
        t = 9.0
        if abs(v.co.x) > WING_MIN_X - 0.3 and v.co.z > 2.0:
            hit = tree.ray_cast(v.co - v.normal * 0.004, -v.normal)
            if hit[0] is not None:
                t = hit[3] + 0.004
        v[thick_layer] = t

    def near_claw(c):
        return any((abs(c.x) - x) ** 2 + (c.y - y) ** 2 < r * r for x, y, r in WING_CLAWS)

    memb = {}
    for f in bm.faces:
        c = f.calc_center_median()
        thin = sum(1 for v in f.verts if v[thick_layer] < MEMBRANE_MAX_THICKNESS)
        memb[f] = thin >= 2 and abs(c.x) > WING_MIN_X and c.z > 2.0 and not near_claw(c)
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
        toe = c.z < 0.30 and abs(c.x) < 2.0 and (c.y > 2.42 or -0.32 < c.y < 0.2)
        if toe or (near_claw(c) and c.z > 2.0 and not memb[f]):
            horn[f] = True
    log("faces: membrane", sum(memb.values()), "horn", n_horn, "claw", sum(horn.values()) - n_horn)

    # Thin the membrane: both faces of the sheet move toward its middle.
    moved = 0
    for v in bm.verts:
        if v.link_faces and all(memb[f] for f in v.link_faces) and v[thick_layer] < MEMBRANE_MAX_THICKNESS:
            v.co -= v.normal * (v[thick_layer] * MEMBRANE_THIN)
            moved += 1
    log("membrane vertices thinned:", moved)

    for f in bm.faces:
        f[src_layer] = 1 if f.material_index == head_slot else 0
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
HEAD_UV_SHARE = 0.30                # of the hide atlas's used area


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


def unwrap(obj, margin, head_share=None):
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
        bpy.ops.uv.smart_project(angle_limit=math.radians(UV_ANGLE), island_margin=0.0, area_weight=0.0,
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
        me.uv_layers[0].data.foreach_set("uv", uv.ravel())
        log(obj.name, "head surface share of UV before", round(float(a), 3), "scale", round(k, 2))
    activate(obj)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.pack_islands(rotate=True, margin=margin, margin_method="FRACTION", shape_method="CONCAVE")
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
        unwrap(obj, 0.0005 if name == "hide" else 0.002, HEAD_UV_SHARE if name == "hide" else None)
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


def finish_maps(name, base, normal, rough, ao, valid):
    """Dilate, clamp and write one material's three maps. Returns the paths."""
    mean_r, lo, hi = ROUGH[name]
    r = rough.copy()
    r = np.clip(mean_r + (r - r[valid].mean()) * 0.6, lo, hi)
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
    body_src = append_object("src_aligned.blend", "body_src")
    head_src = append_object("src_aligned.blend", "head_src")
    src = {}
    for o in (body_src, head_src):
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
        pos = bake_image(sc, obj, "position", size)[..., :3]
        onrm = bake_image(sc, obj, "object_normal", size)[..., :3] * 2.0 - 1.0
        onrm /= np.maximum(np.linalg.norm(onrm, axis=2, keepdims=True), 1e-6)
        out = {}
        for kind in ("diff", "orm", "normal"):
            acc = np.zeros((size, size, 3), np.float32)
            for sname, region in (("body_src", valid & ~head), ("head_src", head)):
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
        ao = None
        if name == "hide":
            ao = bake_image(sc, obj, "ao", size // 2, samples=12)[..., 0]
        thick = None
        if name == "membrane":
            thick = bake_image(sc, obj, "thick", size, attribute="thickmap")[..., 0]
        np.savez(cache("raw_" + name + ".npz"), diff=out["diff"].astype(np.float16),
                 rough=out["orm"][..., 1].astype(np.float16), normal=out["normal"].astype(np.float16),
                 pos=pos.astype(np.float16), onrm=onrm.astype(np.float16), hit=out["hit"], valid=valid,
                 head=head, ao=np.zeros(1) if ao is None else ao.astype(np.float16),
                 thick=np.zeros(1) if thick is None else thick.astype(np.float16))
        log("raw bake saved for", name)


def stage_maps():
    """Grade the raw bakes and write each part's three maps."""
    reset_scene()
    stats = {}
    for name in ("horn", "membrane", "hide"):
        raw = np.load(cache("raw_" + name + ".npz"))
        valid, head, hit = raw["valid"], raw["head"], raw["hit"]
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
        for label, region in (("body", valid & ~head), ("head", head)):
            if region.sum() > 1000:
                _, b = delight(base, onrm, region)
                stats[f"{name}_{label}_light_xyz"] = b
                base[region] = (base * np.exp(-(onrm[..., 0] * b[0]))[..., None])[region]
                log(name, label, "luminance against direction (x, y, z):", b, "- removed x")
        if name == "hide":
            # The head sculpt's neck takes the body sculpt's colour where
            # they meet: one gain for the dark back, one for the pale front.
            z, y = pos[..., 2], pos[..., 1]
            gains = []
            for lo_y, hi_y in ((0.0, 2.25), (2.75, 9.0)):
                hb = head & (z < 4.2) & (y > lo_y) & (y < min(hi_y, 3.3))
                bb = valid & ~head & (z > 3.3) & (z < 3.95) & (y > max(lo_y, 1.7)) & (y < hi_y) \
                    & (np.abs(pos[..., 0]) < 0.7)
                g = np.exp(np.log(base[bb]).mean(axis=0) - np.log(base[hb]).mean(axis=0))
                gains.append(np.clip(g, 0.7, 1.45))
            stats["neck_gain_back_front"] = [[round(float(c), 3) for c in g] for g in gains]
            log("neck colour gains (back, front):", stats["neck_gain_back_front"])
            f = smoothstep((y - 2.3) / 0.4)[..., None]
            gain = gains[0] * (1 - f) + gains[1] * f
            w = (1.0 - NECK_MATCH_KEEP * smoothstep((z - 4.0) / 0.9))[..., None]
            base[head] = (base * gain ** w)[head]
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
            ao = raw["ao"].astype(np.float32)
            ao = np.repeat(np.repeat(ao, 2, axis=0), 2, axis=1)
            ao = np.clip(0.35 + 0.65 * ao, 0, 1)
        else:
            ao = np.ones((size, size), np.float32)
        base = np.clip(base, 0.0, 1.0)
        finish_maps(name, base, tn, rough[..., 0], ao, valid)
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
MOUTH_ROUGH = (0.35, 0.45, 0.25)    # gums, tongue, teeth


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
    ao = np.clip(1.0 - 0.6 * depth * (k == 0), 0.3, 1.0).astype(np.float32)
    orm = np.stack([ao, rough, np.zeros_like(rough)], axis=-1)
    base, v2 = dilate_fill(base, valid, 8)
    base[~v2] = (0.2, 0.03, 0.03)
    orm, v2 = dilate_fill(orm, valid, 8)
    orm[~v2] = (1.0, 0.35, 0.0)
    nrm = np.zeros((size, size, 3))
    nrm[..., :2] = 0.5
    nrm[..., 2] = 1.0
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
        keep = {"jaw": "_JAW"} if oname in ("hide", "mouth") else {}
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
    if want("compare"):
        s = Sheet(sc, sun, cam, "compare", 3)
        design = load_png(DESIGN)
        h, w = design.shape[:2]
        s.add(design[int(h * 0.62):int(h * 0.985), int(w * 0.668):int(w * 0.985)])
        eye, target = (7.5, 7.6, 5.0), (0, 3.05, 4.75)
        s.shot(eye, target, lens=85, sun_az=75)
        for o in hero:
            o.hide_render = True
        sculpt = append_object("src_aligned.blend", "head_src")
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


STAGES = [("import", stage_import), ("align", stage_align), ("low_parts", stage_low_parts),
          ("join", stage_join), ("head", stage_head), ("parts", stage_parts), ("uv", stage_uv),
          ("bake", stage_bake), ("maps", stage_maps), ("export", stage_export)]


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if "--verify-only" in argv:
        verify()
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
