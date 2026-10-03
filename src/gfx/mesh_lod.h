#pragma once

// A static mesh cut to a triangle budget (docs/PORTING.md, R4): the rocks.
// They are scans, flat-shaded with three vertices to a triangle, 500 to 1,500
// triangles each, and two or three thousand stand in a valley -- on an SM2
// card most of a frame's vertex work. The vertices are welded by position
// first (so the simplifier sees one surface), simplified with
// meshoptimizer and given smooth normals: a retro rock is rounder and far
// cheaper. anim/skin_lod.h is the same for skinned meshes.

#include <cstdint>

#include "gfx/mesh.h"

namespace gfx {

// `mesh` with at most about `max_triangles` triangles; unchanged when it
// already fits or `max_triangles` is 0.
MeshData simplify_mesh(const MeshData& mesh, uint32_t max_triangles);

}  // namespace gfx
