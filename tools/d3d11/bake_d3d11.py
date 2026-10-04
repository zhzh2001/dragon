#!/usr/bin/env python3
"""Bake the HLSL shaders to Direct3D 11 bytecode (docs/PORTING.md, D3D11).

    tools/d3d11/bake_d3d11.py OUT_DIR [--tier modern|sm3|sm2|sm2-noshadows|ff]
                                      [--profiles sm50,sm40] [--only stem,...]

The same source as every other backend, through the same first step as the
D3D9 bake: shadercross compiles the HLSL to SPIR-V with the tier's defines,
SPIRV-Cross writes it back as shader model 5.0 HLSL -- cbuffers at
register(bN), textures at tN and samplers at sN, each N our slot number, the
vertex inputs TEXCOORDn, SV_VertexID where a pass asks for it -- and
d3dcompiler_47 (tools/d3d9/hlslc.cpp, under Wine on the Mac) compiles that
to DXBC: vs_5_0/ps_5_0 for feature level 11, vs_4_0/ps_4_0 for 10. The
backend (src/rhi/d3d11) picks the profile its device reached and loads
<stem><defines>.<stage>.<profile>.dxbc. Feature level 9's level_9_x profiles
are not baked here yet: that is the D3D9 tiers' job, and a second step.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHADERS = os.path.join(ROOT, "shaders")
sys.path.insert(0, os.path.join(ROOT, "tools", "d3d9"))
import bake_d3d9  # noqa: E402  the define sets, the compiler wrapper, variant_suffix

TIERS = {name: [d for d in defines if d != "D3D9"] for name, defines in bake_d3d9.TIERS.items()}
TIERS["modern"] = []
PROFILES = {"sm50": ("vs_5_0", "ps_5_0"), "sm40": ("vs_4_0", "ps_4_0")}


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
    defines = TIERS[args.tier]
    suffix = bake_d3d9.variant_suffix(defines)
    profiles = args.profiles.split(",")

    def is_pipeline(f):
        return f.endswith(".hlsl") and "#pragma once" not in open(os.path.join(SHADERS, f)).read()
    stems = sorted(f[:-5] for f in os.listdir(SHADERS) if is_pipeline(f))
    if args.only:
        stems = [s for s in stems if s in args.only.split(",")]
    if "LDR_OUTPUT" in defines:
        stems = [s for s in stems if not s.startswith("post_")]

    work = tempfile.mkdtemp(prefix="bake_d3d11_")
    jobs = []
    for stem in stems:
        for stage, entry, define in (("vertex", "vs_main", "VERTEX_STAGE"), ("fragment", "fs_main", "FRAGMENT_STAGE")):
            spv = os.path.join(work, f"{stem}.{stage}.spv")
            flags = [f"-D{d}" for d in defines + [define]]
            run([bake_d3d9.SHADERCROSS, os.path.join(SHADERS, stem + ".hlsl"), "-s", "HLSL", "-d", "SPIRV", "-t", stage,
                 "-e", entry, "-I", SHADERS, "-o", spv] + flags)
            hlsl = run(["spirv-cross", spv, "--hlsl", "--shader-model", "50"])
            src = os.path.join(work, f"{stem}.{stage}.hlsl")
            open(src, "w").write(hlsl)
            # The backend's own stretch (rhi::Device::blit) is the same in
            # every tier, and loaded without a variant suffix.
            stem_suffix = "" if stem == "blit" else suffix
            for profile in profiles:
                vs, ps = PROFILES[profile]
                jobs.append((vs if stage == "vertex" else ps, src,
                             os.path.join(args.out, f"{stem}{stem_suffix}.{stage}.{profile}.dxbc")))

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
