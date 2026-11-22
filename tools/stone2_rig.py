"""Bind a measured, swept-wing dragon. See artifacts/stone2/README.md for the command."""
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
parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--stem', required=True)
parser.add_argument('--input', required=True)
parser.add_argument('--skeleton', required=True)
parser.add_argument('--target', type=int, default=80000)
parser.add_argument('--keep-uvs', action='store_true', required=True)
args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
TEXTURED = args.keep_uvs
STEM = args.stem
SOURCE = ROOT / args.input
CAP = ROOT / 'artifacts' / STEM
GLB = ROOT / 'assets' / (STEM + '-raw.glb')
TARGET = args.target
if not SOURCE.exists(): raise SystemExit(f'source mesh not found: {SOURCE}')

# The skeleton, and the bounds its coordinates were authored against.
SKEL=json.loads((ROOT/args.skeleton).read_text())
print('SKELETON',args.skeleton,SKEL.get('name'),len(SKEL['bones']),'bones',flush=True)
CAP.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.gltf(filepath=str(SOURCE),merge_vertices=True)
body = next(o for o in bpy.context.selected_objects if o.type == 'MESH')
body.name = STEM.replace('-','_')
bpy.context.view_layer.objects.active = body
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
original_faces = len(body.data.polygons)
# The source faces -Y. Author the rig in +Y so glTF faces -Z.
body.rotation_mode = 'XYZ'
body.rotation_euler.z = math.pi
bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
# The texture export has slightly different bounds. Fit bones/weight landmarks
# to those bounds rather than rescaling the mesh or changing its authored UVs.
reference_lo=Vector(tuple(SKEL['reference_bounds']['lo']))
reference_hi=Vector(tuple(SKEL['reference_bounds']['hi']))
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
    # Join vertices with identical positions. The UVs stay on the face corners.
    import bmesh
    import numpy as np
    coordinates = np.empty(3 * len(body.data.vertices), dtype=np.float32)
    body.data.vertices.foreach_get('co', coordinates)
    _, first, inverse = np.unique(coordinates.reshape(-1, 3), axis=0,
                                  return_index=True, return_inverse=True)
    target = first[inverse]
    duplicates = np.flatnonzero(target != np.arange(len(target)))
    if len(duplicates):
        bm = bmesh.new()
        bm.from_mesh(body.data)
        bm.verts.ensure_lookup_table()
        weld = {bm.verts[int(i)]: bm.verts[int(target[i])] for i in duplicates}
        bmesh.ops.weld_verts(bm, targetmap=weld)
        bm.to_mesh(body.data)
        bm.free()
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
for name, a, b, parent in SKEL['bones']:
    # jaw_tip exists only where the runtime probes a descendant to find the
    # jaw's opening direction; a leaf jaw probes its own unmoving pivot.
    if name == 'jaw_tip' and SKEL.get('jaw_tip_requires_textured') and not TEXTURED:
        continue
    bone(name, a, b, parent)
print('BONES', len(bones), flush=True)

armdata=bpy.data.armatures.new(STEM+' skeleton')
rig=bpy.data.objects.new(STEM.replace('-','_')+'_Rig',armdata)
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

# Anatomy-specific, and therefore optional. A skeleton with no wing* bones
# skips this entirely rather than feeding an empty candidate set into
# continuous_top_four, which would index ordered[0] on an empty list.
WING=SKEL.get('wing_field')
WING_FIELD_RAN=bool(any(wing_bones.values()) and WING)
if not any(wing_bones.values()) or WING is None:
    print('WING_FIELD_SKIPPED',
          'no wing* bones' if not any(wing_bones.values()) else 'no wing_field in skeleton',
          flush=True)
# The membrane selection excluded the crystals. The heat bind still gave them wing-root weight.
# At |X| 0.080, the connected wing clears the crystals and forelegs on both sides.
from tide2_shoulder import limit_wing_heat
# Keep the sculpt's shoulder shape when leveling its raised wing for the bind pose.
# The original heat describes that rest shape inside the socket.
socket_heat = {}
upper_wing = {side: {body.vertex_groups[prefix + side].index for prefix in ('wing_root_', 'wing_arm_')}
              for side in ('l', 'r')}
for v in body.data.vertices:
    x, y, z = v.co
    radius = math.sqrt(((abs(x) - .076) / .076)**2 + ((y - .122) / .067)**2 + ((z - .244) / .086)**2)
    if radius >= 1.:
        continue
    side = 'l' if x < 0 else 'r'
    heat = sum(g.weight for g in v.groups if g.group in upper_wing[side])
    if heat:
        socket_heat[v.index] = (side, heat, 1. - smoothstep(.45, 1., radius))
# Spread the bend along the upper wing. A short transition pinched the web on the downstroke.
wing_share = limit_wing_heat(body, lambda co: abs(co.x) - .080, .064)
for index, share_at_root in wing_share.items():
    v = body.data.vertices[index]
    co=canonical(v.co)
    x,y,z=co
    blend=(share_at_root*smoothstep(*WING['span_x'],abs(x))*smoothstep(*WING['span_z'],z)
           *(1-smoothstep(*WING['fade_y'],y)))
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
    hub=Vector(((-1 if x<0 else 1)*WING['hub'][0],WING['hub'][1],WING['hub'][2]))
    wrist_share=1-smoothstep(*WING['wrist_share'],(co-hub).length)
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
# The mask below is authored in Embercrest canonical coordinates, so a skeleton
# for another head opts out with "jaw_mask": null and keeps the heat weights.
JAW_MASK=SKEL.get('jaw_mask','embercrest')
if JAW_MASK is None:print('JAW_MASK_SKIPPED','jaw_mask is null',flush=True)
# A sealed-mouth sculpt gives heat nothing to split on, so the mandible ends
# up half head, half jaw and the gape is a muzzle bend. The generic form is a
# plane in canonical coordinates, authored per skeleton:
#   "jaw_mask": {"type": "plane", "x_max": .035, "y_min": .25,
#                "line": [[y0, z0], [y1, z1]], "hinge_blend": [ya, yb]}
# Below the (y,z) line within the box is jaw, blended in over hinge_blend.
# Three optional keys, each added because a measured head needed it:
#   "line" may hold more than two points (ascending y): the split is then
#     piecewise linear through them, extrapolating the end segments. A real
#     mouth line is convex -- shallow behind the teeth, steep at the chin --
#     and Rimefang's has no straight line that clears both the rear tongue
#     and the front palate.
#   "z_min": a floor on the box. A head carried over the chest (Rimefang)
#     puts the forefeet inside a box that only bounds x and y.
#   "reclaim_above": true moves jaw/jaw_tip weight ABOVE the line onto head.
#     Without it the plane only adds jaw below the line, and a heat bind that
#     gave the whole snout to the jaw bone (Ironroot, Rimefang: the mouth
#     opened by lifting the nose) keeps it. Opt-in so Ashcoil's build, the
#     first plane mask, reproduces unchanged.
JAW_PLANE=JAW_MASK if isinstance(JAW_MASK,dict) and JAW_MASK.get('type')=='plane' else None
if JAW_PLANE:print('JAW_MASK_PLANE',json.dumps(JAW_PLANE),flush=True)
head_group=body.vertex_groups.get('head');jaw_group=body.vertex_groups.get('jaw')
if (JAW_MASK=='embercrest' or JAW_PLANE) and (head_group is None or jaw_group is None):
    raise RuntimeError('jaw_mask "embercrest" needs head and jaw bones; set "jaw_mask": null for a skeleton without them')
for v in (body.data.vertices if JAW_MASK=='embercrest' else []):
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
def jaw_plane_split(y):
    pts=JAW_PLANE['line']
    i=max(0,min(len(pts)-2,sum(1 for p in pts[1:-1] if y>=p[0])))
    (ya,za),(yb,zb)=pts[i],pts[i+1]
    return za+(zb-za)*(y-ya)/(yb-ya)
from stone2_jaw import lower_surface
mouth_components = lower_surface(body.data, JAW_PLANE['separate_y'], jaw_plane_split)
JAW_PLANE_RECLAIMED=0
for v in (body.data.vertices if JAW_PLANE else []):
    x,y,z=canonical(v.co)
    if abs(x)>JAW_PLANE['x_max'] or y<JAW_PLANE['y_min'] or z<JAW_PLANE.get('z_min',-1e9):continue
    split=jaw_plane_split(y)
    if not mouth_components.get(v.index, z < split):
        if JAW_PLANE.get('reclaim_above'):
            moved=0.
            for gi,w in [(g.group,g.weight) for g in v.groups]:
                if body.vertex_groups[gi].name in ('jaw','jaw_tip') and w>0:
                    moved+=w;body.vertex_groups[gi].remove([v.index])
            if moved>0:head_group.add([v.index],moved,'ADD');JAW_PLANE_RECLAIMED+=1
        continue
    share=smoothstep(*JAW_PLANE['hinge_blend'],y)
    if share<=0:continue
    for gi in [g.group for g in v.groups]:body.vertex_groups[gi].remove([v.index])
    jaw_group.add([v.index],share,'REPLACE')
    if share<1:head_group.add([v.index],1-share,'REPLACE')
if JAW_PLANE:print('JAW_MASK_PLANE_RECLAIMED',JAW_PLANE_RECLAIMED,'vertices above the line had jaw weight moved to head',flush=True)
# Normalize explicitly after edits; vertex-group collection elements are live.
for v in body.data.vertices:
    entries=[(g.group,g.weight) for g in v.groups if g.weight>0]
    total=sum(w for _,w in entries)
    if total<=0:raise RuntimeError(f'Unweighted vertex {v.index}')
    for gi,w in entries:body.vertex_groups[gi].add([v.index],w/total,'REPLACE')
# Remove the sculpt's raised shoulder angle from the runtime bind pose.
# Counter-rotate the wrist so the outer membrane remains spread and level.
# Bake mesh and skeleton together; no scale or pose correction is needed in game.
# The angle is per skeleton ("wing_bind_level_deg", default 35, the Embercrest
# value); a skeleton without wing_root/wing_wrist bones skips the pass.
for pb in rig.pose.bones:pb.rotation_mode='QUATERNION'
WING_LEVEL_DEG=SKEL.get('wing_bind_level_deg',35)
WING_LEVELLED=False
for sign,suffix in [(-1,'l'),(1,'r')]:
    for prefix,degrees in [('wing_root_',WING_LEVEL_DEG),('wing_wrist_',-WING_LEVEL_DEG)]:
        if prefix+suffix not in rig.pose.bones:continue
        pb=rig.pose.bones[prefix+suffix]
        axis=pb.bone.matrix_local.to_quaternion().inverted()@Vector((0,1,0))
        pb.rotation_quaternion=Quaternion(axis,math.radians(sign*degrees))
        WING_LEVELLED=True
# Open the swept hand about body up while the wing still has its source length.
for sign, suffix in [(-1, 'l'), (1, 'r')]:
    pb = rig.pose.bones['wing_wrist_' + suffix]
    up = pb.bone.matrix_local.to_quaternion().inverted() @ Vector((0, 0, 1))
    pb.rotation_quaternion = pb.rotation_quaternion @ Quaternion(up, math.radians(sign * SKEL['wing_bind_spread_deg']))
if not WING_LEVELLED:print('WING_LEVEL_SKIPPED','no wing_root/wing_wrist bones',flush=True)
bpy.context.view_layer.update()
bpy.ops.object.select_all(action='DESELECT');body.select_set(True)
bpy.context.view_layer.objects.active=body
skin=next(m for m in body.modifiers if m.type=='ARMATURE')
# Bake the missing root rotation once. The crystals keep their body weights during flight.
socket_offset = {}
root_rotations = {}
for side in upper_wing:
    root = rig.pose.bones['wing_root_' + side]
    root_rotations[side] = root.matrix @ root.bone.matrix_local.inverted()
for index, (side, heat, falloff) in socket_heat.items():
    v = body.data.vertices[index]
    current = sum(g.weight for g in v.groups if g.group in upper_wing[side])
    if current < heat:
        socket_offset[index] = (root_rotations[side] @ v.co - v.co) * ((heat - current) * falloff)
bpy.ops.object.modifier_apply(modifier=skin.name)
for index, offset in socket_offset.items():
    body.data.vertices[index].co += offset
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
rig['controls']='Body-space rotation axes. Forward is +Y. Up is +Z.'
body['simplification']='Collapse decimation; not quad retopology. Original source unchanged.'
bpy.ops.object.select_all(action='DESELECT')
body.select_set(True)
rig.select_set(True)
bpy.context.view_layer.objects.active = rig
bpy.ops.export_scene.gltf(filepath=str(GLB),export_format='GLB',use_selection=True,export_animations=False,export_skins=True,export_def_bones=True,export_yup=True,export_tangents=True)
tangent_repairs=repair_tangents(GLB)
weights=[sum(g.weight for g in v.groups) for v in body.data.vertices]
stats={'source':SOURCE.name,'source_sha256':hashlib.sha256(SOURCE.read_bytes()).hexdigest(),'source_triangles':original_faces,'triangles':len(body.data.polygons),'vertices':len(body.data.vertices),'bones':len(bones),'max_influences':max(len(v.groups) for v in body.data.vertices),'max_weight_sum_error':max(abs(w-1) for w in weights),'unweighted_vertices':sum(w==0 for w in weights),'binding':'; '.join(['Bone heat body']
    +(['continuous top-four wing field'] if WING_FIELD_RAN else [])
    +([('gap-following jaw mask with hinge blend' if TEXTURED else 'rigid skull/jaw')] if JAW_MASK=='embercrest' else [('plane jaw mask with hinge blend'+(f', {len(JAW_PLANE["line"])}-point line' if len(JAW_PLANE['line'])>2 else '')+(', snout reclaimed' if JAW_PLANE.get('reclaim_above') else ''))] if JAW_PLANE else ['heat-weight jaw (jaw_mask null)'])
    +([('level wing bind' if WING_LEVEL_DEG==35 else f'level wing bind {WING_LEVEL_DEG} deg')] if WING_LEVELLED else [])),'material':'Preserved source UVs and PBR textures' if TEXTURED else 'Neutral clay; source contains no UV or texture','glb_bytes':GLB.stat().st_size}
assert stats['max_influences']<=4 and stats['max_weight_sum_error']<1e-6
lo=[min(v.co[i] for v in body.data.vertices) for i in range(3)]
hi=[max(v.co[i] for v in body.data.vertices) for i in range(3)]
# The engine scales a winged asset by its X extent to a 19 m wingspan. A
# wingless body has no wingspan, so the skeleton says which extent the
# engine size refers to: "reference_span": {"axis": "y", "metres": 12}.
# Default is the Embercrest assumption, X = 19 m.
SPAN=SKEL.get('reference_span',{'axis':'x','metres':19})
span_axis='xyz'.index(SPAN['axis'])
ground=(hi[2]-lo[2])*.5*SPAN['metres']/(hi[span_axis]-lo[span_axis])+.02
stats['reference_span']=SPAN
stats['ground_offset']=ground
stats['repaired_zero_tangents']=tangent_repairs
stats['source_images']=source_images
stats['bone_fit_scale']=list(fit_scale)
stats['bone_fit_bias']=list(fit_bias)
(CAP/'build_stats.json').write_text(json.dumps(stats,indent=2)+'\n')
print(json.dumps(stats),flush=True)
