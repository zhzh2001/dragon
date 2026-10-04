#include "rhi/d3d11/d3d11_backend.h"

#include <SDL3/SDL.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/log.h"

namespace rhi {
namespace d3d11 {
namespace {

// ---- resources

struct D11Buffer {
    BufferUsage usage = BufferUsage::Vertex;
    uint32_t size = 0;
    bool dynamic = false;  // per-frame data: D3D11_USAGE_DYNAMIC, mapped with DISCARD
    ID3D11Buffer* buffer = nullptr;
};

struct D11Texture {
    Format format = Format::Invalid;
    uint32_t width = 0, height = 0, levels = 1;
    uint8_t usage = 0;
    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;  // sampled
    ID3D11RenderTargetView* rtv = nullptr;    // a colour target
    ID3D11DepthStencilView* dsv = nullptr;    // a depth target
    bool generates_mips = false;              // created with D3D11_RESOURCE_MISC_GENERATE_MIPS
};

struct D11Sampler {
    ID3D11SamplerState* state = nullptr;
};

struct D11Pipeline {
    PipelineDesc desc;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* layout = nullptr;  // null for a pipeline fed by SV_VertexID alone
    ID3D11RasterizerState* rasterizer = nullptr;
    ID3D11DepthStencilState* depth = nullptr;
    ID3D11BlendState* blend = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    // A vertex buffer binds with its stride, which is the pipeline's.
    uint32_t stride[8] = {};
};

struct D11Pass {
    uint32_t width = 0, height = 0;
};

D11Buffer* d11(Buffer* b) { return reinterpret_cast<D11Buffer*>(b); }
D11Texture* d11(Texture* t) { return reinterpret_cast<D11Texture*>(t); }
D11Sampler* d11(Sampler* s) { return reinterpret_cast<D11Sampler*>(s); }
D11Pipeline* d11(Pipeline* p) { return reinterpret_cast<D11Pipeline*>(p); }

template <typename T>
void release(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

std::string read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

// The baked file name for a define set: the SDL backend's rule (each define
// lowercased, sorted, after a dot), as tools/d3d11/bake_d3d11.py writes it.
std::string baked_base(const ShaderSource& src) {
    std::vector<std::string> names = src.defines;
    std::sort(names.begin(), names.end());
    std::string root = src.root;
    if (!root.empty() && root.back() != '/' && root.back() != '\\') root += '/';
    std::string base = root + src.stem;
    for (std::string name : names) {
        for (char& c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
        base += "." + name;
    }
    return base;
}

bool is_block(Format f) { return f == Format::BC1 || f == Format::BC1_SRGB || f == Format::BC3 || f == Format::BC3_SRGB; }
bool is_depth(Format f) { return f == Format::D16 || f == Format::D24 || f == Format::D32F; }

// A depth texture is created typeless so it can be a depth target and a
// shader resource at once; its two views name the two typed readings.
DXGI_FORMAT to_dxgi(Format f) {
    switch (f) {
        case Format::RGBA8: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case Format::RGBA8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case Format::RGBA16F: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case Format::BC1: return DXGI_FORMAT_BC1_UNORM;
        case Format::BC1_SRGB: return DXGI_FORMAT_BC1_UNORM_SRGB;
        case Format::BC3: return DXGI_FORMAT_BC3_UNORM;
        case Format::BC3_SRGB: return DXGI_FORMAT_BC3_UNORM_SRGB;
        case Format::D16: return DXGI_FORMAT_R16_TYPELESS;
        case Format::D24: return DXGI_FORMAT_R24G8_TYPELESS;
        case Format::D32F: return DXGI_FORMAT_R32_TYPELESS;
        case Format::Invalid: break;
    }
    return DXGI_FORMAT_UNKNOWN;
}
DXGI_FORMAT depth_view_format(Format f) {
    switch (f) {
        case Format::D16: return DXGI_FORMAT_D16_UNORM;
        case Format::D24: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        default: return DXGI_FORMAT_D32_FLOAT;
    }
}
DXGI_FORMAT depth_sampled_format(Format f) {
    switch (f) {
        case Format::D16: return DXGI_FORMAT_R16_UNORM;
        case Format::D24: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        default: return DXGI_FORMAT_R32_FLOAT;
    }
}

DXGI_FORMAT to_dxgi(VertexFormat f) {
    switch (f) {
        case VertexFormat::Float: return DXGI_FORMAT_R32_FLOAT;
        case VertexFormat::Float2: return DXGI_FORMAT_R32G32_FLOAT;
        case VertexFormat::Float3: return DXGI_FORMAT_R32G32B32_FLOAT;
        case VertexFormat::Float4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case VertexFormat::UByte4: return DXGI_FORMAT_R8G8B8A8_UINT;
        case VertexFormat::UByte4Norm: return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    return DXGI_FORMAT_UNKNOWN;
}

D3D11_COMPARISON_FUNC to_d3d(Compare c) {
    switch (c) {
        case Compare::Never: return D3D11_COMPARISON_NEVER;
        case Compare::Less: return D3D11_COMPARISON_LESS;
        case Compare::Equal: return D3D11_COMPARISON_EQUAL;
        case Compare::LessEqual: return D3D11_COMPARISON_LESS_EQUAL;
        case Compare::Greater: return D3D11_COMPARISON_GREATER;
        case Compare::NotEqual: return D3D11_COMPARISON_NOT_EQUAL;
        case Compare::GreaterEqual: return D3D11_COMPARISON_GREATER_EQUAL;
        case Compare::Always: return D3D11_COMPARISON_ALWAYS;
    }
    return D3D11_COMPARISON_ALWAYS;
}

D3D11_TEXTURE_ADDRESS_MODE to_d3d(Address a) {
    switch (a) {
        case Address::Repeat: return D3D11_TEXTURE_ADDRESS_WRAP;
        case Address::Clamp: return D3D11_TEXTURE_ADDRESS_CLAMP;
        case Address::Mirror: return D3D11_TEXTURE_ADDRESS_MIRROR;
    }
    return D3D11_TEXTURE_ADDRESS_WRAP;
}

D3D11_PRIMITIVE_TOPOLOGY to_d3d(Primitive p) {
    switch (p) {
        case Primitive::TriangleList: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case Primitive::TriangleStrip: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case Primitive::LineList: return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
        case Primitive::LineStrip: return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case Primitive::PointList: return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
    }
    return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

const char* feature_level_name(D3D_FEATURE_LEVEL level) {
    switch (level) {
        case D3D_FEATURE_LEVEL_11_1: return "11_1";
        case D3D_FEATURE_LEVEL_11_0: return "11_0";
        case D3D_FEATURE_LEVEL_10_1: return "10_1";
        case D3D_FEATURE_LEVEL_10_0: return "10_0";
        case D3D_FEATURE_LEVEL_9_3: return "9_3";
        case D3D_FEATURE_LEVEL_9_2: return "9_2";
        case D3D_FEATURE_LEVEL_9_1: return "9_1";
        default: return "?";
    }
}

// The sizes a pushed uniform block may be, one dynamic constant buffer
// each per stage and slot: a block is mapped with DISCARD into the smallest
// that fits, so the driver renames a few hundred bytes, not 64 KB, for the
// small blocks pushed thousands of times a frame.
constexpr uint32_t UNIFORM_CLASSES[] = {256, 1024, 4096, 16384, 65536};
constexpr int UNIFORM_CLASS_COUNT = 5;
constexpr uint32_t UNIFORM_SLOTS = 4;

}  // namespace

class D3D11Device final : public Device {
public:
    bool init(SDL_Window* window, const DeviceConfig& config) {
        headless_ = config.headless;
        hwnd_ = static_cast<HWND>(
            SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (!hwnd_) {
            LOG_ERROR("direct3d11: the window has no HWND");
            return false;
        }
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory_)))) {
            LOG_ERROR("direct3d11: CreateDXGIFactory1 failed");
            return false;
        }
        // DRAGON_D3D11_WARP=1: Microsoft's software rasteriser, every
        // Windows has it. Otherwise DXGI adapter N (--gpu-driver
        // direct3d11:N), in DXGI's order.
        const bool warp = SDL_getenv("DRAGON_D3D11_WARP") && SDL_getenv("DRAGON_D3D11_WARP")[0] == '1';
        IDXGIAdapter1* adapter = nullptr;
        if (!warp && FAILED(factory_->EnumAdapters1(config.adapter, &adapter))) {
            LOG_ERROR("direct3d11: no DXGI adapter %u", config.adapter);
            return false;
        }
        // The feature levels asked for, highest first; DRAGON_D3D11_FEATURE_LEVEL
        // (11_0, 10_1, 10_0) caps the list, which is how the 10_0 path --
        // vs/ps_4_0, 8192-texel textures -- is tested on a card that has 11.
        std::vector<D3D_FEATURE_LEVEL> levels = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                                 D3D_FEATURE_LEVEL_10_0};
        if (const char* cap = SDL_getenv("DRAGON_D3D11_FEATURE_LEVEL")) {
            while (levels.size() > 1 && std::strcmp(feature_level_name(levels.front()), cap) != 0) levels.erase(levels.begin());
        }
        const D3D_DRIVER_TYPE type = warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_UNKNOWN;
        UINT flags = config.debug ? D3D11_CREATE_DEVICE_DEBUG : 0;
        HRESULT hr = E_FAIL;
        for (int attempt = 0; attempt < 4 && FAILED(hr); ++attempt) {
            hr = D3D11CreateDevice(adapter, type, nullptr, flags, levels.data(), UINT(levels.size()), D3D11_SDK_VERSION,
                                   &device_, &level_, &context_);
            // A runtime older than 11.1 rejects the whole list for naming
            // 11_1 (E_INVALIDARG); a machine without the SDK layers rejects
            // the debug flag. Drop each and try again.
            if (hr == E_INVALIDARG && levels.front() == D3D_FEATURE_LEVEL_11_1 && levels.size() > 1) {
                levels.erase(levels.begin());
            } else if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
                flags &= ~UINT(D3D11_CREATE_DEVICE_DEBUG);
            } else if (FAILED(hr)) {
                break;
            }
        }
        if (FAILED(hr)) {
            LOG_ERROR("direct3d11: D3D11CreateDevice failed (0x%08lx)", static_cast<unsigned long>(hr));
            release(adapter);
            return false;
        }
        debug_layer_ = (flags & D3D11_CREATE_DEVICE_DEBUG) != 0;
        {
            DXGI_ADAPTER_DESC desc = {};
            IDXGIDevice* dxgi_device = nullptr;
            IDXGIAdapter* used = adapter;
            if (!used && SUCCEEDED(device_->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_device)))) {
                dxgi_device->GetAdapter(&used);
            }
            if (used) used->GetDesc(&desc);
            char name[128] = "?";
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof name, nullptr, nullptr);
            // vs/ps_5_0 at feature level 11, 4_0 at 10: which baked set loads.
            profile_ = level_ >= D3D_FEATURE_LEVEL_11_0 ? "sm50" : "sm40";
            LOG_INFO("direct3d11: %s, feature level %s, %s shaders%s%s", name, feature_level_name(level_), profile_,
                     warp ? " (WARP)" : "", debug_layer_ ? ", debug layer" : "");
            if (used && used != adapter) used->Release();
            release(dxgi_device);
        }
        release(adapter);

        if (!headless_) {
            int w = 0, h = 0;
            SDL_GetWindowSizeInPixels(window, &w, &h);
            DXGI_SWAP_CHAIN_DESC sd = {};
            sd.BufferDesc.Width = UINT(std::max(w, 1));
            sd.BufferDesc.Height = UINT(std::max(h, 1));
            sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.SampleDesc.Count = 1;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            // Two buffers, blitted and discarded: the oldest presentation
            // model, which every runtime from Vista's has. Flip models need
            // Windows 8 or 10 and buy nothing a game drawing its own
            // composite needs.
            sd.BufferCount = 2;
            sd.OutputWindow = hwnd_;
            sd.Windowed = TRUE;
            sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            if (FAILED(factory_->CreateSwapChain(device_, &sd, &swapchain_))) {
                LOG_ERROR("direct3d11: CreateSwapChain failed");
                return false;
            }
            factory_->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
            swap_width_ = sd.BufferDesc.Width;
            swap_height_ = sd.BufferDesc.Height;
            if (!make_backbuffer_view()) return false;
        }

        // The dynamic constant buffers push_uniforms maps into.
        for (int stage = 0; stage < 2; ++stage) {
            for (uint32_t slot = 0; slot < UNIFORM_SLOTS; ++slot) {
                for (int c = 0; c < UNIFORM_CLASS_COUNT; ++c) {
                    D3D11_BUFFER_DESC bd = {};
                    bd.ByteWidth = UNIFORM_CLASSES[c];
                    bd.Usage = D3D11_USAGE_DYNAMIC;
                    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                    if (FAILED(device_->CreateBuffer(&bd, nullptr, &uniforms_[stage][slot][c]))) {
                        LOG_ERROR("direct3d11: constant buffer creation failed");
                        return false;
                    }
                }
            }
        }
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        device_->CreateSamplerState(&sd, &blit_sampler_);
        return true;
    }

    ~D3D11Device() override {
        if (context_) context_->ClearState();
        release(blit_sampler_);
        release(blit_.vs);
        release(blit_.ps);
        release(blit_.rasterizer);
        release(blit_.depth);
        release(blit_.blend);
        for (auto& stage : uniforms_)
            for (auto& slot : stage)
                for (ID3D11Buffer*& b : slot) release(b);
        release(backbuffer_rtv_);
        release(swapchain_);
        release(context_);
        release(device_);
        release(factory_);
    }

    ID3D11Device* device() const { return device_; }
    ID3D11DeviceContext* context() const { return context_; }

    Backend backend() const override { return Backend::Direct3D11; }
    const char* driver_name() const override { return "direct3d11"; }
    uint32_t max_texture_size() const override {
        return level_ >= D3D_FEATURE_LEVEL_11_0 ? 16384 : level_ >= D3D_FEATURE_LEVEL_10_0 ? 8192 : 4096;
    }

    bool supports_format(Format format, uint8_t usage) const override {
        UINT support = 0;
        const DXGI_FORMAT fmt = is_depth(format) ? depth_view_format(format) : to_dxgi(format);
        if (fmt == DXGI_FORMAT_UNKNOWN || FAILED(device_->CheckFormatSupport(fmt, &support))) return false;
        if (!(support & D3D11_FORMAT_SUPPORT_TEXTURE2D)) return false;
        if ((usage & TEXTURE_COLOR_TARGET) && !(support & D3D11_FORMAT_SUPPORT_RENDER_TARGET)) return false;
        if ((usage & TEXTURE_DEPTH_TARGET) && !(support & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL)) return false;
        if ((usage & TEXTURE_SAMPLED) && !is_depth(format) && !(support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE)) return false;
        return true;
    }

    // ---- the frame
    FrameStatus begin_frame(uint32_t* present_width, uint32_t* present_height) override {
        if (headless_) return FrameStatus::Ready;
        RECT rect = {};
        GetClientRect(hwnd_, &rect);
        const uint32_t w = uint32_t(std::max<LONG>(rect.right - rect.left, 0));
        const uint32_t h = uint32_t(std::max<LONG>(rect.bottom - rect.top, 0));
        if (w == 0 || h == 0) return FrameStatus::Skip;
        if (w != swap_width_ || h != swap_height_) {
            // The window changed size: the swap chain follows it.
            context_->OMSetRenderTargets(0, nullptr, nullptr);
            release(backbuffer_rtv_);
            if (FAILED(swapchain_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0)) || !make_backbuffer_view()) {
                LOG_ERROR("direct3d11: ResizeBuffers(%ux%u) failed", w, h);
                return FrameStatus::Failed;
            }
            swap_width_ = w;
            swap_height_ = h;
        }
        if (present_width) *present_width = w;
        if (present_height) *present_height = h;
        return FrameStatus::Ready;
    }

    void end_frame(Texture* source, uint32_t width, uint32_t height, std::vector<uint8_t>* readback) override {
        D11Texture* src = source ? d11(source) : nullptr;
        if (src && !headless_ && backbuffer_rtv_) {
            stretch(src, width, height, backbuffer_rtv_, swap_width_, swap_height_);
        }
        if (readback && src) download(src, width, height, *readback);
        if (!headless_) {
            swapchain_->Present(1, 0);
        } else {
            wait_idle();
        }
    }

    void blit(Texture* source, uint32_t sw, uint32_t sh, Texture* dest, uint32_t dw, uint32_t dh) override {
        D11Texture* s = source ? d11(source) : nullptr;
        D11Texture* d = dest ? d11(dest) : nullptr;
        if (!s || !d || !s->srv || !d->rtv) return;
        stretch(s, sw, sh, d->rtv, dw, dh);
    }

    void wait_idle() override {
        ID3D11Query* query = nullptr;
        D3D11_QUERY_DESC qd = {D3D11_QUERY_EVENT, 0};
        if (FAILED(device_->CreateQuery(&qd, &query))) return;
        context_->End(query);
        context_->Flush();
        BOOL done = FALSE;
        while (context_->GetData(query, &done, sizeof done, 0) == S_FALSE || !done) {
            if (done) break;
            SwitchToThread();
        }
        query->Release();
    }

    // ---- buffers
    Buffer* create_buffer(BufferUsage usage, uint32_t size, const void* data, const char* debug_name) override {
        if (size == 0) return nullptr;
        auto* b = new D11Buffer;
        b->usage = usage;
        b->size = size;
        // No data now means per-frame data later (map_upload): dynamic.
        b->dynamic = data == nullptr;
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = size;
        bd.Usage = b->dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = usage == BufferUsage::Index ? D3D11_BIND_INDEX_BUFFER : D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = b->dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
        D3D11_SUBRESOURCE_DATA init = {data, 0, 0};
        if (FAILED(device_->CreateBuffer(&bd, data ? &init : nullptr, &b->buffer))) {
            LOG_ERROR("direct3d11: CreateBuffer(%s, %u bytes) failed", debug_name, size);
            delete b;
            return nullptr;
        }
        name_object(b->buffer, debug_name);
        track(b, Kind::Buffer, size);
        return reinterpret_cast<Buffer*>(b);
    }

    void destroy(Buffer* buffer) override {
        if (!buffer) return;
        D11Buffer* b = d11(buffer);
        for (BufferBinding& s : streams_)
            if (s.buffer == buffer) s = {};
        untrack(b);
        release(b->buffer);
        delete b;
    }

    void* map_upload(Buffer* buffer, uint32_t size) override {
        if (!buffer || size == 0) return nullptr;
        D11Buffer* b = d11(buffer);
        if (!b->dynamic) return nullptr;
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(context_->Map(b->buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return nullptr;
        return mapped.pData;
    }

    void commit_upload(Buffer* buffer, uint32_t) override {
        if (!buffer) return;
        D11Buffer* b = d11(buffer);
        if (b->dynamic) context_->Unmap(b->buffer, 0);
    }

    // ---- textures
    Texture* create_texture(const TextureDesc& desc, const char* debug_name) override {
        auto* t = new D11Texture;
        t->format = desc.format;
        t->width = desc.width;
        t->height = desc.height;
        t->levels = std::max(desc.mip_levels, 1u);
        t->usage = desc.usage;
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = desc.width;
        td.Height = desc.height;
        td.MipLevels = t->levels;
        td.ArraySize = 1;
        td.Format = to_dxgi(desc.format);
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        if (desc.usage & TEXTURE_SAMPLED) td.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
        if (desc.usage & TEXTURE_COLOR_TARGET) td.BindFlags |= D3D11_BIND_RENDER_TARGET;
        if (desc.usage & TEXTURE_DEPTH_TARGET) td.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
        // A mip chain the GPU builds (upload_texture's generate_mips) needs
        // the texture renderable and flagged; block formats build theirs on
        // the CPU (upload_texture_level), and are not renderable anyway.
        if ((desc.usage & TEXTURE_SAMPLED) && t->levels > 1 && !is_block(desc.format) && !is_depth(desc.format)) {
            td.BindFlags |= D3D11_BIND_RENDER_TARGET;
            td.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
            t->generates_mips = true;
        }
        HRESULT hr = device_->CreateTexture2D(&td, nullptr, &t->texture);
        if (SUCCEEDED(hr) && (desc.usage & TEXTURE_SAMPLED)) {
            D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
            sd.Format = is_depth(desc.format) ? depth_sampled_format(desc.format) : td.Format;
            sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            sd.Texture2D.MipLevels = t->levels;
            hr = device_->CreateShaderResourceView(t->texture, &sd, &t->srv);
        }
        if (SUCCEEDED(hr) && (desc.usage & TEXTURE_COLOR_TARGET)) {
            hr = device_->CreateRenderTargetView(t->texture, nullptr, &t->rtv);
        }
        if (SUCCEEDED(hr) && (desc.usage & TEXTURE_DEPTH_TARGET)) {
            D3D11_DEPTH_STENCIL_VIEW_DESC dd = {};
            dd.Format = depth_view_format(desc.format);
            dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            hr = device_->CreateDepthStencilView(t->texture, &dd, &t->dsv);
        }
        if (FAILED(hr)) {
            LOG_ERROR("direct3d11: texture '%s' (%ux%u, %u levels) failed (0x%08lx)", debug_name, desc.width,
                      desc.height, t->levels, static_cast<unsigned long>(hr));
            destroy(reinterpret_cast<Texture*>(t));
            return nullptr;
        }
        name_object(t->texture, debug_name);
        track(t, Kind::Texture, texture_bytes(desc));
        return reinterpret_cast<Texture*>(t);
    }

    bool upload_texture(Texture* texture, const void* pixels, uint32_t width, uint32_t height,
                        bool generate_mips) override {
        D11Texture* t = d11(texture);
        if (!t || !t->texture || !pixels) return false;
        (void)height;  // the rows are tightly packed; UpdateSubresource reads the level's own height
        context_->UpdateSubresource(t->texture, 0, nullptr, pixels, width * 4, 0);
        if (generate_mips && t->levels > 1) {
            if (!t->generates_mips || !t->srv) return false;
            context_->GenerateMips(t->srv);
        }
        return true;
    }

    bool upload_texture_level(Texture* texture, uint32_t level, const void* data, uint32_t bytes, uint32_t width,
                              uint32_t height) override {
        D11Texture* t = d11(texture);
        if (!t || !t->texture || !data || level >= t->levels) return false;
        uint32_t pitch = width * 4;
        if (is_block(t->format)) {
            const uint32_t block_bytes = (t->format == Format::BC1 || t->format == Format::BC1_SRGB) ? 8 : 16;
            pitch = std::max(1u, (width + 3) / 4) * block_bytes;
            if (uint64_t(pitch) * std::max(1u, (height + 3) / 4) > bytes) return false;
        } else if (t->format == Format::RGBA16F) {
            pitch = width * 8;
        }
        context_->UpdateSubresource(t->texture, level, nullptr, data, pitch, 0);
        return true;
    }

    void destroy(Texture* texture) override {
        if (!texture) return;
        D11Texture* t = d11(texture);
        untrack(t);
        release(t->srv);
        release(t->rtv);
        release(t->dsv);
        release(t->texture);
        delete t;
    }

    Sampler* create_sampler(const SamplerDesc& desc) override {
        auto* s = new D11Sampler;
        D3D11_SAMPLER_DESC sd = {};
        if (desc.max_anisotropy > 1.0f) {
            sd.Filter = D3D11_FILTER_ANISOTROPIC;
        } else {
            // D3D11_FILTER packs the three choices: min at bit 4, mag at 2,
            // mip at 0, linear = 1.
            const UINT min = desc.min_filter == Filter::Linear ? 1 : 0;
            const UINT mag = desc.mag_filter == Filter::Linear ? 1 : 0;
            const UINT mip = desc.mip_mode == MipMode::Linear ? 1 : 0;
            sd.Filter = D3D11_FILTER((min << 4) | (mag << 2) | mip);
        }
        sd.AddressU = to_d3d(desc.address_u);
        sd.AddressV = to_d3d(desc.address_v);
        sd.AddressW = to_d3d(desc.address_w);
        sd.MaxAnisotropy = UINT(std::clamp(desc.max_anisotropy, 1.0f, 16.0f));
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MinLOD = 0.0f;
        sd.MaxLOD = desc.max_lod >= 1000.0f ? D3D11_FLOAT32_MAX : desc.max_lod;
        if (FAILED(device_->CreateSamplerState(&sd, &s->state))) {
            LOG_ERROR("direct3d11: CreateSamplerState failed");
            delete s;
            return nullptr;
        }
        return reinterpret_cast<Sampler*>(s);
    }

    void destroy(Sampler* sampler) override {
        if (!sampler) return;
        release(d11(sampler)->state);
        delete d11(sampler);
    }

    // ---- pipelines
    Pipeline* create_pipeline(const PipelineDesc& d, const ShaderSource& shaders) override {
        if (shader_root_.empty()) shader_root_ = shaders.root;
        const std::string base = baked_base(shaders);
        const std::string vs_code = read_file(base + ".vertex." + profile_ + ".dxbc");
        const std::string ps_code = read_file(base + ".fragment." + profile_ + ".dxbc");
        if (vs_code.empty() || ps_code.empty()) {
            LOG_ERROR("[%s] no D3D11 shader at %s.{vertex,fragment}.%s.dxbc (tools/d3d11/bake_d3d11.py)", d.name.c_str(),
                      base.c_str(), profile_);
            return nullptr;
        }
        auto* p = new D11Pipeline;
        p->desc = d;
        if (!build_pipeline(*p, vs_code, ps_code)) {
            destroy(reinterpret_cast<Pipeline*>(p));
            return nullptr;
        }
        track(p, Kind::Pipeline, 0);
        return reinterpret_cast<Pipeline*>(p);
    }

    void destroy(Pipeline* pipeline) override {
        if (!pipeline) return;
        D11Pipeline* p = d11(pipeline);
        if (pipeline_ == p) pipeline_ = nullptr;
        untrack(p);
        release_pipeline(*p);
        delete p;
    }

    bool compiles_hlsl() const override { return false; }

    std::vector<std::string> baked_shader_files(const ShaderSource& shaders) const override {
        const std::string base = baked_base(shaders);
        return {base + ".vertex." + profile_ + ".dxbc", base + ".fragment." + profile_ + ".dxbc"};
    }

    // ---- passes
    Pass* begin_pass(const PassDesc& desc) override {
        D11Texture* color = desc.color ? d11(desc.color) : nullptr;
        D11Texture* depth = desc.depth ? d11(desc.depth) : nullptr;
        D11Texture* target = color ? color : depth;
        if (!target || (color && !color->rtv) || (depth && !depth->dsv)) {
            LOG_ERROR("direct3d11: a pass with no target it can draw into");
            return nullptr;
        }
        // Nothing this pass may render into stays bound for sampling: D3D11
        // would unbind the conflicting view itself, with a warning a frame.
        ID3D11ShaderResourceView* const none[16] = {};
        context_->PSSetShaderResources(0, 16, none);
        ID3D11RenderTargetView* rtv = color ? color->rtv : nullptr;
        context_->OMSetRenderTargets(rtv ? 1 : 0, rtv ? &rtv : nullptr, depth ? depth->dsv : nullptr);
        pass_.width = target->width;
        pass_.height = target->height;
        const D3D11_VIEWPORT viewport = {0.0f, 0.0f, float(pass_.width), float(pass_.height), 0.0f, 1.0f};
        context_->RSSetViewports(1, &viewport);
        if (color && desc.clear_color) context_->ClearRenderTargetView(color->rtv, desc.clear_rgba);
        if (depth && desc.clear_depth) context_->ClearDepthStencilView(depth->dsv, D3D11_CLEAR_DEPTH, desc.clear_depth_value, 0);
        // A new pass: the constant buffers pushed so far bind again (the
        // stretch between passes borrows a slot).
        for (int stage = 0; stage < 2; ++stage)
            for (uint32_t slot = 0; slot < UNIFORM_SLOTS; ++slot)
                if (bound_uniforms_[stage][slot]) set_constant_buffer(stage, slot, bound_uniforms_[stage][slot]);
        pipeline_ = nullptr;
        return reinterpret_cast<Pass*>(&pass_);
    }

    void end_pass(Pass*) override {}

    void bind_pipeline(Pass*, Pipeline* pipeline) override {
        D11Pipeline* p = d11(pipeline);
        if (!p || p == pipeline_) return;
        pipeline_ = p;
        apply_pipeline(*p);
        // A vertex buffer binds with its stride, which is this pipeline's, so
        // buffers bound before it are bound again.
        for (uint32_t slot = 0; slot < 8; ++slot) {
            if (!p->stride[slot] || !streams_[slot].buffer) continue;
            ID3D11Buffer* b = d11(streams_[slot].buffer)->buffer;
            const UINT stride = p->stride[slot], offset = streams_[slot].offset;
            context_->IASetVertexBuffers(slot, 1, &b, &stride, &offset);
        }
    }

    void bind_vertex_buffers(Pass*, uint32_t first_slot, const BufferBinding* bindings, uint32_t count) override {
        for (uint32_t i = 0; i < count && first_slot + i < 8; ++i) {
            const uint32_t slot = first_slot + i;
            streams_[slot] = bindings[i];
            ID3D11Buffer* b = bindings[i].buffer ? d11(bindings[i].buffer)->buffer : nullptr;
            const UINT stride = pipeline_ ? pipeline_->stride[slot] : 0, offset = bindings[i].offset;
            context_->IASetVertexBuffers(slot, 1, &b, &stride, &offset);
        }
    }

    void bind_index_buffer(Pass*, const BufferBinding& binding, IndexSize size) override {
        ID3D11Buffer* b = binding.buffer ? d11(binding.buffer)->buffer : nullptr;
        context_->IASetIndexBuffer(b, size == IndexSize::U32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, binding.offset);
    }

    void bind_fragment_textures(Pass*, uint32_t first_slot, const TextureBinding* bindings, uint32_t count) override {
        ID3D11ShaderResourceView* views[16] = {};
        ID3D11SamplerState* samplers[16] = {};
        count = std::min(count, 16u - std::min(first_slot, 16u));
        for (uint32_t i = 0; i < count; ++i) {
            views[i] = bindings[i].texture ? d11(bindings[i].texture)->srv : nullptr;
            samplers[i] = bindings[i].sampler ? d11(bindings[i].sampler)->state : nullptr;
        }
        if (!count) return;
        context_->PSSetShaderResources(first_slot, count, views);
        context_->PSSetSamplers(first_slot, count, samplers);
    }

    void push_uniforms(Stage stage, uint32_t slot, const void* data, uint32_t size) override {
        if (slot >= UNIFORM_SLOTS || size == 0) return;
        const int s = stage == Stage::Vertex ? 0 : 1;
        int c = 0;
        while (c + 1 < UNIFORM_CLASS_COUNT && UNIFORM_CLASSES[c] < size) ++c;
        if (size > UNIFORM_CLASSES[c]) {
            LOG_ERROR("direct3d11: a %u-byte uniform block, past the 64 KB a constant buffer holds", size);
            return;
        }
        ID3D11Buffer* buffer = uniforms_[s][slot][c];
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(context_->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
        std::memcpy(mapped.pData, data, size);
        context_->Unmap(buffer, 0);
        bound_uniforms_[s][slot] = buffer;
        set_constant_buffer(s, slot, buffer);
    }

    void draw(Pass*, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex,
              uint32_t first_instance) override {
        if (!pipeline_ || !vertex_count) return;
        context_->DrawInstanced(vertex_count, std::max(instance_count, 1u), first_vertex, first_instance);
    }

    void draw_indexed(Pass*, uint32_t index_count, uint32_t instance_count, uint32_t first_index,
                      int32_t vertex_offset, uint32_t first_instance) override {
        if (!pipeline_ || !index_count) return;
        context_->DrawIndexedInstanced(index_count, std::max(instance_count, 1u), first_index, vertex_offset, first_instance);
    }

private:
    bool make_backbuffer_view() {
        ID3D11Texture2D* back = nullptr;
        if (FAILED(swapchain_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) return false;
        const HRESULT hr = device_->CreateRenderTargetView(back, nullptr, &backbuffer_rtv_);
        back->Release();
        return SUCCEEDED(hr);
    }

    void name_object(ID3D11DeviceChild* object, const char* name) {
        if (!object || !name || !debug_layer_) return;
        object->SetPrivateData(WKPDID_D3DDebugObjectName, UINT(std::strlen(name)), name);
    }

    void set_constant_buffer(int stage, uint32_t slot, ID3D11Buffer* buffer) {
        if (stage == 0) context_->VSSetConstantBuffers(slot, 1, &buffer);
        else context_->PSSetConstantBuffers(slot, 1, &buffer);
    }

    bool build_pipeline(D11Pipeline& p, const std::string& vs_code, const std::string& ps_code) {
        const PipelineDesc& d = p.desc;
        if (FAILED(device_->CreateVertexShader(vs_code.data(), vs_code.size(), nullptr, &p.vs)) ||
            FAILED(device_->CreatePixelShader(ps_code.data(), ps_code.size(), nullptr, &p.ps))) {
            LOG_ERROR("[%s] the device rejected the D3D11 bytecode", d.name.c_str());
            return false;
        }
        name_object(p.vs, d.name.c_str());
        name_object(p.ps, d.name.c_str());
        for (const VertexBufferLayout& b : d.vertex_buffers)
            if (b.slot < 8) p.stride[b.slot] = b.pitch;
        if (!d.vertex_attributes.empty()) {
            // SPIRV-Cross names every input TEXCOORD<location>.
            std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
            for (const VertexAttribute& a : d.vertex_attributes) {
                bool instanced = false;
                for (const VertexBufferLayout& b : d.vertex_buffers)
                    if (b.slot == a.buffer_slot) instanced = b.rate == InputRate::Instance;
                elements.push_back({"TEXCOORD", a.location, to_dxgi(a.format), a.buffer_slot, a.offset,
                                    instanced ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA,
                                    instanced ? 1u : 0u});
            }
            if (FAILED(device_->CreateInputLayout(elements.data(), UINT(elements.size()), vs_code.data(), vs_code.size(),
                                                  &p.layout))) {
                LOG_ERROR("[%s] input layout rejected: the attributes do not match the vertex shader's inputs",
                          d.name.c_str());
                return false;
            }
        }
        p.topology = to_d3d(d.primitive);

        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode = d.fill == Fill::Wireframe ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
        rd.CullMode = d.cull == Cull::None ? D3D11_CULL_NONE : d.cull == Cull::Front ? D3D11_CULL_FRONT : D3D11_CULL_BACK;
        rd.FrontCounterClockwise = d.front_face == FrontFace::CounterClockwise;
        rd.DepthClipEnable = TRUE;
        if (FAILED(device_->CreateRasterizerState(&rd, &p.rasterizer))) return false;

        D3D11_DEPTH_STENCIL_DESC dd = {};
        dd.DepthEnable = d.depth_test || d.depth_write;
        dd.DepthWriteMask = d.depth_write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
        dd.DepthFunc = d.depth_test ? to_d3d(d.depth_compare) : D3D11_COMPARISON_ALWAYS;
        if (FAILED(device_->CreateDepthStencilState(&dd, &p.depth))) return false;

        D3D11_BLEND_DESC bd = {};
        D3D11_RENDER_TARGET_BLEND_DESC& rt = bd.RenderTarget[0];
        rt.BlendEnable = d.blend != Blend::Opaque;
        rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        if (d.blend == Blend::Additive) {
            rt.SrcBlend = rt.DestBlend = rt.SrcBlendAlpha = rt.DestBlendAlpha = D3D11_BLEND_ONE;
        } else {
            rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
            rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            rt.SrcBlendAlpha = D3D11_BLEND_ONE;
            rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        }
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        return SUCCEEDED(device_->CreateBlendState(&bd, &p.blend));
    }

    void release_pipeline(D11Pipeline& p) {
        release(p.vs);
        release(p.ps);
        release(p.layout);
        release(p.rasterizer);
        release(p.depth);
        release(p.blend);
    }

    void apply_pipeline(const D11Pipeline& p) {
        context_->VSSetShader(p.vs, nullptr, 0);
        context_->PSSetShader(p.ps, nullptr, 0);
        context_->IASetInputLayout(p.layout);
        context_->IASetPrimitiveTopology(p.topology);
        context_->RSSetState(p.rasterizer);
        context_->OMSetDepthStencilState(p.depth, 0);
        context_->OMSetBlendState(p.blend, nullptr, 0xFFFFFFFFu);
    }

    // The stretch (blit, and the present): a fullscreen triangle through
    // shaders/blit.hlsl, baked without a variant suffix.
    bool ensure_blit() {
        if (blit_.vs) return true;
        if (blit_failed_) return false;
        ShaderSource src;
        src.stem = "blit";
        src.root = shader_root_;
        const std::string base = baked_base(src);
        const std::string vs_code = read_file(base + ".vertex." + profile_ + ".dxbc");
        const std::string ps_code = read_file(base + ".fragment." + profile_ + ".dxbc");
        blit_.desc.name = "blit";
        blit_.desc.cull = Cull::None;
        blit_.desc.depth_test = false;
        blit_.desc.depth_write = false;
        if (vs_code.empty() || ps_code.empty() || !build_pipeline(blit_, vs_code, ps_code)) {
            LOG_ERROR("direct3d11: no blit shader at %s.{vertex,fragment}.%s.dxbc; nothing reaches the window", base.c_str(),
                      profile_);
            blit_failed_ = true;
            return false;
        }
        return true;
    }

    void stretch(D11Texture* source, uint32_t sw, uint32_t sh, ID3D11RenderTargetView* dest, uint32_t dw, uint32_t dh) {
        if (!source->srv || !ensure_blit()) return;
        ID3D11ShaderResourceView* const none[16] = {};
        context_->PSSetShaderResources(0, 16, none);
        context_->OMSetRenderTargets(1, &dest, nullptr);
        const D3D11_VIEWPORT viewport = {0.0f, 0.0f, float(dw), float(dh), 0.0f, 1.0f};
        context_->RSSetViewports(1, &viewport);
        apply_pipeline(blit_);
        const float scale[4] = {float(sw) / float(std::max(source->width, 1u)), float(sh) / float(std::max(source->height, 1u)),
                                0.0f, 0.0f};
        // Fragment slot 0, through the same classes as a pushed block; the
        // next pass puts the engine's block back (begin_pass).
        ID3D11Buffer* buffer = uniforms_[1][0][0];
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(context_->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, scale, sizeof scale);
            context_->Unmap(buffer, 0);
        }
        context_->PSSetConstantBuffers(0, 1, &buffer);
        context_->PSSetShaderResources(0, 1, &source->srv);
        context_->PSSetSamplers(0, 1, &blit_sampler_);
        context_->Draw(3, 0);
        context_->PSSetShaderResources(0, 16, none);
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        pipeline_ = nullptr;
    }

    void download(D11Texture* source, uint32_t w, uint32_t h, std::vector<uint8_t>& out) {
        if (!source->texture || (source->format != Format::RGBA8 && source->format != Format::RGBA8_SRGB)) {
            LOG_ERROR("direct3d11: readback wants an RGBA8 source");
            return;
        }
        w = std::min(w, source->width);
        h = std::min(h, source->height);
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = to_dxgi(source->format);
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D* staging = nullptr;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &staging))) return;
        const D3D11_BOX box = {0, 0, 0, w, h, 1};
        context_->CopySubresourceRegion(staging, 0, 0, 0, 0, source->texture, 0, &box);
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(context_->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
            out.resize(size_t(w) * h * 4);
            for (uint32_t y = 0; y < h; ++y) {
                std::memcpy(out.data() + size_t(y) * w * 4, static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch,
                            size_t(w) * 4);
            }
            for (size_t i = 3; i < out.size(); i += 4) out[i] = 255;
            context_->Unmap(staging, 0);
        } else {
            LOG_ERROR("direct3d11: readback failed");
        }
        staging->Release();
    }

    HWND hwnd_ = nullptr;
    bool headless_ = false;
    bool debug_layer_ = false;
    IDXGIFactory1* factory_ = nullptr;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    D3D_FEATURE_LEVEL level_ = D3D_FEATURE_LEVEL_10_0;
    const char* profile_ = "sm50";
    IDXGISwapChain* swapchain_ = nullptr;
    ID3D11RenderTargetView* backbuffer_rtv_ = nullptr;
    uint32_t swap_width_ = 0, swap_height_ = 0;
    std::string shader_root_;  // where the baked shaders are, from the first pipeline
    ID3D11Buffer* uniforms_[2][UNIFORM_SLOTS][UNIFORM_CLASS_COUNT] = {};
    ID3D11Buffer* bound_uniforms_[2][UNIFORM_SLOTS] = {};  // the last pushed block's buffer, per stage and slot
    D11Pass pass_;
    D11Pipeline* pipeline_ = nullptr;
    BufferBinding streams_[8] = {};
    D11Pipeline blit_;
    bool blit_failed_ = false;
    ID3D11SamplerState* blit_sampler_ = nullptr;
};

std::unique_ptr<Device> create(SDL_Window* window, const DeviceConfig& config) {
    auto device = std::make_unique<D3D11Device>();
    if (!device->init(window, config)) return nullptr;
    return device;
}

ID3D11Device* native_device(Device& device) { return static_cast<D3D11Device&>(device).device(); }
ID3D11DeviceContext* native_context(Device& device) { return static_cast<D3D11Device&>(device).context(); }

}  // namespace d3d11
}  // namespace rhi
