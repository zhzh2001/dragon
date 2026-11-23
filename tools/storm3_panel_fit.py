"""Fit the two Storm3 inner wing panels to the flank, arm and fourth rib."""
import json
import sys
from pathlib import Path

import numpy as np

sys.dont_write_bytecode = True
TOOLS = Path(__file__).resolve().parent
sys.path[:0] = [str(TOOLS), str(TOOLS / 'clips')]
from hide.glbio import read_glb, write_glb
from hide.meshdata import accessor, joint_names, primitive
from storm3_web import arm_contour, contour, fade
from wingbeat_studio import Studio


def unit(vector):
    return vector / np.maximum(np.linalg.norm(vector, axis=-1, keepdims=True), 1e-12)


def panel_domain(doc, blob, mesh):
    positions = mesh['POSITION']
    names = joint_names(doc)
    skin = doc['skins'][0]
    inverse = accessor(doc, blob, skin['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)
    heads = np.linalg.inv(inverse)[:, :3, 3]
    arm = heads[[names.index(f'wing_{part}_r') for part in ('root', 'arm', 'wrist')]]
    sections = np.asarray(json.loads((TOOLS / 'skeletons/storm3.json').read_text())['flight_web_sections'])
    span = abs(positions[:, 0])
    front = arm_contour(span, arm[:, [0, 2, 1]] * [1, -1, 1])
    hem = contour(span, sections[:, 0], sections[:, 3:5])
    chord = hem - front
    delta = positions[:, [2, 1]] * [-1, 1] - front
    fraction = np.sum(chord * delta, axis=1) / np.sum(chord * chord, axis=1)
    t = np.clip(fraction, 0, 1)
    inside = .047 + .031 * t
    outside = .231 - .029 * t
    u = np.clip((span - inside) / (outside - inside), 0, 1)
    moving = (np.array([n.startswith('wing_') for n in names])[mesh['JOINTS_0']] * mesh['WEIGHTS_0']).sum(1)
    mask = (fade((span - inside + .006) / .024) * fade((outside + .007 - span) / .017)
            * fade((fraction + .01) / .20) * (moving > 0))
    return span, t, u, mask


def shape_panel(mesh, names, span, t, u, mask):
    coords = mesh['POSITION'].copy()
    # Pull the hem towards the leading arm, with its endpoints fixed.
    bow = np.sin(np.pi * u) ** 1.5 * t ** 1.7 * mask
    coords[:, 2] -= .039 * bow
    coords[:, 1] += .012 * bow

    joints = mesh['JOINTS_0'].copy()
    weights = mesh['WEIGHTS_0'].copy()
    for side, sign in (('l', -1), ('r', 1)):
        ids = np.flatnonzero((mask > 0) & (sign * coords[:, 0] > 0))
        x, s, chord = span[ids], u[ids], t[ids]
        # Each side of the shell follows the same field from flank to rib.
        # The finger shares extend across the membrane towards its fixed inner edge.
        finger = s * fade((chord + .04) / .72)
        chest = (1 - s) * (.23 + .77 * chord)
        arm = 1 - chest - finger
        elbow = fade((x - .102) / .088)
        distal = fade((chord - .28) / .44)
        field = np.zeros((len(ids), len(names)))
        for bone, value in [('chest', chest), (f'wing_root_{side}', arm * (1 - elbow)),
                            (f'wing_arm_{side}', arm * elbow),
                            (f'wing_finger_4a_{side}', finger * (1 - distal)),
                            (f'wing_finger_4b_{side}', finger * distal)]:
            field[:, names.index(bone)] = value
        strength = mask[ids, None]
        field *= strength
        np.add.at(field, (np.arange(len(ids))[:, None], joints[ids]), (1 - strength) * weights[ids])
        nearest = np.argsort(-field, axis=1, kind='stable')[:, :4]
        kept = np.take_along_axis(field, nearest, axis=1)
        joints[ids] = nearest
        weights[ids] = kept / kept.sum(1, keepdims=True)
    return coords, joints, weights


def cruise_matrices():
    # Four points at each joint recover the rotation and translation from the studio's wing solver.
    studio = Studio('storm3')
    ids = np.repeat(np.arange(len(studio.names)), 4)
    studio.joints = ids[:, None]
    studio.weights = np.ones((len(ids), 1))
    basis = np.vstack([np.zeros(3), np.eye(3)])
    studio.offsets = np.tile(basis, (len(studio.names), 1)).reshape(-1, 1, 3)
    for sample in studio.samples():
        if sample.frame >= 294 or (sample.frame - 240) % 4:
            continue
        points = sample.positions.reshape(-1, 4, 3) / studio.scale
        matrix = (points[:, 1:] - points[:, :1]).transpose(0, 2, 1)
        yield matrix, points[:, 0] - np.einsum('bij,bj->bi', matrix, studio.bind)


def mesh_normals(coords, triangles, inverse, count):
    corners = coords[triangles]
    area = np.cross(corners[:, 1] - corners[:, 0], corners[:, 2] - corners[:, 0])
    accum = np.zeros((count, 3))
    for corner in range(3):
        np.add.at(accum, inverse[triangles[:, corner]], area)
    return unit(accum)[inverse]


def fit_normals(mesh, coords, joints, weights, mask):
    # Fit one rest normal to the areas of its posed triangles throughout cruise.
    # Shared positions connect the two sides of each texture seam.
    rows = np.flatnonzero(mask > 0)
    faces = mesh['indices']
    faces = faces[(mask[faces] > 0).any(1)]
    ids, local = np.unique(faces, return_inverse=True)
    faces = local.reshape(-1, 3)
    _, inv = np.unique(mesh['POSITION'][ids], axis=0, return_inverse=True)
    inv = inv.ravel()
    p, j, w = coords[ids], joints[ids], weights[ids]
    rest = mesh_normals(p, faces, inv, inv.max() + 1)
    normals = rest.copy() * 3
    for matrix, origin in cruise_matrices():
        rotation = np.einsum('vj,vjmn->vmn', w, matrix[j])
        posed = np.einsum('vij,vj->vi', rotation, p) + np.einsum('vj,vjn->vn', w, origin[j])
        geom = mesh_normals(posed, faces, inv, inv.max() + 1)
        normals += unit(np.linalg.solve(rotation, geom[:, :, None])[:, :, 0])
    new = mesh['NORMAL'].copy()
    core = np.searchsorted(ids, rows)
    factor = mask[rows, None]
    new[rows] = unit(new[rows] * (1 - factor) + unit(normals[core]) * factor)
    tangents = mesh['TANGENT'].copy()
    old = tangents[rows, :3]
    tangents[rows, :3] = unit(old - new[rows] * (old * new[rows]).sum(1, keepdims=True))
    return new, tangents


def build(source, target):
    assert source.resolve() != target.resolve()
    target.mkdir(exist_ok=True, parents=True)
    doc, blob = read_glb(source / 'storm3-raw.glb')
    mesh = primitive(doc, blob)
    span, t, u, mask = panel_domain(doc, blob, mesh)
    coords, joints, weights = shape_panel(mesh, joint_names(doc), span, t, u, mask)
    normals, tangents = fit_normals(mesh, coords, joints, weights, mask)
    updates = dict(POSITION=coords, JOINTS_0=joints, WEIGHTS_0=weights, NORMAL=normals, TANGENT=tangents)
    for filename in ('storm3-raw.glb', 'storm3.glb'):
        doc, original = read_glb(source / filename)
        blob = bytearray(original)
        attrs = doc['meshes'][0]['primitives'][0]['attributes']
        for key, values in updates.items():
            stream = accessor(doc, blob, attrs[key])
            assert np.array_equal(stream, mesh[key])
            stream[:] = values
        write_glb(target / filename, doc, blob, {})
    displacement = np.linalg.norm(coords - mesh['POSITION'], axis=1)
    print(f'Changed {len(np.unique(coords[mask > 0], axis=0))} distinct panel points.')
    print(f'Largest displacement: {displacement.max():.6f} model units.')


if __name__ == '__main__':
    build(*map(Path, sys.argv[sys.argv.index('--') + 1:]))
