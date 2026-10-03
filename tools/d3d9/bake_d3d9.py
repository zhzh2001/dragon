#!/usr/bin/env python3
"""Bake the HLSL shaders into Direct3D 9 bytecode for the D3D9 backend.

    tools/d3d9/bake_d3d9.py OUT_DIR [--tier sm3]

The shaders are written once, for the modern APIs (shaders/*.hlsl, SM 6
syntax: ConstantBuffer<T>, register spaces). D3D9 needs vs_3_0/ps_3_0, so each
shader takes the route the R3 study found (docs/PORTING.md, R3):

  1. shadercross: HLSL -> SPIR-V, with the tier's defines and D3D9;
  2. SPIRV-Cross: SPIR-V -> HLSL for shader model 3.0 (brew install spirv-cross);
  3. this script: unwrap the uniform blocks into plain globals, which fxc
     packs (only what a shader uses, in consecutive registers -- ps_2_0 has
     32), and pin each sampler to its slot;
  4. d3dcompiler_47 (tools/d3d9/hlslc.cpp, under Wine on the Mac): -> bytecode.

Then it reads back the constant table (CTAB) the compiler embedded and writes
where each block member landed, which the backend copies pushed blocks by:

  <stem><variant>.vs30, .ps30   the bytecode
  <stem><variant>.d3d9.json     {"vertex": {"copies": [[slot, offset, register, count], ...],
                                            "half_pixel": register},
                                 "fragment": {"copies": ..., "samplers": [slots]}}

<variant> is the define set's file suffix, the same rule as
tools/release/bake_shaders.sh with D3D9 added: ".baked_noise.d3d9.packed_joints.swizzled_normals".
"""
import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHADERS = os.path.join(ROOT, "shaders")
SHADERCROSS = os.environ.get("SHADERCROSS") or shutil.which("shadercross") or \
    os.path.expanduser("~/.local/opt/shadercross/bin/shadercross")
D3DCOMPILER = os.environ.get("D3DCOMPILER_DLL") or os.path.expanduser("~/.local/opt/d3dcompiler/d3dcompiler_47.dll")

# The define sets a tier compiles with (gfx/pipeline.cpp), plus D3D9.
TIERS = {
    "sm3": ["BAKED_NOISE", "PACKED_JOINTS", "SWIZZLED_NORMALS", "D3D9"],
    "sm2": ["BAKED_NOISE", "LDR_OUTPUT", "PACKED_JOINTS", "SM2", "SWIZZLED_NORMALS", "D3D9"],
}
PROFILES = {"sm3": ("vs_3_0", "ps_3_0"), "sm2": ("vs_2_0", "ps_2_0")}
# Float constant registers each stage has (vs_3_0 256, ps_3_0 224; vs_2_0
# 256 on every card of the class, ps_2_0 32).
REGISTER_LIMIT = {"sm3": (256, 224), "sm2": (256, 32)}


SURVEY = False  # --survey: report every problem instead of stopping at the first


def run(cmd, **kw):
    result = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if result.returncode != 0:
        if SURVEY:
            print(f"{result.stdout}{result.stderr}")
            return result.stdout
        sys.exit(f"{' '.join(cmd)}\n{result.stdout}{result.stderr}")
    return result.stdout


def variant_suffix(defines):
    return "".join("." + d.lower() for d in sorted(defines))


# ---- step 3: SPIRV-Cross's SM 3.0 HLSL -> uniforms unwrapped, samplers pinned

def unwrap_uniforms(hlsl, sampler_slots, stage):
    """Unwrap each cbuffer into plain globals and pin the samplers.

    fxc ignores packoffset for SM 3.0 and below and packs the constants a
    shader actually uses into consecutive registers -- which ps_2_0 needs, with
    32 registers against the scene block's 39. So the members are left for
    fxc to place, and where it put each is read back from the constant table
    after compiling (read_ctab). Returns the HLSL and {member: (slot, offset
    in its block, in registers)}.
    """
    members = {}
    out = []
    current = None
    for line in hlsl.split("\n"):
        m = re.match(r"cbuffer \w+ : register\(b(\d+)\)", line)
        if m:
            current = int(m.group(1))
            continue
        if current is not None and line.strip() == "{":
            continue
        if current is not None and line.startswith("};"):
            current = None
            continue
        pm = re.match(r"\s*(.+?)\s*:\s*packoffset\(c(\d+)\);", line)
        if current is not None and pm:
            name = pm.group(1).split()[-1].split("[")[0]
            members[name] = (current, int(pm.group(2)))
            line = f"uniform {pm.group(1)};"
        elif current is not None:
            raise ValueError(f"no packoffset: {line}")
        sm = re.match(r"uniform sampler2D (SPIRV_Cross_Combined(\w+));", line)
        if sm:
            slot = sampler_slots.get(sm.group(1))
            if slot is None:
                raise ValueError(f"no slot for {sm.group(1)}")
            line = f"uniform sampler2D {sm.group(1)} : register(s{slot});"
        out.append(line)
    return "\n".join(out), members


def sampler_slots_from_reflection(spv):
    """SPIRV-Cross's combined sampler name -> the texture's binding (our slot)."""
    reflect = json.loads(run(["spirv-cross", spv, "--reflect"]))
    images = {img["name"]: img["binding"] for img in reflect.get("separate_images", [])}
    samplers = [s["name"] for s in reflect.get("separate_samplers", [])]
    slots = {}
    for image, binding in images.items():
        for sampler in samplers:
            slots["SPIRV_Cross_Combined" + image + sampler] = binding
    return slots


# ---- step 4 check: the constant table the compiler wrote

def read_ctab(bytecode):
    """{name: (register_set, index, count)} from a D3D9 shader's CTAB comment."""
    words = struct.unpack(f"<{len(bytecode) // 4}I", bytecode)
    i = 1
    while i < len(words):
        token = words[i]
        if token & 0xFFFF == 0xFFFE:
            length = token >> 16
            if words[i + 1] == 0x42415443:  # 'CTAB'
                table = bytecode[(i + 2) * 4:(i + 1 + length) * 4]
                _, _, _, count, info, _, _ = struct.unpack_from("<7I", table, 0)
                out = {}
                for c in range(count):
                    name_off, reg_set, reg_index, reg_count, _, _, _ = struct.unpack_from("<IHHHHII", table, info + c * 20)
                    name = table[name_off:table.index(b"\0", name_off)].decode()
                    out[name] = (reg_set, reg_index, reg_count)
                return out
            i += 1 + length
        else:
            break
    return {}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("out")
    parser.add_argument("--tier", default="sm3", choices=sorted(TIERS))
    parser.add_argument("--only", help="comma-separated shader stems")
    parser.add_argument("--counts", action="store_true", help="print each stage's instruction slot counts")
    parser.add_argument("--survey", action="store_true",
                        help="list every limit overrun and compile error, write what compiles, and fail at the end")
    args = parser.parse_args()
    global SURVEY
    SURVEY = args.survey
    os.makedirs(args.out, exist_ok=True)
    defines = TIERS[args.tier]
    suffix = variant_suffix(defines)
    vs_profile, ps_profile = PROFILES[args.tier]
    limits = dict(zip(("vertex", "fragment"), REGISTER_LIMIT[args.tier]))

    work = tempfile.mkdtemp(prefix="bake_d3d9_")
    # Every shader file is a pipeline except the headers, which say
    # #pragma once. (Looking for "fs_main(" missed the depth-only passes,
    # whose entry point comes from a macro, DEPTH_ONLY_FRAGMENT.)
    def is_pipeline(f):
        return f.endswith(".hlsl") and "#pragma once" not in open(os.path.join(SHADERS, f)).read()
    stems = sorted(f[:-5] for f in os.listdir(SHADERS) if is_pipeline(f))
    if args.only:
        stems = [s for s in stems if s in args.only.split(",")]
    if "LDR_OUTPUT" in defines:
        # No HDR, no post stack: the app never creates those pipelines.
        stems = [s for s in stems if not s.startswith("post_")]
    jobs = []      # (profile, hlsl path, out path)
    layouts = {}   # stem -> {stage: layout}
    expected = {}  # (stem, stage) -> members
    for stem in stems:
        layouts[stem] = {}
        for stage, entry, define, profile, ext in (("vertex", "vs_main", "VERTEX_STAGE", vs_profile, "vs30"),
                                                   ("fragment", "fs_main", "FRAGMENT_STAGE", ps_profile, "ps30")):
            spv = os.path.join(work, f"{stem}.{stage}.spv")
            flags = [f"-D{d}" for d in defines + [define]]
            run([SHADERCROSS, os.path.join(SHADERS, stem + ".hlsl"), "-s", "HLSL", "-d", "SPIRV", "-t", stage,
                 "-e", entry, "-I", SHADERS, "-o", spv] + flags)
            sm30 = run(["spirv-cross", spv, "--hlsl", "--shader-model", "30"])
            unwrapped, members = unwrap_uniforms(sm30, sampler_slots_from_reflection(spv), stage)
            src = os.path.join(work, f"{stem}.{stage}.hlsl")
            open(src, "w").write(unwrapped)
            layouts[stem][stage] = {"samplers": sorted(set(sampler_slots_from_reflection(spv).values()))} \
                if stage == "fragment" else {}
            expected[(stem, stage)] = members
            jobs.append((profile, src, os.path.join(args.out, f"{stem}{suffix}.{ext}")))

    # Step 4, one compiler start for everything (Wine takes seconds to start).
    hlslc = os.path.join(work, "hlslc.exe")
    run(["x86_64-w64-mingw32-g++", "-O2", "-static", "-s", os.path.join(ROOT, "tools", "d3d9", "hlslc.cpp"),
         "-o", hlslc])
    shutil.copy(D3DCOMPILER, os.path.join(work, "d3dcompiler_47.dll"))
    argv = [hlslc]
    for profile, src, out in jobs:
        argv += [profile, os.path.basename(src), os.path.abspath(out)]
    if sys.platform == "win32":
        listing = run(argv, cwd=work)
    else:
        wine = os.environ.get("WINE") or shutil.which("wine")
        if not wine:
            sys.exit("needs Wine (set WINE to its bin/wine) to run d3dcompiler_47 on this host")
        env = dict(os.environ, WINEDEBUG="-all", WINEDLLOVERRIDES="d3dcompiler_47=n")
        # Wine wants Windows paths for the outputs.
        argv = [wine] + [a if not a.startswith("/") else "Z:" + a.replace("/", "\\") for a in argv]
        listing = run(argv, cwd=work, env=env)
    if args.counts:
        for line in listing.splitlines():
            if "instruction slots" in line:
                print(re.sub(r"^.*[\\/]", "", line))

    # Where the compiler put each constant: the copies the backend makes from
    # a pushed block into the registers, [slot, offset in the block, register,
    # count], and the half-pixel uniform's register.
    problems = 0
    for (stem, stage), members in expected.items():
        ext = "vs30" if stage == "vertex" else "ps30"
        path = os.path.join(args.out, f"{stem}{suffix}.{ext}")
        if not os.path.exists(path):
            problems += 1
            continue
        ctab = read_ctab(open(path, "rb").read())
        copies = []
        for name, (reg_set, index, count) in sorted(ctab.items(), key=lambda kv: kv[1][1]):
            if reg_set != 2:
                continue
            if name == "gl_HalfPixel":
                layouts[stem][stage]["half_pixel"] = index
            elif name in members:
                slot, offset = members[name]
                copies.append([slot, offset, index, count])
            else:
                print(f"UNKNOWN constant {stem} {stage} {name}")
                problems += 1
            if index + count > limits[stage]:
                print(f"OVER {stem} {stage} {name}: c{index + count}, past {limits[stage]}")
                problems += 1
        layouts[stem][stage]["copies"] = copies
    for stem, layout in layouts.items():
        with open(os.path.join(args.out, f"{stem}{suffix}.d3d9.json"), "w") as f:
            json.dump(layout, f, indent=1, sort_keys=True)
    shutil.rmtree(work)
    if problems:
        sys.exit(f"{problems} problems")
    print(f"baked {len(jobs)} stages ({args.tier}{suffix}) into {args.out}")


if __name__ == "__main__":
    main()
