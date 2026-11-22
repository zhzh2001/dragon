"""Pack the native matrix stream from v7-pose-probe.cpp for the rig verifier."""
import argparse
import hashlib
from pathlib import Path

import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stream", type=Path)
    parser.add_argument("model", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    with args.stream.open("rb") as source:
        names = source.readline().decode().split()
        matrices = np.fromfile(source, "<f4").reshape(3, 300, len(names), 4, 4)
    # The engine stores matrices by column. The verifier uses three affine rows.
    skin = matrices.swapaxes(-1, -2)[..., :3, :]
    hashes = {}
    for kind in ("rig", "flight"):
        profile = Path(f"{args.model}.{kind}.cfg")
        hashes[kind] = hashlib.sha256(profile.read_bytes()).hexdigest()
    np.savez_compressed(args.output, names=names, skin=skin, **hashes)


if __name__ == "__main__":
    main()
