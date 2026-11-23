#!/usr/bin/env python3
"""Reconstruct the two studio flap loops and measure their skinned wings with NumPy."""
import argparse
from dataclasses import dataclass
import json
import math
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
import numpy as np
from check_clip import accessor, parent_names, read_glb

ROOT = Path(__file__).resolve().parents[2]
TAU = math.tau


def smoothstep(x):
    x = max(0., min(x, 1.))
    return x*x*(3-2*x)


def beat(phase, down):
    phase %= 1
    if phase < down:
        return math.cos(math.pi*phase/down)
    return -math.cos(math.pi*(phase-down)/(1-down))


def variation(index):
    """Match the two unsigned 32-bit multiplications in DragonRig's life_hash."""
    index = ((index ^ (index >> 16))*0x7feb352d) & 0xffffffff
    index = ((index ^ (index >> 15))*0x846ca68b) & 0xffffffff
    index ^= index >> 16
    return 2*(index & 0xffffff)/0xffffff - 1


def life_wave(t, a, b, c, offset):
    return (.5*math.sin(t*a + offset) + .3*math.sin(t*b + 1.7*offset)
            + .2*math.sin(t*c + 2.3*offset))


def roll(degrees):
    radians = math.radians(degrees)
    c, s = math.cos(radians), math.sin(radians)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def pitch(degrees):
    radians = math.radians(degrees)
    c, s = math.cos(radians), math.sin(radians)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def matrices(quaternions):
    quaternions = np.asarray(quaternions)
    unit = quaternions / np.linalg.norm(quaternions, axis=-1, keepdims=True)
    x, y, z, w = np.moveaxis(unit, -1, 0)
    entries = [1-2*(y*y+z*z), 2*(x*y-w*z), 2*(x*z+w*y),
               2*(x*y+w*z), 1-2*(x*x+z*z), 2*(y*z-w*x),
               2*(x*z-w*y), 2*(y*z+w*x), 1-2*(x*x+y*y)]
    return np.stack(entries, axis=-1).reshape(quaternions.shape[:-1] + (3, 3))


def interpolate_rotations(keys, phase):
    f = phase * 60
    k = int(f)
    u = f - k
    a, b = keys[:, k], keys[:, k+1]
    dot = np.sum(a*b, axis=1, keepdims=True)
    b = np.where(dot < 0, -b, b)
    theta = np.arccos(np.clip(np.abs(dot), 0, 1))
    # sinc computes sin(x)/x at zero as well as for moving joints.
    left = (1-u)*np.sinc((1-u)*theta/math.pi)
    right = u*np.sinc(u*theta/math.pi)
    return matrices(left*a + right*b)


def tunings(model):
    params = {}
    for header in ('src/anim/dragon_rig.h', 'src/game/flight.h'):
        values = re.findall(r'float (\w+) = (-?\d+(?:\.\d+)?)f;', (ROOT/header).read_text())
        params.update((key, float(raw)) for key, raw in values)
    for suffix in ('.rig.cfg', '.flight.cfg'):
        path = ROOT / f'assets/{model}.glb{suffix}'
        for line in path.read_text().splitlines():
            values = line.partition('#')[0].split()
            if values and values[0] in params:
                params[values[0]] = float(values[1])
    return params


@dataclass
class Wing:
    sign: int
    arm: list
    ribs: list
    stations: dict
    weights: np.ndarray
    level: np.ndarray


@dataclass
class Sample:
    frame: int
    phase: float
    heads: np.ndarray
    positions: np.ndarray
    fingertips: list


class Studio:
    """Calculate wing points from the retained rig terms, then apply the exported deltas."""

    def __init__(self, model):
        src, data = read_glb(ROOT/f'assets/{model}.glb')
        self.model = model
        self.params = tunings(model)
        skin = src['skins'][0]
        nodes = [src['nodes'][i] for i in skin['joints']]
        self.names = [n['name'] for n in nodes]
        parents = parent_names(src['nodes'])
        self.parents = [self.names.index(parents[n]) if parents.get(n) in self.names else -1 for n in self.names]
        binds = np.asarray(accessor(src, data, skin['inverseBindMatrices'])).reshape(-1, 4, 4).transpose(0, 2, 1)
        world = np.linalg.inv(binds)
        self.bind = world[:, :3, 3]
        basis = world[:, :3, :3].copy()
        basis /= np.linalg.norm(basis, axis=1, keepdims=True)
        self.forward = 1 if self.bind[self.names.index('head'), 2] > self.bind[0, 2] else -1

        doc, blob = read_glb(ROOT/f'assets/clips/{model}.flight.glb')
        self.clips = {}
        rest = [n.get('rotation', (0, 0, 0, 1)) for n in nodes]
        self.pre = basis @ matrices(rest).transpose(0, 2, 1)
        self.post = basis.transpose(0, 2, 1)
        for action in doc['animations']:
            keys = np.broadcast_to(rest, (61, len(nodes), 4)).transpose(1, 0, 2).copy()
            for channel in action['channels']:
                name = doc['nodes'][channel['target']['node']]['name']
                output = action['samplers'][channel['sampler']]['output']
                keys[self.names.index(name)] = accessor(doc, blob, output)
            self.clips[action['name']] = keys

        config = json.loads((Path(__file__).parent/'wingbeat'/f'{model}.json').read_text())
        right_level = matrices(config['level_xyzw'])
        reflect = np.diag([-1., 1., 1.])
        self.wings = [self.wing('r', 1, right_level), self.wing('l', -1, reflect @ right_level @ reflect)]
        self.leaf_offsets = np.zeros_like(self.bind)
        if model == 'storm3':
            fitted = json.loads((ROOT/'tools/skeletons/storm3.json').read_text())
            for name, head, tail, _ in fitted['bones']:
                if name.startswith('wing_finger_') and 'b_' in name:
                    i = self.names.index(name)
                    self.leaf_offsets[i] = basis[i, :, 1]*math.dist(head, tail)

        primitive, = src['meshes'][0]['primitives']
        attributes = primitive['attributes']
        positions = np.asarray(accessor(src, data, attributes['POSITION']))
        joints = np.asarray(accessor(src, data, attributes['JOINTS_0']))
        weights = np.asarray(accessor(src, data, attributes['WEIGHTS_0']), dtype=float)
        weights /= weights.sum(axis=1, keepdims=True)
        self.owner = joints[np.arange(len(weights)), np.argmax(weights, axis=1)]
        wings = [j for wing in self.wings for j in wing.stations]
        self.ids = np.flatnonzero(np.isin(self.owner, wings))
        self.joints = joints[self.ids]
        self.weights = weights[self.ids]
        self.offsets = (positions[self.ids, None] - self.bind[self.joints]) * self.weights[:, :, None]
        self.scale = (self.params['model_span_m'] or 19.)/np.ptp(positions[:, 0])
        self.owners = self.owner[self.ids]
        self.triangles = np.asarray(accessor(src, data, primitive['indices'])).reshape(-1, 3)

    def wing(self, side, sign, level):
        def joint(stem):
            return self.names.index(f'wing_{stem}_{side}')

        arm = [joint(stem) for stem in ('root', 'arm', 'wrist')]
        count = sum(n.startswith('wing_finger_') and n.endswith('a_'+side) for n in self.names)
        segments = ('a', 'b', 'tip') if f'wing_finger_1tip_{side}' in self.names else ('a', 'b')
        ribs = [[joint(f'finger_{i}{s}') for s in segments] for i in range(1, count+1)]
        path = arm + ribs[0]
        end = 2*self.bind[path[-1]] - self.bind[path[-2]]
        lever = np.linalg.norm((self.bind[path] - end)[:, :2], axis=1)
        lever /= lever[0]
        depth = np.arange(len(path))
        weights = self.params['outboard_decay']**depth * (1-np.minimum(self.params['wing_phase_lag']*depth, .8))
        weights /= weights @ lever
        stations = {j: (k, -1, 0) for k, j in enumerate(arm)}
        for ray, chain in enumerate(ribs):
            for k, j in enumerate(chain):
                stations[j] = len(arm)+k, ray, k/(len(chain)-1)
        return Wing(sign, arm, ribs, stations, weights, level)

    def glide_arm(self, frame=200):
        """Measure the right shoulder in the 26 m/s studio glide."""
        p = self.params
        wing = self.wings[0]
        t = frame / 60
        gust = p['gust_deg'] * 26/40
        sway = gust * life_wave(t, .71, 1.93, 3.10, .4)
        nod = gust*.6 * life_wave(t, .61, 1.61, 2.90, 1.3)
        nod += p['glide_breath_deg'] * math.sin(TAU*t/p['glide_breath_period'])
        trim = p['glide_trim_deg'] * life_wave(t, .53, 1.37, 2.71, 0)
        base = p['glide_dihedral_deg'] + p['wing_rest_deg'] + trim
        posed = roll(self.forward*sway) @ pitch(-self.forward*nod) @ roll(base*wing.weights[0]) @ wing.level
        arm = posed @ (self.bind[wing.arm[1]] - self.bind[wing.arm[0]])
        return math.degrees(math.atan2(arm[1], arm[0]))

    def samples(self, climb=False):
        """Yield frames 240 through 359 at game scale; positions correspond to self.ids."""
        p = self.params
        last_heights = None
        air = load = 0.
        follow = 1 - math.exp(-1/3)
        bottom = p['flap_down_angle_deg'] if climb else (p['flap_cruise_down_angle_deg'] or p['flap_down_angle_deg'])
        keys = self.clips['flap_climb' if climb else 'flap']
        for frame in range(360):
            t = frame / 60
            clock = (frame+1) / 60
            cycle = t / p['flap_period']
            phase = cycle % 1
            count = math.floor(cycle + 1e-9)
            speed = 32 + 1.5*math.sin(TAU*t/.9)
            signal = beat(phase, p['flap_downstroke_fraction'])
            angle = ((1+signal)*p['flap_up_angle_deg'] + (1-signal)*bottom)/2
            keyed = self.pre @ interpolate_rotations(keys, phase) @ self.post
            gust = p['gust_deg'] * speed / 40
            sway = gust * life_wave(clock, .71, 1.93, 3.10, .4)
            nod = gust*.6 * life_wave(clock, .61, 1.61, 2.90, 1.3)
            nod -= p['beat_pitch_deg'] * beat(phase-.5*p['beat_heave_lag'], p['wing_downstroke_fraction'])
            body = roll(self.forward*sway) @ pitch(-self.forward*nod)
            transforms = np.tile(body, (len(self.names), 1, 1))
            heads = self.bind[0] + (self.bind-self.bind[0]) @ body.T
            heights = []
            for side, wing in enumerate(self.wings):
                mix = smoothstep(phase)
                change = variation(2*count+side)*(1-mix) + variation(2*count+2+side)*mix
                base = p['wing_rest_deg'] + angle*p['beat_variation']*change
                n = len(wing.arm) + len(wing.ribs[0])
                flutter = p['flutter_deg']*smoothstep((speed-p['flutter_speed_start'])/45)**2
                flutter /= sum(p['outboard_decay']**np.arange(n))
                curl = p['finger_load_curl_deg']*(.25*air+np.clip(load, -.3, 1))/len(wing.ribs[0])
                for j, (depth, ray, fraction) in wing.stations.items():
                    bend = base * wing.weights[depth]
                    if ray >= 0:
                        offset = ray*1.9 + side*.7
                        waves = math.sin(TAU*13*clock+offset) + .6*math.sin(TAU*21.7*clock+1.7*offset)
                        bend += curl + flutter*fraction**2*waves
                    local = roll(wing.sign*bend)
                    if j == wing.arm[0]:
                        local = local @ wing.level
                    parent = self.parents[j]
                    transforms[j] = transforms[parent] @ local @ keyed[j]
                    heads[j] = heads[parent] + transforms[parent] @ (self.bind[j]-self.bind[parent])
                shoulder = heads[wing.arm[0]]
                wrist = heads[wing.arm[-1]]
                tip = heads[wing.ribs[0][-1]]
                heights.append((tip[1]-shoulder[1])/np.linalg.norm(wrist-shoulder))
            rate = 0 if last_heights is None else (sum(heights)-sum(last_heights))*30
            air += (1-air)*follow
            load += (np.clip(-rate/6, -1, 1)-load)*follow
            last_heights = heights
            if frame >= 240:
                points = (np.einsum('vnij,vnj->vni', transforms[self.joints], self.offsets)
                          + heads[self.joints]*self.weights[:, :, None]).sum(axis=1)
                ends = []
                for wing in self.wings:
                    leaf = [chain[-1] for chain in wing.ribs]
                    tips = heads[leaf] + np.einsum('nij,nj->ni', transforms[leaf], self.leaf_offsets[leaf])
                    ends.append(tips*self.scale)
                yield Sample(frame, phase, heads*self.scale, points*self.scale, ends)


def section_edges(studio, wing):
    """Keep the edges of triangles whose three vertices belong to this wing."""
    local = np.full(len(studio.owner), -1, dtype=int)
    local[studio.ids] = np.arange(len(studio.ids))
    mask = np.isin(studio.owner[studio.triangles], list(wing.stations)).all(axis=1)
    triangles = local[studio.triangles[mask]]
    edges = np.concatenate([triangles[:, [0, 1]], triangles[:, [1, 2]], triangles[:, [2, 0]]])
    return np.unique(np.sort(edges, axis=1), axis=0)


def chord_sections(sample, wing, edges):
    """Measure side-view chords between the front and rear skin at a common X."""
    a, b = sample.positions[edges[:, 0]], sample.positions[edges[:, 1]]
    start = sample.heads[wing.ribs[0][0], 0]
    end = (max(a[:, 0].max(), b[:, 0].max()) if wing.sign > 0
           else min(a[:, 0].min(), b[:, 0].min()))
    station_count = 1000
    # Each edge contributes to the stations between its two endpoints.
    step = (end-start)/station_count
    station_a = np.floor((a[:, 0]-start)/step).astype(int).clip(0, station_count-1)
    station_b = np.floor((b[:, 0]-start)/step).astype(int).clip(0, station_count-1)
    counts = np.abs(station_a-station_b)
    edge = np.repeat(np.arange(len(a)), counts)
    first = np.minimum(station_a, station_b)+1
    offsets = np.cumsum(counts)-counts
    stations = np.arange(len(edge)) + np.repeat(first-offsets, counts)
    p, q = a[edge], b[edge]
    u = (start+step*stations-p[:, 0])/(q[:, 0]-p[:, 0])
    y = p[:, 1]+u*(q[:, 1]-p[:, 1])
    z = p[:, 2]+u*(q[:, 2]-p[:, 2])
    front, rear = np.full(station_count, -np.inf), np.full(station_count, np.inf)
    np.maximum.at(front, stations, z)
    np.minimum.at(rear, stations, z)
    front_y, rear_y = np.zeros(station_count), np.zeros(station_count)
    at_front, at_rear = z == front[stations], z == rear[stations]
    front_y[stations[at_front]] = y[at_front]
    rear_y[stations[at_rear]] = y[at_rear]
    angles = np.abs(np.rad2deg(np.arctan2(front_y[1:]-rear_y[1:], front[1:]-rear[1:])))
    index = int(angles.argmax())
    return angles[index], (index+1)/station_count


def metrics(studio, climb):
    samples = list(studio.samples(climb))
    positions = np.stack([sample.positions for sample in samples])
    spans = np.ptp(positions[:, :, 0], axis=1)
    distances = np.array([abs(sample.phase-40/60) for sample in samples])
    nearest = distances < distances.min()+1e-6
    print(f'{"Climb" if climb else "Cruise"} studio frames {samples[0].frame} to {samples[-1].frame}')
    for wing in studio.wings:
        distances = []
        for rib in wing.ribs:
            mask = np.isin(studio.owners, rib[1:])
            distances.append(np.min(wing.sign*positions[:, mask, 0]))
        side = 'right' if wing.sign > 0 else 'left'
        print(f'{side} rib minima in metres', ' '.join(f'{x:.3f}' for x in distances))
    print('span at nearest sampled peak fold / widest', f'{spans[nearest].min()/spans.max():.3f}')
    # Use one complete cycle so drift from one beat to the next cannot move a reversal.
    wraps = [i for i in range(1, len(samples)) if samples[i].phase < samples[i-1].phase - .5]
    duration = wraps[1] - wraps[0]
    start = wraps[0] - round(.2*duration)
    if start < 0:
        start += duration
    cycle = samples[start:start + duration]
    wing = studio.wings[0]
    origin = np.stack([s.heads[wing.arm[0]] for s in cycle])
    arm = np.stack([s.heads[wing.arm[1]] for s in cycle]) - origin
    angles = np.rad2deg(np.arctan2(arm[:, 1], arm[:, 0]))
    glide = studio.glide_arm()
    print('right shoulder elevation in frame 200 glide', f'{glide:+.2f} deg')
    print('right shoulder elevation against horizontal', f'{angles.max():+.2f} to {angles.min():+.2f} deg')
    print('right shoulder excursion from frame 200 glide',
          f'{angles.max()-glide:+.2f} to {angles.min()-glide:+.2f} deg')
    print('right shoulder high and low phases',
          f'{cycle[angles.argmax()].phase:.3f} {cycle[angles.argmin()].phase:.3f}')
    landmarks = [(studio.names[wing.arm[-1]], [s.heads[wing.arm[-1], 1] for s in cycle])]
    for index, rib in enumerate(wing.ribs, 1):
        name = f'wing_finger_{index}tip_r'
        landmarks.append((name, [s.fingertips[0][index-1, 1] for s in cycle]))
    for name, values in landmarks:
        heights = np.array(values) - origin[:, 1]
        top, low = cycle[heights.argmax()].phase, cycle[heights.argmin()].phase
        print(f'{name} high and low phases {top:.3f} {low:.3f}')
    if studio.model == 'storm3':
        for wing in studio.wings:
            edges = section_edges(studio, wing)
            sections = [(chord_sections(s, wing, edges), s.phase) for s in samples]
            (angle, fraction), phase = max(sections)
            side = 'right' if wing.sign > 0 else 'left'
            print(f'{side} steepest skin chord {angle:.2f} deg at phase {phase:.3f}, '
                  f'{fraction:.1%} of outer hand width across 999 cuts')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('model', choices=('tide2', 'storm3'))
    args = parser.parse_args()
    rig = Studio(args.model)
    for effort in (False, True):
        metrics(rig, effort)
