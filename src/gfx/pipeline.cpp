#include "gfx/pipeline.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>

#include <unordered_set>

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

// Metal's runtime compiler resolves <system> headers but not local ones, since
// a source string has no directory to resolve against. So we inline
// `#include "file"` ourselves, which is what lets shaders share a common
// header. `#pragma once` is honoured, and every file visited is appended to
// `out_files` so hot reload can watch the whole dependency set.
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

    // Line-by-line so we can rewrite includes and drop `#pragma once`.
    size_t line_start = 0;
    while (line_start <= source.size()) {
        size_t line_end = source.find('\n', line_start);
        if (line_end == std::string::npos) line_end = source.size();
        std::string line = source.substr(line_start, line_end - line_start);

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

SDL_GPUShader* compile_shader(SDL_GPUDevice* gpu, const std::string& source,
                              const char* entrypoint, SDL_GPUShaderStage stage,
                              const PipelineDesc& desc) {
    SDL_GPUShaderCreateInfo info = {};
    info.code = reinterpret_cast<const Uint8*>(source.data());
    info.code_size = source.size();
    info.entrypoint = entrypoint;
    info.format = SDL_GPU_SHADERFORMAT_MSL;
    info.stage = stage;

    bool vertex = (stage == SDL_GPU_SHADERSTAGE_VERTEX);
    info.num_uniform_buffers = vertex ? desc.vs_uniform_buffers : desc.fs_uniform_buffers;
    info.num_samplers = vertex ? desc.vs_samplers : desc.fs_samplers;
    info.num_storage_buffers = vertex ? desc.vs_storage_buffers : desc.fs_storage_buffers;
    info.num_storage_textures = 0;

    return SDL_CreateGPUShader(gpu, &info);
}

}  // namespace

void PipelineCache::init(Device* device, std::string shader_root) {
    device_ = device;
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
}

bool PipelineCache::build(Entry& entry) {
    SDL_GPUDevice* gpu = device_->gpu();
    const PipelineDesc& d = entry.desc;

    // Seed the watch list with the primary shader before doing anything else,
    // so a pipeline whose file is missing still has something to watch and gets
    // repaired when that file appears -- without re-reporting the error every
    // poll in the meantime.
    const std::string primary_path = shader_root_ + d.shader_path;
    entry.sources.clear();
    entry.sources.push_back({primary_path, file_mtime(primary_path)});

    std::string source;
    std::vector<std::string> files;
    std::unordered_set<std::string> visited;
    if (!preprocess_shader(shader_root_, d.shader_path, visited, files, source)) {
        LOG_ERROR("[%s] could not read shader '%s'", d.name.c_str(), d.shader_path.c_str());
        return false;
    }

    // Replace with the full dependency set, so editing a shared header reloads
    // every pipeline that includes it.
    entry.sources.clear();
    for (const std::string& path : files) entry.sources.push_back({path, file_mtime(path)});

    SDL_GPUShader* vs = compile_shader(gpu, source, d.vs_entry.c_str(),
                                       SDL_GPU_SHADERSTAGE_VERTEX, d);
    if (!vs) {
        LOG_ERROR("[%s] vertex shader '%s' failed: %s", d.name.c_str(), d.vs_entry.c_str(),
                  SDL_GetError());
        return false;
    }
    SDL_GPUShader* fs = compile_shader(gpu, source, d.fs_entry.c_str(),
                                       SDL_GPU_SHADERSTAGE_FRAGMENT, d);
    if (!fs) {
        LOG_ERROR("[%s] fragment shader '%s' failed: %s", d.name.c_str(), d.fs_entry.c_str(),
                  SDL_GetError());
        SDL_ReleaseGPUShader(gpu, vs);
        return false;
    }

    SDL_GPUColorTargetBlendState blend = {};
    if (d.alpha_blend) {
        blend.enable_blend = true;
        blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
        blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    }

    SDL_GPUColorTargetDescription color_target = {};
    color_target.format = d.color_format != SDL_GPU_TEXTUREFORMAT_INVALID
                              ? d.color_format
                              : device_->scene_color_format();
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
    info.target_info.has_depth_stencil_target = true;
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
