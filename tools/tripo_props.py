"""Convert paid-credit Tripo props through Blender."""
import importlib.util
import math
from pathlib import Path
import sys
import bpy

ROOT = Path(__file__).resolve().parents[1]
SOURCES = {'watchtower':'tripo-watchtower-01.glb', 'spire_tower':'tripo-spire-01.glb',
           'hoard_pile':'tripo-hoard-01.glb', 'hoard_trove':'tripo-hoard-02.glb',
           'ward_stone':'tripo-wardstone-02.glb'}
REJECTED = {
    'hoard_pile': 'Pass 2 normal projection left 0.20% uncovered. Keep the procedural prop.',
    'hoard_trove': 'Pass 2 retained stretched wood grain and crumpled shading. Keep the procedural prop.',
    'ward_stone': 'Pass 2 retained stretched tips and crumpled shading. Keep the procedural prop.',
}

DIMS = {'watchtower':(11,11,40),'spire_tower':(7,7,44),
        'hoard_pile':(16,16,5.5),'hoard_trove':(14,14,4),'ward_stone':(11.6,11.5,14.85)}

def activate(obj):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active=obj

def import_source(path):
    bpy.ops.import_scene.gltf(filepath=str(path))
    meshes=[o for o in bpy.context.scene.objects if o.type=='MESH']
    bpy.ops.object.select_all(action='DESELECT')
    for o in meshes: o.select_set(True)
    bpy.context.view_layer.objects.active=meshes[0]
    bpy.ops.object.join()
    obj=bpy.context.object
    bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
    return obj

def fit(obj,name):
    import numpy as np
    coords=np.empty(len(obj.data.vertices)*3,dtype=np.float32)
    obj.data.vertices.foreach_get('co',coords)
    coords=coords.reshape(-1,3)
    low=coords.min(axis=0);high=coords.max(axis=0)
    factors=np.array(DIMS[name])/(high-low)
    offset=-(high+low)/2
    if name!='ward_stone': offset[2]=-low[2]
    coords=(coords+offset)*factors
    obj.data.vertices.foreach_set('co',coords.astype(np.float32).ravel())
    print('FIT',name,'source offset',offset,'scale',factors,flush=True)
    return obj

def candidates():
    spec=importlib.util.spec_from_file_location('props',ROOT/'tools/build_props.py')
    props=importlib.util.module_from_spec(spec);spec.loader.exec_module(props)
    for path in sorted((ROOT/'assets/tripo-raw').glob('tripo-*.glb')):
        if not any(s in path.name for s in ('watchtower','spire','hoard','wardstone')): continue
        if (ROOT/'artifacts/props/v3/candidates'/(path.stem+'_near.png')).exists(): continue
        name=next(n for n,s in SOURCES.items() if s.split('-')[1]==path.name.split('-')[1])
        if path.name=='tripo-hoard-02.glb': name='hoard_trove'
        props.clear();obj=fit(import_source(path),name)
        props.render(name,obj,ROOT/'artifacts/props/v3/candidates',path.stem,True)

def build(name, source_dir):
    """Bake source colour, surface normals, and metal response onto a new atlas."""
    import numpy as np
    spec=importlib.util.spec_from_file_location('rockkit',ROOT/'tools/build_rockkit.py')
    kit=importlib.util.module_from_spec(spec);sys.modules['rockkit']=kit;spec.loader.exec_module(kit)
    src=fit(import_source(source_dir/SOURCES[name]),name)
    detail=None
    if name=='watchtower': prepare_watchtower(src)
    if name=='spire_tower': detail=prepare_spire(src,kit)
    if name=='ward_stone': prepare_ward(src)
    low=kit.make_lod0(src,7800 if name=='ward_stone' else 19800)
    if detail is not None:
        import bmesh
        bm=bmesh.new();bm.from_mesh(low.data)
        bmesh.ops.delete(bm,geom=[f for f in bm.faces if f.material_index>0],context='FACES')
        bm.to_mesh(low.data);bm.free()
        activate(low);detail.select_set(True);bpy.ops.object.join()
    if name in ('hoard_pile','hoard_trove','spire_tower','ward_stone'):
        for p in low.data.polygons: p.use_smooth=True
        for e in low.data.edges: e.use_edge_sharp=False
        low.data.normals_split_custom_set([(0,0,0)]*len(low.data.loops))
    kit.unwrap(low)
    low.name=name
    mat=bpy.data.materials.new(name+'_tripo');mat.use_nodes=True
    low.data.materials.clear();low.data.materials.append(mat)
    for p in low.data.polygons: p.material_index=0
    nt=mat.node_tree;shader=nt.nodes.get('Principled BSDF')
    target=nt.nodes.new('ShaderNodeTexImage');nt.nodes.active=target
    scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=4
    size=1024 if name=='ward_stone' else 2048
    source_mats=[]
    for m in src.data.materials:
        bs=next(n for n in m.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
        output=next(n for n in m.node_tree.nodes if n.type=='OUTPUT_MATERIAL')
        source_mats.append((m,bs,output))
    baked={}
    for kind in ('basecolor','normal','metallic_roughness'):
        for m,bs,output in source_mats:
            tree=m.node_tree
            if kind=='normal': tree.links.new(bs.outputs['BSDF'],output.inputs['Surface'])
            else:
                emit=tree.nodes.new('ShaderNodeEmission')
                if kind=='basecolor':
                    inp=bs.inputs['Base Color']
                    if inp.links: tree.links.new(inp.links[0].from_socket,emit.inputs['Color'])
                    else: emit.inputs['Color'].default_value=inp.default_value
                else:
                    combine=tree.nodes.new('ShaderNodeCombineColor')
                    combine.inputs['Red'].default_value=1
                    for channel,key in [('Green','Roughness'),('Blue','Metallic')]:
                        inp=bs.inputs[key]
                        if inp.links: tree.links.new(inp.links[0].from_socket,combine.inputs[channel])
                        else: combine.inputs[channel].default_value=inp.default_value
                    tree.links.new(combine.outputs[0],emit.inputs['Color'])
                tree.links.new(emit.outputs[0],output.inputs['Surface'])
        img=bpy.data.images.new(name+'_'+kind,size,size,alpha=False)
        img.colorspace_settings.name='sRGB' if kind=='basecolor' else 'Non-Color'
        target.image=img;nt.nodes.active=target
        activate(low);src.select_set(True)
        bpy.ops.object.bake(type='NORMAL' if kind=='normal' else 'EMIT',
            normal_space='TANGENT',use_selected_to_active=True,
            cage_extrusion=.3 if name.startswith('hoard') else .12,
            max_ray_distance=1.0 if name.startswith('hoard') else .5,margin=16,use_clear=True)
        if kind=='normal' and (name.startswith('hoard') or name=='ward_stone'):
            trials=[]
            longest=kit.np_from_image(img)[...,:3]*2-1
            for extrusion in ((.03,.105) if name.startswith('hoard') else (.012,.042)):
                bpy.ops.object.bake(type='NORMAL',normal_space='TANGENT',
                    use_selected_to_active=True,cage_extrusion=extrusion,
                    max_ray_distance=extrusion*2.5,margin=16,use_clear=True)
                trials.append(kit.np_from_image(img)[...,:3]*2-1)
            mask=kit.np_from_image(baked['basecolor'])[...,:3].max(axis=2)>1e-6
            normals,stats=kit.combine_normal_trials(trials+[longest],mask)
            rgba=np.ones((size,size,4),dtype=np.float32)
            rgba[...,:3]=normals*.5+.5
            img.pixels.foreach_set(rgba.ravel())
            print('NORMAL PROJECTION',name,stats,flush=True)
        img.pack();baked[kind]=img
    target.image=baked['basecolor'];nt.links.new(target.outputs['Color'],shader.inputs['Base Color'])
    normal=nt.nodes.new('ShaderNodeTexImage');normal.image=baked['normal']
    normalmap=nt.nodes.new('ShaderNodeNormalMap');nt.links.new(normal.outputs['Color'],normalmap.inputs['Color'])
    nt.links.new(normalmap.outputs['Normal'],shader.inputs['Normal'])
    mr=nt.nodes.new('ShaderNodeTexImage');mr.image=baked['metallic_roughness']
    separate=nt.nodes.new('ShaderNodeSeparateColor');nt.links.new(mr.outputs['Color'],separate.inputs[0])
    nt.links.new(separate.outputs['Green'],shader.inputs['Roughness'])
    nt.links.new(separate.outputs['Blue'],shader.inputs['Metallic'])
    atlas=next(layer for layer in low.data.uv_layers if layer.active_render)
    for layer in list(low.data.uv_layers):
        if layer!=atlas: low.data.uv_layers.remove(layer)
    bpy.data.objects.remove(src,do_unlink=True)
    activate(low)
    bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
    return low


def prepare_spire(src,kit):
    """Replace the crystal and its socket with an empty iron cradle."""
    import build_props as props
    iron=bpy.data.materials.new('Cradle iron');iron.use_nodes=True
    shader=iron.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value=(.075,.06,.045,1)
    shader.inputs['Metallic'].default_value=.8
    shader.inputs['Roughness'].default_value=.48
    src.data.materials.append(iron)
    kit.cut(src,(0,0,36.7),(0,0,1),True)
    for v in src.data.vertices: v.co.z*=39.3/36.7
    g=props.Geometry()
    g.lathe((0,0,0),[(.8,38.8),(1.3,39.2),(1.1,39.7)],0,16)
    for i in range(4):
        a=i*math.tau/4+math.pi/4
        profile=[(.85,39.3),(1.65,40.2),(2.15,41.7),(1.95,43),(1.35,44)]
        for j,((r,z),(r2,z2)) in enumerate(zip(profile,profile[1:])):
            props.beam(g,(r*math.cos(a),r*math.sin(a),z),(r2*math.cos(a),r2*math.sin(a),z2),.62-j*.075,0)
    top=max(v[2] for v in g.vertices)
    g.vertices=[(x,y,39.3+(z-39.3)*(44-39.3)/(top-39.3)) for x,y,z in g.vertices]
    cradle=g.mesh('Empty iron cradle',iron)
    preserved=cradle.copy();preserved.data=cradle.data.copy()
    bpy.context.collection.objects.link(preserved)
    activate(src);cradle.select_set(True);bpy.ops.object.join()
    print('SPIRE: removed source above 36.7 m; stretched shaft to 39.3 m; added empty cradle at (0,42,0)',flush=True)
    return preserved


def prepare_ward(src):
    """Keep the empty socket and taper shard shoulders inside the radius limit."""
    for v in src.data.vertices:
        p=v.co
        if p.length<4.45: p*=4.45/p.length
        radial=math.hypot(p.x,p.y)
        limit=7.45
        maximum=math.sqrt(max(.01,limit*limit-p.z*p.z))
        if radial>maximum:
            p.x*=maximum/radial;p.y*=maximum/radial
    for p in src.data.polygons: p.use_smooth=True
    for e in src.data.edges: e.use_edge_sharp=False
    src.data.normals_split_custom_set([(0,0,0)]*len(src.data.loops))
    print('WARD: expanded inner vertices to radius 4.45; tapered all shoulders and tips to radius 7.45',flush=True)

def prepare_watchtower(src):
    """Move the roof rearward and add the front flame cradle."""
    import build_props as props
    for v in src.data.vertices:
        if v.co.z>31: v.co.y+=min(1,(v.co.z-31)/2)*.9
    iron=bpy.data.materials.new('Brazier iron');iron.use_nodes=True
    shader=iron.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value=(.06,.045,.03,1)
    shader.inputs['Metallic'].default_value=.8
    shader.inputs['Roughness'].default_value=.55
    g=props.Geometry()
    g.lathe((0,-2,0),[(.25,33.8),(.8,34.3),(.95,34.6),(.85,34.65)],0,16,cap=False)
    props.beam(g,(0,0,33.6),(0,-2,33.8),.32,0)
    for i in range(4):
        a=i*math.tau/4
        props.beam(g,(.9*math.cos(a),-2+.9*math.sin(a),34.5),
                   (1.0*math.cos(a),-2+1.0*math.sin(a),35.5),.16,0)
    cradle=g.mesh('Empty flame cradle',iron)
    activate(src);cradle.select_set(True);bpy.ops.object.join()
    print('WATCHTOWER: moved roof rearward by up to 0.9 m; added empty iron cradle at (0,35.2,2)',flush=True)


if __name__=='__main__':
    candidates()
