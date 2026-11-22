"""Give both faces of the standing wing the same rib weights."""

from math import hypot


class Membrane:
    def __init__(self, skeleton):
        self.hub = skeleton['wing_field']['hub']
        axis = skeleton['wing_bind_roll_axis']
        length = hypot(axis[0], axis[1])
        self.forward = (axis[0] / length, axis[1] / length)
        bones = {name: end for name, _, end, _ in skeleton['bones']}
        self.ribs = []
        for number in range(1, 5):
            name = f'wing_finger_{number}'
            bend = self.project(bones[name + 'a_r'])
            tip = self.project(bones[name + 'b_r'])
            self.ribs.append((name, bend, tip))

    def project(self, point):
        dx = abs(point[0]) - self.hub[0]
        dy = point[1] - self.hub[1]
        return dx * self.forward[0] + dy * self.forward[1], point[2] - self.hub[2]

    def weights(self, span, height, suffix):
        levels = []
        for _, (bend_span, bend_z), (tip_span, tip_z) in self.ribs:
            if span < bend_span:
                levels.append(bend_z * span / bend_span)
            else:
                slope = (tip_z - bend_z) / (tip_span - bend_span)
                levels.append(bend_z + (span - bend_span) * slope)
        pair = next((i for i in range(len(levels) - 1) if height >= levels[i + 1]), len(levels) - 2)
        across = max(0., min(1., (levels[pair] - height) / (levels[pair] - levels[pair + 1])))
        result = {}
        for index, share in ((pair, 1 - across), (pair + 1, across)):
            prefix, (bend_span, _), _ = self.ribs[index]
            along = max(0., min(1., (span / bend_span - 0.68) / 0.64))
            along *= along * (3 - 2 * along)
            result[prefix + 'a_' + suffix] = share * (1 - along)
            result[prefix + 'b_' + suffix] = share * along
        return result
