#!/usr/bin/env python3
"""Convert a LightWave LWO2 object to a static glTF binary.

    tools/3dmark/lwo2gltf.py IN.lwo OUT.glb [--scale S] [--textures DIR]

What survives: points, polygons (fans for anything past a triangle), one
glTF material per LightWave surface with its base colour (COLR), the UV map
each surface's first image layer names (VMAP TXUV, with VMAD per-polygon
overrides: a vertex used with two different UVs is split), and the weight
maps (VMAP WGHT) written as COLOR_1-style extras for a rigger to read. No
skin and no skeleton: the engine's loader wants a rigged mesh, and the rig
is a separate job (tools/skeletons/, docs/MODEL_GENERATION.md). Images are
not embedded: the archive this was written for keeps its textures unnamed
(docs/PORTING.md, "The 3DMark2001 dragon"); --textures DIR looks for
<clip name>.jpg/.png/.dds there and references what it finds.

Written for the 3DMark2001 dragon; LightWave 6-era files only (LWO2).
Layer metadata also retains COLR/DIFF/LUMI channels, opacity, projection and
mapping transforms. A second DIFF UV channel and source-point indices are
accessors so a local baker can preserve seams and collapse supported layers.
"""
import argparse
import json
import os
import struct
import sys


def read_chunks(data, start, end):
    pos = start
    while pos + 8 <= end:
        tag = data[pos:pos + 4]
        size = struct.unpack_from(">I", data, pos + 4)[0]
        yield tag, data[pos + 8:pos + 8 + size]
        pos += 8 + size + (size & 1)


def read_subchunks(body, start=0):
    pos = start
    while pos + 6 <= len(body):
        tag = body[pos:pos + 4]
        size = struct.unpack_from(">H", body, pos + 4)[0]
        yield tag, body[pos + 6:pos + 6 + size]
        pos += 6 + size + (size & 1)


def read_vx(body, pos):
    """A variable-length index: 2 bytes, or 4 with the top byte 0xFF."""
    if body[pos] == 0xFF:
        return struct.unpack_from(">I", body, pos)[0] & 0x00FFFFFF, pos + 4
    return struct.unpack_from(">H", body, pos)[0], pos + 2


def read_s0(body, pos):
    end = body.index(b"\0", pos)
    s = body[pos:end].decode("latin1")
    pos = end + 1
    if pos & 1:
        pos += 1
    return s, pos


def image_layer(body):
    """Keep channel, opacity and projection rather than treating DIFF as COLR."""
    kind = body[:4].decode('ascii')
    size = struct.unpack_from('>H', body, 4)[0]
    header = body[6:6 + size]
    ordinal, start = read_s0(header, 0)
    layer = {'kind': kind, 'ordinal': ordinal, 'enabled': True,
             'channel': None, 'opacity': 1., 'blend': 0, 'uv': None,
             'clip': None, 'projection': None, 'axis': 2,
             'center': [0., 0., 0.], 'size': [1., 1., 1.],
             'rotation': [0., 0., 0.], 'wrap': [1, 1]}
    for tag, data in read_subchunks(header, start):
        if tag == b'CHAN': layer['channel'] = data.decode('ascii')
        elif tag == b'ENAB': layer['enabled'] = bool(struct.unpack_from('>H', data)[0])
        elif tag == b'OPAC':
            layer['blend'], layer['opacity'] = struct.unpack_from('>Hf', data)
        elif tag == b'NEGA': layer['negative'] = bool(struct.unpack_from('>H', data)[0])
    for tag, data in read_subchunks(body, 6 + size + (size & 1)):
        if tag == b'VMAP': layer['uv'], _ = read_s0(data, 0)
        elif tag == b'IMAG': layer['clip'], _ = read_vx(data, 0)
        elif tag == b'PROJ': layer['projection'] = struct.unpack_from('>H', data)[0]
        elif tag == b'AXIS': layer['axis'] = struct.unpack_from('>H', data)[0]
        elif tag == b'WRAP': layer['wrap'] = list(struct.unpack_from('>HH', data))
        elif tag in (b'WRPW', b'WRPH'): layer[tag.decode().lower()] = struct.unpack_from('>f', data)[0]
        elif tag == b'TMAP':
            for sub, value in read_subchunks(data):
                key = {b'CNTR': 'center', b'SIZE': 'size', b'ROTA': 'rotation'}.get(sub)
                if key: layer[key] = list(struct.unpack_from('>3f', value))
    return layer


def parse(data):
    assert data[:4] == b"FORM" and data[8:12] == b"LWO2", "not an LWO2"
    size = struct.unpack_from(">I", data, 4)[0]
    points, polys, tags, ptag, uv_maps, uv_overrides, weights, clips, surfaces = [], [], [], {}, {}, {}, {}, {}, {}
    for tag, body in read_chunks(data, 12, 8 + size):
        if tag == b"PNTS":
            points = [struct.unpack_from(">3f", body, i) for i in range(0, len(body), 12)]
        elif tag == b"TAGS":
            tags = [t.decode("latin1") for t in body.split(b"\0") if t]
        elif tag == b"POLS":
            kind = body[:4]
            pos = 4
            while pos < len(body):
                count = struct.unpack_from(">H", body, pos)[0] & 0x3FF
                pos += 2
                poly = []
                for _ in range(count):
                    v, pos = read_vx(body, pos)
                    poly.append(v)
                polys.append(poly if kind == b"FACE" else None)
        elif tag == b"PTAG" and body[:4] == b"SURF":
            pos = 4
            while pos < len(body):
                poly, pos = read_vx(body, pos)
                ptag[poly] = struct.unpack_from(">H", body, pos)[0]
                pos += 2
        elif tag in (b"VMAP", b"VMAD"):
            kind = body[:4]
            dim = struct.unpack_from(">H", body, 4)[0]
            name, pos = read_s0(body, 6)
            if kind == b"TXUV" and dim == 2:
                table = uv_maps.setdefault(name, {}) if tag == b"VMAP" else uv_overrides.setdefault(name, {})
                while pos < len(body):
                    v, pos = read_vx(body, pos)
                    if tag == b"VMAD":
                        p, pos = read_vx(body, pos)
                        table[(p, v)] = struct.unpack_from(">2f", body, pos)
                    else:
                        table[v] = struct.unpack_from(">2f", body, pos)
                    pos += 8
            elif kind == b"WGHT" and dim == 1 and tag == b"VMAP":
                table = weights.setdefault(name, {})
                while pos < len(body):
                    v, pos = read_vx(body, pos)
                    table[v] = struct.unpack_from(">f", body, pos)[0]
                    pos += 4
        elif tag == b"CLIP":
            index = struct.unpack_from(">I", body, 0)[0]
            for sub, sbody in read_subchunks(body, 4):
                if sub == b"STIL":
                    clips[index], _ = read_s0(sbody, 0)
        elif tag == b"SURF":
            name, pos = read_s0(body, 0)
            _, pos = read_s0(body, pos)  # the parent surface
            surf = {"color": (0.78, 0.78, 0.78), "uv": None, "clip": None,
                    "diffuse": 1., "specular": 0., "glossiness": .4,
                    "luminosity": 0., "layers": []}
            for sub, sbody in read_subchunks(body, pos):
                if sub == b"COLR":
                    surf["color"] = struct.unpack_from(">3f", sbody, 0)
                elif sub in (b'DIFF', b'SPEC', b'GLOS', b'LUMI'):
                    surf[{b'DIFF':'diffuse', b'SPEC':'specular', b'GLOS':'glossiness', b'LUMI':'luminosity'}[sub]] = struct.unpack_from('>f', sbody)[0]
                elif sub == b"BLOK":
                    surf['layers'].append(image_layer(sbody))
                    # The first image map layer: its VMAP (the UV set) and IMAG (the clip).
                    header_tag = sbody[:4]
                    header_size = struct.unpack_from(">H", sbody, 4)[0]
                    rest = sbody[6 + header_size + (header_size & 1):]
                    uv, clip = None, None
                    for b_tag, b_body in read_subchunks(rest):
                        if b_tag == b"VMAP" and uv is None:
                            uv, _ = read_s0(b_body, 0)
                        elif b_tag == b"IMAG" and clip is None:
                            clip, _ = read_vx(b_body, 0)
                        elif b_tag == b"TMAP":
                            for t_tag, t_body in read_subchunks(b_body):
                                pass
                    if header_tag == b"IMAP" and surf["uv"] is None and uv:
                        surf["uv"], surf["clip"] = uv, clip
            surfaces[name] = surf
    return points, polys, tags, ptag, uv_maps, uv_overrides, weights, clips, surfaces


def build_gltf(points, polys, tags, ptag, uv_maps, uv_overrides, weights, clips, surfaces, scale, textures_dir):
    # One primitive per surface. A vertex is (point, uv) so VMAD splits where it must.
    by_surface = {}
    for pi, poly in enumerate(polys):
        if not poly or len(poly) < 3:
            continue
        surface = tags[ptag.get(pi, 0)] if tags else "default"
        by_surface.setdefault(surface, []).append((pi, poly))
    bin_parts, views, accessors, meshes_prims, materials, images, textures, samplers = [], [], [], [], [], [], [], []
    weight_names = sorted(weights)

    def add_view(blob, target):
        while len(b"".join(bin_parts)) % 4:
            bin_parts.append(b"\0")
        offset = sum(len(p) for p in bin_parts)
        bin_parts.append(blob)
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(blob), "target": target})
        return len(views) - 1

    def add_accessor(view, ctype, count, atype, mins=None, maxs=None):
        acc = {"bufferView": view, "componentType": ctype, "count": count, "type": atype}
        if mins is not None:
            acc["min"], acc["max"] = mins, maxs
        accessors.append(acc)
        return len(accessors) - 1

    for surface, faces in sorted(by_surface.items()):
        surf = surfaces.get(surface, {"color": (0.78, 0.78, 0.78), "uv": None, "clip": None})
        uv_name = surf["uv"]
        vmap = uv_maps.get(uv_name, {}) if uv_name else {}
        vmad = uv_overrides.get(uv_name, {}) if uv_name else {}
        diffuse_layer = next((x for x in surf.get('layers', []) if x['channel'] == 'DIFF' and x['uv']), None)
        diffuse_uv = diffuse_layer['uv'] if diffuse_layer else None
        verts, index_of, indices = [], {}, []
        for pi, poly in faces:
            corner_ids = []
            for v in poly:
                uv = vmad.get((pi, v), vmap.get(v, (0.0, 0.0)))
                uv2 = uv_overrides.get(diffuse_uv, {}).get((pi, v), uv_maps.get(diffuse_uv, {}).get(v, (0., 0.)))
                key = (v, uv, uv2)
                if key not in index_of:
                    index_of[key] = len(verts)
                    verts.append(key)
                corner_ids.append(index_of[key])
            for k in range(1, len(corner_ids) - 1):  # a fan; LightWave winds clockwise seen from the front
                indices += [corner_ids[0], corner_ids[k + 1], corner_ids[k]]
        positions = [tuple(c * scale for c in points[v]) for v, _, _ in verts]
        uvs = [(u, 1.0 - w) for _, (u, w), _ in verts]
        pos_blob = b"".join(struct.pack("<3f", *p) for p in positions)
        uv_blob = b"".join(struct.pack("<2f", *t) for t in uvs)
        idx_blob = b"".join(struct.pack("<I", i) for i in indices)
        pos_acc = add_accessor(add_view(pos_blob, 34962), 5126, len(positions), "VEC3",
                               [min(p[i] for p in positions) for i in range(3)],
                               [max(p[i] for p in positions) for i in range(3)])
        uv_acc = add_accessor(add_view(uv_blob, 34962), 5126, len(uvs), "VEC2")
        idx_acc = add_accessor(add_view(idx_blob, 34963), 5125, len(indices), "SCALAR")
        attributes = {"POSITION": pos_acc, "TEXCOORD_0": uv_acc}
        # The weight maps, as extras: a rigger's starting point.
        extras = {}
        source_points = add_accessor(add_view(b''.join(struct.pack('<I', v) for v, _, _ in verts), 34962), 5125, len(verts), 'SCALAR')
        # Preserve the second source UV channel for a layered material bake.
        if diffuse_layer:
            uv2 = [uv for _, _, uv in verts]
            attributes['TEXCOORD_1'] = add_accessor(add_view(b''.join(struct.pack('<2f', u, 1-v) for u,v in uv2), 34962), 5126, len(verts), 'VEC2')
        for name in weight_names:
            table = weights[name]
            values = [table.get(v, 0.0) for v, _, _ in verts]
            if any(values):
                extras[name] = add_accessor(add_view(b"".join(struct.pack("<f", x) for x in values), 34962), 5126,
                                            len(values), "SCALAR")
        material = {"name": surface, "pbrMetallicRoughness": {"baseColorFactor": list(surf["color"]) + [1.0],
                                                                "metallicFactor": 0.0, "roughnessFactor": 0.8}}
        clip_name = clips.get(surf["clip"]) if surf["clip"] is not None else None
        if clip_name and textures_dir:
            stem = os.path.splitext(os.path.basename(clip_name))[0]
            for ext in (".png", ".jpg", ".jpeg"):
                candidate = os.path.join(textures_dir, stem + ext)
                if os.path.exists(candidate):
                    images.append({"uri": os.path.relpath(candidate, os.path.dirname(args.out) or ".")})
                    textures.append({"source": len(images) - 1, "sampler": 0})
                    material["pbrMetallicRoughness"]["baseColorTexture"] = {"index": len(textures) - 1}
                    break
        material['extras'] = {'lightwave_surface': surf}
        if clip_name:
            material["extras"].update({"lightwave_clip": clip_name, "lightwave_uv": uv_name})
        materials.append(material)
        prim = {"attributes": attributes, "indices": idx_acc, "material": len(materials) - 1}
        prim["extras"] = {"weight_maps": extras, "source_points": source_points}
        meshes_prims.append(prim)
    gltf = {
        "asset": {"version": "2.0", "generator": "tools/3dmark/lwo2gltf.py"},
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "lwo"}],
        "meshes": [{"primitives": meshes_prims}],
        "materials": materials,
        "accessors": accessors, "bufferViews": views,
        "buffers": [{"byteLength": 0}],
    }
    if textures:
        gltf["images"], gltf["textures"] = images, textures
        gltf["samplers"] = [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}]
    binary = b"".join(bin_parts)
    while len(binary) % 4:
        binary += b"\0"
    gltf["buffers"][0]["byteLength"] = len(binary)
    return gltf, binary


def write_glb(path, gltf, binary):
    js = json.dumps(gltf, separators=(",", ":")).encode()
    while len(js) % 4:
        js += b" "
    total = 12 + 8 + len(js) + 8 + len(binary)
    with open(path, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(binary), 0x004E4942) + binary)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("src")
    parser.add_argument("out")
    parser.add_argument("--scale", type=float, default=1.0)
    parser.add_argument("--textures")
    args = parser.parse_args()
    parsed = parse(open(args.src, "rb").read())
    points, polys, tags, ptag, uv_maps, uv_overrides, weights, clips, surfaces = parsed
    gltf, binary = build_gltf(*parsed, scale=args.scale, textures_dir=args.textures)
    write_glb(args.out, gltf, binary)
    tris = sum(len(p["indices"] and [1]) for p in gltf["meshes"][0]["primitives"])
    print(f"{args.out}: {len(points)} points, {len(polys)} polygons, {len(gltf['meshes'][0]['primitives'])} surfaces, "
          f"weight maps {sorted(weights)}, clips {sorted(clips.values())}")
