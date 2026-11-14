"""Measure the dragon's pixels in the renders and build the comparison images.

Usage: measure.py BEFORE_DIR AFTER_DIR KEY_A_DIR KEY_B_DIR TARGET.png OUT_DIR

Each of the four directories holds the PNGs that render_views.sh wrote. The
dragon's pixels in a view are those where the two key renders differ (see
make_key.py). The HUD draws over the dragon in the run view; those pixels are
equal in both key renders, so the mask leaves them out.

The dragon in the art target has no key render. Its mask is a colour rule
inside a hand-set box: a red-to-orange hue with some saturation. That rule
selects for saturation, so the target's saturation number is an upper bound.

Luminance is Rec. 709 luma of the display (sRGB-encoded) values, 0 to 1.
Saturation is HSV saturation, (max - min) / max, of the same values.
"""

import os
import sys

import numpy as np
from PIL import Image

VIEWS = ["rear_200_14_20", "side_90_10_5", "below_0_12_-30", "run7"]
TARGET_BOX = (0, 520, 1672, 941)  # the dragon's part of run-target.png
LUMA = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)


def load(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float64) / 255.0


def hsv(rgb):
    high = rgb.max(axis=-1)
    low = rgb.min(axis=-1)
    saturation = np.where(high > 1e-6, (high - low) / np.maximum(high, 1e-6), 0.0)
    delta = np.maximum(high - low, 1e-6)
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    hue = np.where(high == r, ((g - b) / delta) % 6.0, np.where(high == g, (b - r) / delta + 2.0, (r - g) / delta + 4.0))
    return hue * 60.0, saturation, high


def numbers(rgb, mask):
    pixels = rgb[mask]
    hue, saturation, _ = hsv(pixels)
    luma = pixels @ LUMA
    # The circular mean of the hue, weighted by saturation.
    angle = np.deg2rad(hue)
    mean_hue = np.rad2deg(np.arctan2((np.sin(angle) * saturation).sum(), (np.cos(angle) * saturation).sum())) % 360.0
    return {
        "pixels": int(mask.sum()),
        "luminance": float(luma.mean()),
        "saturation": float(saturation.mean()),
        "hue": float(mean_hue),
        "luma_p10": float(np.percentile(luma, 10)),
        "luma_p90": float(np.percentile(luma, 90)),
    }


def key_mask(a, b):
    return np.abs(a - b).max(axis=-1) > 0.08


def target_mask(rgb):
    hue, saturation, value = hsv(rgb)
    warm = (hue < 32.0) | (hue > 345.0)
    mask = warm & (saturation > 0.22) & (value > 0.08)
    box = np.zeros(mask.shape, dtype=bool)
    left, top, right, bottom = TARGET_BOX
    box[top:bottom, left:right] = True
    return mask & box


def bounds(mask, margin, shape):
    rows = np.where(mask.any(axis=1))[0]
    cols = np.where(mask.any(axis=0))[0]
    top = max(int(rows[0]) - margin, 0)
    bottom = min(int(rows[-1]) + margin + 1, shape[0])
    left = max(int(cols[0]) - margin, 0)
    right = min(int(cols[-1]) + margin + 1, shape[1])
    return left, top, right, bottom


def to_image(rgb):
    return Image.fromarray((np.clip(rgb, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8))


def side_by_side(images, gap=6):
    height = max(i.height for i in images)
    width = sum(i.width for i in images) + gap * (len(images) - 1)
    sheet = Image.new("RGB", (width, height), (20, 20, 20))
    x = 0
    for image in images:
        sheet.paste(image, (x, 0))
        x += image.width + gap
    return sheet


def save_jpeg(image, path):
    quality = 93
    while True:
        image.save(path, quality=quality, subsampling=0)
        if os.path.getsize(path) < 1_000_000 or quality <= 60:
            return
        quality -= 4


def main():
    before_dir, after_dir, key_a_dir, key_b_dir, target_path, out_dir = sys.argv[1:7]
    os.makedirs(out_dir, exist_ok=True)
    rows = []
    union = {"before": [], "after": []}
    for view in VIEWS:
        before = load(os.path.join(before_dir, view + ".png"))
        after = load(os.path.join(after_dir, view + ".png"))
        mask = key_mask(load(os.path.join(key_a_dir, view + ".png")), load(os.path.join(key_b_dir, view + ".png")))
        box = bounds(mask, 24 if view != "run7" else 40, mask.shape)
        left, top, right, bottom = box
        crop_mask = mask[top:bottom, left:right]
        b = numbers(before[top:bottom, left:right], crop_mask)
        a = numbers(after[top:bottom, left:right], crop_mask)
        rows.append((view, b, a))
        union["before"].append(before[top:bottom, left:right][crop_mask])
        union["after"].append(after[top:bottom, left:right][crop_mask])
        pair = side_by_side([to_image(before[top:bottom, left:right]), to_image(after[top:bottom, left:right])])
        save_jpeg(pair, os.path.join(out_dir, view + ".jpg"))
        to_image(np.where(crop_mask[..., None], after[top:bottom, left:right], 0.0)).save(
            os.path.join(out_dir, view + "_mask.png"))
        if view == "run7":
            # The run frame beside the target: the game's dragon is 220 pixels
            # wide, so its crops are enlarged three times, without smoothing.
            target = load(target_path)
            t_mask = target_mask(target)
            t_box = bounds(t_mask, 20, t_mask.shape)
            t_crop = to_image(target[t_box[1]:t_box[3], t_box[0]:t_box[2]])
            scale = 3
            crops = [to_image(x[top:bottom, left:right]) for x in (before, after)]
            crops = [c.resize((c.width * scale, c.height * scale), Image.NEAREST) for c in crops]
            t_crop = t_crop.resize((round(t_crop.width * crops[0].height / t_crop.height), crops[0].height), Image.LANCZOS)
            save_jpeg(side_by_side(crops + [t_crop]), os.path.join(out_dir, "run7_vs_target.jpg"))
            to_image(np.where(t_mask[..., None], target, 0.0)).save(os.path.join(out_dir, "target_mask.png"))
            target_numbers = numbers(target, t_mask)

    def fmt(n):
        return f"luminance {n['luminance']:.3f}  saturation {n['saturation']:.3f}  hue {n['hue']:5.1f}  " \
               f"luma p10/p90 {n['luma_p10']:.3f}/{n['luma_p90']:.3f}  pixels {n['pixels']}"

    lines = []
    for view, b, a in rows:
        lines.append(f"{view:16s} before  {fmt(b)}")
        lines.append(f"{view:16s} after   {fmt(a)}")
    for name in ("before", "after"):
        pixels = np.concatenate(union[name])[None, :, :]
        lines.append(f"{'all four views':16s} {name:7s} {fmt(numbers(pixels, np.ones(pixels.shape[:2], dtype=bool)))}")
    lines.append(f"{'run-target.png':16s} target  {fmt(target_numbers)}")
    text = "\n".join(lines)
    print(text)
    with open(os.path.join(out_dir, "numbers.txt"), "w") as f:
        f.write(text + "\n")


if __name__ == "__main__":
    main()
