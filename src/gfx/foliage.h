#pragma once

#include <cstdint>
#include <vector>

#include "core/math.h"
#include "gfx/foliage_instance.h"
#include "gfx/mesh.h"
#include "gfx/noise_lattice.h"
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
// Rocks ride the same instanced static path as trees (DIRECTION.md row 7):
// boulders, slabs, crags, scree and outcrops, meshes from
// assets/props/rocks.glb (tools/rocks.md) or generated when it is absent.
constexpr int ROCK_KINDS = 6;
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
    // A rock kind's mesh (vertex colour already in the plant encoding), and
    // where they lie. Drawn with the trees, in the main and shadow passes.
    void set_rock_mesh(Device& device, int kind, const MeshData& mesh, float height);
    void set_rocks(Device& device, int kind, const std::vector<FoliageInstance>& rocks);
    uint32_t rock_count() const;
    // Per-frame staging of one grass kind, before any render pass opens.
    void upload_grass(Device& device, GrassKind kind, const std::vector<FoliageInstance>& grass);
    // The baked noise lattice the bark grains with, on a tier that bakes it.
    void set_noise_lattice(const NoiseLattice* lattice) { noise_lattice_ = lattice; }

    void draw_trees(Device& device, rhi::Pass* pass, const SceneUniforms& scene);
    void draw_grass(Device& device, rhi::Pass* pass, const SceneUniforms& scene);
    void draw_trees_depth(Device& device, rhi::Pass* pass,
                          const core::Mat4& light_view_proj, float time,
                          core::Vec3 eye = core::Vec3::zero());

    uint32_t tree_count() const;
    // Trees drawn in the last main pass, after the cell culling.
    uint32_t trees_drawn() const { return trees_drawn_; }
    // Trees past this distance from the camera are not drawn at all. Beyond
    // it a tree is under a pixel; the LOD dither has already taken its crowns.
    float tree_draw_distance = 4200.0f;
    // Rocks stop sooner: 4,200 of them at ~900 triangles, drawn to the tree
    // distance and into the shadow map, cost the arena a third of its frame
    // time. Past these a boulder is a pixel or two; the big kinds (big
    // boulder, crag, outcrop) carry further. Their shadows are only cast
    // near the camera, where a rock's shadow is bigger than a texel.
    float rock_draw_distance = 1400.0f;
    float big_rock_draw_distance = 2600.0f;
    float rock_shadow_distance = 600.0f;
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
    rhi::Texture* leaf_texture_ = nullptr;
    rhi::Texture* needle_texture_ = nullptr;
    rhi::Sampler* card_sampler_ = nullptr;
    const NoiseLattice* noise_lattice_ = nullptr;
    // Binds the two card textures at fragment slots `first` and `first + 1`,
    // and the noise lattice after them when there is one.
    void bind_cards(Device& device, rhi::Pass* pass, uint32_t first) const;
    // Static instances are uploaded sorted into square ground cells, each
    // with a bounding sphere, so a draw is one instanced call per cell that
    // the frustum and the distance admit. Six thousand trees over the valley
    // were one draw; sixteen thousand out to the skirt could not be.
    struct Cell {
        core::Vec3 centre;
        float radius = 0.0f;
        uint32_t first = 0;
        uint32_t count = 0;
    };
    struct StaticSet {
        Mesh mesh;
        // Trees: the indices before the detail cards (the crown's coarse set).
        uint32_t coarse_indices = 0;
        rhi::Buffer* instances = nullptr;
        uint32_t count = 0;
        std::vector<Cell> cells;
    };
    static constexpr float CELL_SIZE = 320.0f;
    uint32_t trees_drawn_ = 0;
    struct StreamSet {
        Mesh mesh;
        rhi::Buffer* instances = nullptr;
        uint32_t capacity = 0;
        uint32_t uploaded = 0;
    };
    void draw(Device& device, rhi::Pass* pass, const SceneUniforms& scene,
              const Mesh& mesh, rhi::Buffer* instances, uint32_t count, float fade_start,
              float fade_end, float height);
    bool ensure_capacity(StreamSet& set, uint32_t count, const char* name);

    Device* device_ = nullptr;
    PipelineCache* pipelines_ = nullptr;
    ShadowMap* shadow_map_ = nullptr;
    PipelineHandle pipeline_ = INVALID_PIPELINE;
    PipelineHandle depth_pipeline_ = INVALID_PIPELINE;

    StaticSet trees_[TREE_KINDS];
    StaticSet rocks_[ROCK_KINDS];
    float rock_height_[ROCK_KINDS] = {};
    void fill_static(Device& device, StaticSet& set, const std::vector<FoliageInstance>& items,
                     float height, const char* name);

    StreamSet grass_[GRASS_KINDS];
};

// The plant meshes, generated. Vertex colour carries the material.
MeshData make_tree_mesh(TreeKind kind);
MeshData make_grass_mesh(GrassKind kind);
// A generated rock of `kind` (the rocks.glb order: boulder, big boulder,
// slab, crag, scree, outcrop), for when the file is missing: a displaced
// sphere at the kind's size, grey with dark crevices, base 0.6 m under y 0.
MeshData make_rock_mesh(int kind);
// A rock mesh from the file into the plant encoding: its grey vertex
// colour becomes the brightness of the palette's rock entry.
MeshData encode_rock_mesh(const MeshData& source);
// Each kind's nominal size, x height z, metres (tools/rocks.md).
core::Vec3 rock_size(int kind);

}  // namespace gfx
