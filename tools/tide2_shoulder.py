"""Keep the heat bind inside each wing attachment."""
from mathutils.kdtree import KDTree


def smooth(t):
    t = max(0., min(1., t))
    return t * t * (3 - 2 * t)


def limit_wing_heat(body, distance_from_cut, width):
    mesh = body.data
    distance = [distance_from_cut(v.co) for v in mesh.vertices]
    neighbours = [[] for _ in distance]
    for edge in mesh.edges:
        a, b = edge.vertices
        neighbours[a].append(b)
        neighbours[b].append(a)
    tips = [min(mesh.vertices, key=lambda v: v.co.x).index,
            max(mesh.vertices, key=lambda v: v.co.x).index]
    wing = set(tips)
    rim = set()
    for i in tips:
        for j in neighbours[i]:
            if distance[j] <= 0:
                rim.add(j)
            elif j not in wing:
                wing.add(j)
                tips.append(j)

    collar = KDTree(len(rim))
    for i in rim:
        collar.insert(mesh.vertices[i].co, i)
    collar.balance()
    chest = body.vertex_groups['chest'].index
    amount = {}
    for vertex in mesh.vertices:
        share = smooth(distance[vertex.index] / width) if vertex.index in wing else 0.
        if vertex.index in wing:
            amount[vertex.index] = share
        if share == 1.:
            continue
        groups = [(g.group, g.weight) for g in vertex.groups]
        moving = [(i, w) for i, w in groups if body.vertex_groups[i].name.startswith('wing_')]
        released = (1 - share) * sum(w for _, w in moving)
        if released == 0.:
            continue
        for i, w in moving:
            if share == 0.:
                body.vertex_groups[i].remove([vertex.index])
            else:
                body.vertex_groups[i].add([vertex.index], w * share, 'REPLACE')
        # At the attachment, give released weight to the wing's parent.
        # Amplifying small foreleg weights here makes a pointed fold during flight.
        support = [(i, w) for i, w in groups if not body.vertex_groups[i].name.startswith('wing_')]
        total = sum(w for _, w in support)
        _, _, gap = collar.find(vertex.co)
        body_share = smooth((gap - .012) / .030) if total > 0. else 0.
        if body_share < 1.:
            body.vertex_groups[chest].add([vertex.index], released * (1 - body_share), 'ADD')
        if body_share > 0.:
            for i, w in support:
                body.vertex_groups[i].add([vertex.index], released * body_share * w / total, 'ADD')
    return amount
