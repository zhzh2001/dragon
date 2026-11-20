#include "gfx/rock_kit.h"

#include <cmath>
#include <cstring>
#include <string>

#include "cgltf.h"  // the implementation is compiled in anim/gltf_loader.cpp
#include "core/log.h"
#include "core/paths.h"
#include "gfx/atmosphere.h"
#include "gfx/device.h"
#include "gfx/frustum.h"
#include "gfx/palette.h"
#include "gfx/render_tier.h"
#include "gfx/shadow_map.h"
#include "gfx/texture.h"

namespace gfx {

namespace {

// The kit's vertex: what the files carry (tools/rockkit.md, "File contract").
struct RockVertex {
    core::Vec3 position;
    core::Vec3 normal;
    core::Vec2 uv;
    core::Vec4 tangent;  // xyz the direction, w the handedness
};

// Both stages read it (rock.hlsl's RockParams).
struct RockParams {
    core::Vec4 albedo_scale;  // rgb the piece's colour match, a the moss
    core::Vec4 contact;       // x height (m), y strength, z the terrain tint, w unused
};

std::vector<rhi::VertexBufferLayout> rock_buffers() {
    rhi::VertexBufferLayout vertices = {};
    vertices.slot = 0;
    vertices.pitch = sizeof(RockVertex);
    rhi::VertexBufferLayout instances = {};
    instances.slot = 1;
    instances.pitch = sizeof(RockInstance);
    instances.rate = rhi::InputRate::Instance;
    return {vertices, instances};
}

std::vector<rhi::VertexAttribute> rock_attributes(bool depth_only) {
    std::vector<rhi::VertexAttribute> a;
    const auto add = [&](uint32_t location, uint32_t slot, rhi::VertexFormat format, uint32_t offset) {
        rhi::VertexAttribute attribute = {};
        attribute.location = location;
        attribute.buffer_slot = slot;
        attribute.format = format;
        attribute.offset = offset;
        a.push_back(attribute);
    };
    add(0, 0, rhi::VertexFormat::Float3, offsetof(RockVertex, position));
    if (!depth_only) {
        add(1, 0, rhi::VertexFormat::Float3, offsetof(RockVertex, normal));
        add(2, 0, rhi::VertexFormat::Float2, offsetof(RockVertex, uv));
        add(3, 0, rhi::VertexFormat::Float4, offsetof(RockVertex, tangent));
    }
    // The shader's inputs are numbered in the order it declares them, so
    // the depth stage's three are 0, 1, 2.
    add(depth_only ? 1 : 4, 1, rhi::VertexFormat::Float4, offsetof(RockInstance, position_scale));
    add(depth_only ? 2 : 5, 1, rhi::VertexFormat::Float4, offsetof(RockInstance, rotation));
    if (!depth_only) {
        add(6, 1, rhi::VertexFormat::Float4, offsetof(RockInstance, ground));
        add(7, 1, rhi::VertexFormat::Float4, offsetof(RockInstance, params));
    }
    return a;
}

const cgltf_accessor* attribute(const cgltf_primitive& primitive, cgltf_attribute_type type) {
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        const cgltf_attribute& a = primitive.attributes[i];
        if (a.type == type && a.index == 0) return a.data;
    }
    return nullptr;
}

ImageData decode_embedded(const cgltf_texture_view& view) {
    if (!view.texture || !view.texture->image) return ImageData{};
    const cgltf_image* image = view.texture->image;
    if (!image->buffer_view || !image->buffer_view->buffer || !image->buffer_view->buffer->data) return ImageData{};
    const uint8_t* bytes = static_cast<const uint8_t*>(image->buffer_view->buffer->data) + image->buffer_view->offset;
    return decode_image(bytes, image->buffer_view->size);
}

float srgb_to_linear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

}  // namespace

bool RockKit::init(Device* device, PipelineCache* pipelines, ShadowMap* shadow_map) {
    device_ = device;
    pipelines_ = pipelines;
    shadow_map_ = shadow_map;
    if (active_tier().tier != Tier::Modern) return false;
    PipelineDesc desc;
    desc.name = "rock";
    desc.shader = "rock";
    desc.vertex_buffers = rock_buffers();
    desc.vertex_attributes = rock_attributes(false);
    // Both faces: a cliff piece is a shell, and where its rim stands out of
    // the ground (gfx::VegetationSettings::cliff_rim_tolerance) its back is
    // shaded as rock (rock.hlsl) instead of showing a hole.
    desc.cull = rhi::Cull::None;
    pipeline_ = pipelines_->create(desc);
    if (shadow_map && shadow_map->available()) {
        PipelineDesc depth;
        depth.name = "shadow_rock";
        depth.shader = "rock_depth";
        depth.vertex_buffers = rock_buffers();
        depth.vertex_attributes = rock_attributes(true);
        depth.no_color_target = true;
        depth.depth_format = shadow_map->format();
        depth.depth_compare = rhi::Compare::Less;
        // Both faces: a cliff piece is a shell, and its back would let the
        // sun through where the rim dives under the ground.
        depth.cull = rhi::Cull::None;
        depth_pipeline_ = pipelines_->create(depth);
    }
    sampler_ = create_model_sampler(device->rhi());
    return pipeline_ != INVALID_PIPELINE;
}

bool RockKit::load(Device& device) {
    release(device);
    if (pipeline_ == INVALID_PIPELINE) return false;
    rhi::Device& rhi = device.rhi();
    static const char* suffix[ROCK_LODS] = {"", "_lod1", "_lod2"};
    uint64_t triangles[ROCK_LODS] = {};
    for (int p = 0; p < ROCK_PIECES; ++p) {
        Piece& piece = pieces_[p];
        const char* name = rock_piece(p).name;
        for (int l = 0; l < ROCK_LODS; ++l) {
            const std::string path = core::paths::asset(std::string(rock_piece(p).landmark ? "props/landmarks/" : "props/rockkit/") +
                                                        name + suffix[l] + ".glb");
            cgltf_options options;
            std::memset(&options, 0, sizeof(options));
            cgltf_data* data = nullptr;
            if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success ||
                cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success || data->meshes_count < 1 ||
                data->meshes[0].primitives_count < 1) {
                if (data) cgltf_free(data);
                LOG_WARN("rock kit: could not read %s; the modern tier keeps the old rocks", path.c_str());
                release(device);
                return false;
            }
            const cgltf_primitive& primitive = data->meshes[0].primitives[0];
            const cgltf_accessor* positions = attribute(primitive, cgltf_attribute_type_position);
            const cgltf_accessor* normals = attribute(primitive, cgltf_attribute_type_normal);
            const cgltf_accessor* uvs = attribute(primitive, cgltf_attribute_type_texcoord);
            const cgltf_accessor* tangents = attribute(primitive, cgltf_attribute_type_tangent);
            if (!positions || !normals || !uvs || !tangents || !primitive.indices) {
                cgltf_free(data);
                LOG_WARN("rock kit: %s lacks a position, normal, uv, tangent or index; the old rocks stay", path.c_str());
                release(device);
                return false;
            }
            std::vector<RockVertex> vertices(positions->count);
            core::Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
            for (cgltf_size v = 0; v < positions->count; ++v) {
                float f[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                cgltf_accessor_read_float(positions, v, f, 3);
                vertices[v].position = core::Vec3{f[0], f[1], f[2]};
                lo = core::minv(lo, vertices[v].position);
                hi = core::maxv(hi, vertices[v].position);
                cgltf_accessor_read_float(normals, v, f, 3);
                vertices[v].normal = core::Vec3{f[0], f[1], f[2]};
                cgltf_accessor_read_float(uvs, v, f, 2);
                vertices[v].uv = core::Vec2{f[0], f[1]};
                cgltf_accessor_read_float(tangents, v, f, 4);
                vertices[v].tangent = core::Vec4{f[0], f[1], f[2], f[3]};
            }
            std::vector<uint32_t> indices(primitive.indices->count);
            for (cgltf_size i = 0; i < indices.size(); ++i) {
                indices[i] = uint32_t(cgltf_accessor_read_index(primitive.indices, i));
            }
            Level& level = piece.levels[l];
            level.vertices = rhi.create_buffer(rhi::BufferUsage::Vertex,
                                               uint32_t(vertices.size() * sizeof(RockVertex)), vertices.data(), name);
            level.indices = rhi.create_buffer(rhi::BufferUsage::Index, uint32_t(indices.size() * sizeof(uint32_t)),
                                              indices.data(), name);
            level.index_count = uint32_t(indices.size());
            triangles[l] += indices.size() / 3;
            if (l == 0) {
                piece.bound_centre = (lo + hi) * 0.5f;
                piece.bound_radius = core::length(hi - lo) * 0.5f;
            }
            // The 1024 maps, LOD1's, for every level (tools/rockkit.md:
            // one atlas layout per piece). 2048 for the eight pieces' three
            // maps would be 512 MB uncompressed with mips; 1024 is 128 MB.
            if (l == 1 && primitive.material) {
                const cgltf_material& m = *primitive.material;
                ImageData base = decode_embedded(m.pbr_metallic_roughness.base_color_texture);
                ImageData normal = decode_embedded(m.normal_texture);
                ImageData orm = decode_embedded(m.occlusion_texture.texture ? m.occlusion_texture
                                                                            : m.pbr_metallic_roughness.metallic_roughness_texture);
                if (base.valid()) {
                    // The mean linear albedo, which the shader pulls to the
                    // terrain's rock (RockKit::colour_match).
                    double sum[3] = {0.0, 0.0, 0.0};
                    float lut[256];
                    for (int i = 0; i < 256; ++i) lut[i] = srgb_to_linear(float(i) / 255.0f);
                    const size_t texels = size_t(base.width) * size_t(base.height);
                    for (size_t t = 0; t < texels; ++t) {
                        for (int c = 0; c < 3; ++c) sum[c] += lut[base.rgba[t * 4 + size_t(c)]];
                    }
                    piece.mean_albedo = core::Vec3{float(sum[0] / double(texels)), float(sum[1] / double(texels)),
                                                   float(sum[2] / double(texels))};
                }
                const std::string stem = std::string("rock ") + name;
                piece.base = create_texture_from_image(rhi, base, (stem + " base").c_str(), TextureKind::Colour);
                piece.normal = create_texture_from_image(rhi, normal, (stem + " normal").c_str(), TextureKind::NormalMap);
                piece.orm = create_texture_from_image(rhi, orm, (stem + " orm").c_str(), TextureKind::Orm);
            }
            cgltf_free(data);
            if (!level.vertices || !level.indices) {
                LOG_WARN("rock kit: could not upload %s", path.c_str());
                release(device);
                return false;
            }
        }
        if (!piece.base || !piece.normal || !piece.orm) {
            LOG_WARN("rock kit: %s_lod1.glb lacks one of its three maps; the old rocks stay", name);
            release(device);
            return false;
        }
        LOG_INFO("rock kit: %s, mean albedo (%.3f, %.3f, %.3f)", name, double(piece.mean_albedo.x),
                 double(piece.mean_albedo.y), double(piece.mean_albedo.z));
    }
    LOG_INFO("rock kit: %d pieces, %llu / %llu / %llu triangles a level, %d maps at 1024", ROCK_PIECES,
             (unsigned long long)triangles[0], (unsigned long long)triangles[1], (unsigned long long)triangles[2], 3 * ROCK_PIECES);
    loaded_ = true;
    return true;
}

void RockKit::release(Device& device) {
    rhi::Device& rhi = device.rhi();
    for (Piece& piece : pieces_) {
        for (Level& level : piece.levels) {
            rhi.destroy(level.vertices);
            rhi.destroy(level.indices);
            level = Level{};
        }
        rhi.destroy(piece.base);
        rhi.destroy(piece.normal);
        rhi.destroy(piece.orm);
        piece.base = piece.normal = piece.orm = nullptr;
    }
    loaded_ = false;
}

void RockKit::shutdown(Device& device) {
    release(device);
    device.rhi().destroy(frame_instances_);
    frame_instances_ = nullptr;
    frame_capacity_ = 0;
    device.rhi().destroy(sampler_);
    sampler_ = nullptr;
}

void RockKit::set_instances(int piece, const std::vector<RockInstance>& instances) {
    if (piece < 0 || piece >= ROCK_PIECES) return;
    pieces_[piece].instances = instances;
}

uint32_t RockKit::count() const {
    uint32_t n = 0;
    for (const Piece& piece : pieces_) n += uint32_t(piece.instances.size());
    return n;
}

void RockKit::prepare(Device& device, const SceneUniforms& scene, const Reach& reach) {
    for (auto& list : main_) for (Range& r : list) r = Range{};
    for (auto& list : shadow_) for (Range& r : list) r = Range{};
    drawn_ = 0;
    if (!loaded_) return;
    const Frustum frustum(scene.view_proj);
    const core::Vec3 eye = scene.camera_position.xyz();
    for (auto& pass : scratch_) for (auto& piece : pass) for (auto& level : piece) level.clear();
    for (int p = 0; p < ROCK_PIECES; ++p) {
        const Piece& piece = pieces_[p];
        const bool cliff = rock_piece(p).cliff;
        for (const RockInstance& rock : piece.instances) {
            const float scale = rock.position_scale.w;
            const core::Quat q{rock.rotation.x, rock.rotation.y, rock.rotation.z, rock.rotation.w};
            const core::Vec3 centre = rock.position_scale.xyz() + core::rotate(q, piece.bound_centre * scale);
            const float radius = piece.bound_radius * scale;
            const float distance = core::maxf(core::distance(eye, centre) - radius, 0.0f);
            // A cliff piece, or a boulder grown past 4 m across, carries
            // as far as the old big kinds did.
            const float far = (cliff || radius > 4.0f) ? reach.big : reach.small;
            // By its size on the screen, in radii, and for a large piece by
            // metres too: a wall crag's radius is 70 m and more, and in radii
            // alone 80 of them drew 24,000 triangles each out to 1.5 km.
            const float r = core::maxf(radius, 0.5f);
            const int level = distance < core::minf(lod1_radii * r, lod1_metres)   ? 0
                              : distance < core::minf(lod2_radii * r, lod2_metres) ? 1
                                                                                   : 2;
            if (distance < reach.shadow) scratch_[1][p][level].push_back(rock);
            if (distance > far || !frustum.sees(centre, radius)) continue;
            RockInstance drawn = rock;
            // The fade: 1 whole, 0 gone into the ground (rock.hlsl).
            const float fade_from = far * (1.0f - reach.fade_band);
            drawn.params.w = 1.0f - core::smoothstep(fade_from, far, distance);
            scratch_[0][p][level].push_back(drawn);
        }
    }
    frame_.clear();
    drawn_triangles_ = 0;
    for (int pass = 0; pass < 2; ++pass) {
        for (int p = 0; p < ROCK_PIECES; ++p) {
            for (int l = 0; l < ROCK_LODS; ++l) {
                const std::vector<RockInstance>& list = scratch_[pass][p][l];
                Range& range = pass == 0 ? main_[p][l] : shadow_[p][l];
                range.first = uint32_t(frame_.size());
                range.count = uint32_t(list.size());
                frame_.insert(frame_.end(), list.begin(), list.end());
                if (pass == 0) {
                    drawn_ += range.count;
                    drawn_triangles_ += range.count * (pieces_[p].levels[l].index_count / 3);
                }
            }
        }
    }
    if (frame_.empty()) return;
    rhi::Device& rhi = device.rhi();
    if (frame_.size() > frame_capacity_) {
        rhi.destroy(frame_instances_);
        frame_capacity_ = uint32_t(frame_.size() + frame_.size() / 2 + 256);
        frame_instances_ = rhi.create_buffer(rhi::BufferUsage::Vertex, frame_capacity_ * uint32_t(sizeof(RockInstance)),
                                             nullptr, "rock instances");
        if (!frame_instances_) {
            frame_capacity_ = 0;
            for (auto& list : main_) for (Range& r : list) r = Range{};
            for (auto& list : shadow_) for (Range& r : list) r = Range{};
            return;
        }
    }
    const uint32_t bytes = uint32_t(frame_.size() * sizeof(RockInstance));
    void* mapped = rhi.map_upload(frame_instances_, bytes);
    if (!mapped) return;
    std::memcpy(mapped, frame_.data(), bytes);
    rhi.commit_upload(frame_instances_, bytes);
}

void RockKit::draw(Device& device, rhi::Pass* pass, const SceneUniforms& scene, const Atmosphere* atmosphere) {
    if (!pass || !loaded_ || !frame_instances_ || drawn_ == 0) return;
    rhi::Pipeline* pipeline = pipelines_->get(pipeline_);
    if (!pipeline) return;
    rhi::Device& rhi = device.rhi();
    rhi.bind_pipeline(pass, pipeline);
    rhi.push_uniforms(rhi::Stage::Vertex, 0, &scene, sizeof(SceneUniforms));
    rhi.push_uniforms(rhi::Stage::Fragment, 0, &scene, sizeof(SceneUniforms));
    if (shadow_map_ && shadow_map_->texture()) {
        rhi::TextureBinding binding = {};
        binding.texture = shadow_map_->texture();
        binding.sampler = shadow_map_->sampler();
        rhi.bind_fragment_textures(pass, 0, &binding, 1);
    }
    if (atmosphere) atmosphere->bind(rhi, pass, 4, true);
    // The terrain's rock, as terrain.hlsl mixes it: Rock and RockDark by
    // the patch noise, around `tone`.
    const core::Vec4 rock = scene.palette[int(PaletteEntry::Rock)];
    const core::Vec4 dark = scene.palette[int(PaletteEntry::RockDark)];
    const core::Vec3 target = core::lerp(dark.xyz(), rock.xyz(), tone) * brightness;
    rhi::BufferBinding instances = {};
    instances.buffer = frame_instances_;
    rhi.bind_vertex_buffers(pass, 1, &instances, 1);
    for (int p = 0; p < ROCK_PIECES; ++p) {
        const Piece& piece = pieces_[p];
        bool any = false;
        for (const Range& r : main_[p]) any = any || r.count > 0;
        if (!any) continue;
        RockParams params;
        const core::Vec3 mean = piece.mean_albedo;
        const core::Vec3 match{target.x / core::maxf(mean.x, 0.01f), target.y / core::maxf(mean.y, 0.01f),
                               target.z / core::maxf(mean.z, 0.01f)};
        // A landmark keeps its own colours: masonry and carved stone are
        // not the valley's bedrock, and take none of its warm and cool drift.
        const bool landmark = rock_piece(p).landmark;
        const core::Vec3 scale = landmark ? core::Vec3::one() * landmark_brightness
                                          : core::lerp(core::Vec3::one(), match, colour_match);
        params.albedo_scale = core::Vec4{scale.x, scale.y, scale.z, moss};
        const core::Vec3 size = rock_piece(p).size;
        params.contact = core::Vec4{contact_height, contact, landmark ? 0.0f : terrain_tint,
                                    detail_ ? core::maxf(size.x, core::maxf(size.y, size.z)) * detail : 0.0f};
        rhi.push_uniforms(rhi::Stage::Vertex, 1, &params, sizeof(params));
        rhi.push_uniforms(rhi::Stage::Fragment, 1, &params, sizeof(params));
        rhi::TextureBinding maps[3] = {};
        maps[0].texture = piece.base;
        maps[1].texture = piece.normal;
        maps[2].texture = piece.orm;
        for (rhi::TextureBinding& m : maps) m.sampler = sampler_;
        rhi.bind_fragment_textures(pass, 1, maps, 3);
        // The detail layer's map, or any map where there is none: the slot must hold one.
        rhi::TextureBinding fine = {};
        fine.texture = detail_ ? detail_ : piece.normal;
        fine.sampler = sampler_;
        rhi.bind_fragment_textures(pass, 6, &fine, 1);
        for (int l = 0; l < ROCK_LODS; ++l) {
            const Range& r = main_[p][l];
            if (r.count == 0) continue;
            const Level& level = piece.levels[l];
            rhi::BufferBinding vertices = {};
            vertices.buffer = level.vertices;
            rhi.bind_vertex_buffers(pass, 0, &vertices, 1);
            rhi.bind_index_buffer(pass, rhi::BufferBinding{level.indices, 0}, rhi::IndexSize::U32);
            rhi.draw_indexed(pass, level.index_count, r.count, 0, 0, r.first);
        }
    }
}

void RockKit::draw_depth(Device& device, rhi::Pass* pass, const core::Mat4& light_view_proj) {
    if (!pass || !loaded_ || !frame_instances_) return;
    rhi::Pipeline* pipeline = pipelines_->get(depth_pipeline_);
    if (!pipeline) return;
    rhi::Device& rhi = device.rhi();
    rhi.bind_pipeline(pass, pipeline);
    rhi.push_uniforms(rhi::Stage::Vertex, 0, &light_view_proj, sizeof(core::Mat4));
    rhi::BufferBinding instances = {};
    instances.buffer = frame_instances_;
    rhi.bind_vertex_buffers(pass, 1, &instances, 1);
    for (int p = 0; p < ROCK_PIECES; ++p) {
        for (int l = 0; l < ROCK_LODS; ++l) {
            const Range& r = shadow_[p][l];
            if (r.count == 0) continue;
            const Level& level = pieces_[p].levels[l];
            rhi::BufferBinding vertices = {};
            vertices.buffer = level.vertices;
            rhi.bind_vertex_buffers(pass, 0, &vertices, 1);
            rhi.bind_index_buffer(pass, rhi::BufferBinding{level.indices, 0}, rhi::IndexSize::U32);
            rhi.draw_indexed(pass, level.index_count, r.count, 0, 0, r.first);
        }
    }
}

}  // namespace gfx
