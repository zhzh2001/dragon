"""Count inner wing intersections and compare shading normals with triangle normals."""
import sys
from pathlib import Path

import numpy as np
from mathutils.bvhtree import BVHTree

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from hide.glbio import read_glb
from hide.meshdata import joint_names, primitive


def check(source, model):
    cache = source.parent
    original = primitive(*read_glb(source / 'storm3.glb'))
    doc, blob = read_glb(model)
    mesh = primitive(doc, blob)
    names = joint_names(doc)
    x, y, z = original['POSITION'].T
    wing = np.array([name.startswith('wing_') for name in names])
    moving = (wing[original['JOINTS_0']] * original['WEIGHTS_0']).sum(1)
    # Measure the same shell and arm border before and after the repair.
    region = ((abs(x) > .045) & (abs(x) < .222) & (y > .13)
              & (z < -.025) & (z > -.285) & (moving > 0))
    tris = original['indices']
    tris = tris[region[tris].all(1)]
    ids, local = np.unique(tris, return_inverse=True)
    faces = local.reshape(-1, 3)
    _, position = np.unique(original['POSITION'][ids], axis=0, return_inverse=True)
    welded = position.ravel()[faces]
    order = (cache / 'joints.txt').read_text().splitlines()
    joints = np.array([order.index(name) for name in names])[mesh['JOINTS_0'][ids]]
    weights = mesh['WEIGHTS_0'][ids]
    frame_sets = [('beat', range(200, 254)), ('procedural', range(200, 254)),
                  ('glide', [200]), ('turn', [260]), ('dive', [300]), ('stand', [200])]
    print(f'{len(ids)} vertices and {len(faces)} triangles')
    for mode, frames in frame_sets:
        matrices = np.fromfile(cache / f'{mode}.bin', dtype='<f4').reshape(-1, len(order), 4, 4)
        counts, opposed = [], []
        for frame in frames:
            blend = np.einsum('vi,vimn->vmn', weights, matrices[frame - 1].transpose(0, 2, 1)[joints])
            rot = blend[:, :3, :3]
            points = np.einsum('vij,vj->vi', rot, mesh['POSITION'][ids]) + blend[:, :3, 3]
            tree = BVHTree.FromPolygons(points.tolist(), faces.tolist(), all_triangles=True)
            pairs = np.asarray(tree.overlap(tree), int).reshape(-1, 2)
            a, b = pairs[pairs[:, 0] < pairs[:, 1]].T
            distinct = ~(welded[a, :, None] == welded[b, None, :]).any(axis=(1, 2))
            counts.append(int(distinct.sum()))
            normals = np.einsum('vij,vj->vi', rot, mesh['NORMAL'][ids])
            a, b, c = points[faces].transpose(1, 0, 2)
            geometric = np.cross(b - a, c - a)
            opposed.append(int(((geometric * normals[faces].mean(1)).sum(1) < 0).sum()))
        print(f'{mode}: {len(frames)} frames. Intersecting pairs {min(counts)} to {max(counts)}. '
              f'Opposed normals {min(opposed)} to {max(opposed)}.')
        if len(frames) > 1:
            print('Capture frames:', [(f, counts[f - frames[0]], opposed[f - frames[0]])
                                      for f in [200, 207, 214, 220, 227, 234, 240, 247]])


if __name__ == '__main__':
    check(*map(Path, sys.argv[sys.argv.index('--') + 1:]))
