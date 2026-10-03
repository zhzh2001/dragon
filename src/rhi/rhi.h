#pragma once

// The render hardware interface: everything the engine asks of a GPU, and
// nothing about which API answers it (docs/PORTING.md, R1).
//
// The engine -- src/gfx, the skinned mesh, the app -- talks only to this
// file. A backend implements rhi::Device: today SDL's GPU API
// (src/rhi/sdlgpu, which reaches Metal, D3D12 and Vulkan), later Direct3D 9
// with its SM3, SM2 and fixed-function tiers. The surface is deliberately the
// small set the game uses, shaped so an immediate-mode API maps onto it as
// directly as a command-buffer one:
//
//   - resources are opaque handles the backend owns: buffers, textures,
//     samplers, pipelines;
//   - a pipeline is a description plus a shader; the backend decides how to
//     turn the HLSL into whatever it runs (shadercross, a baked file,
//     D3DCompile);
//   - a frame is passes, and a pass binds, pushes uniforms and draws;
//   - per-frame data reaches a buffer through an upload that is recorded
//     OUTSIDE any pass (a copy cannot open inside a render pass on the
//     command-buffer APIs; on D3D9 it is a Lock/Unlock that does not care);
//   - uniforms are pushed by slot, per stage, like SDL_PushGPU*UniformData
//     and like SetVertexShaderConstantF over a slot's register range.
//
// Handles are created and destroyed through the device; a null handle is
// always safe to destroy. Every call that takes a Pass* must be made between
// begin_pass and end_pass on that pass.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct SDL_Window;

namespace rhi {

struct Buffer;
struct Texture;
struct Sampler;
struct Pipeline;
struct Pass;

enum class Format : uint8_t {
    Invalid,
    RGBA8,       // 8-bit unorm colour: the scene colour target, data textures
    RGBA8_SRGB,  // 8-bit sRGB colour: base-colour textures, sampled to linear
    RGBA16F,     // the linear HDR scene and the bloom chain
    // Block-compressed colour, 4x4 texels a block (DXT1 and DXT5 on D3D9):
    // BC1 is 8 bytes a block, opaque; BC3 adds a separate alpha block, 16.
    BC1,
    BC1_SRGB,
    BC3,
    BC3_SRGB,
    D16,
    D24,
    D32F,
};

enum TextureUsage : uint8_t {
    TEXTURE_SAMPLED = 1,
    TEXTURE_COLOR_TARGET = 2,
    TEXTURE_DEPTH_TARGET = 4,
};

enum class BufferUsage : uint8_t { Vertex, Index };
enum class IndexSize : uint8_t { U16, U32 };
enum class Stage : uint8_t { Vertex, Fragment };

enum class Filter : uint8_t { Nearest, Linear };
enum class MipMode : uint8_t { Nearest, Linear };
enum class Address : uint8_t { Repeat, Clamp, Mirror };
enum class Compare : uint8_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };

enum class Primitive : uint8_t { TriangleList, TriangleStrip, LineList, LineStrip, PointList };
enum class Cull : uint8_t { None, Front, Back };
enum class Fill : uint8_t { Solid, Wireframe };
enum class FrontFace : uint8_t { CounterClockwise, Clockwise };
// Opaque writes the source; Alpha is src-alpha over; Additive is one + one
// (fire, glows: light rather than surface).
enum class Blend : uint8_t { Opaque, Alpha, Additive };

enum class VertexFormat : uint8_t { Float, Float2, Float3, Float4, UByte4, UByte4Norm };
enum class InputRate : uint8_t { Vertex, Instance };

struct VertexBufferLayout {
    uint32_t slot = 0;
    uint32_t pitch = 0;
    InputRate rate = InputRate::Vertex;
};

struct VertexAttribute {
    uint32_t location = 0;  // the shader's TEXCOORD<n>
    uint32_t buffer_slot = 0;
    VertexFormat format = VertexFormat::Float3;
    uint32_t offset = 0;
};

struct TextureDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mip_levels = 1;
    Format format = Format::RGBA8;
    uint8_t usage = TEXTURE_SAMPLED;
};

struct SamplerDesc {
    Filter min_filter = Filter::Linear;
    Filter mag_filter = Filter::Linear;
    MipMode mip_mode = MipMode::Linear;
    Address address_u = Address::Repeat;
    Address address_v = Address::Repeat;
    Address address_w = Address::Repeat;
    float max_anisotropy = 0.0f;  // 0 or 1: off
    float max_lod = 1000.0f;
};

struct PipelineDesc {
    std::string name;  // for logs and GPU debugger labels
    std::vector<VertexBufferLayout> vertex_buffers;
    std::vector<VertexAttribute> vertex_attributes;
    Primitive primitive = Primitive::TriangleList;
    Cull cull = Cull::Back;
    Fill fill = Fill::Solid;
    FrontFace front_face = FrontFace::CounterClockwise;
    bool depth_test = true;
    bool depth_write = true;
    Compare depth_compare = Compare::Greater;  // reversed-Z in the main pass
    Blend blend = Blend::Opaque;
    Format color_format = Format::Invalid;  // Invalid: no colour attachment
    Format depth_format = Format::Invalid;  // Invalid: no depth attachment
};

// What a pipeline's shaders are made from. `stem` names shaders/<stem>.hlsl,
// whose entry points are vs_main and fs_main; `hlsl` is that file with its
// includes inlined (empty for a backend that loads baked shaders instead);
// `root` is the shader directory.
struct ShaderSource {
    std::string stem;
    std::string hlsl;
    std::string root;
    // Preprocessor names defined for both stages, e.g. LDR_OUTPUT for a tier
    // without a float target (gfx/render_tier.h).
    std::vector<std::string> defines;
};

struct BufferBinding {
    Buffer* buffer = nullptr;
    uint32_t offset = 0;
};

struct TextureBinding {
    Texture* texture = nullptr;
    Sampler* sampler = nullptr;
};

// One render pass: at most one colour target and one depth target. A target
// is cleared or its contents kept; a cleared target's old contents may be
// discarded by the backend.
struct PassDesc {
    Texture* color = nullptr;
    bool clear_color = false;
    float clear_rgba[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    Texture* depth = nullptr;
    bool clear_depth = false;
    float clear_depth_value = 0.0f;
    bool keep_depth = false;  // store the depth for a later pass to read
};

enum class FrameStatus : uint8_t {
    Ready,    // record and end the frame
    Skip,     // minimised or mid-resize: nothing to draw into this time
    Failed,
};

struct DeviceConfig {
    bool headless = false;  // never present; frames render offscreen only
    bool debug = true;
    // A backend-specific driver name ("vulkan", "direct3d12", "metal"), or
    // empty for the platform's first.
    std::string driver;
    // Which adapter, for a backend that picks one (D3D9); 0 is the default.
    uint32_t adapter = 0;
};

enum class Backend : uint8_t { SdlGpu, Direct3D9 };

class Device {
public:
    virtual ~Device() = default;

    // Which implementation answers: only Dear ImGui's renderer glue
    // (editor/imgui_layer.cpp) needs to know, since it draws through the
    // backend's native API.
    virtual Backend backend() const = 0;

    // The backend for `config.driver`: "direct3d9" is the D3D9 backend on
    // Windows (rhi/d3d9), anything else SDL's GPU API (rhi/sdlgpu), on `window`
    // (which a headless device never presents to). Null on failure, with the
    // reason logged.
    static std::unique_ptr<Device> create(SDL_Window* window, const DeviceConfig& config);

    virtual const char* driver_name() const = 0;
    virtual bool supports_format(Format format, uint8_t usage) const = 0;
    // The largest texture edge the device takes: 2048 on an X550, which the
    // graphics settings respect (gfx/graphics_settings.h).
    virtual uint32_t max_texture_size() const { return 16384; }

    // ---- the frame
    // Starts recording. Windowed, it also waits for a presentable image and
    // reports its size, which the caller sizes its targets to.
    virtual FrameStatus begin_frame(uint32_t* present_width, uint32_t* present_height) = 0;
    // Presents `source` (scaled to the window) unless headless, then submits.
    // With `readback`, the frame first copies `source` (RGBA8) into it and the
    // submit waits for the GPU -- a debug path (screenshots), never per-frame.
    virtual void end_frame(Texture* source, uint32_t width, uint32_t height,
                           std::vector<uint8_t>* readback) = 0;
    virtual void wait_idle() = 0;

    // ---- resources
    // With `data`, the buffer is filled now, synchronously (load time only).
    virtual Buffer* create_buffer(BufferUsage usage, uint32_t size, const void* data,
                                  const char* debug_name) = 0;
    virtual void destroy(Buffer* buffer) = 0;
    // Per-frame data: map, write up to `size` bytes, commit. Must be called
    // outside any pass; draws recorded after it see the new contents. The
    // previous contents are discarded, so write everything the frame needs.
    virtual void* map_upload(Buffer* buffer, uint32_t size) = 0;
    virtual void commit_upload(Buffer* buffer, uint32_t size) = 0;

    virtual Texture* create_texture(const TextureDesc& desc, const char* debug_name) = 0;
    // Fills mip 0 from tightly packed pixels (synchronously, load time only),
    // and with `generate_mips` the rest of the chain from it.
    virtual bool upload_texture(Texture* texture, const void* pixels, uint32_t width,
                                uint32_t height, bool generate_mips) = 0;
    // Fills one mip level from data already in the texture's format: tightly
    // packed texels, or for a block format tightly packed 4x4 blocks
    // (`bytes` of them). For a chain built on the CPU, or one that cannot be
    // generated on the GPU (block formats cannot be rendered to).
    virtual bool upload_texture_level(Texture* texture, uint32_t level, const void* data,
                                      uint32_t bytes, uint32_t width, uint32_t height) = 0;
    virtual void destroy(Texture* texture) = 0;

    virtual Sampler* create_sampler(const SamplerDesc& desc) = 0;
    virtual void destroy(Sampler* sampler) = 0;

    // Null when the shader fails to build; the reason is logged.
    virtual Pipeline* create_pipeline(const PipelineDesc& desc, const ShaderSource& shaders) = 0;
    virtual void destroy(Pipeline* pipeline) = 0;
    // Whether create_pipeline compiles ShaderSource::hlsl at runtime. If not,
    // it loads prebuilt files -- one set per define set -- which
    // baked_shader_files names so a cache can watch them.
    virtual bool compiles_hlsl() const = 0;
    virtual std::vector<std::string> baked_shader_files(const ShaderSource& shaders) const = 0;

    // ---- passes
    virtual Pass* begin_pass(const PassDesc& desc) = 0;
    virtual void end_pass(Pass* pass) = 0;
    virtual void bind_pipeline(Pass* pass, Pipeline* pipeline) = 0;
    virtual void bind_vertex_buffers(Pass* pass, uint32_t first_slot, const BufferBinding* bindings,
                                     uint32_t count) = 0;
    virtual void bind_index_buffer(Pass* pass, const BufferBinding& binding, IndexSize size) = 0;
    virtual void bind_fragment_textures(Pass* pass, uint32_t first_slot,
                                        const TextureBinding* bindings, uint32_t count) = 0;
    // Uniform block `slot` of `stage`, for the draws that follow.
    virtual void push_uniforms(Stage stage, uint32_t slot, const void* data, uint32_t size) = 0;
    virtual void draw(Pass* pass, uint32_t vertex_count, uint32_t instance_count = 1,
                      uint32_t first_vertex = 0, uint32_t first_instance = 0) = 0;
    virtual void draw_indexed(Pass* pass, uint32_t index_count, uint32_t instance_count = 1,
                              uint32_t first_index = 0, int32_t vertex_offset = 0,
                              uint32_t first_instance = 0) = 0;
};

}  // namespace rhi
