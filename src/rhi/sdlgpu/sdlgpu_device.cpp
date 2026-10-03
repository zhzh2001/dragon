// The SDL GPU backend of the RHI (rhi/rhi.h): Metal on macOS, Direct3D 12 or
// Vulkan on Windows, Vulkan on Linux. Everything here is what the engine did
// directly before R1 (docs/PORTING.md), moved behind the interface unchanged,
// which is what the golden screenshots check.
//
// Handles are SDL's own objects, cast: an rhi::Buffer* is an SDL_GPUBuffer*,
// and so on. The engine never looks inside one, so no wrapper is needed. The
// pipeline is the exception that is not -- it is an SDL pipeline too -- and a
// buffer's per-frame staging lives in a side table keyed by the buffer.

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "core/log.h"
#include "rhi/sdlgpu/sdlgpu.h"

#ifdef DRAGON_SHADERCROSS
#include <SDL3_shadercross/SDL_shadercross.h>
#endif

namespace rhi {
namespace {

SDL_GPUBuffer* sdl(Buffer* b) { return reinterpret_cast<SDL_GPUBuffer*>(b); }
SDL_GPUTexture* sdl(Texture* t) { return reinterpret_cast<SDL_GPUTexture*>(t); }
SDL_GPUSampler* sdl(Sampler* s) { return reinterpret_cast<SDL_GPUSampler*>(s); }
SDL_GPUGraphicsPipeline* sdl(Pipeline* p) { return reinterpret_cast<SDL_GPUGraphicsPipeline*>(p); }
SDL_GPURenderPass* sdl(Pass* p) { return reinterpret_cast<SDL_GPURenderPass*>(p); }

SDL_GPUTextureFormat to_sdl(Format f) {
    switch (f) {
        case Format::RGBA8: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        case Format::RGBA8_SRGB: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
        case Format::RGBA16F: return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        case Format::D16: return SDL_GPU_TEXTUREFORMAT_D16_UNORM;
        case Format::D24: return SDL_GPU_TEXTUREFORMAT_D24_UNORM;
        case Format::D32F: return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        case Format::Invalid: break;
    }
    return SDL_GPU_TEXTUREFORMAT_INVALID;
}

SDL_GPUTextureUsageFlags to_sdl_usage(uint8_t usage) {
    SDL_GPUTextureUsageFlags flags = 0;
    if (usage & TEXTURE_SAMPLED) flags |= SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (usage & TEXTURE_COLOR_TARGET) flags |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    if (usage & TEXTURE_DEPTH_TARGET) flags |= SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    return flags;
}

SDL_GPUFilter to_sdl(Filter f) { return f == Filter::Nearest ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR; }

SDL_GPUSamplerMipmapMode to_sdl(MipMode m) {
    return m == MipMode::Nearest ? SDL_GPU_SAMPLERMIPMAPMODE_NEAREST : SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
}

SDL_GPUSamplerAddressMode to_sdl(Address a) {
    switch (a) {
        case Address::Clamp: return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        case Address::Mirror: return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
        case Address::Repeat: break;
    }
    return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
}

SDL_GPUCompareOp to_sdl(Compare c) {
    switch (c) {
        case Compare::Never: return SDL_GPU_COMPAREOP_NEVER;
        case Compare::Less: return SDL_GPU_COMPAREOP_LESS;
        case Compare::Equal: return SDL_GPU_COMPAREOP_EQUAL;
        case Compare::LessEqual: return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        case Compare::Greater: return SDL_GPU_COMPAREOP_GREATER;
        case Compare::NotEqual: return SDL_GPU_COMPAREOP_NOT_EQUAL;
        case Compare::GreaterEqual: return SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
        case Compare::Always: return SDL_GPU_COMPAREOP_ALWAYS;
    }
    return SDL_GPU_COMPAREOP_ALWAYS;
}

SDL_GPUPrimitiveType to_sdl(Primitive p) {
    switch (p) {
        case Primitive::TriangleStrip: return SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
        case Primitive::LineList: return SDL_GPU_PRIMITIVETYPE_LINELIST;
        case Primitive::LineStrip: return SDL_GPU_PRIMITIVETYPE_LINESTRIP;
        case Primitive::PointList: return SDL_GPU_PRIMITIVETYPE_POINTLIST;
        case Primitive::TriangleList: break;
    }
    return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
}

SDL_GPUCullMode to_sdl(Cull c) {
    switch (c) {
        case Cull::None: return SDL_GPU_CULLMODE_NONE;
        case Cull::Front: return SDL_GPU_CULLMODE_FRONT;
        case Cull::Back: break;
    }
    return SDL_GPU_CULLMODE_BACK;
}

SDL_GPUVertexElementFormat to_sdl(VertexFormat f) {
    switch (f) {
        case VertexFormat::Float: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
        case VertexFormat::Float2: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        case VertexFormat::Float3: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        case VertexFormat::Float4: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        case VertexFormat::UByte4: return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4;
        case VertexFormat::UByte4Norm: return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    }
    return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
}

#ifdef DRAGON_SHADERCROSS
// Development builds compile the HLSL at runtime: DXC to SPIR-V, reflection
// for the resource counts SDL needs, then SPIR-V to whatever the device takes
// (MSL on Metal). The source is compiled once per stage, with VERTEX_STAGE or
// FRAGMENT_STAGE defined (shaders/common.hlsl says why).
SDL_GPUShader* compile_shader(SDL_GPUDevice* gpu, const ShaderSource& src, const char* entrypoint,
                              SDL_GPUShaderStage stage) {
    const bool vertex = stage == SDL_GPU_SHADERSTAGE_VERTEX;
    SDL_ShaderCross_HLSL_Define defines[2] = {};
    defines[0].name = const_cast<char*>(vertex ? "VERTEX_STAGE" : "FRAGMENT_STAGE");

    SDL_ShaderCross_HLSL_Info hlsl = {};
    hlsl.source = src.hlsl.c_str();
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

std::string read_file(const std::string& path) {
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) return {};
    std::string out(static_cast<char*>(data), size);
    SDL_free(data);
    return out;
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

SDL_GPUShader* compile_shader(SDL_GPUDevice* gpu, const ShaderSource& src, const char* entrypoint,
                              SDL_GPUShaderStage stage) {
    std::string root = src.root;
    if (!root.empty() && root.back() != '/' && root.back() != '\\') root += '/';
    const std::string base = root + src.stem + "." + stage_name(stage);
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

class SdlGpuDevice final : public Device {
public:
    bool init(SDL_Window* window, const DeviceConfig& config) {
        window_ = window;
        headless_ = config.headless;
#ifdef DRAGON_SHADERCROSS
        if (!SDL_ShaderCross_Init()) LOG_ERROR("SDL_ShaderCross_Init failed: %s", SDL_GetError());
#endif
        // The shader formats this build can hand the device: shadercross
        // translates the HLSL into any of them at runtime, and a package
        // carries its platform's baked set.
#if defined(__APPLE__)
        const SDL_GPUShaderFormat formats = SDL_GPU_SHADERFORMAT_MSL;
#elif defined(_WIN32)
        const SDL_GPUShaderFormat formats = SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV;
#else
        const SDL_GPUShaderFormat formats = SDL_GPU_SHADERFORMAT_SPIRV;
#endif
        gpu_ = SDL_CreateGPUDevice(formats, config.debug, config.driver.empty() ? nullptr : config.driver.c_str());
        if (!gpu_) {
            LOG_ERROR("SDL_CreateGPUDevice failed: %s", SDL_GetError());
            return false;
        }
        if (!headless_) {
            if (!SDL_ClaimWindowForGPUDevice(gpu_, window_)) {
                LOG_ERROR("SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
                return false;
            }
            claimed_ = true;
            // Mailbox where available (lowest latency), else vsync. Input
            // latency has a direct effect on how responsive flight feels.
            if (SDL_WindowSupportsGPUPresentMode(gpu_, window_, SDL_GPU_PRESENTMODE_MAILBOX)) {
                SDL_SetGPUSwapchainParameters(gpu_, window_, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                              SDL_GPU_PRESENTMODE_MAILBOX);
            }
        }
        return true;
    }

    ~SdlGpuDevice() override {
        if (gpu_) {
            SDL_WaitForGPUIdle(gpu_);
            for (auto& [buffer, staging] : staging_) SDL_ReleaseGPUTransferBuffer(gpu_, staging.transfer);
            staging_.clear();
            if (window_ && claimed_) SDL_ReleaseWindowFromGPUDevice(gpu_, window_);
            SDL_DestroyGPUDevice(gpu_);
        }
#ifdef DRAGON_SHADERCROSS
        SDL_ShaderCross_Quit();
#endif
    }

    SDL_GPUDevice* gpu() const { return gpu_; }
    SDL_GPUCommandBuffer* cmd() const { return cmd_; }

    const char* driver_name() const override { return SDL_GetGPUDeviceDriver(gpu_); }

    bool supports_format(Format format, uint8_t usage) const override {
        return SDL_GPUTextureSupportsFormat(gpu_, to_sdl(format), SDL_GPU_TEXTURETYPE_2D, to_sdl_usage(usage));
    }

    // ---- the frame
    FrameStatus begin_frame(uint32_t* present_width, uint32_t* present_height) override {
        cmd_ = SDL_AcquireGPUCommandBuffer(gpu_);
        if (!cmd_) {
            LOG_ERROR("SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
            return FrameStatus::Failed;
        }
        if (headless_) return FrameStatus::Ready;

        // Blocks until a swapchain image is free, which keeps the CPU from
        // running arbitrarily far ahead of the display.
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd_, window_, &swapchain_, &swapchain_w_, &swapchain_h_)) {
            LOG_ERROR("SDL_WaitAndAcquireGPUSwapchainTexture failed: %s", SDL_GetError());
            SDL_SubmitGPUCommandBuffer(cmd_);
            cmd_ = nullptr;
            return FrameStatus::Failed;
        }
        // Legitimately null while minimized or mid-resize.
        if (!swapchain_) {
            SDL_SubmitGPUCommandBuffer(cmd_);
            cmd_ = nullptr;
            return FrameStatus::Skip;
        }
        if (present_width) *present_width = swapchain_w_;
        if (present_height) *present_height = swapchain_h_;
        return FrameStatus::Ready;
    }

    // A frame that was begun but is abandoned (the caller's targets could not
    // be sized): submit what little was recorded.
    void abandon_frame() {
        if (cmd_) SDL_SubmitGPUCommandBuffer(cmd_);
        cmd_ = nullptr;
        swapchain_ = nullptr;
    }

    void end_frame(Texture* source, uint32_t width, uint32_t height, std::vector<uint8_t>* readback) override {
        if (!cmd_) return;
        if (swapchain_ && source) {
            SDL_GPUBlitInfo blit = {};
            blit.source.texture = sdl(source);
            blit.source.w = width;
            blit.source.h = height;
            blit.destination.texture = swapchain_;
            blit.destination.w = swapchain_w_;
            blit.destination.h = swapchain_h_;
            blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
            blit.filter = SDL_GPU_FILTER_LINEAR;
            SDL_BlitGPUTexture(cmd_, &blit);
        }

        if (readback && source) {
            download(source, width, height, *readback);  // submits and waits itself
            return;
        }

        if (!SDL_SubmitGPUCommandBuffer(cmd_)) LOG_ERROR("SDL_SubmitGPUCommandBuffer failed: %s", SDL_GetError());
        cmd_ = nullptr;
        swapchain_ = nullptr;

        // Windowed frames are throttled by swapchain acquisition. Headless
        // frames are not, so without this the loop submits work as fast as the
        // CPU can build it and lets GPU resources pile up unboundedly.
        if (headless_) SDL_WaitForGPUIdle(gpu_);
    }

    void wait_idle() override {
        if (gpu_) SDL_WaitForGPUIdle(gpu_);
    }

    // ---- buffers
    Buffer* create_buffer(BufferUsage usage, uint32_t size, const void* data, const char* debug_name) override {
        if (size == 0) return nullptr;
        SDL_GPUBufferCreateInfo buffer_info = {};
        buffer_info.usage = usage == BufferUsage::Index ? SDL_GPU_BUFFERUSAGE_INDEX : SDL_GPU_BUFFERUSAGE_VERTEX;
        buffer_info.size = size;
        SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(gpu_, &buffer_info);
        if (!buffer) {
            LOG_ERROR("SDL_CreateGPUBuffer(%s) failed: %s", debug_name, SDL_GetError());
            return nullptr;
        }
        SDL_SetGPUBufferName(gpu_, buffer, debug_name);
        if (data && !upload_now(buffer, data, size, debug_name)) {
            SDL_ReleaseGPUBuffer(gpu_, buffer);
            return nullptr;
        }
        return reinterpret_cast<Buffer*>(buffer);
    }

    void destroy(Buffer* buffer) override {
        if (!buffer) return;
        auto it = staging_.find(buffer);
        if (it != staging_.end()) {
            SDL_ReleaseGPUTransferBuffer(gpu_, it->second.transfer);
            staging_.erase(it);
        }
        SDL_ReleaseGPUBuffer(gpu_, sdl(buffer));
    }

    void* map_upload(Buffer* buffer, uint32_t size) override {
        if (!buffer || size == 0) return nullptr;
        Staging& staging = staging_[buffer];
        if (staging.transfer && staging.size < size) {
            SDL_ReleaseGPUTransferBuffer(gpu_, staging.transfer);
            staging.transfer = nullptr;
        }
        if (!staging.transfer) {
            SDL_GPUTransferBufferCreateInfo info = {};
            info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            info.size = size;
            staging.transfer = SDL_CreateGPUTransferBuffer(gpu_, &info);
            staging.size = staging.transfer ? size : 0;
            if (!staging.transfer) {
                LOG_ERROR("SDL_CreateGPUTransferBuffer(upload) failed: %s", SDL_GetError());
                return nullptr;
            }
        }
        // cycle=true hands back fresh storage rather than stalling on the
        // previous frame's copy still being read.
        void* mapped = SDL_MapGPUTransferBuffer(gpu_, staging.transfer, true);
        if (!mapped) LOG_ERROR("SDL_MapGPUTransferBuffer failed: %s", SDL_GetError());
        return mapped;
    }

    void commit_upload(Buffer* buffer, uint32_t size) override {
        auto it = staging_.find(buffer);
        if (it == staging_.end() || !it->second.transfer || !cmd_) return;
        SDL_UnmapGPUTransferBuffer(gpu_, it->second.transfer);
        if (size == 0) return;
        SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd_);
        SDL_GPUTransferBufferLocation src = {};
        src.transfer_buffer = it->second.transfer;
        SDL_GPUBufferRegion dst = {};
        dst.buffer = sdl(buffer);
        dst.size = size;
        SDL_UploadToGPUBuffer(pass, &src, &dst, true);
        SDL_EndGPUCopyPass(pass);
    }

    // ---- textures
    Texture* create_texture(const TextureDesc& desc, const char* debug_name) override {
        SDL_GPUTextureCreateInfo info = {};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = to_sdl(desc.format);
        info.usage = to_sdl_usage(desc.usage);
        info.width = desc.width;
        info.height = desc.height;
        info.layer_count_or_depth = 1;
        info.num_levels = desc.mip_levels;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        SDL_GPUTexture* texture = SDL_CreateGPUTexture(gpu_, &info);
        if (!texture) {
            LOG_ERROR("SDL_CreateGPUTexture(%s) failed: %s", debug_name, SDL_GetError());
            return nullptr;
        }
        if (debug_name) SDL_SetGPUTextureName(gpu_, texture, debug_name);
        return reinterpret_cast<Texture*>(texture);
    }

    bool upload_texture(Texture* texture, const void* pixels, uint32_t width, uint32_t height,
                        bool generate_mips) override {
        if (!texture || !pixels) return false;
        const uint32_t bytes = width * height * 4;
        SDL_GPUTransferBufferCreateInfo transfer_info = {};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = bytes;
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(gpu_, &transfer_info);
        if (!transfer) {
            LOG_ERROR("SDL_CreateGPUTransferBuffer(texture) failed: %s", SDL_GetError());
            return false;
        }
        void* mapped = SDL_MapGPUTransferBuffer(gpu_, transfer, false);
        std::memcpy(mapped, pixels, bytes);
        SDL_UnmapGPUTransferBuffer(gpu_, transfer);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu_);
        SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo source = {};
        source.transfer_buffer = transfer;
        source.pixels_per_row = width;
        source.rows_per_layer = height;
        SDL_GPUTextureRegion destination = {};
        destination.texture = sdl(texture);
        destination.w = width;
        destination.h = height;
        destination.d = 1;
        SDL_UploadToGPUTexture(pass, &source, &destination, false);
        SDL_EndGPUCopyPass(pass);
        if (generate_mips) SDL_GenerateMipmapsForGPUTexture(cmd, sdl(texture));
        submit_and_wait(cmd);
        SDL_ReleaseGPUTransferBuffer(gpu_, transfer);
        return true;
    }

    void destroy(Texture* texture) override {
        if (texture) SDL_ReleaseGPUTexture(gpu_, sdl(texture));
    }

    // ---- samplers
    Sampler* create_sampler(const SamplerDesc& desc) override {
        SDL_GPUSamplerCreateInfo info = {};
        info.min_filter = to_sdl(desc.min_filter);
        info.mag_filter = to_sdl(desc.mag_filter);
        info.mipmap_mode = to_sdl(desc.mip_mode);
        info.address_mode_u = to_sdl(desc.address_u);
        info.address_mode_v = to_sdl(desc.address_v);
        info.address_mode_w = to_sdl(desc.address_w);
        info.enable_anisotropy = desc.max_anisotropy > 1.0f;
        info.max_anisotropy = desc.max_anisotropy > 1.0f ? desc.max_anisotropy : 0.0f;
        info.max_lod = desc.max_lod;
        SDL_GPUSampler* sampler = SDL_CreateGPUSampler(gpu_, &info);
        if (!sampler) LOG_ERROR("SDL_CreateGPUSampler failed: %s", SDL_GetError());
        return reinterpret_cast<Sampler*>(sampler);
    }

    void destroy(Sampler* sampler) override {
        if (sampler) SDL_ReleaseGPUSampler(gpu_, sdl(sampler));
    }

    // ---- pipelines
    Pipeline* create_pipeline(const PipelineDesc& d, const ShaderSource& shaders) override {
        SDL_GPUShader* vs = compile_shader(gpu_, shaders, "vs_main", SDL_GPU_SHADERSTAGE_VERTEX);
        if (!vs) {
            LOG_ERROR("[%s] vertex shader '%s' failed: %s", d.name.c_str(), shaders.stem.c_str(), SDL_GetError());
            return nullptr;
        }
        SDL_GPUShader* fs = compile_shader(gpu_, shaders, "fs_main", SDL_GPU_SHADERSTAGE_FRAGMENT);
        if (!fs) {
            LOG_ERROR("[%s] fragment shader '%s' failed: %s", d.name.c_str(), shaders.stem.c_str(), SDL_GetError());
            SDL_ReleaseGPUShader(gpu_, vs);
            return nullptr;
        }

        SDL_GPUColorTargetBlendState blend = {};
        if (d.blend == Blend::Additive) {
            blend.enable_blend = true;
            blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
            blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        } else if (d.blend == Blend::Alpha) {
            blend.enable_blend = true;
            blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
            blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        }
        SDL_GPUColorTargetDescription color_target = {};
        color_target.format = to_sdl(d.color_format);
        color_target.blend_state = blend;

        std::vector<SDL_GPUVertexBufferDescription> buffers;
        for (const VertexBufferLayout& b : d.vertex_buffers) {
            SDL_GPUVertexBufferDescription vb = {};
            vb.slot = b.slot;
            vb.pitch = b.pitch;
            vb.input_rate = b.rate == InputRate::Instance ? SDL_GPU_VERTEXINPUTRATE_INSTANCE
                                                          : SDL_GPU_VERTEXINPUTRATE_VERTEX;
            buffers.push_back(vb);
        }
        std::vector<SDL_GPUVertexAttribute> attributes;
        for (const VertexAttribute& a : d.vertex_attributes) {
            SDL_GPUVertexAttribute attribute = {};
            attribute.location = a.location;
            attribute.buffer_slot = a.buffer_slot;
            attribute.format = to_sdl(a.format);
            attribute.offset = a.offset;
            attributes.push_back(attribute);
        }

        SDL_GPUGraphicsPipelineCreateInfo info = {};
        info.vertex_shader = vs;
        info.fragment_shader = fs;
        info.primitive_type = to_sdl(d.primitive);
        info.vertex_input_state.vertex_buffer_descriptions = buffers.empty() ? nullptr : buffers.data();
        info.vertex_input_state.num_vertex_buffers = uint32_t(buffers.size());
        info.vertex_input_state.vertex_attributes = attributes.empty() ? nullptr : attributes.data();
        info.vertex_input_state.num_vertex_attributes = uint32_t(attributes.size());
        info.rasterizer_state.fill_mode = d.fill == Fill::Wireframe ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = to_sdl(d.cull);
        info.rasterizer_state.front_face = d.front_face == FrontFace::Clockwise ? SDL_GPU_FRONTFACE_CLOCKWISE
                                                                                : SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        info.rasterizer_state.enable_depth_clip = true;
        info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        info.depth_stencil_state.enable_depth_test = d.depth_test;
        info.depth_stencil_state.enable_depth_write = d.depth_write;
        info.depth_stencil_state.compare_op = to_sdl(d.depth_compare);
        const bool has_color = d.color_format != Format::Invalid;
        info.target_info.color_target_descriptions = has_color ? &color_target : nullptr;
        info.target_info.num_color_targets = has_color ? 1 : 0;
        info.target_info.has_depth_stencil_target = d.depth_format != Format::Invalid;
        info.target_info.depth_stencil_format = to_sdl(d.depth_format);

        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(gpu_, &info);
        // Shaders are reference-counted by the pipeline, so ours drop either way.
        SDL_ReleaseGPUShader(gpu_, vs);
        SDL_ReleaseGPUShader(gpu_, fs);
        if (!pipeline) {
            LOG_ERROR("[%s] pipeline creation failed: %s", d.name.c_str(), SDL_GetError());
            return nullptr;
        }
        return reinterpret_cast<Pipeline*>(pipeline);
    }

    void destroy(Pipeline* pipeline) override {
        if (pipeline) SDL_ReleaseGPUGraphicsPipeline(gpu_, sdl(pipeline));
    }

    bool compiles_hlsl() const override {
#ifdef DRAGON_SHADERCROSS
        return true;
#else
        return false;
#endif
    }

    std::vector<std::string> baked_shader_files(const std::string& root, const std::string& stem) const override {
#ifdef DRAGON_SHADERCROSS
        (void)root;
        (void)stem;
        return {};
#else
        std::string dir = root;
        if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';
        const BakedFormat baked = baked_format(gpu_);
        return {dir + stem + ".vertex" + baked.ext, dir + stem + ".fragment" + baked.ext};
#endif
    }

    // ---- passes
    Pass* begin_pass(const PassDesc& desc) override {
        if (!cmd_) return nullptr;
        SDL_GPUColorTargetInfo color = {};
        if (desc.color) {
            color.texture = sdl(desc.color);
            color.clear_color = SDL_FColor{desc.clear_rgba[0], desc.clear_rgba[1], desc.clear_rgba[2], desc.clear_rgba[3]};
            color.load_op = desc.clear_color ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            color.store_op = SDL_GPU_STOREOP_STORE;
            // A cleared target's old contents are dead, so SDL may hand back
            // fresh storage rather than wait for a frame still reading it.
            color.cycle = desc.clear_color;
        }
        SDL_GPUDepthStencilTargetInfo depth = {};
        if (desc.depth) {
            depth.texture = sdl(desc.depth);
            depth.clear_depth = desc.clear_depth_value;
            depth.load_op = desc.clear_depth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            depth.store_op = desc.keep_depth ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
            depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
            depth.cycle = desc.clear_depth;
        }
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd_, desc.color ? &color : nullptr,
                                                         desc.color ? 1 : 0, desc.depth ? &depth : nullptr);
        return reinterpret_cast<Pass*>(pass);
    }

    void end_pass(Pass* pass) override {
        if (pass) SDL_EndGPURenderPass(sdl(pass));
    }

    void bind_pipeline(Pass* pass, Pipeline* pipeline) override {
        SDL_BindGPUGraphicsPipeline(sdl(pass), sdl(pipeline));
    }

    void bind_vertex_buffers(Pass* pass, uint32_t first_slot, const BufferBinding* bindings, uint32_t count) override {
        SDL_GPUBufferBinding native[8] = {};
        for (uint32_t i = 0; i < count && i < 8; ++i) {
            native[i].buffer = sdl(bindings[i].buffer);
            native[i].offset = bindings[i].offset;
        }
        SDL_BindGPUVertexBuffers(sdl(pass), first_slot, native, count < 8 ? count : 8);
    }

    void bind_index_buffer(Pass* pass, const BufferBinding& binding, IndexSize size) override {
        SDL_GPUBufferBinding native = {};
        native.buffer = sdl(binding.buffer);
        native.offset = binding.offset;
        SDL_BindGPUIndexBuffer(sdl(pass), &native,
                               size == IndexSize::U16 ? SDL_GPU_INDEXELEMENTSIZE_16BIT : SDL_GPU_INDEXELEMENTSIZE_32BIT);
    }

    void bind_fragment_textures(Pass* pass, uint32_t first_slot, const TextureBinding* bindings, uint32_t count) override {
        SDL_GPUTextureSamplerBinding native[16] = {};
        for (uint32_t i = 0; i < count && i < 16; ++i) {
            native[i].texture = sdl(bindings[i].texture);
            native[i].sampler = sdl(bindings[i].sampler);
        }
        SDL_BindGPUFragmentSamplers(sdl(pass), first_slot, native, count < 16 ? count : 16);
    }

    void push_uniforms(Stage stage, uint32_t slot, const void* data, uint32_t size) override {
        if (!cmd_) return;
        if (stage == Stage::Vertex) SDL_PushGPUVertexUniformData(cmd_, slot, data, size);
        else SDL_PushGPUFragmentUniformData(cmd_, slot, data, size);
    }

    void draw(Pass* pass, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex,
              uint32_t first_instance) override {
        SDL_DrawGPUPrimitives(sdl(pass), vertex_count, instance_count, first_vertex, first_instance);
    }

    void draw_indexed(Pass* pass, uint32_t index_count, uint32_t instance_count, uint32_t first_index,
                      int32_t vertex_offset, uint32_t first_instance) override {
        SDL_DrawGPUIndexedPrimitives(sdl(pass), index_count, instance_count, first_index, vertex_offset,
                                     first_instance);
    }

private:
    struct Staging {
        SDL_GPUTransferBuffer* transfer = nullptr;
        uint32_t size = 0;
    };

    void submit_and_wait(SDL_GPUCommandBuffer* cmd) {
        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (fence) {
            SDL_WaitForGPUFences(gpu_, true, &fence, 1);
            SDL_ReleaseGPUFence(gpu_, fence);
        }
    }

    // Synchronous, for load-time buffers: submits its own copy and waits so
    // the caller can rely on the data being resident.
    bool upload_now(SDL_GPUBuffer* buffer, const void* data, uint32_t size, const char* debug_name) {
        SDL_GPUTransferBufferCreateInfo transfer_info = {};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = size;
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(gpu_, &transfer_info);
        if (!transfer) {
            LOG_ERROR("SDL_CreateGPUTransferBuffer(%s) failed: %s", debug_name, SDL_GetError());
            return false;
        }
        void* mapped = SDL_MapGPUTransferBuffer(gpu_, transfer, false);
        if (!mapped) {
            LOG_ERROR("SDL_MapGPUTransferBuffer(%s) failed: %s", debug_name, SDL_GetError());
            SDL_ReleaseGPUTransferBuffer(gpu_, transfer);
            return false;
        }
        std::memcpy(mapped, data, size);
        SDL_UnmapGPUTransferBuffer(gpu_, transfer);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu_);
        SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation src = {};
        src.transfer_buffer = transfer;
        SDL_GPUBufferRegion dst = {};
        dst.buffer = buffer;
        dst.size = size;
        SDL_UploadToGPUBuffer(pass, &src, &dst, false);
        SDL_EndGPUCopyPass(pass);
        submit_and_wait(cmd);
        SDL_ReleaseGPUTransferBuffer(gpu_, transfer);
        return true;
    }

    // The screenshot path: copies `source` to the CPU in this frame's command
    // buffer, so the submit becomes a blocking one.
    void download(Texture* source, uint32_t w, uint32_t h, std::vector<uint8_t>& out) {
        const uint32_t size = w * h * 4;
        SDL_GPUTransferBufferCreateInfo info = {};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        info.size = size;
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(gpu_, &info);
        if (!transfer) {
            LOG_ERROR("SDL_CreateGPUTransferBuffer(screenshot) failed: %s", SDL_GetError());
            abandon_frame();
            return;
        }
        SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd_);
        SDL_GPUTextureRegion src = {};
        src.texture = sdl(source);
        src.w = w;
        src.h = h;
        src.d = 1;
        SDL_GPUTextureTransferInfo dst = {};
        dst.transfer_buffer = transfer;
        dst.pixels_per_row = w;
        dst.rows_per_layer = h;
        SDL_DownloadFromGPUTexture(pass, &src, &dst);
        SDL_EndGPUCopyPass(pass);

        submit_and_wait(cmd_);
        cmd_ = nullptr;
        swapchain_ = nullptr;

        void* pixels = SDL_MapGPUTransferBuffer(gpu_, transfer, false);
        if (pixels) {
            out.assign(static_cast<const uint8_t*>(pixels), static_cast<const uint8_t*>(pixels) + size);
            SDL_UnmapGPUTransferBuffer(gpu_, transfer);
        }
        SDL_ReleaseGPUTransferBuffer(gpu_, transfer);
    }

    SDL_Window* window_ = nullptr;
    SDL_GPUDevice* gpu_ = nullptr;
    bool headless_ = false;
    bool claimed_ = false;
    SDL_GPUCommandBuffer* cmd_ = nullptr;
    SDL_GPUTexture* swapchain_ = nullptr;
    uint32_t swapchain_w_ = 0, swapchain_h_ = 0;
    std::unordered_map<Buffer*, Staging> staging_;
};

}  // namespace

std::unique_ptr<Device> Device::create(SDL_Window* window, const DeviceConfig& config) {
    auto device = std::make_unique<SdlGpuDevice>();
    if (!device->init(window, config)) return nullptr;
    return device;
}

namespace sdlgpu {

SDL_GPUDevice* native_device(Device& device) { return static_cast<SdlGpuDevice&>(device).gpu(); }
SDL_GPUCommandBuffer* native_command_buffer(Device& device) { return static_cast<SdlGpuDevice&>(device).cmd(); }
SDL_GPURenderPass* native_pass(Pass* pass) { return sdl(pass); }
SDL_GPUTextureFormat native_format(Format format) { return to_sdl(format); }

}  // namespace sdlgpu
}  // namespace rhi
