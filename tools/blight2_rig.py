"""Fit Blight2 and weight each membrane panel between its two ribs."""
import bpy
import json
import math
import hashlib
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from repair_gltf_tangents import repair_tangents
from mathutils import Vector, Quaternion

ROOT = Path(__file__).resolve().parents[1]
STEM = 'blight2'
TEXTURED = True
SOURCE = ROOT / 'assets/blight2-cand-oneshot.glb'
CAP = ROOT / 'artifacts/blight2'
GLB = ROOT / 'assets/blight2-raw.glb'
TARGET = 80000
if not SOURCE.exists(): raise SystemExit(f'source mesh not found: {SOURCE}')

# The skeleton, and the bounds its coordinates were authored against.
SKEL=json.loads((ROOT/'tools/skeletons/blight2.json').read_text())
print('SKELETON',SKEL.get('name'),len(SKEL['bones']),'bones',flush=True)
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
    import bmesh
    bm=bmesh.new();bm.from_mesh(body.data)
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-7)
    # The source joins the upper and lower fangs across the mouth opening.
    # Remove their narrow bridges. Close each exposed ring with a pointed tip.
    def fang_region(co):
        x, y, z = co
        return abs(x) < .016 and .282 + .60 * (z - .334) < y < .300
    bridge = [v for v in bm.verts if .334 < v.co.z < .342 and fang_region(v.co)]
    bmesh.ops.delete(bm, geom=bridge, context='VERTS')
    rim = {v for v in bm.verts if .331 < v.co.z < .345 and fang_region(v.co) and v.is_boundary}
    while rim:
        ring = {rim.pop()}
        pending = list(ring)
        while pending:
            v = pending.pop()
            for e in v.link_edges:
                other = e.other_vert(v)
                if e.is_boundary and other in rim:
                    rim.remove(other)
                    ring.add(other)
                    pending.append(other)
        center = sum((v.co for v in ring), Vector((0, 0, 0))) / len(ring)
        direction = 1 if center.z < .338 else -1
        center += Vector((0, direction * .0010, direction * .0016))
        bmesh.ops.pointmerge(bm, verts=list(ring), merge_co=center)
    bm.to_mesh(body.data);bm.free()
mod = body.modifiers.new('Silhouette simplification', 'DECIMATE')
mod.ratio = TARGET / len(body.data.polygons)
bpy.ops.object.modifier_apply(modifier=mod.name)
# The fang repair leaves 79,999 triangles after decimation. Bisect one exposed fang edge.
# The new vertex interpolates the edge's position and texture coordinates.
if len(body.data.polygons) == TARGET - 1:
    bm = bmesh.new()
    bm.from_mesh(body.data)
    edge = max((e for e in bm.edges if e.is_boundary and fang_region(e.verts[0].co)),
               key=lambda e: e.calc_length())
    face = edge.link_faces[0]
    opposite = next(v for v in face.verts if v not in edge.verts[:])
    _, midpoint = bmesh.utils.edge_split(edge, edge.verts[0], .5)
    bmesh.utils.face_split(face, midpoint, opposite)
    bm.to_mesh(body.data)
    bm.free()
assert len(body.data.polygons) == TARGET
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

WING = SKEL['wing_field']
hub = Vector(WING['hub'])
right_bones = {name: end for name, _, end, _ in bones if name.endswith('_r')}
rib_count = sum(name.startswith('wing_finger_') and name.endswith('a_r') for name in right_bones)
ribs = [(right_bones[f'wing_finger_{i}a_r'], right_bones[f'wing_finger_{i}b_r']) for i in range(1, rib_count + 1)]
normal = Vector(WING['plane_normal']).normalized()
span = Vector(ribs[0][1]) - hub
span = (span - normal * span.dot(normal)).normalized()
forward = normal.cross(span)
def in_plane(point):
    offset = Vector(point) - hub
    return Vector((offset.dot(span), offset.dot(forward)))
rib_angles = [math.atan2(in_plane(tip).y, in_plane(tip).x) for mid, tip in ribs]
# Both haunches crossed the membrane gates. Trace the wing from its tip above the foreleg.
# The sloped shoulder cut follows the arm down to the chest.
from tide2_shoulder import limit_wing_heat
def shoulder_distance(co):
    return abs(co.x) - .070 + .50 * max(0., min(.060, co.z - .200))

wing_share = limit_wing_heat(body, shoulder_distance, .024)
for index, share_at_root in wing_share.items():
    v = body.data.vertices[index]
    co=canonical(v.co)
    x,y,z=co
    blend=(share_at_root*smoothstep(*WING['span_x'],abs(x))*smoothstep(*WING['span_z'],z)
           *(1-smoothstep(*WING['fade_y'],y)))
    if blend==0:
        continue
    suffix='l' if x<0 else 'r'
    # Each pair of ribs bounds one membrane panel. Blend across that panel.
    # Ignore surface height here so both faces carry the same fold.
    planar = in_plane((abs(x), y, z))
    radius = planar.length
    angle = math.atan2(planar.y, planar.x)
    finger_weights = {}
    if angle >= rib_angles[0]:
        finger_weights[0] = 1.0
    elif angle <= rib_angles[-1]:
        finger_weights[len(ribs) - 1] = 1.0
    else:
        for i in range(len(ribs) - 1):
            if rib_angles[i] >= angle >= rib_angles[i + 1]:
                share = (angle - rib_angles[i + 1]) / (rib_angles[i] - rib_angles[i + 1])
                finger_weights[i] = share
                finger_weights[i + 1] = 1.0 - share
                break
    field = {}
    for i, share in finger_weights.items():
        mid, tip = ribs[i]
        joint_r = in_plane(mid).length
        end_r = in_plane(tip).length
        distal = smoothstep(joint_r * .70, joint_r + .32 * (end_r - joint_r), radius)
        field[f'wing_finger_{i+1}a_{suffix}'] = share * (1 - distal)
        field[f'wing_finger_{i+1}b_{suffix}'] = share * distal
    # The arm carries the inner web. Its wrist supports the base of each rib.
    across = smoothstep(hub.x - .032, hub.x + .015, abs(x))
    across = max(across, smoothstep(.045, .09, hub.y - y) * smoothstep(.1, .16, abs(x)))
    fan = across * smoothstep(.012, .08, radius)
    field = {name: w * fan for name, w in field.items()}
    wrist = smoothstep(.095, .165, abs(x))
    elbow = smoothstep(.066, .13, abs(x))
    field['wing_wrist_' + suffix] = (1 - fan) * wrist
    field['wing_arm_' + suffix] = (1 - fan) * (1 - wrist) * elbow
    field['wing_root_' + suffix] = (1 - fan) * (1 - wrist) * (1 - elbow)
    combined={body.vertex_groups[g.group].name:g.weight*(1-blend) for g in v.groups}
    for n,w in field.items():combined[n]=combined.get(n,0)+blend*w
    final=continuous_top_four(combined)
    for gi in [g.group for g in v.groups]:body.vertex_groups[gi].remove([v.index])
    for n,w in final.items():body.vertex_groups[n].add([v.index],w,'REPLACE')
# Trim to the runtime contract and explicitly normalize.
bpy.context.view_layer.objects.active=body
bpy.ops.object.vertex_group_limit_total(limit=4)
bpy.ops.object.vertex_group_normalize_all(lock_active=False)
# The palate and mandible follow separate bones on either side of the measured gap.
JAW_PLANE = SKEL['jaw_mask']
head_group = body.vertex_groups['head']
jaw_group = body.vertex_groups['jaw']
def jaw_plane_split(y):
    pts=JAW_PLANE['line']
    i=max(0,min(len(pts)-2,sum(1 for p in pts[1:-1] if y>=p[0])))
    (ya,za),(yb,zb)=pts[i],pts[i+1]
    return za+(zb-za)*(y-ya)/(yb-ya)
# Forward of Y 0.250, the open mouth has separate upper and lower surfaces.
# Follow mesh edges to keep every tooth attached to its own jaw.
front = {v.index for v in body.data.vertices if v.co.y > .250}
adjacent = {i: [] for i in front}
for e in body.data.edges:
    a, b = e.vertices
    if a in front and b in front:
        adjacent[a].append(b)
        adjacent[b].append(a)
jaw_surface = {}
while front:
    component = [front.pop()]
    for i in component:
        for j in adjacent[i]:
            if j in front:
                front.remove(j)
                component.append(j)
    depth = sum(jaw_plane_split(body.data.vertices[i].co.y) - body.data.vertices[i].co.z for i in component)
    for i in component:
        jaw_surface[i] = float(depth > 0)
for v in body.data.vertices:
    x, y, z = canonical(v.co)
    if y < JAW_PLANE['y_min'] or z < JAW_PLANE['z_min']:
        continue
    width = .010
    share = smoothstep(-width, width, jaw_plane_split(y) - z)
    if v.index in jaw_surface:
        rigid = smoothstep(.250, .258, y)
        share += rigid * (jaw_surface[v.index] - share)
    share *= smoothstep(*JAW_PLANE['hinge_blend'], y)
    for gi in [g.group for g in v.groups]:
        body.vertex_groups[gi].remove([v.index])
    if share > 0:
        jaw_group.add([v.index], share, 'REPLACE')
    if share < 1:
        head_group.add([v.index], 1 - share, 'REPLACE')
# Normalize explicitly after edits; vertex-group collection elements are live.
for v in body.data.vertices:
    entries=[(g.group,g.weight) for g in v.groups if g.weight>0]
    total=sum(w for _,w in entries)
    if total<=0:raise RuntimeError(f'Unweighted vertex {v.index}')
    for gi,w in entries:body.vertex_groups[gi].add([v.index],w/total,'REPLACE')
# Unfold the shoulder, then level the hand about its own wrist.
# These two rotations preserve the sculpted bone lengths.
def rotate_local(name, rotation):
    bone = rig.pose.bones[name]
    basis = bone.bone.matrix_local.to_quaternion()
    bone.rotation_quaternion = basis.inverted() @ rotation @ basis

for sign, suffix in [(-1, 'l'), (1, 'r')]:
    shoulder = Quaternion((0, 0, 1), math.radians(sign * 21)) @ Quaternion((0, 1, 0), math.radians(sign * SKEL['wing_bind_level_deg']))
    rotate_local('wing_root_' + suffix, shoulder)
    surface = Vector((sign * normal.x, normal.y, normal.z))
    level = surface.rotation_difference(Vector((0, 0, 1)))
    leading = level @ Vector((sign * span.x, span.y, span.z))
    sweep = math.atan2(leading.y, sign * leading.x)
    hand = Quaternion((0, 0, 1), -sign * sweep) @ level
    rotate_local('wing_wrist_' + suffix, shoulder.inverted() @ hand)
bpy.context.view_layer.update()
bpy.ops.object.select_all(action='DESELECT');body.select_set(True)
bpy.context.view_layer.objects.active=body
skin=next(m for m in body.modifiers if m.type=='ARMATURE')
bpy.ops.object.modifier_apply(modifier=skin.name)
body.select_set(False);rig.select_set(True);bpy.context.view_layer.objects.active=rig
bpy.ops.object.mode_set(mode='POSE');bpy.ops.pose.armature_apply(selected=False)
bpy.ops.object.mode_set(mode='OBJECT')
skin=body.modifiers.new('Embercrest skin','ARMATURE');skin.object=rig
for material in body.data.materials:
    material.use_backface_culling = False
for image in bpy.data.images:
    if image.type == 'IMAGE' and image.has_data:
        image.pack()
rig['source']=SOURCE.name
rig['controls']='FK deform bones in body space. +Y forward, +Z up.'
body['simplification']='Collapse decimation; not quad retopology. Original source unchanged.'
bpy.context.view_layer.update()
bpy.ops.object.select_all(action='DESELECT');body.select_set(True);rig.select_set(True);bpy.context.view_layer.objects.active=rig
bpy.ops.export_scene.gltf(filepath=str(GLB),export_format='GLB',use_selection=True,export_animations=False,export_skins=True,export_def_bones=True,export_yup=True,export_tangents=True)
tangent_repairs=repair_tangents(GLB)
weights=[sum(g.weight for g in v.groups) for v in body.data.vertices]
stats = {
    'source': SOURCE.name,
    'source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
    'source_triangles': original_faces,
    'triangles': len(body.data.polygons),
    'vertices': len(body.data.vertices),
    'bones': len(bones),
    'max_influences': max(len(v.groups) for v in body.data.vertices),
    'max_weight_sum_error': max(abs(w-1) for w in weights),
    'unweighted_vertices': sum(w == 0 for w in weights),
    'binding': 'Bone heat body; adjacent rib interpolation; separated fang tips and surface-weighted jaw; unfolded shoulder and leveled hand',
    'material': 'Preserved source UVs and PBR textures',
    'glb_bytes': GLB.stat().st_size,
}
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
