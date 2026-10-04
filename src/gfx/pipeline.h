#pragma once

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
    std::string name;    // for logs and GPU debugger labels
    // The shader's stem under the shader root: "terrain" is terrain.hlsl,
    // whose entry points are always vs_main and fs_main. The resource counts
    // the backend needs come from reflection, not from here, so they cannot
    // drift from what the shader declares.
    std::string shader;

    std::vector<rhi::VertexBufferLayout> vertex_buffers;
    std::vector<rhi::VertexAttribute> vertex_attributes;

    rhi::Primitive primitive = rhi::Primitive::TriangleList;
    rhi::Cull cull = rhi::Cull::Back;
    rhi::Fill fill = rhi::Fill::Solid;
    rhi::FrontFace front_face = rhi::FrontFace::CounterClockwise;

    bool depth_test = true;
    bool depth_write = true;
    bool alpha_blend = false;
    // Additive (src ONE, dst ONE): fire, glows, anything that is light rather
    // than surface. Wins over alpha_blend if both are set.
    bool additive_blend = false;

    // Defaults to GREATER for the reversed-Z main pass. The shadow pass uses a
    // conventional [0,1] depth range and so overrides this with LESS.
    rhi::Compare depth_compare = rhi::Compare::Greater;

    // Depth-only passes (shadow maps) have no colour attachment at all.
    bool no_color_target = false;
    // Colour-only passes (post-process) have no depth attachment at all.
    bool no_depth_target = false;

    // Overrides the colour target format. Invalid means "use the scene HDR format".
    rhi::Format color_format = rhi::Format::Invalid;
    // Overrides the depth format. Invalid means "use the device depth format".
    rhi::Format depth_format = rhi::Format::Invalid;
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
    rhi::Pipeline* get(PipelineHandle handle) const;

    // Rebuilds any pipeline whose shader file changed since the last check.
    // Returns the number rebuilt. Cheap enough to call every frame.
    int poll_hot_reload();
    // Builds every pipeline again, for a change of defines (a setting that
    // is a shader variant, gfx/graphics_settings.h). The GPU must be idle.
    void rebuild_all();

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
        rhi::Pipeline* pipeline = nullptr;
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
