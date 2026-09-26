#include "gfx/static_model.h"

#include <cstring>

#include "cgltf.h"  // the implementation is compiled in anim/gltf_loader.cpp
#include "core/log.h"

namespace gfx {

namespace {

const cgltf_accessor* attribute(const cgltf_primitive& primitive, cgltf_attribute_type type) {
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        const cgltf_attribute& a = primitive.attributes[i];
        if (a.type == type && a.index == 0) return a.data;
    }
    return nullptr;
}

}  // namespace

bool load_static_gltf(const char* path, std::vector<StaticMesh>& out, std::string* error) {
    out.clear();
    cgltf_options options;
    std::memset(&options, 0, sizeof(options));
    cgltf_data* data = nullptr;
    auto fail = [&](const char* why) {
        if (error) *error = why;
        if (data) cgltf_free(data);
        return false;
    };
    if (cgltf_parse_file(&options, path, &data) != cgltf_result_success) return fail("could not parse");
    if (cgltf_load_buffers(&options, data, path) != cgltf_result_success) return fail("could not load buffers");

    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node& node = data->nodes[n];
        if (!node.mesh) continue;
        float world[16];
        cgltf_node_transform_world(&node, world);
        core::Mat4 transform;
        std::memcpy(&transform, world, sizeof(world));  // both column-major

        StaticMesh mesh;
        mesh.name = node.name ? node.name : (node.mesh->name ? node.mesh->name : "");
        bool any_normals = true;
        mesh.bounds_min = core::Vec3{1e30f, 1e30f, 1e30f};
        mesh.bounds_max = core::Vec3{-1e30f, -1e30f, -1e30f};
        for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p) {
            const cgltf_primitive& primitive = node.mesh->primitives[p];
            if (primitive.type != cgltf_primitive_type_triangles) continue;
            const cgltf_accessor* positions = attribute(primitive, cgltf_attribute_type_position);
            if (!positions) continue;
            const cgltf_accessor* normals = attribute(primitive, cgltf_attribute_type_normal);
            const cgltf_accessor* colors = attribute(primitive, cgltf_attribute_type_color);
            const cgltf_accessor* uvs = attribute(primitive, cgltf_attribute_type_texcoord);
            any_normals = any_normals && normals;
            mesh.has_color = mesh.has_color || colors;
            const uint32_t base = uint32_t(mesh.data.vertices.size());
            for (cgltf_size v = 0; v < positions->count; ++v) {
                MeshVertex vertex;
                float f[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                cgltf_accessor_read_float(positions, v, f, 3);
                vertex.position = core::transform_point(transform, core::Vec3{f[0], f[1], f[2]});
                if (normals) {
                    cgltf_accessor_read_float(normals, v, f, 3);
                    vertex.normal = core::normalize_or(
                        core::transform_dir(transform, core::Vec3{f[0], f[1], f[2]}), core::Vec3::up());
                } else {
                    vertex.normal = core::Vec3::up();
                }
                if (colors) {
                    float c[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                    cgltf_accessor_read_float(colors, v, c, cgltf_num_components(colors->type));
                    vertex.color = core::Vec3{c[0], c[1], c[2]};
                } else {
                    vertex.color = core::Vec3::one();
                }
                if (uvs) {
                    cgltf_accessor_read_float(uvs, v, f, 2);
                    vertex.uv = core::Vec2{f[0], f[1]};
                }
                mesh.bounds_min = core::minv(mesh.bounds_min, vertex.position);
                mesh.bounds_max = core::maxv(mesh.bounds_max, vertex.position);
                mesh.data.vertices.push_back(vertex);
            }
            if (primitive.indices) {
                for (cgltf_size i = 0; i < primitive.indices->count; ++i) {
                    mesh.data.indices.push_back(base + uint32_t(cgltf_accessor_read_index(primitive.indices, i)));
                }
            } else {
                for (cgltf_size i = 0; i < positions->count; ++i) mesh.data.indices.push_back(base + uint32_t(i));
            }
        }
        if (mesh.data.indices.empty()) continue;
        if (!any_normals) mesh.data.recompute_normals();
        out.push_back(std::move(mesh));
    }
    cgltf_free(data);
    if (out.empty()) {
        if (error) *error = "no mesh nodes";
        return false;
    }
    return true;
}

}  // namespace gfx
