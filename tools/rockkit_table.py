#!/usr/bin/env python3
"""Print gfx::RockPieceInfo entries for src/gfx/rock_piece.h from
assets/props/rockkit/pieces.json (tools/rockkit.md, "Placement data").

    tools/rockkit_table.py crag_a crag_b ...
    tools/rockkit_table.py --landmarks tower_ruin arch_ruin ...

`--landmarks` reads assets/props/landmarks/pieces.json (tools/landmarks.md)
and marks each entry as a landmark: the kit draws it from that folder in its
own colours.

The header's table is the placement's copy of each piece's size and check
points, so the tests and the placement need no JSON reader. Paste the output
after the last entry and raise ROCK_PIECES. A boulder, crag or mound stands
upright: its mounting plane is unused and written as the ground's.
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def vec(p):
    return "{%.2ff, %.2ff, %.2ff}" % (p[0], p[1], p[2])


def main():
    names = sys.argv[1:]
    landmarks = bool(names) and names[0] == "--landmarks"
    if landmarks:
        names = names[1:]
    kit = "landmarks" if landmarks else "rockkit"
    with open(os.path.join(ROOT, "assets/props", kit, "pieces.json")) as f:
        pieces = {p["name"]: p for p in json.load(f)["pieces"]}
    for name in names:
        p = pieces[name]
        points = p["check_points"][:26]
        rows = []
        for i in range(0, len(points), 4):
            rows.append(", ".join(vec(q) for q in points[i:i + 4]))
        body = ",\n          ".join(rows)
        print('        {"%s", false, %s, %s, {}, {0.0f, 1.0f, 0.0f},' % (
            name, "true" if p["kind"] == "slab" else "false", vec(p["size"])))
        print("         {" + body + "},")
        print("         %d%s}," % (len(points), ", true" if landmarks else ""))


if __name__ == "__main__":
    main()
