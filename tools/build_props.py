#!/usr/bin/env python3
"""Build the four second-generation metre-scale Dragon Engine props with Blender, then verify GLBs.

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
from mathutils.geometry import closest_point_on_tri

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
               (.13,.15,.17), (.09,.065,.035), (.82,.49,.09), (.85,.55,.12),
               (.27,.105,.035), (.68,.025,.045), (.025,.46,.27), (.025,.24,.64),
               (.85,.55,.12), (.85,.55,.12), (.68,.40,.07), (.23,.25,.24)]
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
                # Broad gold surface: real coin discs carry the readable detail.
                factor = .86 + noise
            rgb=[max(0,min(1,c*factor)) for c in base]
            if tile in (6,7,12,13,14):
                # Gold palette is linear; glTF base-colour PNG is sRGB encoded.
                rgb=[12.92*c if c<=.0031308 else 1.055*c**(1/2.4)-.055 for c in rgb]
            pixels.extend(rgb+[1])
    image = bpy.data.images.new(name+'_basecolor', width=512, height=512, alpha=True)
    image.pixels.foreach_set(pixels)
    image.file_format = 'PNG'
    image.pack()
    mat = bpy.data.materials.new(name+'_atlas')
    mat.use_nodes = True
    shader = mat.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Roughness'].default_value = .72 if name in ('watchtower','spire_tower') else .40
    shader.inputs['Metallic'].default_value = 0.0 if name in ('watchtower','spire_tower') else .45
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


def beam(g, a, b, width, tile=4):
    a,b=Vector(a),Vector(b)
    transform=Matrix.Translation((a+b)/2) @ (b-a).to_track_quat('Z','Y').to_matrix().to_4x4()
    h=(b-a).length/2
    g.add([(-width/2,-width/2,-h),(width/2,-width/2,-h),(width/2,width/2,-h),(-width/2,width/2,-h),
           (-width/2,-width/2,h),(width/2,-width/2,h),(width/2,width/2,h),(-width/2,width/2,h)],
          [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],tile,transform)


def square(g, z0, z1, half0, half1, tile):
    g.lathe((0,0,0),[(half0*math.sqrt(2),z0),(half1*math.sqrt(2),z1)],tile,4,
            Matrix.Rotation(math.pi/4,4,'Z'))


def watchtower():
    g=Geometry();rng=random.Random(204)
    g.box((0,0,.4),(11,11,.8),2,.12)
    g.box((0,0,1.1),(10,10,.6),1,.1)
    # Three tapering sections with metre-scale masonry and pronounced courses.
    for z0,z1,h0,h1 in ((1.4,12,4.5,4.1),(12,22,4.1,3.7),(22,30,3.7,3.4)):
        square(g,z0,z1,h0,h1,0)
        for z in range(math.ceil(z0),int(z1),2):
            half=h0+(h1-h0)*(z-z0)/(z1-z0)
            for side in range(4):
                rot=Matrix.Rotation(side*math.pi/2,4,'Z')
                for x in (-half*.67,0,half*.67):
                    g.box(rot@Vector((x,-half,z+.5)),(half*.64,.16,.95),rng.randrange(4),.04,side*math.pi/2)
        g.box((0,0,z1),(h1*2+.8,h1*2+.8,.65),1,.08)
    for side in range(4):
        rot=Matrix.Rotation(side*math.pi/2,4,'Z')
        for x in (-3.5,3.5):
            beam(g,rot@Vector((x,-4.9,1)),rot@Vector((x,-3.9,10)),1.25,2)
        for z,half in ((7,4.3),(17,3.9),(26,3.55)):
            for x in (-1.9,1.9):
                g.box(rot@Vector((x,-half-.12,z)),(.65,.12,2.0),5,0,side*math.pi/2)
                # Side jambs stand proud of the dark inset, a broad sill below.
                for dx in (-.48,.48):
                    g.box(rot@Vector((x+dx,-half-.24,z)),(.22,.40,2.35),1,.03,side*math.pi/2)
                g.box(rot@Vector((x,-half-.26,z-1.1)),(1.15,.48,.25),1,.03,side*math.pi/2)
                if z==26:
                    g.box(rot@Vector((x,-half-.20,z)),(.35,.06,1.35),14,0,side*math.pi/2)
        for x in (-2.8,0,2.8):
            beam(g,rot@Vector((x,-3.3,28.2)),rot@Vector((x,-4.65,30.8)),.65,8)
        g.box(rot@Vector((0,-4.25,31.3)),(9,.65,1.6),8,.05,side*math.pi/2)
        for x in (-3,0,3):
            g.box(rot@Vector((x,-4.61,31.4)),(1.25,.10,.65),5,0,side*math.pi/2)
    g.box((0,0,32.3),(9.8,9.8,.6),1,.10)
    for side in range(4):
        rot=Matrix.Rotation(side*math.pi/2,4,'Z')
        g.box(rot@Vector((0,-4.5,33)),(9.6,.65,.85),2,.06,side*math.pi/2)
        for x in (-4,-2,0,2,4):
            g.box(rot@Vector((x,-4.5,33.9)),(1.1,.85,1.1),1,.07,side*math.pi/2)
    # Roof on rear half; front half of platform is open to the sky.
    g.box((0,1.6,34.2),(4.2,3.5,3.2),0,.08)
    g.lathe((0,1.6,0),[(3.4,35.8),(.08,40)],15,4,Matrix.Rotation(math.pi/4,4,'Z'))
    g.lathe((0,-2,0),[(1.3,32.6),(1.3,33.1),(.65,33.1),(.65,34.1)],2,12)
    g.lathe((0,-2,0),[(.7,34),(1.45,34.7),(1.45,34.9),(1.2,34.9),(.55,34.2)],4,16)
    for i in range(8):
        a=i*TAU/8
        beam(g,(.6*math.cos(a),-2+.6*math.sin(a),34.2),(1.5*math.cos(a),-2+1.5*math.sin(a),35.4),.16)
    return g


def spire_tower():
    g=Geometry()
    g.box((0,0,.4),(7,7,.8),2,.1)
    g.lathe((0,0,0),[(3.4,.8),(3.2,2),(2.5,3),(1.7,25),(1.15,36),(.7,39)],0,8)
    for i in range(8):
        a=i*TAU/8
        def p(r,z): return (r*math.cos(a),r*math.sin(a),z)
        for r0,z0,r1,z1,w in ((3,1,2.2,12,.55),(2.2,12,1.5,28,.4),(1.5,28,.8,39,.3)):
            beam(g,p(r0,z0),p(r1,z1),w,1)
        # Attached flying buttress fins supporting the apparently hovering rings.
        beam(g,p(1.3,29),p(3.3,32),.34,15)
        beam(g,p(1.0,34),p(2.7,36),.30,15)
    for z,r in ((32,3.5),(36,2.9)):
        g.lathe((0,0,0),[(r-.4,z-.3),(r,z),(r,z+.4),(r-.4,z+.5),(r-.4,z-.3)],1,32,cap=False)
    g.lathe((0,0,0),[(.8,38.5),(1.3,39.2),(1.1,39.7)],15,12)
    # Four continuous curved polygonal prongs; empty orb volume at (0,42,0).
    for i in range(4):
        a=i*TAU/4+math.pi/4
        profile=[(.85,39.3),(1.65,40.2),(2.15,41.7),(1.95,43),(1.35,44)]
        for j,((r,z),(r2,z2)) in enumerate(zip(profile,profile[1:])):
            beam(g,(r*math.cos(a),r*math.sin(a),z),(r2*math.cos(a),r2*math.sin(a),z2),.62-j*.075,15)
    # Normalize tip height exactly, including prong cross sections.
    top=max(v[2] for v in g.vertices)
    g.vertices=[(x,y,z*44/top) for x,y,z in g.vertices]
    return g


def mound(g,radius,height,seed):
    rng=random.Random(seed);n=48;rings=9
    def h(r): return height*max(0,1-(r/radius)**1.5)
    vs=[(0,0,height)];fs=[]
    for k in range(1,rings+1):
        r=radius*k/rings
        for i in range(n):
            a=i*TAU/n;vs.append((r*math.cos(a),r*math.sin(a),max(0,h(r)+(rng.uniform(-.16,.16) if k<rings else 0))))
    for i in range(n):fs.append((0,1+i,1+(i+1)%n))
    for k in range(rings-1):
        for i in range(n):
            a=1+k*n+i;b=1+k*n+(i+1)%n;fs.append((a,a+n,b+n,b))
    fs.append(tuple(reversed(range(1+(rings-1)*n,1+rings*n))))
    g.add(vs,fs,12)
    for i in range(150):
        a=rng.uniform(0,TAU);r=radius*.92*math.sqrt(rng.random());rr=rng.uniform(.22,.42)
        g.lathe((r*math.cos(a),r*math.sin(a),h(r)+.09),[(rr,0),(rr,.10)],rng.choice((6,7,13)),8)
    return h


def chest(g,x,y,z,angle=0,s=1):
    # Hollow box, visible gold fill, upright open lid with gold binding.
    local=Geometry()
    local.box((0,0,.12),(2.3,1.5,.24),8,.04)
    for xx in (-1.05,1.05):local.box((xx,0,.55),(.2,1.5,1),8,.03)
    for yy in (-.65,.65):local.box((0,yy,.55),(2.2,.2,1),8,.03)
    local.box((0,0,.78),(1.9,1.1,.2),12,.03)
    for ix in range(5):
        for iy in range(3):
            local.lathe((-.74+ix*.37,-.36+iy*.36,.89),[(.21,0),(.21,.09)],6,8)

    local.box((0,.8,1.65),(2.3,.2,1.5),8,.06)
    for xx in (-.78,.78):
        local.box((xx,.66,1.65),(.17,.12,1.5),7,.02)
        local.box((xx,-.77,.55),(.17,.08,1.05),7,.02)
    local.box((0,-.8,.7),(.32,.15,.4),7,.02)
    for i in range(8):
        xx=(i%4-.5)*.35-.35;yy=-.8-(i//4)*.35
        local.lathe((xx,yy,.32-(i//4)*.16),[(.25,0),(.25,.1)],6,8)
    mat=Matrix.Translation(Vector((x,y,z)))@Matrix.Rotation(angle,4,'Z')@Matrix.Scale(s,4)
    # Preserve per-face atlas assignments while merging components.
    offset=len(g.vertices);g.vertices.extend(tuple(mat@Vector(v)) for v in local.vertices)
    g.faces.extend(tuple(offset+i for i in f) for f in local.faces);g.tiles.extend(local.tiles)


def treasures(g,h):
    for x,y,s,tile in ((-3,-2,1.0,9),(3,-1,1.2,10),(.7,2.4,1.1,11),(-4,1,.85,11)):
        g.lathe((x,y,h(math.hypot(x,y))),[(.45*s,0),(s,.35*s),(.55*s,1.25*s)],tile,6)
    for x,y in ((-2,-4),(3,3)):
        z=h(math.hypot(x,y))
        for row in range(3):
            for col in range(3-row):
                g.box((x+col*.63,y,z+.23+row*.38),(.60,1.2,.36),7,.09,.1)
    for x,y in ((-4,-.6),(2,-3.3)):
        z=h(math.hypot(x,y))
        g.lathe((x,y,z),[(.5,0),(.5,.12),(.15,.3),(.15,.75),(.55,.95),(.65,1.5),(.5,1.5),(.4,1.1)],7,12)


def hoard_pile():
    g=Geometry();h=mound(g,8,3.7,901)
    for x,y,a,s in ((-3,1.8,-.35,1.1),(3.3,-3,.4,1.1),(1,4,2.8,.9)):
        chest(g,x,y,h(math.hypot(x,y))-.15,a,s)
    treasures(g,h)
    # Crown is the summit, hollow and visibly serrated.
    g.lathe((0,-.6,3.75),[(1,0),(1,.55),(.82,.55),(.82,0)],7,16,cap=False)
    for i in range(8):
        a=i*TAU/8
        g.lathe((.91*math.cos(a),-.6+.91*math.sin(a),4.2),[(.25,0),(.06,.8)],7,4)
        g.lathe((1.01*math.cos(a),-.6+1.01*math.sin(a),4.03),[(.18,0),(.08,.22)],9 if i%2 else 11,6)
    # Shields lean out of the mound; sword stands against the crown.
    for x,y,a in ((-4,-3,-.5),(4,1,.5)):
        z=h(math.hypot(x,y))
        tr=Matrix.Rotation(math.pi/3,4,'X')@Matrix.Rotation(a,4,'Z')
        g.lathe((x,y,z+.4),[(1.05,0),(1.05,.15),(.78,.24),(.25,.38)],15,8,tr)
        g.lathe((x,y,z+.4),[(.25,.38),(.12,.5)],7,8,tr)
    g.add([(1.5,.4,2.9),(1.15,.4,4.6),(1.5,.28,4.6),(1.85,.4,4.6),(1.5,.52,4.6)],
          [(0,2,1),(0,3,2),(0,4,3),(0,1,4),(1,2,3,4)],15)
    beam(g,(1.5,.4,4.6),(1.5,.4,5.1),.22,15)
    g.box((1.5,.4,4.7),(1.1,.22,.18),7,.04)
    g.lathe((1.5,.4,5.05),[(.18,0),(.18,.45)],8,8)
    return g


def hoard_trove():
    g=Geometry();h=mound(g,5.6,2.0,902)
    # Broken altar ring, wide gaps and uneven large blocks.
    for i in range(20):
        a=i*TAU/20
        if i in (3,4,12):continue
        g.box((6.1*math.cos(a),6.1*math.sin(a),.4),(1.65,1.3,.8),2,.13,a+math.pi/2)
        if i in (0,1,7,8,9,15,16):
            g.box((6.1*math.cos(a),6.1*math.sin(a),1.1),(1.5,1.2,.65),0,.12,a+math.pi/2)
    chest(g,-2,-1,h(math.sqrt(5))-.2,-.2,1.05)
    chest(g,2.8,1,h(3)-.2,.5,.85)
    treasures(g,h)
    for x,y,a in ((-3.8,2,.8),(3.5,-2.7,-.7)):
        g.lathe((x,y,1),[(.45,0),(.7,.3),(.85,1.1),(.5,1.7),(.4,2),(.3,2),(.3,1.65)],3,12,
                Matrix.Rotation(math.pi/2,4,'Y')@Matrix.Rotation(a,4,'Z'))
    beam(g,(-3,2,1),(-3,2,4),.15,8)
    # Two-sided folded red banner, authored geometry rather than alpha cards.
    g.add([(-3,2,3.85),(-1.8,2.2,3.8),(-.8,2,3.65),(-3,2,2.5),(-1.8,2.2,2.35),(-.8,2,2.7)],
          [(0,1,4,3),(1,2,5,4),(3,4,1,0),(4,5,2,1)],9)
    # Exactly centered 14 m altar foundation.
    g.lathe((0,0,0),[(7,0),(7,.18),(6.7,.35)],2,40)
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
    assert 0 < tris <= 20000, 'Triangle budget exceeded'
    expected={'watchtower':(11,40,11),'spire_tower':(7,44,7),
              'hoard_pile':(16,5.5,16),'hoard_trove':(14,4,14)}[name]
    assert all(abs(dimensions[a]-expected[a])<(.3 if a!=1 else .6) for a in range(3)), 'Incorrect dimensions'
    assert not doc['meshes'][0].get('weights'), 'Morph weights forbidden'
    if name in ('watchtower','spire_tower'):
        center=(0,35.2,2) if name=='watchtower' else (0,42,0)
        clearance=.65 if name=='watchtower' else 1.2
        center_vec=Vector(center)
        for k in range(0,len(idx),3):
            triangle=[Vector(positions[idx[k+j][0]]) for j in range(3)]
            assert (closest_point_on_tri(center_vec,*triangle)-center_vec).length>clearance, 'Effect socket obstructed'

    assert len(accessor(doc,blob,skin_doc['inverseBindMatrices']))==1
    assert all(all(jj==0 for jj in j) for j in joints), 'Extra joint indices'

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
    if name in ('watchtower','spire_tower'):
        print(f'  effect centre glTF metres: {formatted(center)}; empty sphere radius={clearance:.2f} m: PASS',flush=True)
    print(f'  glTF Y-up bbox metres: min={formatted(low)} max={formatted(high)}',flush=True)
    print(f'  dimensions X/Y/Z metres: {formatted(dimensions)}; triangles={tris}; bones=1 [root]',flush=True)
    print(f'  materials=[{mats[0]["name"]}]; textures=[{image.get("name",name+"_basecolor")}: embedded PNG {w}x{h}, baseColor]',flush=True)
    print('  single mesh/skin; root rest+inverse bind=identity; root weights=1; transforms=identity; no animations/morphs/extra nodes: PASS',flush=True)
    print('  axis mapping: Blender (X,Y,Z) -> glTF (X,Z,-Y); forward authored -Y -> +Z; base/centering/budget: PASS',flush=True)
    return obj


def render(name,obj):
    scene=bpy.context.scene
    scene.render.engine='CYCLES';scene.cycles.samples=32;scene.cycles.seed=17
    scene.render.resolution_x=1200;scene.render.resolution_y=1000
    scene.render.resolution_percentage=100
    scene.world.color=(.32,.32,.32)
    scene.view_settings.view_transform='AgX'
    bpy.ops.mesh.primitive_plane_add(size=2000,location=(0,0,-.04))
    floor=bpy.context.object
    mat=bpy.data.materials.new('Inspection floor');mat.diffuse_color=(.075,.095,.10,1)
    floor.data.materials.append(mat)
    height=max(v.co.z for v in obj.data.vertices)
    target=Vector((0,0,height*.46))
    bpy.ops.object.camera_add();camera=bpy.context.object;scene.camera=camera
    camera.data.type='PERSP';camera.data.lens=50;camera.data.clip_end=3000
    bpy.ops.object.light_add(type='SUN',location=(0,0,80))
    sun=bpy.context.object;sun.rotation_euler=(.45,-.5,-.4);sun.data.energy=2.5;sun.data.angle=.15
    out=ROOT/'artifacts/props/v2';out.mkdir(parents=True,exist_ok=True)
    for label,distance in (('near',height*2.25 if height>10 else 27),('far',250)):
        camera.location=target+Vector((.55,-.67,.5)).normalized()*distance
        camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
        scene.render.filepath=str(out/(name+'_'+label+'.png'))
        bpy.ops.render.render(write_still=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify-only',action='store_true');parser.add_argument('--render',action='store_true')
    parser.add_argument('--output-dir',type=Path,default=ROOT/'assets'/'props')
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    args.output_dir.mkdir(parents=True,exist_ok=True)
    for name,builder in (('watchtower',watchtower),('spire_tower',spire_tower),('hoard_pile',hoard_pile),('hoard_trove',hoard_trove)):
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
