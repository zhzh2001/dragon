"""Select the connected wing surface beyond each shoulder."""

def select_wing_surface(mesh, cut):
    vertices = mesh.vertices
    eligible = {v.index for v in vertices if abs(v.co.x) > cut}
    neighbours = {i: [] for i in eligible}
    for edge in mesh.edges:
        a, b = edge.vertices
        if a in eligible and b in eligible:
            neighbours[a].append(b)
            neighbours[b].append(a)

    # Each outer tip connects to one membrane. A leg spike can cross the same X plane.
    tips = [min(eligible, key=lambda i: vertices[i].co.x),
            max(eligible, key=lambda i: vertices[i].co.x)]
    selected = set(tips)
    while tips:
        for i in neighbours[tips.pop()]:
            if i not in selected:
                selected.add(i)
                tips.append(i)
    return selected
