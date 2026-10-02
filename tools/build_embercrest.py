"""Original Embercrest asset. Run with Blender 5.x, or exec through Blender MCP.

Blender axes: +Y forward, +Z up. glTF export produces -Z forward, +Y up.
All geometry, UVs, materials and skin weights are authored here; no source mesh
from either reference dragon is used. Run stages individually via MCP:
  exec(compile(open(PATH).read(), PATH, 'exec')); build(); finish()
Or: blender --background --python tools/build_embercrest.py -- --build
"""
import bpy
import math
import json
import os
import sys
from pathlib import Path
from mathutils import Vector, Quaternion
import numpy as np

ROOT = Path(__file__).resolve().parents[1] if '__file__' in globals() else Path.cwd()
sys.path.insert(0,str(ROOT/'tools'))
from embercrest_materials import generate_maps
OUT = ROOT / 'assets/embercrest'
CAP = ROOT / 'artifacts/embercrest-scripted'
for p in [OUT / 'source', OUT / 'textures', CAP]:
    p.mkdir(parents=True, exist_ok=True)

def mixw(a, b, t):
    d = {}
    for n,w in a.items(): d[n] = d.get(n,0) + w*(1-t)
    for n,w in b.items(): d[n] = d.get(n,0) + w*t
    return {n:w for n,w in d.items() if w > 1e-6}

def weight(name): return {name:1.0}

class Surface:
    def __init__(self):
        self.v=[]; self.f=[]; self.uv=[]; self.w=[]; self.mat=[]; self.flat=set()
    def vertex(self,p,uv,w):
        i=len(self.v); self.v.append(tuple(p)); self.uv.append(uv); self.w.append(w); return i
    def face(self,ids,mat): self.f.append(tuple(ids)); self.mat.append(mat)
    def tube(self,points,radii,weights,mat, sides=16, steps=3, uvscale=1.0, ellipse=None):
        # Cubic Hermite interpolation gives continuous anatomical profiles without
        # changing manually specified joint/weight anchors.
        ps=[Vector(p) for p in points]
        skin_uv=MATERIALS[mat].get('surface_kind')=='skin'
        uv_radius=max(max(r) if not isinstance(r,(float,int)) else r for r in radii)
        uv_length=sum((b-a).length for a,b in zip(ps,ps[1:]))
        rings=[]
        for k in range((len(ps)-1)*steps+1):
            j=min(k//steps,len(ps)-2); t=(k-j*steps)/steps
            a,b=ps[j],ps[j+1]
            ma=(ps[j+1]-ps[max(0,j-1)])*.5 if j else b-a
            mb=(ps[min(len(ps)-1,j+2)]-ps[j])*.5 if j+1<len(ps)-1 else b-a
            c=(2*t**3-3*t*t+1)*a+(t**3-2*t*t+t)*ma+(-2*t**3+3*t*t)*b+(t**3-t*t)*mb
            tangent=((6*t*t-6*t)*a+(3*t*t-4*t+1)*ma+(-6*t*t+6*t)*b+(3*t*t-2*t)*mb).normalized()
            ref=Vector((1,0,0)) if abs(tangent.x)<.9 else Vector((0,1,0))
            u=(ref-tangent*ref.dot(tangent)).normalized(); v=u.cross(tangent).normalized()
            r0,r1=radii[j],radii[j+1]
            if isinstance(r0,(int,float)): r0=(r0,r0)
            if isinstance(r1,(int,float)): r1=(r1,r1)
            rx=r0[0]*(1-t)+r1[0]*t; ry=r0[1]*(1-t)+r1[1]*t
            w=mixw(weights[j],weights[j+1],t)
            ring=[]
            for s in range(sides+1):
                ang=2*math.pi*s/sides
                co,si=math.cos(ang),math.sin(ang)
                if ellipse:
                    co=math.copysign(abs(co)**(2/ellipse),co)
                    si=math.copysign(abs(si)**(2/ellipse),si)
                p=c+u*(co*rx)+v*(si*ry)
                if skin_uv:
                    # A common physical scale avoids giant coins on limbs and
                    # compressed horizontal bands on the neck.
                    uv=(s/sides*math.tau*uv_radius/4.0,k/((len(ps)-1)*steps)*uv_length/4.0)
                else: uv=(s/sides,k/((len(ps)-1)*steps)*uvscale)
                ring.append(self.vertex(p,uv,w))
            rings.append(ring)
        for a,b in zip(rings,rings[1:]):
            for s in range(sides): self.face((a[s],b[s],b[s+1],a[s+1]),mat)
        for ri,reverse in [(rings[0],True),(rings[-1],False)]:
            cap=[]
            for s,idx in enumerate(ri[:-1]):
                angle=math.tau*s/sides
                cap.append(self.vertex(self.v[idx],(.5+.45*math.cos(angle),.5+.45*math.sin(angle)),self.w[idx]))
            self.face(tuple(reversed(cap)) if reverse else cap,mat)
    def ellipsoid(self,c,r,w,mat,segments=20,rings=12):
        # Nondegenerate end rings (tiny caps), consistent outward winding.
        ps=[];rs=[]
        for i in range(rings+1):
            a=-math.pi*.5+math.pi*i/rings
            ps.append((c[0],c[1]+r[1]*math.sin(a),c[2]))
            rs.append((max(.001,r[0]*math.cos(a)),max(.001,r[2]*math.cos(a))))
        self.tube(ps,rs,[w]*len(ps),mat,sides=segments,steps=1)
    def plate(self,base,width,length,height,w,mat,back=-1):
        # Upright keratin blade, with the actual apex swept toward the tail.
        # The first version placed its apex over the front edge and consequently
        # read as a row of forward-pointing paper arrows.
        x,y,z=base
        self.tube([(x,y,z-.06),(x,y+back*length*.16,z+height*.3),
                   (x,y+back*length*.54,z+height*.78),(x,y+back*length,z+height)],
                  [(width*.42,length*.25),(width*.29,length*.20),(width*.12,length*.095),(.001,.001)],
                  [w]*4,mat,12,4,uvscale=1.0,ellipse=2.5)
    def scute(self,c,n,t,width,length,height,w,mat):
        n=Vector(n).normalized();t=Vector(t);t=(t-n*t.dot(n)).normalized()
        a=n.cross(t).normalized();c=Vector(c);rings=[]
        for r in [1,.72,.35,.025]:
            ring=[]
            for i in range(13):
                theta=math.tau*i/12;co=math.cos(theta);si=math.sin(theta)
                edge=.94+.06*math.cos(theta*5)
                q=c+a*(co*width*.5*r*edge)+t*(si*length*.5*r*(1+.22*si)*edge)+n*(height*(1-r*r)-.008)
                ring.append(self.vertex(q,(.5+co*.5*r,.5+si*.5*r),w))
            rings.append(ring)
        for ra,rb in zip(rings,rings[1:]):
            for i in range(12): self.face((ra[i],ra[i+1],rb[i+1],rb[i]),mat)
        self.face(rings[-1][:-1],mat)
    def ventral_scutes(self,points,radii,weights,mat,count=36):
        ps=[Vector(p) for p in points]
        def sample(q):
            j=min(int(q),len(ps)-2);t=q-j;a,b=ps[j],ps[j+1]
            ma=(ps[j+1]-ps[max(0,j-1)])*.5 if j else b-a
            mb=(ps[min(len(ps)-1,j+2)]-ps[j])*.5 if j+1<len(ps)-1 else b-a
            c=(2*t**3-3*t*t+1)*a+(t**3-2*t*t+t)*ma+(-2*t**3+3*t*t)*b+(t**3-t*t)*mb
            tan=((6*t*t-6*t)*a+(3*t*t-4*t+1)*ma+(-6*t*t+6*t)*b+(3*t*t-2*t)*mb).normalized()
            u=Vector((1,0,0));v=u.cross(tan).normalized()
            rx=radii[j][0]*(1-t)+radii[j+1][0]*t;rz=radii[j][1]*(1-t)+radii[j+1][1]*t
            return c,u,v,rx,rz,mixw(weights[j],weights[j+1],t)
        # Small raised panels follow precisely the same Hermite surface as skin.
        for i in range(count):
            q=.8+i*(len(ps)-1.8)/count;grid=[]
            for k in range(4):
                row=[]
                for j in range(13):
                    u=j/12;z=k/3
                    c,ax,ay,rx,rz,w=sample(q+(z-.5)*.23)
                    angle=-math.pi/2+(u-.5)*1.5
                    h=.018+.018*math.sin(math.pi*u)*math.sin(math.pi*z)
                    p=c+ax*((rx+h)*math.cos(angle))+ay*((rz+h)*math.sin(angle))
                    row.append(self.vertex(p,(u,z),w))
                grid.append(row)
            for a,b in zip(grid,grid[1:]):
                for j in range(12): self.face((a[j],b[j],b[j+1],a[j+1]),mat)

MATERIALS=[]
def texture(name, data, noncolor=False):
    h,w=data.shape[:2]
    im=bpy.data.images.new(name,width=w,height=h,alpha=True)
    im.colorspace_settings.name='Non-Color' if noncolor else 'sRGB'
    rgba=np.ones((h,w,4),dtype=np.float32);rgba[:,:,:data.shape[2]]=data
    im.pixels.foreach_set(rgba.ravel());im.update()
    im.filepath_raw=str(OUT/'textures'/f'{name}.png');im.file_format='PNG';im.save();im.pack()
    return im

def material(name,color,kind='solid',rough=.65):
    size=2048 if kind in ('skin','membrane') else 1024 if kind=='horn' else 256
    col,normal_pixels,orm=generate_maps(kind,color,size,193+len(MATERIALS))
    baseline={'skin':.73,'membrane':.76,'horn':.53}.get(kind,.66)
    orm[:,:,1]=np.clip(orm[:,:,1]+rough-baseline,.08,.98)
    base=texture(name+'_base',col)
    normal=texture(name+'_normal',normal_pixels,True)
    ormim=texture(name+'_orm',orm,True)
    mat=bpy.data.materials.new(name);mat.use_nodes=True
    n=mat.node_tree.nodes;l=mat.node_tree.links;bs=n.get('Principled BSDF')
    bs.inputs['Roughness'].default_value=rough
    for im,slot in [(base,'Base Color')]:
        node=n.new('ShaderNodeTexImage');node.image=im;l.new(node.outputs['Color'],bs.inputs[slot])
    node=n.new('ShaderNodeTexImage');node.image=normal
    nm=n.new('ShaderNodeNormalMap');l.new(node.outputs['Color'],nm.inputs['Color']);l.new(nm.outputs['Normal'],bs.inputs['Normal'])
    node=n.new('ShaderNodeTexImage');node.image=ormim
    sep=n.new('ShaderNodeSeparateColor');l.new(node.outputs['Color'],sep.inputs['Color']);l.new(sep.outputs['Green'],bs.inputs['Roughness']);l.new(sep.outputs['Blue'],bs.inputs['Metallic'])
    mat.diffuse_color=(*color,1)
    mat['surface_kind']=kind
    MATERIALS.append(mat);return len(MATERIALS)-1

BONES=[]
def bone(name,a,b,parent=None):
    BONES.append((name,tuple(a),tuple(b),parent));return name

def chain(names,points,parent):
    for i,n in enumerate(names): bone(n,points[i],points[min(i+1,len(points)-1)] if i+1<len(points) else Vector(points[i])+Vector((0,.15,0)),parent if i==0 else names[i-1])

def loft_frame(points,radii,weights,q):
    ps=[Vector(p) for p in points];j=min(int(q),len(ps)-2);t=q-j;a,b=ps[j],ps[j+1]
    ma=(ps[j+1]-ps[max(0,j-1)])*.5 if j else b-a
    mb=(ps[min(len(ps)-1,j+2)]-ps[j])*.5 if j+1<len(ps)-1 else b-a
    c=(2*t**3-3*t*t+1)*a+(t**3-2*t*t+t)*ma+(-2*t**3+3*t*t)*b+(t**3-t*t)*mb
    tan=((6*t*t-6*t)*a+(3*t*t-4*t+1)*ma+(-6*t*t+6*t)*b+(3*t*t-2*t)*mb).normalized()
    u=Vector((1,0,0));v=u.cross(tan).normalized()
    rx=radii[j][0]*(1-t)+radii[j+1][0]*t;rz=radii[j][1]*(1-t)+radii[j+1][1]*t
    return c,u,v,tan,rx,rz,mixw(weights[j],weights[j+1],t)

def body_armour(points,radii,weights):
    rng=np.random.default_rng(20481)
    for row in range(28):
        q=1.0+row*8.5/28
        for col in range(10):
            if rng.random()<.22: continue
            theta=-.12+(col+.5*(row%2))*math.pi/9 + rng.uniform(-.035,.035)
            c,u,v,t,rx,rz,w=loft_frame(points,radii,weights,q+rng.uniform(-.04,.04))
            n=(u*(math.cos(theta)/rx)+v*(math.sin(theta)/rz)).normalized()
            p=c+u*(math.cos(theta)*rx)+v*(math.sin(theta)*rz)
            width=(.19 if q<6 else .13)*rng.uniform(.85,1.15)
            S.scute(p,n,-t,width,width*1.35,.012 if q<6 else .010,w,ARMOR)
    # Spine blades attach to the true dorsal surface rather than fixed Z values.
    # They all point aft (-Y) and up; size falls smoothly toward the tail.
    for q,h,l in [(2.2,.20,.29),(3.0,.27,.35),(3.8,.31,.38),(4.5,.34,.43),
                  (5.2,.32,.40),(6.0,.37,.44),(6.9,.45,.53),(7.8,.50,.59),(8.6,.53,.66),(9.25,.46,.65)]:
        c,u,v,t,rx,rz,w=loft_frame(points,radii,weights,q)
        S.plate(c+v*rz,.17,l,h,w,RIDGE)

def raise_neck():
    # Rig and mesh change together; horn/eye/jaw geometry follows the whole head
    # rigidly, while the weighted neck smoothly acquires the upright S profile.
    phase={'neck_01':0,'neck_02':.20,'neck_03':.48,'neck_04':.76,'neck_05':.94,
           'head':1,'jaw':1,'jaw_tip':1}
    delta=Vector((0,-.55,1.28))
    for i,w in enumerate(S.w):
        amount=sum(phase.get(n,0)*v for n,v in w.items())
        S.v[i]=tuple(Vector(S.v[i])+delta*amount)
    heads={tuple(a):phase.get(n,0) for n,a,b,p in BONES}
    for i,(n,a,b,p) in enumerate(BONES):
        BONES[i]=(n,tuple(Vector(a)+delta*phase.get(n,0)),
                  tuple(Vector(b)+delta*heads.get(tuple(b),phase.get(n,0))),p)

def build():
    global S, ARM, BODY, SKIN, COPPER, BELLY, HORN, RIDGE, DARK, GOLD, PUPIL, IVORY, MOUTH, ARMOR, BONES, MATERIALS
    BONES=[];MATERIALS=[]
    scene=bpy.data.scenes.get('Embercrest Workshop') or bpy.data.scenes.new('Embercrest Workshop')
    if bpy.context.window: bpy.context.window.scene=scene
    # Only the named build collection is replaceable; other scenes are preserved.
    old=bpy.data.collections.get('Embercrest Asset')
    if old:
        for obj in list(old.objects): bpy.data.objects.remove(obj,do_unlink=True)
        bpy.data.collections.remove(old)
    # Remove only orphaned resources owned by earlier runs of this generator.
    # This keeps repeat builds compact and prevents stale actions being exported.
    for datablocks in [bpy.data.meshes,bpy.data.armatures,bpy.data.materials,bpy.data.actions,bpy.data.images]:
        for block in list(datablocks):
            if block.name.startswith('Embercrest') and block.users==0: datablocks.remove(block)
    coll=bpy.data.collections.new('Embercrest Asset');scene.collection.children.link(coll)
    SKIN=material('Embercrest charcoal bronze',(.20,.175,.145),'skin',.69)
    COPPER=material('Embercrest copper membrane',(.47,.235,.105),'membrane',.79)
    BELLY=material('Embercrest warm throat scutes',(.49,.40,.28),'horn',.70)
    HORN=material('Embercrest horn',(.18,.145,.095),'horn',.62)
    RIDGE=material('Embercrest oxidized copper crest',(.29,.17,.095),'horn',.66)
    DARK=material('Embercrest sockets',(.042,.027,.024),rough=.80)
    GOLD=material('Embercrest amber iris',(.92,.49,.075),rough=.27)
    PUPIL=material('Embercrest pupils',(.011,.009,.008),rough=.2)
    IVORY=material('Embercrest ivory',(.72,.64,.46),'horn',.44)
    MOUTH=material('Embercrest mouth',(.16,.055,.047),rough=.56)
    ARMOR=material('Embercrest dermal armour',(.20,.175,.14),'horn',.72)
    S=Surface()
    bone('root',(0,0,2.45),(0,.6,2.5))
    bone('chest',(0,.15,2.6),(0,1.2,2.85),'root')
    neckp=[(0,1.15,2.95),(0,1.55,3.25),(0,1.94,3.65),(0,2.3,4.02),(0,2.68,4.23),(0,3.08,4.24)]
    neckn=[f'neck_{i:02}' for i in range(1,6)]
    chain(neckn,neckp,'chest')
    bone('head',neckp[-1],(0,4.30,4.13),neckn[-1])
    bone('jaw',(0,3.1,3.97),(0,4.33,3.94),'head')
    bone('jaw_tip',(0,4.33,3.94),(0,4.50,3.94),'jaw')
    tailp=[(0,-1.6,2.55),(0,-2.25,2.27),(-.05,-2.95,1.95),(-.1,-3.65,1.54),(-.15,-4.3,1.14),(-.12,-4.9,.94),(.08,-5.45,1.04),(.35,-5.9,1.28),(.64,-6.2,1.63)]
    tailn=[f'tail_{i:02}' for i in range(1,9)]
    chain(tailn,tailp,'root')
    # Torso and neck are one continuous loft; the rising, tapered neck has
    # no ball-joint spheres. Wide chest, tucked waist and deep breast keel.
    pts=[(0,-1.85,2.5),(0,-1.5,2.5),(0,-.9,2.55),(0,-.15,2.58),(0,.6,2.68),(0,1.1,2.83)]+neckp[1:]
    radii=[(.30,.38),(.55,.60),(.60,.67),(.71,.84),(.82,1.0),(.69,.84),(.53,.62),(.44,.54),(.37,.46),(.34,.38),(.34,.33)]
    ws=[weight('root')]*3+[mixw(weight('root'),weight('chest'),.5)]+[weight('chest')]*2+[weight(n) for n in neckn[1:]]+[weight('head')]
    S.tube(pts,radii,ws,SKIN,40,5,uvscale=3.5)
    S.ventral_scutes(pts,radii,ws,BELLY)
    body_armour(pts,radii,ws)
    S.tube(tailp,[(.38,.40),(.32,.34),(.26,.28),(.20,.22),(.155,.18),(.115,.14),(.08,.11),(.045,.07),(.006,.008)],
           [weight(n) for n in tailn]+[weight(tailn[-1])],SKIN,20,4,uvscale=3)
    # A relaxed taper and a few diminishing scutes, without the old paddle tip.
    for i in range(1,6):
        x,y,z=tailp[i];r=[.4,.34,.28,.22,.18,.14][i]
        S.plate((x,y,z+r),.15-i*.016,.32-i*.027,.18-i*.02,weight(tailn[i]),RIDGE)
    # Long, angular skull; separate mandible and actual mouth cavity.
    skull_pts=[(0,2.90,4.23),(0,3.17,4.31),(0,3.48,4.30),(0,3.84,4.20),(0,4.26,4.14),(0,4.49,4.12),(0,4.57,4.12),(0,4.61,4.12)]
    skull_radii=[(.30,.31),(.45,.35),(.43,.29),(.30,.22),(.255,.15),(.20,.105),(.11,.05),(.003,.003)]
    skull_weights=[weight('head')]*8
    S.tube(skull_pts,skull_radii,skull_weights,SKIN,32,4,uvscale=1.2,ellipse=2.9)
    for row in range(10):
        q=.6+row*.48
        c,u,v,t,rx,rz,w=loft_frame(skull_pts,skull_radii,skull_weights,q)
        for col in range(7):
            theta=.10+(col+.3*(row%2))*(math.pi-.2)/7
            co=math.cos(theta);si=math.sin(theta)
            p=c+u*(math.copysign(abs(co)**(2/2.9),co)*rx)+v*(abs(si)**(2/2.9)*rz)
            n=(u*co+v*si).normalized()
            S.scute(p,n,-t,.13 if row<5 else .095,.20 if row<5 else .13,.022,w,ARMOR)
    # A sculpted nasal keel terminates in a small hooked rostrum.
    S.tube([(0,3.42,4.56),(0,3.89,4.39),(0,4.34,4.255),(0,4.61,4.15),(0,4.69,4.045)],
           [(.036,.025),(.031,.031),(.026,.024),(.018,.018),(.001,.001)],
           [weight('head')]*5,ARMOR,12,3,ellipse=2.8)
    S.tube([(0,3.06,3.96),(0,3.4,3.86),(0,3.88,3.88),(0,4.28,3.97),(0,4.45,4.01)],
           [(.30,.13),(.36,.13),(.28,.10),(.24,.06),(.17,.015)],[weight('jaw')]*5,SKIN,20,3)
    S.ellipsoid((0,3.82,4.005),(.255,.62,.048),weight('head'),DARK,20,8)
    S.ellipsoid((0,3.84,4.0),(.23,.50,.032),weight('jaw'),MOUTH,20,8)
    S.tube([(0,3.30,4.015),(0,3.67,4.027),(0,4.04,4.037),(0,4.24,4.03)],
           [(.10,.018),(.13,.020),(.10,.018),(.005,.005)],[weight('jaw')]*4,MOUTH,12,3)
    for side in (-1,1):
        # Eyes are visible in side/chase views under angular brows.
        S.ellipsoid((side*.40,3.48,4.34),(.069,.135,.089),weight('head'),DARK)
        S.ellipsoid((side*.455,3.50,4.345),(.034,.079,.051),weight('head'),GOLD)
        S.ellipsoid((side*.484,3.51,4.345),(.009,.015,.044),weight('head'),PUPIL,16,10)
        S.scute((side*.4,3.50,4.43),(side*.7,0,.7),(0,-1,.15),.20,.70,.065,weight('head'),ARMOR)
        # Back-swept paired horns: flattened at the base, then slender.
        S.tube([(side*.35,3.10,4.52),(side*.48,2.84,4.76),(side*.64,2.42,4.9),(side*.69,2.03,4.94),(side*.64,1.78,5.03)],
               [(.15,.19),(.13,.145),(.095,.105),(.045,.045),(.001,.001)],[weight('head')]*5,HORN,16,4,uvscale=1.7)
        # Cheek flanges, forward nostrils and recessed mouth line.
        S.tube([(side*.40,3.18,4.18),(side*.58,2.9,4.12),(side*.66,2.67,4.24)],
               [(.14,.20),(.095,.11),(.002,.004)],[weight('head')]*3,RIDGE,12,3)
        S.ellipsoid((side*.242,4.28,4.19),(.018,.105,.047),weight('head'),DARK,16,8)
        S.tube([(side*.29,4.28,4.06),(side*.335,3.95,4.03),(side*.41,3.53,4.04),(side*.40,3.22,4.08)],
               [.014,.016,.019,.008],[weight('head')]*4,DARK,8,2)
        for i in range(7):
            y=3.30+i*.155;x=side*(.35-(y-3.3)*.115)
            S.tube([(x,y,4.035),(x,y+.025,3.935 if i<3 else 3.97)], [.045 if i<3 else .03,.002],[weight('head')]*2,IVORY,8,2)
        for i in range(6):
            y=3.42+i*.15;x=side*(.32-(y-3.42)*.11)
            S.tube([(x,y,3.982),(x,y-.02,4.055)],[.028,.001],[weight('jaw')]*2,IVORY,8,2)
    # Two crown blades echo the paired horns; other blades follow the neck.
    for y,z,w,l,h,n in [(3.28,4.58,.18,.66,.30,'head'),(2.98,4.55,.20,.86,.43,'head')]:
        S.plate((0,y,z),w,l,h,weight(n),RIDGE)
    limbs_and_wings()
    raise_neck()
    # Build one mesh and one armature; every material primitive shares one skin.
    mesh=bpy.data.meshes.new('Embercrest weighted topology');mesh.from_pydata(S.v,[],S.f);mesh.update()
    BODY=bpy.data.objects.new('Embercrest',mesh);coll.objects.link(BODY)
    for m in MATERIALS: mesh.materials.append(m)
    uv=mesh.uv_layers.new(name='UVMap')
    for poly,mi in zip(mesh.polygons,S.mat):
        poly.material_index=mi;poly.use_smooth=poly.index not in S.flat
        for loop in poly.loop_indices: uv.data[loop].uv=S.uv[mesh.loops[loop].vertex_index]
    # Recalculate outward normals for closed pieces. Membranes are double-sided.
    bpy.context.view_layer.objects.active=BODY;BODY.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.mesh.normals_make_consistent(inside=False);bpy.ops.object.mode_set(mode='OBJECT')
    armdata=bpy.data.armatures.new('Embercrest deform skeleton');ARM=bpy.data.objects.new('Embercrest_Rig',armdata);coll.objects.link(ARM)
    BODY.select_set(False);ARM.select_set(True);bpy.context.view_layer.objects.active=ARM;bpy.ops.object.mode_set(mode='EDIT')
    for name,a,b,parent in BONES:
        eb=armdata.edit_bones.new(name);eb.head=a;eb.tail=b
        if parent: eb.parent=armdata.edit_bones[parent]
        eb.use_deform=True
    bpy.ops.object.mode_set(mode='OBJECT');ARM.show_in_front=True;armdata.display_type='OCTAHEDRAL'
    for label,prefixes in [('Body and head',('root','chest','neck','head','jaw')),
                           ('Tail',('tail',)),('Wings',('wing',)),
                           ('Legs and claws',('thigh','shin','ankle','foot','upper_arm','forearm','wrist','hand','toe'))]:
        bc=armdata.collections.new(label)
        for b in armdata.bones:
            if b.name.startswith(prefixes): bc.assign(b)
    groups={name:BODY.vertex_groups.new(name=name) for name,_,_,_ in BONES}
    for i,w in enumerate(S.w):
        total=sum(w.values())
        for n,v in w.items(): groups[n].add([i],v/total,'REPLACE')
    mod=BODY.modifiers.new('Embercrest skin','ARMATURE');mod.object=ARM;BODY.parent=ARM
    ARM['asset']='Original Embercrest / authored procedurally from selected concept A'
    ARM['axes']='Blender +Y forward, +Z up; glTF -Z forward, +Y up'
    ARM['rig_notes']='Body-space runtime rotations. Wings have shoulder/elbow/wrist and four two-segment fingers. Jaw hinge + forward tip. No IK helpers exported.'
    BODY['design']='Revision 2: upright neck; armoured wedge muzzle; swept-back crown/spines; broad copper membranes; relaxed tapered tail.'
    scene.render.fps=30;scene.frame_start=1;scene.frame_end=120
    # Rotation-only subtle idle; procedural runtime supplies flight/attack motion.
    for name in ['chest','jaw']+[n for n,_,_,_ in BONES if n.startswith('toe_')]:
        pb=ARM.pose.bones[name];pb.rotation_mode='QUATERNION'
        for frame in [1,31,61,91,121]:
            angle=math.sin((frame-1)/120*math.tau)*(.006 if name=='chest' else .015 if name=='jaw' else .025)
            pb.rotation_quaternion=Quaternion((1,0,0),angle);pb.keyframe_insert(data_path='rotation_quaternion',frame=frame,group=name)
    ARM.animation_data.action.name='Embercrest Rest'
    scene.frame_set(1)
    print(json.dumps({'vertices':len(S.v),'faces':len(S.f),'bones':len(BONES),'materials':len(MATERIALS)}))

def limbs_and_wings():
    for side,suffix in [(-1,'l'),(1,'r')]:
        def p(x,y,z): return (side*x,y,z)
        for fore in (False,True):
            if fore:
                ns=[f'upper_arm_{suffix}',f'forearm_{suffix}',f'wrist_{suffix}',f'hand_{suffix}']
                ps=[p(.76,.93,2.72),p(.98,1.32,1.55),p(.86,1.02,.56),p(.88,1.24,.26),p(.88,1.7,.23)]
                rs=[(.31,.41),(.23,.26),(.12,.14),(.18,.13),(.13,.065)]
                par='chest'
            else:
                ns=[f'thigh_{suffix}',f'shin_{suffix}',f'ankle_{suffix}',f'foot_{suffix}']
                ps=[p(.63,-1.18,2.53),p(1.03,-.60,1.47),p(1.06,-1.33,.66),p(1.03,-1.05,.26),p(1.03,-.58,.23)]
                rs=[(.43,.54),(.30,.33),(.14,.17),(.20,.15),(.14,.065)]
                par='root'
            chain(ns,ps,par)
            # Defined upper muscle bellies, elbow and long tapered tendons.
            muscle=Vector(ps[0]).lerp(Vector(ps[1]),.43)
            points=[ps[0],muscle,ps[1],Vector(ps[1]).lerp(Vector(ps[2]),.52)]+ps[2:]
            radii=[rs[0],(rs[0][0]*1.08,rs[0][1]*.98),rs[1],(rs[1][0]*.68,rs[1][1]*.66)]+rs[2:]
            weights=[weight(ns[0]),weight(ns[0]),mixw(weight(ns[0]),weight(ns[1]),.65),weight(ns[1])]+[weight(n) for n in ns[2:]]+[weight(ns[-1])]
            S.tube(points,radii,weights,SKIN,24,4,uvscale=1.5)
            for i in range(3):
                q=Vector(ps[0]).lerp(Vector(ps[1]),i*.18)
                q.x+=side*(rs[0][0]*.84)
                S.scute(q,(side,0,.25),(0,-.3,-1),.27-i*.025,.34,.045,weight(ns[0]),ARMOR)
            foot=Vector(ps[-2]);prefix='fore' if fore else 'hind'
            for j in range(3):
                tx=foot.x+(j-1)*.16;ty=foot.y+.16
                n=f'toe_{prefix}_{j+1}_{suffix}'
                bone(n,(tx,ty,.24),(tx+(j-1)*.045,ty+.48,.19),ns[-1])
                tip=(tx+(j-1)*.07,ty+.52,.18)
                S.tube([(tx,ty,.25),(tx+(j-1)*.04,ty+.30,.24),tip],[.085,.073,.044],[weight(n)]*3,SKIN,12,3)
                S.tube([tip,(tip[0],tip[1]+.16,.16),(tip[0],tip[1]+.24,.09)],[.055,.039,.001],[weight(n)]*3,IVORY,10,3)
        shoulder=Vector(p(.68,.60,3.13));elbow=Vector(p(1.85,.35,3.61));wrist=Vector(p(3.40,.94,3.98));hub=Vector(p(3.76,1.02,4.03))
        wn=[f'wing_root_{suffix}',f'wing_arm_{suffix}',f'wing_wrist_{suffix}']
        chain(wn,[shoulder,elbow,wrist,hub],'chest')
        S.tube([shoulder,elbow,wrist,hub],[(.25,.32),(.15,.17),(.095,.115),(.085,.10)],
               [weight(n) for n in wn]+[weight(wn[-1])],SKIN,20,4,uvscale=2)
        # Carpal hook projects forward and makes the front edge distinctive.
        S.tube([hub,hub+Vector(p(.05,.25,.09)),hub+Vector(p(-.12,.46,.15))],[.11,.08,.001],[weight(wn[-1])]*3,HORN,12,3)
        ends=[Vector(p(7.78,.25,3.89)),Vector(p(7.05,-2.15,3.22)),Vector(p(5.60,-3.78,2.70)),Vector(p(3.25,-3.50,2.32))]
        mids=[];fn=[]
        for j,end in enumerate(ends):
            mid=hub.lerp(end,.53)+Vector((0,.10,.13));mids.append(mid)
            names=[f'wing_finger_{j+1}a_{suffix}',f'wing_finger_{j+1}b_{suffix}'];fn.append(names)
            bone(names[0],hub,mid,wn[-1]);bone(names[1],mid,end,names[0])
            S.tube([hub,mid,end],[.072,.038,.005],[weight(names[0]),mixw(weight(names[0]),weight(names[1]),.5),weight(names[1])],SKIN,12,6,uvscale=1.5)
        def fingerpos(j,t):
            return hub.lerp(ends[j],t)+Vector((0,math.sin(math.pi*t)*.1,math.sin(math.pi*t)*.13))
        def fingerw(j,t):
            if t<.17: return mixw(weight(wn[-1]),weight(fn[j][0]),t/.17)
            return mixw(weight(fn[j][0]),weight(fn[j][1]),max(0,min(1,(t-.32)/.50)))
        # Each membrane cell blends only neighbouring finger segments (max 4).
        for j in range(3):
            grid=[]
            for it in range(25):
                t=it/24;row=[]
                for iu in range(13):
                    u=iu/12;tt=t*(1-.13*math.sin(math.pi*u)*t**3)
                    q=fingerpos(j,tt).lerp(fingerpos(j+1,tt),u)
                    q.z-=math.sin(math.pi*u)*math.sin(math.pi*t)*.16
                    q.z+=.016*math.sin(u*math.pi*5+t*7)*math.sin(math.pi*u)*math.sin(math.pi*t)
                    row.append(S.vertex(q,(u*.32+j*.32,t),mixw(fingerw(j,tt),fingerw(j+1,tt),u)))
                grid.append(row)
            for row,(a,b) in enumerate(zip(grid,grid[1:])):
                for k in range(12):
                    S.face((a[k],b[k],b[k+1]) if row==0 else (a[k],b[k],b[k+1],a[k+1]),COPPER)
        # Inner trailing membrane reaches the flank; seam uses the fourth finger.
        root=Vector(p(.58,-1.48,2.65))
        grid=[]
        for it in range(25):
            t=it/24;row=[]
            for iu in range(17):
                u=iu/16
                inner=shoulder.lerp(root,t)
                q=inner.lerp(fingerpos(3,t),u)
                q.z-=math.sin(math.pi*u)*math.sin(math.pi*t)*.2
                q.y+=math.sin(math.pi*u)*t**4*.38
                iw=mixw(weight(wn[0]),weight('root'),t)
                row.append(S.vertex(q,(u,t),mixw(iw,fingerw(3,t),u)))
            grid.append(row)
        for a,b in zip(grid,grid[1:]):
            for k in range(16): S.face((a[k],b[k],b[k+1],a[k+1]),COPPER)
        # Front patagium closes the shoulder-elbow-wrist/hub strip with matched weights.
        for a,b,c,wa,wb,wc in [(shoulder,elbow,hub,weight(wn[0]),weight(wn[1]),weight(wn[2]))]:
            ids=[S.vertex(q,uv,w) for q,uv,w in [(a,(0,0),wa),(b,(.5,1),wb),(c,(1,0),wc)]];S.face(ids,COPPER)

def setup_preview():
    scene=bpy.context.scene
    coll=bpy.data.collections.get('Embercrest Presentation')
    if coll:
        for obj in list(coll.objects): bpy.data.objects.remove(obj,do_unlink=True)
        bpy.data.collections.remove(coll)
    coll=bpy.data.collections.new('Embercrest Presentation');scene.collection.children.link(coll)
    floor_mesh=bpy.data.meshes.new('Embercrest studio floor')
    floor_mesh.from_pydata([(-200,-200,0),(200,-200,0),(200,200,0),(-200,200,0)],[],[(0,1,2,3)])
    floor=bpy.data.objects.new('Embercrest studio floor',floor_mesh);coll.objects.link(floor)
    floor_mat=bpy.data.materials.new('Embercrest studio matte');floor_mat.diffuse_color=(.07,.085,.105,1);floor_mat.use_nodes=True
    floor_mat.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value=(.07,.085,.105,1)
    floor_mat.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value=.95
    floor_mesh.materials.append(floor_mat)
    world=bpy.data.worlds.new('Embercrest Studio');world.use_nodes=True;world.node_tree.nodes['Background'].inputs[0].default_value=(.16,.19,.23,1);world.node_tree.nodes['Background'].inputs[1].default_value=.5;scene.world=world
    def area(name,loc,energy,size,color):
        dat=bpy.data.lights.new(name,'AREA');dat.energy=energy;dat.shape='DISK';dat.size=size;dat.color=color
        obj=bpy.data.objects.new(name,dat);coll.objects.link(obj);obj.location=loc;obj.rotation_euler=(Vector((0,0,2))-obj.location).to_track_quat('-Z','Y').to_euler()
    area('Warm key',(5,8,12),1900,8,(1,.81,.63))
    area('Cool fill',(-7,3,7),1400,7,(.66,.78,1))
    area('Copper rim',(1,-8,9),2400,6,(1,.53,.27))
    cam=bpy.data.cameras.new('Embercrest Camera');obj=bpy.data.objects.new('Embercrest Camera',cam);coll.objects.link(obj);scene.camera=obj
    obj.location=(12,16,14);target=Vector((0,-.7,2.5));obj.rotation_euler=(target-obj.location).to_track_quat('-Z','Y').to_euler();cam.type='ORTHO';cam.ortho_scale=18.5;cam.lens=48
    scene.render.engine='CYCLES';scene.cycles.samples=32;scene.cycles.use_denoising=True
    scene.render.resolution_x=1600;scene.render.resolution_y=1200;scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG';scene.render.film_transparent=False
    scene.view_settings.view_transform='AgX'
    for a in bpy.context.screen.areas if bpy.context.screen else []:
        if a.type=='VIEW_3D':
            a.spaces.active.region_3d.view_distance=18
            a.spaces.active.region_3d.view_location=(0,-.5,2.6)
            a.spaces.active.region_3d.view_rotation=obj.rotation_euler.to_quaternion()
            a.spaces.active.shading.type='MATERIAL'

def finish():
    global BODY, ARM
    BODY=bpy.data.objects['Embercrest'];ARM=bpy.data.objects['Embercrest_Rig']
    scene=bpy.context.scene;scene.frame_set(1)
    # Tangents require triangles; triangulate at export without changing source mesh.
    tri=BODY.modifiers.new('Export triangulation','TRIANGULATE');tri.quad_method='BEAUTY'
    bpy.ops.object.select_all(action='DESELECT');BODY.select_set(True);ARM.select_set(True);bpy.context.view_layer.objects.active=ARM
    bpy.ops.export_scene.gltf(filepath=str(ROOT/'assets/embercrest-scripted.glb'),export_format='GLB',use_selection=True,
        export_apply=True,export_animations=True,export_skins=True,export_def_bones=True,
        export_tangents=True,export_yup=True,export_image_format='AUTO',export_keep_originals=False,use_active_scene=True)
    BODY.modifiers.remove(tri)
    setup_preview()
    versions=bpy.context.preferences.filepaths.save_version
    bpy.context.preferences.filepaths.save_version=0
    try: bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'source/embercrest.blend'),compress=True)
    finally: bpy.context.preferences.filepaths.save_version=versions
    stats={'vertices':len(BODY.data.vertices),'polygons':len(BODY.data.polygons),
           'triangles':sum(len(p.vertices)-2 for p in BODY.data.polygons),
           'bones':len(ARM.data.bones),'materials':len(BODY.data.materials),
           'max_vertex_influences':max(len(v.groups) for v in BODY.data.vertices),
           'source':'Original procedural geometry and generated PBR textures; no reference asset geometry reused',
           'forward':'glTF -Z','up':'glTF +Y','revision':2,
           'texture_sizes':{'skin':2048,'membrane':2048,'horn_and_armour':1024,'details':256},
           'neck_rise':float(ARM.data.bones['head'].head_local.z-ARM.data.bones['neck_01'].head_local.z)}
    (CAP/'build_stats.json').write_text(json.dumps(stats,indent=2)+'\n')
    lo=[min(v.co[i] for v in BODY.data.vertices) for i in range(3)]
    hi=[max(v.co[i] for v in BODY.data.vertices) for i in range(3)]
    ground=(hi[2]-lo[2])*.5*19/(hi[0]-lo[0])
    (ROOT/'assets/embercrest-scripted.glb.flight.cfg').write_text(
        '# Embercrest: centered skinned bounds at the engine 19 m wingspan.\n'
        '# Resting clearance to the sole, plus 2 cm; no handling overrides.\n'
        f'ground_offset {ground+.02:.6f}\n')
    print(json.dumps(stats))

if __name__=='__main__' and '--build' in sys.argv:
    build();finish()
