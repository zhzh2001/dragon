"""Check intersections between the outer surfaces of the posed wing ribs in Blender."""
import argparse
from pathlib import Path
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import numpy as np
from mathutils.bvhtree import BVHTree
from wingbeat_studio import Studio


def rib_patches(studio):
    """Keep triangles carried by one rib, beyond its shared knuckle."""
    positions = (studio.offsets + studio.bind[studio.joints] * studio.weights[:, :, None]).sum(axis=1)
    owner = np.full(len(studio.ids), -1)
    names = []
    for wing in studio.wings:
        for chain in wing.ribs:
            origin = studio.bind[chain[0]]
            direction = studio.bind[chain[-1]] + studio.leaf_offsets[chain[-1]] - origin
            distance = ((positions - origin) @ direction) / (direction @ direction)
            weight = (studio.weights * np.isin(studio.joints, chain)).sum(axis=1)
            owner[(weight > .85) & (distance > .25)] = len(names)
            names.append(studio.names[chain[0]])

    index = np.full(len(studio.owner), -1)
    index[studio.ids] = np.arange(len(studio.ids))
    faces = index[studio.triangles]
    faces = faces[(faces >= 0).all(axis=1)]
    patches = []
    for i, name in enumerate(names):
        triangles = faces[(owner[faces] == i).all(axis=1)]
        if not len(triangles):
            raise ValueError(f'{name} had no outer surface. Check the source weights.')
        vertices, indices = np.unique(triangles, return_inverse=True)
        patches.append((name, vertices, indices.reshape(-1, 3).tolist()))
    return patches


def check(studio, climb):
    patches = rib_patches(studio)
    frames = 0
    for sample in studio.samples(climb):
        trees = [BVHTree.FromPolygons(sample.positions[ids].tolist(), triangles, all_triangles=True)
                 for _, ids, triangles in patches]
        for i, tree in enumerate(trees):
            for j in range(i + 1, len(trees)):
                if tree.overlap(trees[j]):
                    print(f'FAIL frame {sample.frame} intersects {patches[i][0]} and {patches[j][0]}')
                    return False
        frames += 1
    count = sum(len(triangles) for _, _, triangles in patches)
    clip = 'flap_climb' if climb else 'flap'
    print(f'PASS {clip} {frames} frames, {len(patches)} outer rib patches, {count} triangles, zero intersecting pairs')
    return True


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('model', choices=('tide2', 'storm3', 'gold-dragon', 'silver-dragon'))
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    studio = Studio(args.model)
    if not all(check(studio, climb) for climb in (False, True)):
        raise SystemExit(1)
