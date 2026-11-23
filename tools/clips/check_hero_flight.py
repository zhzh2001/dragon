#!/usr/bin/env python3
"""Measure the exported flight tracks and their posed source landmarks."""
import argparse
import math
from pathlib import Path
import sys

sys.dont_write_bytecode = True
from check_clip import accessor, angle, inverse, multiply, normalize, parent_names, read_glb

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'assets/embercrest-hero.glb'
CLIP = ROOT / 'assets/clips/embercrest-hero.flight.glb'
IDENTITY = (0, 0, 0, 1)
LEVEL = normalize((-.336819, -.078848, -.053912, .936712))


def rotate(q, v):
    return multiply(multiply(q, (*v, 0)), inverse(q))[:3]


def plus(a, b):
    return tuple(x + y for x, y in zip(a, b))


def minus(a, b):
    return tuple(x - y for x, y in zip(a, b))


def roll(v):
    return math.degrees(math.atan2(v[1], v[0]))


def peak(values, func=max):
    return func(range(60), key=values.__getitem__)


def measure(nodes, original, parents, tracks):
    names = list(nodes)
    rest = {}
    def head(n):
        if n not in rest:
            parent = parents.get(n)
            p, q = head(parent) if parent else ((0, 0, 0), IDENTITY)
            rest[n] = (plus(p, rotate(q, nodes[n].get('translation', (0, 0, 0)))),
                       multiply(q, nodes[n].get('rotation', IDENTITY)))
        return rest[n]
    for n in names:
        head(n)
    result = {}
    def put(key, value):
        result.setdefault(key, []).append(value)
    for frame in range(61):
        world = {}
        def fk(n):
            if n not in world:
                parent = parents.get(n)
                p, q = fk(parent) if parent else ((0, 0, 0), LEVEL)
                local = tracks[n][frame] if n in tracks else nodes[n].get('rotation', IDENTITY)
                world[n] = (plus(p, rotate(q, nodes[n].get('translation', (0, 0, 0)))),
                            multiply(q, local))
            return world[n]
        def pos(stem):
            return fk('wing_' + stem + '_r')[0]
        def tail(stem):
            n = 'wing_' + stem + '_r'
            offset = rotate(inverse(rest[n][1]), minus(original[n]['extras']['tail'], rest[n][0]))
            p, q = fk(n)
            return plus(p, rotate(q, offset))
        root = pos('root')
        put('arm angle', roll(minus(pos('arm'), root)))
        put('wrist height', pos('wrist')[1] - root[1])
        put('wrist forward', root[2] - pos('wrist')[2])
        for ray, end in [(1, 'c'), (2, 'b'), (3, 'b')]:
            tip = tail(f'finger_{ray}{end}')
            put(f'tip {ray} height', tip[1] - root[1])
            if ray == 1:
                put('span', 2 * (1.15 + tip[0] - root[0]))
            put(f'finger {ray} elevation', roll(minus(tip, pos('finger_1a'))))
        # Remove the fixed roll that sets each segment's open chord.
        # The resulting frame exposes independent sweep, pitch and stroke angles.
        yaw = {}
        for stem, trim in [('root', -30), ('arm', -35), ('wrist', -6), ('finger_1a', 0), ('finger_3a', 0)]:
            a = -math.radians(trim)/2
            q = multiply(multiply(fk('wing_'+stem+'_r')[1], inverse(LEVEL)), (0, 0, math.sin(a), math.cos(a)))
            yaw[stem] = -math.degrees(math.asin(max(-1, min(1, rotate(q, (1, 0, 0))[2]))))
            if stem == 'wrist':
                put('pitch', math.degrees(math.atan2(rotate(q,(0,1,0))[2],rotate(q,(0,0,1))[2])))
        put('elbow fold', yaw['arm'] - yaw['root'])
        put('wrist fold', yaw['arm'] - yaw['wrist'])
        put('sweep', yaw['root'])
        put('fan closure', yaw['finger_3a'] - yaw['finger_1a'])
    return result


def check(path):
    results = []
    def record(n, ok, detail):
        results.append((n, bool(ok), detail))
    src, _ = read_glb(SOURCE)
    original = {src['nodes'][i]['name']: src['nodes'][i] for i in src['skins'][0]['joints']}
    source_parents = parent_names(src['nodes'])
    doc, data = read_glb(path)
    animations = doc['animations']
    nodes = {n['name']: n for n in doc['nodes']}
    parents = parent_names(doc['nodes'])
    topology = all(parents.get(n) == (source_parents.get(n) if source_parents.get(n) in original else None)
                   for n in nodes)
    shape = (len(nodes) == len(doc['nodes']) == len(original) and nodes.keys() == original.keys()
             and len(animations) == 2 and {a['name'] for a in animations} == {'flap','flap_climb'}
             and topology and not any(doc.get(k) for k in ('meshes','skins','images','materials','textures'))
             and doc['scenes'][doc.get('scene',0)]['nodes'] == [i for i,n in enumerate(doc['nodes']) if n['name'] not in parents])
    record(1, shape, f'{len(nodes)} joint nodes, hierarchy, scene roots and two named animations')
    if not shape:
        return results
    exact = all(all(n.get(k) == original[name].get(k) for k in ('translation','rotation','scale'))
                and 'matrix' not in n for name,n in nodes.items())
    record(2, exact, 'model rest translation, rotation and scale')
    metrics = {}
    for anim in animations:
        name = anim['name']
        tracks, valid, dense = {}, True, True
        for c in anim['channels']:
            joint = doc['nodes'][c['target']['node']]['name']
            s = anim['samplers'][c['sampler']]
            times = accessor(doc, data, s['input'])
            keys = accessor(doc, data, s['output'])
            valid &= c['target']['path'] == 'rotation' and joint.startswith('wing_') and joint not in tracks
            dense &= (len(times) == len(keys) == 61 and s.get('interpolation','LINEAR') == 'LINEAR'
                      and all(abs(t[0]-i/60) < 1e-7 for i,t in enumerate(times))
                      and all(len(q) == 4 and abs(sum(x*x for x in q)-1) < .00001 for q in keys))
            tracks[joint] = keys
        required = {n for n in nodes if n.startswith(('wing_root','wing_arm','wing_wrist','wing_finger'))}
        valid &= required <= tracks.keys() and all(n[:-1]+('l' if n.endswith('r') else 'r') in tracks for n in tracks)
        record(1, dense, name+' 61 unit rotations from 0 to 1 second')
        record(2, valid, name+' paired wing rotation channels only')
        if not (dense and valid):
            continue
        seam = max(angle(v[0],v[-1]) for v in tracks.values())
        record(3, seam <= .05, f'{name} seam {seam:.5f} deg')
        mirror = 0
        for n,v in tracks.items():
            if n.endswith('_r'):
                for a,b in zip(v,tracks[n[:-1]+'l']):
                    mirror = max(mirror, angle((a[0],-a[1],-a[2],a[3]), b))
        record(4, mirror <= .5, f'{name} mirror {mirror:.5f} deg')
        m = metrics[name] = measure(nodes, original, parents, tracks)
        arm = m['arm angle']
        top, bottom = peak(arm), peak(arm,min)
        record(5, min(top,60-top)/60 <= .02 and abs(bottom/60-.5) <= .03,
               f'{name} arm high {arm[top]:.2f} at {top}/60, low {arm[bottom]:.2f} at {bottom}/60')
        climb = name == 'flap_climb'
        for station in ['wrist height', 'tip 1 height', 'tip 2 height', 'tip 3 height']:
            values = m[station]
            lags = ((peak(values)-top)%60, (peak(values,min)-bottom)%60)
            lo,hi = (.04,.06) if station.startswith('wrist') else (.08,.10)
            if climb:
                lo,hi = lo*4/3, hi*4/3
            ok = all(lo-1e-6 <= n/60 <= hi+1e-6 for n in lags)
            record(6, ok, f'{name} {station} reversal lags {lags[0]}/60 and {lags[1]}/60')
        for key in ('elbow fold','wrist fold'):
            values = m[key]
            i = peak(values)
            record(7, .60 <= i/60 <= .72 and abs(values[57])<=3,
                   f'{name} {key} {values[i]:.2f} deg at {i}/60, {values[57]:.2f} at 57/60')
        step, joint = max((max(angle(a,b) for a,b in zip(v,v[1:])),n) for n,v in tracks.items())
        record(8, step <= 12, f'{name} largest step {step:.2f} deg on {joint}')
        ranges = [('arm high',max(arm),65 if climb else 55),('arm depth',-min(arm),60 if climb else 40),
                  ('elbow fold',max(m['elbow fold']),45 if climb else 35),
                  ('wrist fold',max(m['wrist fold']),65 if climb else 48),
                  ('forward sweep',m['sweep'][30]-m['sweep'][0],25 if climb else 15),
                  ('pitch down',-m['pitch'][15],13),('finger closure',max(m['fan closure']),15)]
        for label,value,target in ranges:
            record(9, .8*target <= value <= 1.2*target, f'{name} {label} {value:.2f} deg')
        mid_bend = m['finger 1 elevation'][15] - arm[15]
        record(9, 8 <= mid_bend <= 24, f'{name} loaded outer wing {mid_bend:.2f} deg above arm at 15/60')
        recovery = peak(m['wrist fold'])
        span_ratio = m['span'][recovery] / max(m['span'])
        if not climb:
            record(9, .8*2/3 <= span_ratio <= 1.2*2/3,
                   f'{name} span at peak wrist fold {span_ratio:.3f} of maximum')
        record(9, m['pitch'][recovery] > 0 and m['finger 1 elevation'][recovery] < 0,
               f'{name} recovery pitch {m["pitch"][recovery]:.2f}, hand-to-tip elevation {m["finger 1 elevation"][recovery]:.2f} deg')
        record(9, m['wrist forward'][30] > m['wrist forward'][0],
               f'{name} wrist moves {m["wrist forward"][30]-m["wrist forward"][0]:.3f} m toward nose during downstroke')
    if len(metrics) == 2:
        a,b = metrics['flap'],metrics['flap_climb']
        larger = (max(b['arm angle']) > max(a['arm angle']) and min(b['arm angle']) < min(a['arm angle'])
                  and all(max(b[k])>max(a[k]) for k in ('elbow fold','wrist fold')))
        record(9, larger, 'climb exceeds cruise at both reversals and both hinges')
    return results


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('clip', nargs='?', type=Path, default=CLIP)
    args = parser.parse_args()
    try:
        results = check(args.clip)
    except Exception as error:
        results = [(1, False, str(error))]
    for group, ok, detail in results:
        print('PASS' if ok else 'FAIL', group, detail)
    sys.exit(int(any(not ok for _,ok,_ in results)))
