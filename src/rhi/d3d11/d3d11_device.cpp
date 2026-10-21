#include "rhi/d3d11/d3d11_backend.h"

#include <SDL3/SDL.h>
#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <map>
#include <tuple>
#include <string>
#include <unordered_map>
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
    // Feature level 9_1 has no 32-bit indices: an index buffer keeps its
    // data here, and a 32-bit draw is rebuilt from it as a 16-bit one
    // (D3D11Device::narrow_indices). `generation` invalidates those copies
    // when the data is uploaded again.
    std::vector<uint8_t> shadow;
    uint32_t generation = 0;
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
    // Feature level 9 cannot sample a depth texture: a sampled depth target
    // gets an R32F colour stand-in the depth-only shaders write z/w into
    // (common.hlsl, DEPTH_ONLY_FRAGMENT under LEVEL9); `srv` is its view.
    ID3D11Texture2D* standin = nullptr;
    ID3D11RenderTargetView* standin_rtv = nullptr;
};

struct D11Sampler {
    ID3D11SamplerState* state = nullptr;
    // The same sampler point-filtered, for the R32F shadow stand-in on a
    // feature level 9 card that cannot filter a 32-bit float format (the
    // X550 cannot; the shaders' PCF still softens it).
    ID3D11SamplerState* point = nullptr;
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
    // Below feature level 9_3 there is no instancing: an instanced slot binds
    // with a stride of 0 at the instance's element and the draw repeats
    // (`looped`); `instance_pitch` is the slot's real pitch.
    bool looped = false;
    bool instanced[8] = {};
    uint32_t instance_pitch[8] = {};
    bool vertex_id = false;  // no attributes at feature level 9: fed from the backend's id buffer
    // Feature level 9: the shader's cbuffers hold only the members it uses
    // (tools/d3d11/bake_d3d11.py, compact_cbuffers), assembled from the
    // pushed blocks through these copies, in float4 registers.
    struct Copy {
        uint32_t slot, src, count, dst;
    };
    std::vector<Copy> copies[2];                 // per stage
    uint32_t compact_regs[2][4] = {};            // per stage and slot: the assembled block's size
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
// lowercased, sorted, after a dot), as tools/d3d11/bake_d3d11.py writes it,
// with LEVEL9 added at feature level 9.
std::string baked_base(const ShaderSource& src, bool level9) {
    std::vector<std::string> names = src.defines;
    if (level9) names.push_back("LEVEL9");
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
        case Format::RGB565:  // D3D9's alone (rhi.h)
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

// Uniform blocks pushed to a stage's slot go into a dynamic constant buffer
// of exactly their size (rounded to 16 bytes), made on first use and kept
// per size: a slot sees one or two struct sizes, the driver renames a few
// hundred bytes for the small blocks pushed thousands of times a frame and
// not 64 KB, and feature level 9 -- which maps every bound buffer whole
// onto its 256 vertex constant registers (32 for pixels) -- is never handed
// more bytes than the shader declared.
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
                                                 D3D_FEATURE_LEVEL_10_0,  D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_2,
                                                 D3D_FEATURE_LEVEL_9_1};
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
        if (debug_layer_) {
            // The validation messages, read back into the log: an SSH session
            // has no debugger to print them to.
            device_->QueryInterface(__uuidof(ID3D11InfoQueue), reinterpret_cast<void**>(&info_queue_));
        }
        // DRAGON_D3D11_TRACE=1 logs every pass and draw, with the validation
        // messages they raise: how a silent death on a feature level 9 card
        // is placed.
        trace_ = SDL_getenv("DRAGON_D3D11_TRACE") && SDL_getenv("DRAGON_D3D11_TRACE")[0] == '1';
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
            // vs/ps_5_0 at feature level 11, 4_0 at 10, the level_9 profiles
            // of a retro tier's LEVEL9 variant below: which baked set loads.
            level9_ = level_ < D3D_FEATURE_LEVEL_10_0;
            instancing_ = level_ >= D3D_FEATURE_LEVEL_9_3;
            // 9_1 also lacks 32-bit indices and a separate alpha blend.
            no_u32_indices_ = level_ == D3D_FEATURE_LEVEL_9_1;
            // One level-9 set: level_9_3's pixel side is still ps_2_x-sized
            // (32 constants, no texlod), so the sm3 tier does not fit it and
            // 9_3 runs the sm2 tier's level_9_1 bytecode like 9_1 and 9_2.
            profile_ = level_ >= D3D_FEATURE_LEVEL_11_0 ? "sm50" : level_ >= D3D_FEATURE_LEVEL_10_0 ? "sm40" : "fl91";
            LOG_INFO("direct3d11: %s, feature level %s, %s shaders%s%s%s", name, feature_level_name(level_), profile_,
                     warp ? " (WARP)" : "", debug_layer_ ? ", debug layer" : "",
                     level9_ ? (instancing_ ? " (level 9: shader model 2/3 limits)" : " (level 9: no instancing)") : "");
            if (level9_) {
                UINT support = 0;
                device_->CheckFormatSupport(DXGI_FORMAT_R32_FLOAT, &support);
                r32f_filterable_ = (support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) != 0;
                LOG_INFO("direct3d11: R32_FLOAT shadow stand-in %s, %s", (support & D3D11_FORMAT_SUPPORT_RENDER_TARGET) ? "renderable" : "NOT a render target here",
                         r32f_filterable_ ? "filtered" : "point-sampled (not filterable)");
                if (!r32f_filterable_ && info_queue_) {
                    // The stand-in is point-sampled where it cannot be
                    // filtered, which the debug layer says makes this
                    // warning ignorable -- and it raises it on every draw.
                    D3D11_MESSAGE_ID deny[] = {D3D11_MESSAGE_ID_DEVICE_DRAW_RESOURCE_FORMAT_SAMPLE_UNSUPPORTED};
                    D3D11_INFO_QUEUE_FILTER filter = {};
                    filter.DenyList.NumIDs = 1;
                    filter.DenyList.pIDList = deny;
                    info_queue_->AddStorageFilterEntries(&filter);
                }
            }
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

        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        device_->CreateSamplerState(&sd, &blit_sampler_);
        if (level9_) {
            // The vertex index a fullscreen triangle reads (VERTEX_ID_INPUT):
            // no SV_VertexID at feature level 9.
            const float ids[6] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth = sizeof ids;
            bd.Usage = D3D11_USAGE_IMMUTABLE;
            bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            const D3D11_SUBRESOURCE_DATA init = {ids, 0, 0};
            if (FAILED(device_->CreateBuffer(&bd, &init, &vertex_ids_))) return false;
        }
        return true;
    }

    ~D3D11Device() override {
        drain_messages("shutdown");
        release(info_queue_);
        if (context_) context_->ClearState();
        release(blit_sampler_);
        release(vertex_ids_);
        release(blit_.vs);
        release(blit_.ps);
        release(blit_.rasterizer);
        release(blit_.depth);
        release(blit_.blend);
        for (auto& stage : uniforms_)
            for (auto& slot : stage)
                for (auto& [size, b] : slot) b->Release();
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
        return level_ >= D3D_FEATURE_LEVEL_11_0 ? 16384 : level_ >= D3D_FEATURE_LEVEL_10_0 ? 8192
               : level_ >= D3D_FEATURE_LEVEL_9_3 ? 4096 : 2048;
    }

    bool supports_format(Format format, uint8_t usage) const override {
        UINT support = 0;
        const DXGI_FORMAT fmt = is_depth(format) ? depth_view_format(format) : to_dxgi(format);
        if (fmt == DXGI_FORMAT_UNKNOWN || FAILED(device_->CheckFormatSupport(fmt, &support))) return false;
        if (!(support & D3D11_FORMAT_SUPPORT_TEXTURE2D)) return false;
        if ((usage & TEXTURE_COLOR_TARGET) && !(support & D3D11_FORMAT_SUPPORT_RENDER_TARGET)) return false;
        if ((usage & TEXTURE_DEPTH_TARGET) && !(support & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL)) return false;
        if ((usage & TEXTURE_SAMPLED) && !is_depth(format) && !(support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE)) return false;
        if ((usage & TEXTURE_SAMPLED) && is_depth(format) && level9_) {
            // Sampled through the R32F stand-in, which 9_1 does not have:
            // the graphics settings then hold the shadows off.
            UINT standin = 0;
            if (FAILED(device_->CheckFormatSupport(DXGI_FORMAT_R32_FLOAT, &standin))) return false;
            const UINT need = D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_RENDER_TARGET;
            if ((standin & need) != need) return false;
        }
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
        drain_messages("end_frame");
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
        if (no_u32_indices_ && usage == BufferUsage::Index) {
            b->shadow.assign(size, 0);
            if (data) std::memcpy(b->shadow.data(), data, size);
        }
        track(b, Kind::Buffer, size);
        return reinterpret_cast<Buffer*>(b);
    }

    void destroy(Buffer* buffer) override {
        if (!buffer) return;
        D11Buffer* b = d11(buffer);
        for (BufferBinding& s : streams_)
            if (s.buffer == buffer) s = {};
        if (index_.buffer == buffer) index_ = {};
        for (auto it = narrowed_.begin(); it != narrowed_.end();) {
            if (std::get<0>(it->first) == b) {
                release(it->second.buffer);
                it = narrowed_.erase(it);
            } else {
                ++it;
            }
        }
        untrack(b);
        release(b->buffer);
        delete b;
    }

    void* map_upload(Buffer* buffer, uint32_t size) override {
        if (!buffer || size == 0) return nullptr;
        D11Buffer* b = d11(buffer);
        if (!b->dynamic) return nullptr;
        if (!b->shadow.empty()) {
            // Written here, copied up at commit (9_1's index shadow).
            ++b->generation;
            return b->shadow.data();
        }
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(context_->Map(b->buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return nullptr;
        return mapped.pData;
    }

    void commit_upload(Buffer* buffer, uint32_t size) override {
        if (!buffer) return;
        D11Buffer* b = d11(buffer);
        if (!b->dynamic) return;
        if (!b->shadow.empty()) {
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (SUCCEEDED(context_->Map(b->buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                std::memcpy(mapped.pData, b->shadow.data(), std::min<size_t>(size ? size : b->size, b->shadow.size()));
                context_->Unmap(b->buffer, 0);
            }
            return;
        }
        context_->Unmap(b->buffer, 0);
    }

    // ---- textures
    Texture* create_texture(const TextureDesc& desc, const char* debug_name) override {
        auto* t = new D11Texture;
        t->format = desc.format;
        // Feature level 9 has no 32-bit float depth; D24 holds the
        // conventional range the retro tiers draw in.
        if (level9_ && t->format == Format::D32F) t->format = Format::D24;
        const bool standin = level9_ && (desc.usage & TEXTURE_DEPTH_TARGET) && (desc.usage & TEXTURE_SAMPLED);
        t->width = desc.width;
        t->height = desc.height;
        t->levels = std::max(desc.mip_levels, 1u);
        t->usage = desc.usage;
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = desc.width;
        td.Height = desc.height;
        td.MipLevels = t->levels;
        td.ArraySize = 1;
        // A depth texture is typeless so it can be sampled too -- except at
        // feature level 9, which has no typeless resources and no sampled
        // depth: there it is the typed depth format, and the stand-in samples.
        td.Format = level9_ && is_depth(t->format) ? depth_view_format(t->format) : to_dxgi(t->format);
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        if ((desc.usage & TEXTURE_SAMPLED) && !standin) td.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
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
        if (SUCCEEDED(hr) && standin) {
            // The R32F colour the depth-only passes write z/w into, sampled
            // in the depth texture's place.
            D3D11_TEXTURE2D_DESC cd = td;
            cd.Format = DXGI_FORMAT_R32_FLOAT;
            cd.MipLevels = 1;
            cd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            cd.MiscFlags = 0;
            hr = device_->CreateTexture2D(&cd, nullptr, &t->standin);
            if (SUCCEEDED(hr)) hr = device_->CreateShaderResourceView(t->standin, nullptr, &t->srv);
            if (SUCCEEDED(hr)) hr = device_->CreateRenderTargetView(t->standin, nullptr, &t->standin_rtv);
        } else if (SUCCEEDED(hr) && (desc.usage & TEXTURE_SAMPLED)) {
            D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
            sd.Format = is_depth(t->format) ? depth_sampled_format(t->format) : td.Format;
            sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            sd.Texture2D.MipLevels = t->levels;
            hr = device_->CreateShaderResourceView(t->texture, &sd, &t->srv);
        }
        if (SUCCEEDED(hr) && (desc.usage & TEXTURE_COLOR_TARGET)) {
            hr = device_->CreateRenderTargetView(t->texture, nullptr, &t->rtv);
        }
        if (SUCCEEDED(hr) && (desc.usage & TEXTURE_DEPTH_TARGET)) {
            D3D11_DEPTH_STENCIL_VIEW_DESC dd = {};
            dd.Format = depth_view_format(t->format);
            dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            hr = device_->CreateDepthStencilView(t->texture, level9_ ? nullptr : &dd, &t->dsv);
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
        context_->UpdateSubresource(t->texture, 0, nullptr, pixels, width * texel_bytes(t->format), 0);
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
        release(t->standin_rtv);
        release(t->standin);
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
        // Feature level 9_1 filters two ways at most; 9_2 and up sixteen.
        sd.MaxAnisotropy = UINT(std::clamp(desc.max_anisotropy, 1.0f, level_ == D3D_FEATURE_LEVEL_9_1 ? 2.0f : 16.0f));
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MinLOD = 0.0f;
        sd.MaxLOD = desc.max_lod >= 1000.0f ? D3D11_FLOAT32_MAX : desc.max_lod;
        if (level9_) {
            // Feature level 9 takes no LOD clamp. A max_lod of 0 means "the
            // base level only", which the textures it is set on have anyway
            // (render targets, the lattice): sample the whole chain.
            sd.MaxLOD = D3D11_FLOAT32_MAX;
        }
        if (FAILED(device_->CreateSamplerState(&sd, &s->state))) {
            LOG_ERROR("direct3d11: CreateSamplerState failed (filter %d, address %d/%d/%d, anisotropy %u, max lod %g)",
                      int(sd.Filter), int(sd.AddressU), int(sd.AddressV), int(sd.AddressW), unsigned(sd.MaxAnisotropy),
                      double(sd.MaxLOD));
            delete s;
            return nullptr;
        }
        if (level9_ && !r32f_filterable_) {
            sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
            sd.MaxAnisotropy = 1;
            device_->CreateSamplerState(&sd, &s->point);
        }
        return reinterpret_cast<Sampler*>(s);
    }

    void destroy(Sampler* sampler) override {
        if (!sampler) return;
        release(d11(sampler)->state);
        release(d11(sampler)->point);
        delete d11(sampler);
    }

    // ---- pipelines
    Pipeline* create_pipeline(const PipelineDesc& d, const ShaderSource& shaders) override {
        if (shader_root_.empty()) shader_root_ = shaders.root;
        const std::string base = baked_base(shaders, level9_);
        const std::string vs_code = read_file(base + ".vertex." + profile_ + ".dxbc");
        const std::string ps_code = read_file(base + ".fragment." + profile_ + ".dxbc");
        if (vs_code.empty() || ps_code.empty()) {
            LOG_ERROR("[%s] no D3D11 shader at %s.{vertex,fragment}.%s.dxbc (tools/d3d11/bake_d3d11.py%s)", d.name.c_str(),
                      base.c_str(), profile_, level9_ ? " --tier sm2 --profiles fl91" : "");
            return nullptr;
        }
        auto* p = new D11Pipeline;
        p->desc = d;
        if (level9_) {
            read_copies(*p, 0, base + ".vertex." + profile_ + ".l9copies");
            read_copies(*p, 1, base + ".fragment." + profile_ + ".l9copies");
        }
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
        const std::string base = baked_base(shaders, level9_);
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
        // A depth-only pass on a sampled depth target at feature level 9
        // draws into its R32F stand-in as well.
        ID3D11RenderTargetView* rtv = color ? color->rtv : depth && depth->standin_rtv ? depth->standin_rtv : nullptr;
        context_->OMSetRenderTargets(rtv ? 1 : 0, rtv ? &rtv : nullptr, depth ? depth->dsv : nullptr);
        pass_.width = target->width;
        pass_.height = target->height;
        const D3D11_VIEWPORT viewport = {0.0f, 0.0f, float(pass_.width), float(pass_.height), 0.0f, 1.0f};
        context_->RSSetViewports(1, &viewport);
        if (color && desc.clear_color) context_->ClearRenderTargetView(color->rtv, desc.clear_rgba);
        if (depth && desc.clear_depth) {
            context_->ClearDepthStencilView(depth->dsv, D3D11_CLEAR_DEPTH, desc.clear_depth_value, 0);
            if (!color && depth->standin_rtv) {
                // The stand-in holds depth too, so it clears to the same value.
                const float v[4] = {desc.clear_depth_value, desc.clear_depth_value, desc.clear_depth_value, 1.0f};
                context_->ClearRenderTargetView(depth->standin_rtv, v);
            }
        }
        // A new pass: the constant buffers pushed so far bind again (the
        // stretch between passes borrows a slot).
        for (int stage = 0; stage < 2; ++stage)
            for (uint32_t slot = 0; slot < UNIFORM_SLOTS; ++slot)
                if (bound_uniforms_[stage][slot]) set_constant_buffer(stage, slot, bound_uniforms_[stage][slot]);
        pipeline_ = nullptr;
        if (trace_) {
            LOG_INFO("direct3d11 trace: pass %ux%u color %s depth %s", pass_.width, pass_.height, color ? "yes" : "no",
                     depth ? (depth->standin_rtv ? "stand-in" : "yes") : "no");
            drain_messages("begin_pass");
        }
        return reinterpret_cast<Pass*>(&pass_);
    }

    void end_pass(Pass*) override {}

    void bind_pipeline(Pass*, Pipeline* pipeline) override {
        D11Pipeline* p = d11(pipeline);
        if (!p || p == pipeline_) return;
        pipeline_ = p;
        apply_pipeline(*p);
        pushed_dirty_[0] = pushed_dirty_[1] = 0xF;  // a new layout: assemble every block again
        if (trace_) {
            std::string layout;
            for (const VertexAttribute& a : p->desc.vertex_attributes)
                layout += " " + std::to_string(a.location) + ":" + std::to_string(int(a.format)) + "@" + std::to_string(a.buffer_slot) + "+" + std::to_string(a.offset);
            LOG_INFO("direct3d11 trace: pipeline %s, strides %u/%u/%u, blend %d cull %d depth %d/%d cmp %d, attributes%s", p->desc.name.c_str(),
                     p->stride[0], p->stride[1], p->stride[2], int(p->desc.blend), int(p->desc.cull), int(p->desc.depth_test), int(p->desc.depth_write),
                     int(p->desc.depth_compare), layout.c_str());
            drain_messages("bind_pipeline");
        }
        if (p->vertex_id) {
            const UINT stride = sizeof(float), offset = 0;
            context_->IASetVertexBuffers(0, 1, &vertex_ids_, &stride, &offset);
            return;
        }
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
            if (trace_) LOG_INFO("direct3d11 trace: vertex buffer slot %u: %u bytes, offset %u", slot, bindings[i].buffer ? d11(bindings[i].buffer)->size : 0, bindings[i].offset);
            const UINT stride = pipeline_ ? pipeline_->stride[slot] : 0, offset = bindings[i].offset;
            context_->IASetVertexBuffers(slot, 1, &b, &stride, &offset);
        }
        if (trace_) drain_messages("bind_vertex_buffers");
    }

    void bind_index_buffer(Pass*, const BufferBinding& binding, IndexSize size) override {
        ID3D11Buffer* b = binding.buffer ? d11(binding.buffer)->buffer : nullptr;
        if (trace_) LOG_INFO("direct3d11 trace: index buffer %u bytes, %s, offset %u", binding.buffer ? d11(binding.buffer)->size : 0, size == IndexSize::U32 ? "u32" : "u16", binding.offset);
        index_ = binding;
        index_size_ = size;
        // 9_1 binds a 32-bit buffer per draw, narrowed (draw_indexed).
        if (no_u32_indices_ && size == IndexSize::U32) return;
        context_->IASetIndexBuffer(b, size == IndexSize::U32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, binding.offset);
        if (trace_) drain_messages("bind_index_buffer");
    }

    void bind_fragment_textures(Pass*, uint32_t first_slot, const TextureBinding* bindings, uint32_t count) override {
        ID3D11ShaderResourceView* views[16] = {};
        ID3D11SamplerState* samplers[16] = {};
        count = std::min(count, 16u - std::min(first_slot, 16u));
        for (uint32_t i = 0; i < count; ++i) {
            D11Texture* t = bindings[i].texture ? d11(bindings[i].texture) : nullptr;
            D11Sampler* s = bindings[i].sampler ? d11(bindings[i].sampler) : nullptr;
            views[i] = t ? t->srv : nullptr;
            // The stand-in point-sampled where the card cannot filter R32F.
            samplers[i] = !s ? nullptr : (t && t->standin && s->point) ? s->point : s->state;
        }
        if (!count) return;
        if (trace_) {
            for (uint32_t i = 0; i < count; ++i) {
                D11Texture* t = bindings[i].texture ? d11(bindings[i].texture) : nullptr;
                D11Sampler* s = bindings[i].sampler ? d11(bindings[i].sampler) : nullptr;
                D3D11_SAMPLER_DESC sd = {};
                if (s && s->state) s->state->GetDesc(&sd);
                LOG_INFO("direct3d11 trace: texture slot %u: %ux%u levels %u format %d%s, sampler filter %d address %d/%d aniso %u",
                         first_slot + i, t ? t->width : 0, t ? t->height : 0, t ? t->levels : 0, t ? int(t->format) : -1,
                         t && t->standin ? " (stand-in)" : "", int(sd.Filter), int(sd.AddressU), int(sd.AddressV), unsigned(sd.MaxAnisotropy));
            }
        }
        context_->PSSetShaderResources(first_slot, count, views);
        context_->PSSetSamplers(first_slot, count, samplers);
        if (trace_) drain_messages("bind_fragment_textures");
    }

    void push_uniforms(Stage stage, uint32_t slot, const void* data, uint32_t size) override {
        if (slot >= UNIFORM_SLOTS || size == 0) return;
        const int s = stage == Stage::Vertex ? 0 : 1;
        if (trace_) LOG_INFO("direct3d11 trace: uniforms %s slot %u, %u bytes", s == 0 ? "vertex" : "fragment", slot, size);
        if (level9_) {
            // Kept raw; the bound pipeline's compact layout is assembled
            // from it before the draw (flush_level9_uniforms).
            pushed_[s][slot].assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
            pushed_dirty_[s] |= 1u << slot;
            return;
        }
        ID3D11Buffer* buffer = uniform_buffer(s, slot, size);
        if (!buffer) return;
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(context_->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
        std::memcpy(mapped.pData, data, size);
        context_->Unmap(buffer, 0);
        bound_uniforms_[s][slot] = buffer;
        set_constant_buffer(s, slot, buffer);
    }

    void set_viewport(Pass*, float x, float y, float width, float height) override {
        const D3D11_VIEWPORT viewport = {x, y, width, height, 0.0f, 1.0f};
        context_->RSSetViewports(1, &viewport);
    }

    void draw(Pass*, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex,
              uint32_t first_instance) override {
        if (!pipeline_ || !vertex_count) return;
        if (trace_) LOG_INFO("direct3d11 trace: draw %s x%u %u verts", pipeline_->desc.name.c_str(), instance_count, vertex_count);
        if (level9_) flush_level9_uniforms(*pipeline_);
        if (pipeline_->looped) {
            for (uint32_t i = 0; i < std::max(instance_count, 1u); ++i) {
                bind_instance(first_instance + i);
                context_->Draw(vertex_count, first_vertex);
            }
            return;
        }
        // The plain draw where nothing is instanced: feature levels 9_1 and
        // 9_2 have no instanced draw at all, even for one instance.
        if (instance_count <= 1 && first_instance == 0) {
            context_->Draw(vertex_count, first_vertex);
            return;
        }
        context_->DrawInstanced(vertex_count, instance_count, first_vertex, first_instance);
    }

    void draw_indexed(Pass*, uint32_t index_count, uint32_t instance_count, uint32_t first_index,
                      int32_t vertex_offset, uint32_t first_instance) override {
        if (!pipeline_ || !index_count) return;
        if (trace_) {
            LOG_INFO("direct3d11 trace: draw_indexed %s x%u %u indices from %u, base %d", pipeline_->desc.name.c_str(),
                     instance_count, index_count, first_index, vertex_offset);
            drain_messages("before draw");
        }
        if (level9_) flush_level9_uniforms(*pipeline_);
        if (no_u32_indices_ && index_size_ == IndexSize::U32 && index_.buffer) {
            // The draw's indices, rebased to their smallest and narrowed to
            // 16 bits, with the base vertex moved up by what was taken off.
            const Narrowed* n = narrow_indices(d11(index_.buffer), index_.offset / 4 + first_index, index_count);
            if (!n) return;
            context_->IASetIndexBuffer(n->buffer, DXGI_FORMAT_R16_UINT, 0);
            first_index = 0;
            vertex_offset += int32_t(n->base);
        }
        if (pipeline_->looped) {
            // One draw per instance (no instancing below feature level 9_3):
            // the instance slots bound at that instance's element with a
            // stride of 0, so every vertex reads it.
            for (uint32_t i = 0; i < std::max(instance_count, 1u); ++i) {
                bind_instance(first_instance + i);
                context_->DrawIndexed(index_count, first_index, vertex_offset);
            }
            return;
        }
        if (instance_count <= 1 && first_instance == 0) {
            context_->DrawIndexed(index_count, first_index, vertex_offset);
            return;
        }
        context_->DrawIndexedInstanced(index_count, instance_count, first_index, vertex_offset, first_instance);
    }

private:
    struct Narrowed {
        ID3D11Buffer* buffer = nullptr;
        uint32_t base = 0;
        uint32_t generation = 0;
    };

    // A 32-bit draw range as a 16-bit index buffer, made once and kept
    // (feature level 9_1). Null when the range spans 65535 vertices or
    // more, which no 9_1 card could draw in one call anyway.
    const Narrowed* narrow_indices(D11Buffer* b, uint32_t first, uint32_t count) {
        const auto key = std::make_tuple(b, first, count);
        auto it = narrowed_.find(key);
        if (it != narrowed_.end() && it->second.generation == b->generation) return &it->second;
        const size_t available = b->shadow.size() / 4;
        if (size_t(first) + count > available) return nullptr;
        const uint32_t* src = reinterpret_cast<const uint32_t*>(b->shadow.data()) + first;
        uint32_t lo = UINT32_MAX, hi = 0;
        for (uint32_t i = 0; i < count; ++i) {
            lo = std::min(lo, src[i]);
            hi = std::max(hi, src[i]);
        }
        if (hi - lo >= 0xFFFFu) {
            LOG_ERROR("direct3d11: a draw spanning %u vertices, past feature level 9_1's 16-bit indices", hi - lo + 1);
            return nullptr;
        }
        std::vector<uint16_t> narrow(count);
        for (uint32_t i = 0; i < count; ++i) narrow[i] = uint16_t(src[i] - lo);
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = UINT(count * sizeof(uint16_t) + 3) & ~3u;
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        narrow.resize(bd.ByteWidth / sizeof(uint16_t), 0);
        const D3D11_SUBRESOURCE_DATA init = {narrow.data(), 0, 0};
        Narrowed n;
        n.base = lo;
        n.generation = b->generation;
        if (FAILED(device_->CreateBuffer(&bd, &init, &n.buffer))) return nullptr;
        if (it != narrowed_.end()) {
            release(it->second.buffer);
            it->second = n;
            return &it->second;
        }
        return &narrowed_.emplace(key, n).first->second;
    }

    // Everything the debug layer has to say since the last call, to the log.
    void drain_messages(const char* where) {
        if (!info_queue_) return;
        const UINT64 count = info_queue_->GetNumStoredMessages();
        for (UINT64 i = 0; i < count; ++i) {
            SIZE_T length = 0;
            if (FAILED(info_queue_->GetMessage(i, nullptr, &length)) || length == 0) continue;
            std::vector<uint8_t> storage(length);
            auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
            if (FAILED(info_queue_->GetMessage(i, message, &length))) continue;
            LOG_WARN("direct3d11 [%s] %.*s", where, int(message->DescriptionByteLength), message->pDescription);
        }
        if (count) info_queue_->ClearStoredMessages();
    }

    // The constant buffer for a `size`-byte block in `slot` of stage `s`.
    ID3D11Buffer* uniform_buffer(int s, uint32_t slot, uint32_t size) {
        const uint32_t rounded = (size + 15u) & ~15u;
        auto& buffers = uniforms_[s][slot];
        auto it = buffers.find(rounded);
        if (it != buffers.end()) return it->second;
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = rounded;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ID3D11Buffer* buffer = nullptr;
        const HRESULT hr = device_->CreateBuffer(&bd, nullptr, &buffer);
        if (FAILED(hr)) {
            LOG_ERROR("direct3d11: a %u-byte constant buffer for %s slot %u failed (0x%08lx%s)", rounded, s == 0 ? "vertex" : "fragment",
                      slot, static_cast<unsigned long>(hr), hr == DXGI_ERROR_DEVICE_REMOVED ? ", device removed" : "");
            return nullptr;
        }
        buffers.emplace(rounded, buffer);
        return buffer;
    }

    void bind_instance(uint32_t instance) {
        for (uint32_t slot = 0; slot < 8; ++slot) {
            if (!pipeline_->instanced[slot] || !streams_[slot].buffer) continue;
            ID3D11Buffer* b = d11(streams_[slot].buffer)->buffer;
            const UINT stride = 0, offset = streams_[slot].offset + instance * pipeline_->instance_pitch[slot];
            context_->IASetVertexBuffers(slot, 1, &b, &stride, &offset);
        }
    }

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

    // The level-9 copy table beside a stage's bytecode (.l9copies: "slot
    // source count destination" a line, in registers).
    void read_copies(D11Pipeline& p, int stage, const std::string& path) {
        const std::string text = read_file(path);
        std::istringstream in(text);
        D11Pipeline::Copy c;
        while (in >> c.slot >> c.src >> c.count >> c.dst) {
            if (c.slot >= UNIFORM_SLOTS) continue;
            p.copies[stage].push_back(c);
            p.compact_regs[stage][c.slot] = std::max(p.compact_regs[stage][c.slot], c.dst + c.count);
        }
    }

    // Feature level 9: each cbuffer the bound pipeline declares, assembled
    // from the pushed blocks in its compact layout and bound.
    void flush_level9_uniforms(D11Pipeline& p) {
        for (int s = 0; s < 2; ++s) {
            for (uint32_t slot = 0; slot < UNIFORM_SLOTS; ++slot) {
                if (!p.compact_regs[s][slot] || !(pushed_dirty_[s] & (1u << slot))) continue;
                const uint32_t bytes = p.compact_regs[s][slot] * 16;
                scratch_.assign(bytes, 0);
                for (const D11Pipeline::Copy& c : p.copies[s]) {
                    if (c.slot != slot) continue;
                    const std::vector<uint8_t>& from = pushed_[s][slot];
                    const size_t src = size_t(c.src) * 16, n = size_t(c.count) * 16;
                    if (src >= from.size()) continue;
                    std::memcpy(scratch_.data() + size_t(c.dst) * 16, from.data() + src, std::min(n, from.size() - src));
                }
                ID3D11Buffer* buffer = uniform_buffer(s, slot, bytes);
                D3D11_MAPPED_SUBRESOURCE mapped = {};
                if (!buffer || FAILED(context_->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) continue;
                std::memcpy(mapped.pData, scratch_.data(), bytes);
                context_->Unmap(buffer, 0);
                bound_uniforms_[s][slot] = buffer;
                set_constant_buffer(s, slot, buffer);
                if (trace_) {
                    LOG_INFO("direct3d11 trace: assembled %s slot %u, %u bytes", s == 0 ? "vertex" : "fragment", slot, bytes);
                    drain_messages("flush");
                }
            }
            pushed_dirty_[s] = 0;
        }
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
        for (const VertexBufferLayout& b : d.vertex_buffers) {
            if (b.slot >= 8) continue;
            p.stride[b.slot] = b.pitch;
            if (b.rate == InputRate::Instance && !instancing_) {
                // No instancing below 9_3: the slot binds per draw with a
                // stride of 0 (bind_instance), and the layout reads it per vertex.
                p.looped = true;
                p.instanced[b.slot] = true;
                p.instance_pitch[b.slot] = b.pitch;
                p.stride[b.slot] = 0;
            }
        }
        std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
        if (d.vertex_attributes.empty() && level9_) {
            // No SV_VertexID: the fullscreen passes read the index from the
            // backend's own buffer (VERTEX_ID_INPUT under LEVEL9).
            p.vertex_id = true;
            elements.push_back({"TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0});
        }
        // SPIRV-Cross names every input TEXCOORD<location>.
        for (const VertexAttribute& a : d.vertex_attributes) {
            const bool instanced = instancing_ && p.instance_pitch[a.buffer_slot % 8] == 0 && [&] {
                for (const VertexBufferLayout& b : d.vertex_buffers)
                    if (b.slot == a.buffer_slot) return b.rate == InputRate::Instance;
                return false;
            }();
            // Feature level 9's shaders have no integer inputs: joint indices
            // arrive as UNORM bytes and the shader scales them (skin_common.hlsl).
            const DXGI_FORMAT format = level9_ && a.format == VertexFormat::UByte4 ? DXGI_FORMAT_R8G8B8A8_UNORM : to_dxgi(a.format);
            elements.push_back({"TEXCOORD", a.location, format, a.buffer_slot, a.offset,
                                instanced ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA, instanced ? 1u : 0u});
        }
        if (!elements.empty() &&
            FAILED(device_->CreateInputLayout(elements.data(), UINT(elements.size()), vs_code.data(), vs_code.size(),
                                              &p.layout))) {
            LOG_ERROR("[%s] input layout rejected: the attributes do not match the vertex shader's inputs", d.name.c_str());
            return false;
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
        // A blended draw leaves the target's alpha alone, as on SDL GPU
        // (the share the ambient occlusion may darken, occlusion_share). 9_1
        // blends alpha as it blends colour; it runs the sm2 tier, which
        // reads no alpha, so only the stored value differs there.
        if (d.blend == Blend::Additive) {
            rt.SrcBlend = rt.DestBlend = D3D11_BLEND_ONE;
            rt.SrcBlendAlpha = level_ == D3D_FEATURE_LEVEL_9_1 ? D3D11_BLEND_ONE : D3D11_BLEND_ZERO;
            rt.DestBlendAlpha = D3D11_BLEND_ONE;
        } else if (d.blend == Blend::Premultiplied) {
            rt.SrcBlend = D3D11_BLEND_ONE;
            rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            rt.SrcBlendAlpha = level_ == D3D_FEATURE_LEVEL_9_1 ? D3D11_BLEND_ONE : D3D11_BLEND_ZERO;
            rt.DestBlendAlpha = level_ == D3D_FEATURE_LEVEL_9_1 ? D3D11_BLEND_INV_SRC_ALPHA : D3D11_BLEND_ONE;
        } else {
            rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
            rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            rt.SrcBlendAlpha = level_ == D3D_FEATURE_LEVEL_9_1 ? D3D11_BLEND_SRC_ALPHA : D3D11_BLEND_ZERO;
            rt.DestBlendAlpha = level_ == D3D_FEATURE_LEVEL_9_1 ? D3D11_BLEND_INV_SRC_ALPHA : D3D11_BLEND_ONE;
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
        // Baked without a variant suffix in every tier, level 9 included.
        const std::string base = baked_base(src, false);
        const std::string vs_code = read_file(base + ".vertex." + profile_ + ".dxbc");
        const std::string ps_code = read_file(base + ".fragment." + profile_ + ".dxbc");
        blit_.desc.name = "blit";
        blit_.desc.cull = Cull::None;
        blit_.desc.depth_test = false;
        blit_.desc.depth_write = false;
        if (level9_) {
            read_copies(blit_, 0, base + ".vertex." + profile_ + ".l9copies");
            read_copies(blit_, 1, base + ".fragment." + profile_ + ".l9copies");
        }
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
        if (blit_.vertex_id) {
            const UINT stride = sizeof(float), offset = 0;
            context_->IASetVertexBuffers(0, 1, &vertex_ids_, &stride, &offset);
        }
        const float scale[4] = {float(sw) / float(std::max(source->width, 1u)), float(sh) / float(std::max(source->height, 1u)),
                                0.0f, 0.0f};
        // Fragment slot 0, through the same classes as a pushed block; the
        // next pass puts the engine's block back (begin_pass).
        if (level9_) {
            pushed_[1][0].assign(reinterpret_cast<const uint8_t*>(scale), reinterpret_cast<const uint8_t*>(scale) + sizeof scale);
            pushed_dirty_[1] |= 1u;
            flush_level9_uniforms(blit_);
        } else {
            ID3D11Buffer* buffer = uniform_buffer(1, 0, sizeof scale);
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (buffer && SUCCEEDED(context_->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                std::memcpy(mapped.pData, scale, sizeof scale);
                context_->Unmap(buffer, 0);
            }
            context_->PSSetConstantBuffers(0, 1, &buffer);
        }
        context_->PSSetShaderResources(0, 1, &source->srv);
        context_->PSSetSamplers(0, 1, &blit_sampler_);
        context_->Draw(3, 0);
        context_->PSSetShaderResources(0, 16, none);
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        pipeline_ = nullptr;
        pushed_dirty_[0] = pushed_dirty_[1] = 0xF;  // the engine's blocks go back at the next draw
    }

    void download(D11Texture* source, uint32_t w, uint32_t h, std::vector<uint8_t>& out) {
        if (!source->texture || (source->format != Format::RGBA8 && source->format != Format::RGBA8_SRGB)) {
            LOG_ERROR("direct3d11: readback wants an RGBA8 source");
            return;
        }
        w = std::min(w, source->width);
        h = std::min(h, source->height);
        // A staging copy of the whole texture, then the top-left w x h of it:
        // feature level 9 copies whole resources only (no box), and a full
        // copy costs nothing a screenshot minds.
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = source->width;
        td.Height = source->height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = to_dxgi(source->format);
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D* staging = nullptr;
        const HRESULT hr = device_->CreateTexture2D(&td, nullptr, &staging);
        if (FAILED(hr)) {
            LOG_ERROR("direct3d11: readback staging texture failed (0x%08lx%s)", static_cast<unsigned long>(hr),
                      hr == DXGI_ERROR_DEVICE_REMOVED ? ", device removed" : "");
            return;
        }
        if (source->levels == 1) {
            context_->CopyResource(staging, source->texture);
        } else {
            const D3D11_BOX box = {0, 0, 0, source->width, source->height, 1};
            context_->CopySubresourceRegion(staging, 0, 0, 0, 0, source->texture, 0, &box);
        }
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
    bool trace_ = false;
    ID3D11InfoQueue* info_queue_ = nullptr;
    IDXGIFactory1* factory_ = nullptr;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    D3D_FEATURE_LEVEL level_ = D3D_FEATURE_LEVEL_10_0;
    bool level9_ = false;     // feature level 9_x: the LEVEL9 shader variants and D3D9's workarounds
    bool instancing_ = true;  // false below 9_3
    bool r32f_filterable_ = true;
    bool no_u32_indices_ = false;  // feature level 9_1
    BufferBinding index_ = {};
    IndexSize index_size_ = IndexSize::U16;
    std::map<std::tuple<D11Buffer*, uint32_t, uint32_t>, Narrowed> narrowed_;
    const char* profile_ = "sm50";
    ID3D11Buffer* vertex_ids_ = nullptr;  // feature level 9: what a fullscreen triangle reads its index from
    IDXGISwapChain* swapchain_ = nullptr;
    ID3D11RenderTargetView* backbuffer_rtv_ = nullptr;
    uint32_t swap_width_ = 0, swap_height_ = 0;
    std::string shader_root_;  // where the baked shaders are, from the first pipeline
    std::unordered_map<uint32_t, ID3D11Buffer*> uniforms_[2][UNIFORM_SLOTS];  // by rounded size
    std::vector<uint8_t> pushed_[2][UNIFORM_SLOTS];  // feature level 9: the blocks as pushed
    uint32_t pushed_dirty_[2] = {0xF, 0xF};
    std::vector<uint8_t> scratch_;
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

uint32_t probe_feature_level(uint32_t adapter_index) {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) return 0;
    IDXGIAdapter1* adapter = nullptr;
    const bool warp = SDL_getenv("DRAGON_D3D11_WARP") && SDL_getenv("DRAGON_D3D11_WARP")[0] == '1';
    if (!warp && FAILED(factory->EnumAdapters1(adapter_index, &adapter))) {
        factory->Release();
        return 0;
    }
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
                                        D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_2,  D3D_FEATURE_LEVEL_9_1};
    D3D_FEATURE_LEVEL reached = D3D_FEATURE_LEVEL(0);
    ID3D11Device* device = nullptr;
    const HRESULT hr = D3D11CreateDevice(adapter, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, levels,
                                         UINT(std::size(levels)), D3D11_SDK_VERSION, &device, &reached, nullptr);
    release(device);
    release(adapter);
    factory->Release();
    uint32_t level = SUCCEEDED(hr) ? uint32_t(reached) : 0;
    // The same cap as the device honours, so a forced 9_x probes as 9_x.
    if (const char* cap = SDL_getenv("DRAGON_D3D11_FEATURE_LEVEL")) {
        for (D3D_FEATURE_LEVEL l : levels)
            if (std::strcmp(feature_level_name(l), cap) == 0 && level > uint32_t(l)) level = uint32_t(l);
    }
    return level;
}

ID3D11Device* native_device(Device& device) { return static_cast<D3D11Device&>(device).device(); }
ID3D11DeviceContext* native_context(Device& device) { return static_cast<D3D11Device&>(device).context(); }

}  // namespace d3d11
}  // namespace rhi
