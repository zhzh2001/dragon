#pragma once

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <vector>

#include "core/math.h"
#include "gfx/foliage_instance.h"
#include "gfx/mesh.h"
#include "gfx/pipeline.h"
#include "gfx/scene_uniforms.h"

namespace gfx {

class Device;
class ShadowMap;

// The kinds of plant. Each is one generated mesh drawn instanced; the
// placement code decides which kind stands where.
enum class TreeKind : int { Spruce = 0, Pine, Broadleaf, Dead, Count };
enum class GrassKind : int { Tuft = 0, Reed, Bush, Count };

constexpr int TREE_KINDS = int(TreeKind::Count);
constexpr int GRASS_KINDS = int(GrassKind::Count);

// Instanced plants: a few generated meshes, each drawn thousands of times from
// an instance buffer. Trees are static (uploaded when planted, and they cast
// shadows); grass is streamed every frame around the camera, like the
// particles, and casts none -- a shadow map texel is bigger than a blade.
class Foliage {
public:
    bool init(Device* device, PipelineCache* pipelines, ShadowMap* shadow_map);
    void shutdown(Device& device);

    // Load-time upload of one tree kind; replaces that kind's previous forest.
    void set_trees(Device& device, TreeKind kind, const std::vector<FoliageInstance>& trees);
    // Per-frame staging of one grass kind, before any render pass opens.
    void upload_grass(Device& device, GrassKind kind, const std::vector<FoliageInstance>& grass);

    void draw_trees(Device& device, SDL_GPURenderPass* pass, const SceneUniforms& scene);
    void draw_grass(Device& device, SDL_GPURenderPass* pass, const SceneUniforms& scene);
    void draw_trees_depth(Device& device, SDL_GPURenderPass* pass,
                          const core::Mat4& light_view_proj, float time);

    uint32_t tree_count() const;
    uint32_t grass_count() const;

    // Sway amplitude in metres at the top of a plant.
    float wind = 1.0f;
    // Grass shrinks to nothing between these camera distances.
    float grass_fade_start = 90.0f;
    float grass_fade_end = 130.0f;
    // Past this camera distance a crown's detail cards are dropped and its
    // coarse cards carry it: the impostor, without a second mesh or a second
    // draw. Dithered over a band so trees do not pop.
    float lod_distance = 240.0f;

    // Nominal height of each kind at scale 1, for the sway's height fraction.
    static float tree_height(TreeKind kind);
    static float grass_height(GrassKind kind);

private:
    struct Params {
        core::Vec4 wind_time_fade;  // x wind, y time, z fade start, w fade end
        core::Vec4 extra;           // x nominal height, y LOD distance
    };
    SDL_GPUTexture* leaf_texture_ = nullptr;
    SDL_GPUTexture* needle_texture_ = nullptr;
    SDL_GPUSampler* card_sampler_ = nullptr;
    // Binds the two card textures at fragment slots `first` and `first + 1`.
    void bind_cards(SDL_GPURenderPass* pass, uint32_t first) const;
    struct StaticSet {
        Mesh mesh;
        SDL_GPUBuffer* instances = nullptr;
        uint32_t count = 0;
    };
    struct StreamSet {
        Mesh mesh;
        SDL_GPUBuffer* instances = nullptr;
        SDL_GPUTransferBuffer* transfer = nullptr;
        uint32_t capacity = 0;
        uint32_t uploaded = 0;
    };
    void draw(Device& device, SDL_GPURenderPass* pass, const SceneUniforms& scene,
              const Mesh& mesh, SDL_GPUBuffer* instances, uint32_t count, float fade_start,
              float fade_end, float height);
    bool ensure_capacity(StreamSet& set, uint32_t count, const char* name);

    Device* device_ = nullptr;
    PipelineCache* pipelines_ = nullptr;
    ShadowMap* shadow_map_ = nullptr;
    PipelineHandle pipeline_ = INVALID_PIPELINE;
    PipelineHandle depth_pipeline_ = INVALID_PIPELINE;

    StaticSet trees_[TREE_KINDS];
    StreamSet grass_[GRASS_KINDS];
};

// The plant meshes, generated. Vertex colour carries the material.
MeshData make_tree_mesh(TreeKind kind);
MeshData make_grass_mesh(GrassKind kind);

}  // namespace gfx
