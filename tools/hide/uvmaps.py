"""Rasterize per-vertex values of the mesh into the texture's UV space.

The hide retexture needs to know which texels belong to the wing membrane,
the horns and the claws. The colours alone do not say: a dark scale and a
dark horn have the same pixels. The mesh does say, through its skin weights
and bind-pose positions, so this module draws those per-vertex values into
the atlas with the same UVs the engine samples.
"""

import numpy as np


def rasterize(uv, indices, values, size):
    """Return (image, covered): `values` (N, C) interpolated over each triangle.

    A texel is covered when its centre lies inside a triangle, or within
    half a texel of one, so thin charts leave no holes. glTF's UV origin is
    the top left corner of the image, so v maps to rows without a flip.
    """
    values = np.asarray(values, dtype=np.float32)
    if values.ndim == 1:
        values = values[:, None]
    channels = values.shape[1]
    image = np.zeros((size, size, channels), dtype=np.float32)
    covered = np.zeros((size, size), dtype=bool)
    px = uv[:, 0].astype(np.float64) * size - 0.5
    py = uv[:, 1].astype(np.float64) * size - 0.5
    slack = 0.75
    for a, b, c in indices:
        x0, y0, x1, y1, x2, y2 = px[a], py[a], px[b], py[b], px[c], py[c]
        left = max(int(np.floor(min(x0, x1, x2) - slack)), 0)
        right = min(int(np.ceil(max(x0, x1, x2) + slack)), size - 1)
        top = max(int(np.floor(min(y0, y1, y2) - slack)), 0)
        bottom = min(int(np.ceil(max(y0, y1, y2) + slack)), size - 1)
        if right < left or bottom < top:
            continue
        area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if abs(area) < 1e-12:
            continue
        xs = np.arange(left, right + 1, dtype=np.float64)[None, :]
        ys = np.arange(top, bottom + 1, dtype=np.float64)[:, None]
        w0 = ((x1 - xs) * (y2 - ys) - (x2 - xs) * (y1 - ys)) / area
        w1 = ((x2 - xs) * (y0 - ys) - (x0 - xs) * (y2 - ys)) / area
        w2 = 1.0 - w0 - w1
        # The slack in barycentric units: 0.75 texel over the triangle's size.
        edge = slack / max(np.sqrt(abs(area)), 1.0)
        inside = (w0 >= -edge) & (w1 >= -edge) & (w2 >= -edge)
        if not inside.any():
            continue
        w0 = np.clip(w0, 0.0, 1.0)
        w1 = np.clip(w1, 0.0, 1.0)
        w2 = np.clip(w2, 0.0, 1.0)
        total = w0 + w1 + w2
        block = (w0[..., None] * values[a] + w1[..., None] * values[b] + w2[..., None] * values[c]) / total[..., None]
        target = image[top:bottom + 1, left:right + 1]
        seen = covered[top:bottom + 1, left:right + 1]
        target[inside] = block[inside]
        seen |= inside
    return image, covered


def fill_uncovered(image, covered):
    """Give every uncovered texel the value of its nearest covered texel.

    The atlas has padding between its charts, and the mip chain and the
    bilinear filter read it. A region mask that stopped at the chart edge
    would leave a seam there.
    """
    # Push-pull: average the covered texels down a pyramid, then fill each
    # level's holes from the level below it. The size must be a power of two.
    weight = covered.astype(np.float32)[..., None]
    levels = [(image * weight, weight)]
    while levels[-1][0].shape[0] > 1:
        value, w = levels[-1]
        h, wd = value.shape[0] // 2, value.shape[1] // 2
        value = value.reshape(h, 2, wd, 2, -1).sum(axis=(1, 3))
        w = w.reshape(h, 2, wd, 2, 1).sum(axis=(1, 3))
        levels.append((value, w))
    value, w = levels[-1]
    filled = value / np.maximum(w, 1e-6)
    for value, w in reversed(levels[:-1]):
        up = np.repeat(np.repeat(filled, 2, axis=0), 2, axis=1)
        have = w > 0
        filled = np.where(have, value / np.maximum(w, 1e-6), up)
    return filled


def box_blur(image, radius, passes=3):
    """Blur a float image with `passes` box filters: close to a Gaussian.

    Edges are extended, not wrapped. Uses only cumulative sums, so the result
    does not depend on a library's filter implementation.
    """
    out = np.asarray(image, dtype=np.float32)
    squeeze = out.ndim == 2
    if squeeze:
        out = out[..., None]
    radius = int(radius)
    if radius < 1:
        return out[..., 0] if squeeze else out
    width = 2 * radius + 1
    for _ in range(passes):
        for axis in (0, 1):
            pad = [(0, 0)] * out.ndim
            pad[axis] = (radius + 1, radius)
            padded = np.pad(out, pad, mode="edge").astype(np.float64)
            total = np.cumsum(padded, axis=axis)
            upper = [slice(None)] * out.ndim
            lower = [slice(None)] * out.ndim
            upper[axis] = slice(width, None)
            lower[axis] = slice(0, -width)
            out = ((total[tuple(upper)] - total[tuple(lower)]) / width).astype(np.float32)
    return out[..., 0] if squeeze else out
