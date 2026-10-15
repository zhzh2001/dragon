// The GLES 2.0 backend (rhi/gles/gles_backend.h; docs/porting/MOBILE.md,
// M4). OpenGL ES 2.0 plus what Mesa's vc4 offers on the Pi 1B -- 32-bit
// indices (OES_element_index_uint) and non-power-of-two textures
// (OES_texture_npot) -- and nothing ANGLE has that vc4 lacks: no
// instancing, no sRGB, no float targets, no sampler objects, no VAOs.
//
// What GLES 2.0 does not have, done here:
//   - uniform blocks: the bake flattened each into a vec4 array, and a
//     pushed block is copied into it before a draw that needs it;
//   - instancing: one draw per instance, the instance's attributes set as
//     constants from a CPU copy of the buffer (rhi/d3d9's Loop mode, with
//     glVertexAttrib in place of a stride-0 stream);
//   - base vertex: the attribute pointers are offset instead;
//   - sampler objects: a texture carries the last sampler state applied to
//     it, and binding it with another sampler rewrites its parameters;
//   - sRGB textures: converted to linear 8-bit at upload, as vc4 has no
//     EXT_sRGB (the darkest values lose steps; the retro tiers' textures
//     are 512 and below, and their tonemap is in the shader anyway);
//   - a blit: a textured quad, which also presents.
//
// The vertex stage's y is flipped by the bake (--flip-vert-y): a pass's top
// row lands in GL's row 0, which is where D3D keeps it, so a rendered target
// samples, places viewports and reads back exactly as on the other backends.
// The flip reverses winding, so the front face is the other one. Only the
// present, into the window's GL-oriented framebuffer, flips back.

#include "rhi/gles/gles_backend.h"

#include <SDL3/SDL.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/log.h"

namespace rhi {
namespace gles {
namespace {

// ---- handles ----------------------------------------------------------------

struct GlBuffer {
    GLuint id = 0;
    BufferUsage usage = BufferUsage::Vertex;
    uint32_t size = 0;
    // A vertex buffer's contents on the CPU too: an instanced draw reads its
    // per-instance attributes from here (no divisors in GLES 2.0).
    std::vector<uint8_t> shadow;
    std::vector<uint8_t> staging;  // map_upload's
};

struct GlTexture {
    GLuint id = 0;            // a texture
    GLuint renderbuffer = 0;  // or a depth renderbuffer
    TextureDesc desc;
    bool srgb = false;
    uint32_t levels_uploaded = 0;  // a bitmask of mip levels filled
    bool mips_generated = false;
    // The sampler state last written into the texture's parameters.
    bool params_set = false;
    SamplerDesc params;
};

struct GlSampler {
    SamplerDesc desc;
};

struct UniformArray {
    int slot = 0;
    GLint location = -1;
    int vec4s = 0;
    uint64_t uploaded = 0;  // the generation of the slot's data last copied in
};

struct GlPipeline {
    GLuint program = 0;
    PipelineDesc desc;
    std::vector<UniformArray> uniforms[2];  // vertex, fragment
    uint32_t attributes = 0;                // bit n: a<n> is read
    bool vertex_id = false;                 // a0 with no attribute declared: the vertex index
    bool instanced = false;                 // reads a per-instance buffer
    std::string name;
};

struct GlPass {
    uint32_t width = 0, height = 0;
    GLuint framebuffer = 0;
    bool discard_depth = false;
};

GlBuffer* gl(Buffer* b) { return reinterpret_cast<GlBuffer*>(b); }
GlTexture* gl(Texture* t) { return reinterpret_cast<GlTexture*>(t); }
GlSampler* gl(Sampler* s) { return reinterpret_cast<GlSampler*>(s); }
GlPipeline* gl(Pipeline* p) { return reinterpret_cast<GlPipeline*>(p); }

// ---- small helpers ----------------------------------------------------------

bool has_extension(const char* name) {
    const char* all = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (!all) return false;
    const size_t n = std::strlen(name);
    for (const char* p = std::strstr(all, name); p; p = std::strstr(p + 1, name)) {
        if ((p == all || p[-1] == ' ') && (p[n] == ' ' || p[n] == '\0')) return true;
    }
    return false;
}

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

GLenum gl_compare(Compare c) {
    switch (c) {
        case Compare::Never: return GL_NEVER;
        case Compare::Less: return GL_LESS;
        case Compare::Equal: return GL_EQUAL;
        case Compare::LessEqual: return GL_LEQUAL;
        case Compare::Greater: return GL_GREATER;
        case Compare::NotEqual: return GL_NOTEQUAL;
        case Compare::GreaterEqual: return GL_GEQUAL;
        case Compare::Always: return GL_ALWAYS;
    }
    return GL_ALWAYS;
}

GLenum gl_primitive(Primitive p) {
    switch (p) {
        case Primitive::TriangleList: return GL_TRIANGLES;
        case Primitive::TriangleStrip: return GL_TRIANGLE_STRIP;
        case Primitive::LineList: return GL_LINES;
        case Primitive::LineStrip: return GL_LINE_STRIP;
        case Primitive::PointList: return GL_POINTS;
    }
    return GL_TRIANGLES;
}

GLenum gl_wrap(Address a) {
    switch (a) {
        case Address::Repeat: return GL_REPEAT;
        case Address::Clamp: return GL_CLAMP_TO_EDGE;
        case Address::Mirror: return GL_MIRRORED_REPEAT;
    }
    return GL_REPEAT;
}

struct AttribFormat {
    GLint components;
    GLenum type;
    GLboolean normalized;
    uint32_t bytes;
};
AttribFormat attrib_format(VertexFormat f) {
    switch (f) {
        case VertexFormat::Float: return {1, GL_FLOAT, GL_FALSE, 4};
        case VertexFormat::Float2: return {2, GL_FLOAT, GL_FALSE, 8};
        case VertexFormat::Float3: return {3, GL_FLOAT, GL_FALSE, 12};
        case VertexFormat::Float4: return {4, GL_FLOAT, GL_FALSE, 16};
        // Joint indices: floats 0..255, as under D3D9 (shaders/skin_common.hlsl).
        case VertexFormat::UByte4: return {4, GL_UNSIGNED_BYTE, GL_FALSE, 4};
        case VertexFormat::UByte4Norm: return {4, GL_UNSIGNED_BYTE, GL_TRUE, 4};
    }
    return {4, GL_FLOAT, GL_FALSE, 16};
}

// sRGB to linear, for an 8-bit texel (see the top of the file).
const uint8_t* srgb_to_linear_table() {
    static uint8_t table[256];
    static bool built = false;
    if (!built) {
        for (int i = 0; i < 256; ++i) {
            const float c = float(i) / 255.0f;
            const float l = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
            table[i] = uint8_t(std::lround(l * 255.0f));
        }
        built = true;
    }
    return table;
}

GLuint compile(GLenum stage, const std::string& source, const std::string& label) {
    const GLuint shader = glCreateShader(stage);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096] = {};
        glGetShaderInfoLog(shader, sizeof log, nullptr, log);
        LOG_ERROR("[%s] %s shader: %s", label.c_str(), stage == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// The bake's sidecar (tools/gles/bake_gles.py): per stage, uniform arrays
// [slot, name, vec4s], samplers [slot, name] and attributes [location, name].
// Parsed by hand: the file is ours and flat, and a JSON library is not worth
// a dependency for it.
struct StageMeta {
    struct Uniform {
        int slot;
        std::string name;
        int vec4s;
    };
    std::vector<Uniform> uniforms;
    std::vector<std::pair<int, std::string>> samplers;
    std::vector<std::pair<int, std::string>> attributes;
};

// The array after "key": in `object`, as its top-level [ ... ] entries.
std::vector<std::string> json_entries(const std::string& object, const char* key) {
    std::vector<std::string> out;
    const std::string k = std::string("\"") + key + "\"";
    size_t at = object.find(k);
    if (at == std::string::npos) return out;
    at = object.find('[', at);
    if (at == std::string::npos) return out;
    int depth = 0;
    size_t start = 0;
    for (size_t i = at; i < object.size(); ++i) {
        const char c = object[i];
        if (c == '[') {
            if (++depth == 2) start = i + 1;
        } else if (c == ']') {
            if (depth == 2) out.push_back(object.substr(start, i - start));
            if (--depth == 0) break;
        }
    }
    return out;
}

// "0, \"name\", 57" -> its fields.
std::vector<std::string> json_fields(const std::string& entry) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (char c : entry) {
        if (c == '"') {
            quoted = !quoted;
            continue;
        }
        if (c == ',' && !quoted) {
            out.push_back(cur);
            cur.clear();
            continue;
        }
        if (!quoted && (c == ' ' || c == '\n')) continue;
        cur += c;
    }
    out.push_back(cur);
    return out;
}

StageMeta parse_stage(const std::string& json, const char* stage) {
    StageMeta meta;
    const std::string key = std::string("\"") + stage + "\"";
    size_t at = json.find(key);
    if (at == std::string::npos) return meta;
    // The stage's object runs to its matching brace.
    at = json.find('{', at);
    int depth = 0;
    size_t end = at;
    for (; end < json.size(); ++end) {
        if (json[end] == '{') ++depth;
        if (json[end] == '}' && --depth == 0) break;
    }
    const std::string object = json.substr(at, end - at + 1);
    for (const std::string& e : json_entries(object, "uniforms")) {
        const auto f = json_fields(e);
        if (f.size() == 3) meta.uniforms.push_back({std::atoi(f[0].c_str()), f[1], std::atoi(f[2].c_str())});
    }
    for (const std::string& e : json_entries(object, "samplers")) {
        const auto f = json_fields(e);
        if (f.size() == 2) meta.samplers.push_back({std::atoi(f[0].c_str()), f[1]});
    }
    for (const std::string& e : json_entries(object, "attributes")) {
        const auto f = json_fields(e);
        if (f.size() == 2) meta.attributes.push_back({std::atoi(f[0].c_str()), f[1]});
    }
    return meta;
}

// The blit and the present: a textured quad from a triangle strip of four,
// with `flip` turning the texture over for the window's framebuffer.
const char* kBlitVertex =
    "#version 100\n"
    "attribute vec2 a0;\n"
    "uniform vec4 u_rect;\n"  // xy: uv scale, z: flip
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  vec2 uv = a0 * 0.5 + 0.5;\n"
    "  v_uv = vec2(uv.x, u_rect.z > 0.5 ? 1.0 - uv.y : uv.y) * u_rect.xy;\n"
    "  gl_Position = vec4(a0, 0.0, 1.0);\n"
    "}\n";
const char* kBlitFragment =
    "#version 100\n"
    "precision mediump float;\n"
    "uniform sampler2D u_texture;\n"
    "varying vec2 v_uv;\n"
    "void main() { gl_FragColor = texture2D(u_texture, v_uv); }\n";

constexpr int kMaxSlots = 8;
constexpr int kMaxTextures = 16;
constexpr int kMaxAttributes = 16;

// ---- the device --------------------------------------------------------------

class GlesDevice final : public Device {
public:
    bool init(SDL_Window* window, const DeviceConfig& config) {
        window_ = window;
        headless_ = config.headless;
        context_ = SDL_GL_CreateContext(window);
        if (!context_) {
            LOG_ERROR("gles: SDL_GL_CreateContext failed: %s", SDL_GetError());
            return false;
        }
        SDL_GL_MakeCurrent(window, context_);
        SDL_GL_SetSwapInterval(config.vsync ? 1 : 0);
        LOG_INFO("gles: %s | %s | %s", glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
        index32_ = has_extension("GL_OES_element_index_uint");
        depth24_ = has_extension("GL_OES_depth24");
        discard_ = has_extension("GL_EXT_discard_framebuffer");
        if (discard_) {
            discard_framebuffer_ = reinterpret_cast<PFNGLDISCARDFRAMEBUFFEREXTPROC>(
                SDL_GL_GetProcAddress("glDiscardFramebufferEXT"));
            discard_ = discard_framebuffer_ != nullptr;
        }
        GLint v = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v);
        max_texture_ = uint32_t(v);
        glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &v);
        max_attributes_ = std::min(int(v), kMaxAttributes);
        if (!index32_) LOG_WARN("gles: no OES_element_index_uint; 32-bit index buffers will not draw");

        // The vertex index for a pipeline without attributes (a fullscreen
        // triangle; shaders/common.hlsl VERTEX_ID_INPUT under D3D9).
        const float ids[6] = {0, 1, 2, 3, 4, 5};
        glGenBuffers(1, &vertex_ids_);
        glBindBuffer(GL_ARRAY_BUFFER, vertex_ids_);
        glBufferData(GL_ARRAY_BUFFER, sizeof ids, ids, GL_STATIC_DRAW);
        const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
        glGenBuffers(1, &quad_);
        glBindBuffer(GL_ARRAY_BUFFER, quad_);
        glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
        const GLuint vs = compile(GL_VERTEX_SHADER, kBlitVertex, "blit");
        const GLuint fs = compile(GL_FRAGMENT_SHADER, kBlitFragment, "blit");
        if (!vs || !fs) return false;
        blit_program_ = glCreateProgram();
        glAttachShader(blit_program_, vs);
        glAttachShader(blit_program_, fs);
        glBindAttribLocation(blit_program_, 0, "a0");
        glLinkProgram(blit_program_);
        glDeleteShader(vs);
        glDeleteShader(fs);
        blit_rect_ = glGetUniformLocation(blit_program_, "u_rect");
        glUseProgram(blit_program_);
        glUniform1i(glGetUniformLocation(blit_program_, "u_texture"), 0);
        forget();
        return true;
    }

    ~GlesDevice() override {
        if (context_) {
            for (auto& [key, fbo] : framebuffers_) glDeleteFramebuffers(1, &fbo);
            glDeleteBuffers(1, &vertex_ids_);
            glDeleteBuffers(1, &quad_);
            glDeleteProgram(blit_program_);
            SDL_GL_DestroyContext(context_);
        }
    }

    // Everything cached about the GL state is unknown again (after ImGui).
    void forget() {
        program_ = 0;
        bound_pipeline_ = nullptr;
        array_buffer_ = 0;
        enabled_attributes_ = 0;
        for (auto& t : bound_textures_) t = 0;
        active_unit_ = -1;
        state_known_ = false;
        attributes_valid_ = false;
    }

    Backend backend() const override { return Backend::OpenGLES2; }
    const char* driver_name() const override { return "gles"; }
    uint32_t max_texture_size() const override { return max_texture_; }

    bool supports_format(Format format, uint8_t usage) const override {
        switch (format) {
            case Format::RGBA8:
            case Format::RGBA8_SRGB:
                return true;
            case Format::D16:
            case Format::D24:
                // Depth to draw in, never to sample: the shaders' sampled
                // depth is D3D9's float colour target, which vc4 cannot
                // render (no float targets), so shadows are off on GLES.
                return !(usage & TEXTURE_SAMPLED) && (format == Format::D16 || depth24_);
            default:
                return false;  // D32F, RGBA16F, the BC formats, RGB565
        }
    }

    // ---- the frame
    FrameStatus begin_frame(uint32_t* present_width, uint32_t* present_height) override {
        if (!headless_) {
            int w = 0, h = 0;
            SDL_GetWindowSizeInPixels(window_, &w, &h);
            if (w <= 0 || h <= 0) return FrameStatus::Skip;
            if (present_width) *present_width = uint32_t(w);
            if (present_height) *present_height = uint32_t(h);
            window_w_ = w;
            window_h_ = h;
        }
        return FrameStatus::Ready;
    }

    void end_frame(Texture* source, uint32_t width, uint32_t height, std::vector<uint8_t>* readback) override {
        if (readback && source) download(source, width, height, *readback);
        if (!headless_ && source) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            draw_quad(gl(source), width, height, window_w_, window_h_, true);
            SDL_GL_SwapWindow(window_);
        } else {
            glFlush();
        }
    }

    void wait_idle() override { glFinish(); }

    void blit(Texture* source, uint32_t sw, uint32_t sh, Texture* dest, uint32_t dw, uint32_t dh) override {
        if (!source || !dest) return;
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer(gl(dest), nullptr));
        draw_quad(gl(source), sw, sh, dw, dh, false);
    }

    // ---- resources
    Buffer* create_buffer(BufferUsage usage, uint32_t size, const void* data, const char*) override {
        auto* b = new GlBuffer;
        b->usage = usage;
        b->size = size;
        glGenBuffers(1, &b->id);
        const GLenum target = usage == BufferUsage::Index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
        bind_buffer(target, b->id);
        glBufferData(target, size, data, data ? GL_STATIC_DRAW : GL_STREAM_DRAW);
        if (usage == BufferUsage::Vertex) {
            b->shadow.assign(size, 0);
            if (data) std::memcpy(b->shadow.data(), data, size);
        }
        track(b, Kind::Buffer, size);
        return reinterpret_cast<Buffer*>(b);
    }

    void destroy(Buffer* buffer) override {
        if (!buffer) return;
        GlBuffer* b = gl(buffer);
        if (array_buffer_ == b->id) array_buffer_ = 0;
        attributes_valid_ = false;  // a new buffer may take its address
        glDeleteBuffers(1, &b->id);
        untrack(b);
        delete b;
    }

    void* map_upload(Buffer* buffer, uint32_t size) override {
        GlBuffer* b = gl(buffer);
        if (b->staging.size() < size) b->staging.resize(size);
        return b->staging.data();
    }

    void commit_upload(Buffer* buffer, uint32_t size) override {
        GlBuffer* b = gl(buffer);
        size = std::min(size, b->size);
        const GLenum target = b->usage == BufferUsage::Index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
        bind_buffer(target, b->id);
        // Orphan, then fill: the previous contents may still be in flight.
        glBufferData(target, b->size, nullptr, GL_STREAM_DRAW);
        glBufferSubData(target, 0, size, b->staging.data());
        if (b->usage == BufferUsage::Vertex) std::memcpy(b->shadow.data(), b->staging.data(), size);
        attributes_valid_ = false;  // an instance's values may have changed with it
    }

    Texture* create_texture(const TextureDesc& desc, const char* debug_name) override {
        auto* t = new GlTexture;
        t->desc = desc;
        t->srgb = desc.format == Format::RGBA8_SRGB;
        const bool depth = desc.format == Format::D16 || desc.format == Format::D24 || desc.format == Format::D32F;
        if (depth) {
            glGenRenderbuffers(1, &t->renderbuffer);
            glBindRenderbuffer(GL_RENDERBUFFER, t->renderbuffer);
            const GLenum internal = desc.format != Format::D16 && depth24_ ? GL_DEPTH_COMPONENT24_OES : GL_DEPTH_COMPONENT16;
            glRenderbufferStorage(GL_RENDERBUFFER, internal, GLsizei(desc.width), GLsizei(desc.height));
        } else if (desc.format == Format::RGBA8 || desc.format == Format::RGBA8_SRGB) {
            glGenTextures(1, &t->id);
            bind_texture(0, t->id);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GLsizei(desc.width), GLsizei(desc.height), 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, nullptr);
            if (desc.usage & TEXTURE_COLOR_TARGET) t->levels_uploaded = 1;  // drawn, not uploaded
        } else {
            LOG_ERROR("gles: texture '%s': format %d is not supported", debug_name ? debug_name : "", int(desc.format));
            delete t;
            return nullptr;
        }
        track(t, Kind::Texture, texture_bytes(desc));
        return reinterpret_cast<Texture*>(t);
    }

    bool upload_texture(Texture* texture, const void* pixels, uint32_t width, uint32_t height,
                        bool generate_mips) override {
        GlTexture* t = gl(texture);
        if (!t || !t->id) return false;
        upload_level(t, 0, pixels, width, height);
        if (generate_mips && t->desc.mip_levels > 1) {
            glGenerateMipmap(GL_TEXTURE_2D);
            t->mips_generated = true;
        }
        return true;
    }

    bool upload_texture_level(Texture* texture, uint32_t level, const void* data, uint32_t,
                              uint32_t width, uint32_t height) override {
        GlTexture* t = gl(texture);
        if (!t || !t->id) return false;
        upload_level(t, level, data, width, height);
        return true;
    }

    void destroy(Texture* texture) override {
        if (!texture) return;
        GlTexture* t = gl(texture);
        for (auto it = framebuffers_.begin(); it != framebuffers_.end();) {
            if (it->first.first == t || it->first.second == t) {
                glDeleteFramebuffers(1, &it->second);
                it = framebuffers_.erase(it);
            } else {
                ++it;
            }
        }
        for (auto& b : bound_textures_) {
            if (b == t->id) b = 0;
        }
        if (t->id) glDeleteTextures(1, &t->id);
        if (t->renderbuffer) glDeleteRenderbuffers(1, &t->renderbuffer);
        untrack(t);
        delete t;
    }

    Sampler* create_sampler(const SamplerDesc& desc) override {
        auto* s = new GlSampler;
        s->desc = desc;
        return reinterpret_cast<Sampler*>(s);
    }
    void destroy(Sampler* sampler) override { delete gl(sampler); }

    // ---- pipelines
    bool compiles_hlsl() const override { return false; }

    std::vector<std::string> baked_shader_files(const ShaderSource& shaders) const override {
        const std::string base = baked_base(shaders);
        return {base + ".vertex.glsl", base + ".fragment.glsl", base + ".gles.json"};
    }

    Pipeline* create_pipeline(const PipelineDesc& d, const ShaderSource& shaders) override {
        const std::string base = baked_base(shaders);
        const std::string vs_src = read_file(base + ".vertex.glsl");
        const std::string fs_src = read_file(base + ".fragment.glsl");
        const std::string json = read_file(base + ".gles.json");
        if (vs_src.empty() || fs_src.empty() || json.empty()) {
            LOG_ERROR("[%s] no baked GLSL at %s.* (tools/gles/bake_gles.py)", d.name.c_str(), base.c_str());
            return nullptr;
        }
        const StageMeta vmeta = parse_stage(json, "vertex"), fmeta = parse_stage(json, "fragment");
        const GLuint vs = compile(GL_VERTEX_SHADER, vs_src, d.name);
        const GLuint fs = vs ? compile(GL_FRAGMENT_SHADER, fs_src, d.name) : 0;
        if (!vs || !fs) {
            if (vs) glDeleteShader(vs);
            return nullptr;
        }
        auto* p = new GlPipeline;
        p->desc = d;
        p->name = d.name;
        p->program = glCreateProgram();
        glAttachShader(p->program, vs);
        glAttachShader(p->program, fs);
        for (const auto& [location, name] : vmeta.attributes) {
            if (location >= max_attributes_) {
                LOG_ERROR("[%s] attribute %d past the GPU's %d", d.name.c_str(), location, max_attributes_);
                continue;
            }
            glBindAttribLocation(p->program, GLuint(location), name.c_str());
            p->attributes |= 1u << location;
        }
        glLinkProgram(p->program);
        glDeleteShader(vs);
        glDeleteShader(fs);
        GLint ok = 0;
        glGetProgramiv(p->program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[4096] = {};
            glGetProgramInfoLog(p->program, sizeof log, nullptr, log);
            LOG_ERROR("[%s] link: %s", d.name.c_str(), log);
            glDeleteProgram(p->program);
            delete p;
            return nullptr;
        }
        p->vertex_id = d.vertex_attributes.empty() && (p->attributes & 1u);
        for (const VertexBufferLayout& l : d.vertex_buffers) p->instanced |= l.rate == InputRate::Instance;
        glUseProgram(p->program);
        program_ = p->program;
        for (int stage = 0; stage < 2; ++stage) {
            const StageMeta& meta = stage == 0 ? vmeta : fmeta;
            for (const auto& u : meta.uniforms) {
                UniformArray a;
                a.slot = u.slot;
                a.vec4s = u.vec4s;
                // An array's location is its first element's.
                a.location = glGetUniformLocation(p->program, u.name.c_str());
                if (a.location < 0) a.location = glGetUniformLocation(p->program, (u.name + "[0]").c_str());
                if (a.location >= 0 && a.slot >= 0 && a.slot < kMaxSlots) p->uniforms[stage].push_back(a);
            }
            for (const auto& [slot, name] : meta.samplers) {
                const GLint loc = glGetUniformLocation(p->program, name.c_str());
                if (loc >= 0) glUniform1i(loc, slot);
            }
        }
        track(p, Kind::Pipeline, 0);
        return reinterpret_cast<Pipeline*>(p);
    }

    void destroy(Pipeline* pipeline) override {
        if (!pipeline) return;
        GlPipeline* p = gl(pipeline);
        if (program_ == p->program) program_ = 0;
        if (bound_pipeline_ == p) bound_pipeline_ = nullptr;
        attributes_valid_ = false;
        glDeleteProgram(p->program);
        untrack(p);
        delete p;
    }

    // ---- passes
    Pass* begin_pass(const PassDesc& desc) override {
        GlTexture* color = gl(desc.color);
        GlTexture* depth = gl(desc.depth);
        pass_ = GlPass{};
        pass_.framebuffer = framebuffer(color, depth);
        pass_.width = color ? color->desc.width : depth ? depth->desc.width : 0;
        pass_.height = color ? color->desc.height : depth ? depth->desc.height : 0;
        pass_.discard_depth = depth && !desc.keep_depth && discard_;
        glBindFramebuffer(GL_FRAMEBUFFER, pass_.framebuffer);
        glViewport(0, 0, GLsizei(pass_.width), GLsizei(pass_.height));
        GLbitfield clear = 0;
        if (color && desc.clear_color) {
            glClearColor(desc.clear_rgba[0], desc.clear_rgba[1], desc.clear_rgba[2], desc.clear_rgba[3]);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            clear |= GL_COLOR_BUFFER_BIT;
        }
        if (depth && desc.clear_depth) {
            glClearDepthf(desc.clear_depth_value);
            glDepthMask(GL_TRUE);
            clear |= GL_DEPTH_BUFFER_BIT;
        }
        if (clear) {
            glDisable(GL_SCISSOR_TEST);
            glClear(clear);
            state_known_ = false;  // the masks changed under the cache
        }
        bound_pipeline_ = nullptr;
        return reinterpret_cast<Pass*>(&pass_);
    }

    void end_pass(Pass*) override {
        // A tiler need not write back a depth buffer nobody reads.
        if (pass_.discard_depth && pass_.framebuffer) {
            const GLenum attachment = GL_DEPTH_ATTACHMENT;
            discard_framebuffer_(GL_FRAMEBUFFER, 1, &attachment);
        }
    }

    void bind_pipeline(Pass*, Pipeline* pipeline) override {
        GlPipeline* p = gl(pipeline);
        if (!p) return;
        bound_pipeline_ = p;
        if (program_ != p->program) {
            glUseProgram(p->program);
            program_ = p->program;
        }
        const PipelineDesc& d = p->desc;
        set_enabled(GL_DEPTH_TEST, d.depth_format != Format::Invalid && d.depth_test, depth_test_);
        glDepthMask(d.depth_write ? GL_TRUE : GL_FALSE);
        if (d.depth_test) glDepthFunc(gl_compare(d.depth_compare));
        set_enabled(GL_CULL_FACE, d.cull != Cull::None, cull_);
        if (d.cull != Cull::None) {
            glCullFace(d.cull == Cull::Front ? GL_FRONT : GL_BACK);
            // The bake flipped y, which turns every triangle over.
            glFrontFace(d.front_face == FrontFace::CounterClockwise ? GL_CW : GL_CCW);
        }
        set_enabled(GL_BLEND, d.blend != Blend::Opaque, blend_);
        // The colour as SDL GPU blends it (rhi/sdlgpu); the destination's
        // alpha kept.
        if (d.blend == Blend::Alpha) glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        if (d.blend == Blend::Additive) glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        if (!state_known_) {
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_STENCIL_TEST);
            glDisable(GL_DITHER);
            state_known_ = true;
        }
    }

    void bind_vertex_buffers(Pass*, uint32_t first_slot, const BufferBinding* bindings, uint32_t count) override {
        for (uint32_t i = 0; i < count && first_slot + i < kMaxSlots; ++i) vertex_buffers_[first_slot + i] = bindings[i];
    }

    void bind_index_buffer(Pass*, const BufferBinding& binding, IndexSize size) override {
        index_buffer_ = binding;
        index_size_ = size;
    }

    void bind_fragment_textures(Pass*, uint32_t first_slot, const TextureBinding* bindings, uint32_t count) override {
        for (uint32_t i = 0; i < count && first_slot + i < kMaxTextures; ++i) {
            const uint32_t unit = first_slot + i;
            GlTexture* t = gl(bindings[i].texture);
            if (!t || !t->id) continue;
            bind_texture(int(unit), t->id);
            if (bindings[i].sampler) apply_sampler(t, gl(bindings[i].sampler)->desc);
        }
    }

    void set_viewport(Pass*, float x, float y, float width, float height) override {
        // The pass's top row is GL's row 0 (the bake's y flip), so a rectangle
        // from the top left is the same rectangle here.
        glViewport(GLint(x), GLint(y), GLsizei(width), GLsizei(height));
    }

    void push_uniforms(Stage stage, uint32_t slot, const void* data, uint32_t size) override {
        if (slot >= kMaxSlots) return;
        auto& block = uniforms_[stage == Stage::Vertex ? 0 : 1][slot];
        block.data.assign(static_cast<const float*>(data), static_cast<const float*>(data) + size / 4);
        block.generation = ++generation_;
    }

    void draw(Pass*, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex,
              uint32_t first_instance) override {
        GlPipeline* p = bound_pipeline_;
        if (!p) return;
        flush_uniforms(p);
        const GLenum mode = gl_primitive(p->desc.primitive);
        for (uint32_t i = 0; i < std::max(1u, instance_count); ++i) {
            if (!setup_attributes(p, 0, first_instance + i)) return;
            glDrawArrays(mode, GLint(first_vertex), GLsizei(vertex_count));
        }
    }

    void draw_indexed(Pass*, uint32_t index_count, uint32_t instance_count, uint32_t first_index,
                      int32_t vertex_offset, uint32_t first_instance) override {
        GlPipeline* p = bound_pipeline_;
        GlBuffer* ib = gl(index_buffer_.buffer);
        if (!p || !ib) return;
        if (index_size_ == IndexSize::U32 && !index32_) return;
        flush_uniforms(p);
        bind_buffer(GL_ELEMENT_ARRAY_BUFFER, ib->id);
        const GLenum type = index_size_ == IndexSize::U16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
        const uintptr_t offset =
            index_buffer_.offset + uintptr_t(first_index) * (index_size_ == IndexSize::U16 ? 2 : 4);
        const GLenum mode = gl_primitive(p->desc.primitive);
        for (uint32_t i = 0; i < std::max(1u, instance_count); ++i) {
            if (!setup_attributes(p, vertex_offset, first_instance + i)) return;
            glDrawElements(mode, GLsizei(index_count), type, reinterpret_cast<const void*>(offset));
        }
    }

private:
    // <root>/<stem>.<define>... in lower case, sorted: the engine's define set,
    // the same rule as tools/release/bake_shaders.sh. DRAGON_GLES_SHADERS
    // overrides the root, for a development build whose root is the HLSL.
    std::string baked_base(const ShaderSource& shaders) const {
        std::vector<std::string> defines = shaders.defines;
        for (std::string& d : defines) {
            for (char& c : d) c = char(std::tolower(static_cast<unsigned char>(c)));
        }
        std::sort(defines.begin(), defines.end());
        std::string root = shaders.root;
        if (const char* override_root = SDL_getenv("DRAGON_GLES_SHADERS")) root = override_root;
        std::string base = root;
        if (!base.empty() && base.back() != '/') base += '/';
        base += shaders.stem;
        for (const std::string& d : defines) base += "." + d;
        return base;
    }

    void bind_buffer(GLenum target, GLuint id) {
        if (target == GL_ARRAY_BUFFER) {
            if (array_buffer_ == id) return;
            array_buffer_ = id;
        }
        glBindBuffer(target, id);
    }

    void bind_texture(int unit, GLuint id) {
        if (active_unit_ != unit) {
            glActiveTexture(GLenum(GL_TEXTURE0 + unit));
            active_unit_ = unit;
        }
        if (unit < kMaxTextures && bound_textures_[unit] == id) return;
        glBindTexture(GL_TEXTURE_2D, id);
        if (unit < kMaxTextures) bound_textures_[unit] = id;
    }

    void set_enabled(GLenum cap, bool on, int& cached) {
        if (state_known_ && cached == int(on)) return;
        if (on) glEnable(cap);
        else glDisable(cap);
        cached = int(on);
    }

    void upload_level(GlTexture* t, uint32_t level, const void* pixels, uint32_t width, uint32_t height) {
        bind_texture(0, t->id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        const void* data = pixels;
        std::vector<uint8_t> linear;
        if (t->srgb && pixels) {
            const uint8_t* table = srgb_to_linear_table();
            const auto* src = static_cast<const uint8_t*>(pixels);
            linear.resize(size_t(width) * height * 4);
            for (size_t i = 0; i < linear.size(); ++i) linear[i] = (i & 3) == 3 ? src[i] : table[src[i]];
            data = linear.data();
        }
        glTexImage2D(GL_TEXTURE_2D, GLint(level), GL_RGBA, GLsizei(width), GLsizei(height), 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, data);
        if (level < 32) t->levels_uploaded |= 1u << level;
    }

    // A texture's whole mip chain is there to sample with a mip filter;
    // otherwise GLES 2.0 calls the texture incomplete and samples black.
    static bool has_mips(const GlTexture* t) {
        if (t->desc.mip_levels <= 1) return false;
        if (t->mips_generated) return true;
        uint32_t w = t->desc.width, h = t->desc.height, levels = 1;
        while (w > 1 || h > 1) {
            w = std::max(1u, w / 2);
            h = std::max(1u, h / 2);
            ++levels;
        }
        for (uint32_t l = 0; l < std::min(levels, 32u); ++l) {
            if (!(t->levels_uploaded & (1u << l))) return false;
        }
        return true;
    }

    void apply_sampler(GlTexture* t, const SamplerDesc& s) {
        if (t->params_set && t->params.min_filter == s.min_filter && t->params.mag_filter == s.mag_filter &&
            t->params.mip_mode == s.mip_mode && t->params.address_u == s.address_u &&
            t->params.address_v == s.address_v)
            return;
        const bool mips = has_mips(t);
        GLenum min = s.min_filter == Filter::Linear ? GL_LINEAR : GL_NEAREST;
        if (mips) {
            if (s.min_filter == Filter::Linear)
                min = s.mip_mode == MipMode::Linear ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST;
            else
                min = s.mip_mode == MipMode::Linear ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST;
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(min));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, s.mag_filter == Filter::Linear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLint(gl_wrap(s.address_u)));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLint(gl_wrap(s.address_v)));
        t->params = s;
        t->params_set = true;
    }

    GLuint framebuffer(GlTexture* color, GlTexture* depth) {
        const auto key = std::make_pair(color, depth);
        for (const auto& [k, fbo] : framebuffers_) {
            if (k == key) return fbo;
        }
        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        if (color) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color->id, 0);
        if (depth) glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth->renderbuffer);
        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) LOG_ERROR("gles: framebuffer incomplete (0x%x)", status);
        framebuffers_.push_back({key, fbo});
        return fbo;
    }

    void flush_uniforms(GlPipeline* p) {
        for (int stage = 0; stage < 2; ++stage) {
            for (UniformArray& a : p->uniforms[stage]) {
                const auto& block = uniforms_[stage][a.slot];
                if (block.generation == a.uploaded || block.data.empty()) continue;
                const int vec4s = std::min(a.vec4s, int(block.data.size() / 4));
                glUniform4fv(a.location, vec4s, block.data.data());
                a.uploaded = block.generation;
            }
        }
    }

    // Points each attribute the program reads at its buffer, `vertex_offset`
    // vertices in (the base vertex), or sets it to `instance`'s values.
    // Skipped when nothing it reads changed since the last draw: pointing
    // them again made vc4 revalidate its vertex state every draw, and the
    // terrain alone is hundreds of draws of one mesh.
    bool setup_attributes(GlPipeline* p, int32_t vertex_offset, uint32_t instance) {
        AttributeKey key;
        key.pipeline = p;
        key.vertex_offset = vertex_offset;
        key.instance = p->instanced ? instance : 0;
        for (int i = 0; i < kMaxSlots; ++i) key.buffers[i] = vertex_buffers_[i];
        if (attributes_valid_ && key == attribute_key_) return true;
        uint32_t wanted = 0;
        if (p->vertex_id) {
            bind_buffer(GL_ARRAY_BUFFER, vertex_ids_);
            glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
            wanted |= 1u;
        }
        for (const VertexAttribute& a : p->desc.vertex_attributes) {
            if (a.location >= uint32_t(max_attributes_) || !(p->attributes & (1u << a.location))) continue;
            const VertexBufferLayout* layout = nullptr;
            for (const VertexBufferLayout& l : p->desc.vertex_buffers) {
                if (l.slot == a.buffer_slot) layout = &l;
            }
            if (!layout || a.buffer_slot >= kMaxSlots) continue;
            const BufferBinding& binding = vertex_buffers_[a.buffer_slot];
            GlBuffer* b = gl(binding.buffer);
            if (!b) return false;
            const AttribFormat f = attrib_format(a.format);
            if (layout->rate == InputRate::Instance) {
                const size_t at = binding.offset + size_t(instance) * layout->pitch + a.offset;
                if (at + f.bytes > b->shadow.size()) return false;
                float v[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                const uint8_t* src = b->shadow.data() + at;
                for (int c = 0; c < f.components; ++c) {
                    if (f.type == GL_FLOAT) {
                        std::memcpy(&v[c], src + c * 4, 4);
                    } else {
                        v[c] = f.normalized ? float(src[c]) / 255.0f : float(src[c]);
                    }
                }
                if (enabled_attributes_ & (1u << a.location)) glDisableVertexAttribArray(a.location);
                enabled_attributes_ &= ~(1u << a.location);
                glVertexAttrib4fv(a.location, v);
                continue;
            }
            bind_buffer(GL_ARRAY_BUFFER, b->id);
            const intptr_t offset = intptr_t(binding.offset) + intptr_t(vertex_offset) * layout->pitch + a.offset;
            glVertexAttribPointer(a.location, f.components, f.type, f.normalized, GLsizei(layout->pitch),
                                  reinterpret_cast<const void*>(offset));
            wanted |= 1u << a.location;
        }
        for (int i = 0; i < max_attributes_; ++i) {
            const uint32_t bit = 1u << i;
            if ((wanted & bit) && !(enabled_attributes_ & bit)) glEnableVertexAttribArray(GLuint(i));
            if (!(wanted & bit) && (enabled_attributes_ & bit)) glDisableVertexAttribArray(GLuint(i));
        }
        enabled_attributes_ = (enabled_attributes_ & ~((1u << max_attributes_) - 1)) | wanted;
        attribute_key_ = key;
        attributes_valid_ = true;
        return true;
    }

    struct AttributeKey {
        const GlPipeline* pipeline = nullptr;
        int32_t vertex_offset = 0;
        uint32_t instance = 0;
        BufferBinding buffers[kMaxSlots] = {};
        bool operator==(const AttributeKey& o) const {
            if (pipeline != o.pipeline || vertex_offset != o.vertex_offset || instance != o.instance) return false;
            for (int i = 0; i < kMaxSlots; ++i) {
                if (buffers[i].buffer != o.buffers[i].buffer || buffers[i].offset != o.buffers[i].offset) return false;
            }
            return true;
        }
    };

    // `source`'s top-left sw x sh over the bound framebuffer's dw x dh.
    void draw_quad(GlTexture* source, uint32_t sw, uint32_t sh, int dw, int dh, bool flip) {
        glViewport(0, 0, dw, dh);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glUseProgram(blit_program_);
        program_ = blit_program_;
        bound_pipeline_ = nullptr;
        glUniform4f(blit_rect_, float(sw) / float(source->desc.width), float(sh) / float(source->desc.height),
                    flip ? 1.0f : 0.0f, 0.0f);
        bind_texture(0, source->id);
        SamplerDesc linear;
        linear.mip_mode = MipMode::Nearest;
        linear.address_u = linear.address_v = Address::Clamp;
        apply_sampler(source, linear);
        bind_buffer(GL_ARRAY_BUFFER, quad_);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        for (int i = 0; i < max_attributes_; ++i) {
            const uint32_t bit = 1u << i;
            if (i == 0 && !(enabled_attributes_ & bit)) glEnableVertexAttribArray(0);
            if (i != 0 && (enabled_attributes_ & bit)) glDisableVertexAttribArray(GLuint(i));
        }
        enabled_attributes_ = 1u;
        attributes_valid_ = false;
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        state_known_ = false;
    }

    void download(Texture* source, uint32_t width, uint32_t height, std::vector<uint8_t>& out) {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer(gl(source), nullptr));
        out.resize(size_t(width) * height * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        // Row 0 is the frame's top (the bake's flip): top row first, as the
        // other backends hand it back.
        glReadPixels(0, 0, GLsizei(width), GLsizei(height), GL_RGBA, GL_UNSIGNED_BYTE, out.data());
        for (size_t i = 3; i < out.size(); i += 4) out[i] = 255;
    }

    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    bool headless_ = false;
    bool index32_ = false, depth24_ = false, discard_ = false;
    PFNGLDISCARDFRAMEBUFFEREXTPROC discard_framebuffer_ = nullptr;
    uint32_t max_texture_ = 2048;
    int max_attributes_ = 8;
    int window_w_ = 0, window_h_ = 0;

    GLuint vertex_ids_ = 0, quad_ = 0, blit_program_ = 0;
    GLint blit_rect_ = -1;
    std::vector<std::pair<std::pair<GlTexture*, GlTexture*>, GLuint>> framebuffers_;

    GlPass pass_;
    GlPipeline* bound_pipeline_ = nullptr;
    BufferBinding vertex_buffers_[kMaxSlots] = {};
    BufferBinding index_buffer_;
    IndexSize index_size_ = IndexSize::U16;
    struct Block {
        std::vector<float> data;
        uint64_t generation = 0;
    };
    Block uniforms_[2][kMaxSlots];
    uint64_t generation_ = 0;

    // The state cache.
    GLuint program_ = 0;
    GLuint array_buffer_ = 0;
    uint32_t enabled_attributes_ = 0;
    GLuint bound_textures_[kMaxTextures] = {};
    int active_unit_ = -1;
    bool state_known_ = false;
    int depth_test_ = -1, cull_ = -1, blend_ = -1;
    AttributeKey attribute_key_;
    bool attributes_valid_ = false;

};

}  // namespace

std::unique_ptr<Device> create(SDL_Window* window, const DeviceConfig& config) {
    auto device = std::make_unique<GlesDevice>();
    if (!device->init(window, config)) return nullptr;
    return device;
}

void forget_state(Device* device) {
    if (device && device->backend() == Backend::OpenGLES2) static_cast<GlesDevice*>(device)->forget();
}

void prepare_video() {
#if defined(__APPLE__) && defined(DRAGON_ANGLE_DIR)
    // SDL's Cocoa driver makes a GLES context through EGL, which on the Mac
    // is ANGLE (tools/gles/setup_angle_mac.sh).
    SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
    SDL_SetHint(SDL_HINT_VIDEO_FORCE_EGL, "1");
    SDL_SetHint(SDL_HINT_EGL_LIBRARY, DRAGON_ANGLE_DIR "/libEGL.dylib");
    SDL_SetHint(SDL_HINT_OPENGL_LIBRARY, DRAGON_ANGLE_DIR "/libGLESv2.dylib");
#endif
}

void prepare_window() {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);  // every pass has its own target
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
}

}  // namespace gles
}  // namespace rhi
