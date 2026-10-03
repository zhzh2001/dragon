#include "gfx/pipeline.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>

#include <unordered_set>

#include "core/log.h"
#include "gfx/render_tier.h"

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
        device_->rhi().destroy(e.pipeline);
        e.pipeline = nullptr;
    }
    entries_.clear();
}

bool PipelineCache::build(Entry& entry) {
    rhi::Device& rhi = device_->rhi();
    const PipelineDesc& d = entry.desc;

    rhi::ShaderSource shaders;
    shaders.stem = d.shader;
    shaders.root = shader_root_;
    if (!active_tier().hdr) shaders.defines.push_back("LDR_OUTPUT");
    if (active_tier().baked_noise) shaders.defines.push_back("BAKED_NOISE");
    if (active_tier().compress_textures) shaders.defines.push_back("SWIZZLED_NORMALS");
    if (active_tier().packed_joints) shaders.defines.push_back("PACKED_JOINTS");
    if (active_tier().vertex_lighting) shaders.defines.push_back("SM2");
    if (!active_tier().depth.reversed) shaders.defines.push_back("CONVENTIONAL_DEPTH");
    entry.sources.clear();
    if (rhi.compiles_hlsl()) {
        // Seed the watch list with the primary shader before doing anything
        // else, so a pipeline whose file is missing still has something to
        // watch and gets repaired when that file appears -- without
        // re-reporting the error every poll in the meantime.
        const std::string file = d.shader + ".hlsl";
        const std::string primary_path = shader_root_ + file;
        entry.sources.push_back({primary_path, file_mtime(primary_path)});

        std::vector<std::string> files;
        std::unordered_set<std::string> visited;
        if (!preprocess_shader(shader_root_, file, visited, files, shaders.hlsl)) {
            LOG_ERROR("[%s] could not read shader '%s'", d.name.c_str(), file.c_str());
            return false;
        }
        // Replace with the full dependency set, so editing a shared header
        // reloads every pipeline that includes it.
        entry.sources.clear();
        for (const std::string& path : files) entry.sources.push_back({path, file_mtime(path)});
    } else {
        // Baked shaders are read as they are; watching them still lets a
        // package pick up a re-bake without a restart.
        for (const std::string& path : rhi.baked_shader_files(shaders))
            entry.sources.push_back({path, file_mtime(path)});
    }

    rhi::PipelineDesc desc;
    desc.name = d.name;
    desc.vertex_buffers = d.vertex_buffers;
    desc.vertex_attributes = d.vertex_attributes;
    desc.primitive = d.primitive;
    desc.cull = d.cull;
    desc.fill = d.fill;
    desc.front_face = d.front_face;
    desc.depth_test = d.depth_test;
    desc.depth_write = d.depth_write;
    desc.depth_compare = d.depth_compare;
    // A main-pass pipeline (one on the device's own depth buffer) states its
    // compare for reversed-Z; a retro tier's conventional depth flips it. The
    // shadow pipelines name their own depth format and are conventional
    // already, so they are left alone.
    if (!depth_convention().reversed && d.depth_format == rhi::Format::Invalid) {
        if (desc.depth_compare == rhi::Compare::Greater) desc.depth_compare = rhi::Compare::Less;
        else if (desc.depth_compare == rhi::Compare::GreaterEqual) desc.depth_compare = rhi::Compare::LessEqual;
    }
    desc.blend = d.additive_blend ? rhi::Blend::Additive
                 : d.alpha_blend  ? rhi::Blend::Alpha
                                  : rhi::Blend::Opaque;
    // World pipelines render into the main target: the HDR scene, or the
    // 8-bit one for a tier without HDR. The composite and the UI name theirs.
    desc.color_format = d.no_color_target ? rhi::Format::Invalid
                        : d.color_format != rhi::Format::Invalid ? d.color_format
                                                                 : device_->main_color_format();
    desc.depth_format = d.no_depth_target ? rhi::Format::Invalid
                        : d.depth_format != rhi::Format::Invalid ? d.depth_format
                                                                 : device_->depth_format();

    rhi::Pipeline* pipeline = rhi.create_pipeline(desc, shaders);
    if (!pipeline) return false;
    // Only now is the old one dropped, so a failed rebuild is non-destructive.
    rhi.destroy(entry.pipeline);
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

rhi::Pipeline* PipelineCache::get(PipelineHandle handle) const {
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
