#!/usr/bin/env python3
"""Bake the HLSL shaders to Direct3D 11 bytecode (docs/PORTING.md, D3D11).

    tools/d3d11/bake_d3d11.py OUT_DIR [--tier modern|sm3|sm2|sm2-noshadows|ff]
                                      [--profiles sm50,sm40] [--only stem,...]
    tools/d3d11/bake_d3d11.py OUT_DIR --tier sm2 --profiles fl91    # feature level 9

The same source as every other backend, through the same first step as the
D3D9 bake: shadercross compiles the HLSL to SPIR-V with the tier's defines,
SPIRV-Cross writes it back as shader model 5.0 HLSL -- cbuffers at
register(bN), textures at tN and samplers at sN, each N our slot number, the
vertex inputs TEXCOORDn, SV_VertexID where a pass asks for it -- and
d3dcompiler_47 (tools/d3d9/hlslc.cpp, under Wine on the Mac) compiles that
to DXBC: vs_5_0/ps_5_0 for feature level 11, vs_4_0/ps_4_0 for 10. The
backend (src/rhi/d3d11) picks the profile its device reached and loads
<stem><defines>.<stage>.<profile>.dxbc. Feature level 9 (fl91:
vs/ps_4_0_level_9_1, run at 9_1, 9_2 and 9_3) bakes a retro tier's variant
with LEVEL9 defined and its cbuffers compacted to the members each stage
uses (compact_cbuffers, the .l9copies sidecar), since level 9 loads a
cbuffer whole into D3D9's register file (docs/PORTING.md, "Feature level 9
on D3D11, as built").
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHADERS = os.path.join(ROOT, "shaders")
sys.path.insert(0, os.path.join(ROOT, "tools", "d3d9"))
import bake_d3d9  # noqa: E402  the define sets, the compiler wrapper, variant_suffix

TIERS = {name: [d for d in defines if d != "D3D9"] for name, defines in bake_d3d9.TIERS.items()}
# The modern tier compiles the physically based sky (gfx/atmosphere.h).
TIERS["modern"] = ["ATMOSPHERE"]
PROFILES = {"sm50": ("vs_5_0", "ps_5_0"), "sm40": ("vs_4_0", "ps_4_0"),
            # Feature level 9 (docs/PORTING.md, "Feature level 9 on D3D11"):
            # fxc's level_9_1 profiles take what ps_2_0 takes, so they compile
            # the sm2 tier's variant with LEVEL9 defined -- D3D9's two
            # workarounds (no SV_VertexID, the R32F shadow stand-in) and joint
            # indices as UNORM bytes -- and the bytecode runs at 9_1, 9_2 and
            # 9_3 alike. There is no level_9_3 set: ps_4_0_level_9_3 still has
            # ps_2_x's 32 constant registers and no texlod, so the sm3 tier's
            # pixel shaders do not fit it (terrain: "Invalid const register
            # num: 32", "texlod not supported"); 9_3 runs the sm2 tier too.
            "fl91": ("vs_4_0_level_9_1", "ps_4_0_level_9_1")}
LEVEL9 = {"fl91"}


def compact_cbuffers(hlsl):
    """Keep only the members a level-9 shader uses in each cbuffer.

    Feature level 9 runs the shader's constants through D3D9's register
    file -- 256 float4 for a vertex shader, 32 for a pixel shader -- and the
    runtime loads every cbuffer at its DECLARED size: a pixel shader that
    declares the 39-register scene block and reads three of its members
    still has 39 registers set, and both the X550's driver and the 5060 Ti's
    answered that with DXGI_ERROR_DRIVER_INTERNAL_ERROR. So each cbuffer is
    rewritten with its used members alone, at consecutive registers, and
    the backend assembles that layout from the pushed block through the
    copies returned here: (slot, source register, count, destination
    register), what the sidecar .l9copies file carries.
    """
    lines = hlsl.split("\n")
    blocks = []  # (slot, first line, last line, [(line index, name, reg, count)])
    i = 0
    while i < len(lines):
        m = re.match(r"cbuffer \w+ : register\(b(\d+)\)", lines[i])
        if not m:
            i += 1
            continue
        slot = int(m.group(1))
        first = i
        members = []
        i += 1
        while not lines[i].startswith("};"):
            pm = re.match(r"\s*(.+?)\s*:\s*packoffset\(c(\d+)\);", lines[i])
            if pm:
                decl = pm.group(1)
                name = decl.split()[-1].split("[")[0]
                count = 4 if "float4x4" in decl else 1
                am = re.search(r"\[(\d+)\]", decl)
                if am:
                    count *= int(am.group(1))
                if "." in pm.group(0).split("packoffset")[1]:
                    raise ValueError(f"sub-register packoffset: {lines[i]}")
                members.append((i, name, int(pm.group(2)), count))
            elif lines[i].strip() != "{":
                raise ValueError(f"unexpected cbuffer line: {lines[i]}")
            i += 1
        blocks.append((slot, first, i, members))
        i += 1
    inside = set()
    for _, first, last, _ in blocks:
        inside.update(range(first, last + 1))
    body = "\n".join(l for k, l in enumerate(lines) if k not in inside)
    copies = []
    drop = set()
    for slot, first, last, members in blocks:
        dst = 0
        kept = 0
        for index, name, reg, count in members:
            if re.search(rf"\b{re.escape(name)}\b", body):
                lines[index] = re.sub(r"packoffset\(c\d+\)", f"packoffset(c{dst})", lines[index])
                copies.append((slot, reg, count, dst))
                dst += count
                kept += 1
            else:
                drop.add(index)
        if kept == 0:
            drop.update(range(first, last + 1))
    return "\n".join(l for k, l in enumerate(lines) if k not in drop), copies


def run(cmd, **kw):
    result = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if result.returncode != 0:
        sys.exit(f"{' '.join(cmd)}\n{result.stdout}{result.stderr}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("out")
    parser.add_argument("--tier", default="modern", choices=sorted(TIERS))
    parser.add_argument("--profiles", default="sm50,sm40")
    parser.add_argument("--only")
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    profiles = args.profiles.split(",")
    level9 = [p in LEVEL9 for p in profiles]
    if any(level9) and not all(level9):
        sys.exit("feature level 9 profiles bake from the D3D9 define sets: run them separately")
    if all(level9) and args.tier not in ("sm2", "sm2-noshadows", "ff"):
        sys.exit("feature level 9 runs the sm2 tier (--tier sm2, sm2-noshadows or ff)")
    defines = sorted(TIERS[args.tier] + ["LEVEL9"]) if all(level9) else TIERS[args.tier]
    suffix = bake_d3d9.variant_suffix(defines)
    model = "40" if all(level9) else "50"

    def is_pipeline(f):
        return f.endswith(".hlsl") and "#pragma once" not in open(os.path.join(SHADERS, f)).read()
    stems = sorted(f[:-5] for f in os.listdir(SHADERS) if is_pipeline(f))
    if args.only:
        stems = [s for s in stems if s in args.only.split(",")]
    if "LDR_OUTPUT" in defines:
        stems = [s for s in stems if not s.startswith("post_")]
    if "ATMOSPHERE" not in defines:
        # The atmosphere's tables: the modern tier's alone.
        stems = [s for s in stems if not s.startswith(("atmosphere_", "clouds_")) and s not in ("terrain_sun", "post_taa", "post_ssao", "post_ssao_blur", "velocity_skinned")]

    units = [(stem, defines, suffix) for stem in stems]
    # The modern tier's terrain also comes in its light shading, the Terrain
    # setting at Low and below (gfx/pipeline.cpp, TERRAIN_LITE).
    if "ATMOSPHERE" in defines and "terrain" in stems:
        lite = sorted(defines + ["TERRAIN_LITE"])
        units.append(("terrain", lite, bake_d3d9.variant_suffix(lite)))

    work = tempfile.mkdtemp(prefix="bake_d3d11_")
    jobs = []
    for stem, unit_defines, unit_suffix in units:
        for stage, entry, define in (("vertex", "vs_main", "VERTEX_STAGE"), ("fragment", "fs_main", "FRAGMENT_STAGE")):
            spv = os.path.join(work, f"{stem}{unit_suffix}.{stage}.spv")
            flags = [f"-D{d}" for d in unit_defines + [define]]
            run([bake_d3d9.SHADERCROSS, os.path.join(SHADERS, stem + ".hlsl"), "-s", "HLSL", "-d", "SPIRV", "-t", stage,
                 "-e", entry, "-I", SHADERS, "-o", spv] + flags)
            hlsl = run(["spirv-cross", spv, "--hlsl", "--shader-model", model])
            copies = None
            if all(level9):
                hlsl, copies = compact_cbuffers(hlsl)
            src = os.path.join(work, f"{stem}{unit_suffix}.{stage}.hlsl")
            open(src, "w").write(hlsl)
            # The backend's own stretch (rhi::Device::blit) is the same in
            # every tier, and loaded without a variant suffix.
            stem_suffix = "" if stem == "blit" else unit_suffix
            for profile in profiles:
                vs, ps = PROFILES[profile]
                base = os.path.join(args.out, f"{stem}{stem_suffix}.{stage}.{profile}")
                jobs.append((vs if stage == "vertex" else ps, src, base + ".dxbc"))
                if copies is not None:
                    with open(base + ".l9copies", "w") as f:
                        f.write("".join(f"{slot} {reg} {count} {dst}\n" for slot, reg, count, dst in copies))

    hlslc = os.path.join(work, "hlslc.exe")
    run(["x86_64-w64-mingw32-g++", "-O2", "-static", "-s", os.path.join(ROOT, "tools", "d3d9", "hlslc.cpp"), "-o", hlslc])
    shutil.copy(bake_d3d9.D3DCOMPILER, os.path.join(work, "d3dcompiler_47.dll"))
    argv = [hlslc]
    for profile, src, out in jobs:
        argv += [profile, os.path.basename(src), os.path.abspath(out)]
    if sys.platform == "win32":
        run(argv, cwd=work)
    else:
        wine = os.environ.get("WINE") or shutil.which("wine")
        if not wine:
            sys.exit("needs Wine (set WINE) to run d3dcompiler_47 on this host")
        env = dict(os.environ, WINEDEBUG="-all", WINEDLLOVERRIDES="d3dcompiler_47=n")
        argv = [wine] + [a if not a.startswith("/") else "Z:" + a.replace("/", "\\") for a in argv]
        run(argv, cwd=work, env=env)
    missing = [out for _, _, out in jobs if not os.path.exists(out)]
    if missing:
        sys.exit(f"{len(missing)} shader(s) did not compile, e.g. {missing[0]}")
    print(f"baked {len(jobs)} shader stages ({args.tier}{suffix}, {', '.join(profiles)}) into {args.out}")
    shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
