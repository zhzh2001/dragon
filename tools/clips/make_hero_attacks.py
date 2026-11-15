"""Author hero attack overlays and render their exported keys in Blender.

Run Blender with -b --python tools/clips/make_hero_attacks.py -- --preview.
Use --probe-only to measure axes without writing clips.
The writer follows make_brake_bank.py and copies source rest TRS exactly.
"""
import argparse
import hashlib
import inspect
import json
import math
from pathlib import Path
import struct
import subprocess
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import bpy
from mathutils import Matrix, Quaternion, Vector
import make_brake_bank as base
from check_clip import read_glb, accessor

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'assets/embercrest-hero.glb'
OUTPUT = ROOT / 'assets/clips/embercrest-hero.attacks.glb'
ART = ROOT / 'artifacts/hero-clips'
FPS = 30
LENGTHS = {'bite': 27, 'claw_r': 27, 'claw_l': 27, 'tail_r': 30, 'tail_l': 30}
base.SOURCE, base.OUTPUT, base.ART = SOURCE, OUTPUT, ART
C, CQ = base.C, base.CQ
AXES = base.AXES


def measure_axes(rig, nodes, parents, rotations):
    """Measure positive model-axis probes through actual Blender deformation."""
    base.reset_pose(rig)
    source_doc, _ = read_glb(SOURCE)
    source_nodes = {n['name']: n for n in source_doc['nodes']}
    result = {'degrees': 5, 'frame': '+X right, +Y up, -Z forward', 'joints': {}}
    for i, node in enumerate(nodes):
        name = node['name']
        pb = rig.pose.bones[name]
        children = node.get('children', [])
        child = nodes[children[0]]['name'] if children else name
        # Preserve the source tip; Blender invents vertical tips for leaf bones.
        source_tip = C @ Vector(source_nodes[child]['extras']['tail'])
        tip_local = rig.data.bones[child].matrix_local.inverted() @ source_tip
        point = lambda: rig.matrix_world @ rig.pose.bones[child].matrix @ tip_local
        start = point().copy()
        pr = rotations[parents[i]] if i in parents else Quaternion()
        br = rig.data.bones[name].matrix_local.to_quaternion()
        row = {'point': child + '.source_tail', 'axes': {}}
        for label, axis in AXES.items():
            pa = pr.inverted() @ axis
            pb.rotation_quaternion = base.blender_basis(rig, name, pr, Quaternion(pa, math.radians(5)))
            bpy.context.view_layer.update()
            row['axes'][label] = {'parent_axis': list(pa), 'blender_bone_axis': list(br.inverted() @ (C @ axis)),
                                 'displacement_m': list(C.inverted() @ (point() - start))}
            pb.rotation_quaternion = Quaternion()
            bpy.context.view_layer.update()
        result['joints'][name] = row
    (ART / 'axis-probes.json').write_text(json.dumps(result, indent=2) + '\n')
    print('PASS measured 77 joints on three axes in Blender', flush=True)
    return result


def curves():
    """Use eased pose knots in frame units, with explicit chain delays."""
    clips = {k: {} for k in ('bite', 'claw_r', 'tail_r')}

    def add(clip, joint, axis, knots):
        end = LENGTHS[clip]
        assert knots[0] == (0, 0) and knots[-1] == (end, 0)
        delay = 0
        if joint.startswith('tail_') or (clip == 'bite' and joint.startswith('neck_')):
            delay = int(joint[-2:])-1
        elif clip == 'bite' and joint == 'head':
            delay = 3
        elif clip.startswith('claw') and joint in ('forearm_r','wrist_r'):
            delay = 1 if joint == 'forearm_r' else 2
        if delay:
            assert delay < knots[1][0]
            knots = [knots[0],(delay,0),*knots[1:]]
        clips[clip].setdefault(joint, []).append((axis, knots))

    def pose(clip, joint, axis, wind, hit, wtime, htime, release, settle, end=None):
        end = LENGTHS[clip] if end is None else end
        add(clip, joint, axis, [(0, 0), (wtime, wind), (htime, hit), (release, hit),
                               (settle, -hit * .075), (end, 0)])

    for j in range(3):
        add('bite', f'neck_{j+1:02}', 'pitch', [(0, 0), (3+j, 14), (5+j*.5, 14),
            (7+j, -9.5), (11+j*.3, -9.5), (17+j, .7), (27, 0)])
    add('bite', 'head', 'pitch', [(0, 0), (6, 5), (10, -5), (14, 8), (21, -.5), (27, 0)])
    add('bite', 'jaw', 'pitch', [(0, 0), (4.8, -40), (5.5, -40), (8, 2.5), (10.8, 0), (27, 0)])
    pose('bite', 'root', 'pitch', 2, -6, 4, 7, 10, 18)
    pose('bite', 'chest', 'pitch', 3, -8, 5, 8, 11, 19)
    for side, sign in [('r', 1), ('l', -1)]:
        pose('bite', 'wing_root_'+side, 'yaw', sign*3, -sign*15, 5, 8, 13, 23)
        pose('bite', 'upper_arm_'+side, 'pitch', -8, 25, 4, 8, 10, 19)
        pose('bite', 'forearm_'+side, 'pitch', 8, -12, 5, 9, 11, 20)
        for toe in range(1, 4):
            pose('bite', f'toe_fore_{toe}_{side}', 'pitch', 0, 12, 4, 9, 11, 20)
            pose('bite', f'toe_fore_{toe}_{side}', 'yaw', 0, -sign*(toe-2)*12, 4, 8, 11, 21)
    for j in range(8):
        peak = -12 if j == 0 else -(4-j*.25)
        pose('bite', f'tail_{j+1:02}', 'pitch', 0, peak, 3+j, 8+j, 10+j, 20+j*.5)

    # Positive X reaches forward and closes the forearm's bent elbow.
    pose('claw_r', 'upper_arm_r', 'pitch', -30, 30, 5.4, 9, 10, 20)
    add('claw_r', 'upper_arm_r', 'roll', [(0,0),(5.4,-10),(10,-35),(12.6,-38),(21,2.5),(27,0)])
    pose('claw_r', 'forearm_r', 'pitch', 30, -20, 6.4, 10, 12, 21)
    pose('claw_r', 'wrist_r', 'pitch', 8, -40, 7.4, 11, 12.6, 22)
    pose('claw_r', 'upper_arm_l', 'pitch', 0, -15, 4, 9, 12, 20)
    pose('claw_r', 'root', 'yaw', 4, -12, 5, 9, 12, 20)
    pose('claw_r', 'chest', 'yaw', 5, -15, 5.4, 10, 12.6, 21)
    for j in range(3):
        pose('claw_r', f'neck_{j+1:02}', 'yaw', 1, -5, 4+j*.3, 8+j, 12, 19+j)
    pose('claw_r', 'head', 'yaw', 3, -15, 3, 7, 11, 18)
    pose('claw_r', 'wing_root_r', 'roll', -3, 15, 5, 9, 12, 22)
    pose('claw_r', 'wing_root_l', 'roll', -2, 10, 5, 10, 13, 23)
    for toe in range(1, 4):
        add('claw_r', f'toe_fore_{toe}_r', 'pitch', [(0,0),(7,18),(10,18),(13,-28),(22,2),(27,0)])
        add('claw_r', f'toe_fore_{toe}_r', 'yaw', [(0,0),(7,-(toe-2)*16),(10,-(toe-2)*16),(14,0),(27,0)])
    for j in range(8):
        pose('claw_r', f'tail_{j+1:02}', 'yaw', 3 if j == 0 else 1.5,
             -22 if j == 0 else -(5+j*.4), 4+j, 9+j, 11+j, 21+j*.5)

    # A narrow positive pulse travels down the tail. Distal hinges peak later and larger.
    for j in range(8):
        wind = -25 if j == 0 else -(6+j*.5)
        hit = 35+j
        add('tail_r', f'tail_{j+1:02}', 'yaw', [(0,0),(6+j,wind),(10+j,hit),
            (13+j,0),(17+j,-hit*.06),(30,0)])
    pose('tail_r', 'root', 'yaw', -5, 12, 6, 10, 13, 23)
    pose('tail_r', 'chest', 'yaw', -3, 10, 7, 11, 14, 24)
    for j in range(3):
        pose('tail_r', f'neck_{j+1:02}', 'yaw', 2, -5, 6+j, 11+j, 15+j, 24+j)
    pose('tail_r', 'head', 'yaw', 4, -15, 8, 14, 17, 26)
    pose('tail_r', 'thigh_r', 'pitch', 4, -15, 7, 11, 15, 25)
    return clips


def evaluate(knots, frame):
    for (a, x), (b, y) in zip(knots, knots[1:]):
        if frame <= b:
            u = max(0, (frame-a)/(b-a))
            return x+(y-x)*u*u*(3-2*u)
    return knots[-1][1]


def bake(nodes, parents, rotations, authored):
    clips = {}
    for clip, end in LENGTHS.items():
        mirrored = clip.endswith('_l')
        origin = clip[:-1]+'r' if mirrored else clip
        tracks = {n['name']: [] for n in nodes}
        for f in range(end+1):
            for i, n in enumerate(nodes):
                name = n['name']
                other = name
                if mirrored and name.endswith(('_r','_l')):
                    other = name[:-1]+('l' if name.endswith('_r') else 'r')
                v = Vector((0,0,0))
                for axis, knots in authored[origin].get(other, []):
                    v += AXES[axis]*evaluate(knots, f)
                d = Quaternion(v.normalized(), math.radians(v.length)) if v.length else Quaternion()
                if mirrored:
                    d.y, d.z = -d.y, -d.z
                pr = rotations[parents[i]] if i in parents else Quaternion()
                q = (pr.inverted() @ d @ pr @ base.quat(n.get('rotation',(0,0,0,1)))).normalized()
                if tracks[name] and sum(a*b for a,b in zip(tracks[name][-1],base.xyzw(q))) < 0:
                    q.negate()
                tracks[name].append(base.xyzw(q))
        clips[clip] = tracks
    return clips


def write_clips(nodes, parents, clips):
    binary = bytearray()
    doc = {'asset': {'version':'2.0', 'generator':'Hero attack clips; Blender exact-rest writer',
                    'extras':{'ai_generated':True}}, 'scene':0,
           'scenes':[{'nodes':[i for i in range(len(nodes)) if i not in parents]}],
           'nodes':nodes,'animations':[],'accessors':[],'bufferViews':[],'buffers':[]}
    def add(values, kind):
        offset = len(binary)
        for v in values:
            binary.extend(struct.pack('<'+'f'*len(v), *v))
        view = len(doc['bufferViews'])
        doc['bufferViews'].append({'buffer':0,'byteOffset':offset,'byteLength':len(binary)-offset})
        a = {'bufferView':view,'componentType':5126,'count':len(values),'type':kind}
        if kind == 'SCALAR':
            a.update(min=[values[0][0]],max=[values[-1][0]])
        doc['accessors'].append(a)
        return len(doc['accessors'])-1
    for name, tracks in clips.items():
        times = add([[f/FPS] for f in range(LENGTHS[name]+1)], 'SCALAR')
        anim = {'name':name,'channels':[],'samplers':[]}
        for i,n in enumerate(nodes):
            output = add(tracks[n['name']], 'VEC4')
            anim['channels'].append({'sampler':len(anim['samplers']),'target':{'node':i,'path':'rotation'}})
            anim['samplers'].append({'input':times,'output':output,'interpolation':'LINEAR'})
        doc['animations'].append(anim)
    doc['buffers'] = [{'byteLength':len(binary)}]
    data = json.dumps(doc,separators=(',',':'),allow_nan=False).encode()
    data += b' '*(-len(data)%4)
    binary += b'\0'*(-len(binary)%4)
    OUTPUT.write_bytes(struct.pack('<4sII',b'glTF',2,28+len(data)+len(binary))+
        struct.pack('<I4s',len(data),b'JSON')+data+struct.pack('<I4s',len(binary),b'BIN\0')+binary)


def actions_and_audit(rig, nodes, parents, worlds, rotations, clips):
    """Apply exported tracks as Blender actions and compare all joint positions."""
    actions, audit = {}, {}
    rig.animation_data_create()
    for clip, tracks in clips.items():
        action = bpy.data.actions.new(clip)
        rig.animation_data.action = action
        maximum = 0
        for f in range(LENGTHS[clip]+1):
            base.set_pose(rig,nodes,parents,rotations,tracks,f)
            for pb in rig.pose.bones:
                pb.keyframe_insert('rotation_quaternion',frame=f,group=pb.name)
            posed = {}
            def visit(i):
                if i not in posed:
                    n = nodes[i]
                    local = Matrix.LocRotScale(Vector(n.get('translation',(0,0,0))),
                        base.quat(tracks[n['name']][f]),Vector(n.get('scale',(1,1,1))))
                    posed[i] = visit(parents[i]) @ local if i in parents else local
                return posed[i]
            for i,n in enumerate(nodes):
                expected = C @ visit(i).translation
                maximum = max(maximum,(rig.pose.bones[n['name']].head-expected).length)
        for slot in action.slots:
            for layer in action.layers:
                for strip in layer.strips:
                    bag = strip.channelbag(slot)
                    if bag:
                        for fc in bag.fcurves:
                            for key in fc.keyframe_points:
                                key.interpolation = 'LINEAR'
        assert maximum < .0001, (clip,maximum)
        actions[clip] = action
        audit[clip] = {'max_joint_position_error_m':maximum}
    (ART/'preview-pose-check.json').write_text(json.dumps(audit,indent=2)+'\n')
    print('PASS Blender actions match exported forward kinematics',audit,flush=True)
    return actions


def render_previews(rig, meshes, actions):
    scene = bpy.context.scene
    scene.render.fps = FPS
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.render.resolution_x, scene.render.resolution_y = 960, 640
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'JPEG'
    scene.render.image_settings.quality = 93
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.color_type = 'MATERIAL'
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.display.shading.cavity_type = 'BOTH'
    scene.display.shading.curvature_ridge_factor = 1.3
    scene.display.shading.curvature_valley_factor = 1.0
    scene.display.shading.background_type = 'WORLD'
    scene.world = bpy.data.worlds.new('Preview world')
    scene.world.color = (.045,.055,.07)
    scene.view_settings.view_transform = 'Standard'
    for mesh in meshes:
        for mat in mesh.data.materials:
            mat.diffuse_color = (.48,.59,.66,1)
    cd = bpy.data.cameras.new('Attack preview')
    camera = bpy.data.objects.new('Attack preview',cd)
    scene.collection.objects.link(camera)
    scene.camera = camera
    cd.type, cd.clip_end = 'ORTHO', 200
    manifest = {'source_sha256':hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
                'clip_sha256':hashlib.sha256(OUTPUT.read_bytes()).hexdigest(),
                'blender':bpy.app.version_string,'frames':[],'ai_generated':True}
    samples = {'bite':[0,4,5,8,10,14,20,27], 'claw':[0,5,7,9,12,16,21,27],
               'tail':[0,6,10,12,15,17,23,30]}
    for clip, action in actions.items():
        rig.animation_data.action = action
        # Use the striking side for the side view. Both top views use one camera.
        sign = -1 if clip.endswith('_l') else 1
        views = {'side':(Vector((sign*35,0,0)),Vector((0,-1.7,2.4)),23),
                 'top':(Vector((0,0,40)),Vector((0,-3.6,2.4)),30)}
        folder = ART/'frames'/clip
        folder.mkdir(parents=True,exist_ok=True)
        for view,(offset,target,scale) in views.items():
            camera.location = target+offset
            camera.rotation_euler = (-offset).to_track_quat('-Z','Y').to_euler()
            cd.ortho_scale = scale
            for f in samples[clip.split('_')[0]]:
                scene.frame_set(f)
                path = folder/f'{view}-{f:02}.jpg'
                scene.render.filepath = str(path)
                bpy.ops.render.render(write_still=True)
                manifest['frames'].append({'clip':clip,'view':view,'frame':f,'time':f/FPS,
                                           'path':str(path.relative_to(ART))})
    # Full-size front, rear, and head views supplement the requested strips.
    manifest['details'] = []
    for clip, action in actions.items():
        rig.animation_data.action = action
        frames = [5, 9] if clip == 'bite' else ([5, 12] if clip.startswith('claw') else [6, 12])
        for f in frames:
            scene.frame_set(f)
            for view, offset in [('front', Vector((0,35,0))), ('rear', Vector((0,-35,0)))]:
                target = Vector((0,0,2.8))
                camera.location = target+offset
                camera.rotation_euler = (-offset).to_track_quat('-Z','Y').to_euler()
                cd.ortho_scale = 24
                path = ART/'frames'/clip/f'{view}-{f:02}.jpg'
                scene.render.filepath = str(path)
                bpy.ops.render.render(write_still=True)
                manifest['details'].append({'clip':clip,'view':view,'frame':f,'path':str(path.relative_to(ART))})
        for f in ([0,5,8,10,14] if clip == 'bite' else [12]):
            scene.frame_set(f)
            head = rig.pose.bones['head']
            target = head.head + Vector((0,0,.4))
            turn = head.matrix.to_quaternion() @ rig.data.bones['head'].matrix_local.to_quaternion().inverted()
            offset = turn @ Vector((-15 if clip.endswith('_l') else 15,0,0))
            camera.location = target+offset
            camera.rotation_euler = (-offset).to_track_quat('-Z','Y').to_euler()
            cd.ortho_scale = 5.7
            path = ART/'frames'/clip/f'head-{f:02}.jpg'
            scene.render.filepath = str(path)
            bpy.ops.render.render(write_still=True)
            manifest['details'].append({'clip':clip,'view':'head','frame':f,'path':str(path.relative_to(ART))})
    (ART/'preview-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    # Pillow runs in system Python. The source stays inside the allowed builder file.
    python = '/opt/homebrew/bin/python3.14'
    code = 'import json\nfrom pathlib import Path\n'
    code += f'ART = Path({str(ART)!r})\nLENGTHS = {LENGTHS!r}\n'
    code += inspect.getsource(sheets) + '\nsheets()\n'
    subprocess.run([python,'-B','-c',code],check=True)


def sheets():
    from PIL import Image, ImageDraw, ImageFont
    manifest = json.loads((ART/'preview-manifest.json').read_text())
    font = ImageFont.truetype('/System/Library/Fonts/Menlo.ttc',22)
    for clip in LENGTHS:
        for view in ('side','top'):
            frames = [r for r in manifest['frames'] if r['clip']==clip and r['view']==view]
            canvas = Image.new('RGB',(960*4,688*2+48),(15,19,25))
            draw = ImageDraw.Draw(canvas)
            draw.text((16,10),f'{clip} / {view} / AI-authored / exported keys at 30 fps',font=font,fill='white')
            for j,row in enumerate(frames):
                x,y = j%4*960,48+j//4*688
                with Image.open(ART/row['path']) as im:
                    canvas.paste(im.convert('RGB'),(x,y+40))
                draw.text((x+16,y+8),f"{row['time']:.3f} s   frame {row['frame']}",font=font,fill='white')
            canvas.save(ART/f'{clip}-{view}.jpg',quality=93)
    from PIL import ImageChops
    for clip in LENGTHS:
        view = 'top' if clip.startswith('tail') else 'side'
        frame = 12 if clip.startswith('tail') else (8 if clip == 'bite' else 9)
        paths = [ART/'frames'/clip/f'{view}-{f:02}.jpg' for f in (0,frame)]
        pictures = [Image.open(p).convert('RGB') for p in paths]
        boxes = []
        for im in pictures:
            bg = Image.new('RGB',im.size,im.getpixel((0,0)))
            mask = ImageChops.difference(im,bg).convert('L').point(lambda x:255 if x>25 else 0)
            boxes.append(mask.getbbox())
        box = (max(0,min(b[0] for b in boxes)-12),max(0,min(b[1] for b in boxes)-12),
               min(960,max(b[2] for b in boxes)+12),min(640,max(b[3] for b in boxes)+12))
        w,h = box[2]-box[0],box[3]-box[1]
        pair = Image.new('RGB',(w*2,h+48),(15,19,25))
        draw = ImageDraw.Draw(pair)
        for i,(im,f) in enumerate(zip(pictures,(0,frame))):
            pair.paste(im.crop(box),(i*w,48))
            draw.text((i*w+12,10),f'{clip} / {f/30:.3f} s / AI-authored',font=font,fill='white')
        pair.save(ART/f'{clip}-rest-strike.jpg',quality=93)



def main():
    args = sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--preview',action='store_true')
    parser.add_argument('--probe-only',action='store_true')
    opt = parser.parse_args(args)
    ART.mkdir(parents=True,exist_ok=True)
    doc,_ = read_glb(SOURCE)
    nodes,parents,worlds,rotations = base.source_rig(doc)
    rig,meshes = base.import_mesh()
    measure_axes(rig,nodes,parents,rotations)
    if opt.probe_only:
        return
    authored = curves()
    (ART/'authoring-knots.json').write_text(json.dumps(authored,indent=2)+'\n')
    clips = bake(nodes,parents,rotations,authored)
    write_clips(nodes,parents,clips)
    exported = base.read_exported_tracks()
    actions = actions_and_audit(rig,nodes,parents,worlds,rotations,exported)
    if opt.preview:
        render_previews(rig,meshes,actions)


if __name__ == '__main__':
    main()
