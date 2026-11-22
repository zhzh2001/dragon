"""Fit Storm3's inner membranes between the posed wing arms and trailing hems."""
import numpy as np


def contour(span, stations, yz):
    return np.column_stack([np.interp(span, stations, component) for component in yz.T])


def arm_contour(span, arm):
    # Extend the upper arm to the inner edge of the shoulder.
    inside = arm[0, 1:] + .2 * (arm[0, 1:] - arm[1, 1:])
    return contour(span, np.r_[.034, abs(arm[:, 0])], np.vstack([inside, arm[:, 1:]]))


def section_normal(chord):
    return np.column_stack([-chord[:, 1], chord[:, 0]]) / np.linalg.norm(chord, axis=1)[:, None]


def fade(t):
    t = np.clip(t, 0, 1)
    return t * t * (3 - 2 * t)


def fair_inner_web(original, posed, names, deltas, spec):
    # Each measured section gives span, standing hem YZ, and flight hem YZ in Blender coordinates.
    sections = np.array(spec['flight_web_sections'])
    joints = {name: start for name, start, _, _ in spec['bones']}
    for side, sign in [('l', -1), ('r', 1)]:
        span = sign * original[:, 0]
        rows = np.flatnonzero((span > .029) & (span < sections[-1, 0]) & (original[:, 1] > .237))
        span = span[rows]
        standing = original[rows][:, [2, 1]] * [-1, 1]
        flight = posed[rows][:, [2, 1]] * [-1, 1]
        bones = [f'wing_{part}_{side}' for part in ('root', 'arm', 'wrist')]
        arm = np.array([joints[name] for name in bones])
        gltf_arm = arm[:, [0, 2, 1]] * [1, 1, -1]
        transforms = deltas[[names.index(name) for name in bones]]
        arm_posed = np.einsum('bij,bj->bi', transforms[:, :3, :3], gltf_arm) + transforms[:, :3, 3]
        front = arm_contour(span, arm)
        new_front = arm_contour(span, arm_posed[:, [0, 2, 1]] * [1, -1, 1])
        chord = contour(span, sections[:, 0], sections[:, 1:3]) - front
        new_chord = contour(span, sections[:, 0], sections[:, 3:5]) - new_front

        # Preserve both faces' distance from the chord, including the rounded hem.
        relative = standing - front
        fraction = (relative * chord).sum(axis=1) / (chord * chord).sum(axis=1)
        depth = (relative * section_normal(chord)).sum(axis=1)
        surface = new_front + fraction[:, None] * new_chord + depth[:, None] * section_normal(new_chord)

        # Blend into the flank, the fourth rib, and the thick leading arm.
        weight = (fade((span - .029) / .014) * fade((.211 - span) / .035)
                  * fade((fraction - .06) / .28) * fade((standing[:, 1] - .237) / .020)
                  * fade((.034 - abs(depth)) / .017))
        active = weight > 0
        flight[active] += (surface[active] - flight[active]) * weight[active, None]
        posed[rows[active], 2] = -flight[active, 0]
        posed[rows[active], 1] = flight[active, 1]
