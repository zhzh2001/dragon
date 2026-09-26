#!/usr/bin/env python3
"""Blender 5.2: deterministic Mossback LOD, rotation clips, raw GLB verification.
blender -b --factory-startup --python tools/build_grazer.py -- [--verify-only]
"""
import bpy, bmesh, numpy as np, math, json, struct, zlib, sys
from pathlib import Path
from mathutils import Matrix, Vector, Quaternion
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'assets/props/grazer.glb'
ART=ROOT/'artifacts/prey'
TAU=2*math.pi
DUR={'graze':4.,'walk':1.2,'run':.55}
def png(a):
    h,w,_=a.shape
    def chunk(t,d): return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(b'\0'+r.tobytes() for r in a),9))+chunk(b'IEND',b'')
def rx(t):
    c,s=math.cos(t),math.sin(t); return np.array([[1,0,0],[0,c,-s],[0,s,c]])
def mat(t,q):
    m=np.eye(4); m[:3,:3]=np.array(Quaternion((q[3],*q[:3])).to_matrix()); m[:3,3]=t; return m
class GLB:
    def __init__(self,path):
        b=path.read_bytes(); n=struct.unpack_from('<I',b,12)[0]; self.j=json.loads(b[20:20+n]); self.b=b[28+n:]
    def acc(self,i):
        a=self.j['accessors'][i]; v=self.j['bufferViews'][a['bufferView']]; typ={5126:'<f4',5123:'<u2',5125:'<u4',5121:'u1'}[a['componentType']]; k={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']]
        return np.frombuffer(self.b,dtype=typ,count=a['count']*k,offset=v.get('byteOffset',0)+a.get('byteOffset',0)).reshape(-1,k).copy()
def verify():
    g=GLB(OUT); j=g.j
    assert len(j['meshes'])==len(j['skins'])==len(j['materials'])==1
    p=j['meshes'][0]['primitives']; assert len(p)==1; p=p[0]; a=p['attributes']; pos=g.acc(a['POSITION']); weights=g.acc(a['WEIGHTS_0']); joints=g.acc(a['JOINTS_0']).astype(int)
    tri=g.acc(p['indices']).size//3; assert tri<=9000
    lo,hi=pos.min(0),pos.max(0); dim=hi-lo
    assert 4.45<dim[2]<4.55 and 2.8<dim[1]<3.8 and 1.5<dim[0]<2.6 and abs(lo[1])<.015,(lo,hi)
    assert abs((lo[0]+hi[0])/2)<.03 and abs((lo[2]+hi[2])/2)<.4
    sk=j['skins'][0]; ids=sk['joints']; names=[j['nodes'][i]['name'] for i in ids]; spec=json.loads((ROOT/'tools/skeletons/grazing-quadruped.json').read_text()); expected={r[0]:r[3] for r in spec['bones']}; assert set(names)==set(expected)
    parents={c:i for i,n in enumerate(j['nodes']) for c in n.get('children',[])}
    for i,name in zip(ids,names): assert (j['nodes'][parents[i]]['name'] if i in parents and parents[i] in ids else None)==expected[name]
    ib=g.acc(sk['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
    def worlds(qs={}):
        world={}
        def one(i):
            n=j['nodes'][i]; m=mat(n.get('translation',[0,0,0]),qs.get(i,n.get('rotation',[0,0,0,1]))); assert n.get('scale',[1,1,1])==[1,1,1]
            world[i]=one(parents[i])@m if i in parents and parents[i] not in world else (world[parents[i]]@m if i in parents else m); return world[i]
        for i in range(len(j['nodes'])):
            if i not in world: one(i)
        return np.array([world[i] for i in ids])
    ph=np.c_[pos,np.ones(len(pos))]
    def deform(w): return np.einsum('vk,vkij,vj->vi',weights,(w@ib)[joints],ph)[:,:3]
    bind_world=worlds(); assert bind_world[names.index('head'),2,3]>bind_world[names.index('root'),2,3] and bind_world[names.index('head'),2,3]>bind_world[names.index('tail_05'),2,3]
    err=np.max(np.linalg.norm(deform(bind_world)-pos,axis=1)); assert err<1e-5,err
    assert np.max(abs(weights.sum(1)-1))<1e-5
    image=j['images'][0]; assert image['mimeType']=='image/png' and 'bufferView' in image
    v=j['bufferViews'][image['bufferView']]; blob=g.b[v['byteOffset']:v['byteOffset']+v['byteLength']]; assert struct.unpack_from('>II',blob,16)==(1024,1024)
    assert len(j['animations'])==3 and set(x['name'] for x in j['animations'])==set(DUR)
    for i in range(len(j['accessors'])): assert np.isfinite(g.acc(i)).all(),i
    masks={}
    for side in ('l','r'):
        for end in ('hand','foot'):
            idx=names.index(end+'_'+side); influence=(weights*(joints==idx)).sum(1)
            mask=(influence>.4)&(pos[:,1]<.24); assert mask.sum()>4; masks[end+'_'+side]=mask
    report=[f'Dimensions X/Y/Z: {dim[0]:.4f} / {dim[1]:.4f} / {dim[2]:.4f} m',f'Triangles: {tri}; joints: {len(ids)}; bind maximum error: {err:.9f} m']
    for anim in j['animations']:
        tracks=[]
        assert anim['channels'] and anim['samplers']
        for c in anim['channels']:
            assert c['target']['path']=='rotation' and c['target']['node'] in ids; s=anim['samplers'][c['sampler']]; assert s['interpolation']=='LINEAR'; ts=g.acc(s['input']).ravel(); q=g.acc(s['output']); assert ts[0]==0 and np.all(np.diff(ts)>0) and len(ts)==len(q); assert np.array_equal(q[0],q[-1]); assert abs(ts[-1]-DUR[anim['name']])<1e-6; assert np.max(abs(np.linalg.norm(q,axis=1)-1))<1e-5; tracks.append((c['target']['node'],ts,q))
        minimum=99.; contact=0.; stance_gap=0.
        for t in np.linspace(0,DUR[anim['name']],241):
            qs={}
            for i,ts,q in tracks:
                k=min(np.searchsorted(ts,t,side='right')-1,len(ts)-2); f=(t-ts[k])/(ts[k+1]-ts[k]); qa,qb=q[k],q[k+1]; dot=np.dot(qa,qb); qb=qb if dot>=0 else -qb; dot=abs(dot)
                if dot>.9995: v=(1-f)*qa+f*qb
                else:
                    theta=np.arccos(np.clip(dot,-1,1)); v=(np.sin((1-f)*theta)*qa+np.sin(f*theta)*qb)/np.sin(theta)
                qs[i]=v/np.linalg.norm(v)
            mesh=deform(worlds(qs)); feet=[mesh[m,1].min() for m in masks.values()]; minimum=min(minimum,*feet); contact=max(contact,min(feet))
            if anim['name']=='walk':
                for key,height in zip(masks,feet):
                    side=key[-1]; front=key.startswith('hand'); offset={('l',False):0,('l',True):.25,('r',False):.5,('r',True):.75}[(side,front)]
                    if (t/DUR['walk']-offset)%1<.66: stance_gap=max(stance_gap,height)
            elif anim['name']=='graze': stance_gap=max(stance_gap,*feet)
        if anim['name']!='run': assert minimum>=-.05 and contact<.065 and stance_gap<.065,(anim['name'],minimum,contact,stance_gap)
        report.append(f"{anim['name']}: {DUR[anim['name']]:.2f} s; 241 sampled poses; sole min {minimum:.4f} m; worst nearest sole {contact:.4f} m; stance gap {format(stance_gap,'.4f')+' m' if anim['name']!='run' else 'N/A'}; rotation-only/loop PASS")
    report.append('PASS: bounds, budget, mesh/skin/material, hierarchy, embedded 1024 texture, bind reproduction, normalized weights, clips, rotations, exact loops, interpolated sole contact.')
    print('\n'.join(report)); return report

def build():
    bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.gltf(filepath=str(ROOT/'assets/mossback.glb'))
    arm=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE'); mesh=next(o for o in bpy.context.scene.objects if o.type=='MESH' and any(m.type=='ARMATURE' for m in o.modifiers))
    for o in list(bpy.context.scene.objects):
        if o not in (arm,mesh): bpy.data.objects.remove(o,do_unlink=True)
    mesh.name='Grazer'; bpy.context.view_layer.objects.active=mesh
    for m in list(mesh.modifiers): mesh.modifiers.remove(m)
    bm=bmesh.new(); bm.from_mesh(mesh.data); before=len(bm.verts); bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=0.00001); bm.to_mesh(mesh.data); bm.free(); mesh.data.update(); print('Weld',before,'to',len(mesh.data.vertices),flush=True)
    dec=mesh.modifiers.new('Silhouette LOD','DECIMATE'); dec.ratio=.106; dec.use_collapse_triangulate=True; bpy.ops.object.modifier_apply(modifier=dec.name)
    for polygon in mesh.data.polygons: polygon.use_smooth=True
    # Decimator interpolates source vertex groups along its edge collapses; retain strongest four.
    for v in mesh.data.vertices:
        w=sorted([(g.group,g.weight) for g in v.groups],key=lambda x:-x[1])[:4]; total=sum(x[1] for x in w)
        for group in mesh.vertex_groups: group.remove([v.index])
        for i,value in w: mesh.vertex_groups[i].add([v.index],value/total,'REPLACE')
    coords=np.array([v.co[:] for v in mesh.data.vertices]); scale=4.5/(coords[:,1].max()-coords[:,1].min()); offset=np.array([0,-.055,coords[:,2].min()]); coords=(coords-offset)*scale
    for v,c in zip(mesh.data.vertices,coords): v.co=c
    # Transform rest armature identically, with no residual object transform.
    transform=Matrix.Diagonal((scale,scale,scale,1)); transform.translation=Vector(-offset*scale); arm.data.transform(transform)
    bones=list(arm.data.bones); names=[b.name for b in bones]; parent=[names.index(b.parent.name) if b.parent else -1 for b in bones]; rest=np.array([np.array(b.matrix_local) for b in bones]); local=np.array([np.linalg.inv(rest[p])@r if p>=0 else r for p,r in zip(parent,rest)])
    image=next(n.image for n in mesh.data.materials[0].node_tree.nodes if n.type=='TEX_IMAGE' and n.image and 'normal' not in n.image.name and 'metallic' not in n.image.name)
    image.scale(1024,1024); pixels=np.array(image.pixels[:]).reshape(1024,1024,4); pixels[:,:,:3]*=.72; image.pixels[:]=pixels.ravel(); tex=png((np.clip(pixels[::-1],0,1)*255+.5).astype(np.uint8))
    matl=bpy.data.materials.new('Mossback matte'); matl.use_nodes=True; bs=matl.node_tree.nodes.get('Principled BSDF'); bs.inputs['Roughness'].default_value=.88; node=matl.node_tree.nodes.new('ShaderNodeTexImage'); node.image=image; matl.node_tree.links.new(node.outputs['Color'],bs.inputs['Base Color']); mesh.data.materials.clear(); mesh.data.materials.append(matl)
    # Foot soles are weighted rigidly to their existing terminal bones, avoiding toe splay.
    for v in mesh.data.vertices:
        if v.co.z<.25:
            side='l' if v.co.x<0 else 'r'; end='hand' if v.co.y>-.3 else 'foot'; group=mesh.vertex_groups.get(end+'_'+side)
            for gr in mesh.vertex_groups: gr.remove([v.index])
            group.add([v.index],1.,'REPLACE')
    weight=np.zeros((len(coords),len(bones)))
    for v in mesh.data.vertices:
        for g in v.groups:
            name=mesh.vertex_groups[g.group].name
            if name in names: weight[v.index,names.index(name)]=g.weight
    inv=np.linalg.inv(rest); homogeneous=np.c_[coords,np.ones(len(coords))]
    def forward(angles,sideangles=None):
        world=[]
        for i in range(len(bones)):
            # Apply rotation in original body frame, conjugated into the rest local basis.
            delta=np.eye(4); delta[:3,:3]=rest[i,:3,:3].T@rx(angles[i])@rest[i,:3,:3]
            if sideangles is not None:
                q=Quaternion(Vector(rest[i,:3,:3].T@np.array([0,0,1])),sideangles[i]); delta[:3,:3]=delta[:3,:3]@np.array(q.to_matrix())
            m=local[i]@delta; world.append(world[parent[i]]@m if parent[i]>=0 else m)
        return np.array(world)
    chains=[]
    for side in ('l','r'):
        for stems in [('upper_arm','forearm','wrist','hand'),('thigh','shin','ankle','foot')]:
            ids=[names.index(x+'_'+side) for x in stems]; mask=(weight[:,ids[-1]]>.4)&(coords[:,2]<.24); chains.append((side,stems[0],ids,mask))
    def solve(angles,ids,target):
        for it in range(18):
            w=forward(angles)
            if np.linalg.norm(w[ids[-1],:3,3]-target)<.0002: break
            for idx in reversed(ids[:-1]):
                w=forward(angles); pivot=w[idx,:3,3]; a=w[ids[-1],:3,3]-pivot; b=target-pivot
                d=math.atan2(a[1]*b[2]-a[2]*b[1],a[1]*b[1]+a[2]*b[2]); angles[idx]+=np.clip(d,-.18,.18)
        # Horizontal sole: cancel inherited sagittal rotations, preserving rest orientation.
        idx=ids[-1]; p=parent[idx]; total=0
        while p>=0: total+=angles[p]; p=parent[p]
        angles[idx]=-total
    clips={}
    for name,duration in DUR.items():
        count={'graze':121,'walk':73,'run':67}[name]; frames=[]
        for f in range(count):
            phase=f/(count-1) if f<count-1 else 0.; a=np.zeros(len(bones)); lateral=np.zeros(len(bones)); s=math.sin(TAU*phase)
            if name=='graze':
                # Mostly grazing, with a slow lift and settle to check its surroundings.
                lower=.5-.5*math.cos(TAU*phase); a[names.index('chest')]=math.radians(-20)*lower
                for n in ('neck_01','neck_02','neck_03'): a[names.index(n)]=math.radians(-19)*lower
                a[names.index('head')]=math.radians(42)*lower; a[names.index('jaw')]=math.radians(2.0)*(1+math.sin(TAU*phase*8))*lower
                lateral[names.index('head')]=.035*math.sin(TAU*phase*3)*lower
            elif name=='walk':
                a[names.index('chest')]=.018*math.sin(TAU*phase*2)
                for n in ('neck_01','neck_02'): a[names.index(n)]=.035*math.sin(TAU*phase*2+.4)
            else:
                a[names.index('chest')]=.10*math.sin(TAU*phase)
                for n in ('neck_01','neck_02','neck_03'): a[names.index(n)]=.09+.025*s
                a[names.index('head')]=-.05
            for k in range(1,6):
                lateral[names.index('tail_%02d'%k)]=.13*math.sin(TAU*phase-.5*k)
                if name=='run': a[names.index('tail_%02d'%k)]=-.16
            for side,stem,ids,mask in chains:
                front=stem=='upper_arm'; shift=0.
                if name=='walk':
                    # Hind left, fore left, hind right, fore right: lateral four-beat.
                    ph=(phase-({('l',False):0,('l',True):.25,('r',False):.5,('r',True):.75}[(side,front)]))%1
                    stance=.66
                    if ph<stance: stride=.32*(1-2*ph/stance); lift=0
                    else:
                        u=(ph-stance)/(1-stance); stride=-.32*math.cos(math.pi*u); lift=.25*math.sin(math.pi*u)
                elif name=='run':
                    ph=(phase-(.02 if side=='r' else 0)-(.49 if front else 0))%1
                    stance=.28
                    if ph<stance: stride=.48*(1-2*ph/stance); lift=0
                    else:
                        u=(ph-stance)/(1-stance); stride=-.48*math.cos(math.pi*u); lift=.48*math.sin(math.pi*u)
                else: stride=.035*s; lift=0
                target=rest[ids[-1],:3,3].copy(); target[1]+=stride; target[2]+=lift
                for correction in range(4):
                    solve(a,ids,target); w=forward(a); skin=w@inv; deformed=np.einsum('vj,jab,vb->va',weight[mask],skin,homogeneous[mask]); minz=deformed[:,2].min(); target[2]+=(lift+.008-minz)
            w=forward(a,lateral); qs=[]
            for i in range(len(bones)):
                m=np.linalg.inv(w[parent[i]])@w[i] if parent[i]>=0 else w[i]
                q=Matrix(m.tolist()).to_quaternion(); qs.append([q.x,q.y,q.z,q.w])
            frames.append(qs)
        frames[-1]=frames[0]; clips[name]=np.array(frames)
        print('Baked',name,flush=True)
    # Serialize canonical Y-up, +Z-forward coordinates using a proper rotation.
    C=np.array([[-1,0,0],[0,0,1],[0,1,0]]); C4=np.eye(4); C4[:3,:3]=C
    binary=bytearray(); views=[]; access=[]
    def view(blob):
        while len(binary)%4: binary.append(0)
        i=len(views); views.append({'buffer':0,'byteOffset':len(binary),'byteLength':len(blob)}); binary.extend(blob); return i
    def acc(data,typ='VEC3',comp=5126):
        data=np.asarray(data,dtype={5126:'<f4',5123:'<u2',5125:'<u4'}[comp]); i=len(access); d={'bufferView':view(data.tobytes()),'componentType':comp,'count':len(data),'type':typ}
        if typ!='MAT4': d.update(min=data.reshape(len(data),-1).min(0).tolist(),max=data.reshape(len(data),-1).max(0).tolist())
        access.append(d); return i
    mesh.data.calc_loop_triangles(); pp=[]; nn=[]; uv=[]; jj=[]; ww=[]
    for tri in mesh.data.loop_triangles:
        for li in tri.loops:
            loop=mesh.data.loops[li]; vi=loop.vertex_index; pp.append(C@coords[vi]); nn.append(C@np.array(mesh.data.vertices[vi].normal)); uv.append([mesh.data.uv_layers.active.data[li].uv.x,1-mesh.data.uv_layers.active.data[li].uv.y]); inds=np.argsort(-weight[vi])[:4]; jj.append(inds); ww.append(weight[vi,inds])
    prim={'attributes':{'POSITION':acc(pp),'NORMAL':acc(nn),'TEXCOORD_0':acc(uv,'VEC2'),'JOINTS_0':acc(jj,'VEC4',5123),'WEIGHTS_0':acc(ww,'VEC4')},'indices':acc(np.arange(len(pp)).reshape(-1,1),'SCALAR',5125),'material':0}
    nodes=[]
    for i,b in enumerate(bones):
        m=C4@local[i]@C4.T; q=Matrix(m.tolist()).to_quaternion(); node={'name':b.name,'translation':m[:3,3].tolist(),'rotation':[q.x,q.y,q.z,q.w]}; children=[k for k,p in enumerate(parent) if p==i]
        if children: node['children']=children
        nodes.append(node)
    nodes.append({'name':'Grazer','mesh':0,'skin':0})
    animations=[]
    for name,frames in clips.items():
        time=acc(np.linspace(0,DUR[name],len(frames)).reshape(-1,1),'SCALAR'); samplers=[]; channels=[]
        for i in range(len(bones)):
            qout=[]
            for q in frames[:,i]:
                m=C@np.array(Quaternion((q[3],*q[:3])).to_matrix())@C.T; v=Matrix(m.tolist()).to_quaternion(); qout.append([v.x,v.y,v.z,v.w])
            for k in range(1,len(qout)):
                if np.dot(qout[k-1],qout[k])<0: qout[k]=[-v for v in qout[k]]
            qout[-1]=qout[0]; samplers.append({'input':time,'output':acc(qout,'VEC4'),'interpolation':'LINEAR'}); channels.append({'sampler':i,'target':{'node':i,'path':'rotation'}})
        animations.append({'name':name,'samplers':samplers,'channels':channels})
    ib=acc(np.array([C4@m@C4.T for m in inv]).transpose(0,2,1).reshape(-1,16),'MAT4'); iv=view(tex)
    data={'asset':{'version':'2.0','generator':'Dragon Engine deterministic grazer builder'},'scene':0,'scenes':[{'nodes':[i for i,p in enumerate(parent) if p<0]+[len(bones)]}],'nodes':nodes,'meshes':[{'name':'Grazer','primitives':[prim]}],'skins':[{'joints':list(range(len(bones))),'inverseBindMatrices':ib,'skeleton':0}],'animations':animations,'materials':[{'name':'Mossback matte','pbrMetallicRoughness':{'baseColorTexture':{'index':0},'metallicFactor':0,'roughnessFactor':.88}}],'textures':[{'source':0}],'images':[{'mimeType':'image/png','bufferView':iv}],'bufferViews':views,'accessors':access,'buffers':[{'byteLength':len(binary)}]}
    js=json.dumps(data,separators=(',',':')).encode(); js+=b' '*((-len(js))%4); binary+=b'\0'*((-len(binary))%4); OUT.parent.mkdir(exist_ok=True); OUT.write_bytes(struct.pack('<III',0x46546c67,2,28+len(js)+len(binary))+struct.pack('<II',len(js),0x4e4f534a)+js+struct.pack('<II',len(binary),0x004e4942)+binary)
    verify()
    if '--build-only' not in sys.argv: render()

def render():
    # Re-import the deliverable, so preview exercises glTF UVs, skin and animation data.
    bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.gltf(filepath=str(OUT))
    mesh=next(o for o in bpy.context.scene.objects if o.type=='MESH')
    arm=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE')
    actions={a.name.split('.')[0]:a for a in bpy.data.actions}
    ART.mkdir(parents=True,exist_ok=True)
    selected=next((set(x.split('=',1)[1].split(',')) for x in sys.argv if x.startswith('--views=')),None)
    scene=bpy.context.scene; scene.render.engine='CYCLES'; scene.cycles.samples=24; scene.cycles.use_denoising=True; scene.render.resolution_x=900; scene.render.resolution_y=650; scene.render.resolution_percentage=100
    scene.world.color=(.22,.22,.22); scene.view_settings.view_transform='AgX'
    bpy.ops.mesh.primitive_plane_add(size=200); floor=bpy.context.object; floor.name='Preview ground'; m=bpy.data.materials.new('Ground'); m.diffuse_color=(.12,.15,.13,1); floor.data.materials.append(m)
    for loc,power,size in [((3,4,8),1600,6),((-4,-2,5),1000,5)]:
        bpy.ops.object.light_add(type='AREA',location=loc); o=bpy.context.object; o.data.energy=power; o.data.shape='DISK'; o.data.size=size
    bpy.ops.object.camera_add(location=(8,0,3.1)); cam=bpy.context.object; cam.data.type='ORTHO'; cam.data.ortho_scale=5.9; scene.camera=cam
    def aim(loc,target): cam.location=loc; cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler()
    def pose(name,phase):
        action=next(a for a in bpy.data.actions if a.name==name or a.name.startswith(name+'_') or a.name.startswith(name+'.'))
        arm.animation_data_create(); arm.animation_data.action=action
        if action.slots: arm.animation_data.action_slot=action.slots[0]
        for track in arm.animation_data.nla_tracks: track.mute=True
        scene.frame_set(int(phase*DUR[name]*scene.render.fps),subframe=phase*DUR[name]*scene.render.fps%1)
        bpy.context.view_layer.update()
    for name in ([] if '--details-only' in sys.argv or selected is not None else DUR):
        imgs=[]
        for k in range(8):
            pose(name,k/8); aim((8,0,2.35),(0,0,1.55)); scene.render.filepath=str(ART/f'{name}-{k:02d}.png'); bpy.ops.render.render(write_still=True)
            im=bpy.data.images.load(scene.render.filepath,check_existing=False); imgs.append(np.array(im.pixels[:]).reshape(650,900,4)); bpy.data.images.remove(im)
        sheet=np.zeros((1300,3600,4),dtype=np.float32)
        for k,im in enumerate(imgs): sheet[(1-k//4)*650:(2-k//4)*650,k%4*900:(k%4+1)*900]=im
        out=bpy.data.images.new(name+' sheet',3600,1300); out.pixels[:]=sheet.ravel(); out.filepath_raw=str(ART/f'{name}-sheet.png'); out.file_format='PNG'; out.save(); bpy.data.images.remove(out)
    if selected is None or 'grazer-three-quarter' in selected:
        pose('walk',0); aim((-7,-7,4.5),(0,0,1.5)); scene.render.filepath=str(ART/'grazer-three-quarter.png'); bpy.ops.render.render(write_still=True)
    for label,clip,phase,loc,target,ortho in [
        ('run-front','run',.375,(0,-9,2.5),(0,0,1.5),5.3),
        ('run-rear','run',.375,(0,9,2.5),(0,0,1.5),6.3),
        ('run-top','run',.375,(0,0,10),(0,0,0),7.8),
        ('graze-head','graze',.5,(6,-2.0,1.5),(0,-1.3,.8),2.5),
        ('walk-feet','walk',.125,(6,-3,1.9),(0,0,.45),4.4),
    ]:
        if selected is not None and label not in selected: continue
        pose(clip,phase); cam.data.ortho_scale=ortho; aim(loc,target); scene.render.filepath=str(ART/(label+'.png')); bpy.ops.render.render(write_still=True)


if __name__=='__main__':
    try:
        if '--render-only' in sys.argv or '--details-only' in sys.argv or any(x.startswith('--views=') for x in sys.argv): render()
        elif '--verify-only' not in sys.argv: build()
        verify()
    except Exception:
        import traceback, os
        traceback.print_exc(); sys.stdout.flush(); sys.stderr.flush(); os._exit(1)
