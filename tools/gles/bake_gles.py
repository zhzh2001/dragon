#!/usr/bin/env python3
"""Bake the HLSL shaders into GLSL ES 1.00 for the GLES 2.0 backend.

    tools/gles/bake_gles.py OUT_DIR [--tier sm2-noshadows] [--only terrain,sky]

The GLES backend (src/rhi/gles, docs/porting/MOBILE.md M4) runs the retro
tiers' content on GLES 2.0 -- the Raspberry Pi 1B's VideoCore IV through
Mesa's vc4, developed against ANGLE on the Mac. Each shader takes:

  1. shadercross: HLSL -> SPIR-V, with the tier's D3D9 define set
     (tools/d3d9/bake_d3d9.py): GLES 2.0 shares D3D9's gaps -- no
     SV_VertexID, no integer vertex inputs -- and the shaders already
     answer them under D3D9;
  2. SPIRV-Cross: SPIR-V -> GLSL ES 1.00 (brew install spirv-cross), the
     uniform blocks flattened into vec4 arrays (the blocks are float4 and
     float4x4 only, so a block's bytes are its array's), the vertex stage's
     clip space moved to GL's [-w, w] and its y flipped. Every pass renders
     upside down in GL's terms, which is the D3D layout in memory: textures
     sample, viewports place and readback rows come out the way the rest of
     the engine expects, and only the present flips;
  3. this script: names the interface by location (attributes a<n>,
     varyings v<n>) so the stages link, prefixes each stage's uniforms so a
     block both stages read cannot clash on precision or size, and records
     which uniform takes which pushed slot and which sampler which slot:

  <stem><variant>.vertex.glsl, .fragment.glsl   the sources, compiled by the driver
  <stem><variant>.gles.json   {"vertex": {"uniforms": [[slot, name, vec4s], ...],
                                          "samplers": [[slot, name], ...],
                                          "attributes": [[location, name], ...]},
                               "fragment": {...}}

<variant> is the engine's define set's suffix, without D3D9 (gfx/pipeline.cpp
builds it; rhi/gles reads it).
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHADERS = os.path.join(ROOT, "shaders")
sys.path.insert(0, os.path.join(ROOT, "tools", "d3d9"))
import bake_d3d9  # noqa: E402  the tiers' define sets and the shadercross path

TIERS = bake_d3d9.TIERS


def run(cmd):
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f"{' '.join(cmd)}\n{result.stdout}{result.stderr}")
    return result.stdout


def bake_stage(src, stage, defines, work):
    entry, define = ("vs_main", "VERTEX_STAGE") if stage == "vertex" else ("fs_main", "FRAGMENT_STAGE")
    spv = os.path.join(work, f"{stage}.spv")
    run([bake_d3d9.SHADERCROSS, src, "-s", "HLSL", "-d", "SPIRV", "-t", stage, "-e", entry, "-I", SHADERS,
         "-o", spv, f"-D{define}"] + [f"-D{d}" for d in defines])
    reflect = json.loads(run(["spirv-cross", spv, "--reflect"]))
    prefix = "vs_" if stage == "vertex" else "fs_"

    cmd = ["spirv-cross", spv, "--es", "--version", "100", "--flatten-ubo"]
    if stage == "vertex":
        cmd += ["--fixup-clipspace", "--flip-vert-y"]
        for i in reflect.get("inputs", []):
            cmd += ["--rename-interface-variable", "in", str(i["location"]), f"a{i['location']}"]
        for o in reflect.get("outputs", []):
            cmd += ["--rename-interface-variable", "out", str(o["location"]), f"v{o['location']}"]
    else:
        for i in reflect.get("inputs", []):
            cmd += ["--rename-interface-variable", "in", str(i["location"]), f"v{i['location']}"]
    glsl = run(cmd)
    # Full precision where the GPU has it (VC4 is fp32 throughout; the
    # default mediump would quantise world positions on a GPU that is not).
    glsl = glsl.replace("precision mediump float;",
                        "#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif")

    # The uniform blocks, flattened: "uniform vec4 type_X[N];". Which slot
    # each is comes from its binding (common.hlsl, UNIFORM_SLOT); SPIRV-Cross
    # names the array after the block's type.
    uniforms = []
    for ubo in reflect.get("ubos", []):
        # "type.ConstantBuffer.SceneUniforms" in the reflection; GLSL
        # identifiers take underscores.
        type_name = re.sub(r"[^A-Za-z0-9_]", "_", reflect["types"][ubo["type"]]["name"])
        m = re.search(r"uniform (?:highp |mediump )?vec4 (" + re.escape(type_name) + r")\[(\d+)\];", glsl)
        if not m:
            sys.exit(f"{src} {stage}: block {type_name} not found flattened")
        name = prefix + m.group(1)
        glsl = re.sub(r"\b" + re.escape(m.group(1)) + r"\b", name, glsl)
        uniforms.append([ubo["binding"], name, int(m.group(2))])

    # The samplers: SPIRV-Cross combines each image with the sampler it is
    # sampled through, named SPIRV_Cross_Combined<image><sampler>; the image's
    # binding is the slot the engine binds (TEXTURE_SLOT).
    images = {img["name"]: img["binding"] for img in reflect.get("separate_images", [])}
    samplers = []
    for m in re.finditer(r"uniform (?:highp |mediump |lowp )?sampler2D (\w+);", glsl):
        combined = m.group(1)
        slot = None
        for image, binding in images.items():
            if combined.startswith("SPIRV_Cross_Combined" + image):
                if slot is None or len(image) > len(slot[0]):
                    slot = (image, binding)
        if slot is None:
            sys.exit(f"{src} {stage}: no image for sampler {combined}")
        samplers.append([slot[1], combined])
    attributes = [[i["location"], f"a{i['location']}"] for i in reflect.get("inputs", [])] if stage == "vertex" else []
    return glsl, {"uniforms": uniforms, "samplers": samplers, "attributes": attributes}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("out")
    parser.add_argument("--tier", default="sm2-noshadows", choices=sorted(TIERS))
    parser.add_argument("--only", help="comma-separated shader stems")
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)

    # GLES: what GLSL can do that D3D9's fixed-function stages cannot (the
    # rivals' recolour). Like D3D9, not part of the variant's name.
    defines = TIERS[args.tier] + ["GLES"]
    suffix = bake_d3d9.variant_suffix([d for d in defines if d not in ("D3D9", "GLES")])

    def is_pipeline(f):
        return f.endswith(".hlsl") and "#pragma once" not in open(os.path.join(SHADERS, f)).read()
    stems = sorted(f[:-5] for f in os.listdir(SHADERS) if is_pipeline(f))
    # The modern tier's alone: the atmosphere, the clouds, the temporal and
    # occlusion passes (tools/release/bake_shaders.sh).
    stems = [s for s in stems if not s.startswith(("atmosphere_", "clouds_")) and
             s not in ("terrain_sun", "post_taa", "post_ssao", "post_ssao_blur", "velocity_skinned")]
    if "LDR_OUTPUT" in defines:
        stems = [s for s in stems if not s.startswith("post_")]
    if args.only:
        stems = [s for s in stems if s in args.only.split(",")]

    work = tempfile.mkdtemp(prefix="bake_gles_")
    for stem in stems:
        src = os.path.join(SHADERS, stem + ".hlsl")
        meta = {}
        for stage in ("vertex", "fragment"):
            glsl, meta[stage] = bake_stage(src, stage, defines, work)
            with open(os.path.join(args.out, f"{stem}{suffix}.{stage}.glsl"), "w") as f:
                f.write(glsl)
        with open(os.path.join(args.out, f"{stem}{suffix}.gles.json"), "w") as f:
            json.dump(meta, f)
    print(f"baked {len(stems)} shaders ({args.tier}{suffix}) into {args.out}")


if __name__ == "__main__":
    main()
