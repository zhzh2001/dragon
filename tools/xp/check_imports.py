#!/usr/bin/env python3
"""Check that an executable imports nothing Windows XP SP3 lacks (docs/PORTING.md, R6).

    tools/xp/check_imports.py build-xp/dragon.exe                    # against the recorded exports
    tools/xp/check_imports.py build-xp/dragon.exe --dlls DIR         # against real XP DLLs
    tools/xp/check_imports.py --record DIR                           # record DIR's exports

A Windows program that imports one function the system does not export does
not start at all: the loader refuses it before main runs ("The procedure
entry point ... could not be located"). So the XP build's imports are read
from its import table (llvm-objdump) and every one is looked up in what XP
SP3's own DLLs export. Those lists come from the real files -- kernel32.dll,
user32.dll, gdi32.dll, msvcrt.dll, shell32.dll from an XP SP3 install, and
d3d9.dll from DirectX 9.0c -- read with --dlls, or once with --record into
tools/xp/xp-sp3-exports.txt, which this script reads by default. A DLL the
list does not know is an error too: XP may not have it at all.

An import that fails gets a replacement in src/platform/xp/xp_thunks.cpp,
which is what makes the build start on XP.
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RECORDED = os.path.join(ROOT, "tools", "xp", "xp-sp3-exports.txt")
OBJDUMP = os.environ.get("LLVM_OBJDUMP") or "/opt/homebrew/opt/llvm@22/bin/llvm-objdump"


def imports(exe):
    """{dll (lower case): [names]} from the executable's import table."""
    text = subprocess.run([OBJDUMP, "-p", exe], capture_output=True, text=True, check=True).stdout
    table = {}
    dll = None
    in_imports = False
    for line in text.splitlines():
        if line.startswith("The Import Tables:"):
            in_imports = True
            continue
        if in_imports and line.startswith("Export Table:"):
            break
        m = re.match(r"\s+DLL Name: (\S+)", line)
        if m:
            dll = m.group(1).lower()
            table.setdefault(dll, [])
            continue
        m = re.match(r"\s+\d+\s+(\S+)$", line)
        if dll and m and in_imports:
            table[dll].append(m.group(1))
    return table


def exports(path):
    """The names a DLL exports."""
    text = subprocess.run([OBJDUMP, "-p", path], capture_output=True, text=True, check=True).stdout
    names = set()
    in_exports = False
    for line in text.splitlines():
        if line.startswith("Export Table:"):
            in_exports = True
            continue
        if in_exports:
            # "ordinal  RVA  Name", or "ordinal  Name (forwarded to ...)".
            m = re.match(r"\s+\d+\s+(?:0x[0-9a-fA-F]+\s+)?([A-Za-z_?@$][^\s]*)", line)
            if m:
                names.add(m.group(1))
    return names


def dll_exports(directory):
    table = {}
    for f in sorted(os.listdir(directory)):
        if f.lower().endswith(".dll"):
            table[f.lower()] = exports(os.path.join(directory, f))
    return table


def read_recorded():
    table = {}
    if not os.path.exists(RECORDED):
        return None
    dll = None
    for line in open(RECORDED):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        if line.startswith("[") and line.endswith("]"):
            dll = line[1:-1].lower()
            table[dll] = set()
        elif dll:
            table[dll].add(line.strip())
    return table


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("exe", nargs="?")
    parser.add_argument("--dlls", help="a directory of XP SP3's own DLLs")
    parser.add_argument("--record", metavar="DIR", help="write DIR's DLL exports to tools/xp/xp-sp3-exports.txt")
    args = parser.parse_args()

    if args.record:
        table = dll_exports(args.record)
        with open(RECORDED, "w") as f:
            f.write("# What Windows XP SP3's DLLs export, read from the real files by\n")
            f.write("# tools/xp/check_imports.py --record (the names only).\n")
            for dll, names in sorted(table.items()):
                f.write(f"\n[{dll}]\n")
                for n in sorted(names):
                    f.write(n + "\n")
        print(f"recorded {sum(len(v) for v in table.values())} exports of {len(table)} DLLs in {RECORDED}")
        return 0

    if not args.exe:
        parser.error("an executable to check, or --record DIR")
    known = dll_exports(args.dlls) if args.dlls else read_recorded()
    if known is None:
        sys.exit(f"no XP export list: run with --dlls DIR (XP SP3's DLLs), or --record DIR once ({RECORDED})")
    problems = 0
    # The header first: XP's loader refuses an image stamped for a later
    # Windows before it looks at a single import.
    header = subprocess.run([OBJDUMP, "-p", args.exe], capture_output=True, text=True, check=True).stdout
    for field in ("MajorOSystemVersion", "MajorSubsystemVersion"):
        m = re.search(rf"{field}\s+(\d+)", header)
        minor = re.search(rf"{field.replace('Major', 'Minor')}\s+(\d+)", header)
        if m and minor and (int(m.group(1)), int(minor.group(1))) > (5, 1):
            print(f"HEADER {field} {m.group(1)}.{minor.group(1)}: XP (5.1) will not start it")
            problems += 1
    found = imports(args.exe)
    for dll, names in sorted(found.items()):
        if dll not in known:
            print(f"MISSING DLL {dll}: XP has no such DLL in the list ({len(names)} imports)")
            problems += 1
            continue
        for n in names:
            if n not in known[dll]:
                print(f"MISSING {dll} {n}")
                problems += 1
    total = sum(len(v) for v in found.values())
    if problems:
        print(f"{problems} import(s) XP SP3 does not have, of {total}: thunk them in src/platform/xp/xp_thunks.cpp")
        return 1
    print(f"ok: all {total} imports from {len(found)} DLLs are in XP SP3")
    return 0


if __name__ == "__main__":
    sys.exit(main())
