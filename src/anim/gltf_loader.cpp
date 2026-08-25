#include "anim/gltf_loader.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "core/log.h"

using core::Mat4;
using core::Quat;
using core::Transform;
using core::Vec3;

namespace anim {
namespace {

// Rotation from an orthonormal basis, by the largest-trace branch. Done directly
// rather than via look_rotation, which rebuilds the right axis from a cross
// product and so would quietly discard a mirrored basis.
Quat quat_from_basis(Vec3 x, Vec3 y, Vec3 z) {
    const float trace = x.x + y.y + z.z;
    Quat q;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q = Quat{(y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s, 0.25f * s};
    } else if (x.x > y.y && x.x > z.z) {
        const float s = std::sqrt(1.0f + x.x - y.y - z.z) * 2.0f;
        q = Quat{0.25f * s, (y.x + x.y) / s, (z.x + x.z) / s, (y.z - z.y) / s};
    } else if (y.y > z.z) {
        const float s = std::sqrt(1.0f + y.y - x.x - z.z) * 2.0f;
        q = Quat{(y.x + x.y) / s, 0.25f * s, (z.y + y.z) / s, (z.x - x.z) / s};
    } else {
        const float s = std::sqrt(1.0f + z.z - x.x - y.y) * 2.0f;
        q = Quat{(z.x + x.z) / s, (z.y + y.z) / s, 0.25f * s, (x.y - y.x) / s};
    }
    return core::normalize(q);
}

// Decomposes a column-major 4x4 into translation, rotation and scale.
Transform decompose(const float m[16]) {
    Transform t;
    t.position = Vec3{m[12], m[13], m[14]};
    Vec3 axis_x{m[0], m[1], m[2]};
    Vec3 axis_y{m[4], m[5], m[6]};
    Vec3 axis_z{m[8], m[9], m[10]};
    t.scale = Vec3{core::length(axis_x), core::length(axis_y), core::length(axis_z)};
    axis_x = t.scale.x > 1e-8f ? axis_x / t.scale.x : Vec3::unit_x();
    axis_y = t.scale.y > 1e-8f ? axis_y / t.scale.y : Vec3::up();
    axis_z = t.scale.z > 1e-8f ? axis_z / t.scale.z : Vec3::unit_z();
    t.rotation = quat_from_basis(axis_x, axis_y, axis_z);
    return t;
}

Transform decompose_matrix(const Mat4& m) {
    float raw[16];
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) raw[c * 4 + r] = m.col[c][r];
    }
    return decompose(raw);
}

Transform node_local_transform(const cgltf_node* node) {
    if (node->has_matrix) return decompose(node->matrix);
    Transform t;
    t.position = Vec3{node->translation[0], node->translation[1], node->translation[2]};
    t.rotation = core::normalize(
        Quat{node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3]});
    t.scale = Vec3{node->scale[0], node->scale[1], node->scale[2]};
    return t;
}

Mat4 mat4_from_gltf(const float m[16]) {
    Mat4 out;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) out.col[c][r] = m[c * 4 + r];
    }
    return out;
}

Mat4 node_world_matrix(const cgltf_node* node) {
    float world[16];
    cgltf_node_transform_world(node, world);
    return mat4_from_gltf(world);
}

std::string joint_name(const cgltf_node* node, size_t index) {
    if (node->name && node->name[0]) return node->name;
    return "joint_" + std::to_string(index);
}

// One entry per distinct image the model needs, in the order they are first
// referenced. `srgb` records the colour space, which follows from the glTF slot
// the image appeared in and cannot be recovered from the pixels later.
struct ImageSlot {
    const cgltf_image* image;
    int index;
    bool srgb;
};

// Maps a texture reference to a slot, deduplicating so an image shared by
// several materials -- which this model does -- is decoded and uploaded once.
int image_index(const cgltf_texture_view& view, bool srgb, std::vector<ImageSlot>& slots) {
    if (!view.texture || !view.texture->image) return -1;
    const cgltf_image* image = view.texture->image;
    for (const auto& slot : slots) {
        if (slot.image == image) return slot.index;
    }
    const int index = int(slots.size());
    slots.push_back({image, index, srgb});
    return index;
}

}  // namespace

GltfLoadResult load_skinned_gltf(const char* path, Skeleton& out_skeleton,
                                 SkinnedMeshData& out_mesh) {
    GltfLoadResult result;

    cgltf_options options = {};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path, &data) != cgltf_result_success) {
        result.error = std::string("could not parse '") + path + "'";
        return result;
    }
    // Buffers hold the actual vertex data; for a .glb they live inside the file.
    if (cgltf_load_buffers(&options, data, path) != cgltf_result_success) {
        cgltf_free(data);
        result.error = "could not load buffers";
        return result;
    }
    if (data->skins_count == 0) {
        cgltf_free(data);
        result.error = "file contains no skin (is the model actually rigged?)";
        return result;
    }

    const cgltf_skin* skin = &data->skins[0];
    if (skin->joints_count > size_t(MAX_JOINTS)) {
        cgltf_free(data);
        result.error = "skin has " + std::to_string(skin->joints_count) + " joints, over the " +
                       std::to_string(MAX_JOINTS) + " limit";
        return result;
    }

    // ---- skeleton ----
    //
    // Joint bind transforms are derived from the file's inverse bind matrices
    // rather than from the node hierarchy's TRS.
    //
    // The inverse bind matrix IS the inverse of the joint's bind world transform,
    // by definition, so inverting it gives that transform exactly. Building the
    // skeleton from those makes "skinning at the bind pose is the identity" true
    // by construction, for any exporter, whatever transforms it left on ancestor
    // nodes. Reading node TRS instead means reconstructing the same information
    // through a chain of conventions -- which node absorbs the scene transform,
    // whether the mesh node's transform is divided out -- and getting any of it
    // wrong silently deforms the whole model. Two different assets disagreed
    // about those conventions; neither can disagree about this.
    //
    // glTF joint order is arbitrary, but this engine requires parents to precede
    // children, so joints are emitted in dependency order.
    out_skeleton = Skeleton();
    std::vector<int> gltf_to_skeleton(skin->joints_count, NO_PARENT);
    std::vector<bool> emitted(skin->joints_count, false);

    const bool have_inverse_binds =
        skin->inverse_bind_matrices && skin->inverse_bind_matrices->count >= skin->joints_count;

    std::vector<Mat4> bind_world(skin->joints_count, Mat4::identity());
    std::vector<Mat4> inverse_bind(skin->joints_count, Mat4::identity());
    if (have_inverse_binds) {
        for (size_t i = 0; i < skin->joints_count; ++i) {
            float m[16];
            cgltf_accessor_read_float(skin->inverse_bind_matrices, i, m, 16);
            inverse_bind[i] = mat4_from_gltf(m);
            bind_world[i] = core::inverse(inverse_bind[i]);
        }
    } else {
        for (size_t i = 0; i < skin->joints_count; ++i) {
            bind_world[i] = node_world_matrix(skin->joints[i]);
            inverse_bind[i] = core::inverse(bind_world[i]);
        }
    }

    auto find_joint_index = [&](const cgltf_node* node) -> int {
        if (!node) return -1;
        for (size_t i = 0; i < skin->joints_count; ++i) {
            if (skin->joints[i] == node) return int(i);
        }
        return -1;  // not part of the skin
    };

    size_t remaining = skin->joints_count;
    while (remaining > 0) {
        bool progress = false;
        for (size_t i = 0; i < skin->joints_count; ++i) {
            if (emitted[i]) continue;
            const cgltf_node* node = skin->joints[i];
            const int parent_gltf = find_joint_index(node->parent);
            if (parent_gltf >= 0 && !emitted[size_t(parent_gltf)]) continue;

            const int parent = parent_gltf >= 0 ? gltf_to_skeleton[size_t(parent_gltf)] : NO_PARENT;
            // Local transform relative to the parent's bind world, so the
            // hierarchy reproduces bind_world exactly.
            const Mat4 local_matrix =
                parent_gltf >= 0
                    ? core::inverse(bind_world[size_t(parent_gltf)]) * bind_world[i]
                    : bind_world[i];
            gltf_to_skeleton[i] =
                out_skeleton.add_joint(joint_name(node, i), parent, decompose_matrix(local_matrix));
            emitted[i] = true;
            --remaining;
            progress = true;
        }
        if (!progress) {
            cgltf_free(data);
            result.error = "joint hierarchy contains a cycle";
            return result;
        }
    }

    for (size_t i = 0; i < skin->joints_count; ++i) {
        out_skeleton.set_inverse_bind(gltf_to_skeleton[i], inverse_bind[i]);
    }
    LOG_INFO("inverse bind matrices: %s", have_inverse_binds ? "from file" : "derived from nodes");

    // ---- mesh ----
    out_mesh = SkinnedMeshData();
    std::vector<ImageSlot> image_slots;
    result.bounds_min = Vec3{1e30f, 1e30f, 1e30f};
    result.bounds_max = Vec3{-1e30f, -1e30f, -1e30f};

    for (size_t mesh_index = 0; mesh_index < data->meshes_count; ++mesh_index) {
        const cgltf_mesh* mesh = &data->meshes[mesh_index];
        for (size_t prim_index = 0; prim_index < mesh->primitives_count; ++prim_index) {
            const cgltf_primitive* primitive = &mesh->primitives[prim_index];
            if (primitive->type != cgltf_primitive_type_triangles) continue;

            const cgltf_accessor* positions = nullptr;
            const cgltf_accessor* normals = nullptr;
            const cgltf_accessor* joints = nullptr;
            const cgltf_accessor* weights = nullptr;
            const cgltf_accessor* uvs = nullptr;
            const cgltf_accessor* tangents = nullptr;
            for (size_t a = 0; a < primitive->attributes_count; ++a) {
                const cgltf_attribute* attribute = &primitive->attributes[a];
                switch (attribute->type) {
                    case cgltf_attribute_type_position: positions = attribute->data; break;
                    case cgltf_attribute_type_normal: normals = attribute->data; break;
                    case cgltf_attribute_type_joints: joints = attribute->data; break;
                    case cgltf_attribute_type_weights: weights = attribute->data; break;
                    case cgltf_attribute_type_texcoord:
                        // Only the set the base-colour texture actually uses.
                        if (attribute->index == 0) uvs = attribute->data;
                        break;
                    case cgltf_attribute_type_tangent: tangents = attribute->data; break;
                    default: break;
                }
            }
            // Unskinned primitives would be rigidly stuck at the origin, so they
            // are skipped rather than silently misplaced.
            if (!positions || !joints || !weights) continue;

            const uint32_t base = uint32_t(out_mesh.vertices.size());
            const size_t count = positions->count;
            for (size_t v = 0; v < count; ++v) {
                float position[3] = {};
                cgltf_accessor_read_float(positions, v, position, 3);
                float normal[3] = {0.0f, 1.0f, 0.0f};
                if (normals) cgltf_accessor_read_float(normals, v, normal, 3);

                cgltf_uint joint_index[4] = {0, 0, 0, 0};
                cgltf_accessor_read_uint(joints, v, joint_index, 4);
                float weight[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                cgltf_accessor_read_float(weights, v, weight, 4);

                int mapped[4] = {0, 0, 0, 0};
                for (int i = 0; i < 4; ++i) {
                    const size_t g = size_t(joint_index[i]);
                    mapped[i] = g < gltf_to_skeleton.size() ? gltf_to_skeleton[g] : 0;
                }

                float uv[2] = {0.0f, 0.0f};
                if (uvs) cgltf_accessor_read_float(uvs, v, uv, 2);

                const Vec3 local{position[0], position[1], position[2]};
                // No textures yet, so shade by height within the model: a
                // lighter underside is most of what reads as a creature rather
                // than a silhouette.
                const uint32_t index =
                    out_mesh.add(local, Vec3::one(), core::Vec2{uv[0], uv[1]}, mapped, weight);
                out_mesh.vertices[index].normal = Vec3{normal[0], normal[1], normal[2]};
                if (tangents) {
                    // glTF stores tangents as xyzw with w = +/-1 handedness, which
                    // is exactly what the shader wants; no derivation needed.
                    float tangent[4] = {1.0f, 0.0f, 0.0f, 1.0f};
                    cgltf_accessor_read_float(tangents, v, tangent, 4);
                    out_mesh.vertices[index].tangent =
                        core::Vec4{tangent[0], tangent[1], tangent[2], tangent[3]};
                }

                result.bounds_min = core::minv(result.bounds_min, local);
                result.bounds_max = core::maxv(result.bounds_max, local);
            }

            const uint32_t index_start = uint32_t(out_mesh.indices.size());
            if (primitive->indices) {
                for (size_t i = 0; i < primitive->indices->count; ++i) {
                    out_mesh.indices.push_back(
                        base + uint32_t(cgltf_accessor_read_index(primitive->indices, i)));
                }
            } else {
                for (size_t i = 0; i < count; ++i) out_mesh.indices.push_back(base + uint32_t(i));
            }

            // One submesh per primitive, so each keeps its own material.
            SkinnedSubmesh submesh;
            submesh.index_offset = index_start;
            submesh.index_count = uint32_t(out_mesh.indices.size()) - index_start;
            if (primitive->material) {
                const cgltf_material* material = primitive->material;
                if (material->has_pbr_metallic_roughness) {
                    const cgltf_pbr_metallic_roughness& pbr = material->pbr_metallic_roughness;
                    submesh.base_color_texture =
                        image_index(pbr.base_color_texture, true, image_slots);
                    submesh.orm_texture =
                        image_index(pbr.metallic_roughness_texture, false, image_slots);
                }
                // A normal map is useless without tangents, and this model only
                // ships them on the body meshes -- the eyes have neither.
                if (tangents) {
                    submesh.normal_texture =
                        image_index(material->normal_texture, false, image_slots);
                }
            }
            out_mesh.submeshes.push_back(submesh);
        }
    }

    // ---- animations ----
    for (size_t a = 0; a < data->animations_count; ++a) {
        const cgltf_animation* animation = &data->animations[a];
        AnimationClip clip;
        clip.name = animation->name ? animation->name : ("clip_" + std::to_string(a));

        for (size_t c = 0; c < animation->channels_count; ++c) {
            const cgltf_animation_channel* channel = &animation->channels[c];
            if (channel->target_path != cgltf_animation_path_type_rotation) continue;
            if (!channel->target_node || !channel->sampler) continue;

            int joint = NO_PARENT;
            for (size_t i = 0; i < skin->joints_count; ++i) {
                if (skin->joints[i] == channel->target_node) {
                    joint = gltf_to_skeleton[i];
                    break;
                }
            }
            if (joint == NO_PARENT) continue;  // targets something outside the skin

            const cgltf_animation_sampler* sampler = channel->sampler;
            RotationTrack track;
            track.joint = joint;
            track.step = sampler->interpolation == cgltf_interpolation_type_step;

            // Keys are stored as a delta from the node's own rest rotation, not
            // as the absolute rotation the file holds.
            //
            // The skeleton's bind rotations come from the inverse bind matrices,
            // which for this asset do not agree with the node hierarchy's TRS --
            // the joint below the root absorbs a whole scene transform that the
            // node's own rotation knows nothing about. Writing the file's
            // absolute rotation over that bind rotation discards the absorbed
            // transform and rolls the entire dragon onto its back. A delta is
            // immune to the disagreement: at the clip's rest key it is the
            // identity, so the bind pose is reproduced exactly no matter which
            // convention built the skeleton.
            const cgltf_node* node = channel->target_node;
            const Quat rest = node->has_rotation
                                  ? core::normalize(Quat{node->rotation[0], node->rotation[1],
                                                         node->rotation[2], node->rotation[3]})
                                  : Quat::identity();
            const Quat rest_inverse = core::conjugate(rest);

            const size_t keys = sampler->input->count;
            track.times.reserve(keys);
            track.rotations.reserve(keys);
            for (size_t k = 0; k < keys; ++k) {
                float t = 0.0f;
                cgltf_accessor_read_float(sampler->input, k, &t, 1);
                float q[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                // Cubic spline samplers store tangents either side of the value;
                // reading the plain value gives a usable linear approximation.
                cgltf_accessor_read_float(sampler->output, k, q, 4);
                track.times.push_back(t);
                // Post-multiplied, so the delta is expressed in the bone's own
                // frame -- which is what "this bone rotated" means, and is what
                // survives the two conventions describing that frame differently.
                track.rotations.push_back(
                    core::normalize(rest_inverse * core::normalize(Quat{q[0], q[1], q[2], q[3]})));
                clip.duration = core::maxf(clip.duration, t);
            }
            if (!track.times.empty()) clip.tracks.push_back(std::move(track));
        }

        if (clip.valid()) {
            LOG_INFO("animation '%s': %zu rotation tracks, %.2f s", clip.name.c_str(),
                     clip.tracks.size(), clip.duration);
            result.animations.push_back(std::move(clip));
        }
    }

    // Decode the base-colour images before releasing the glTF data: the image
    // bytes live in a buffer that cgltf_free releases, so decoding afterwards
    // reads freed memory. It even looked plausible -- every material appeared to
    // share one image, because the freed pointers happened to compare equal.
    result.textures.resize(image_slots.size());
    result.texture_srgb.assign(image_slots.size(), 1);
    for (const auto& slot : image_slots) {
        result.texture_srgb[size_t(slot.index)] = slot.srgb ? 1 : 0;
        const cgltf_image* image = slot.image;
        if (!image->buffer_view || !image->buffer_view->buffer ||
            !image->buffer_view->buffer->data) {
            LOG_WARN("image %d is not embedded; skipping", slot.index);
            continue;
        }
        const uint8_t* bytes = static_cast<const uint8_t*>(image->buffer_view->buffer->data) +
                               image->buffer_view->offset;
        result.textures[size_t(slot.index)] =
            gfx::decode_image(bytes, image->buffer_view->size);
    }

    cgltf_free(data);

    if (out_mesh.vertices.empty()) {
        result.error = "no skinned primitives found";
        return result;
    }

    // Tint the underside lighter, using the loaded bounds so it works whatever
    // scale the asset came in at.
    const float low = result.bounds_min.y;
    const float high = result.bounds_max.y;
    for (SkinnedVertex& v : out_mesh.vertices) {
        const float t = high > low ? core::saturate((v.position.y - low) / (high - low)) : 0.5f;
        v.color = core::lerp(Vec3{0.62f, 0.55f, 0.46f}, Vec3{0.30f, 0.26f, 0.30f},
                             core::smoothstep(0.25f, 0.75f, t));
    }

    // Check that the skin actually reconciles with the node hierarchy.
    //
    // The property that matters: at the bind pose, skinning must reproduce the
    // vertices exactly. skinning[j] = world_bind[j] * inverseBind[j], so if the
    // file's inverse binds and the hierarchy agree this is the identity and every
    // vertex lands where it started. When they disagree the whole model is
    // silently deformed, which is what a missing term in the skinning formula
    // looks like.
    //
    // This replaced a heuristic on how far vertices sit from their bones, which
    // false-positived on perfectly good assets -- a wing membrane is legitimately
    // far from the bone that drives it.
    {
        Pose bind;
        bind.reset_to_bind(out_skeleton);
        std::vector<Mat4> world, skinning;
        compute_world_matrices(out_skeleton, bind, world);
        compute_skinning_matrices(out_skeleton, world, skinning);

        const Vec3 extent = result.bounds_max - result.bounds_min;
        const float scale = core::maxf(core::maxf(extent.x, extent.y), extent.z);
        double total = 0.0;
        size_t counted = 0;
        for (size_t i = 0; i < out_mesh.vertices.size(); i += 7) {
            const SkinnedVertex& v = out_mesh.vertices[i];
            Vec3 blended = Vec3::zero();
            for (int k = 0; k < 4; ++k) {
                if (v.weights[k] <= 0.0f) continue;
                blended += core::transform_point(skinning[v.joints[k]], v.position) * v.weights[k];
            }
            total += double(core::length(blended - v.position));
            ++counted;
        }
        result.bind_pose_error =
            counted > 0 && scale > 1e-6f ? float(total / double(counted)) / scale : 0.0f;

        if (result.bind_pose_error > 0.01f) {
            char message[256];
            std::snprintf(message, sizeof(message),
                          "bind pose does not reconcile: skinning at rest moves the average "
                          "vertex by %.1f%% of the model's size, so the file's inverse bind "
                          "matrices disagree with its node hierarchy.",
                          result.bind_pose_error * 100.0f);
            result.error = message;
            LOG_WARN("%s", result.error.c_str());
            return result;
        }
    }

    result.ok = true;
    result.joint_count = out_skeleton.count();
    result.vertex_count = out_mesh.vertices.size();
    result.triangle_count = out_mesh.indices.size() / 3;
    LOG_INFO("glTF '%s': %d joints, %zu verts, %zu tris, %zu submesh(es), %zu texture(s)", path,
             result.joint_count, result.vertex_count, result.triangle_count,
             out_mesh.submeshes.size(), result.textures.size());
    return result;
}

}  // namespace anim
