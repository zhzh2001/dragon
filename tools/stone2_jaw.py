"""Assign the exposed teeth to the jaw surface that carries them."""

def lower_surface(mesh, cut, split_height):
    beyond = {v.index for v in mesh.vertices if v.co.y > cut}
    neighbours = {i: [] for i in beyond}
    for edge in mesh.edges:
        a, b = edge.vertices
        if a in beyond and b in beyond:
            neighbours[a].append(b)
            neighbours[b].append(a)
    owners = {}
    while beyond:
        group = [beyond.pop()]
        for i in group:
            for j in neighbours[i]:
                if j in beyond:
                    beyond.remove(j)
                    group.append(j)
        lower = sum(mesh.vertices[i].co.z < split_height(mesh.vertices[i].co.y) for i in group)
        owners.update(dict.fromkeys(group, lower * 2 > len(group)))
    return owners
