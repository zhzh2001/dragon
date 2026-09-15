"""Measure an unrigged Frostvein candidate without guessing skeleton landmarks.

Run from this worktree after the one-shot GLB is ready::

    blender --background --factory-startup --python tools/frostvein_measure.py -- \
        --input assets/frostvein-cand-oneshot.glb \
        --output artifacts/frostvein/measurement

The import, transform application, and 180 degree Z turn match
``rig_embercrest_candidate.py``.  The report is deliberately a measurement
surface rather than an automatic skeleton author: it records the complete
post-transform mesh bounds, longitudinal slices, component/material bounds,
and several conservative wing-plane subsets.  A measured skeleton can then be
written from those values and inspected against the rendered views.

This tool never writes the input GLB or any shared engine file.  It is specific
to Frostvein so a future source with a different body plan cannot silently
inherit an Embercrest probe's thresholds.
"""

import argparse
import csv
import hashlib
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, help="unrigged candidate GLB")
    parser.add_argument(
        "--output", default="artifacts/frostvein/measurement", help="report directory"
    )
    parser.add_argument(
        "--views",
        action="store_true",
        help="also write full-size orthographic side/front/rear/top PNG renders",
    )
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1 :])


def vec_list(v):
    return [round(float(v[i]), 9) for i in range(3)]


def bounds(points):
    if not points:
        return None
    lo = [min(p[i] for p in points) for i in range(3)]
    hi = [max(p[i] for p in points) for i in range(3)]
    return {"lo": [round(float(x), 9) for x in lo], "hi": [round(float(x), 9) for x in hi]}


def quantile(sorted_values, q):
    if not sorted_values:
        return None
    if len(sorted_values) == 1:
        return float(sorted_values[0])
    at = max(0.0, min(1.0, q)) * (len(sorted_values) - 1)
    lo = int(math.floor(at))
    hi = min(len(sorted_values) - 1, lo + 1)
    t = at - lo
    return float(sorted_values[lo] * (1.0 - t) + sorted_values[hi] * t)


def fit_plane(points):
    """Fit a plane with a tiny Jacobi eigensolver (no NumPy in Blender)."""
    if len(points) < 3:
        return None
    # Keep the Blender run bounded on a million-triangle candidate while
    # preserving a deterministic, evenly spaced sample from each subset.
    step = max(1, len(points) // 60000)
    sample = points[::step]
    centroid = [sum(p[i] for p in sample) / len(sample) for i in range(3)]
    cov = [[0.0] * 3 for _ in range(3)]
    for p in sample:
        d = [p[i] - centroid[i] for i in range(3)]
        for i in range(3):
            for j in range(3):
                cov[i][j] += d[i] * d[j]
    inv_n = 1.0 / len(sample)
    for i in range(3):
        for j in range(3):
            cov[i][j] *= inv_n
    vecs = [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]
    for _ in range(48):
        p, q = 0, 1
        largest = abs(cov[0][1])
        for i, j in ((0, 2), (1, 2)):
            if abs(cov[i][j]) > largest:
                p, q, largest = i, j, abs(cov[i][j])
        if largest < 1e-14:
            break
        theta = 0.5 * math.atan2(2.0 * cov[p][q], cov[q][q] - cov[p][p])
        c, s = math.cos(theta), math.sin(theta)
        for k in range(3):
            cp, cq = cov[p][k], cov[q][k]
            cov[p][k] = c * cp - s * cq
            cov[q][k] = s * cp + c * cq
        for k in range(3):
            cpk, cqk = cov[k][p], cov[k][q]
            cov[k][p] = c * cpk - s * cqk
            cov[k][q] = s * cpk + c * cqk
        for k in range(3):
            vp, vq = vecs[k][p], vecs[k][q]
            vecs[k][p] = c * vp - s * vq
            vecs[k][q] = s * vp + c * vq
    eig = [cov[i][i] for i in range(3)]
    axis = min(range(3), key=lambda i: eig[i])
    normal = [vecs[i][axis] for i in range(3)]
    norm = math.sqrt(sum(v * v for v in normal)) or 1.0
    normal = [v / norm for v in normal]
    # Use a consistent sign for comparing both wings to body up.
    if normal[2] < 0.0:
        normal = [-v for v in normal]
    rms = math.sqrt(
        sum(
            (sum((p[i] - centroid[i]) * normal[i] for i in range(3))) ** 2
            for p in sample
        )
        / len(sample)
    )
    up_angle = math.degrees(math.acos(max(-1.0, min(1.0, normal[2]))))
    return {
        "sample_count": len(sample),
        "centroid": [round(float(v), 9) for v in centroid],
        "normal": [round(float(v), 9) for v in normal],
        "rms": round(float(rms), 9),
        "angle_to_body_up_deg": round(float(up_angle), 4),
    }


def object_points(obj):
    return [obj.matrix_world @ v.co for v in obj.data.vertices]


def ray_crossings(tree, x, y, z_top, z_bottom):
    """Return ordered Z crossings of one vertical ray through the mesh."""
    origin = Vector((x, y, z_top))
    direction = Vector((0.0, 0.0, -1.0))
    crossings = []
    # A mouth gap has several crossings, while a ray through solid anatomy has
    # two.  The cap prevents a pathological self-intersection from hanging the
    # diagnostic on a generated mesh with duplicated faces.
    for _ in range(32):
        location, _normal, _index, distance = tree.ray_cast(origin, direction, z_top - z_bottom)
        if location is None or distance is None:
            break
        z = float(location.z)
        if z < z_bottom - 1e-6:
            break
        crossings.append(round(z, 9))
        origin = location + direction * 1e-5
    return crossings


def measure_jaw_crossings(obj, lo, hi):
    """Record raw vertical crossings from the head region for a jaw-plane fit.

    The ranges are broad mesh-relative screening ranges and are written into
    the report.  They do not decide which crossing is the lip: that choice is
    made by comparing the full-size side render with the crossing windows.
    """
    # Blender 5.x exposes object/depsgraph construction as FromObject; this
    # also avoids duplicating the million-triangle index buffers in Python.
    tree = BVHTree.FromObject(obj, bpy.context.evaluated_depsgraph_get())
    width = hi[0] - lo[0]
    length = hi[1] - lo[1]
    height = hi[2] - lo[2]
    x_span = min(width * 0.09, 0.09)
    y0 = lo[1] + length * 0.74
    y1 = hi[1] - length * 0.005
    z_top = hi[2] + height * 0.08
    z_bottom = lo[2] - height * 0.02
    rows = []
    for yi in range(25):
        y = y0 + (y1 - y0) * yi / 24.0
        for xi in range(9):
            x = -x_span + 2.0 * x_span * xi / 8.0
            rows.append(
                {
                    "x": round(x, 9),
                    "y": round(y, 9),
                    "z_crossings": ray_crossings(tree, x, y, z_top, z_bottom),
                }
            )
    return {
        "screening_rule": {
            "y_fraction_of_mesh": [0.74, 0.995],
            "x_half_width": x_span,
            "note": "raw crossings only; select the mandible-top/palate window from side render",
        },
        "rows": rows,
    }


def measure_wing_stations(all_points, lo, hi):
    """Summarise each wing's outboard stations for measured finger placement."""
    width = hi[0] - lo[0]
    height = hi[2] - lo[2]
    z_gate = lo[2] + height * 0.34
    stations = []
    for side in (-1, 1):
        for fraction in (0.12, 0.20, 0.30, 0.40, 0.50):
            x_abs = width * fraction
            half = max(width * 0.012, 0.004)
            pts = [
                p
                for p in all_points
                if side * p[0] > 0
                and abs(abs(p[0]) - x_abs) <= half
                and p[2] >= z_gate
            ]
            ys = sorted(p[1] for p in pts)
            zs = sorted(p[2] for p in pts)
            stations.append(
                {
                    "side": "left" if side < 0 else "right",
                    "x_fraction": fraction,
                    "x_abs": round(x_abs, 9),
                    "count": len(pts),
                    "y_lo": round(ys[0], 9) if ys else None,
                    "y_q50": round(quantile(ys, 0.50), 9) if ys else None,
                    "y_hi": round(ys[-1], 9) if ys else None,
                    "z_lo": round(zs[0], 9) if zs else None,
                    "z_q50": round(quantile(zs, 0.50), 9) if zs else None,
                    "z_hi": round(zs[-1], 9) if zs else None,
                }
            )
    return {
        "z_gate": round(z_gate, 9),
        "stations": stations,
        "note": "stations are broad outboard screening bands; membrane/finger joints still require side/top inspection",
    }


def measure_wing_planes(all_points, lo, hi):
    """Fit both broad membrane subsets and report their measured tilt."""
    width = hi[0] - lo[0]
    length = hi[1] - lo[1]
    height = hi[2] - lo[2]
    rule = {
        "abs_x_fraction_min": 0.16,
        "y_fraction": [0.30, 0.82],
        "z_fraction_min": 0.28,
        "note": "broad membrane screening subset; ridges and joints are inspected separately",
    }
    out = {"rule": rule}
    for side in (-1, 1):
        pts = [
            p
            for p in all_points
            if side * p[0] > width * rule["abs_x_fraction_min"]
            and lo[1] + length * rule["y_fraction"][0] <= p[1] <= lo[1] + length * rule["y_fraction"][1]
            and p[2] > lo[2] + height * rule["z_fraction_min"]
        ]
        out["left" if side < 0 else "right"] = {
            "count": len(pts),
            "bounds": bounds(pts),
            "fit": fit_plane(pts),
        }
    return out


def make_render(output, target, all_points):
    """Write diagnostic views from the transformed mesh; no skeleton is made."""
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x = 1600
    scene.render.resolution_y = 1200
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.studio_light = "paint.sl"
    scene.display.shading.color_type = "MATERIAL"
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.display.shading.cavity_type = "BOTH"
    scene.world.color = (0.12, 0.12, 0.12)

    center = Vector(
        (
            (target["lo"][0] + target["hi"][0]) * 0.5,
            (target["lo"][1] + target["hi"][1]) * 0.5,
            (target["lo"][2] + target["hi"][2]) * 0.5,
        )
    )
    extent = max(target["hi"][i] - target["lo"][i] for i in range(3))
    camera_data = bpy.data.cameras.new("Frostvein measurement camera")
    camera = bpy.data.objects.new("Frostvein measurement camera", camera_data)
    scene.collection.objects.link(camera)
    scene.camera = camera
    camera_data.type = "ORTHO"
    camera_data.ortho_scale = extent * 1.34

    # Canonical post-turn mesh is +Y forward and +Z up.  These cameras are
    # explicit so the report's images can be compared with the four source
    # plates without relying on a viewport's current orientation.
    views = {
        "side": ((extent * 0.95, center.y + extent * 0.03, center.z + extent * 0.1), (0, 0, 1)),
        "front": ((0, center.y + extent * 1.1, center.z + extent * 0.1), (0, 0, 1)),
        "rear": ((0, center.y - extent * 1.1, center.z + extent * 0.1), (0, 0, 1)),
        "top": ((0, center.y, center.z + extent * 1.1), (0, 1, 0)),
    }
    # Track -Z with Y as the camera up axis.  The top view needs a stable
    # forward direction so the tail/head axis remains readable.
    targets = {
        "side": (Vector((0, 0, 1)), Vector((0, 0, 1))),
        "front": (Vector((0, 0, 1)), Vector((0, 0, 1))),
        "rear": (Vector((0, 0, 1)), Vector((0, 0, 1))),
        "top": (Vector((0, 1, 0)), Vector((0, 0, 1))),
    }
    for name, (location, _up) in views.items():
        location = Vector(location)
        camera.location = location
        camera.rotation_euler = (center - location).to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = str(output / f"{name}.png")
        bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(camera, do_unlink=True)


def main():
    args = parse_args()
    source = Path(args.input).resolve()
    output = Path(args.output).resolve()
    if not source.exists():
        raise SystemExit(f"source mesh not found: {source}")
    output.mkdir(parents=True, exist_ok=True)

    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.gltf(filepath=str(source), merge_vertices=True)

    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        raise SystemExit("candidate contains no mesh objects")

    # Match the rigger exactly: apply imported object transforms, then turn a
    # source that faces -Y into the +Y authoring frame.  Keep every mesh object;
    # auxiliary eyes/horns are useful evidence when checking head bounds.
    for obj in meshes:
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        obj.rotation_mode = "XYZ"
        obj.rotation_euler.z = math.pi
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
        obj.select_set(False)

    point_sets = {obj.name: [tuple(p) for p in object_points(obj)] for obj in meshes}
    all_points = [p for values in point_sets.values() for p in values]
    mesh_bounds = bounds(all_points)
    lo = mesh_bounds["lo"]
    hi = mesh_bounds["hi"]
    width = hi[0] - lo[0]
    length = hi[1] - lo[1]
    height = hi[2] - lo[2]

    # A long-axis profile gives a reviewer the evidence behind root/chest,
    # neck/head, tail, and leg-region placement.  The bins use all geometry,
    # while the percentiles make spikes and wing tips visible without allowing
    # one isolated outlier to choose a joint.
    slices = []
    bins = 64
    for i in range(bins):
        a = lo[1] + length * i / bins
        b = lo[1] + length * (i + 1) / bins
        pts = [p for p in all_points if a <= p[1] < b or (i == bins - 1 and a <= p[1] <= b)]
        if not pts:
            slices.append({"index": i, "y_lo": a, "y_hi": b, "count": 0})
            continue
        xs = sorted(p[0] for p in pts)
        zs = sorted(p[2] for p in pts)
        slices.append(
            {
                "index": i,
                "y_lo": round(a, 9),
                "y_hi": round(b, 9),
                "count": len(pts),
                "x_lo": round(xs[0], 9),
                "x_hi": round(xs[-1], 9),
                "x_q05": round(quantile(xs, 0.05), 9),
                "x_q95": round(quantile(xs, 0.95), 9),
                "z_lo": round(zs[0], 9),
                "z_hi": round(zs[-1], 9),
                "z_q05": round(quantile(zs, 0.05), 9),
                "z_q50": round(quantile(zs, 0.50), 9),
                "z_q95": round(quantile(zs, 0.95), 9),
            }
        )

    # Conservative wing candidate subsets.  A spread wing must be both
    # laterally outboard and above the central body in this reference.  The
    # thresholds are reported explicitly; they are a diagnostic partition,
    # never silently promoted into a skeleton or a weight field.
    mid_x = max(0.10 * width, 1e-6)
    z_gate = lo[2] + 0.34 * height
    wing_points = {
        "left": [p for p in all_points if p[0] < -mid_x and p[2] > z_gate],
        "right": [p for p in all_points if p[0] > mid_x and p[2] > z_gate],
    }
    wing_summary = {
        "candidate_rule": {
            "abs_x_min": mid_x,
            "z_min": z_gate,
            "note": "screening subset only; fit membrane and joints from renders/mesh after inspection",
        },
        "left": {"count": len(wing_points["left"]), "bounds": bounds(wing_points["left"])},
        "right": {"count": len(wing_points["right"]), "bounds": bounds(wing_points["right"])},
    }

    components = []
    for obj in meshes:
        entry = {
            "object": obj.name,
            "vertices": len(obj.data.vertices),
            "triangles": len(obj.data.polygons),
            "bounds": bounds(point_sets[obj.name]),
        }
        materials = []
        for slot_index, slot in enumerate(obj.material_slots):
            name = slot.material.name if slot.material else "<none>"
            indices = []
            for poly in obj.data.polygons:
                if poly.material_index != slot_index:
                    continue
                indices.extend(poly.vertices)
            mats = [point_sets[obj.name][i] for i in sorted(set(indices))]
            materials.append({"slot": slot_index, "material": name, "vertices": len(mats), "bounds": bounds(mats)})
        entry["materials"] = materials
        components.append(entry)

    report = {
        "tool": "tools/frostvein_measure.py",
        "source": str(source),
        "source_size_bytes": source.stat().st_size,
        "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "source_triangles": sum(len(obj.data.polygons) for obj in meshes),
        "source_vertices": sum(len(obj.data.vertices) for obj in meshes),
        "authoring_frame": {"forward": "+Y", "up": "+Z", "turn_z_deg": 180},
        "bounds": mesh_bounds,
        "extents": {"x": width, "y": length, "z": height},
        "components": components,
        "wing_candidates": wing_summary,
        "wing_stations": measure_wing_stations(all_points, lo, hi),
        "wing_planes": measure_wing_planes(all_points, lo, hi),
        "jaw_vertical_crossings": measure_jaw_crossings(meshes[0], lo, hi),
        "longitudinal_slices": slices,
        "measurement_notes": [
            "Use these bounds as the skeleton reference_bounds only after checking whether non-body accessories are included.",
            "Measure jaw plane from vertical crossings in a full-resolution side crop; do not inherit Rimefang coordinates.",
            "Measure wing_field gates from the actual flank-to-membrane transition and wrist hub; do not inherit Embercrest values.",
            "Measure leg heads, knees, ankles, feet, and toe groups from front and side views; bind joints must sit in the corresponding mesh anatomy.",
        ],
    }
    (output / "mesh_measurements.json").write_text(json.dumps(report, indent=2) + "\n")
    with (output / "longitudinal_slices.csv").open("w", newline="") as stream:
        if slices:
            writer = csv.DictWriter(stream, fieldnames=list(slices[0].keys()))
            writer.writeheader()
            writer.writerows(slices)

    if args.views:
        make_render(output, mesh_bounds, all_points)
    print(json.dumps({"output": str(output), "bounds": mesh_bounds, "components": len(meshes)}), flush=True)


if __name__ == "__main__":
    main()
