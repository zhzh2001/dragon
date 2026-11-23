#!/usr/bin/env python3
"""Check the hero's exported body motion in the model's coordinate frame."""
import argparse
import math
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
from check_clip import accessor, angle, inverse, multiply, normalize, parent_names, read_glb, rotation_vector

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'assets/embercrest-hero.glb'
CLIP = ROOT / 'assets/clips/embercrest-hero.body.glb'
NAMES = ('flap_body', 'flap_climb_body', 'glide', 'walk', 'stand')
REST = (0, 0, 0, 1)
NECK = ('neck_01', 'neck_02', 'neck_03')
TAIL = tuple(f'tail_{i:02}' for i in range(1, 9))
FRONT = (0, 0, -1, 0)


def span(v):
    return max(v) - min(v)


def peak_phase(v, choose=max):
    return choose(range(len(v)), key=v.__getitem__) / len(v)


def sway_phase(v):
    """Measure the once-per-cycle motion separately from faster tip motion."""
    angles = [math.tau*i/len(v) for i in range(len(v))]
    x = sum(a*math.cos(t) for a,t in zip(v,angles))
    y = sum(a*math.sin(t) for a,t in zip(v,angles))
    return (math.atan2(y,x)/math.tau) % 1


def distance(a, b):
    return abs((a - b + .5) % 1 - .5)


def total(curves):
    return list(map(sum, zip(*curves)))


def peaks(v):
    """Find circular maxima, including the midpoint of a flat peak."""
    changes = [i for i in range(len(v)) if abs(v[i] - v[i-1]) > 1e-5]
    found = []
    for k, i in enumerate(changes):
        j = changes[(k+1) % len(changes)]
        if v[i] > v[i-1] and v[j] < v[j-1]:
            found.append(((i + ((j-i-1) % len(v))/2) / len(v)) % 1)
    return found


class Motion:
    def __init__(self, name, tracks, report):
        self.name = name
        self.tracks = tracks
        self.report = report
        self.count = len(next(iter(tracks.values()))) - 1
        self.vectors = {n: [rotation_vector(q) for q in keys[:-1]] for n, keys in tracks.items()}

    def axis(self, name, axis):
        if name not in self.vectors:
            return [0.] * self.count
        return [v[axis] for v in self.vectors[name]]

    def chain(self, names, axis):
        return total([self.axis(n, axis) for n in names])

    def direction(self):
        result = []
        for i in range(self.count):
            q = REST
            for name in (*NECK, 'head'):
                if name in self.tracks:
                    q = multiply(q, self.tracks[name][i])
            result.append(normalize(multiply(multiply(q, FRONT), inverse(q))[:3]))
        return result

    def line(self, ok, label, group=7):
        self.report.append((group, bool(ok), f'{self.name} {label}'))

    def bounds(self, label, value, low, high, units='deg'):
        self.line(low - 1e-5 <= value <= high + 1e-5,
                  f'{label} {value:.3f} {units} (range {low:.3f} to {high:.3f})')

    def amplitude(self, label, value, low, high=None):
        self.bounds(label, value, .7*low, 1.3*(low if high is None else high))

    def phase(self, label, value, target, tolerance):
        self.line(distance(value, target) <= tolerance + 1e-6,
                  f'{label} phase {value:.3f} (target {target:.3f} +/- {tolerance:.3f})')

    def cycles(self, label, v, count):
        p = peaks(v)
        self.line(len(p) == count, f'{label} has {len(p)} peaks at phases {[round(x, 3) for x in p]}')
        return p


def check_beat(m):
    climb = m.name == 'flap_climb_body'
    scale = 1.5 if climb else 1
    tail = [m.axis(n, 0) for n in TAIL]
    base, distal = span(total(tail[:4])), span(total(tail[4:]))
    m.amplitude('base half tail pitch excursion', base, 40*scale)
    m.line(distal > base, f'distal half tail pitch excursion {distal:.2f} deg vs base {base:.2f}')
    ranges = [span(v) for v in tail]
    m.line(all(a <= b + .05 for a,b in zip(ranges,ranges[1:])) and ranges[-1] > ranges[0],
           f'tail pitch excursions base to tip {[round(x,2) for x in ranges]} deg')
    phases = [peak_phase(v) for v in tail]
    m.phase('base tail dip', phases[0], .5 + .15, .07)
    lag = [(b-a) % 1 for a, b in zip(phases, phases[1:])]
    m.line(all(0 < x < .2 for x in lag) and sum(lag) < 1,
           f'tail dip phases from base to tip {[round(x, 3) for x in phases]}')
    tip = tail[-1]
    steps = [x-tip[i-1] for i, x in enumerate(tip)]
    m.line(max(steps) > 1.15*-min(steps), f'tip down/up speed {max(steps):.3f}/{-min(steps):.3f} deg/frame')
    result = {'tail':base}
    for n, lo, hi, sign, timing in [
        ('thigh_r',8,10,-1,.35), ('shin_r',6,8,1,.35),
        ('forearm_r',5,5,-1,.50), ('wrist_r',6,8,-1,.50)]:
        v = [sign*x for x in m.axis(n,0)]
        peak = peak_phase(v)
        m.amplitude(f'{n} stroke pitch', max(v), lo*scale, hi*scale)
        m.phase(f'{n} stroke', peak, timing + (.03 if climb else 0), .09)
        result[n] = max(v)
        result[n+' phase'] = peak
    m.phase('foreleg delay after hind leg', (result['forearm_r phase']-result['thigh_r phase'])%1,.15,.05)
    for region in ('hind','fore'):
        for i in range(1,4):
            n = f'toe_{region}_{i}_r'
            v = [-x for x in m.axis(n,0)]
            if region == 'hind':
                m.amplitude(f'{n} curl', max(v), 10*scale, 15*scale)
            else:
                m.line(max(v) > scale, f'{n} curls {max(v):.2f} deg')
            gather = (peak_phase(v) - (.15 if region == 'fore' else 0)) % 1
            m.line(.5 < gather < 1, f'{n} gather phase {peak_phase(v):.3f}')
    for n in NECK:
        v = m.axis(n,0)
        m.amplitude(n+' downstroke pitch', -min(v), 4*scale, 6*scale)
        m.line(.05 < peak_phase(v,min) < .6, f'{n} dip phase {peak_phase(v,min):.3f}')
        result[n] = span(v)
    v = m.axis('chest',0)
    m.amplitude('chest arch', max(v), 2*scale, 3*scale)
    m.phase('chest arch', peak_phase(v), .5, .1)
    if not climb:
        directions = m.direction()
        smallest = min(sum(a*b for a,b in zip(u,v)) for u in directions for v in directions)
        turn = math.degrees(math.acos(min(1,max(-1,smallest))))
        m.line(turn < 2, f'head aim diameter {turn:.4f} deg (limit 2)', 8)
    return result


def check(path, through=None):
    src, _ = read_glb(SOURCE)
    doc, data = read_glb(path)
    original = {src['nodes'][i]['name']: src['nodes'][i] for i in src['skins'][0]['joints']}
    nodes = {n['name']:n for n in doc['nodes']}
    expected = NAMES[:NAMES.index(through)+1] if through else NAMES
    animations = {a['name']:a for a in doc['animations']}
    parents = parent_names(doc['nodes'])
    src_parents = parent_names(src['nodes'])
    topology = all(parents.get(n) == (src_parents.get(n) if src_parents.get(n) in original else None) for n in nodes)
    roots = [i for i,n in enumerate(doc['nodes']) if n['name'] not in parents]
    shape = (nodes.keys() == original.keys() and len(nodes) == len(doc['nodes']) and topology
             and set(expected) <= animations.keys() <= set(NAMES) and len(animations) == len(doc['animations'])
             and doc['scenes'][doc.get('scene',0)]['nodes'] == roots
             and not any(doc.get(k) for k in ('meshes','skins','materials','textures','images')))
    results = [(1,shape,f'{len(nodes)} joint names and hierarchy, armature alone, animations {", ".join(animations)}')]
    if not shape:
        return results
    rot = max(angle(n.get('rotation',REST),original[k].get('rotation',REST)) for k,n in nodes.items())
    trans = max(math.dist(n.get('translation',(0,0,0)),original[k].get('translation',(0,0,0))) for k,n in nodes.items())
    scale = all(n.get('scale',(1,1,1)) == original[k].get('scale',(1,1,1)) and 'matrix' not in n for k,n in nodes.items())
    results.append((2,rot <= .01 and trans <= .0001 and scale, f'rest differences {rot:.6f} deg, {1000*trans:.6f} mm, scale matches {scale}'))
    beats = []
    for name in expected:
        anim = animations[name]
        frames = {'glide':240,'stand':300}.get(name,60)
        fps = 30 if frames > 60 else 60
        tracks, paths, forbidden, invalid = {}, [], [], []
        for ch in anim['channels']:
            target = ch['target']
            joint = doc['nodes'][target['node']]['name']
            if target['path'] != 'rotation':
                paths.append(joint+'/'+target['path'])
                continue
            air = name in NAMES[:3]
            if (joint.startswith(('jaw','root')) or (air and joint.startswith('wing_'))
                or (not air and not joint.startswith(('neck_', 'tail_', 'wing_')) and joint != 'head')):
                forbidden.append(joint)
            s = anim['samplers'][ch['sampler']]
            times = accessor(doc,data,s['input'])
            keys = accessor(doc,data,s['output'])
            if (s.get('interpolation','LINEAR') != 'LINEAR' or joint in tracks
                or len(keys) != frames+1 or len(times) != len(keys)
                or not all(len(t)==1 and abs(t[0]-i/fps) < 1e-6 for i,t in enumerate(times))
                or not all(len(q)==4 and abs(sum(x*x for x in q)-1) < 1e-5 for q in keys)):
                invalid.append(joint)
                continue
            rest = inverse(nodes[joint].get('rotation',REST))
            tracks[joint] = [multiply(rest,q) for q in keys]
        results.extend([(1,bool(tracks) and not invalid,f'{name} {frames+1} unit quaternion keys at {fps} Hz, malformed tracks {invalid}'),
                        (2,not paths,f'{name} nonrotation paths {paths}'),
                        (3,not forbidden,f'{name} forbidden joints {forbidden}')])
        if invalid or not tracks:
            continue
        worst, joint = max((angle(v[0],v[-1]),j) for j,v in tracks.items())
        results.append((4,worst <= .05,f'{name} seam value {joint} {worst:.6f} deg (limit 0.05)'))
        rates = []
        for j,v in tracks.items():
            incoming = rotation_vector(multiply(v[-1],inverse(v[-2])))
            outgoing = rotation_vector(multiply(v[1],inverse(v[0])))
            speed = max(math.hypot(*incoming),math.hypot(*outgoing))
            err = math.dist(incoming,outgoing)
            # Ignore differences below a millionth of a degree per sample.
            rates.append((err/speed if err > 1e-6 else 0,j,speed*fps,err*fps))
        ratio,j,speed,err = max(rates,key=lambda r:(r[0],r[2]))
        results.append((4,ratio <= .20,f'{name} seam velocity {j} difference {err:.6f} of {speed:.6f} deg/s ({ratio:.1%}, limit 20%)'))
        if name in NAMES[:2]:
            mirrors = []
            for j,v in tracks.items():
                partner = j[:-1]+('l' if j.endswith('_r') else 'r') if j.endswith(('_l','_r')) else j
                if partner not in tracks:
                    mirrors.append((math.inf,j))
                    continue
                mirrors.append((max(angle((q[0],-q[1],-q[2],q[3]),p) for q,p in zip(v,tracks[partner])),j))
            worst,j = max(mirrors)
            results.append((5,worst <= .5,f'{name} mirror {j} {worst:.5f} deg (limit 0.5)'))
        worst,j = max((max(angle(a,b) for a,b in zip(v,v[1:])),j) for j,v in tracks.items())
        results.append((6,worst <= 10,f'{name} largest key step {j} {worst:.4f} deg (limit 10)'))
        m = Motion(name,tracks,results)
        if name in NAMES[:2]:
            beats.append(check_beat(m))
        else:
            check_other(m)
    if len(beats) == 2:
        a,b = beats
        for k in ['tail','thigh_r',*NECK]:
            ratio = b[k]/a[k] if a[k] else 0
            results.append((7,1.05 <= ratio <= 1.95,f'climb/cruise {k} excursion ratio {ratio:.3f} (range 1.05 to 1.95)'))
        for k in ('thigh_r','shin_r','forearm_r','wrist_r'):
            lag = (b[k+' phase']-a[k+' phase'])%1
            results.append((7,.01 <= lag <= .055,f'climb {k} follows cruise by {lag:.3f} cycle'))
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('clip', type=Path, nargs='?', default=CLIP)
    parser.add_argument('--through', choices=NAMES)
    args = parser.parse_args()
    try:
        results = check(args.clip,args.through)
    except (OSError, ValueError, IndexError, KeyError, TypeError, ArithmeticError, struct.error) as error:
        results = [(1, False, f'Could not read the clip: {error}')]
    for n, ok, detail in results:
        print('PASS' if ok else 'FAIL', n, detail)
    return int(any(not ok for _,ok,_ in results))


def check_other(m):
    tail = m.chain(TAIL,1)
    base, tip = m.axis(TAIL[0],1), m.axis(TAIL[-1],1)
    direction = m.direction()
    yaw = [math.degrees(math.atan2(-x,-z)) for x,y,z in direction]
    pitch = [math.degrees(math.asin(max(-1,min(1,y)))) for x,y,z in direction]
    if m.name == 'glide':
        breath = m.axis('chest',0)
        m.amplitude('chest breath',max(map(abs,breath)),1.5)
        m.cycles('chest breath',breath,2)
        sign = 1 if yaw[m.count//4] >= 0 else -1
        turn = [sign*x for x in yaw]
        m.amplitude('first look',max(turn),8)
        m.amplitude('second look',-min(turn),6)
        m.phase('first look',peak_phase(turn),.25,.09)
        m.phase('second look',peak_phase(turn,min),.75,.09)
        m.line(abs(turn[m.count//2]) < 2,f'head yaw between looks {turn[m.count//2]:.2f} deg')
        m.line(pitch[round(peak_phase(turn,min)*m.count)] < -.25,
               f'pitch at second look {pitch[round(peak_phase(turn,min)*m.count)]:.2f} deg')
        neck = [[sign*x for x in m.axis(j,1)] for j in NECK]
        p = [peak_phase(v) for v in neck]
        m.line(all(span(v) > .2 for v in neck) and p == sorted(p) and p[-1] > p[0],
               f'neck look phases from base to head {[round(x,3) for x in p]}')
        thighs = [m.axis('thigh_'+side,0) for side in 'lr']
        for side,v in zip('lr',thighs):
            m.amplitude(f'{side} hip drift',max(map(abs,v)),3,4)
        drift = distance(*[peak_phase(v) for v in thighs])
        m.line(drift > .05,f'hip drift phase difference {drift:.3f}')
        toes = {side:max((max(-x for x in m.axis(f'toe_hind_{i}_{side}',0)),i) for i in range(1,4)) for side in 'lr'}
        active,other = sorted(toes,key=lambda side:toes[side],reverse=True)
        amplitude,i = toes[active]
        m.amplitude(active+' hind toe curl',amplitude,10)
        m.line(toes[other][0] < 1,f'{other} hind toe curl {toes[other][0]:.2f} deg')
        m.cycles('active toe curl',[-x for x in m.axis(f'toe_hind_{i}_{active}',0)],1)
        for side in 'lr':
            shift = max((span(m.axis(j+'_'+side,axis)),j+'_'+side,axis)
                        for j in ('upper_arm','forearm','wrist','hand') for axis in range(3))
            excursion,j,axis = shift
            m.line(excursion > .5,f'{side} foreleg shift {excursion:.2f} deg on {j}')
            v = m.axis(j,axis)
            # Count the flexed peak in either direction.
            m.cycles(side+' foreleg shift',v if max(v) >= -min(v) else [-x for x in v],1)
        m.amplitude('summed tail sway',max(map(abs,tail)),14)
        m.phase('slow tail tip lag', (sway_phase(tip)-sway_phase(base))%1,.25,.075)
        m.cycles('base tail sway',base,1)
        # The twitch can cross the slow sway or bend in another plane.
        extra = max(span(m.axis(TAIL[-1],i)) for i in (0,2))
        m.line(extra > .5 or len(peaks(tip)) > 1,f'tip twitch, off-axis excursion {extra:.2f} deg, yaw peaks {len(peaks(tip))}')
    elif m.name == 'walk':
        dip = [-x for x in m.chain(NECK,0)]
        m.amplitude('summed neck dip',max(dip),4)
        p = peaks(dip)
        m.line(len(p) == 2 and all(min((distance(x,target) for x in p),default=1) <= .05 for target in (0,.5)),
               f'neck dip peaks at phases {[round(x,3) for x in p]} (targets 0, 0.5 +/- 0.05)',9)
        m.line(span(pitch) < 2,f'head pitch excursion {span(pitch):.3f} deg')
        neck = m.chain(NECK,1)
        m.amplitude('summed neck sway',max(map(abs,neck)),3,4)
        m.cycles('neck sway',neck,1)
        m.line(neck[m.count//4] < 0 < neck[3*m.count//4],
               f'neck yaw over planted right/left forefoot {neck[m.count//4]:.2f}/{neck[3*m.count//4]:.2f} deg')
        m.amplitude('summed tail sway',max(map(abs,tail)),20)
        m.line(tail[m.count//4] > 0 > tail[3*m.count//4],
               f'tail yaw opposite planted left/right hind foot {tail[m.count//4]:.2f}/{tail[3*m.count//4]:.2f} deg')
        m.cycles('base tail sway',base,1)
        m.phase('tail tip lag',(peak_phase(tip)-peak_phase(base))%1,.15,.05)
        for side in 'lr':
            v = m.axis('wing_wrist_'+side,2)
            m.amplitude(side+' wrist bounce',max(map(abs,v)),2,3)
            m.cycles(side+' wrist bounce',v,2)
    else:
        breath = m.axis(NECK[0],0)
        m.amplitude('base neck breath',max(breath),1,1.5)
        breaths = m.cycles('neck breath',breath,3)
        for side,sign in [('r',1),('l',-1)]:
            v = [sign*x for x in m.axis('wing_root_'+side,2)]
            m.amplitude(side+' shoulder lift',max(v),1.5,2)
            lifts = m.cycles(side+' shoulder lift',v,3)
            aligned = all(any(distance(p,b) <= .06 for b in breaths) for p in lifts)
            m.line(aligned, f'{side} shoulder lift follows each neck breath within 0.06 cycle')
        sign = 1 if yaw[round(.3*m.count)] >= 0 else -1
        turn = [y*sign for y in yaw]
        m.amplitude('head look',max(turn),12)
        hold = [turn[round(i*m.count/100)] for i in range(30,41)]
        m.line(min(hold) >= .85*max(turn),f'look held at 3-4 s, minimum {min(hold):.2f} of {max(turn):.2f} deg')
        m.line(abs(turn[round(.7*m.count)]) < .3*max(turn),f'head yaw after look {turn[round(.7*m.count)]:.2f} deg')
        roll = m.chain((*NECK,'head'),2)
        m.amplitude('head tilt',max(map(abs,roll)),5)
        m.phase('head tilt',peak_phase(list(map(abs,roll))),.7,.10)
        m.amplitude('summed tail sway',max(map(abs,tail)),12)
        m.cycles('base tail sway',base,1)
        v = max((m.axis(TAIL[-1],i) for i in (0,2)),key=span)
        m.line(span(v) > .5 or len(peaks(tip)) > 1,
               f'tip curl, off-axis excursion {span(v):.2f} deg, yaw peaks {len(peaks(tip))}')


if __name__ == '__main__':
    sys.exit(main())
