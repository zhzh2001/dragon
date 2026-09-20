#pragma once

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <string>
#include <vector>

#include "gfx/device.h"

namespace gfx {

using PipelineHandle = uint32_t;
constexpr PipelineHandle INVALID_PIPELINE = 0xFFFFFFFFu;

// Everything needed to (re)build a graphics pipeline. Kept as data so hot
// reload can rebuild from the original description after the shader changes.
struct PipelineDesc {
    std::string name;         // for logs and GPU debugger labels
    std::string shader_path;  // relative to the shader root, e.g. "triangle.msl"
    std::string vs_entry = "vs_main";
    std::string fs_entry = "fs_main";

    // Resource counts must match what the shader actually declares, or the
    // Metal backend will bind to the wrong slots.
    uint32_t vs_uniform_buffers = 0;
    uint32_t fs_uniform_buffers = 0;
    uint32_t vs_samplers = 0;
    uint32_t fs_samplers = 0;
    uint32_t vs_storage_buffers = 0;
    uint32_t fs_storage_buffers = 0;

    std::vector<SDL_GPUVertexBufferDescription> vertex_buffers;
    std::vector<SDL_GPUVertexAttribute> vertex_attributes;

    SDL_GPUPrimitiveType primitive = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    SDL_GPUCullMode cull = SDL_GPU_CULLMODE_BACK;
    SDL_GPUFillMode fill = SDL_GPU_FILLMODE_FILL;
    SDL_GPUFrontFace front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

    bool depth_test = true;
    bool depth_write = true;
    bool alpha_blend = false;
    // Additive (src ONE, dst ONE): fire, glows, anything that is light rather
    // than surface. Wins over alpha_blend if both are set.
    bool additive_blend = false;

    // Defaults to GREATER for the reversed-Z main pass. The shadow pass uses a
    // conventional [0,1] depth range and so overrides this with LESS.
    SDL_GPUCompareOp depth_compare = SDL_GPU_COMPAREOP_GREATER;

    // Depth-only passes (shadow maps) have no colour attachment at all.
    bool no_color_target = false;
    // Colour-only passes (post-process) have no depth attachment at all.
    bool no_depth_target = false;

    // Overrides the colour target format. Zero means "use the scene format".
    SDL_GPUTextureFormat color_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    // Overrides the depth format. Zero means "use the device depth format".
    SDL_GPUTextureFormat depth_format = SDL_GPU_TEXTUREFORMAT_INVALID;
};

// Creates pipelines and rebuilds them when their shader source changes on disk.
//
// Handles stay valid across reloads: callers hold a PipelineHandle and resolve
// it through get() every frame, so a reload is invisible to them.
class PipelineCache {
public:
    void init(Device* device, std::string shader_root);
    void shutdown();

    // Returns INVALID_PIPELINE if the shader fails to compile. A failed
    // pipeline is still tracked, so fixing the file and saving will reload it.
    PipelineHandle create(PipelineDesc desc);

    // May return nullptr if the pipeline is currently broken (shader error).
    // Callers must check and skip drawing rather than assume success.
    SDL_GPUGraphicsPipeline* get(PipelineHandle handle) const;

    // Rebuilds any pipeline whose shader file changed since the last check.
    // Returns the number rebuilt. Cheap enough to call every frame.
    int poll_hot_reload();

    // Number of pipelines that currently have no valid GPU object.
    int broken_count() const;

private:
    // A shader file and the modification time we last built it at.
    struct SourceFile {
        std::string path;
        int64_t mtime = 0;
    };

    struct Entry {
        PipelineDesc desc;
        SDL_GPUGraphicsPipeline* pipeline = nullptr;
        // The shader itself plus every file it includes, so editing a shared
        // header reloads all the pipelines that depend on it.
        std::vector<SourceFile> sources;
    };

    // Returns false and leaves entry.pipeline untouched on compile failure, so
    // a bad save keeps the last working shader on screen.
    bool build(Entry& entry);

    Device* device_ = nullptr;
    std::string shader_root_;
    std::vector<Entry> entries_;
};

}  // namespace gfx
