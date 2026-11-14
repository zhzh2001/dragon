"""Rebuild Embercrest's hide textures: a warmer, lighter, more readable dragon.

Usage:
    /opt/homebrew/bin/python3.14 tools/hide/build_hide.py \
        [SHIPPED.glb] [OUT.glb]

Defaults: assets/embercrest-prehide.glb (the Hunyuan textures as first
shipped, kept locally) and out/embercrest-hide.glb. Copy the output over
assets/embercrest.glb to install it. The script needs NumPy and Pillow, and nothing else.
It is deterministic: the same input gives the same bytes.

The script changes the three images and nothing else. `check_mesh.py` proves
that.

What was wrong. The shipped base colour is dark: the back's median albedo is
0.03 linear (sRGB 59,46,37), and it is close to grey. The engine multiplies
the sun by the albedo before the tonemap, so the back rendered near black and
the scales, plates and membrane sat within one stop of each other.

What the script does, per texel, in linear light:

1. Splits the albedo's luminance into a base (a wide blur of its logarithm)
   and a detail ratio (the scales, plate edges, wrinkles and veins).
2. Lifts the base through a power curve, which raises dark hide far more than
   light plate, and puts the detail back raised to a power above 1. The lift
   alone would flatten the scales; the power keeps them and separates them.
3. Moves the hue toward rust. Ridges go toward a lighter orange and creases
   toward a deep red-brown, so a scale reads by hue as well as by value.
4. Treats four regions differently, found from the mesh and not painted:
   the membrane (skin weights on the wing fingers, as the engine's
   `App::mark_membranes` does) is lifted more and tinted orange; the belly
   plates (already light in the texture) keep their value and warm a little;
   the horns and the claws (bind-pose position on the head and toes) stay
   dark.
5. Normal map: scales x and y by NORMAL_STRENGTH and renormalizes. The
   convention does not change.
6. Metallic-roughness image: lowers roughness a little on scale ridges so
   they take a highlight from the low sun, and softens the red channel. The
   engine reads red as ambient occlusion (`shaders/skinned.hlsl`), and the
   generator's red channel went as low as 0.3 on the head and back.
"""

import io
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import glbio  # noqa: E402
import meshdata  # noqa: E402
import uvmaps  # noqa: E402

Image.MAX_IMAGE_PIXELS = None

SHIPPED = "assets/embercrest-prehide.glb"
OUTPUT = "out/embercrest-hide.glb"

LUMA = np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)

# --- The dials. Every one was set from an engine render (out/REPORT.md). ---

# The lift: new base luminance = LIFT_GAIN * base ** LIFT_POWER.
LIFT_GAIN = 0.78
LIFT_POWER = 0.55
# The detail ratio is raised to this power: above 1 separates the scales.
DETAIL_POWER_BODY = 1.60
DETAIL_POWER_MEMBRANE = 1.20
# The blur that separates base from detail, in texels of the 4096 atlas.
DETAIL_RADIUS = 14
# The membrane is lifted this much more than the body.
MEMBRANE_GAIN = 1.50
# Horns and claws: a plain gain, no lift curve.
HORN_GAIN = 1.25
# Albedo luminance never leaves this range.
ALBEDO_MIN = 0.004
ALBEDO_MAX = 0.80
# The belly plates get this gain on top of the lift.
BELLY_GAIN = 1.30

# Tints, as sRGB bytes. Only their hue and saturation are used: each is
# normalized to luminance 1 and multiplied by the new luminance.
TINT_CREASE = (110, 46, 30)     # deep red-brown, in the creases
TINT_BODY = (172, 94, 60)       # rust
TINT_RIDGE = (216, 140, 88)     # orange, on the ridges
TINT_MEMBRANE = (200, 116, 70)   # the membrane: more orange-red than the body
TINT_BELLY = (236, 192, 142)    # warm cream
TINT_HORN = (70, 52, 44)        # dark warm grey
# How far the hue moves from the texture's own toward the tint, 0 to 1.
TINT_STRENGTH_BODY = 0.66
TINT_STRENGTH_MEMBRANE = 0.62
TINT_STRENGTH_BELLY = 0.60
TINT_STRENGTH_HORN = 0.35
# A texel is belly plate when its base luminance is between these (linear).
BELLY_FROM = 0.085
BELLY_TO = 0.19

NORMAL_STRENGTH = 1.5
ROUGHNESS_RIDGE_DROP = 0.22   # the share of roughness a full ridge loses
ROUGHNESS_MEMBRANE = 1.0      # the membrane's roughness is not changed
OCCLUSION_KEEP = 0.55         # 1 keeps the red channel, 0 makes it white


def srgb_to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4).astype(np.float32)


def linear_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(c, 1.0 / 2.4) - 0.055).astype(np.float32)


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return (t * t * (3.0 - 2.0 * t)).astype(np.float32)


def chroma(srgb_bytes):
    """A tint at luminance 1, in linear RGB."""
    linear = srgb_to_linear(np.array(srgb_bytes, dtype=np.float32) / 255.0)
    return linear / float(linear @ LUMA)


def decode(doc, blob, index):
    image = Image.open(io.BytesIO(glbio.image_bytes(doc, blob, index)))
    if image.mode != "RGB":
        raise ValueError(f"image {index} is {image.mode}, expected RGB")
    return np.asarray(image, dtype=np.float32) / 255.0


def encode(array):
    data = (np.clip(array, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    buffer = io.BytesIO()
    Image.fromarray(data, "RGB").save(buffer, format="PNG", compress_level=6)
    return buffer.getvalue()


def region_maps(doc, blob, size):
    """Membrane, horn and claw shares per texel, from the mesh."""
    mesh = meshdata.primitive(doc, blob)
    names = meshdata.joint_names(doc)
    weights = mesh["WEIGHTS_0"].astype(np.float32)
    joints = mesh["JOINTS_0"]
    position = mesh["POSITION"]

    def share(predicate):
        chosen = np.array([predicate(n) for n in names])
        return (weights * chosen[joints]).sum(axis=1)

    # The engine's rule (App::mark_membranes): the wing fingers and their
    # children.
    membrane = share(lambda n: "wing_finger" in n)

    # Joint positions in the bind pose, from the inverse bind matrices.
    inverse_bind = meshdata.accessor(doc, blob, doc["skins"][0]["inverseBindMatrices"]).reshape(-1, 4, 4)
    joint_position = np.array([np.linalg.inv(m.T.astype(np.float64))[:3, 3] for m in inverse_bind])

    # Horns: on the head, behind and above the skull. The model faces -Z in
    # its bind pose. Offsets are relative to the head joint, in model units,
    # read off a side view of the head's vertices.
    head = joint_position[names.index("head")]
    head_share = share(lambda n: n in ("head", "jaw", "jaw_tip"))
    behind = smoothstep(0.004, 0.024, position[:, 2] - head[2])
    above = smoothstep(0.054, 0.069, position[:, 1] - head[1])
    horn = head_share * behind * above

    # Claws: the forward end of each toe, past its joint.
    claw = np.zeros(len(position), dtype=np.float32)
    for index, name in enumerate(names):
        if not name.startswith("toe_"):
            continue
        toe = (weights * (joints == index)).sum(axis=1)
        ahead = joint_position[index][2] - position[:, 2]
        claw = np.maximum(claw, toe * smoothstep(0.022, 0.038, ahead))

    values = np.stack([membrane, horn, claw], axis=1)
    image, covered = uvmaps.rasterize(mesh["TEXCOORD_0"], mesh["indices"], values, size)
    image = uvmaps.fill_uncovered(image, covered)
    # A short blur, so a region ends over a few texels and not on one edge.
    image = uvmaps.box_blur(image, 2, passes=2)
    return np.clip(image, 0.0, 1.0), covered


def new_base_colour(base, regions, covered):
    membrane = regions[..., 0]
    dark_part = np.maximum(regions[..., 1], regions[..., 2])  # horn or claw

    linear = srgb_to_linear(base)
    luminance = np.maximum(linear @ LUMA, 1e-4)

    # Base and detail. The blur is weighted by coverage so the atlas padding,
    # which is a smear of the charts' colours, does not leak into the charts.
    log_l = np.log(luminance)
    weight = covered.astype(np.float32)
    blurred_weight = uvmaps.box_blur(weight, DETAIL_RADIUS)
    blurred = uvmaps.box_blur(log_l * weight, DETAIL_RADIUS)
    plain = uvmaps.box_blur(log_l, DETAIL_RADIUS)
    log_base = np.where(blurred_weight > 0.05, blurred / np.maximum(blurred_weight, 0.05), plain)
    del blurred, plain, blurred_weight, weight
    detail = np.clip(log_l - log_base, -2.0, 2.0)
    base_l = np.exp(log_base)
    del log_l, log_base

    # The membrane's own skin is mid-toned; the finger bones that share its
    # skin weights are dark. Only the skin gets the membrane's treatment.
    skin = membrane * smoothstep(0.022, 0.05, base_l)
    belly = smoothstep(BELLY_FROM, BELLY_TO, base_l) * (1.0 - membrane) * (1.0 - dark_part)

    lifted = LIFT_GAIN * np.power(base_l, LIFT_POWER)
    lifted *= 1.0 + (MEMBRANE_GAIN - 1.0) * skin
    lifted *= 1.0 + (BELLY_GAIN - 1.0) * belly
    power = DETAIL_POWER_BODY + (DETAIL_POWER_MEMBRANE - DETAIL_POWER_BODY) * membrane
    hide_l = lifted * np.exp(detail * power)
    horn_l = luminance * HORN_GAIN
    new_l = hide_l + (horn_l - hide_l) * dark_part
    new_l = np.clip(new_l, ALBEDO_MIN, ALBEDO_MAX).astype(np.float32)
    del lifted, hide_l, horn_l, power

    # The hue. Creases, body and ridges form one ramp over the detail ratio.
    ridge = smoothstep(-0.45, 0.45, detail)[..., None]
    low = chroma(TINT_CREASE) + (chroma(TINT_BODY) - chroma(TINT_CREASE)) * np.minimum(ridge * 2.0, 1.0)
    tint = low + (chroma(TINT_RIDGE) - chroma(TINT_BODY)) * np.maximum(ridge * 2.0 - 1.0, 0.0)
    del low
    strength = np.full(luminance.shape, TINT_STRENGTH_BODY, dtype=np.float32)

    def blend(tint, strength, colour, amount, weight):
        w = weight[..., None]
        tint = tint + (chroma(colour) - tint) * w
        strength = strength + (amount - strength) * weight
        return tint, strength

    # The membrane keeps a part of the ramp, so its veins stay deeper.
    tint, strength = blend(tint, strength, TINT_MEMBRANE, TINT_STRENGTH_MEMBRANE, skin * 0.75)
    tint, strength = blend(tint, strength, TINT_BELLY, TINT_STRENGTH_BELLY, belly)
    tint, strength = blend(tint, strength, TINT_HORN, TINT_STRENGTH_HORN, dark_part)

    own = linear / luminance[..., None]  # the texture's own hue, at luminance 1
    mixed = own + (tint - own) * strength[..., None]
    del own, tint
    out = mixed * new_l[..., None]
    # A saturated colour can pass 1 in red before its luminance does. Pull
    # such a texel toward its own grey; that keeps the luminance.
    peak = out.max(axis=-1)
    over = np.clip((peak - 0.92) / np.maximum(peak - new_l, 1e-4), 0.0, 1.0)[..., None]
    out = out + (new_l[..., None] - out) * over
    # The padding between the charts. The filter and the mip chain read it,
    # so it must continue the charts' colours. Within three texels of a chart
    # the generator's own padding does that, and it is kept as transformed.
    # Farther out its smears, amplified by the detail power, were bright
    # specks, so those texels are filled again from the charts.
    near = uvmaps.box_blur(covered.astype(np.float32), 3, passes=1) > 0.0
    out = np.where(near[..., None], out, uvmaps.fill_uncovered(out, near))
    return linear_to_srgb(out), detail, skin, dark_part


def new_normal(normal):
    xy = (normal[..., :2] * 2.0 - 1.0) * NORMAL_STRENGTH
    z = np.maximum(normal[..., 2:3] * 2.0 - 1.0, 0.05)
    vector = np.concatenate([xy, z], axis=-1)
    vector /= np.linalg.norm(vector, axis=-1, keepdims=True)
    return vector * 0.5 + 0.5


def new_orm(orm, detail, skin, dark_part):
    out = orm.copy()
    # Red: the engine's ambient occlusion.
    out[..., 0] = 1.0 - (1.0 - orm[..., 0]) * OCCLUSION_KEEP
    # Green: roughness. Ridges lose a little; membrane, horn and claw do not.
    ridge = smoothstep(0.0, 0.5, detail) * (1.0 - skin) * (1.0 - dark_part)
    out[..., 1] = np.clip(orm[..., 1] * (1.0 - ROUGHNESS_RIDGE_DROP * ridge), 0.05, 1.0)
    # Blue: metallic, unchanged.
    return out


def main():
    shipped = sys.argv[1] if len(sys.argv) > 1 else SHIPPED
    output = sys.argv[2] if len(sys.argv) > 2 else OUTPUT
    doc, blob = glbio.read_glb(shipped)
    material = doc["materials"][0]
    image_of = lambda texture: doc["textures"][texture["index"]]["source"]  # noqa: E731
    normal_index = image_of(material["normalTexture"])
    base_index = image_of(material["pbrMetallicRoughness"]["baseColorTexture"])
    orm_index = image_of(material["pbrMetallicRoughness"]["metallicRoughnessTexture"])

    base = decode(doc, blob, base_index)
    size = base.shape[0]
    if base.shape[:2] != (size, size) or size & (size - 1):
        raise ValueError("the base colour is not a square power of two")
    print(f"regions from the mesh at {size}x{size} ...", flush=True)
    regions, covered = region_maps(doc, blob, size)
    print("base colour ...", flush=True)
    new_base, detail, skin, dark_part = new_base_colour(base, regions, covered)
    del base
    images = {base_index: encode(new_base)}
    del new_base
    print("normal map ...", flush=True)
    images[normal_index] = encode(new_normal(decode(doc, blob, normal_index)))
    print("metallic-roughness ...", flush=True)
    images[orm_index] = encode(new_orm(decode(doc, blob, orm_index), detail, skin, dark_part))

    os.makedirs(os.path.dirname(os.path.abspath(output)), exist_ok=True)
    glbio.write_glb(output, doc, blob, images)
    print(f"wrote {output} ({os.path.getsize(output)} bytes)")


if __name__ == "__main__":
    main()
