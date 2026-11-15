#!/usr/bin/env python3
"""Check hero attack exports with Python's standard library.

Angles are rest-relative model-axis rotation vectors, in degrees.
Timing allows one 30 fps frame for rounding and downstream overlap.
The checker reads exported keys independently of the authoring curves.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import struct

sys.dont_write_bytecode = True
from check_clip import read_glb, accessor, normalize, inverse, multiply, angle, rotation_vector, parent_names

ROOT = Path(__file__).resolve().parents[2]
EXPECTED = {'bite': .9, 'claw_r': .9, 'claw_l': .9, 'tail_r': 1., 'tail_l': 1.}
TOL = 1/30 + 1e-6


def check(source, clip_path):
    results, measurements = [], []
    def require(ok, label, detail=''):
        results.append({'pass': bool(ok), 'check': label, 'detail': detail})
    src, _ = read_glb(source)
    doc, binary = read_glb(clip_path)
    require(True, 'GLB loads', f'{Path(clip_path).stat().st_size} bytes')
    original = {src['nodes'][i]['name']:src['nodes'][i] for i in src['skins'][0]['joints']}
    nodes = {n['name']:n for n in doc['nodes']}
    require(len(doc['nodes']) == len(nodes) == len(original) == 77 and nodes.keys() == original.keys(),
            '77 unique model joints')
    require(not any(doc.get(k) for k in ('meshes','skins','materials','textures','images','extensionsRequired')),
            'armature alone', 'no mesh, skin, material, image, or required extension')
    require(len(doc['buffers']) == 1 and 'uri' not in doc['buffers'][0], 'embedded animation buffer')
    sp, cp = parent_names(src['nodes']), parent_names(doc['nodes'])
    require(all(cp.get(n) == (sp.get(n) if sp.get(n) in original else None) for n in nodes), 'model hierarchy')
    require(doc['scenes'][doc.get('scene',0)]['nodes'] == [i for i,n in enumerate(doc['nodes']) if n['name'] not in cp],
            'joint scene roots')
    rest_r, rest_t, rest_exact = 0,0,True
    for n,row in nodes.items():
        a = original[n]
        rest_r = max(rest_r, angle(row.get('rotation',(0,0,0,1)),a.get('rotation',(0,0,0,1))))
        rest_t = max(rest_t, math.dist(row.get('translation',(0,0,0)),a.get('translation',(0,0,0))))
        for key in ('translation','rotation','scale'):
            rest_exact &= row.get(key) == a.get(key)
        rest_exact &= 'matrix' not in row
    require(rest_r <= .01 and rest_t <= .0001 and rest_exact, 'exact rest TRS',
            f'rotation {rest_r:.9f} deg; translation {rest_t:.9f} m; exact fields {rest_exact}')
    animations = doc['animations']
    require(len(animations) == 5 and {a['name'] for a in animations} == set(EXPECTED), 'five named animations')
    # Model-frame deltas make reflection independent of the imported Blender axes.
    world_rest = {}
    def rest_world(n):
        if n not in world_rest:
            q = nodes[n].get('rotation',(0,0,0,1))
            world_rest[n] = multiply(rest_world(cp[n]),q) if n in cp else q
        return world_rest[n]
    tracks, vectors, peak_rows = {}, {}, {}
    for anim in animations:
        name = anim['name']
        keys_by_joint, vec = {}, {}
        good_channels = True
        good_samples = True
        endpoint, pop = 0, (0,'',0)
        rows = {}
        for c in anim['channels']:
            n = doc['nodes'][c['target']['node']]['name']
            good_channels &= c['target']['path'] == 'rotation' and n not in keys_by_joint
            s = anim['samplers'][c['sampler']]
            times = [v[0] for v in accessor(doc,binary,s['input'])]
            keys = accessor(doc,binary,s['output'])
            count = round(EXPECTED[name]*30)+1
            good_samples &= len(times) == len(keys) == count
            good_samples &= all(abs(t-i/30)<2e-7 for i,t in enumerate(times))
            good_samples &= s.get('interpolation','LINEAR') == 'LINEAR'
            good_samples &= all(math.isfinite(x) for q in keys for x in q)
            good_samples &= all(abs(sum(x*x for x in q)-1)<2e-6 for q in keys)
            good_samples &= all(sum(x*y for x,y in zip(a,b))>0 for a,b in zip(keys,keys[1:]))
            rest = nodes[n].get('rotation',(0,0,0,1))
            pr = rest_world(cp[n]) if n in cp else (0,0,0,1)
            ds = [multiply(multiply(pr,multiply(q,inverse(rest))),inverse(pr)) for q in keys]
            keys_by_joint[n] = ds
            vec[n] = [rotation_vector(q) for q in ds]
            endpoint = max(endpoint,angle(ds[0]),angle(ds[-1]))
            for f,(a,b) in enumerate(zip(keys,keys[1:])):
                step = angle(a,b)
                if step > pop[0]:
                    pop = step,n,f
            magnitudes = [angle(q) for q in ds]
            peak = max(magnitudes)
            f = next(i for i,v in enumerate(magnitudes) if v>=peak-1e-5)
            rows[n] = {'peak_deg':peak,'time_s':f/30}
        good_channels &= keys_by_joint.keys() == nodes.keys()
        require(good_channels, name+' rotation-only channels', 'one rotation channel per joint; no translation or scale keys')
        require(good_samples, name+' duration and 30 fps sampling', f'{EXPECTED[name]:.2f} s; {round(EXPECTED[name]*30)+1} unit-quaternion keys per joint')
        require(endpoint<=.05, name+' rest endpoints', f'max {endpoint:.8f} deg')
        require(pop[0]<=25+1e-5, name+' no pop', f'{pop[0]:.5f} deg; {pop[1]}; frames {pop[2]} to {pop[2]+1}')
        tracks[name],vectors[name],peak_rows[name] = keys_by_joint,vec,rows

    for family in ('claw','tail'):
        error = 0
        for n,qs in tracks[family+'_r'].items():
            other = n[:-1]+('l' if n.endswith('_r') else 'r') if n.endswith(('_r','_l')) else n
            for a,b in zip(qs,tracks[family+'_l'][other]):
                error=max(error,angle((a[0],-a[1],-a[2],a[3]),b))
        require(error<=.5, family+' exact mirrored deltas', f'max {error:.9f} deg over every joint and key')

    def component(clip,n,axis,sign=1):
        return [v['xyz'.index(axis)]*sign for v in vectors[clip][n]]
    def total(clip,names,axis,sign=1):
        return [sum(v) for v in zip(*(component(clip,n,axis,sign) for n in names))]
    def metric(clip,label,values,limits,window):
        value=max(values)
        frame=next(i for i,v in enumerate(values) if v>=value-1e-5)
        t=frame/30
        ok=limits[0]-1e-4<=value<=limits[1]+1e-4 and window[0]-TOL<=t<=window[1]+TOL
        require(ok,clip+' '+label, f'{value:.4f} deg at {t:.4f} s; range {limits}; window {window}')
        measurements.append({'clip':clip,'metric':label,'peak_deg':value,'time_s':t,'range_deg':limits,'window_s':window})
        return frame
    neck=[f'neck_{i:02}' for i in range(1,4)]
    tail=[f'tail_{i:02}' for i in range(1,9)]
    metric('bite','neck coil total',total('bite',neck,'x'),(35,45),(0,.16))
    metric('bite','neck throw total',total('bite',neck,'x',-1),(25,30),(.16,.36))
    metric('bite','jaw opens',component('bite','jaw','x',-1),(32,48),(0,.16))
    metric('bite','jaw closed overshoot',component('bite','jaw','x'),(2,3),(.24,.28))
    metric('bite','head recoil',component('bite','head','x'),(6.4,9.6),(.36,.65))
    for n in ('root','chest'):
        metric('bite',n+' pitch',component('bite',n,'x',-1),(6,10),(.16,.36))
    for side,sgn in [('r',1),('l',-1)]:
        metric('bite','wing_root_'+side+' sweep',component('bite','wing_root_'+side,'y',-sgn),(12,18),(.16,.36))
        metric('bite','upper_arm_'+side+' reach',component('bite','upper_arm_'+side,'x'),(20,30),(.16,.36))
    metric('bite','tail root lift',component('bite','tail_01','x',-1),(10,15),(.16,.36))

    for side in ('r','l'):
        c='claw_'+side
        sign=1 if side=='r' else -1
        other='l' if side=='r' else 'r'
        shoulder=component(c,'upper_arm_'+side,'x')
        metric(c,'shoulder wind-up',[-v for v in shoulder],(24,36),(0,.18))
        metric(c,'shoulder forward travel',[v-min(shoulder) for v in shoulder],(60*.8,70*1.2),(.18,.30))
        elbow=component(c,'forearm_'+side,'x')
        metric(c,'elbow closed',elbow,(24,36),(0,.22))
        metric(c,'elbow opening travel',[max(elbow)-v for v in elbow],(40,60),(.18,.42))
        metric(c,'wrist flick',component(c,'wrist_'+side,'x',-1),(32,48),(.18,.42))
        metric(c,'opposite foreleg brace',component(c,'upper_arm_'+other,'x',-1),(12,18),(.18,.42))
        metric(c,'spine twist',component(c,'chest','y',-sign),(12,18),(.18,.42))
        metric(c,'neck twist total',total(c,neck,'y',-sign),(12,18),(.18,.42))
        metric(c,'head lead',component(c,'head','y',-sign),(12,18),(.18,.30))
        metric(c,'striking wing lift',component(c,'wing_root_'+side,'z',sign),(12,18),(.18,.42))
        metric(c,'opposite wing dip',component(c,'wing_root_'+other,'z',sign),(8,12),(.18,.42))
        metric(c,'tail opposite swing',component(c,'tail_01','y',-sign),(20,25),(.18,.42))
        # Signed model-space FK establishes crossing without relying on the camera.
        xs=[]
        for f in range(len(shoulder)):
            relative=(0.,0.,0.)
            q=(0,0,0,1)
            for joint in ('upper_arm_'+side,'forearm_'+side,'wrist_'+side,'hand_'+side):
                t=nodes[joint].get('translation',(0,0,0))
                rotated=multiply(multiply(q,(*t,0)),inverse(q))[:3]
                relative=tuple(a+b for a,b in zip(relative,rotated))
                q=multiply(q,tracks[c][joint][f])
            xs.append(relative[0]*sign)
        require(min(xs[6:14])<0,c+' paw crosses chest centre', f'min signed hand X {min(xs[6:14]):.5f} m')
        for toe in range(1,4):
            metric(c,f'toe {toe} curl',component(c,f'toe_fore_{toe}_{side}','x',-1),(20,34),(.30,.48))
        metric(c,'outer toe spread',component(c,f'toe_fore_3_{side}','y',-sign),(10,20),(.18,.34))
        metric(c,'inner toe spread',component(c,f'toe_fore_1_{side}','y',sign),(10,20),(.18,.34))

    for side,sign in [('r',1),('l',-1)]:
        c='tail_'+side
        metric(c,'root left coil',component(c,'tail_01','y',-sign),(20,30),(0,.22))
        metric(c,'root right whip',component(c,'tail_01','y',sign),(28,42),(.22,.38))
        peaks=[]
        for j,n in enumerate(tail):
            values=component(c,n,'y',sign)
            peaks.append(max(values))
            metric(c,n+' whip',values,(28+j,42+j),(.22,.55))
        require(all(b>a for a,b in zip(peaks,peaks[1:])),c+' larger distal peaks',str([round(x,3) for x in peaks]))
        metric(c,'tip cumulative bend',total(c,tail,'y',sign),(70,180),(.22,.55))
        for n in ('root','chest'):
            metric(c,n+' hip/spine twist',component(c,n,'y',sign),(10,15),(.22,.55))
        metric(c,'neck counter-swing',total(c,neck,'y',-sign),(12,18),(.22,.55))
        metric(c,'head counter-swing',component(c,'head','y',-sign),(12,18),(.22,.55))
        metric(c,'hind kick',component(c,'thigh_'+side,'x',-1),(12,18),(.22,.55))

    for clip,names,axis,sign,label in [('bite',neck,'x',1,'neck coil'),('bite',neck,'x',-1,'neck throw'),
            ('bite',tail,'x',-1,'tail lift'),('tail_r',tail,'y',1,'tail whip'),('claw_r',tail,'y',-1,'tail swing')]:
        fs, starts = [], []
        for n in names:
            values=component(clip,n,axis,sign)
            starts.append(next(i for i,v in enumerate(values) if abs(v)>.01))
            peak=max(values)
            fs.append(next(i for i,v in enumerate(values) if v>=peak-1e-5))
        require(all(1<=b-a<=3 for a,b in zip(fs,fs[1:])),clip+' '+label+' overlap',f'peak frames {fs}; 1 to 3 frames per joint')
        require(all(1<=b-a<=3 for a,b in zip(starts,starts[1:])),clip+' '+label+' delayed starts',f'first moving frames {starts}')

    # Each active track must show changing speed across the attack interval.
    # LINEAR export interpolation joins dense samples of eased source curves.
    for clip in EXPECTED:
        start,end=(4,12) if clip=='bite' else ((5,14) if clip.startswith('claw') else (6,19))
        straight=[]
        for n,vs in vectors[clip].items():
            velocities=[tuple(b-a for a,b in zip(vs[f],vs[f+1])) for f in range(start,end)]
            if max(math.sqrt(sum(v*v for v in d)) for d in velocities)<.1:
                continue
            acceleration=max(math.dist(a,b) for a,b in zip(velocities,velocities[1:]))
            if acceleration<.02:
                straight.append(n)
        require(not straight,clip+' eased strike samples', 'constant-speed active tracks: '+str(straight))

    # Recovery must cross rest at most once after the final main pose.
    # The head's explicit bite recoil starts recovery at frame 14.
    for clip in EXPECTED:
        start=14 if clip=='bite' else (14 if clip.startswith('claw') else 18)
        bad=[]
        for n,vs in vectors[clip].items():
            for k in range(3):
                vals=[v[k] for v in vs]
                if max(abs(v) for v in vals)<.1:
                    continue
                # Tail recovery starts after its travelling peak, not at root recovery.
                anchor=start
                if clip.startswith('tail') and n.startswith('tail_'):
                    anchor=10+int(n[-2:])-1
                if clip=='bite' and n.startswith('tail_'):
                    anchor=10+int(n[-2:])-1
                if clip.startswith('claw') and n.startswith('tail_'):
                    anchor=11+int(n[-2:])-1
                seq=[v for v in vals[anchor:] if abs(v)>.02]
                crossings=sum(a*b<0 for a,b in zip(seq,seq[1:]))
                if crossings>1:
                    bad.append(n+':multiple crossings')
                if seq:
                    opposite=[abs(v) for v in seq if v*seq[0]<0]
                    peak=max(abs(v) for v in vals)
                    if opposite and max(opposite)>peak*.1001:
                        bad.append(n+':large overshoot')
        require(not bad,clip+' recovery overshoot',str(bad))
    # Forward kinematics measures landmark travel, including ancestor rotations.
    def rotate(q, v):
        return multiply(multiply(q, (*v, 0)), inverse(q))[:3]
    def positions(clip, frame):
        positions, orientations = {}, {}
        def visit(n):
            if n in positions:
                return
            parent = cp.get(n)
            if parent:
                visit(parent)
            pr = rest_world(parent) if parent else (0,0,0,1)
            local_delta = multiply(multiply(inverse(pr),tracks[clip][n][frame]),pr)
            local_q = multiply(local_delta,nodes[n].get('rotation',(0,0,0,1)))
            pq = orientations[parent] if parent else (0,0,0,1)
            p = positions[parent] if parent else (0,0,0)
            t = rotate(pq,nodes[n].get('translation',(0,0,0)))
            positions[n] = tuple(a+b for a,b in zip(p,t))
            orientations[n] = multiply(pq,local_q)
        for n in nodes:
            visit(n)
        return positions,orientations
    landmark_rows = {}
    for clip in EXPECTED:
        rest_pos,rest_q = positions(clip,0)
        markers = ['head','jaw_tip','hand_r','hand_l','tail_08']
        points = {n:[] for n in markers}
        for f in range(round(EXPECTED[clip]*30)+1):
            ps,qs = positions(clip,f)
            for n in markers:
                points[n].append(ps[n])
        rows = []
        for n in markers:
            ds = [math.dist(p,rest_pos[n]) for p in points[n]]
            f = ds.index(max(ds))
            rows.append({'joint':n,'distance_m':ds[f],'time_s':f/30,
                         'delta_xyz_m':[a-b for a,b in zip(points[n][f],rest_pos[n])]})
        landmark_rows[clip] = rows
        if clip == 'bite':
            coil = tuple(a-b for a,b in zip(points['head'][5],rest_pos['head']))
            hit = tuple(a-b for a,b in zip(points['head'][9],rest_pos['head']))
            require(coil[1]>0 and coil[2]>0 and hit[1]<0 and hit[2]<0,
                    'bite head moves up/back then down/forward', f'coil XYZ {coil}; hit XYZ {hit}')
    return {'results':results,'measurements':measurements,'joint_peaks':peak_rows,'landmarks':landmark_rows,
            'source_sha256':hashlib.sha256(Path(source).read_bytes()).hexdigest(),
            'clip_sha256':hashlib.sha256(Path(clip_path).read_bytes()).hexdigest()}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,default=ROOT/'assets/embercrest-hero.glb')
    p.add_argument('--clip',type=Path,default=ROOT/'assets/clips/embercrest-hero.attacks.glb')
    p.add_argument('--report',type=Path)
    args=p.parse_args()
    try:
        report=check(args.source,args.clip)
    except (OSError,ValueError,KeyError,IndexError,ZeroDivisionError,struct.error) as exc:
        print('FAIL load or structure:',exc)
        return 1
    for r in report['results']:
        print(('PASS' if r['pass'] else 'FAIL')+' '+r['check']+(': '+r['detail'] if r['detail'] else ''))
    if args.report:
        args.report.write_text(json.dumps(report,indent=2)+'\n')
    failures=sum(not r['pass'] for r in report['results'])
    print(('FAIL' if failures else 'PASS')+f' complete checker: {len(report["results"])-failures}/{len(report["results"])} checks')
    return bool(failures)


if __name__=='__main__':
    sys.exit(main())
