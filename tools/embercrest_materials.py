"""Procedural Embercrest material maps.

The mesh builder owns Blender image creation and node wiring.  This module only
generates the three RGB images that the builder passes to that pipeline:

``generate_maps(kind, color, size, seed) -> (base, normal, orm)``

All arrays are contiguous ``float32`` values in ``[0, 1]`` and have shape
``(height, width, 3)``.  ``base`` is the muted, linear-ish colour field used by
the existing builder, ``normal`` is an encoded tangent-space normal, and
``orm`` stores occlusion, roughness, and metallic in RGB respectively.  The
textures are periodic in both UV directions, which makes them safe to repeat
on the simple tube and membrane UVs used by Embercrest.

Only NumPy is required.  The cellular fields search a fixed 3x3 neighbourhood
of a jittered point lattice, so the work stays O(pixels) rather than scaling
with the number of cells.  Seeded local generators keep results deterministic
without touching NumPy's global random state.
"""

from __future__ import annotations

import math
from typing import Iterable, Tuple

import numpy as np


__all__ = ["generate_maps"]

_TAU = np.float32(2.0 * math.pi)
_EPS = np.float32(1.0e-7)


def _shape(size: int | Tuple[int, int]) -> tuple[int, int]:
    """Return a validated ``(height, width)`` shape.

    The public API is normally called with a square integer.  Accepting a pair
    is useful for quick material previews and does not change the builder's
    square-texture behaviour.
    """

    if isinstance(size, (tuple, list)):
        if len(size) != 2:
            raise ValueError("size must be an integer or a (height, width) pair")
        height, width = (int(size[0]), int(size[1]))
    else:
        height = width = int(size)
    if height < 8 or width < 8:
        raise ValueError("texture dimensions must be at least 8 pixels")
    return height, width


def _rng(seed: int, salt: int = 0) -> np.random.Generator:
    # Keep the conversion explicit so negative Python seeds and NumPy integer
    # scalars behave alike on the Python versions bundled with Blender.
    mixed = (int(seed) + 0x9E3779B9 * int(salt + 1)) & 0xFFFFFFFFFFFFFFFF
    return np.random.default_rng(mixed)


def _smoothstep(edge0: float, edge1: float, value: np.ndarray) -> np.ndarray:
    t = np.clip((value - np.float32(edge0)) / np.float32(edge1 - edge0), 0.0, 1.0)
    return t * t * (np.float32(3.0) - np.float32(2.0) * t)


def _fade(value: np.ndarray) -> np.ndarray:
    return value * value * (np.float32(3.0) - np.float32(2.0) * value)


def _tile_noise(
    height: int,
    width: int,
    grid_y: int,
    grid_x: int,
    seed: int,
) -> np.ndarray:
    """Periodic bilinear value noise in ``[0, 1]``.

    The random lattice is tiny compared with the output image.  Indexing the
    lattice with wrapped integer coordinates gives exact periodicity at the UV
    boundary while the interpolation keeps broad pigment and wear coherent.
    """

    grid_y = max(2, int(grid_y))
    grid_x = max(2, int(grid_x))
    rng = _rng(seed, grid_y * 131 + grid_x * 17)
    lattice = rng.random((grid_y, grid_x)).astype(np.float32)

    y = np.arange(height, dtype=np.float32) * np.float32(grid_y / height)
    x = np.arange(width, dtype=np.float32) * np.float32(grid_x / width)
    y0 = np.floor(y).astype(np.int32)
    x0 = np.floor(x).astype(np.int32)
    fy = _fade(y - y0.astype(np.float32))[:, None]
    fx = _fade(x - x0.astype(np.float32))[None, :]
    y0 %= grid_y
    x0 %= grid_x
    y1 = (y0 + 1) % grid_y
    x1 = (x0 + 1) % grid_x

    a = lattice[y0[:, None], x0[None, :]]
    b = lattice[y0[:, None], x1[None, :]]
    row0 = a + (b - a) * fx
    a = lattice[y1[:, None], x0[None, :]]
    b = lattice[y1[:, None], x1[None, :]]
    row1 = a + (b - a) * fx
    return (row0 + (row1 - row0) * fy).astype(np.float32, copy=False)


def _fbm(
    height: int,
    width: int,
    seed: int,
    octaves: Iterable[tuple[int, int, float]],
) -> np.ndarray:
    """A small periodic fBm field in approximately ``[0, 1]``."""

    result = np.zeros((height, width), dtype=np.float32)
    amplitude_sum = np.float32(0.0)
    for i, (grid_y, grid_x, amplitude) in enumerate(octaves):
        value = _tile_noise(height, width, grid_y, grid_x, seed + 7919 * (i + 1))
        result += value * np.float32(amplitude)
        amplitude_sum += np.float32(amplitude)
    if amplitude_sum > 0.0:
        result /= amplitude_sum
    return np.clip(result, 0.0, 1.0).astype(np.float32, copy=False)


def _periodic_voronoi(
    u: np.ndarray,
    v: np.ndarray,
    cells_x: int,
    cells_y: int,
    seed: int,
) -> tuple[
    np.ndarray,
    np.ndarray,
    np.ndarray,
    np.ndarray,
    np.ndarray,
    np.ndarray,
]:
    """Jittered, periodic cellular distance and nearest-point data.

    ``u`` and ``v`` are already in cell units.  The jittered points are kept in
    a small lattice; each pixel visits the same nine possible neighbours.  In
    addition to the first and second distances, return the wrapped nearest
    cell id and the unwrapped nearest point position for local scute shaping.
    """

    cells_x = max(2, int(cells_x))
    cells_y = max(2, int(cells_y))
    rng = _rng(seed, cells_x * 19 + cells_y * 37)
    # Keep points away from lattice boundaries.  This avoids pathological
    # zero-width cells while retaining substantial cell-size variation.
    point_x = np.float32(0.12) + np.float32(0.76) * rng.random((cells_y, cells_x)).astype(np.float32)
    point_y = np.float32(0.12) + np.float32(0.76) * rng.random((cells_y, cells_x)).astype(np.float32)

    ix = np.floor(u).astype(np.int32)
    iy = np.floor(v).astype(np.int32)
    first = np.full(u.shape, np.float32(np.inf), dtype=np.float32)
    second = np.full(u.shape, np.float32(np.inf), dtype=np.float32)
    nearest_x = np.zeros(u.shape, dtype=np.int32)
    nearest_y = np.zeros(u.shape, dtype=np.int32)
    nearest_px = np.zeros(u.shape, dtype=np.float32)
    nearest_py = np.zeros(u.shape, dtype=np.float32)

    for oy in (-1, 0, 1):
        cell_y = (iy + oy) % cells_y
        for ox in (-1, 0, 1):
            cell_x = (ix + ox) % cells_x
            candidate_x = ix.astype(np.float32) + np.float32(ox) + point_x[cell_y, cell_x]
            candidate_y = iy.astype(np.float32) + np.float32(oy) + point_y[cell_y, cell_x]
            dx = u - candidate_x
            dy = v - candidate_y
            distance = dx * dx + dy * dy
            closer = distance < first
            second = np.where(closer, first, np.minimum(second, distance))
            first = np.where(closer, distance, first)
            nearest_x = np.where(closer, cell_x, nearest_x)
            nearest_y = np.where(closer, cell_y, nearest_y)
            nearest_px = np.where(closer, candidate_x, nearest_px)
            nearest_py = np.where(closer, candidate_y, nearest_py)

    return (
        np.sqrt(first).astype(np.float32, copy=False),
        np.sqrt(second).astype(np.float32, copy=False),
        nearest_x,
        nearest_y,
        nearest_px,
        nearest_py,
    )


def _normal_from_height(height: np.ndarray, strength: float) -> np.ndarray:
    """Encode a tangent normal from UV derivatives at any output resolution."""

    h, w = height.shape
    # Central differences use UV spacing rather than one-pixel spacing.  The
    # x derivative wraps; y uses edge differences because the tube end caps do
    # not have a meaningful opposite seam.
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * np.float32(0.5 * w)
    dy = np.gradient(height, axis=0).astype(np.float32, copy=False) * np.float32(h)
    nx = -dx * np.float32(strength)
    ny = -dy * np.float32(strength)
    nz = np.ones_like(height, dtype=np.float32)
    length = np.sqrt(nx * nx + ny * ny + nz * nz)
    normal = np.stack((nx / length, ny / length, nz / length), axis=-1)
    return np.clip(normal * np.float32(0.5) + np.float32(0.5), 0.0, 1.0).astype(np.float32, copy=False)


def _finish(base: np.ndarray, height: np.ndarray, occlusion: np.ndarray, roughness: np.ndarray, normal_strength: float) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    normal = _normal_from_height(height.astype(np.float32, copy=False), normal_strength)
    orm = np.stack(
        (
            np.clip(occlusion, 0.0, 1.0),
            np.clip(roughness, 0.0, 1.0),
            np.zeros_like(occlusion, dtype=np.float32),
        ),
        axis=-1,
    ).astype(np.float32, copy=False)
    base = np.clip(base, 0.0, 1.0).astype(np.float32, copy=False)
    return (
        np.ascontiguousarray(base),
        np.ascontiguousarray(normal),
        np.ascontiguousarray(orm),
    )


def _skin_maps(height: int, width: int, color: np.ndarray, seed: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Irregular, overlapping scutes for the charcoal-bronze hide."""

    # A warped jittered cellular field supplies varied centres and boundaries.
    # The warp is deliberately low frequency so scales remain readable rather
    # than dissolving into pixel noise.
    warp_u = (_fbm(height, width, seed + 11, ((7, 9, 0.65), (13, 17, 0.35))) - 0.5) * np.float32(0.58)
    warp_v = (_fbm(height, width, seed + 23, ((6, 8, 0.70), (12, 15, 0.30))) - 0.5) * np.float32(0.48)
    x = np.arange(width, dtype=np.float32)[None, :] / np.float32(width)
    y = np.arange(height, dtype=np.float32)[:, None] / np.float32(height)
    cells_x, cells_y = 58, 70
    u = x * np.float32(cells_x) + warp_u
    v = y * np.float32(cells_y) + warp_v
    d1, d2, cell_x, cell_y, point_x, point_y = _periodic_voronoi(u, v, cells_x, cells_y, seed + 101)

    # A second, smaller cellular layer breaks up the broad hide into partly
    # overlapping secondary scutes and fine fissures.  Both fields remain
    # constant-work 3x3 searches rather than one loop per cell.
    fine_warp_u = (_tile_noise(height, width, 11, 14, seed + 131) - 0.5) * np.float32(0.34)
    fine_warp_v = (_tile_noise(height, width, 10, 13, seed + 149) - 0.5) * np.float32(0.28)
    d1_f, d2_f, _, _, _, _ = _periodic_voronoi(
        x * np.float32(116.0) + fine_warp_u,
        y * np.float32(132.0) + fine_warp_v,
        116,
        132,
        seed + 163,
    )

    large = _fbm(height, width, seed + 181, ((4, 5, 0.58), (8, 10, 0.28), (17, 21, 0.14)))
    medium = _fbm(height, width, seed + 197, ((13, 17, 0.55), (27, 33, 0.30), (54, 65, 0.15)))
    fine = _tile_noise(height, width, 170, 214, seed + 211)

    # Voronoi distance difference is zero at a cell boundary.  The jitter,
    # domain warp, and per-cell wear turn this into broken, nonuniform cracks.
    boundary = np.exp(-np.square((d2 - d1) / np.float32(0.047))).astype(np.float32)
    fine_boundary = np.exp(-np.square((d2_f - d1_f) / np.float32(0.039))).astype(np.float32)
    cell_tone = _tile_noise(cells_y, cells_x, cells_y, cells_x, seed + 227)[cell_y, cell_x]
    cell_wear = _tile_noise(cells_y, cells_x, cells_y, cells_x, seed + 241)[cell_y, cell_x]
    cell_angle = (_tile_noise(cells_y, cells_x, cells_y, cells_x, seed + 257)[cell_y, cell_x] - 0.5) * np.float32(0.9)
    boundary *= np.clip(np.float32(0.38) + np.float32(0.98) * cell_wear + np.float32(0.20) * (medium - 0.5), 0.18, 1.0)
    fine_boundary *= np.float32(0.48) + np.float32(0.52) * fine

    # Rounded raised centres and an off-axis lap shadow suggest individual
    # scutes that overlap toward a changing local direction.  The lap term is
    # intentionally subtle; the boundary network remains the main cue.
    local_x = u - point_x
    local_y = v - point_y
    cs = np.cos(cell_angle)
    sn = np.sin(cell_angle)
    lap_axis = local_x * cs + local_y * sn
    lap = np.exp(-np.square((lap_axis + np.float32(0.27)) / np.float32(0.095))).astype(np.float32)
    lap *= np.clip(np.float32(1.35) - d1 * np.float32(1.8), 0.0, 1.0)
    # A broad local plate silhouette and a paired upper/lower lip are what make
    # the cellular boundaries read as lapping reptile scutes instead of cracked
    # stone.  Cell jitter still changes every silhouette and lip direction.
    plate_rx = np.float32(0.43) + np.float32(0.07) * (cell_tone - 0.5)
    plate_ry = np.float32(0.39) + np.float32(0.08) * (0.5 - cell_wear)
    plate = np.exp(
        -np.float32(0.5) * (np.square(local_x / plate_rx) + np.square((local_y + np.float32(0.025)) / plate_ry))
    ).astype(np.float32)
    upper_lip = np.exp(-np.square((lap_axis - np.float32(0.245)) / np.float32(0.105))).astype(np.float32) * plate
    lower_lip = np.exp(-np.square((lap_axis + np.float32(0.285)) / np.float32(0.085))).astype(np.float32) * plate
    centre = np.clip(np.float32(1.0) - d1 * np.float32(1.30), 0.0, 1.0)
    centre = centre * centre * (np.float32(0.72) + np.float32(0.28) * cell_tone)
    fine_centre = np.clip(np.float32(1.0) - d1_f * np.float32(1.8), 0.0, 1.0)
    pores = np.clip((np.float32(0.39) - fine) * np.float32(3.0), 0.0, 1.0)

    # Height has no lighting baked into it.  It is merely the microrelief that
    # becomes tangent normals in _finish; cracks sink and worn scute rims lift.
    height_map = (
        np.float32(0.025) * centre
        + np.float32(0.006) * plate
        + np.float32(0.004) * upper_lip
        - np.float32(0.007) * lower_lip
        + np.float32(0.009) * lap
        + np.float32(0.010) * fine_centre
        - np.float32(0.021) * boundary
        - np.float32(0.007) * fine_boundary
        - np.float32(0.0025) * pores
    ).astype(np.float32)

    mottle = np.float32(0.58) * large + np.float32(0.30) * medium + np.float32(0.12) * fine
    pigment = np.float32(0.76) + np.float32(0.30) * mottle
    pigment += np.float32(0.07) * (centre - 0.35) + np.float32(0.045) * (cell_tone - 0.5)
    pigment *= np.float32(1.0) - np.float32(0.52) * boundary - np.float32(0.17) * fine_boundary
    pigment *= np.float32(1.0) - np.float32(0.10) * pores
    pigment *= np.float32(0.96) + np.float32(0.10) * (plate - np.float32(0.45))
    pigment *= np.float32(1.0) - np.float32(0.14) * lower_lip

    # Small warm oxide patches and edge chips keep charcoal bronze from reading
    # as a perfectly monochrome brown while remaining deliberately muted.
    oxide = _smoothstep(0.53, 0.84, np.float32(0.68) * large + np.float32(0.32) * medium)
    warm = np.asarray((0.031, 0.013, -0.003), dtype=np.float32)
    edge_warm = np.asarray((0.020, 0.010, 0.001), dtype=np.float32)
    base = color[None, None, :] * pigment[..., None]
    base += warm[None, None, :] * oxide[..., None]
    base += edge_warm[None, None, :] * lap[..., None]
    base += np.asarray((0.012, 0.007, 0.002), dtype=np.float32)[None, None, :] * upper_lip[..., None]
    base += np.asarray((0.006, 0.004, 0.002), dtype=np.float32)[None, None, :] * (fine_centre * (1.0 - boundary))[..., None]

    occlusion = np.float32(0.96) - np.float32(0.38) * boundary - np.float32(0.11) * fine_boundary - np.float32(0.045) * pores
    occlusion += np.float32(0.025) * centre
    roughness = np.float32(0.73) + np.float32(0.10) * boundary + np.float32(0.055) * pores - np.float32(0.075) * centre
    roughness += np.float32(0.055) * (0.5 - mottle)
    return _finish(base, height_map, occlusion, roughness, 0.19)


def _segment_distance(
    xx: np.ndarray,
    yy: np.ndarray,
    p0: tuple[float, float],
    p1: tuple[float, float],
) -> np.ndarray:
    """Squared distance to a short segment on a unit torus."""

    # Represent each segment by its shortest wrapped displacement.  Branches
    # are kept below half a tile, so this gives a continuous periodic field.
    vx = np.float32((p1[0] - p0[0] + 0.5) % 1.0 - 0.5)
    vy = np.float32((p1[1] - p0[1] + 0.5) % 1.0 - 0.5)
    rx = (xx - np.float32(p0[0]) + np.float32(0.5)) % np.float32(1.0) - np.float32(0.5)
    ry = (yy - np.float32(p0[1]) + np.float32(0.5)) % np.float32(1.0) - np.float32(0.5)
    t = (rx * vx + ry * vy) / (vx * vx + vy * vy + _EPS)
    t = np.clip(t, 0.0, 1.0)
    dx = rx - vx * t
    dy = ry - vy * t
    return dx * dx + dy * dy


def _vein_fields(height: int, width: int, seed: int) -> tuple[np.ndarray, np.ndarray]:
    """Build a sparse, branching vein network with periodic short segments."""

    xx = np.arange(width, dtype=np.float32)[None, :] / np.float32(width)
    yy = np.arange(height, dtype=np.float32)[:, None] / np.float32(height)
    rng = _rng(seed, 313)
    main = np.zeros((height, width), dtype=np.float32)
    secondary = np.zeros((height, width), dtype=np.float32)

    # Several compact networks avoid a stripe or spoke pattern.  Each trunk
    # gets two child branches from a random point along it; the segment count is
    # fixed and small, independent of output resolution.
    for network in range(3):
        root = (float(rng.random()), float(rng.random()))
        for trunk in range(3):
            angle = float(rng.uniform(0.0, 2.0 * math.pi))
            length = float(rng.uniform(0.18, 0.34))
            bend = float(rng.uniform(-0.35, 0.35))
            mid = (
                (root[0] + math.cos(angle) * length * 0.52 + 0.02 * math.cos(angle + math.pi * 0.5)) % 1.0,
                (root[1] + math.sin(angle) * length * 0.52 + 0.02 * math.sin(angle + math.pi * 0.5)) % 1.0,
            )
            end = (
                (root[0] + math.cos(angle + bend) * length) % 1.0,
                (root[1] + math.sin(angle + bend) * length) % 1.0,
            )
            d0 = _segment_distance(xx, yy, root, mid)
            d1 = _segment_distance(xx, yy, mid, end)
            radius = float(rng.uniform(0.0025, 0.0045))
            main = np.maximum(main, np.exp(-np.minimum(d0, d1) / np.float32(2.0 * radius * radius)).astype(np.float32))

            branch_point = (
                (mid[0] + ((end[0] - mid[0] + 0.5) % 1.0 - 0.5) * 0.30) % 1.0,
                (mid[1] + ((end[1] - mid[1] + 0.5) % 1.0 - 0.5) * 0.30) % 1.0,
            )
            for branch in range(2):
                branch_angle = angle + (1.0 if branch else -1.0) * float(rng.uniform(0.55, 1.18))
                branch_length = float(rng.uniform(0.10, 0.21))
                branch_end = (
                    (branch_point[0] + math.cos(branch_angle) * branch_length) % 1.0,
                    (branch_point[1] + math.sin(branch_angle) * branch_length) % 1.0,
                )
                bd = _segment_distance(xx, yy, branch_point, branch_end)
                br = float(rng.uniform(0.001, 0.0025))
                secondary = np.maximum(secondary, np.exp(-bd / np.float32(2.0 * br * br)).astype(np.float32))

    # Break edges gently with periodic noise so the veins have organic wear and
    # do not look like perfect vector strokes.
    break_up = np.float32(0.68) + np.float32(0.32) * _fbm(height, width, seed + 331, ((9, 11, 0.55), (23, 29, 0.30), (61, 73, 0.15)))
    main *= break_up
    secondary *= np.float32(0.76) + np.float32(0.24) * _tile_noise(height, width, 48, 57, seed + 347)
    return main, secondary


def _membrane_maps(height: int, width: int, color: np.ndarray, seed: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Thin warm-copper leather with a branching, vein-like support pattern."""

    large = _fbm(height, width, seed + 401, ((4, 5, 0.58), (8, 10, 0.28), (17, 21, 0.14)))
    medium = _fbm(height, width, seed + 419, ((11, 14, 0.54), (25, 31, 0.31), (52, 63, 0.15)))
    fine = _tile_noise(height, width, 112, 137, seed + 433)
    main_vein, secondary_vein = _vein_fields(height, width, seed + 449)

    mottle = np.float32(0.64) * large + np.float32(0.28) * medium + np.float32(0.08) * fine
    translucency = _smoothstep(0.26, 0.78, np.float32(0.62) * large + np.float32(0.38) * medium)
    thin_wrinkle = (medium - 0.5) * np.float32(0.004) + (fine - 0.5) * np.float32(0.002)
    height_map = thin_wrinkle + np.float32(0.011) * main_vein + np.float32(0.005) * secondary_vein

    # The warm red shift represents thin, translucent copper membrane colour
    # variation baked in RGB.  It is a pigment field, never a light direction.
    pigment = np.float32(0.46) + np.float32(0.90) * mottle
    base = color[None, None, :] * pigment[..., None]
    base += np.asarray((0.040, 0.013, -0.004), dtype=np.float32)[None, None, :] * translucency[..., None]
    base += np.asarray((0.020, 0.006, -0.002), dtype=np.float32)[None, None, :] * (medium * (1.0 - main_vein))[..., None]
    age = _smoothstep(0.48, 0.69, medium)
    base += np.asarray((0.055, 0.039, 0.019), dtype=np.float32)[None, None, :] * age[..., None]
    base *= (np.float32(0.89) + np.float32(0.22)*fine)[..., None]
    base *= np.float32(1.0) - np.float32(0.29) * main_vein[..., None] - np.float32(0.15) * secondary_vein[..., None]
    # Soft rusty vein shoulders keep the network readable after mipmapping.
    base += np.asarray((0.013, 0.003, -0.001), dtype=np.float32)[None, None, :] * (main_vein * (1.0 - secondary_vein))[..., None]

    occlusion = np.float32(0.96) - np.float32(0.17) * main_vein - np.float32(0.08) * secondary_vein
    occlusion -= np.float32(0.025) * (fine - 0.5)
    roughness = np.float32(0.76) + np.float32(0.075) * (0.5 - mottle) + np.float32(0.075) * main_vein + np.float32(0.025) * secondary_vein
    return _finish(base, height_map, occlusion, roughness, 0.13)


def _horn_maps(height: int, width: int, color: np.ndarray, seed: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Lengthwise horn growth striations, mottling, and worn ridges."""

    large = _fbm(height, width, seed + 503, ((4, 5, 0.56), (8, 10, 0.29), (16, 20, 0.15)))
    medium = _fbm(height, width, seed + 521, ((12, 15, 0.54), (26, 32, 0.31), (52, 64, 0.15)))
    fine = _tile_noise(height, width, 144, 181, seed + 539)
    # Horn UVs run around the circumference in x and along growth in y.  Warp
    # x slowly along y, so striations stay lengthwise but vary and weather.
    warp = (_fbm(height, width, seed + 557, ((5, 9, 0.58), (11, 17, 0.30), (21, 29, 0.12))) - 0.5) * np.float32(0.52)
    x = np.arange(width, dtype=np.float32)[None, :] / np.float32(width)
    phase = x * np.float32(19.0) + warp
    primary = np.sin(_TAU * phase)
    secondary = np.sin(_TAU * (phase * np.float32(1.73) + np.float32(0.17)))
    ridge = np.float32(0.5) + np.float32(0.40) * primary + np.float32(0.10) * secondary
    ridge = np.clip(ridge, 0.0, 1.0)
    groove = np.clip((np.float32(0.44) - ridge) * np.float32(2.5), 0.0, 1.0)

    abrasion = _tile_noise(height, width, 23, 31, seed + 571)
    wear = np.clip(
        np.float32(0.52)
        + np.float32(0.70) * (medium - 0.5)
        + np.float32(0.32) * (fine - 0.5)
        + np.float32(0.34) * (abrasion - 0.5),
        0.0,
        1.0,
    )
    worn_ridge = ridge * (np.float32(0.48) + np.float32(0.70) * wear)
    striation_visibility = np.float32(0.36) + np.float32(0.64) * wear
    groove *= striation_visibility
    height_map = (
        np.float32(0.007) * worn_ridge
        - np.float32(0.0085) * groove * (np.float32(0.64) + np.float32(0.46) * wear)
        + np.float32(0.002) * (fine - 0.5)
    ).astype(np.float32)

    mottle = np.float32(0.61) * large + np.float32(0.29) * medium + np.float32(0.10) * fine
    pigment = np.float32(0.79) + np.float32(0.25) * mottle
    pigment *= np.float32(0.95) + np.float32(0.09) * worn_ridge
    pigment *= np.float32(1.0) - np.float32(0.13) * groove * (np.float32(0.55) + np.float32(0.65) * wear)[...]
    # Abraded ridges expose a restrained, warm horn highlight rather than a
    # polished plastic stripe.
    worn_highlight = np.asarray((0.020, 0.013, 0.006), dtype=np.float32)
    base = color[None, None, :] * pigment[..., None]
    base += worn_highlight[None, None, :] * (worn_ridge * wear)[..., None]
    base += np.asarray((0.008, 0.005, 0.002), dtype=np.float32)[None, None, :] * (1.0 - wear)[..., None] * groove[..., None]

    occlusion = np.float32(0.97) - np.float32(0.13) * groove + np.float32(0.018) * worn_ridge
    roughness = np.float32(0.53) + np.float32(0.10) * groove + np.float32(0.065) * (0.5 - mottle) - np.float32(0.055) * worn_ridge * wear
    return _finish(base, height_map, occlusion, roughness, 0.17)


def _solid_maps(height: int, width: int, color: np.ndarray, seed: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    mottle = _fbm(height, width, seed + 601, ((5, 6, 0.62), (15, 19, 0.28), (48, 59, 0.10)))
    fine = _tile_noise(height, width, 96, 121, seed + 617)
    base = color[None, None, :] * (np.float32(0.88) + np.float32(0.18) * mottle + np.float32(0.025) * (fine - 0.5))[..., None]
    height_map = (mottle - 0.5) * np.float32(0.003) + (fine - 0.5) * np.float32(0.001)
    occlusion = np.float32(0.97) - np.float32(0.025) * (1.0 - mottle)
    roughness = np.float32(0.66) + np.float32(0.035) * (0.5 - mottle)
    return _finish(base, height_map, occlusion, roughness, 0.10)


def generate_maps(
    kind: str,
    color: Iterable[float],
    size: int | Tuple[int, int],
    seed: int = 0,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Generate Embercrest ``(base, normal, orm)`` RGB maps.

    Parameters
    ----------
    kind:
        ``"skin"`` for irregular bronze scutes, ``"membrane"`` for copper
        wing leather with branching veins, ``"horn"`` for worn lengthwise
        growth striations.  Any other value receives a restrained solid-map
        treatment suitable for the builder's eyes, teeth, and mouth materials.
    color:
        Three linear-ish colour floats in the same muted range used by
        ``build_embercrest.py``.  Values outside the display range are clipped
        only in the returned maps.
    size:
        Square output dimension, or a ``(height, width)`` pair for previews.
    seed:
        Deterministic variation seed; global NumPy RNG state is untouched.
    """

    height, width = _shape(size)
    color_array = np.asarray(tuple(color), dtype=np.float32)
    if color_array.shape != (3,):
        raise ValueError("color must contain exactly three values")
    if not np.all(np.isfinite(color_array)):
        raise ValueError("color must contain finite values")
    kind_key = str(kind).strip().lower()
    if kind_key == "skin":
        return _skin_maps(height, width, color_array, int(seed))
    if kind_key == "membrane":
        return _membrane_maps(height, width, color_array, int(seed))
    if kind_key in ("horn", "armor", "armour", "ridge"):
        return _horn_maps(height, width, color_array, int(seed))
    return _solid_maps(height, width, color_array, int(seed))
