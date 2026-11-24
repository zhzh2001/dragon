#!/usr/bin/env python3
"""Check the exported wing rotations against the source rig and the flight brief."""
import argparse
import json
import math
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
from wingbeat.profiles import flight_value
from check_clip import accessor, angle, inverse, multiply, normalize, parent_names, read_glb

ROOT = Path(__file__).resolve().parents[2]
IDENTITY = (0, 0, 0, 1)
ZERO = (0, 0, 0)
STUDIES = ('gold-dragon', 'silver-dragon')


def rotation(q, v):
    return multiply(multiply(q, (*v, 0)), inverse(q))[:3]


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def difference(a, b):
    return tuple(x - y for x, y in zip(a, b))


def rotations_and_heads(nodes, parents, keys, level=IDENTITY, before=None):
    """Place each joint using its exported local rotation and the source offsets."""
    posed = {}

    def visit(name):
        if name in posed:
            return posed[name]
        node = nodes[name]
        parent = parents.get(name)
        head, basis = visit(parent) if parent in nodes else (ZERO, IDENTITY)
        origin = add(head, rotation(basis, node.get('translation', ZERO)))
        if name == 'wing_root_r':
            basis = multiply(level, basis)
        local = keys.get(name, node.get('rotation', IDENTITY))
        if before and name in before:
            local = multiply(before[name], local)
        posed[name] = origin, normalize(multiply(basis, local))
        return posed[name]

    for name in nodes:
        visit(name)
    return posed


def bounded_angle(a):
    return (a + 180) % 360 - 180


def bearing(v):
    return math.degrees(math.atan2(v[2], v[0]))


def roll(v):
    return math.degrees(math.atan2(v[1], v[0]))


def mirrored_error(tracks, rest):
    worst = 0
    for right, keys in tracks.items():
        if not right.endswith('_r'):
            continue
        left = right[:-1] + 'l'
        for a, b in zip(keys, tracks[left]):
            # Compare the two rotation deltas in the same model axes.
            def model_delta(name, key):
                local_rest, world_rest = rest[name]
                d = multiply(inverse(local_rest), key)
                return multiply(multiply(world_rest, d), inverse(world_rest))

            x, y, z, w = model_delta(right, a)
            worst = max(worst, angle((x, -y, -z, w), model_delta(left, b)))
    return worst


def peak(values, extreme=max):
    return extreme(range(60), key=values.__getitem__)


def measure(nodes, parents, tracks, level, config, model):
    nodes = dict(nodes)
    parents = dict(parents)
    if model == 'storm3':
        source = json.loads((ROOT/'tools/skeletons/storm3.json').read_text())
        for name, head, tail, _ in source['bones']:
            if name.startswith('wing_finger_') and name.endswith('b_r'):
                tip = name.replace('b_r', 'tip_r')
                nodes[tip] = {'name': tip, 'translation': (0, math.dist(head, tail), 0)}
                parents[tip] = name
    rest = rotations_and_heads(nodes, parents, {})
    before = {}
    for stem, degrees in config.get('foundation_rolls_deg', {}).items():
        name = stem + '_r'
        half = math.radians(degrees)/2
        body = (0, 0, math.sin(half), math.cos(half))
        if stem == 'wing_root':
            body = multiply(body, level)
        parent = rest[parents[name]][1]
        before[name] = multiply(inverse(parent), multiply(body, parent))
    root_level = IDENTITY if before else level
    neutral = rotations_and_heads(nodes, parents, {}, root_level, before)
    root = 'wing_root_r'
    if model in STUDIES:
        bases = sorted(n for n in nodes if n.startswith('wing_finger_') and n.endswith('_01_r'))
        tips = [n.removesuffix('_01_r') + '_03_r' for n in bases]
    else:
        bases = sorted((n for n in nodes if n.startswith('wing_finger_') and n.endswith('a_r')),
                       key=lambda n: int(n.split('_')[2][:-1]))
        tips = [n.replace('a_r', 'tip_r') for n in bases]
    arm = 'wing_arm_r'
    wrist = 'wing_wrist_r'
    neutral_angle = roll(difference(neutral[arm][0], neutral[root][0])) + config.get('glide_offset_deg', 0)
    reference_pose = neutral if before else rest
    reference = [bearing(difference(reference_pose[t][0], reference_pose[b][0])) for b, t in zip(bases, tips)]
    gaps = [bounded_angle(b - a) for a, b in zip(reference, reference[1:])]
    metrics = {}

    def put(key, value):
        metrics.setdefault(key, []).append(value)

    for frame in range(61):
        pose = rotations_and_heads(nodes, parents, {n: v[frame] for n, v in tracks.items()}, root_level, before)

        def yaw(name):
            q = multiply(pose[name][1], inverse(neutral[name][1]))
            return -math.degrees(math.asin(max(-1, min(1, rotation(q, (1, 0, 0))[2]))))

        put('arm', roll(difference(pose[arm][0], pose[root][0])) - neutral_angle)
        put('wrist height', pose[wrist][0][1] - pose[root][0][1])
        put('elbow fold', yaw(arm) - yaw(root))
        put('wrist fold', yaw(arm) - yaw(wrist))
        put('span', 2 * max(p[0] for n, (p, _) in pose.items() if n.startswith('wing_') and n.endswith('_r')))
        palm_inv = multiply(reference_pose[wrist][1], inverse(pose[wrist][1]))
        if model == 'storm3':
            chains = [[pose[n][0] for n in (base, base.replace('a_r', 'b_r'), tip)]
                      for base, tip in zip(bases, tips)]
            put('chord', chord_inclination(chains))
        if model in STUDIES:
            outer = bases[0].removesuffix('_01_r') + '_02_r'
            put('outer elevation', roll(difference(pose[tips[0]][0], pose[outer][0])))
            put('arm elevation', roll(difference(pose[arm][0], pose[root][0])))
        rays = []
        for index, (base, tip) in enumerate(zip(bases, tips), 1):
            put(f'rib {index} height', pose[tip][0][1] - pose[root][0][1])
            rays.append(bearing(rotation(palm_inv, difference(pose[tip][0], pose[base][0]))))
        separation = [bounded_angle(b - a) for a, b in zip(rays, rays[1:])]
        signed = [s * math.copysign(1, g) for s, g in zip(separation, gaps)]
        put('min gap', min(signed))
        put('max closure', max(abs(g) - s for g, s in zip(gaps, signed)))
        put('fan width', sum(signed))
    return metrics


def chord_inclination(chains):
    """Cut the posed rib segments at common spans and measure their fore-aft chords."""
    start = chains[0][0][0]
    end = max(p[0] for chain in chains for p in chain)
    peak_angle = 0.
    for i in range(1, 40):
        x = start + (end-start)*i/40
        points = []
        for chain in chains:
            for a, b in zip(chain, chain[1:]):
                if min(a[0], b[0]) < x <= max(a[0], b[0]):
                    t = (x-a[0])/(b[0]-a[0])
                    points.append(tuple(av+t*(bv-av) for av, bv in zip(a, b)))
        if len(points) >= 2:
            points.sort(key=lambda p: p[2])
            chord = difference(points[-1], points[0])
            peak_angle = max(peak_angle, math.degrees(math.atan2(abs(chord[1]), chord[2])))
    return peak_angle


def longest_slow_run(keys):
    """Count consecutive slow intervals across the seam, in sixtieths of a beat."""
    steps = [angle(a, b) for a, b in zip(keys, keys[1:])]
    peak_step = max(steps)
    if peak_step < 1e-5:
        return len(steps)
    longest = current = 0
    for step in steps + steps:
        current = current + 1 if step < .15 * peak_step else 0
        longest = max(longest, current)
    return min(longest, len(steps))


def measure_checks(model, animations, nodes, parents, original, config, results):
    def record(group, ok, text):
        results.append((group, bool(ok), text))

    rest_pose = rotations_and_heads(nodes, parents, {})
    rest = {n: (original[n].get('rotation', IDENTITY), rest_pose[n][1]) for n in nodes}
    all_metrics = {}
    down = flight_value(ROOT, model, 'flap_downstroke_fraction')
    for name, tracks in animations.items():
        climb = name == 'flap_climb'
        seam = max(angle(v[0], v[-1]) for v in tracks.values())
        record(3, seam <= .05, f'{name} loop seam {seam:.5f} deg')
        mirror = mirrored_error(tracks, rest)
        record(4, mirror <= .005, f'{name} mirror error in model axes {mirror:.5f} deg')
        m = all_metrics[name] = measure(nodes, parents, tracks, config['level_xyzw'], config, model)
        arm = m['arm']
        top, low = peak(arm), peak(arm, min)
        record(5, min(top, 60-top) <= 1 and abs(low/60 - down) <= .03,
               f'{name} arm relative to neutral {arm[top]:+.2f} at {top}/60, {arm[low]:+.2f} deg at {low}/60')
        for station in (s for s in m if s.endswith('height')):
            path = m[station]
            delays = ((peak(path) - top) % 60, (peak(path, min) - low) % 60)
            a, b = ((.04, .06) if station.startswith('wrist') else (.08, .10))
            if climb:
                a, b = a*4/3, b*4/3
            if model in STUDIES:
                if station.startswith('wrist'):
                    a, b = 4/60, 6/60
                elif station == 'rib 1 height':
                    a, b = 7/60, 10/60
                else:
                    # The inner rays attach near the torso. The leading tip defines stroke timing.
                    continue
            record(6, all(a-1e-6 <= d/60 <= b+1e-6 for d in delays),
                   f'{name} {station} lags {delays[0]}/60 and {delays[1]}/60')
        for key in ('elbow fold', 'wrist fold'):
            i = peak(m[key])
            record(7, .60 <= i/60 <= .72 and abs(m[key][57]) <= 3,
                   f'{name} {key} {m[key][i]:.2f} deg at {i}/60, {m[key][57]:.2f} at 57/60')
        step, joint = max((max(angle(a, b) for a, b in zip(v, v[1:])), n) for n, v in tracks.items())
        record(8, step <= 12, f'{name} largest step {step:.2f} deg on {joint}')
        top_target, depth_target = (50, 32) if climb else (40, 22)
        if model == 'storm3':
            top_target += 5
        if model in STUDIES:
            top_target, depth_target = (55, 40) if climb else (45, 30)
            run, held = max((longest_slow_run(v), n) for n, v in tracks.items())
            record(10, run/60 <= .08, f'{name} longest run below 15% of joint peak speed '
                   f'{run}/60 on {held} (limit 8% of the beat)')
        record(9, .8*top_target <= arm[top] <= 1.2*top_target and .8*depth_target <= -arm[low] <= 1.2*depth_target,
               f'{name} arm stroke {arm[top]:+.2f} to {arm[low]:+.2f} deg')
        scale = 1.25 if climb else 1
        limits = [('elbow fold', 20, 28), ('wrist fold', 25, 35)]
        if model == 'storm3':
            scale = 1
            limits = [('elbow fold', 0, 20), ('wrist fold', 0, 25)]
        if model in STUDIES:
            limits = [('elbow fold', 20, 30), ('wrist fold', 25, 40)]
        for key, lo, hi in limits:
            value = max(m[key])
            record(9, lo * scale <= value <= hi * scale, f'{name} peak {key} {value:.2f} deg')
        recovery = peak(m['wrist fold'])
        span = m['span'][recovery] / max(m['span'])
        span_ok = .70 <= span <= .85 if model in STUDIES else span > (.80 if model == 'storm3' else .70)
        record(9, span_ok, f'{name} span at peak wrist fold {span:.3f} of widest')
        gap = min(m['min gap'])
        closure = max(m['max closure'])
        record(9, gap > 0 and closure <= 8,
               f'{name} smallest adjacent tip separation {gap:.2f} deg, largest closure {closure:.2f} deg')
        if model != 'storm3':
            narrowing = m['fan width'][0] - m['fan width'][recovery]
            record(9, narrowing > 0, f'{name} fan narrows {narrowing:.2f} deg at peak wrist fold')
        else:
            inclination = max(m['chord'])
            record(9, inclination <= 35, f'{name} largest side-view chord between ribs {inclination:.2f} deg')
        if model == 'silver-dragon':
            mid_down = round(60 * down / 2)
            mid_up = round(60 * (down + (1-down)/2))
            arm_down, arm_up = (m['arm elevation'][i] for i in (mid_down, mid_up))
            tip_down, tip_up = (m['outer elevation'][i] for i in (mid_down, mid_up))
            wave = tip_down > max(0, arm_down) and tip_up < min(0, arm_up)
            record(9, wave,
                   f'{name} midstroke arm / outer elevation, down {arm_down:+.1f} / {tip_down:+.1f}, '
                   f'up {arm_up:+.1f} / {tip_up:+.1f} deg')
    if len(all_metrics) == 2:
        a, b = all_metrics['flap'], all_metrics['flap_climb']
        larger = max(b['arm']) > max(a['arm']) and min(b['arm']) < min(a['arm'])
        detail = 'climb exceeds cruise at both reversals'
        if model != 'storm3':
            larger &= all(max(b[k]) > max(a[k]) for k in ('elbow fold', 'wrist fold'))
            detail += ' and both hinges'
        record(9, larger, detail)


def check(model, path):
    src, _ = read_glb(ROOT / f'assets/{model}.glb')
    original = {src['nodes'][i]['name']: src['nodes'][i] for i in src['skins'][0]['joints']}
    source_parents = parent_names(src['nodes'])
    doc, data = read_glb(path)
    nodes = {n['name']: n for n in doc['nodes']}
    parents = parent_names(doc['nodes'])
    actions = doc['animations']
    shape = (len(nodes) == len(doc['nodes']) == len(original) and nodes.keys() == original.keys()
             and len(actions) == 2 and {a['name'] for a in actions} == {'flap', 'flap_climb'}
             and all(parents.get(n) == (source_parents[n] if source_parents.get(n) in original else None)
                     for n in nodes)
             and not any(doc.get(k) for k in ('meshes', 'skins', 'materials', 'textures', 'images', 'cameras'))
             and doc['scenes'][doc.get('scene', 0)]['nodes'] ==
                 [i for i, n in enumerate(doc['nodes']) if n['name'] not in parents])
    results = [(1, shape, f'{len(nodes)} joint nodes, source hierarchy and two named loops')]
    if not shape:
        return results
    exact = all(all(n.get(k) == original[name].get(k) for k in ('translation', 'rotation', 'scale'))
                and 'matrix' not in n for name, n in nodes.items())
    results.append((2, exact, 'source rest translations, rotations and scales'))
    required = {n for n in nodes if n.startswith('wing_') and 'tip_' not in n}
    animations = {}
    for a in actions:
        tracks = {}
        valid = dense = True
        for c in a['channels']:
            n = doc['nodes'][c['target']['node']]['name']
            sampler = a['samplers'][c['sampler']]
            times, keys = (accessor(doc, data, sampler[k]) for k in ('input', 'output'))
            valid &= c['target']['path'] == 'rotation' and n.startswith('wing_') and n not in tracks
            dense &= (len(times) == len(keys) == 61 and sampler.get('interpolation', 'LINEAR') == 'LINEAR'
                      and all(len(t) == 1 and abs(t[0]-i/60) < 1e-7 for i, t in enumerate(times))
                      and all(len(q) == 4 and abs(sum(x*x for x in q)-1) < .00001 for q in keys))
            tracks[n] = keys
        valid &= required <= tracks.keys() and all(n[:-1]+('l' if n.endswith('r') else 'r') in tracks for n in tracks)
        name = a['name']
        results.append((1, dense, f'{name} 61 unit rotations from 0 to 1 second'))
        results.append((2, valid, f'{name} paired wing rotation channels only'))
        if dense and valid:
            animations[name] = tracks
    config = json.loads((Path(__file__).parent / 'wingbeat' / f'{model}.json').read_text())
    measure_checks(model, animations, nodes, parents, original, config, results)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('model', choices=('tide2', 'storm3', *STUDIES))
    parser.add_argument('clip', nargs='?', type=Path)
    args = parser.parse_args()
    try:
        path = args.clip or ROOT / f'assets/clips/{args.model}.flight.glb'
        results = check(args.model, path)
    except (ValueError, OSError, KeyError, IndexError, struct.error) as exc:
        results = [(1, False, str(exc))]
    for group, ok, text in results:
        print('PASS' if ok else 'FAIL', group, text)
    return int(any(not ok for _, ok, _ in results))


if __name__ == '__main__':
    sys.exit(main())
