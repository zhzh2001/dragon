"""Simplify and fit a deform rig to the selected Hunyuan candidate.
Run: Blender --background --factory-startup --python tools/rig_embercrest_candidate.py
Add -- --textured after the script path to retain the textured candidate's PBR maps.
The candidate and original procedural Embercrest remain untouched.
"""
import bpy
import json
import math
import hashlib
import argparse
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from repair_gltf_tangents import repair_tangents
from mathutils import Vector, Quaternion

ROOT = Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--textured',action='store_true',help='Rig the textured Hunyuan candidate and retain its UVs and PBR maps')
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
TEXTURED=args.textured
STEM='embercrest-textured' if TEXTURED else 'embercrest-selected'
SOURCE = ROOT / ('assets/embercrest-cand-hunyuan-textured.glb' if TEXTURED else 'assets/embercrest-cand-hunyuan-1p5m.glb')
OUT = ROOT / ('assets/embercrest/textured' if TEXTURED else 'assets/embercrest/selected')
CAP = ROOT / ('artifacts/'+STEM)
GLB = ROOT / ('assets/'+STEM+'.glb')
TARGET = 80000
OUT.mkdir(parents=True, exist_ok=True)
CAP.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.gltf(filepath=str(SOURCE),merge_vertices=True)
body = next(o for o in bpy.context.selected_objects if o.type == 'MESH')
body.name = 'Embercrest_Textured' if TEXTURED else 'Embercrest_Selected'
bpy.context.view_layer.objects.active = body
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
original_faces = len(body.data.polygons)
# The source faces -Y. Author the rig in +Y so glTF faces -Z.
body.rotation_mode = 'XYZ'
body.rotation_euler.z = math.pi
bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
# The texture export has slightly different bounds. Fit bones/weight landmarks
# to those bounds rather than rescaling the mesh or changing its authored UVs.
reference_lo=Vector((-.582478404,-.661605179,0.))
reference_hi=Vector((.580175459,.400670350,.518391728))
actual_lo=Vector(tuple(min(v.co[i] for v in body.data.vertices) for i in range(3)))
actual_hi=Vector(tuple(max(v.co[i] for v in body.data.vertices) for i in range(3)))
fit_scale=Vector(tuple((actual_hi[i]-actual_lo[i])/(reference_hi[i]-reference_lo[i]) if TEXTURED else 1. for i in range(3)))
fit_bias=actual_lo-Vector(tuple(reference_lo[i]*fit_scale[i] for i in range(3))) if TEXTURED else Vector((0,0,0))
def fitted(co):return Vector(tuple(co[i]*fit_scale[i]+fit_bias[i] for i in range(3)))
def canonical(co):return Vector(tuple((co[i]-fit_bias[i])/fit_scale[i] for i in range(3)))
source_images=[{'name':im.name,'size':list(im.size),'colorspace':im.colorspace_settings.name} for im in bpy.data.images if im.type=='IMAGE']
if TEXTURED:
    assert body.data.uv_layers and body.data.materials, 'Textured source must have UVs and materials'
    # Weld exact geometric duplicates while retaining per-corner UVs. This
    # connects UV islands for heat binding without throwing away texture seams.
    import bmesh
    bm=bmesh.new();bm.from_mesh(body.data)
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-7)
    bm.to_mesh(body.data);bm.free()
mod = body.modifiers.new('Silhouette simplification', 'DECIMATE')
mod.ratio = TARGET / original_faces
bpy.ops.object.modifier_apply(modifier=mod.name)
for p in body.data.polygons:
    p.use_smooth = True
print('SIMPLIFIED',len(body.data.vertices),len(body.data.polygons),flush=True)

bones = []
def bone(name, a, b, parent=None):
    bones.append((name, tuple(a), tuple(b), parent))
def chain(names, points, parent):
    for i, name in enumerate(names):
        bone(name, points[i], points[i+1], parent if i == 0 else names[i-1])
bone('root', (0,-.035,.17), (0,.08,.19))
bone('chest', (0,.08,.19), (0,.21,.23), 'root')
chain(['neck_01','neck_02','neck_03'], [(0,.21,.23),(0,.255,.30),(0,.255,.36),(0,.285,.40)], 'chest')
bone('head',(0,.285,.40),(0,.395,.385),'neck_03')
bone('jaw',(0,.285,.367),(0,.385,.337),'head')
if TEXTURED:
    # Runtime determines opening direction by probing a descendant tip. A leaf
    # jaw only probes its unmoving pivot and can choose the closing direction.
    bone('jaw_tip',(0,.385,.337),(0,.400,.332),'jaw')
chain([f'tail_{i:02d}' for i in range(1,9)],[(0,y,z) for y,z in [(-.035,.17),(-.11,.135),(-.19,.105),(-.27,.080),(-.35,.067),(-.43,.065),(-.51,.072),(-.59,.080),(-.66,.062)]], 'root')
for side,suffix in [(-1,'l'),(1,'r')]:
    def p(x,y,z):return (side*x,y,z)
    chain([f'upper_arm_{suffix}',f'forearm_{suffix}',f'wrist_{suffix}',f'hand_{suffix}'],
          [p(.052,.205,.17),p(.068,.18,.09),p(.074,.215,.038),p(.077,.225,.014),p(.077,.277,.013)],'chest')
    chain([f'thigh_{suffix}',f'shin_{suffix}',f'ankle_{suffix}',f'foot_{suffix}'],
          [p(.058,-.014,.168),p(.085,.040,.084),p(.082,-.054,.055),p(.083,-.039,.014),p(.083,.016,.012)],'root')
    for prefix,base,parent in [('fore',(.077,.245,.014),f'hand_{suffix}'),('hind',(.083,-.012,.014),f'foot_{suffix}')]:
        for j in range(3):
            x=base[0]+(j-1)*.022
            bone(f'toe_{prefix}_{j+1}_{suffix}',p(x,base[1],base[2]),p(x+(j-1)*.009,base[1]+.035,.008),parent)
    shoulder=p(.055,.145,.215);elbow=p(.145,.008,.297);wrist=p(.295,.118,.492);hub=p(.318,.111,.487)
    chain([f'wing_root_{suffix}',f'wing_arm_{suffix}',f'wing_wrist_{suffix}'],[shoulder,elbow,wrist,hub],'chest')
    ends=[p(.580,-.135,.401),p(.520,-.335,.270),p(.313,-.294,.128)]
    for j,end in enumerate(ends):
        mid=Vector(hub).lerp(Vector(end),.52)
        chain([f'wing_finger_{j+1}a_{suffix}',f'wing_finger_{j+1}b_{suffix}'],[hub,mid,end],f'wing_wrist_{suffix}')
armdata=bpy.data.armatures.new('Embercrest Selected skeleton')
rig=bpy.data.objects.new('Embercrest_Textured_Rig' if TEXTURED else 'Embercrest_Selected_Rig',armdata)
bpy.context.collection.objects.link(rig)
bpy.ops.object.select_all(action='DESELECT');rig.select_set(True);bpy.context.view_layer.objects.active=rig
bpy.ops.object.mode_set(mode='EDIT')
for name,a,b,parent in bones:
    eb=armdata.edit_bones.new(name);eb.head=fitted(a);eb.tail=fitted(b)
    if parent:eb.parent=armdata.edit_bones[parent]
bpy.ops.object.mode_set(mode='OBJECT')
rig.show_in_front=True;armdata.display_type='OCTAHEDRAL'
for label,prefixes in [('Body',('root','chest','neck','head','jaw')),('Tail',('tail',)),('Wings',('wing',)),('Legs',('upper','forearm','wrist','hand','thigh','shin','ankle','foot','toe'))]:
    coll=armdata.collections.new(label)
    for b in armdata.bones:
        if b.name.startswith(prefixes):coll.assign(b)
# Heat diffusion respects the mesh surface, avoiding cross-talk between a
# wing, a leg and the tail when they overlap in the spread resting pose.
body.select_set(True)
print('HEAT_BIND_START',flush=True)
bpy.ops.object.parent_set(type='ARMATURE_AUTO')
print('HEAT_BIND_DONE',flush=True)
missing=[v.index for v in body.data.vertices if not any(g.weight>1e-8 for g in v.groups)]
if missing:
    raise RuntimeError(f'Automatic bind left {len(missing)} unweighted vertices')
# Thin membranes need matching weights on both faces. Bone heat can attach
# opposite skins to different fingers; use a continuous wing-space field.
wing_bones={suffix:[(n,Vector(a),Vector(b)) for n,a,b,_ in bones
                    if n.startswith('wing') and n.endswith('_'+suffix)]
            for suffix in ('l','r')}
def smoothstep(a,b,x):
    t=max(0.,min(1.,(x-a)/(b-a)))
    return t*t*(3-2*t)

def continuous_top_four(weights):
    """Let a departing fourth influence reach zero before changing groups."""
    ordered=sorted(weights.items(),key=lambda item:item[1],reverse=True)
    cutoff=ordered[4][1] if len(ordered)>4 else 0.
    kept={n:max(0.,w-cutoff) for n,w in ordered[:4]}
    total=sum(kept.values())
    if total<1e-12:
        return {ordered[0][0]:1.}
    return {n:w/total for n,w in kept.items() if w>0}

for v in body.data.vertices:
    co=canonical(v.co)
    x,y,z=co
    blend=(smoothstep(.075,.145,abs(x))*smoothstep(.105,.155,z)
           *(1-smoothstep(.18,.22,y)))
    if blend==0:
        continue
    suffix='l' if x<0 else 'r'
    candidates={}
    for name,a,b in wing_bones[suffix]:
        delta=b-a
        t=max(0.,min(1.,(co-a).dot(delta)/delta.length_squared))
        d=(co-(a+t*delta)).length
        candidates[name]=1/(d*d+.025**2)**1.5
    field=continuous_top_four(candidates)
    hub=Vector(((-1 if x<0 else 1)*.318,.111,.487))
    wrist_share=1-smoothstep(.015,.06,(co-hub).length)
    field={n:w*(1-wrist_share) for n,w in field.items()}
    wrist_name='wing_wrist_'+suffix
    field[wrist_name]=field.get(wrist_name,0)+wrist_share
    combined={body.vertex_groups[g.group].name:g.weight*(1-blend) for g in v.groups}
    for n,w in field.items():combined[n]=combined.get(n,0)+blend*w
    final=continuous_top_four(combined)
    for gi in [g.group for g in v.groups]:body.vertex_groups[gi].remove([v.index])
    for n,w in final.items():body.vertex_groups[n].add([v.index],w,'REPLACE')
# Trim to the runtime contract and explicitly normalize.
bpy.context.view_layer.objects.active=body
bpy.ops.object.vertex_group_limit_total(limit=4)
bpy.ops.object.vertex_group_normalize_all(lock_active=False)
# The cavity is real; rigid jaw/skull assignments keep teeth from stretching.
head_group=body.vertex_groups['head'];jaw_group=body.vertex_groups['jaw']
for v in body.data.vertices:
    co=canonical(v.co)
    x,y,z=co
    if TEXTURED and y>.30 and z>.305:
        # Follow the visible mouth gap instead of cutting through the rising
        # upper surface of the mandible. A flat low plane pins lower teeth and
        # gum vertices to the skull, producing stretched strips on closure.
        jaw_line=(.365 + (.383-.365)*smoothstep(.30,.338,y)
                  - .35*max(y-.338,0))
        share=smoothstep(.295,.335,y) if z<jaw_line else 0.0
        for gi in [g.group for g in v.groups]:body.vertex_groups[gi].remove([v.index])
        if share>0:jaw_group.add([v.index],share,'REPLACE')
        if share<1:head_group.add([v.index],1-share,'REPLACE')
    elif y>.30 and z>.305:
        for gi in [g.group for g in v.groups]:body.vertex_groups[gi].remove([v.index])
        # Lower mandible slopes down toward the muzzle. Upper teeth remain skull.
        jaw_line=.363 - .16*(y-.30)
        (jaw_group if z<jaw_line else head_group).add([v.index],1,'REPLACE')
# Normalize explicitly after edits; vertex-group collection elements are live.
for v in body.data.vertices:
    entries=[(g.group,g.weight) for g in v.groups if g.weight>0]
    total=sum(w for _,w in entries)
    if total<=0:raise RuntimeError(f'Unweighted vertex {v.index}')
    for gi,w in entries:body.vertex_groups[gi].add([v.index],w/total,'REPLACE')
# Remove the sculpt's raised shoulder angle from the runtime bind pose.
# Counter-rotate the wrist so the outer membrane remains spread and level.
# Bake mesh and skeleton together; no scale or pose correction is needed in game.
for pb in rig.pose.bones:pb.rotation_mode='QUATERNION'
for sign,suffix in [(-1,'l'),(1,'r')]:
    for prefix,degrees in [('wing_root_',35),('wing_wrist_',-35)]:
        pb=rig.pose.bones[prefix+suffix]
        axis=pb.bone.matrix_local.to_quaternion().inverted()@Vector((0,1,0))
        pb.rotation_quaternion=Quaternion(axis,math.radians(sign*degrees))
bpy.context.view_layer.update()
bpy.ops.object.select_all(action='DESELECT');body.select_set(True)
bpy.context.view_layer.objects.active=body
skin=next(m for m in body.modifiers if m.type=='ARMATURE')
bpy.ops.object.modifier_apply(modifier=skin.name)
body.select_set(False);rig.select_set(True);bpy.context.view_layer.objects.active=rig
bpy.ops.object.mode_set(mode='POSE');bpy.ops.pose.armature_apply(selected=False)
bpy.ops.object.mode_set(mode='OBJECT')
skin=body.modifiers.new('Embercrest skin','ARMATURE');skin.object=rig
if not TEXTURED:
    # Neutral clay texture is intentional: the selected generation has no UVs or
    # authored textures. A real texture is used because the game ignores factors.
    uv=body.data.uv_layers.new(name='UVMap')
    for poly in body.data.polygons:
        for li in poly.loop_indices:
            v=body.data.vertices[body.data.loops[li].vertex_index].co
            uv.data[li].uv=(v.x+.6,v.y+.7)
    mat=bpy.data.materials.new('Neutral clay - untextured source');mat.use_nodes=True
    mat.diffuse_color=(.42,.45,.48,1)
    bs=mat.node_tree.nodes.get('Principled BSDF');bs.inputs['Roughness'].default_value=.72
    im=bpy.data.images.new('Embercrest neutral clay',width=8,height=8)
    im.pixels[:]=[.42,.45,.48,1]*64;im.pack()
    tex=mat.node_tree.nodes.new('ShaderNodeTexImage');tex.image=im
    mat.node_tree.links.new(tex.outputs['Color'],bs.inputs['Base Color'])
    mat.use_backface_culling=False
    body.data.materials.clear();body.data.materials.append(mat)
else:
    for mat in body.data.materials:
        mat.use_backface_culling=False
    for image in bpy.data.images:
        if image.type=='IMAGE' and image.has_data:image.pack()
rig['source']=SOURCE.name
rig['controls']='FK deform bones; body-space runtime axes; +Y forward, +Z up. Pose action provides deformation checks.'
body['simplification']='Collapse decimation; not quad retopology. Original source unchanged.'
# Save a scrub-able FK inspection action, excluding it from runtime export.
for pb in rig.pose.bones:pb.rotation_mode='QUATERNION'
def reset():
    for pb in rig.pose.bones:pb.rotation_quaternion=Quaternion()
def rotate(name,axis,deg):
    pb=rig.pose.bones[name]
    pb.rotation_quaternion=Quaternion(pb.bone.matrix_local.to_quaternion().inverted()@Vector(axis),math.radians(deg))
scene=bpy.context.scene;scene.frame_start=1;scene.frame_end=101;scene.render.fps=30
for frame in [1,21,41,61,81,101]:
    reset()
    if frame in (21,41):
        for s,suf in [(-1,'l'),(1,'r')]:
            rotate('wing_root_'+suf,(0,1,0),s*(-30 if frame==21 else 35))
            rotate('wing_arm_'+suf,(0,0,1),s*(0 if frame==21 else -25))
            if frame==41:
                rotate('thigh_'+suf,(1,0,0),-25);rotate('upper_arm_'+suf,(1,0,0),25)
    if frame==61:
        rotate('head',(0,0,1),15);rotate('jaw',(1,0,0),-16)
    if frame==81:
        for i in range(1,9):rotate(f'tail_{i:02d}',(0,0,1),6)
    for pb in rig.pose.bones:pb.keyframe_insert(data_path='rotation_quaternion',frame=frame)
rig.animation_data.action.name='Deformation checks - rest flap tuck jaw tail'
scene.frame_set(1);reset();bpy.context.view_layer.update()
bpy.ops.object.select_all(action='DESELECT');body.select_set(True);rig.select_set(True);bpy.context.view_layer.objects.active=rig
bpy.ops.export_scene.gltf(filepath=str(GLB),export_format='GLB',use_selection=True,export_animations=False,export_skins=True,export_def_bones=True,export_yup=True,export_tangents=True)
tangent_repairs=repair_tangents(GLB)
# Presentation camera/light are only in the editable Blender source.
camdata=bpy.data.cameras.new('Preview Camera');cam=bpy.data.objects.new('Preview Camera',camdata);scene.collection.objects.link(cam);scene.camera=cam
camdata.type='ORTHO';camdata.ortho_scale=1.45
cam.location=(1.3,1.9,1.0);target=Vector((0,-.08,.24));cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler()
scene.render.engine='CYCLES';scene.cycles.samples=16;scene.cycles.use_denoising=True
scene.world.color=(.18,.18,.18)
for name,loc,power in [('Key',(1,2,3),100),('Fill',(-2,1,1),70),('Rim',(0,-2,2),130)]:
 d=bpy.data.lights.new(name,'AREA');d.energy=power;d.size=2
 ob=bpy.data.objects.new(name,d);scene.collection.objects.link(ob);ob.location=loc;ob.rotation_euler=(target-ob.location).to_track_quat('-Z','Y').to_euler()
scene.render.resolution_x=1200;scene.render.resolution_y=1000;scene.render.resolution_percentage=100
scene.name='Embercrest Textured - Finished Rig' if TEXTURED else 'Embercrest - Finished Rig'
for frame,label in [(1,'REST'),(21,'FLAP'),(41,'FOLD / LEGS'),(61,'HEAD / JAW'),(81,'TAIL'),(101,'REST')]:
    scene.timeline_markers.new(label,frame=frame)
for ob in scene.objects:
    if ob.type in {'CAMERA','LIGHT'}:ob.hide_set(True)
for win in bpy.context.window_manager.windows:
    for area in win.screen.areas:
        if area.type=='VIEW_3D':
            sp=area.spaces.active
            sp.region_3d.view_location=target
            sp.region_3d.view_distance=1.3
            sp.region_3d.view_perspective='ORTHO'
            sp.region_3d.view_rotation=cam.rotation_euler.to_quaternion()
            sp.clip_start=.001
            sp.shading.type='MATERIAL' if TEXTURED else 'SOLID'
            sp.overlay.show_floor=False
            sp.overlay.show_axis_x=False
            sp.overlay.show_axis_y=False
bpy.ops.object.select_all(action='DESELECT')
rig.select_set(True);bpy.context.view_layer.objects.active=rig
bpy.ops.object.mode_set(mode='POSE')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/(STEM+'.blend')),compress=True)
weights=[sum(g.weight for g in v.groups) for v in body.data.vertices]
stats={'source':SOURCE.name,'source_sha256':hashlib.sha256(SOURCE.read_bytes()).hexdigest(),'source_triangles':original_faces,'triangles':len(body.data.polygons),'vertices':len(body.data.vertices),'bones':len(bones),'max_influences':max(len(v.groups) for v in body.data.vertices),'max_weight_sum_error':max(abs(w-1) for w in weights),'unweighted_vertices':sum(w==0 for w in weights),'binding':'Bone heat body; continuous top-four wing field; gap-following jaw mask with hinge blend; level wing bind' if TEXTURED else 'Bone heat body; continuous top-four wing field; rigid skull/jaw; level wing bind','material':'Preserved source UVs and PBR textures' if TEXTURED else 'Neutral clay; source contains no UV or texture','glb_bytes':GLB.stat().st_size}
assert stats['max_influences']<=4 and stats['max_weight_sum_error']<1e-6
lo=[min(v.co[i] for v in body.data.vertices) for i in range(3)]
hi=[max(v.co[i] for v in body.data.vertices) for i in range(3)]
ground=(hi[2]-lo[2])*.5*19/(hi[0]-lo[0])+.02
GLB.with_suffix('.glb.flight.cfg').write_text(
    '# Selected Hunyuan: resting sole clearance at the engine 19 m wingspan.\n'
    f'ground_offset {ground:.6f}\n')
stats['ground_offset']=ground
stats['repaired_zero_tangents']=tangent_repairs
stats['source_images']=source_images
stats['bone_fit_scale']=list(fit_scale)
stats['bone_fit_bias']=list(fit_bias)
(CAP/'build_stats.json').write_text(json.dumps(stats,indent=2)+'\n')
print(json.dumps(stats),flush=True)
for frame,name in [(1,'rest'),(21,'flap'),(41,'tuck'),(61,'jaw'),(81,'tail')]:
 scene.frame_set(frame);scene.render.filepath=str(CAP/(name+'.png'));bpy.ops.render.render(write_still=True)
