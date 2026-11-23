"""Fit the two Storm3 inner wing panels to the flank, arm and fourth rib."""
import json
import sys
from pathlib import Path

import numpy as np

sys.dont_write_bytecode = True
TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
from hide.glbio import read_glb, write_glb
from hide.meshdata import accessor, joint_names, primitive
from storm3_web import arm_contour, contour, fade


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


def fitting_poses(cache, names):
    order = (cache / 'joints.txt').read_text().splitlines()
    slots = [order.index(name) for name in names]
    poses = []
    for mode, frames in [('beat', range(200, 254, 4)), ('procedural', range(200, 254, 4)),
                         ('stand', [200]), ('glide', [200]), ('turn', [260]), ('dive', [300])]:
        path = cache / (mode + '.bin')
        matrices = np.fromfile(path, '<f4').reshape(-1, len(order), 4, 4).transpose(0, 1, 3, 2)
        poses.extend(matrices[np.array(frames) - 1][:, slots])
    return np.asarray(poses, dtype=float)


def fit_thickness(mesh, coords, joints, weights, domain, poses):
    from mathutils import kdtree
    span, t, u, mask = domain
    base = mesh['POSITION']
    adjust = np.flatnonzero(mask > 0)
    _, inverse = np.unique(base[adjust], axis=0, return_inverse=True)
    _, first = np.unique(inverse.ravel(), return_index=True)
    ids = adjust[first]
    # Neighbouring points on both sides sample one midsurface.
    # The fourth coordinate distinguishes the sides when a section contains more vertices on one side.
    chart = np.c_[base[:, 0], base[:, 2] - base[:, 1] * .75]
    pool = np.flatnonzero((span < .26) & (base[:, 1] > .12) & (base[:, 2] < -.015))
    _, first = np.unique(base[pool], axis=0, return_index=True)
    pool = pool[first]
    trees = kdtree.KDTree(len(pool))
    for i in pool:
        trees.insert((*chart[i], 0), int(i))
    trees.balance()
    neighbors = np.array([[row[1] for row in trees.find_n((*chart[i], 0), 60)] for i in ids])
    delta = chart[neighbors] - chart[ids, None]
    distance = np.linalg.norm(delta, axis=2)
    side = mesh['NORMAL'][:, 1] + mesh['NORMAL'][:, 2] * .75
    side = np.clip(side / .5, -1, 1)
    basis = np.concatenate([np.ones_like(delta[:, :, :1]), delta / .02, side[neighbors, None]], axis=2)
    gaussian = np.exp(-.5 * (distance / .014) ** 2)
    normal = np.einsum('vna,vnb,vn->vab', basis, basis, gaussian) + np.eye(4)[None] * .0001
    coeff = np.linalg.solve(normal, basis.transpose(0, 2, 1) * gaussian[:, None, :])

    def frames(pos):
        plane = np.einsum('vkn,vnd->vkd', coeff, pos[neighbors])
        normal = unit(np.cross(plane[:, 1], plane[:, 2]))
        frame = np.stack([plane[:, 1], plane[:, 2], normal], axis=2)
        return plane[:, 0], frame

    center, rest_frame = frames(coords)
    offset = np.einsum('vij,vj->vi', np.linalg.pinv(rest_frame), coords[ids] - center)
    js = joints[ids].copy()
    original_w = weights[ids].copy()
    original_p = coords[ids].copy()
    targets = []
    for pose in poses:
        rot = pose[:, :3, :3]
        trans = pose[:, :3, 3]
        q = np.einsum('vi,vijk,vk->vj', weights, rot[joints], coords) + np.einsum('vi,vij->vj', weights, trans[joints])
        mid, posed_frame = frames(q)
        target = mid + np.einsum('vij,vj->vi', posed_frame, offset)
        targets.append(q[ids] + mask[ids,None] * (target - q[ids]))
    target = np.array(targets).transpose(1, 0, 2)
    p = original_p.copy()
    w = original_w.copy()
    bone = poses[:, js].transpose(1, 0, 2, 3, 4)
    for _ in range(5):
        blend = np.einsum('vi,vfijk->vfjk', w, bone)
        current = np.einsum('vfij,vj->vfi', blend[:, :, :3, :3], p) + blend[:, :, :3, 3]
        qs = np.einsum('vfsij,vj->vfsi', bone[:, :, :, :3, :3], p) + bone[:, :, :, :3, 3]
        dw = (qs[:, :, 1:] - qs[:, :, :1]).transpose(0, 1, 3, 2)
        jac = np.concatenate([blend[:, :, :3, :3], dw], axis=-1).reshape(len(ids), -1, 6)
        residual = (target - current).reshape(len(ids), -1)
        regular = np.array([3., 3., 3., .0015, .0015, .0015])
        hessian = jac.transpose(0, 2, 1) @ jac + np.diag(regular)
        rhs = np.einsum('vni,vn->vi', jac, residual)
        rhs[:, :3] += (original_p - p) * regular[:3]
        rhs[:, 3:] += (original_w[:, 1:] - w[:, 1:]) * regular[3:]
        step = np.linalg.solve(hessian, rhs[:, :, None])[:, :, 0]
        p += np.clip(step[:, :3], -.012, .012)
        w[:, 1:] += step[:, 3:]
        w[:, 0] -= step[:, 3:].sum(1)
        w = np.maximum(w, 0)
        w /= w.sum(1, keepdims=True)
    # Keep the corrections continuous at the existing shell and shoulder boundaries.
    amount = mask[ids, None]
    coords[adjust] = (original_p + (p - original_p) * amount)[inverse.ravel()]
    weights[adjust] = (original_w + (w - original_w) * amount)[inverse.ravel()]


def smooth_contacts(mesh, coords, joints, weights, mask, poses):
    from mathutils.bvhtree import BVHTree
    base = mesh['POSITION']
    points, first, inverse = np.unique(base, axis=0, return_index=True, return_inverse=True)
    inverse = inverse.ravel()
    faces = mesh['indices']
    faces = inverse[faces[(mask[faces] > .03).any(1)]]
    ids, local = np.unique(faces, return_inverse=True)
    faces = local.reshape(-1, 3)
    rows = first[ids]
    local_mask = mask[rows]
    neighborhood = [set() for i in ids]
    for a, b, c in faces:
        neighborhood[a].update((b, c))
        neighborhood[b].update((a, c))
        neighborhood[c].update((a, b))
    w = np.zeros((len(ids), poses.shape[1]))
    np.add.at(w, (np.arange(len(ids))[:, None], joints[rows]), weights[rows])
    p = coords[rows].copy()
    for iteration in range(8):
        near = np.argsort(-w, axis=1, kind='stable')[:, :4]
        value = np.take_along_axis(w, near, axis=1)
        value = value / value.sum(1, keepdims=True)
        marked = np.zeros(len(ids), bool)
        for pose in poses:
            blend = np.einsum('vj,vjmn->vmn', value, pose[near])
            posed = np.einsum('vij,vj->vi', blend[:, :3, :3], p) + blend[:, :3, 3]
            tree = BVHTree.FromPolygons(posed.tolist(), faces.tolist(), all_triangles=True)
            pairs = np.asarray(tree.overlap(tree), int).reshape(-1, 2)
            a, b = pairs[pairs[:, 0] < pairs[:, 1]].T
            distinct = ~(faces[a, :, None] == faces[b, None, :]).any(axis=(1, 2))
            marked[faces[a[distinct]]] = True
            marked[faces[b[distinct]]] = True
        active = np.flatnonzero(marked & (local_mask > .03))
        if not len(active):
            break
        p_new, w_new = p.copy(), w.copy()
        for i in active:
            adjacent = list(neighborhood[i])
            edge = np.linalg.norm(points[ids[adjacent]] - points[ids[i]], axis=1)
            scale = 1 / np.maximum(edge, .0001)
            scale /= scale.sum()
            blend = .52 * local_mask[i]
            p_new[i] += blend * (scale @ p[adjacent] - p[i])
            w_new[i] += blend * (scale @ w[adjacent] - w[i])
        p, w = p_new, w_new
    near = np.argsort(-w, axis=1, kind='stable')[:, :4]
    value = np.take_along_axis(w, near, axis=1)
    value /= value.sum(1, keepdims=True)
    update = np.flatnonzero(mask > .03)
    selected = np.searchsorted(ids, inverse[update])
    coords[update] = p[selected]
    joints[update] = near[selected]
    weights[update] = value[selected]


def mesh_normals(coords, triangles, inverse, count):
    corners = coords[triangles]
    area = np.cross(corners[:, 1] - corners[:, 0], corners[:, 2] - corners[:, 0])
    accum = np.zeros((count, 3))
    for corner in range(3):
        np.add.at(accum, inverse[triangles[:, corner]], area)
    return unit(accum)[inverse]


def fit_normals(mesh, coords, joints, weights, mask, poses):
    # Fit the rest normal to the adjoining triangles across the two beats.
    # A camera at a grazing angle can otherwise see the opposite face's lighting.
    rows = np.flatnonzero(mask > 0)
    faces = mesh['indices']
    faces = faces[(mask[faces] > 0).any(1)]
    ids, local = np.unique(faces, return_inverse=True)
    faces = local.reshape(-1, 3)
    _, inv = np.unique(mesh['POSITION'][ids], axis=0, return_inverse=True)
    inv = inv.ravel()
    p, j, w = coords[ids], joints[ids], weights[ids]
    count = inv.max() + 1
    rest = mesh_normals(p, faces, inv, count)
    normals = rest.copy() * 3
    rays, limits = [], []
    center = (coords.max(0) + coords.min(0)) / 2
    scale = 14.65 / np.ptp(coords[:, 0])
    for pose in poses:
        blend = np.einsum('vj,vjmn->vmn', w, pose[j])
        rot = blend[:, :3, :3]
        q = np.einsum('vij,vj->vi', rot, p) + blend[:, :3, 3]
        geom = mesh_normals(q, faces, inv, count)
        normals += unit(np.linalg.solve(rot, geom[:, :, None])[:, :, 0])
        for angle, distance, elevation in [(223, 12.6, -1), (200, 8, 35), (320, 8, -35), (0, 16, 14)]:
            a, e = np.radians([angle, elevation])
            camera = np.array([np.sin(a) * np.cos(e), np.sin(e), np.cos(a) * np.cos(e)])
            toward = unit(distance * camera - (q - center) * scale)
            dot = (toward * geom).sum(1)
            rays.append(np.einsum('vji,vj->vi', rot, toward) * np.sign(dot)[:, None])
            limits.append(np.clip(abs(dot) - .08, -.05, .18))
    rays = np.asarray(rays)
    limits = np.asarray(limits)
    normal = unit(normals)
    start = normal.copy()
    for _ in range(15):
        gap = np.maximum(0, limits - np.einsum('nvi,vi->nv', rays, normal))
        gradient = np.einsum('nv,nvi->vi', gap, rays)
        normal = unit(normal + .7 * gradient / (1 + (gap > 0).sum(0)[:,None]) + .025 * (start - normal))
    new = mesh['NORMAL'].copy()
    factor = mask[rows, None]
    new[rows] = unit(new[rows] * (1 - factor) + normal[np.searchsorted(ids, rows)] * factor)
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
    poses = fitting_poses(Path('build-rig-cache'), joint_names(doc))
    fit_thickness(mesh, coords, joints, weights, (span, t, u, mask), poses)
    smooth_contacts(mesh, coords, joints, weights, mask, poses[:28:2])
    normals, tangents = fit_normals(mesh, coords, joints, weights, mask, poses)
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
