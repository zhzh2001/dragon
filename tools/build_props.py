#!/usr/bin/env python3
"""Build the two metre-scale Dragon Engine props with Blender, then verify GLBs.

blender -b --factory-startup --python tools/build_props.py
Optional script arguments after --: --verify-only, --render, --output-dir PATH.
Only generated geometry and a deterministic, painted-in-code PNG atlas are used.
"""
import argparse
import json
import math
import os
from pathlib import Path
import random
import struct
import sys
import traceback

import bpy
import bmesh
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[1]
TAU = math.tau
IDENTITY = [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.]


def clear():
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    for collection in (bpy.data.meshes, bpy.data.armatures, bpy.data.materials,
                       bpy.data.images, bpy.data.cameras, bpy.data.lights):
        for item in list(collection):
            if not item.users:
                collection.remove(item)
    bpy.context.scene.unit_settings.system = 'METRIC'
    bpy.context.scene.unit_settings.scale_length = 1.0


def atlas(name):
    """16 padded atlas cells: masonry, iron, gold, timber and jewel colors."""
    palette = [(0.43,.38,.30), (.49,.43,.34), (.37,.34,.29), (.55,.48,.37),
               (.13,.15,.17), (.09,.065,.035), (.82,.49,.09), (.98,.71,.20),
               (.27,.105,.035), (.68,.025,.045), (.025,.46,.27), (.025,.24,.64),
               (.63,.33,.045), (.96,.65,.13), (.68,.40,.07), (.23,.25,.24)]
    rng = random.Random(712)
    pixels = []
    # A regular jittered coin field with dark seams; no input images or baking.
    for y in range(512):
        for x in range(512):
            tile = (y//128)*4 + x//128
            u,v = (x%128+.5)/128, (y%128+.5)/128
            noise = rng.uniform(-.045,.045)
            base = palette[tile]
            factor = 1 + noise
            edge = min(u,v,1-u,1-v)
            if tile < 4:
                factor *= .70 + .30*min(1,edge*24)
                factor += .018*math.sin(u*153+math.sin(v*113))
            elif tile == 8:
                factor *= .77+.20*math.sin(u*80+math.sin(v*6))**2
            elif tile in (6,13,14):
                radius = math.hypot(u-.5,v-.5)
                if .34 < radius < .40 or radius < .035:
                    factor *= .69
                elif .40 < radius < .46:
                    factor *= 1.12
            elif tile == 12:
                row = int(v*8)
                xx = (u*8 + .5*(row%2))%1-.5
                yy = (v*8)%1-.5
                radius=math.hypot(xx,yy)
                factor = .35 if radius > .46 else 1.0 + .28*(yy+.5)
                if .32 < radius < .39: factor *= .70
                factor += noise
            pixels.extend([max(0,min(1,c*factor)) for c in base]+[1])
    image = bpy.data.images.new(name+'_basecolor', width=512, height=512, alpha=True)
    image.pixels.foreach_set(pixels)
    image.file_format = 'PNG'
    image.pack()
    mat = bpy.data.materials.new(name+'_atlas')
    mat.use_nodes = True
    shader = mat.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Roughness'].default_value = .72 if name=='watchtower' else .40
    shader.inputs['Metallic'].default_value = 0.0 if name=='watchtower' else .45
    tex=mat.node_tree.nodes.new('ShaderNodeTexImage'); tex.image=image
    mat.node_tree.links.new(tex.outputs['Color'],shader.inputs['Base Color'])
    return mat


class Geometry:
    def __init__(self):
        self.vertices=[]; self.faces=[]; self.tiles=[]

    def add(self, vertices, faces, tile, transform=None):
        offset=len(self.vertices)
        self.vertices.extend(tuple(transform @ Vector(v)) if transform else tuple(v) for v in vertices)
        self.faces.extend(tuple(offset+i for i in face) for face in faces)
        self.tiles.extend([tile]*len(faces))

    def box(self, center, size, tile=0, bevel=0, angle=0):
        bm=bmesh.new()
        bmesh.ops.create_cube(bm,size=1)
        for v in bm.verts:
            v.co.x*=size[0]; v.co.y*=size[1]; v.co.z*=size[2]
        if bevel:
            bmesh.ops.bevel(bm,geom=list(bm.edges),offset=bevel,segments=1,affect='EDGES')
        bm.verts.ensure_lookup_table(); bm.verts.index_update()
        transform=Matrix.Translation(Vector(center)) @ Matrix.Rotation(angle,4,'Z')
        self.add([v.co for v in bm.verts],[[v.index for v in f.verts] for f in bm.faces],tile,transform)
        bm.free()

    def lathe(self, center, profile, tile, n=16, transform=None, cap=True):
        vs=[(r*math.cos(i*TAU/n),r*math.sin(i*TAU/n),z) for r,z in profile for i in range(n)]
        fs=[]
        for k in range(len(profile)-1):
            for i in range(n):
                j=(i+1)%n
                fs.append((k*n+i,k*n+j,(k+1)*n+j,(k+1)*n+i))
        if cap:
            fs += [tuple(reversed(range(n))),tuple((len(profile)-1)*n+i for i in range(n))]
        mat=Matrix.Translation(Vector(center)) @ (transform or Matrix.Identity(4))
        self.add(vs,fs,tile,mat)

    def mesh(self, name, material):
        mesh=bpy.data.meshes.new(name+'_mesh')
        mesh.from_pydata(self.vertices,[],self.faces); mesh.update()
        obj=bpy.data.objects.new(name,mesh); bpy.context.collection.objects.link(obj)
        mesh.materials.append(material)
        uv=mesh.uv_layers.new(name='UVMap')
        # Each polygon stays within one padded atlas tile, including bevels.
        for poly,tile in zip(mesh.polygons,self.tiles):
            axis=max(range(3),key=lambda a:abs(poly.normal[a]))
            axes=[a for a in range(3) if a!=axis]
            coords=[mesh.vertices[mesh.loops[i].vertex_index].co for i in poly.loop_indices]
            bounds=[(min(v[a] for v in coords),max(v[a] for v in coords)) for a in axes]
            for loop,co in zip(poly.loop_indices,coords):
                local=[(co[a]-low)/max(high-low,1e-7) for a,(low,high) in zip(axes,bounds)]
                uv.data[loop].uv=((tile%4+.035+.93*local[0])/4,
                                  (tile//4+.035+.93*local[1])/4)
        bpy.context.view_layer.objects.active=obj; obj.select_set(True)
        triangulate=obj.modifiers.new('Applied triangulation','TRIANGULATE')
        bpy.ops.object.modifier_apply(modifier=triangulate.name)
        return obj


def watchtower():
    g=Geometry(); rng=random.Random(204)
    # Foundation deliberately reaches exactly x/y +/-2.5 and z=0.
    g.box((0,0,.25),(5,5,.5),2,.07)
    g.box((0,0,6.35),(4.1,4.1,11.7),2)
    # Eight individually beveled ashlar blocks per course.
    for course in range(10):
        z=.5+(course+.5)*1.19
        half=2.43-.31*(course+.5)/10
        for side in range(4):
            extent=half if side%2==0 else half-.60
            split=.34 if course%2 else -.34
            for start,end in ((-extent,split),(split,extent)):
                along=(start+end)/2
                p=Vector((along, -half+.30, z))
                rot=Matrix.Rotation(side*math.pi/2,4,'Z')
                p=rot@p
                size=(end-start-.035,.60,1.15)
                if side%2: size=(size[1],size[0],size[2])
                g.box(p,size,rng.randrange(4),.055)
    # Contrasting front doorway and recessed arrow-slit surfaces face -Y -> +Z.
    g.box((0,-2.45,1.85),(1.1,.035,2.45),5)
    for z in (5.1,8.5,11.2):
        surface=-(2.43-.31*((z-.5)/11.9))-.045
        g.box((0,surface,z),(.28,.05,1.35),5)
    g.box((0,0,12.55),(4.7,4.7,.42),1,.06)
    # Four-sided corbel support, parapet walkway, open central deck.
    for side in range(4):
        rot=Matrix.Rotation(side*math.pi/2,4,'Z')
        for t in (-1.65,0,1.65):
            g.box(rot@Vector((t,-2.38,12.92)),(.48,.62,.64),2,.05,side*math.pi/2)
    g.box((0,0,13.25),(5.8,5.8,.5),1,.07)
    for side in range(4):
        rot=Matrix.Rotation(side*math.pi/2,4,'Z')
        length=5.8 if side%2==0 else 4.7
        g.box(rot@Vector((0,-2.625,13.94)),(length,.55,.88),0,.05,side*math.pi/2)
        g.box(rot@Vector((0,-2.61,14.79)),(.99,.64,.87),1,.05,side*math.pi/2)
    for x in (-2.4,2.4):
        for y in (-2.4,2.4):
            g.box((x,y,14.79),(1.0,1.0,.87),1,.05)
    g.box((0,0,14.19),(1.65,1.65,1.38),2,.10)
    g.lathe((0,0,0),[(.60,14.88),(.65,15.12),(1.23,15.68),(1.38,15.88),
                              (1.38,16.0),(1.18,16.0),(.98,15.66),(.48,15.30)],4,24)
    return g


def mound_height(radius):
    return 2.80*max(0,1-(radius/5.5)**1.48)


def hoard_pile():
    g=Geometry(); rng=random.Random(901)
    n=48; rings=8
    vs=[(0,0,2.8)]; fs=[]
    for ring in range(1,rings+1):
        r=5.5*ring/rings
        for i in range(n):
            a=i*TAU/n
            # Outer ring perfectly centered; irregularity is inside silhouette.
            z=mound_height(r)+(rng.uniform(-.11,.11) if ring<rings else 0)
            vs.append((r*math.cos(a),r*math.sin(a),z))
    for i in range(n): fs.append((0,1+i,1+(i+1)%n))
    for ring in range(rings-1):
        a=1+ring*n;b=a+n
        for i in range(n):
            j=(i+1)%n;fs.append((a+i,b+i,b+j,a+j))
    fs.append(tuple(reversed(range(1+(rings-1)*n,1+rings*n))))
    g.add(vs,fs,12)
    # Coins span the whole mound and scatter onto the lower skirt.
    for i in range(176):
        a=rng.uniform(0,TAU); r=5.05*math.sqrt(rng.random())
        x,y=r*math.cos(a),r*math.sin(a)
        radius=rng.uniform(.16,.31)
        z=max(.055,mound_height(r)+rng.uniform(.03,.12))
        tilt=Matrix.Rotation(rng.uniform(-.22,.22),4,'X') @ Matrix.Rotation(rng.uniform(-.22,.22),4,'Y')
        g.lathe((x,y,z),[(radius,0),(radius,.075)],rng.choice((6,7,13,14)),8,tilt)
    # A raised, half-buried domed chest lid, pointing toward Blender -Y.
    cx,cy=0,-.60;cz=2.72
    g.box((cx,cy,cz+.12),(2.12,1.24,.33),8,.035,angle=.12)
    # Eight staves follow the arched crown in cross section.
    for i in range(8):
        theta=-math.pi/2+(i+.5)*math.pi/8
        x=math.sin(theta)*1.02;z=math.cos(theta)*.67
        transform=Matrix.Rotation(.12,4,'Z')
        center=transform@Vector((x,0,0))+Vector((cx,cy,cz+.25+z))
        # Separate timber panels with chunky gold binding hoops below.
        g.box(center,(.39,1.18,.14),8,.018,.12)
    for y in (-.46,.46):
        points=[]
        for i in range(13):
            a=-math.pi/2+i*math.pi/12
            for off in (-.085,.085):
                points.append((math.sin(a)*1.065,y+off,.25+math.cos(a)*.73))
        faces=[(i*2,i*2+1,i*2+3,i*2+2) for i in range(12)]
        g.add(points,faces,7,Matrix.Translation(Vector((cx,cy,cz)))@Matrix.Rotation(.12,4,'Z'))
    g.box((cx,cy-.655,cz+.2),(.28,.12,.33),7,.025)
    # Oversized ceremonial cups: foot, stem, open bowl and thick lip.
    for x,y,s in ((2.45,-1.05,1.0),(-2.15,-1.65,.85),(1.55,2.0,.72)):
        z=mound_height(math.hypot(x,y))+.02
        profile=[(.42,0),(.42,.09),(.15,.16),(.11,.48),(.31,.56),(.48,.98),
                 (.49,1.10),(.40,1.10),(.33,.93),(.19,.65)]
        g.lathe((x,y,z),[(r*s,h*s) for r,h in profile],7,12)
    # Gems catch light with genuinely faceted crown geometry.
    for x,y,scale,tile in ((-2.7,.4,.48,9),(2.7,1.3,.40,10),(-.9,-3.0,.48,11),
                           (3.9,-1.7,.30,9),(-3.5,-2.2,.30,10),(.6,3.4,.32,11)):
        z=mound_height(math.hypot(x,y))+.10
        g.lathe((x,y,z),[(.4*scale,0),(scale,.16*scale),(.52*scale,.8*scale)],tile,6)
    return g


def skin(obj):
    arm=bpy.data.armatures.new('Prop_root_armature')
    rig=bpy.data.objects.new('Prop_armature',arm); bpy.context.collection.objects.link(rig)
    bpy.ops.object.select_all(action='DESELECT');rig.select_set(True)
    bpy.context.view_layer.objects.active=rig
    bpy.ops.object.mode_set(mode='EDIT')
    bone=arm.edit_bones.new('root');bone.head=(0,0,0);bone.tail=(0,1,0)
    bpy.ops.object.mode_set(mode='OBJECT')
    assert all(abs(arm.bones[0].matrix_local[r][c]-(1 if r==c else 0))<1e-6 for r in range(4) for c in range(4))
    group=obj.vertex_groups.new(name='root');group.add(list(range(len(obj.data.vertices))),1.0,'REPLACE')
    obj.parent=rig
    # This modifier is the skin declaration, not an unapplied geometric edit.
    modifier=obj.modifiers.new('root skin binding','ARMATURE');modifier.object=rig
    return rig


def read_glb(path):
    data=Path(path).read_bytes()
    magic,version,length=struct.unpack_from('<III',data)
    assert magic==0x46546c67 and version==2 and length==len(data),'Invalid GLB'
    jl,jt=struct.unpack_from('<II',data,12)
    assert jt==0x4e4f534a
    doc=json.loads(data[20:20+jl]);off=20+jl
    bl,bt=struct.unpack_from('<II',data,off);assert bt==0x004e4942
    return doc,bytearray(data[off+8:off+8+bl])


def write_glb(path,doc,blob):
    encoded=json.dumps(doc,separators=(',',':')).encode()
    encoded+=b' '*((-len(encoded))%4);blob+=b'\0'*((-len(blob))%4)
    data=struct.pack('<III',0x46546c67,2,28+len(encoded)+len(blob))
    data+=struct.pack('<II',len(encoded),0x4e4f534a)+encoded
    data+=struct.pack('<II',len(blob),0x004e4942)+blob
    Path(path).write_bytes(data)


def accessor(doc,blob,index):
    acc=doc['accessors'][index];view=doc['bufferViews'][acc['bufferView']]
    fmt={5120:'b',5121:'B',5122:'h',5123:'H',5125:'I',5126:'f'}[acc['componentType']]
    width={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[acc['type']]
    size=struct.calcsize('<'+fmt*width);stride=view.get('byteStride',size)
    start=view.get('byteOffset',0)+acc.get('byteOffset',0)
    assert not acc.get('sparse'),'Sparse accessor unsupported'
    return [struct.unpack_from('<'+fmt*width,blob,start+i*stride) for i in range(acc['count'])]


def canonicalize_root(path):
    """Cancel exporter coordinate-frame rotation on this single static joint.

    Blender's +Y rest bone is identity in Blender, but the exporter emits a
    -90 X root rotation and its inverse bind. Their product is identity.
    Replacing BOTH with identity preserves every vertex and makes the engine
    root identity in glTF too; mesh coordinates already received Y-up conversion.
    """
    doc,blob=read_glb(path);sk=doc['skins'][0]
    root=doc['nodes'][sk['joints'][0]]
    root.pop('rotation',None);root.pop('translation',None);root.pop('scale',None);root.pop('matrix',None)
    acc=doc['accessors'][sk['inverseBindMatrices']];view=doc['bufferViews'][acc['bufferView']]
    struct.pack_into('<16f',blob,view.get('byteOffset',0)+acc.get('byteOffset',0),*IDENTITY)
    write_glb(path,doc,blob)


def identity_node(node):
    return (all(abs(a-b)<1e-6 for a,b in zip(node.get('matrix',IDENTITY),IDENTITY))
            and all(abs(x)<1e-6 for x in node.get('translation',[0,0,0]))
            and all(abs(a-b)<1e-6 for a,b in zip(node.get('rotation',[0,0,0,1]),[0,0,0,1]))
            and all(abs(x-1)<1e-6 for x in node.get('scale',[1,1,1])))


def verify(path,name):
    doc,blob=read_glb(path)
    assert len(doc.get('meshes',[]))==1,'Expected one mesh'
    assert len(doc.get('skins',[]))==1,'Expected one skin'
    assert not doc.get('animations'),'Animations forbidden'
    assert len(doc.get('nodes',[]))==3,'Only mesh, armature and its one joint permitted'
    assert all(identity_node(n) for n in doc['nodes']),'Nonidentity exported transform'
    skin_doc=doc['skins'][0];assert len(skin_doc['joints'])==1,'Expected exactly one bone'
    joint=skin_doc['joints'][0];assert doc['nodes'][joint]['name']=='root'
    assert all(abs(a-b)<1e-6 for a,b in zip(accessor(doc,blob,skin_doc['inverseBindMatrices'])[0],IDENTITY)),'Nonidentity root bind'
    mesh_nodes=[n for n in doc['nodes'] if 'mesh' in n]
    assert len(mesh_nodes)==1 and mesh_nodes[0].get('mesh')==0 and mesh_nodes[0].get('skin')==0,'Mesh is not skinned'
    mesh_index=doc['nodes'].index(mesh_nodes[0])
    arm_index=next(i for i in range(3) if i not in (joint,mesh_index))
    assert set(doc['nodes'][arm_index].get('children',[]))=={joint,mesh_index},'Unexpected hierarchy'
    assert not doc['nodes'][joint].get('children') and not mesh_nodes[0].get('children')
    assert doc['scenes'][doc.get('scene',0)]['nodes']==[arm_index]
    assert not doc.get('cameras') and not doc.get('extensions',{}).get('KHR_lights_punctual')
    primitives=doc['meshes'][0]['primitives'];assert len(primitives)==1,'Expected one material primitive'
    prim=primitives[0];assert prim.get('mode',4)==4 and not prim.get('targets'),'Triangles only, no morphs'
    attrs=prim['attributes'];assert 'COLOR_0' not in attrs
    positions=accessor(doc,blob,attrs['POSITION'])
    assert all(math.isfinite(c) for p in positions for c in p)
    uvs=accessor(doc,blob,attrs['TEXCOORD_0'])
    assert len(uvs)==len(positions) and all(math.isfinite(c) and 0<=c<=1 for uv in uvs for c in uv),'Invalid UVs'
    weights=accessor(doc,blob,attrs['WEIGHTS_0']);joints=accessor(doc,blob,attrs['JOINTS_0'])
    assert len(weights)==len(positions)==len(joints)
    assert all(abs(w[0]-1)<1e-6 and all(abs(v)<1e-6 for v in w[1:]) for w in weights),'Weights must be root=1'
    assert all(j[0]==0 for j in joints),'Wrong joint'
    idx=accessor(doc,blob,prim['indices']);assert len(idx)%3==0
    assert all(0<=i[0]<len(positions) for i in idx)
    tris=len(idx)//3
    low=[min(p[a] for p in positions) for a in range(3)]
    high=[max(p[a] for p in positions) for a in range(3)]
    dimensions=[high[i]-low[i] for i in range(3)]
    assert abs(low[1])<=.02,'Base is not at Y=0'
    assert abs(low[0]+high[0])<=.2 and abs(low[2]+high[2])<=.2,'Footprint not centered'
    if name=='watchtower':
        assert 1500<=tris<=6000 and 15.5<=dimensions[1]<=16.5,'Tower size/triangle budget'
        assert all(5.5<=dimensions[a]<=6.0 for a in (0,2)),'Parapet footprint'
        shaft=[p for p in positions if .51<p[1]<12.35]
        assert all(max(p[a] for p in shaft)-min(p[a] for p in shaft)<=5.1 for a in (0,2)),'Shaft footprint'
        base=[p for p in positions if p[1]<.51]
        assert all(4.8<=max(p[a] for p in base)-min(p[a] for p in base)<=5.2 for a in (0,2))
        # The front door/slits use dark atlas tile5; inspect exported positions,
        # not a comment claiming the object's forward direction. glTF flips V.
        front=[p for p,uv in zip(positions,uvs) if .25<uv[0]<.5 and .5<uv[1]<.75]
        assert front and all(p[2]>1.8 for p in front),'Front features must face glTF +Z'
        # The iron bowl alone reaches the top; its flat lip must meet fireball.
        assert 15.5<=high[1]<=16.2,'Brazier rim height'
    else:
        assert 2000<=tris<=9000 and 3<=dimensions[1]<=4,'Hoard height/triangle budget'
        assert all(10<=dimensions[a]<=12 for a in (0,2)),'Hoard footprint'
    mats=doc.get('materials',[]);assert len(mats)==1 and prim.get('material')==0
    pbr=mats[0]['pbrMetallicRoughness']; assert pbr.get('baseColorFactor',[1,1,1,1])==[1,1,1,1]
    assert not mats[0].get('normalTexture')
    tex=doc['textures'][pbr['baseColorTexture']['index']]
    images=doc.get('images',[]);assert len(images)==1
    image=images[tex['source']];assert image['mimeType']=='image/png' and 'uri' not in image
    view=doc['bufferViews'][image['bufferView']];start=view.get('byteOffset',0)
    png=blob[start:start+view['byteLength']];assert png[:8]==b'\x89PNG\r\n\x1a\n'
    w,h=struct.unpack_from('>II',png,16);assert w==h and w in (512,1024)
    # Full round trip, independent of glTF accessor bounds and exporter state.
    clear();bpy.ops.import_scene.gltf(filepath=str(path), disable_bone_shape=True)
    meshes=[o for o in bpy.context.scene.objects if o.type=='MESH']
    rigs=[o for o in bpy.context.scene.objects if o.type=='ARMATURE']
    assert len(meshes)==len(rigs)==1 and len(bpy.context.scene.objects)==2
    obj=meshes[0];rig=rigs[0]
    assert len(rig.data.bones)==1 and rig.data.bones[0].name=='root'
    assert not obj.data.shape_keys and not obj.animation_data and not rig.animation_data
    assert len(obj.data.materials)==1 and obj.data.materials[0].use_nodes
    nodes=obj.data.materials[0].node_tree.nodes
    bsdf=next(n for n in nodes if n.type=='BSDF_PRINCIPLED')
    links=bsdf.inputs['Base Color'].links
    assert len(links)==1 and links[0].from_node.type=='TEX_IMAGE','Base color texture disconnected'
    reimage=links[0].from_node.image
    assert tuple(reimage.size)==(w,h) and reimage.packed_file,'Missing reimported embedded texture'
    assert len(obj.vertex_groups)==1 and obj.vertex_groups[0].name=='root'
    assert all(len(v.groups)==1 and abs(v.groups[0].weight-1)<1e-6 for v in obj.data.vertices)
    obj.data.calc_loop_triangles();assert len(obj.data.loop_triangles)==tris
    imported=[obj.matrix_world@v.co for v in obj.data.vertices]
    # Blender re-import is Z-up. Undo only that coordinate conversion for bbox.
    ip=[(v.x,v.z,-v.y) for v in imported]
    assert all(abs(min(p[a] for p in ip)-low[a])<1e-4 and abs(max(p[a] for p in ip)-high[a])<1e-4 for a in range(3))
    formatted=lambda values:'('+', '.join(f'{v:.3f}' for v in values)+')'
    print(f'VERIFY {name}: PASS',flush=True)
    print(f'  glTF Y-up bbox metres: min={formatted(low)} max={formatted(high)}',flush=True)
    print(f'  dimensions X/Y/Z metres: {formatted(dimensions)}; triangles={tris}; bones=1 [root]',flush=True)
    print(f'  materials=[{mats[0]["name"]}]; textures=[{image.get("name",name+"_basecolor")}: embedded PNG {w}x{h}, baseColor]',flush=True)
    print('  single mesh/skin; root rest+inverse bind=identity; root weights=1; transforms=identity; no animations/morphs/extra nodes: PASS',flush=True)
    print('  axis mapping: Blender (X,Y,Z) -> glTF (X,Z,-Y); forward authored -Y -> +Z; base/centering/budget: PASS',flush=True)
    return obj


def render(name,obj):
    scene=bpy.context.scene
    scene.render.engine='CYCLES';scene.cycles.samples=40
    scene.render.resolution_x=1000;scene.render.resolution_y=1100 if name=='watchtower' else 850
    scene.render.resolution_percentage=100
    scene.world.color=(.23,.23,.23)
    scene.view_settings.view_transform='AgX'
    bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.03))
    floor=bpy.context.object
    mat=bpy.data.materials.new('Inspection floor');mat.diffuse_color=(.10,.12,.14,1)
    floor.data.materials.append(mat)
    target=Vector((0,0,7.8 if name=='watchtower' else 1.3))
    bpy.ops.object.camera_add(location=(23,-29,24) if name=='watchtower' else (12,-16,13))
    camera=bpy.context.object;camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
    camera.data.type='ORTHO';camera.data.ortho_scale=20 if name=='watchtower' else 15
    scene.camera=camera
    for location,power,size in (((-10,-12,22),2400,9),((12,-2,14),1800,8),((0,12,19),2600,7)):
        bpy.ops.object.light_add(type='AREA',location=location)
        lamp=bpy.context.object;lamp.data.energy=power;lamp.data.shape='DISK';lamp.data.size=size
        lamp.rotation_euler=(target-lamp.location).to_track_quat('-Z','Y').to_euler()
    scene.render.filepath=f'/tmp/props-{name}.png';bpy.ops.render.render(write_still=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify-only',action='store_true');parser.add_argument('--render',action='store_true')
    parser.add_argument('--output-dir',type=Path,default=ROOT/'assets'/'props')
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    args.output_dir.mkdir(parents=True,exist_ok=True)
    for name,builder in (('watchtower',watchtower),('hoard_pile',hoard_pile)):
        path=args.output_dir/(name+'.glb')
        if not args.verify_only:
            clear(); obj=builder().mesh(name,atlas(name));skin(obj)
            assert all(m.type=='ARMATURE' for m in obj.modifiers)
            bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',export_animations=False,
                export_yup=True,export_apply=False,export_extras=False,export_cameras=False,export_lights=False)
            canonicalize_root(path)
        obj=verify(path,name)
        if args.render: render(name,obj)


if __name__=='__main__':
    try:
        main()
    except BaseException:
        traceback.print_exc();sys.stdout.flush();sys.stderr.flush()
        # Blender otherwise returns success for Python errors without a CLI flag.
        os._exit(1)
