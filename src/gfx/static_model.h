#pragma once

#include <string>
#include <vector>

#include "core/math.h"
#include "gfx/mesh.h"

namespace gfx {

// A mesh from a static (unskinned) glTF: one per mesh node, in world space
// (the node transform applied), with its vertex colour when the file has one.
//
// The engine read only skinned glTF until now, so the run's props were each
// skinned to a single identity bone just to load. Anything that does not
// animate -- rocks, and props from here on -- comes through this instead.
struct StaticMesh {
    std::string name;  // the node's name, or the mesh's
    MeshData data;
    bool has_color = false;
    core::Vec3 bounds_min = core::Vec3::zero();
    core::Vec3 bounds_max = core::Vec3::zero();
};

// Every mesh node in the file. Positions in metres, Y-up, as glTF defines;
// normals recomputed when the file has none; colour white when it has none.
bool load_static_gltf(const char* path, std::vector<StaticMesh>& out, std::string* error = nullptr);

}  // namespace gfx
