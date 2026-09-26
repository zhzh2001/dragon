#!/usr/bin/env python3
"""Build assets/textures/terrain_detail.png: four tileable greyscale detail maps in one RGBA PNG.

python3 tools/build_terrain_detail.py            # build, verify, write the tiled preview
python3 tools/build_terrain_detail.py --verify-only

One channel per terrain material, each an independent map with mean 0.5, meant to
multiply the terrain colour by (0.5 + channel) so a flat channel is a no-op:

  R  rock   strata bands, cracks between cells, chips           features 0.5-3 m on a 16 m tile
  G  grass  clumped tufts over broad patches                    features 0.2-1.5 m
  B  dirt   pebbles and gravel over a fine grain                features 0.05-0.5 m
  A  snow   soft drifts and wind-cut sastrugi, low contrast     features 1-6 m

Everything is built from spectrally filtered noise (an FFT over the tile is periodic
by construction) and from point features drawn with wrapped indexing, so every map
tiles in both directions without any edge blending. Deterministic: one seeded numpy
generator, no time or ordering dependence. Pure numpy + PIL, no Blender.
"""
import argparse
from pathlib import Path
import sys

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
N = 1024                     # pixels per side
TILE_M = 16.0                # metres the tile is meant to span in the engine
PX_PER_M = N / TILE_M
SEED = 20260926
TARGETS = {                  # channel -> (label, target std)
    0: ('rock', 0.15), 1: ('grass', 0.15), 2: ('dirt', 0.15), 3: ('snow', 0.06)}


def band_noise(rng, wavelength_px, octaves=1, gain=0.5, lacunarity=2.0, aniso=1.0, width=0.6):
    """Periodic noise whose energy sits around one wavelength (in pixels).

    A log-normal band in frequency space is applied to seeded white noise; octaves
    step the wavelength down by `lacunarity` and the amplitude by `gain`. `aniso`
    stretches features along X by shrinking the effective X frequency. Unit std.
    """
    k = np.fft.fftfreq(N)                       # cycles per pixel
    kx, ky = np.meshgrid(k, k)
    radial = np.sqrt((kx / aniso) ** 2 + ky ** 2)
    radial[0, 0] = 1e-9
    total = np.zeros((N, N))
    amplitude = 1.0
    for octave in range(octaves):
        centre = 1.0 / (wavelength_px / lacunarity ** octave)
        band = np.exp(-0.5 * (np.log(radial / centre) / width) ** 2)
        band[0, 0] = 0.0
        white = np.fft.fft2(rng.standard_normal((N, N)))
        layer = np.real(np.fft.ifft2(white * band))
        total += amplitude * layer / (layer.std() + 1e-12)
        amplitude *= gain
    return total / (total.std() + 1e-12)


def normalise(field, std):
    field = (field - field.mean()) / (field.std() + 1e-12)
    return 0.5 + std * field


def soft_clip(field, std):
    """Fold the tails into [0, 1] and settle the map at mean 0.5 with the target std.

    The tanh compresses the extremes (a deep crack, a pebble highlight) instead of
    clipping them flat, but it also rescales the middle, so the map is renormalised
    after each pass; three passes converge to within a percent of the target.
    """
    for _ in range(3):
        centred = (field - 0.5) * 2.0
        field = 0.5 + 0.5 * np.tanh(centred * 1.1) / np.tanh(1.1)
        field = normalise(field, std)
    return np.clip(field, 0.0, 1.0)


def wrapped_points(rng, count):
    return rng.uniform(0, N, size=(count, 2))


def worley(rng, count, jitter_seed=None):
    """Periodic Worley distances F1 and F2 (pixels) for `count` random feature points.

    Each point only reaches its neighbourhood, so the distance field is built by
    stamping a window around each point with wrapped indices; the window is sized
    from the expected point spacing so the true nearest point is never missed.
    """
    pts = wrapped_points(rng, count)
    spacing = N / np.sqrt(count)
    half = int(np.ceil(spacing * 2.2))
    f1 = np.full((N, N), np.inf)
    f2 = np.full((N, N), np.inf)
    offs = np.arange(-half, half + 1)
    oy, ox = np.meshgrid(offs, offs, indexing='ij')
    for px, py in pts:
        cx, cy = int(round(px)), int(round(py))
        d = np.sqrt((ox + cx - px) ** 2 + (oy + cy - py) ** 2)
        rows = (oy + cy) % N
        cols = (ox + cx) % N
        cur1 = f1[rows, cols]
        cur2 = f2[rows, cols]
        new2 = np.where(d < cur1, cur1, np.minimum(cur2, d))
        new1 = np.minimum(cur1, d)
        f1[rows, cols] = new1
        f2[rows, cols] = new2
    return f1, f2, spacing


def stamp_discs(rng, count, radius_px, height_fn):
    """Sum of overlapping discs, each shaded by `height_fn(r_norm, dx_norm, dy_norm)`.

    Discs are placed with wrapped indexing so those straddling the edge continue on
    the far side. Later discs overwrite earlier ones where they overlap (a pebble
    lying on top), giving a packed-gravel look rather than a blurred sum.
    """
    field = np.zeros((N, N))
    mask = np.zeros((N, N), dtype=bool)
    pts = wrapped_points(rng, count)
    radii = rng.uniform(radius_px[0], radius_px[1], size=count)
    for (px, py), r in zip(pts, radii):
        half = int(np.ceil(r)) + 1
        offs = np.arange(-half, half + 1)
        oy, ox = np.meshgrid(offs, offs, indexing='ij')
        cx, cy = int(round(px)), int(round(py))
        dx = (ox + cx - px) / r
        dy = (oy + cy - py) / r
        rn = np.sqrt(dx ** 2 + dy ** 2)
        inside = rn < 1.0
        rows = (oy + cy) % N
        cols = (ox + cx) % N
        values = height_fn(rn, dx, dy)
        field[rows[inside], cols[inside]] = values[inside]
        mask[rows[inside], cols[inside]] = True
    return field, mask


def warp(field, rng, amount_px, wavelength_px):
    """Periodic domain warp: resample `field` at wrapped, noise-displaced coordinates."""
    dx = band_noise(rng, wavelength_px) * amount_px
    dy = band_noise(rng, wavelength_px) * amount_px
    yy, xx = np.meshgrid(np.arange(N), np.arange(N), indexing='ij')
    return field[np.round(yy + dy).astype(int) % N, np.round(xx + dx).astype(int) % N]


def rock(rng):
    # Strata: near-horizontal beds warped by a broad noise. A sawtooth profile gives
    # each bed a bright weathered top and a dark undercut instead of a soft sine.
    bend = band_noise(rng, 7.0 * PX_PER_M, octaves=2) * 0.22 * PX_PER_M
    y = np.arange(N)[:, None] * np.ones((1, N))
    strata_period = N / round(N / (1.6 * PX_PER_M))     # an integer number of bands per tile
    t = ((y + bend) / strata_period) % 1.0
    strata = np.where(t < 0.15, t / 0.15, 1.0 - (t - 0.15) / 0.85) * 2 - 1
    strata = 0.85 * strata + 0.15 * np.sin(t * 4 * np.pi + 0.7)
    # Cracks along cell boundaries: cells ~2.5 m across, thin dark lines where F2-F1
    # is small, broken up so the network is not a complete net, then warped so the
    # lines wander instead of running straight between the cell corners.
    f1, f2, _ = worley(rng, int((TILE_M / 2.5) ** 2))
    cracks = np.clip(1.0 - (f2 - f1) / (0.11 * PX_PER_M), 0, 1) ** 1.2
    keep = band_noise(rng, 1.5 * PX_PER_M, octaves=2) > -0.6
    cracks = warp(cracks * keep, rng, 0.25 * PX_PER_M, 1.0 * PX_PER_M)
    # Chips: flaked facets 0.4-0.8 m, stretched along the beds, subtle.
    c1, c2, _ = worley(rng, int((TILE_M / 0.6) ** 2))
    chips = np.clip((c2 - c1) / (0.25 * PX_PER_M), 0, 1)
    chips = warp(chips, rng, 0.3 * PX_PER_M, 2.0 * PX_PER_M)
    chips = (chips - chips.mean()) / chips.std()
    grain = band_noise(rng, 0.10 * PX_PER_M, octaves=2)
    field = 1.2 * strata + 0.4 * chips - 3.0 * cracks + 0.3 * grain
    return normalise(field, TARGETS[0][1])


def grass(rng):
    # Two sizes of tuft (0.25 m and 0.6 m), brighter at the crown, over patches
    # 1.5 m across that thin the sward out to bare, darker ground in places.
    def tufts(metres, power):
        f1, _, spacing = worley(rng, int((TILE_M / metres) ** 2))
        t = (1.0 - np.clip(f1 / (spacing * 0.8), 0, 1)) ** power
        return (t - t.mean()) / t.std()
    small = tufts(0.25, 1.8)
    large = tufts(0.6, 1.4)
    blades = band_noise(rng, 0.08 * PX_PER_M, octaves=2, aniso=0.6)
    patches = band_noise(rng, 1.5 * PX_PER_M, octaves=2, gain=0.6)
    density = 0.5 + 0.5 * np.tanh(patches * 1.2)          # 0 bare .. 1 thick
    field = (0.35 + 0.65 * density) * (0.7 * small + 0.6 * large + 0.35 * blades) + 1.1 * patches
    return normalise(field, TARGETS[1][1])


def dirt(rng):
    # Fine grain first, then gravel (0.06-0.16 m), then a sparser layer of pebbles
    # (0.15-0.45 m) laid over it; each stone is lit from the upper left.
    def shade(rn, dx, dy):
        dome = np.sqrt(np.clip(1.0 - rn ** 2, 0, 1))
        light = 0.5 + 0.5 * (-0.55 * dx - 0.65 * dy) / np.maximum(np.sqrt(dx**2 + dy**2), 1e-6) * (1 - dome)
        return 0.15 + 0.85 * (0.55 * dome + 0.45 * light)
    grain = band_noise(rng, 0.05 * PX_PER_M, octaves=2, gain=0.7)
    gravel, gmask = stamp_discs(rng, 5200, (0.03 * PX_PER_M, 0.08 * PX_PER_M), shade)
    pebbles, pmask = stamp_discs(rng, 700, (0.10 * PX_PER_M, 0.28 * PX_PER_M), shade)
    field = 0.45 * grain
    field = np.where(gmask, 1.5 * (gravel - 0.55), field)
    field = np.where(pmask, 1.9 * (pebbles - 0.55), field)
    field += 0.3 * band_noise(rng, 1.0 * PX_PER_M, octaves=2)
    return normalise(field, TARGETS[2][1])


def snow(rng):
    # Drifts: broad soft noise, slightly stretched along the wind (X). Sastrugi:
    # crests cut across the drifts, from a stretched band folded into ridges, and
    # confined to the windward patches so the field is not uniformly furrowed.
    drifts = band_noise(rng, 6.0 * PX_PER_M, octaves=3, gain=0.5, aniso=1.6)
    ridge_src = band_noise(rng, 1.0 * PX_PER_M, octaves=2, gain=0.5, aniso=3.0)
    sastrugi = 1.0 - np.abs(np.tanh(ridge_src * 1.2))
    sastrugi = (sastrugi - sastrugi.mean()) / sastrugi.std()
    where = 0.5 + 0.5 * np.tanh(band_noise(rng, 5.0 * PX_PER_M) * 1.5)
    sparkle = band_noise(rng, 0.06 * PX_PER_M, octaves=1)
    field = 1.0 * drifts + 0.5 * where * sastrugi + 0.15 * sparkle
    return normalise(field, TARGETS[3][1])


def build(path):
    rng = np.random.default_rng(SEED)
    channels = [soft_clip(fn(rng), TARGETS[c][1]) for c, fn in enumerate((rock, grass, dirt, snow))]
    stack = np.stack(channels, axis=-1)
    data = np.clip(np.round(stack * 255.0), 0, 255).astype(np.uint8)
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(data, mode='RGBA').save(path, optimize=True)


def seam_stats(channel):
    """Mean absolute step across the wrap seam vs. the mean step between interior neighbours."""
    ch = channel.astype(np.float64) / 255.0
    interior_cols = np.abs(np.diff(ch, axis=1)).mean()
    interior_rows = np.abs(np.diff(ch, axis=0)).mean()
    seam_cols = np.abs(ch[:, 0] - ch[:, -1]).mean()
    seam_rows = np.abs(ch[0, :] - ch[-1, :]).mean()
    return interior_cols, seam_cols, interior_rows, seam_rows


def verify(path):
    image = Image.open(path)
    assert image.size == (N, N), f'Expected {N}x{N}, got {image.size}'
    assert image.mode == 'RGBA', f'Expected RGBA, got {image.mode}'
    data = np.asarray(image)
    ok = True
    shown = path.relative_to(ROOT) if path.is_relative_to(ROOT) else path
    print(f'VERIFY {shown}: {N}x{N} RGBA')
    for c, (label, target_std) in TARGETS.items():
        ch = data[..., c].astype(np.float64) / 255.0
        mean, std = ch.mean(), ch.std()
        icol, scol, irow, srow = seam_stats(data[..., c])
        ratio = max(scol / icol, srow / irow)
        mean_ok = abs(mean - 0.5) < 0.03
        std_lo, std_hi = (0.04, 0.09) if c == 3 else (0.12, 0.18)
        std_ok = std_lo <= std <= std_hi
        seam_ok = ratio < 1.5
        range_ok = ch.min() >= 0.0 and ch.max() <= 1.0 and (ch.max() - ch.min()) > 0.3
        line_ok = mean_ok and std_ok and seam_ok and range_ok
        ok &= line_ok
        print(f'  {"RGBA"[c]} {label:5s} mean={mean:.3f} std={std:.3f} min={ch.min():.2f} max={ch.max():.2f} '
              f'seam/interior step: cols {scol:.4f}/{icol:.4f} rows {srow:.4f}/{irow:.4f} '
              f'(ratio {ratio:.2f}) {"PASS" if line_ok else "FAIL"}')
    print(f'VERIFY terrain_detail: {"PASS" if ok else "FAIL"}')
    return ok


def preview(path, out):
    """2x2 tiling of each channel as greyscale, four panels, so a seam would show as a cross."""
    data = np.asarray(Image.open(path))
    panel = 1024
    sheet = Image.new('L', (panel * 2 + 3 * 16, panel * 2 + 3 * 16), 128)
    draw = ImageDraw.Draw(sheet)
    for c, (label, _) in TARGETS.items():
        ch = Image.fromarray(data[..., c], mode='L')
        tiled = Image.new('L', (N * 2, N * 2))
        for ty in range(2):
            for tx in range(2):
                tiled.paste(ch, (tx * N, ty * N))
        tiled = tiled.resize((panel, panel), Image.LANCZOS)
        x = 16 + (c % 2) * (panel + 16)
        y = 16 + (c // 2) * (panel + 16)
        sheet.paste(tiled, (x, y))
        draw.rectangle((x, y, x + 150, y + 24), fill=0)
        draw.text((x + 6, y + 6), f'{"RGBA"[c]}: {label} (2x2 tiles)', fill=255)
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out, optimize=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--verify-only', action='store_true')
    parser.add_argument('--output', type=Path, default=ROOT / 'assets' / 'textures' / 'terrain_detail.png')
    parser.add_argument('--preview', type=Path, default=ROOT / 'artifacts' / 'rocks' / 'terrain_detail_tiled.png')
    args = parser.parse_args()
    if not args.verify_only:
        build(args.output)
        preview(args.output, args.preview)
    sys.exit(0 if verify(args.output) else 1)


if __name__ == '__main__':
    main()
