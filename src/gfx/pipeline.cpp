#include "gfx/pipeline.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>

#include <cstdlib>
#include <unordered_set>

#ifdef DRAGON_SHADERCROSS
#include <SDL3_shadercross/SDL_shadercross.h>
#endif

#include "core/log.h"

namespace gfx {
namespace {

// Reads a whole text file. Returns empty on failure.
std::string read_file(const std::string& path) {
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) return {};
    std::string out(static_cast<char*>(data), size);
    SDL_free(data);
    return out;
}

int64_t file_mtime(const std::string& path) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path.c_str(), &info)) return 0;
    return info.modify_time;
}

// We inline `#include "file"` ourselves rather than handing DXC an include
// directory, for one reason: every file visited is appended to `out_files`,
// so hot reload watches the whole dependency set and an edit to a shared
// header rebuilds every pipeline that uses it. `#pragma once` is honoured.
//
// Returns false if any included file is missing.
bool preprocess_shader(const std::string& root, const std::string& relative_path,
                       std::unordered_set<std::string>& already_included,
                       std::vector<std::string>& out_files, std::string& out_source) {
    const std::string full_path = root + relative_path;
    if (already_included.count(relative_path)) return true;  // #pragma once
    already_included.insert(relative_path);

    std::string source = read_file(full_path);
    if (source.empty()) {
        LOG_ERROR("shader include not found or empty: '%s'", full_path.c_str());
        return false;
    }
    out_files.push_back(full_path);

    // `#line` markers around every inlined file, so a compile error names the
    // file and line it is really on rather than a line of the flattened whole.
    out_source += "#line 1 \"" + relative_path + "\"\n";

    // Line-by-line so we can rewrite includes and drop `#pragma once`.
    size_t line_start = 0;
    int line_number = 0;
    while (line_start <= source.size()) {
        size_t line_end = source.find('\n', line_start);
        if (line_end == std::string::npos) line_end = source.size();
        std::string line = source.substr(line_start, line_end - line_start);
        ++line_number;

        size_t first = line.find_first_not_of(" \t");
        bool handled = false;
        if (first != std::string::npos && line[first] == '#') {
            const std::string directive = line.substr(first);
            if (directive.rfind("#pragma once", 0) == 0) {
                handled = true;  // meaningless once inlined
            } else if (directive.rfind("#include \"", 0) == 0) {
                size_t open_quote = directive.find('"');
                size_t close_quote = directive.find('"', open_quote + 1);
                if (close_quote != std::string::npos) {
                    std::string included =
                        directive.substr(open_quote + 1, close_quote - open_quote - 1);
                    if (!preprocess_shader(root, included, already_included, out_files,
                                           out_source)) {
                        return false;
                    }
                    // Back in this file: the blanked directive is this line.
                    out_source += "#line " + std::to_string(line_number) + " \"" + relative_path + "\"\n";
                    handled = true;
                }
            }
        }

        // Blank out handled lines rather than removing them, so reported error
        // line numbers still line up with the file on disk.
        out_source += handled ? "" : line;
        out_source += '\n';

        if (line_end == source.size()) break;
        line_start = line_end + 1;
    }
    return true;
}

#ifdef DRAGON_SHADERCROSS
// Development builds compile the HLSL at runtime: DXC to SPIR-V, reflection
// for the resource counts SDL needs, then SPIR-V to whatever the device takes
// (MSL on Metal). The source is compiled once per stage, with VERTEX_STAGE or
// FRAGMENT_STAGE defined (shaders/common.hlsl says why).
SDL_GPUShader* compile_shader(SDL_GPUDevice* gpu, const std::string& source,
                              const std::string& /*stem*/, const char* entrypoint,
                              SDL_GPUShaderStage stage) {
    const bool vertex = stage == SDL_GPU_SHADERSTAGE_VERTEX;
    SDL_ShaderCross_HLSL_Define defines[2] = {};
    defines[0].name = const_cast<char*>(vertex ? "VERTEX_STAGE" : "FRAGMENT_STAGE");

    SDL_ShaderCross_HLSL_Info hlsl = {};
    hlsl.source = source.c_str();
    hlsl.entrypoint = entrypoint;
    hlsl.defines = defines;
    hlsl.shader_stage = vertex ? SDL_SHADERCROSS_SHADERSTAGE_VERTEX : SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT;

    size_t size = 0;
    void* spirv = SDL_ShaderCross_CompileSPIRVFromHLSL(&hlsl, &size);
    if (!spirv) return nullptr;  // DXC's diagnostics are in SDL_GetError()

    SDL_ShaderCross_GraphicsShaderMetadata* metadata =
        SDL_ShaderCross_ReflectGraphicsSPIRV(static_cast<const Uint8*>(spirv), size, 0);
    SDL_GPUShader* shader = nullptr;
    if (metadata) {
        SDL_ShaderCross_SPIRV_Info info = {};
        info.bytecode = static_cast<const Uint8*>(spirv);
        info.bytecode_size = size;
        info.entrypoint = entrypoint;
        info.shader_stage = hlsl.shader_stage;
        shader = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(gpu, &info, &metadata->resource_info, 0);
        SDL_free(metadata);
    }
    SDL_free(spirv);
    return shader;
}
#else
const char* stage_name(SDL_GPUShaderStage stage) {
    return stage == SDL_GPU_SHADERSTAGE_VERTEX ? "vertex" : "fragment";
}

// A package carries the shaders already translated (tools/release/
// bake_shaders.sh) as <stem>.<stage>.<ext> beside <stem>.<stage>.json, the
// reflection shadercross wrote, which holds the resource counts SDL needs.
// The format is whichever the device takes: MSL on Metal, DXIL on D3D12,
// SPIR-V on Vulkan, so a Windows package that carries both runs either.
struct BakedFormat {
    SDL_GPUShaderFormat format;
    const char* ext;
};

BakedFormat baked_format(SDL_GPUDevice* gpu) {
    const SDL_GPUShaderFormat accepted = SDL_GetGPUShaderFormats(gpu);
    if (accepted & SDL_GPU_SHADERFORMAT_MSL) return {SDL_GPU_SHADERFORMAT_MSL, ".msl"};
    if (accepted & SDL_GPU_SHADERFORMAT_DXIL) return {SDL_GPU_SHADERFORMAT_DXIL, ".dxil"};
    return {SDL_GPU_SHADERFORMAT_SPIRV, ".spv"};
}

uint32_t json_count(const std::string& json, const char* key) {
    const std::string needle = std::string("\"") + key + "\":";
    const size_t at = json.find(needle);
    return at == std::string::npos ? 0 : uint32_t(std::strtoul(json.c_str() + at + needle.size(), nullptr, 10));
}

SDL_GPUShader* compile_shader(SDL_GPUDevice* gpu, const std::string& root, const std::string& stem,
                              const char* entrypoint, SDL_GPUShaderStage stage) {
    const std::string base = root + stem + "." + stage_name(stage);
    const BakedFormat baked = baked_format(gpu);
    const std::string code = read_file(base + baked.ext);
    const std::string json = read_file(base + ".json");
    if (code.empty() || json.empty()) {
        SDL_SetError("no baked shader at %s%s/.json", base.c_str(), baked.ext);
        return nullptr;
    }
    SDL_GPUShaderCreateInfo info = {};
    info.code = reinterpret_cast<const Uint8*>(code.data());
    info.code_size = code.size();
    info.entrypoint = entrypoint;
    info.format = baked.format;
    info.stage = stage;
    info.num_samplers = json_count(json, "samplers");
    info.num_storage_textures = json_count(json, "storage_textures");
    info.num_storage_buffers = json_count(json, "storage_buffers");
    info.num_uniform_buffers = json_count(json, "uniform_buffers");
    return SDL_CreateGPUShader(gpu, &info);
}
#endif

}  // namespace

void PipelineCache::init(Device* device, std::string shader_root) {
    device_ = device;
#ifdef DRAGON_SHADERCROSS
    if (!SDL_ShaderCross_Init()) LOG_ERROR("SDL_ShaderCross_Init failed: %s", SDL_GetError());
#endif
    shader_root_ = std::move(shader_root);
    if (!shader_root_.empty() && shader_root_.back() != '/') shader_root_ += '/';
    LOG_INFO("shader root: %s", shader_root_.c_str());
}

void PipelineCache::shutdown() {
    if (!device_) return;
    for (Entry& e : entries_) {
        if (e.pipeline) SDL_ReleaseGPUGraphicsPipeline(device_->gpu(), e.pipeline);
        e.pipeline = nullptr;
    }
    entries_.clear();
#ifdef DRAGON_SHADERCROSS
    SDL_ShaderCross_Quit();
#endif
}

bool PipelineCache::build(Entry& entry) {
    SDL_GPUDevice* gpu = device_->gpu();
    const PipelineDesc& d = entry.desc;

#ifdef DRAGON_SHADERCROSS
    // Seed the watch list with the primary shader before doing anything else,
    // so a pipeline whose file is missing still has something to watch and gets
    // repaired when that file appears -- without re-reporting the error every
    // poll in the meantime.
    const std::string file = d.shader + ".hlsl";
    const std::string primary_path = shader_root_ + file;
    entry.sources.clear();
    entry.sources.push_back({primary_path, file_mtime(primary_path)});

    std::string source;
    std::vector<std::string> files;
    std::unordered_set<std::string> visited;
    if (!preprocess_shader(shader_root_, file, visited, files, source)) {
        LOG_ERROR("[%s] could not read shader '%s'", d.name.c_str(), file.c_str());
        return false;
    }

    // Replace with the full dependency set, so editing a shared header reloads
    // every pipeline that includes it.
    entry.sources.clear();
    for (const std::string& path : files) entry.sources.push_back({path, file_mtime(path)});
#else
    // Baked shaders are read as they are; watching them still lets a package
    // pick up a re-bake without a restart.
    const std::string& source = shader_root_;
    entry.sources.clear();
    for (const char* stage : {"vertex", "fragment"}) {
        const std::string path = shader_root_ + d.shader + "." + stage + baked_format(gpu).ext;
        entry.sources.push_back({path, file_mtime(path)});
    }
#endif

    SDL_GPUShader* vs = compile_shader(gpu, source, d.shader, "vs_main", SDL_GPU_SHADERSTAGE_VERTEX);
    if (!vs) {
        LOG_ERROR("[%s] vertex shader '%s' failed: %s", d.name.c_str(), d.shader.c_str(), SDL_GetError());
        return false;
    }
    SDL_GPUShader* fs = compile_shader(gpu, source, d.shader, "fs_main", SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (!fs) {
        LOG_ERROR("[%s] fragment shader '%s' failed: %s", d.name.c_str(), d.shader.c_str(), SDL_GetError());
        SDL_ReleaseGPUShader(gpu, vs);
        return false;
    }

    SDL_GPUColorTargetBlendState blend = {};
    if (d.additive_blend) {
        blend.enable_blend = true;
        blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
        blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    } else if (d.alpha_blend) {
        blend.enable_blend = true;
        blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
        blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    }

    SDL_GPUColorTargetDescription color_target = {};
    // World pipelines render into the HDR scene target; the post-process
    // composite and the UI are the only things that write the 8-bit one.
    color_target.format = d.color_format != SDL_GPU_TEXTUREFORMAT_INVALID
                              ? d.color_format
                              : device_->scene_hdr_format();
    color_target.blend_state = blend;

    SDL_GPUGraphicsPipelineCreateInfo info = {};
    info.vertex_shader = vs;
    info.fragment_shader = fs;
    info.primitive_type = d.primitive;

    info.vertex_input_state.vertex_buffer_descriptions =
        d.vertex_buffers.empty() ? nullptr : d.vertex_buffers.data();
    info.vertex_input_state.num_vertex_buffers = uint32_t(d.vertex_buffers.size());
    info.vertex_input_state.vertex_attributes =
        d.vertex_attributes.empty() ? nullptr : d.vertex_attributes.data();
    info.vertex_input_state.num_vertex_attributes = uint32_t(d.vertex_attributes.size());

    info.rasterizer_state.fill_mode = d.fill;
    info.rasterizer_state.cull_mode = d.cull;
    info.rasterizer_state.front_face = d.front_face;
    info.rasterizer_state.enable_depth_clip = true;

    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;

    // Reversed-Z: a fragment is visible when its depth is GREATER than what is
    // already there. Pairs with clear_depth = 0 in Device::begin_main_pass.
    info.depth_stencil_state.enable_depth_test = d.depth_test;
    info.depth_stencil_state.enable_depth_write = d.depth_write;
    info.depth_stencil_state.compare_op = d.depth_compare;

    info.target_info.color_target_descriptions = d.no_color_target ? nullptr : &color_target;
    info.target_info.num_color_targets = d.no_color_target ? 0 : 1;
    info.target_info.has_depth_stencil_target = !d.no_depth_target && (true);
    info.target_info.depth_stencil_format = d.depth_format != SDL_GPU_TEXTUREFORMAT_INVALID
                                                ? d.depth_format
                                                : device_->depth_format();

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(gpu, &info);

    // Shaders are reference-counted by the pipeline, so we drop ours either way.
    SDL_ReleaseGPUShader(gpu, vs);
    SDL_ReleaseGPUShader(gpu, fs);

    if (!pipeline) {
        LOG_ERROR("[%s] pipeline creation failed: %s", d.name.c_str(), SDL_GetError());
        return false;
    }

    // Only now do we drop the old one, so a failed rebuild is non-destructive.
    if (entry.pipeline) SDL_ReleaseGPUGraphicsPipeline(gpu, entry.pipeline);
    entry.pipeline = pipeline;
    return true;
}

PipelineHandle PipelineCache::create(PipelineDesc desc) {
    Entry entry;
    entry.desc = std::move(desc);
    bool ok = build(entry);
    entries_.push_back(std::move(entry));
    PipelineHandle handle = PipelineHandle(entries_.size() - 1);

    if (ok) {
        LOG_INFO("pipeline '%s' built", entries_[handle].desc.name.c_str());
    } else {
        LOG_ERROR("pipeline '%s' NOT built -- fix the shader and save to retry",
                  entries_[handle].desc.name.c_str());
    }
    // Handle is returned even on failure so a later save can repair it.
    return handle;
}

SDL_GPUGraphicsPipeline* PipelineCache::get(PipelineHandle handle) const {
    if (handle >= entries_.size()) return nullptr;
    return entries_[handle].pipeline;
}

int PipelineCache::poll_hot_reload() {
    int reloaded = 0;
    for (Entry& e : entries_) {
        bool changed = false;
        for (SourceFile& source : e.sources) {
            int64_t mtime = file_mtime(source.path);
            if (mtime != 0 && mtime != source.mtime) {
                source.mtime = mtime;
                changed = true;
            }
        }
        if (!changed) continue;

        if (build(e)) {
            LOG_INFO("reloaded '%s'", e.desc.name.c_str());
            ++reloaded;
        }
    }
    return reloaded;
}

int PipelineCache::broken_count() const {
    int n = 0;
    for (const Entry& e : entries_) {
        if (!e.pipeline) ++n;
    }
    return n;
}

}  // namespace gfx
