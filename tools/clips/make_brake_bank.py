"""Build one additive brake loop and its mirror in Blender 5.2.

Run: blender -b -P tools/clips/make_brake_bank.py
Use -- --preview to also render the mesh and build contact sheets.
Use -- --probe-only to measure bone axes without exporting animation.

All curves use degrees in the source model frame: X right, Y up, -Z forward.
Each layer has a mean and (amplitude, cycles per loop, phase) sine terms.
Only 1, 2 and 3 cycles occur. Their common period is exactly 2.4 seconds.
Phases describe delayed load responses, not independent wingbeats.

The GLB writer copies source joint TRS values without Blender decomposition.
Blender supplies quaternion math, the measured rig, and preview deformation.
The output contains joint nodes, not a Blender armature object or skin.

Axis measurements use actual Blender pose changes of positive five degrees.
The report records each child displacement and both parent and bone axes.
Leaf probes use the imported bone tip because leaves have no child.
See axis-probes.json for all 62 joints and three axes per joint.

Measured child displacements in source units, from positive five-degree probes:
- Root +X moved the chest +0.009758 up: positive X pitches the nose up.
- Neck_01 +X moved neck_02 +0.003590 up and +0.006051 aft.
- Left wing_root +Z moved wing_arm -0.010468 down: negative Z raises it.
- Left wing_arm +Z moved wing_wrist -0.020434 down.
- Left wing_finger_3a +X moved its child -0.017319 down.
- Tail_01 +Y moved tail_02 +0.006412 right: positive Y is the outside rudder.
- Thigh_l +X moved shin_l -0.006858 forward and +0.004925 up.
- Shin_l +X moved ankle_l -0.002788 forward and -0.007930 down.
- Jaw +X raised jaw_tip +0.008659: use negative X to breathe open.
- Toe_hind_2_l +X raised its tip +0.002293: negative X curls downward.
These are model-frame directions. Per-joint parent axes are measured and saved.
The script converts them through each parent's rest rotation before baking.
"""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_clip import accessor, check, read_glb

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'assets/embercrest.glb'
OUTPUT = ROOT / 'assets/clips/embercrest.brake_bank.glb'
ART = ROOT / 'artifacts/clip-trial'
DURATION, FPS, COUNT = 2.4, 30, 73
# glTF Y-up to Blender Z-up: (x, y, z) -> (x, -z, y).
C = Matrix(((1, 0, 0), (0, 0, -1), (0, 1, 0)))
CQ = C.to_quaternion()
AXES = {'pitch': Vector((1, 0, 0)), 'yaw': Vector((0, 1, 0)), 'roll': Vector((0, 0, 1))}


def quat(values):
    x, y, z, w = values
    return Quaternion((w, x, y, z)).normalized()


def xyzw(q):
    return [q.x, q.y, q.z, q.w]


def source_rig(doc):
    indices = doc['skins'][0]['joints']
    remap = {old: new for new, old in enumerate(indices)}
    nodes = []
    for old in indices:
        n = doc['nodes'][old]
        node = {k: copy.deepcopy(n[k]) for k in ('name', 'translation', 'rotation', 'scale') if k in n}
        if 'children' in n:
            node['children'] = [remap[c] for c in n['children'] if c in remap]
        nodes.append(node)
    parents = {c: i for i, n in enumerate(nodes) for c in n.get('children', [])}
    worlds, rotations = {}, {}

    def visit(i):
        if i in worlds:
            return
        n = nodes[i]
        t = Vector(n.get('translation', (0, 0, 0)))
        q = quat(n.get('rotation', (0, 0, 0, 1)))
        local = Matrix.LocRotScale(t, q, Vector(n.get('scale', (1, 1, 1))))
        if i in parents:
            visit(parents[i])
            worlds[i] = worlds[parents[i]] @ local
            rotations[i] = rotations[parents[i]] @ q
        else:
            worlds[i], rotations[i] = local, q
    for i in range(len(nodes)):
        visit(i)
    return nodes, parents, worlds, rotations


def import_mesh():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.import_scene.gltf(filepath=str(SOURCE), bone_heuristic='BLENDER')
    rig = next(o for o in bpy.context.scene.objects if o.type == 'ARMATURE')
    meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH'
              and any(m.type == 'ARMATURE' and m.object == rig for m in o.modifiers)]
    for o in list(bpy.context.scene.objects):
        if o != rig and o not in meshes:
            bpy.data.objects.remove(o, do_unlink=True)
    return rig, meshes


def blender_basis(rig, name, parent_rotation, delta):
    # Convert a parent-frame delta to the imported bone's own pose basis.
    body_delta = parent_rotation @ delta @ parent_rotation.inverted()
    blender_delta = CQ @ body_delta @ CQ.inverted()
    bone_rotation = rig.data.bones[name].matrix_local.to_quaternion()
    return (bone_rotation.inverted() @ blender_delta @ bone_rotation).normalized()


def reset_pose(rig):
    for pb in rig.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
        pb.rotation_mode = 'QUATERNION'
    bpy.context.view_layer.update()


def measure_axes(rig, nodes, parents, rotations):
    reset_pose(rig)
    report = {'probe_deg': 5, 'coordinate_frame': 'source glTF: X right, Y up, -Z forward', 'joints': {}}
    for i, n in enumerate(nodes):
        name = n['name']
        pb = rig.pose.bones[name]
        child = n.get('children', [None])[0]
        child_name = nodes[child]['name'] if child is not None else None
        get_point = (lambda: rig.pose.bones[child_name].head.copy()) if child_name else (lambda: pb.tail.copy())
        before = get_point()
        parent_rotation = rotations[parents[i]] if i in parents else Quaternion()
        br = rig.data.bones[name].matrix_local.to_quaternion()
        measures = {}
        for semantic, axis in AXES.items():
            parent_axis = parent_rotation.inverted() @ axis
            delta = Quaternion(parent_axis, math.radians(5))
            pb.rotation_quaternion = blender_basis(rig, name, parent_rotation, delta)
            bpy.context.view_layer.update()
            displacement = C.inverted() @ (get_point() - before)
            measures[semantic] = {'parent_axis': list(parent_axis),
                                  'bone_axis': list(br.inverted() @ (C @ axis)),
                                  'child_displacement_gltf': list(displacement)}
            pb.rotation_quaternion = Quaternion()
            bpy.context.view_layer.update()
        report['joints'][name] = {'probe_point': child_name or 'imported bone tip', 'axes': measures}
    (ART / 'axis-probes.json').write_text(json.dumps(report, indent=2) + '\n')
    for name in ('root', 'neck_01', 'jaw', 'wing_root_l', 'wing_arm_l', 'wing_wrist_l',
                 'wing_finger_3a_l', 'tail_01', 'thigh_l', 'shin_l', 'toe_hind_2_l'):
        print('AXIS', name, {a: [round(x, 6) for x in d['child_displacement_gltf']]
                             for a, d in report['joints'][name]['axes'].items()}, flush=True)


def author_curves(names):
    curves = {n: [] for n in names}

    def layer(name, axis, mean=0, terms=()):
        assert all(cycles in (1, 2, 3) for _, cycles, _ in terms)
        curves[name].append({'axis': axis, 'mean_deg': mean, 'terms': list(terms)})

    # The chest takes load first. The pelvis follows without repeating its path.
    body = {
        'root': {'pitch': [(0.95, 2, -0.38), (0.38, 1, 0.9)],
                 'roll': [(1.35, 1, -0.35), (0.22, 3, 0.5)],
                 'yaw': [(0.25, 2, 1.2)]},
        'chest': {'pitch': [(1.25, 2, 0.12), (0.35, 3, -0.5)],
                  'roll': [(1.7, 1, 0.05), (0.32, 2, 1.6)],
                  'yaw': [(0.32, 1, -0.7), (0.15, 3, 0.4)]}}
    for name, axes in body.items():
        for axis, terms in axes.items():
            layer(name, axis, terms=terms)

    # The neck absorbs the body's rotation in unequal shares.
    # Additional bends travel through the neck instead of nodding the skull.
    for j, share in enumerate((0.44, 0.34, 0.22), 1):
        name = f'neck_{j:02}'
        for axis in AXES:
            terms = [(-share * a, c, p) for v in body.values() for a, c, p in v[axis]]
            layer(name, axis, terms=terms)
        layer(name, 'pitch', terms=[((1.6, -1.35, -0.25)[j-1], 1, (0.5, 0.15, -0.9)[j-1]),
                                    (0.28, 3, 0.4 - j * 0.62)])
        layer(name, 'yaw', mean=(0.8, 0.65, 0.45)[j-1],
              terms=[((1.7, -1.3, -0.4)[j-1], 1, 0.8 - j * 0.25), (0.22, 2, -j * 0.5)])
    # Head aim stays smaller than the neck corrections. Positive Y looks left.
    layer('head', 'yaw', 0.65, [(0.28, 1, -1.4), (0.12, 3, 0.2)])
    layer('head', 'pitch', 0, [(-0.35, 1, 0.8), (0.15, 2, -0.8)])
    layer('head', 'roll', -0.35, [(0.18, 2, -1.1)])
    # Negative X lowers the jaw tip. Breathing never closes beyond bind.
    layer('jaw', 'pitch', -2.1, [(0.8, 1, -0.7), (0.2, 2, 0.4)])
    # jaw_tip is a marker. It follows the jaw with an identity delta.

    # Each wing corrects load once per loop, with a distinct two-cycle accent.
    # The inside wing cups more. Shoulder, elbow and wrist respond in order.
    for side, sign, phase in [('l', -1, 0.25), ('r', 1, 2.7)]:
        inside = side == 'l'
        layer('wing_root_' + side, 'roll', sign * (0.8 if inside else 0.2),
              [(sign * 2.7, 1, phase), (sign * 0.8, 2, phase + 0.6)])
        layer('wing_root_' + side, 'yaw', -sign * (1.0 if inside else 0.4),
              [(-sign * 1.4, 1, phase - 0.4), (-sign * 0.45, 3, phase + 1.2)])
        layer('wing_arm_' + side, 'roll', sign * 0.35,
              [(sign * 2.0, 1, phase - 0.42), (sign * 0.65, 2, phase - 0.3)])
        layer('wing_arm_' + side, 'pitch', 0, [(0.7, 2, phase - 0.5), (0.4, 1, phase + 0.2)])
        layer('wing_wrist_' + side, 'pitch', 1.5 if inside else 0.4,
              [(2.5, 1, phase - 0.9), (0.7, 2, phase - 1.0)])
        layer('wing_wrist_' + side, 'roll', -sign * (1.3 if inside else 0.4),
              [(-sign * 1.5, 1, phase - 0.75), (sign * 0.45, 3, phase - 0.6)])
        # Ray delays differ. The rear two distal segments carry small flutter.
        # A positive X bend lowers aft-pointing rays to deepen the membrane cup.
        for ray in range(1, 4):
            for segment, extra in [('a', 0.0), ('b', 0.38)]:
                name = f'wing_finger_{ray}{segment}_{side}'
                lag = 1.03 + ray * 0.22 + extra
                amp = (1.8 + ray * 0.38) * (1.0 if segment == 'a' else 0.76)
                terms = [(amp, 1, phase - lag), (0.65, 2, phase - lag * 1.6)]
                if ray >= 2 and segment == 'b':
                    terms.append((0.55 + 0.1 * ray, 3, phase - lag * 2.4 + ray * 0.5))
                layer(name, 'pitch', (1.8 if inside else 0.45) * (1 if segment == 'a' else 0.6), terms)
                layer(name, 'yaw', sign * 0.25, [(sign * 0.55, 1, phase - lag - 0.6)])

    # A broad S wave carries the rudder adjustment to the tip.
    # Positive Y moves the aft tail toward +X, outside a left turn.
    # The long phase span prevents eight rotations from becoming one hinge.
    for j in range(1, 9):
        delay = (j - 1) * 0.56
        layer(f'tail_{j:02}', 'yaw', 0.75 - j * 0.035,
              [(1.25 + j * 0.29, 1, -0.65 - delay), (0.32 + j * 0.035, 2, 0.3 - delay * 1.3)])
        layer(f'tail_{j:02}', 'pitch', 0, [(0.45 + j * 0.07, 1, 1.4 - delay), (0.2, 3, -delay)])

    # Legs paddle independently. The knee and ankle lag the thigh.
    # The inside leg stays slightly gathered; toes release after the ankle.
    for side, sign, phase in [('l', -1, -0.45), ('r', 1, 2.35)]:
        inside = side == 'l'
        for joint, amp, lag, mean in [('thigh', 4.1, 0, -1.0 if inside else 0.3),
                                      ('shin', -3.3, 0.5, 1.8 if inside else 0.4),
                                      ('ankle', 3.2, 0.9, -1.0 if inside else 0),
                                      ('foot', -2.7, 1.2, 0.5)]:
            layer(joint + '_' + side, 'pitch', mean,
                  [(amp, 1, phase - lag), (0.65, 2, phase - lag * 1.5 + 0.3)])
        layer('thigh_' + side, 'roll', sign * 0.65,
              [(sign * 0.9, 2, phase - 0.3), (sign * 0.6, 1, phase + 0.7)])
        for joint, amp, lag in [('upper_arm', 2.4, 0), ('forearm', -1.9, 0.4),
                                ('wrist', 1.7, 0.8), ('hand', -1.5, 1.05)]:
            layer(joint + '_' + side, 'pitch', 0,
                  [(amp, 1, phase + 1.15 - lag), (0.4, 3, phase - lag)])
        for limb, amp, shift in [('hind', 4.3, 0), ('fore', 2.7, 1.1)]:
            for toe in range(1, 4):
                layer(f'toe_{limb}_{toe}_{side}', 'pitch', -1.2,
                      [(amp, 1, phase + shift - 1.4 - toe * 0.16), (0.6, 2, phase - toe * 0.31)])
                layer(f'toe_{limb}_{toe}_{side}', 'yaw', sign * (toe - 2) * 0.3,
                      [(sign * (toe - 2) * 0.65, 2, phase - toe * 0.2)])
    return curves


def evaluate(curves, t):
    result = {}
    for name, layers in curves.items():
        vector = Vector((0, 0, 0))
        for layer in layers:
            degrees = layer['mean_deg'] + sum(a * math.sin(2 * math.pi * c * t / DURATION + p)
                                               for a, c, p in layer['terms'])
            vector += AXES[layer['axis']] * degrees
        result[name] = Quaternion(vector.normalized(), math.radians(vector.length)) if vector.length else Quaternion()
    return result


def bake(nodes, parents, rotations, curves):
    clips = {}
    for side in ('l', 'r'):
        samples = {n['name']: [] for n in nodes}
        for frame in range(COUNT - 1):
            body_deltas = evaluate(curves, frame / FPS)
            for i, n in enumerate(nodes):
                name = n['name']
                other = name
                if side == 'r' and name.endswith(('_l', '_r')):
                    other = name[:-2] + ('_r' if name.endswith('_l') else '_l')
                d = body_deltas[other].copy()
                if side == 'r':
                    # Reflection across X changes axial vectors as (x, -y, -z).
                    d.y, d.z = -d.y, -d.z
                pr = rotations[parents[i]] if i in parents else Quaternion()
                delta = pr.inverted() @ d @ pr
                key = (delta @ quat(n.get('rotation', (0, 0, 0, 1)))).normalized()
                if samples[name] and sum(a * b for a, b in zip(samples[name][-1], xyzw(key))) < 0:
                    key.negate()
                samples[name].append(xyzw(key))
        for values in samples.values():
            values.append(values[0].copy())
        clips['brake_bank_' + side] = samples
    return clips


def write_glb(nodes, parents, clips):
    binary = bytearray()
    doc = {'asset': {'version': '2.0', 'generator': 'Embercrest brake-bank trial; Blender + exact-rest GLB writer',
                     'extras': {'ai_generated': True}},
           'scene': 0, 'scenes': [{'nodes': [i for i in range(len(nodes)) if i not in parents]}],
           'nodes': nodes, 'animations': [], 'accessors': [], 'bufferViews': [], 'buffers': []}

    def add(values, kind):
        offset = len(binary)
        for v in values:
            binary.extend(struct.pack('<' + 'f' * len(v), *v))
        view = len(doc['bufferViews'])
        doc['bufferViews'].append({'buffer': 0, 'byteOffset': offset, 'byteLength': len(binary) - offset})
        a = {'bufferView': view, 'componentType': 5126, 'count': len(values), 'type': kind}
        if kind == 'SCALAR':
            a.update(min=[values[0][0]], max=[values[-1][0]])
        doc['accessors'].append(a)
        return len(doc['accessors']) - 1
    times = add([[i / FPS] for i in range(COUNT)], 'SCALAR')
    for name, samples in clips.items():
        anim = {'name': name, 'channels': [], 'samplers': []}
        for i, n in enumerate(nodes):
            output = add(samples[n['name']], 'VEC4')
            anim['channels'].append({'sampler': len(anim['samplers']), 'target': {'node': i, 'path': 'rotation'}})
            anim['samplers'].append({'input': times, 'output': output, 'interpolation': 'LINEAR'})
        doc['animations'].append(anim)
    doc['buffers'].append({'byteLength': len(binary)})
    data = json.dumps(doc, separators=(',', ':'), allow_nan=False).encode()
    data += b' ' * (-len(data) % 4)
    binary += b'\0' * (-len(binary) % 4)
    OUTPUT.write_bytes(struct.pack('<4sII', b'glTF', 2, 28 + len(data) + len(binary))
                       + struct.pack('<I4s', len(data), b'JSON') + data
                       + struct.pack('<I4s', len(binary), b'BIN\0') + binary)


def read_exported_tracks():
    # Preview the actual float32 export, not the unrounded authoring curves.
    doc, binary = read_glb(OUTPUT)
    result = {}
    for anim in doc['animations']:
        tracks = {}
        for channel in anim['channels']:
            sampler = anim['samplers'][channel['sampler']]
            name = doc['nodes'][channel['target']['node']]['name']
            tracks[name] = accessor(doc, binary, sampler['output'])
        result[anim['name']] = tracks
    return result


def set_pose(rig, nodes, parents, rotations, tracks, frame):
    for i, n in enumerate(nodes):
        rest = quat(n.get('rotation', (0, 0, 0, 1)))
        delta = quat(tracks[n['name']][frame]) @ rest.inverted()
        pr = rotations[parents[i]] if i in parents else Quaternion()
        rig.pose.bones[n['name']].rotation_quaternion = blender_basis(rig, n['name'], pr, delta)
    bpy.context.view_layer.update()


def check_preview_pose(rig, nodes, parents, rest_worlds, rotations, clips):
    # Compare Blender joints against forward kinematics from exported keys.
    # Forward kinematics composes each joint transform through its parent chain.
    audit = {}
    for clip, tracks in clips.items():
        maximum = 0.0
        angles = {name: [] for name in ('root', 'chest', 'neck_01', 'head')}
        for frame in range(COUNT):
            set_pose(rig, nodes, parents, rotations, tracks, frame)
            worlds = {}

            def visit(i):
                if i in worlds:
                    return
                n = nodes[i]
                q = quat(tracks[n['name']][frame])
                local = Matrix.LocRotScale(Vector(n.get('translation', (0, 0, 0))), q,
                                          Vector(n.get('scale', (1, 1, 1))))
                if i in parents:
                    visit(parents[i])
                    worlds[i] = worlds[parents[i]] @ local
                else:
                    worlds[i] = local
            for i, n in enumerate(nodes):
                visit(i)
                expected = C @ worlds[i].translation
                actual = rig.pose.bones[n['name']].head
                maximum = max(maximum, (expected - actual).length)
                if n['name'] in angles:
                    d = worlds[i].to_quaternion() @ rest_worlds[i].to_quaternion().inverted()
                    angles[n['name']].append(math.degrees(d.angle))
        assert maximum < 0.000002, ('Preview joint mismatch', clip, maximum)
        audit[clip] = {'max_joint_position_error_source_units': maximum,
                       'world_offset_peak_deg': {n: max(v) for n, v in angles.items()}}
    (ART / 'preview-pose-check.json').write_text(json.dumps(audit, indent=2) + '\n')
    print('PASS: Blender preview matches exported joint transforms', audit, flush=True)


def render_previews(rig, meshes, nodes, parents, rotations, clips):
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.render.resolution_x = 960
    scene.render.resolution_y = 600
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.film_transparent = False
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.studiolight_rotate_z = 0.3
    scene.display.shading.color_type = 'MATERIAL'
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.display.shading.cavity_type = 'BOTH'
    scene.display.shading.curvature_ridge_factor = 1.35
    scene.display.shading.curvature_valley_factor = 1.1
    scene.display.shading.show_specular_highlight = True
    scene.display.shading.background_type = 'WORLD'
    scene.world = bpy.data.worlds.new('Preview world')
    scene.world.color = (0.035, 0.045, 0.060)
    scene.view_settings.view_transform = 'Standard'
    for mesh in meshes:
        for mat in mesh.data.materials:
            mat.diffuse_color = (0.44, 0.55, 0.63, 1)
    camera_data = bpy.data.cameras.new('Clip preview')
    camera = bpy.data.objects.new('Clip preview', camera_data)
    scene.collection.objects.link(camera)
    scene.camera = camera
    camera_data.type = 'ORTHO'
    camera_data.lens = 50
    camera_data.clip_start, camera_data.clip_end = 0.01, 100
    target = Vector((0, -0.14, 0.19))
    views = {'side': (Vector((2.6, -0.02, 0.1)), 1.50),
             'front': (Vector((0, 3, 0.06)), 1.64),
             'below': (Vector((0, 0.35, -3)), 1.95)}
    # Camera offsets are relative to the fixed target. All frames share framing.
    manifest = {'source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
                'clip_sha256': hashlib.sha256(OUTPUT.read_bytes()).hexdigest(),
                'renderer': 'Blender 5.2 Workbench', 'procedural_flare': False,
                'frames': [], 'detail_frames': [], 'ai_generated': True}
    for clip, tracks in clips.items():
        folder = ART / 'frames' / clip
        folder.mkdir(parents=True, exist_ok=True)
        for view, (offset, scale) in views.items():
            camera.location = target + offset
            camera.rotation_euler = (-offset).to_track_quat('-Z', 'Y').to_euler()
            camera_data.ortho_scale = scale
            for frame in range(0, 72, 9):
                set_pose(rig, nodes, parents, rotations, tracks, frame)
                path = folder / f'{view}_{frame:02}.png'
                scene.render.filepath = str(path)
                bpy.ops.render.render(write_still=True)
                manifest['frames'].append({'clip': clip, 'view': view, 'frame': frame,
                                           'time': frame / FPS, 'path': str(path.relative_to(ART))})
        # Full-size detail views supplement the requested contact sheets.
        details = [('above', Vector((0, -0.15, 3)), target, 1.95, 18),
                   ('rear', Vector((0, -3, 0.1)), target, 1.64, 45),
                   ('head', Vector((2, 0, 0.08)), Vector((0, 0.29, 0.38)), 0.46, 36)]
        for view, offset, aim, scale, frame in details:
            set_pose(rig, nodes, parents, rotations, tracks, frame)
            camera.location = aim + offset
            camera.rotation_euler = (-offset).to_track_quat('-Z', 'Y').to_euler()
            camera_data.ortho_scale = scale
            path = folder / f'{view}_{frame:02}.png'
            scene.render.filepath = str(path)
            bpy.ops.render.render(write_still=True)
            manifest['detail_frames'].append({'clip': clip, 'view': view, 'frame': frame,
                                             'path': str(path.relative_to(ART))})
    reset_pose(rig)
    folder = ART / 'frames' / 'bind'
    folder.mkdir(parents=True, exist_ok=True)
    for view, offset, aim, scale in [
            ('front', Vector((0, 3, 0.06)), target, 1.64),
            ('head', Vector((2, 0, 0.08)), Vector((0, 0.29, 0.38)), 0.46)]:
        camera.location = aim + offset
        camera.rotation_euler = (-offset).to_track_quat('-Z', 'Y').to_euler()
        camera_data.ortho_scale = scale
        scene.render.filepath = str(folder / (view + '.png'))
        bpy.ops.render.render(write_still=True)
    (ART / 'preview-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    subprocess.run(['/opt/homebrew/bin/python3.14', str(ROOT / 'tools/clips/preview_report.py')], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--preview', action='store_true')
    parser.add_argument('--probe-only', action='store_true')
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    ART.mkdir(parents=True, exist_ok=True)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    doc, _ = read_glb(SOURCE)
    nodes, parents, worlds, rotations = source_rig(doc)
    rig, meshes = import_mesh()
    measure_axes(rig, nodes, parents, rotations)
    if args.probe_only:
        return
    curves = author_curves([n['name'] for n in nodes])
    clips = bake(nodes, parents, rotations, curves)
    write_glb(nodes, parents, clips)
    report = check(SOURCE, OUTPUT)
    (ART / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
    if report['errors']:
        raise RuntimeError('\n'.join(report['errors']))
    print('PASS: exported and checked', OUTPUT, flush=True)
    if args.preview:
        exported = read_exported_tracks()
        check_preview_pose(rig, nodes, parents, worlds, rotations, exported)
        render_previews(rig, meshes, nodes, parents, rotations, exported)


if __name__ == '__main__':
    main()
