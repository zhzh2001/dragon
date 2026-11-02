#!/usr/bin/env python3
"""Check that every baked DXIL fragment stage links to its vertex stage.

    tools/release/check_dxil_linkage.py SHADER_DIR

The bake (bake_shaders.sh) makes DXIL through SPIR-V. On that route a
fragment stage loses the inputs it does not read, and the inputs it keeps
are renumbered from register 0. D3D12 refuses a pipeline when a fragment
input is in a different register from the vertex output of the same
semantic: CreateGraphicsPipelineState returns E_INVALIDARG, and the
pipeline is "NOT built". In 0.3.0 this blacked out the sky and the ground
(the atmosphere tables read only SV_Position, and the foliage pre-pass
read only two of five varyings). A fragment stage that reads a prefix of
the vertex outputs links. This check reads the signatures out of each
<stem>.vertex.dxil / <stem>.fragment.dxil pair and exits 1 on a mismatch.
"""
import glob
import os
import struct
import sys

# Fragment inputs that the rasterizer makes, not the vertex stage.
GENERATED = {"SV_ISFRONTFACE", "SV_SAMPLEINDEX", "SV_PRIMITIVEID", "SV_COVERAGE"}


def signature(data, tag):
    """The (semantic, index) -> (register, mask) elements of one signature chunk."""
    if data[:4] != b"DXBC":
        raise ValueError("not a DXIL container")
    count = struct.unpack_from("<I", data, 28)[0]
    for offset in struct.unpack_from("<%dI" % count, data, 32):
        if data[offset:offset + 4] != tag:
            continue
        base = offset + 8
        elements, first = struct.unpack_from("<II", data, base)
        out = {}
        for i in range(elements):
            _stream, name_at, index, _sv, _type, register, mask = struct.unpack_from(
                "<IIIIIIB", data, base + first + i * 32)
            name = data[base + name_at:data.index(b"\0", base + name_at)].decode().upper()
            out[(name, index)] = (register, mask)
        return out
    return {}


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    pairs = 0
    failed = 0
    for fragment in sorted(glob.glob(os.path.join(sys.argv[1], "*.fragment.dxil"))):
        vertex = fragment.replace(".fragment.dxil", ".vertex.dxil")
        if not os.path.exists(vertex):
            continue
        pairs += 1
        with open(vertex, "rb") as f:
            outputs = signature(f.read(), b"OSG1")
        with open(fragment, "rb") as f:
            inputs = signature(f.read(), b"ISG1")
        errors = []
        for (name, index), (register, mask) in sorted(inputs.items()):
            if name in GENERATED:
                continue
            written = outputs.get((name, index))
            if written is None:
                errors.append(f"{name}{index} is not written by the vertex stage")
            elif written[0] != register or mask & ~written[1]:
                errors.append(f"{name}{index} is in register {register}, the vertex stage writes register {written[0]}")
        if errors:
            failed += 1
            stem = os.path.basename(fragment)[:-len(".fragment.dxil")]
            print(f"{stem}: " + "; ".join(errors), file=sys.stderr)
    if pairs == 0:
        print(f"no DXIL pairs in {sys.argv[1]}", file=sys.stderr)
        return 1
    if failed:
        print(f"{failed} of {pairs} DXIL pipelines do not link. A fragment stage must read a prefix "
              "of its vertex stage's outputs (shaders/atmosphere_common.hlsl, AtmoVertex).", file=sys.stderr)
        return 1
    print(f"ok: {pairs} DXIL pipelines link")
    return 0


if __name__ == "__main__":
    sys.exit(main())
