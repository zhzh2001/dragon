#include "rhi/d3d9/d3d9_backend.h"

#include <SDL3/SDL.h>

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
namespace d3d9 {
namespace {

// ---- resources

struct D9Buffer {
    BufferUsage usage = BufferUsage::Vertex;
    uint32_t size = 0;
    IDirect3DVertexBuffer9* vb = nullptr;
    // Index buffers: D3D9 fixes the index width when the buffer is created,
    // and rhi::create_buffer does not say it, so the data waits here until
    // the first bind names it.
    std::vector<uint8_t> index_data;
    IDirect3DIndexBuffer9* ib = nullptr;
    IndexSize ib_size = IndexSize::U16;
    bool dynamic = false;
};

struct D9Texture {
    Format format = Format::Invalid;
    uint32_t width = 0, height = 0, levels = 1;
    uint8_t usage = 0;
    IDirect3DTexture9* texture = nullptr;  // sampled, or a colour target
    IDirect3DSurface9* depth = nullptr;    // a depth target's surface
    // A depth target that is also sampled: `texture` is an R32F colour target
    // the depth-only shaders write z/w into, beside the real depth surface.
    bool depth_as_color = false;
    bool srgb() const {
        return format == Format::RGBA8_SRGB || format == Format::BC1_SRGB || format == Format::BC3_SRGB;
    }
};

struct D9Sampler {
    SamplerDesc desc;
};

// Where part of a pushed uniform block goes: `count` float4s from register
// `offset` of block `slot` into constant register `reg`. fxc packs only the
// members a shader uses, so a block arrives as several copies
// (tools/d3d9/bake_d3d9.py writes them from the compiler's constant table).
struct ConstantCopy {
    int slot = 0;
    int offset = 0;
    int reg = 0;
    int count = 0;
};

struct StageLayout {
    std::vector<ConstantCopy> copies;
    int half_pixel = -1;
};

struct D9Pipeline {
    PipelineDesc desc;
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    IDirect3DVertexDeclaration9* decl = nullptr;
    StageLayout vertex, fragment;
    bool vertex_id = false;  // no attributes: fed from the backend's own buffer
    uint32_t stream_pitch[8] = {};
    bool stream_instanced[8] = {};
};

struct D9Pass {
    uint32_t width = 0, height = 0;
};

D9Buffer* d9(Buffer* b) { return reinterpret_cast<D9Buffer*>(b); }
D9Texture* d9(Texture* t) { return reinterpret_cast<D9Texture*>(t); }
D9Sampler* d9(Sampler* s) { return reinterpret_cast<D9Sampler*>(s); }
D9Pipeline* d9(Pipeline* p) { return reinterpret_cast<D9Pipeline*>(p); }

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

// The baked file name for a define set: the same rule as the SDL backend's,
// with D3D9 added (tools/d3d9/bake_d3d9.py).
std::string baked_base(const ShaderSource& src) {
    std::vector<std::string> names = src.defines;
    names.push_back("D3D9");
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

// Reads one stage's object from the bake's JSON: {"copies": [[slot, offset,
// register, count], ...], "half_pixel": 238}. The file is ours and flat, so a
// scan does.
StageLayout parse_layout(const std::string& json, const char* stage) {
    StageLayout layout;
    const size_t at = json.find(std::string("\"") + stage + "\"");
    if (at == std::string::npos) return layout;
    // The stage's object ends at the brace matching the one after its key.
    size_t open = json.find('{', at);
    int depth = 0;
    size_t end = open;
    for (; end < json.size(); ++end) {
        if (json[end] == '{') ++depth;
        if (json[end] == '}' && --depth == 0) break;
    }
    const std::string body = json.substr(open, end - open + 1);
    const size_t copies = body.find("\"copies\"");
    if (copies != std::string::npos) {
        // [[a, b, c, d], [a, b, c, d], ...]: four numbers per inner bracket.
        size_t p = body.find('[', copies) + 1;
        const char* c = body.c_str();
        while (true) {
            const size_t open_inner = body.find('[', p);
            const size_t close_outer = body.find(']', p);
            if (open_inner == std::string::npos || open_inner > close_outer) break;
            char* next = nullptr;
            ConstantCopy copy;
            copy.slot = int(std::strtol(c + open_inner + 1, &next, 10));
            copy.offset = int(std::strtol(std::strchr(next, ',') + 1, &next, 10));
            copy.reg = int(std::strtol(std::strchr(next, ',') + 1, &next, 10));
            copy.count = int(std::strtol(std::strchr(next, ',') + 1, &next, 10));
            if (copy.slot >= 0 && copy.slot < 4) layout.copies.push_back(copy);
            p = body.find(']', open_inner) + 1;
        }
    }
    const size_t hp = body.find("\"half_pixel\"");
    if (hp != std::string::npos) layout.half_pixel = std::atoi(body.c_str() + body.find(':', hp) + 1);
    return layout;
}

D3DFORMAT to_d3d(Format f) {
    switch (f) {
        case Format::RGBA8:
        case Format::RGBA8_SRGB: return D3DFMT_A8R8G8B8;
        case Format::RGBA16F: return D3DFMT_A16B16G16R16F;
        case Format::D16: return D3DFMT_D16;
        case Format::D24:
        case Format::D32F: return D3DFMT_D24X8;  // D3D9-era cards have no float depth
        case Format::BC1:
        case Format::BC1_SRGB: return D3DFMT_DXT1;
        case Format::BC3:
        case Format::BC3_SRGB: return D3DFMT_DXT5;
        case Format::Invalid: break;
    }
    return D3DFMT_UNKNOWN;
}

bool is_block(Format f) {
    return f == Format::BC1 || f == Format::BC1_SRGB || f == Format::BC3 || f == Format::BC3_SRGB;
}

D3DCMPFUNC to_d3d(Compare c) {
    switch (c) {
        case Compare::Never: return D3DCMP_NEVER;
        case Compare::Less: return D3DCMP_LESS;
        case Compare::Equal: return D3DCMP_EQUAL;
        case Compare::LessEqual: return D3DCMP_LESSEQUAL;
        case Compare::Greater: return D3DCMP_GREATER;
        case Compare::NotEqual: return D3DCMP_NOTEQUAL;
        case Compare::GreaterEqual: return D3DCMP_GREATEREQUAL;
        case Compare::Always: return D3DCMP_ALWAYS;
    }
    return D3DCMP_ALWAYS;
}

D3DTEXTUREADDRESS to_d3d(Address a) {
    switch (a) {
        case Address::Repeat: return D3DTADDRESS_WRAP;
        case Address::Clamp: return D3DTADDRESS_CLAMP;
        case Address::Mirror: return D3DTADDRESS_MIRROR;
    }
    return D3DTADDRESS_WRAP;
}

BYTE to_decl_type(VertexFormat f) {
    switch (f) {
        case VertexFormat::Float: return D3DDECLTYPE_FLOAT1;
        case VertexFormat::Float2: return D3DDECLTYPE_FLOAT2;
        case VertexFormat::Float3: return D3DDECLTYPE_FLOAT3;
        case VertexFormat::Float4: return D3DDECLTYPE_FLOAT4;
        case VertexFormat::UByte4: return D3DDECLTYPE_UBYTE4;
        case VertexFormat::UByte4Norm: return D3DDECLTYPE_UBYTE4N;
    }
    return D3DDECLTYPE_FLOAT4;
}

D3DCOLOR to_color(const float rgba[4]) {
    auto b = [](float v) { return DWORD(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return D3DCOLOR_ARGB(b(rgba[3]), b(rgba[0]), b(rgba[1]), b(rgba[2]));
}

class D3D9Device final : public Device {
public:
    bool init(SDL_Window* window, const DeviceConfig& config) {
        headless_ = config.headless;
        adapter_ = config.adapter;
        hwnd_ = static_cast<HWND>(
            SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (!hwnd_) {
            LOG_ERROR("direct3d9: the window has no HWND");
            return false;
        }
        d3d_ = Direct3DCreate9(D3D_SDK_VERSION);
        if (!d3d_) {
            LOG_ERROR("direct3d9: Direct3DCreate9 failed");
            return false;
        }
        if (adapter_ >= d3d_->GetAdapterCount()) {
            LOG_ERROR("direct3d9: no adapter %u (%u: one per display output with a desktop)", adapter_,
                      d3d_->GetAdapterCount());
            return false;
        }
        D3DADAPTER_IDENTIFIER9 id = {};
        d3d_->GetAdapterIdentifier(adapter_, 0, &id);
        d3d_->GetDeviceCaps(adapter_, D3DDEVTYPE_HAL, &caps_);
        LOG_INFO("direct3d9: %s, vs_%lu_%lu ps_%lu_%lu", id.Description,
                 D3DSHADER_VERSION_MAJOR(caps_.VertexShaderVersion), D3DSHADER_VERSION_MINOR(caps_.VertexShaderVersion),
                 D3DSHADER_VERSION_MAJOR(caps_.PixelShaderVersion), D3DSHADER_VERSION_MINOR(caps_.PixelShaderVersion));
        if (D3DSHADER_VERSION_MAJOR(caps_.PixelShaderVersion) < 2) {
            LOG_ERROR("direct3d9: this backend needs shader model 2 (--tier sm2) or 3; the card has less");
            return false;
        }

        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        pp_.Windowed = TRUE;
        pp_.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp_.BackBufferFormat = D3DFMT_X8R8G8B8;
        pp_.BackBufferWidth = UINT(std::max(w, 1));
        pp_.BackBufferHeight = UINT(std::max(h, 1));
        pp_.hDeviceWindow = hwnd_;
        pp_.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
        // FPU_PRESERVE: without it D3D9 drops the FPU to single precision for
        // the whole process, under the flight model and the terrain.
        const DWORD flags = D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE;
        const HRESULT hr = d3d_->CreateDevice(adapter_, D3DDEVTYPE_HAL, hwnd_, flags, &pp_, &dev_);
        if (FAILED(hr)) {
            LOG_ERROR("direct3d9: CreateDevice failed (0x%08lx)%s", static_cast<unsigned long>(hr),
                      hr == D3DERR_INVALIDCALL ? " -- no desktop session? (tools/x99/run_interactive.ps1)" : "");
            return false;
        }

        // The vertex index a fullscreen triangle reads (VERTEX_ID_INPUT).
        dev_->CreateVertexBuffer(sizeof(float) * 6, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &vertex_ids_, nullptr);
        if (vertex_ids_) {
            float* ids = nullptr;
            if (SUCCEEDED(vertex_ids_->Lock(0, 0, reinterpret_cast<void**>(&ids), 0))) {
                for (int i = 0; i < 6; ++i) ids[i] = float(i);
                vertex_ids_->Unlock();
            }
        }
        const D3DVERTEXELEMENT9 id_decl[] = {{0, 0, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                             D3DDECL_END()};
        dev_->CreateVertexDeclaration(id_decl, &vertex_id_decl_);
        // Whether the R32F shadow stand-in can be filtered: modern cards, yes;
        // the GeForce 6 and 7 cannot filter a 32-bit float format.
        r32f_filterable_ = SUCCEEDED(d3d_->CheckDeviceFormat(adapter_, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8,
                                                             D3DUSAGE_QUERY_FILTER, D3DRTYPE_TEXTURE, D3DFMT_R32F));
        LOG_INFO("direct3d9: R32F shadow map %s", r32f_filterable_ ? "filtered" : "point-sampled (not filterable)");
        // Instancing (the foliage): SM3 hardware has stream frequencies. ATI's
        // SM2 parts (R300 on) expose the same thing behind a FOURCC switch,
        // 'INST', turned on through D3DRS_POINTSIZE. Anything else draws an
        // instanced batch one instance at a time.
        const DWORD inst = MAKEFOURCC('I', 'N', 'S', 'T');
        if (D3DSHADER_VERSION_MAJOR(caps_.VertexShaderVersion) >= 3) {
            instancing_ = Instancing::Native;
        } else if (SUCCEEDED(d3d_->CheckDeviceFormat(adapter_, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0,
                                                     D3DRTYPE_SURFACE, D3DFORMAT(inst)))) {
            instancing_ = Instancing::AtiInst;
            dev_->SetRenderState(D3DRS_POINTSIZE, inst);
        } else {
            instancing_ = Instancing::Loop;
        }
        LOG_INFO("direct3d9: instancing %s", instancing_ == Instancing::Native  ? "native (SM3)"
                                              : instancing_ == Instancing::AtiInst ? "through ATI's INST switch"
                                                                                   : "emulated, a draw per instance");
        return vertex_ids_ && vertex_id_decl_;
    }

    ~D3D9Device() override {
        release(vertex_id_decl_);
        release(vertex_ids_);
        release(dev_);
        release(d3d_);
    }

    IDirect3DDevice9* device() const { return dev_; }

    Backend backend() const override { return Backend::Direct3D9; }
    const char* driver_name() const override { return "direct3d9"; }

    bool supports_format(Format format, uint8_t usage) const override {
        DWORD d3d_usage = 0;
        D3DRESOURCETYPE type = D3DRTYPE_TEXTURE;
        D3DFORMAT fmt = to_d3d(format);
        if (usage & TEXTURE_DEPTH_TARGET) {
            // A sampled depth target is the R32F stand-in; a plain one a surface.
            if (usage & TEXTURE_SAMPLED) {
                fmt = D3DFMT_R32F;
                d3d_usage = D3DUSAGE_RENDERTARGET;
            } else {
                d3d_usage = D3DUSAGE_DEPTHSTENCIL;
                type = D3DRTYPE_SURFACE;
            }
        } else if (usage & TEXTURE_COLOR_TARGET) {
            d3d_usage = D3DUSAGE_RENDERTARGET;
        }
        return SUCCEEDED(d3d_->CheckDeviceFormat(adapter_, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, d3d_usage,
                                                 type, fmt));
    }

    // ---- the frame
    FrameStatus begin_frame(uint32_t* present_width, uint32_t* present_height) override {
        const HRESULT coop = dev_->TestCooperativeLevel();
        if (coop == D3DERR_DEVICELOST) return FrameStatus::Skip;
        if (coop == D3DERR_DEVICENOTRESET) {
            // Reset needs every D3DPOOL_DEFAULT resource released first; the
            // renderers' targets are recreated by size, but this backend does
            // not track them yet (R3 runs windowed at a fixed size).
            LOG_ERROR("direct3d9: device needs a reset, which is not handled yet");
            return FrameStatus::Failed;
        }
        if (!headless_) {
            RECT rect = {};
            GetClientRect(hwnd_, &rect);
            const uint32_t w = uint32_t(std::max<LONG>(rect.right - rect.left, 0));
            const uint32_t h = uint32_t(std::max<LONG>(rect.bottom - rect.top, 0));
            if (w == 0 || h == 0) return FrameStatus::Skip;
            if (present_width) *present_width = w;
            if (present_height) *present_height = h;
        }
        dev_->BeginScene();
        in_scene_ = true;
        return FrameStatus::Ready;
    }

    void end_frame(Texture* source, uint32_t width, uint32_t height, std::vector<uint8_t>* readback) override {
        if (!in_scene_) return;
        D9Texture* src = source ? d9(source) : nullptr;
        IDirect3DSurface9* src_surface = nullptr;
        if (src && src->texture) src->texture->GetSurfaceLevel(0, &src_surface);
        if (src_surface && !headless_) {
            IDirect3DSurface9* back = nullptr;
            if (SUCCEEDED(dev_->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back))) {
                RECT rect = {0, 0, LONG(width), LONG(height)};
                dev_->StretchRect(src_surface, &rect, back, nullptr, D3DTEXF_LINEAR);
                back->Release();
            }
        }
        dev_->EndScene();
        in_scene_ = false;
        if (readback && src_surface) download(src_surface, width, height, *readback);
        if (src_surface) src_surface->Release();
        if (!headless_) dev_->Present(nullptr, nullptr, nullptr, nullptr);
        else wait_idle();
    }

    void wait_idle() override {
        IDirect3DQuery9* query = nullptr;
        if (FAILED(dev_->CreateQuery(D3DQUERYTYPE_EVENT, &query))) return;
        query->Issue(D3DISSUE_END);
        while (query->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) SwitchToThread();
        query->Release();
    }

    // ---- buffers
    Buffer* create_buffer(BufferUsage usage, uint32_t size, const void* data, const char* debug_name) override {
        if (size == 0) return nullptr;
        auto* buffer = new D9Buffer;
        buffer->usage = usage;
        buffer->size = size;
        // No data now means per-frame data later (map_upload): dynamic.
        buffer->dynamic = data == nullptr;
        if (usage == BufferUsage::Index) {
            buffer->index_data.assign(size, 0);
            if (data) std::memcpy(buffer->index_data.data(), data, size);
            return reinterpret_cast<Buffer*>(buffer);
        }
        const DWORD d3d_usage = D3DUSAGE_WRITEONLY | (buffer->dynamic ? D3DUSAGE_DYNAMIC : 0);
        const D3DPOOL pool = buffer->dynamic ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED;
        if (FAILED(dev_->CreateVertexBuffer(size, d3d_usage, 0, pool, &buffer->vb, nullptr))) {
            LOG_ERROR("direct3d9: CreateVertexBuffer(%s, %u bytes) failed", debug_name, size);
            delete buffer;
            return nullptr;
        }
        if (data) {
            void* mapped = nullptr;
            if (SUCCEEDED(buffer->vb->Lock(0, size, &mapped, 0))) {
                std::memcpy(mapped, data, size);
                buffer->vb->Unlock();
            }
        }
        return reinterpret_cast<Buffer*>(buffer);
    }

    void destroy(Buffer* buffer) override {
        if (!buffer) return;
        D9Buffer* b = d9(buffer);
        release(b->vb);
        release(b->ib);
        delete b;
    }

    void* map_upload(Buffer* buffer, uint32_t size) override {
        if (!buffer || size == 0) return nullptr;
        D9Buffer* b = d9(buffer);
        if (b->usage == BufferUsage::Index) {
            if (b->index_data.size() < size) b->index_data.resize(size);
            release(b->ib);  // rebuilt at the next bind
            return b->index_data.data();
        }
        void* mapped = nullptr;
        if (!b->vb || FAILED(b->vb->Lock(0, size, &mapped, b->dynamic ? D3DLOCK_DISCARD : 0))) return nullptr;
        return mapped;
    }

    void commit_upload(Buffer* buffer, uint32_t) override {
        if (!buffer) return;
        D9Buffer* b = d9(buffer);
        if (b->vb) b->vb->Unlock();
    }

    // ---- textures
    Texture* create_texture(const TextureDesc& desc, const char* debug_name) override {
        auto* t = new D9Texture;
        t->format = desc.format;
        t->width = desc.width;
        t->height = desc.height;
        t->levels = desc.mip_levels;
        t->usage = desc.usage;
        HRESULT hr = S_OK;
        if (desc.usage & TEXTURE_DEPTH_TARGET) {
            hr = dev_->CreateDepthStencilSurface(desc.width, desc.height, D3DFMT_D24X8, D3DMULTISAMPLE_NONE, 0, TRUE,
                                                 &t->depth, nullptr);
            if (SUCCEEDED(hr) && (desc.usage & TEXTURE_SAMPLED)) {
                t->depth_as_color = true;
                hr = dev_->CreateTexture(desc.width, desc.height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F,
                                         D3DPOOL_DEFAULT, &t->texture, nullptr);
            }
        } else if (desc.usage & TEXTURE_COLOR_TARGET) {
            hr = dev_->CreateTexture(desc.width, desc.height, 1, D3DUSAGE_RENDERTARGET, to_d3d(desc.format),
                                     D3DPOOL_DEFAULT, &t->texture, nullptr);
            t->levels = 1;
        } else {
            hr = dev_->CreateTexture(desc.width, desc.height, desc.mip_levels, 0, to_d3d(desc.format),
                                     D3DPOOL_MANAGED, &t->texture, nullptr);
        }
        if (FAILED(hr)) {
            LOG_ERROR("direct3d9: texture '%s' (%ux%u) failed (0x%08lx)", debug_name, desc.width, desc.height,
                      static_cast<unsigned long>(hr));
            destroy(reinterpret_cast<Texture*>(t));
            return nullptr;
        }
        return reinterpret_cast<Texture*>(t);
    }

    bool upload_texture(Texture* texture, const void* pixels, uint32_t width, uint32_t height,
                        bool generate_mips) override {
        D9Texture* t = d9(texture);
        if (!t || !t->texture || !pixels) return false;
        // Mip 0, then a box-filtered chain built here: D3D9's own generation
        // (AUTOGENMIPMAP) is a usage flag the texture would have needed at
        // creation, and not every card has it.
        std::vector<uint8_t> level(static_cast<const uint8_t*>(pixels),
                                   static_cast<const uint8_t*>(pixels) + size_t(width) * height * 4);
        uint32_t w = width, h = height;
        const uint32_t levels = generate_mips ? t->levels : 1;
        for (uint32_t l = 0; l < levels; ++l) {
            if (!upload_texture_level(texture, l, level.data(), uint32_t(level.size()), w, h)) return false;
            if (l + 1 == levels) break;
            const uint32_t nw = std::max(w / 2, 1u), nh = std::max(h / 2, 1u);
            std::vector<uint8_t> next(size_t(nw) * nh * 4);
            for (uint32_t y = 0; y < nh; ++y) {
                for (uint32_t x = 0; x < nw; ++x) {
                    for (int c = 0; c < 4; ++c) {
                        const uint32_t x0 = std::min(x * 2, w - 1), x1 = std::min(x * 2 + 1, w - 1);
                        const uint32_t y0 = std::min(y * 2, h - 1), y1 = std::min(y * 2 + 1, h - 1);
                        const uint32_t sum = level[(size_t(y0) * w + x0) * 4 + c] + level[(size_t(y0) * w + x1) * 4 + c] +
                                             level[(size_t(y1) * w + x0) * 4 + c] + level[(size_t(y1) * w + x1) * 4 + c];
                        next[(size_t(y) * nw + x) * 4 + c] = uint8_t((sum + 2) / 4);
                    }
                }
            }
            level.swap(next);
            w = nw;
            h = nh;
        }
        return true;
    }

    bool upload_texture_level(Texture* texture, uint32_t level, const void* data, uint32_t bytes, uint32_t width,
                              uint32_t height) override {
        D9Texture* t = d9(texture);
        if (!t || !t->texture || !data) return false;
        D3DLOCKED_RECT locked = {};
        if (FAILED(t->texture->LockRect(level, &locked, nullptr, 0))) return false;
        const auto* src = static_cast<const uint8_t*>(data);
        auto* dst = static_cast<uint8_t*>(locked.pBits);
        if (is_block(t->format)) {
            // The block layouts are the same: BC1 is DXT1, BC3 is DXT5.
            const uint32_t block_bytes = (t->format == Format::BC1 || t->format == Format::BC1_SRGB) ? 8 : 16;
            const uint32_t row = std::max(1u, (width + 3) / 4) * block_bytes;
            const uint32_t rows = std::max(1u, (height + 3) / 4);
            for (uint32_t y = 0; y < rows && size_t(y + 1) * row <= bytes; ++y)
                std::memcpy(dst + size_t(y) * locked.Pitch, src + size_t(y) * row, row);
        } else if (t->format == Format::RGBA16F) {
            for (uint32_t y = 0; y < height; ++y)
                std::memcpy(dst + size_t(y) * locked.Pitch, src + size_t(y) * width * 8, size_t(width) * 8);
        } else {
            // RGBA in memory -> A8R8G8B8, which is B, G, R, A in memory.
            for (uint32_t y = 0; y < height; ++y) {
                const uint8_t* s = src + size_t(y) * width * 4;
                uint8_t* d = dst + size_t(y) * locked.Pitch;
                for (uint32_t x = 0; x < width; ++x) {
                    d[x * 4 + 0] = s[x * 4 + 2];
                    d[x * 4 + 1] = s[x * 4 + 1];
                    d[x * 4 + 2] = s[x * 4 + 0];
                    d[x * 4 + 3] = s[x * 4 + 3];
                }
            }
        }
        t->texture->UnlockRect(level);
        return true;
    }

    void destroy(Texture* texture) override {
        if (!texture) return;
        D9Texture* t = d9(texture);
        release(t->texture);
        release(t->depth);
        delete t;
    }

    Sampler* create_sampler(const SamplerDesc& desc) override {
        auto* s = new D9Sampler;
        s->desc = desc;
        return reinterpret_cast<Sampler*>(s);
    }

    void destroy(Sampler* sampler) override { delete d9(sampler); }

    // ---- pipelines
    Pipeline* create_pipeline(const PipelineDesc& d, const ShaderSource& shaders) override {
        const std::string base = baked_base(shaders);
        const std::string vs_code = read_file(base + ".vs30");
        const std::string ps_code = read_file(base + ".ps30");
        const std::string json = read_file(base + ".d3d9.json");
        if (vs_code.empty() || ps_code.empty() || json.empty()) {
            LOG_ERROR("[%s] no D3D9 shader at %s.{vs30,ps30,d3d9.json} (tools/d3d9/bake_d3d9.py)", d.name.c_str(),
                      base.c_str());
            return nullptr;
        }
        auto* p = new D9Pipeline;
        p->desc = d;
        p->vertex = parse_layout(json, "vertex");
        p->fragment = parse_layout(json, "fragment");
        if (FAILED(dev_->CreateVertexShader(reinterpret_cast<const DWORD*>(vs_code.data()), &p->vs)) ||
            FAILED(dev_->CreatePixelShader(reinterpret_cast<const DWORD*>(ps_code.data()), &p->ps))) {
            LOG_ERROR("[%s] the device rejected the D3D9 bytecode in %s", d.name.c_str(), base.c_str());
            destroy(reinterpret_cast<Pipeline*>(p));
            return nullptr;
        }
        for (const VertexBufferLayout& b : d.vertex_buffers) {
            if (b.slot < 8) {
                p->stream_pitch[b.slot] = b.pitch;
                p->stream_instanced[b.slot] = b.rate == InputRate::Instance;
            }
        }
        if (d.vertex_attributes.empty()) {
            p->vertex_id = true;
        } else {
            std::vector<D3DVERTEXELEMENT9> elements;
            for (const VertexAttribute& a : d.vertex_attributes) {
                elements.push_back({WORD(a.buffer_slot), WORD(a.offset), to_decl_type(a.format), D3DDECLMETHOD_DEFAULT,
                                    D3DDECLUSAGE_TEXCOORD, BYTE(a.location)});
            }
            elements.push_back(D3DDECL_END());
            if (FAILED(dev_->CreateVertexDeclaration(elements.data(), &p->decl))) {
                LOG_ERROR("[%s] vertex declaration rejected", d.name.c_str());
                destroy(reinterpret_cast<Pipeline*>(p));
                return nullptr;
            }
        }
        return reinterpret_cast<Pipeline*>(p);
    }

    void destroy(Pipeline* pipeline) override {
        if (!pipeline) return;
        D9Pipeline* p = d9(pipeline);
        if (pipeline_ == p) pipeline_ = nullptr;
        release(p->vs);
        release(p->ps);
        release(p->decl);
        delete p;
    }

    bool compiles_hlsl() const override { return false; }

    std::vector<std::string> baked_shader_files(const ShaderSource& shaders) const override {
        const std::string base = baked_base(shaders);
        return {base + ".vs30", base + ".ps30"};
    }

    // ---- passes
    Pass* begin_pass(const PassDesc& desc) override {
        D9Texture* color = desc.color ? d9(desc.color) : nullptr;
        D9Texture* depth = desc.depth ? d9(desc.depth) : nullptr;
        // A depth-only pass on a sampled depth target draws into its R32F
        // stand-in (D9Texture::depth_as_color).
        D9Texture* target = color ? color : (depth && depth->depth_as_color ? depth : nullptr);
        IDirect3DSurface9* surface = nullptr;
        if (!target || !target->texture || FAILED(target->texture->GetSurfaceLevel(0, &surface))) {
            LOG_ERROR("direct3d9: a pass with no colour target it can draw into");
            return nullptr;
        }
        dev_->SetRenderTarget(0, surface);
        surface->Release();
        dev_->SetDepthStencilSurface(depth ? depth->depth : nullptr);
        pass_.width = target->width;
        pass_.height = target->height;
        D3DVIEWPORT9 viewport = {0, 0, pass_.width, pass_.height, 0.0f, 1.0f};
        dev_->SetViewport(&viewport);

        DWORD flags = 0;
        D3DCOLOR clear = 0;
        if (color && desc.clear_color) {
            flags |= D3DCLEAR_TARGET;
            clear = to_color(desc.clear_rgba);
        }
        if (depth && desc.clear_depth) {
            flags |= D3DCLEAR_ZBUFFER;
            if (!color && depth->depth_as_color) {
                // The stand-in holds depth too, so it clears to the same value.
                const float v[4] = {desc.clear_depth_value, desc.clear_depth_value, desc.clear_depth_value, 1.0f};
                flags |= D3DCLEAR_TARGET;
                clear = to_color(v);
            }
        }
        if (flags) dev_->Clear(0, nullptr, flags, clear, desc.clear_depth_value, 0);
        has_depth_ = depth != nullptr;
        return reinterpret_cast<Pass*>(&pass_);
    }

    void end_pass(Pass*) override {}

    void bind_pipeline(Pass*, Pipeline* pipeline) override {
        D9Pipeline* p = d9(pipeline);
        if (!p) return;
        pipeline_ = p;
        const PipelineDesc& d = p->desc;
        dev_->SetVertexShader(p->vs);
        dev_->SetPixelShader(p->ps);
        if (p->vertex_id) {
            dev_->SetVertexDeclaration(vertex_id_decl_);
            dev_->SetStreamSource(0, vertex_ids_, 0, sizeof(float));
        } else {
            dev_->SetVertexDeclaration(p->decl);
            // D3D9 binds a stream with its stride, which is the pipeline's, so
            // buffers bound before this pipeline (or under a fullscreen pass,
            // which borrowed stream 0) are bound again with this one's.
            for (uint32_t slot = 0; slot < 8; ++slot) {
                if (!p->stream_pitch[slot]) continue;
                D9Buffer* b = streams_[slot].buffer ? d9(streams_[slot].buffer) : nullptr;
                dev_->SetStreamSource(slot, b ? b->vb : nullptr, streams_[slot].offset, p->stream_pitch[slot]);
            }
        }
        // Back faces of a counter-clockwise front are the clockwise ones.
        DWORD cull = D3DCULL_NONE;
        if (d.cull != Cull::None) {
            const bool cull_clockwise = (d.cull == Cull::Back) == (d.front_face == FrontFace::CounterClockwise);
            cull = cull_clockwise ? D3DCULL_CW : D3DCULL_CCW;
        }
        dev_->SetRenderState(D3DRS_CULLMODE, cull);
        dev_->SetRenderState(D3DRS_FILLMODE, d.fill == Fill::Wireframe ? D3DFILL_WIREFRAME : D3DFILL_SOLID);
        const bool test = d.depth_test && has_depth_;
        dev_->SetRenderState(D3DRS_ZENABLE, test || (d.depth_write && has_depth_) ? D3DZB_TRUE : D3DZB_FALSE);
        dev_->SetRenderState(D3DRS_ZFUNC, test ? to_d3d(d.depth_compare) : D3DCMP_ALWAYS);
        dev_->SetRenderState(D3DRS_ZWRITEENABLE, d.depth_write && has_depth_ ? TRUE : FALSE);
        dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, d.blend != Blend::Opaque);
        if (d.blend == Blend::Additive) {
            dev_->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
            dev_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
            dev_->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
        } else if (d.blend == Blend::Alpha) {
            dev_->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            dev_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            dev_->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
            dev_->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
            dev_->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA);
        }
        dev_->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
        dev_->SetRenderState(D3DRS_LIGHTING, FALSE);
        dev_->SetRenderState(D3DRS_FOGENABLE, FALSE);
        dev_->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        dev_->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
        uniforms_dirty_[0] = uniforms_dirty_[1] = 0xF;  // a new layout: push every block again
    }

    void bind_vertex_buffers(Pass*, uint32_t first_slot, const BufferBinding* bindings, uint32_t count) override {
        for (uint32_t i = 0; i < count && first_slot + i < 8; ++i) {
            const uint32_t slot = first_slot + i;
            streams_[slot] = bindings[i];
            D9Buffer* b = bindings[i].buffer ? d9(bindings[i].buffer) : nullptr;
            const uint32_t pitch = pipeline_ ? pipeline_->stream_pitch[slot] : 0;
            dev_->SetStreamSource(slot, b ? b->vb : nullptr, bindings[i].offset, pitch);
        }
    }

    void bind_index_buffer(Pass*, const BufferBinding& binding, IndexSize size) override {
        D9Buffer* b = binding.buffer ? d9(binding.buffer) : nullptr;
        if (!b) return;
        if (!b->ib || b->ib_size != size) {
            release(b->ib);
            const D3DFORMAT format = size == IndexSize::U32 ? D3DFMT_INDEX32 : D3DFMT_INDEX16;
            const UINT bytes = UINT(b->index_data.size());
            if (FAILED(dev_->CreateIndexBuffer(bytes, D3DUSAGE_WRITEONLY, format, D3DPOOL_MANAGED, &b->ib, nullptr)))
                return;
            void* mapped = nullptr;
            if (SUCCEEDED(b->ib->Lock(0, bytes, &mapped, 0))) {
                std::memcpy(mapped, b->index_data.data(), bytes);
                b->ib->Unlock();
            }
            b->ib_size = size;
        }
        index_offset_ = binding.offset / (size == IndexSize::U32 ? 4 : 2);
        dev_->SetIndices(b->ib);
    }

    void bind_fragment_textures(Pass*, uint32_t first_slot, const TextureBinding* bindings, uint32_t count) override {
        for (uint32_t i = 0; i < count && first_slot + i < 16; ++i) {
            const DWORD stage = first_slot + i;
            D9Texture* t = bindings[i].texture ? d9(bindings[i].texture) : nullptr;
            D9Sampler* s = bindings[i].sampler ? d9(bindings[i].sampler) : nullptr;
            dev_->SetTexture(stage, t ? t->texture : nullptr);
            if (!s) continue;
            const SamplerDesc& sd = s->desc;
            // The R32F stand-in is point-sampled where the card cannot filter
            // a 32-bit float format; the shaders' 3x3 PCF still softens it.
            const bool point_only = t && t->depth_as_color && !r32f_filterable_;
            const bool aniso = sd.max_anisotropy > 1.0f && !point_only;
            auto filter = [&](Filter f) {
                if (point_only) return D3DTEXF_POINT;
                if (aniso) return D3DTEXF_ANISOTROPIC;
                return f == Filter::Linear ? D3DTEXF_LINEAR : D3DTEXF_POINT;
            };
            dev_->SetSamplerState(stage, D3DSAMP_MINFILTER, filter(sd.min_filter));
            dev_->SetSamplerState(stage, D3DSAMP_MAGFILTER, point_only ? D3DTEXF_POINT
                                                                         : (sd.mag_filter == Filter::Linear ? D3DTEXF_LINEAR
                                                                                                            : D3DTEXF_POINT));
            const bool mips = t && t->levels > 1 && sd.max_lod > 0.0f;
            dev_->SetSamplerState(stage, D3DSAMP_MIPFILTER,
                                  !mips ? D3DTEXF_NONE : sd.mip_mode == MipMode::Linear ? D3DTEXF_LINEAR : D3DTEXF_POINT);
            dev_->SetSamplerState(stage, D3DSAMP_MAXANISOTROPY,
                                  aniso ? DWORD(std::min<float>(sd.max_anisotropy, float(caps_.MaxAnisotropy))) : 1);
            dev_->SetSamplerState(stage, D3DSAMP_ADDRESSU, to_d3d(sd.address_u));
            dev_->SetSamplerState(stage, D3DSAMP_ADDRESSV, to_d3d(sd.address_v));
            dev_->SetSamplerState(stage, D3DSAMP_ADDRESSW, to_d3d(sd.address_w));
            dev_->SetSamplerState(stage, D3DSAMP_SRGBTEXTURE, t && t->srgb() ? TRUE : FALSE);
        }
    }

    void push_uniforms(Stage stage, uint32_t slot, const void* data, uint32_t size) override {
        if (slot >= 4) return;
        const int s = stage == Stage::Vertex ? 0 : 1;
        std::vector<float>& store = uniforms_[s][slot];
        store.assign(static_cast<const float*>(data), static_cast<const float*>(data) + size / 4);
        uniforms_dirty_[s] |= 1u << slot;
    }

    void draw(Pass*, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex,
              uint32_t first_instance) override {
        (void)instance_count;
        (void)first_instance;
        if (!flush()) return;
        UINT prims = 0;
        const D3DPRIMITIVETYPE type = primitive(vertex_count, &prims);
        if (prims) dev_->DrawPrimitive(type, first_vertex, prims);
    }

    void draw_indexed(Pass*, uint32_t index_count, uint32_t instance_count, uint32_t first_index,
                      int32_t vertex_offset, uint32_t first_instance) override {
        if (!flush()) return;
        UINT prims = 0;
        const D3DPRIMITIVETYPE type = primitive(index_count, &prims);
        if (!prims) return;
        // Vertices the draw may touch: stream 0's whole buffer from its offset.
        UINT vertices = 0;
        if (D9Buffer* b = streams_[0].buffer ? d9(streams_[0].buffer) : nullptr) {
            const uint32_t pitch = pipeline_->stream_pitch[0];
            vertices = pitch ? (b->size - streams_[0].offset) / pitch : 0;
        }
        bool instanced = false;
        for (uint32_t s = 0; s < 8; ++s) instanced |= pipeline_->stream_instanced[s];
        if (instanced && instancing_ == Instancing::Loop) {
            // One draw per instance: the instance streams bound at that
            // instance's element with a zero stride, so every vertex reads it.
            for (uint32_t i = 0; i < instance_count; ++i) {
                for (uint32_t s = 0; s < 8; ++s) {
                    if (!pipeline_->stream_instanced[s] || !pipeline_->stream_pitch[s]) continue;
                    D9Buffer* b = streams_[s].buffer ? d9(streams_[s].buffer) : nullptr;
                    dev_->SetStreamSource(s, b ? b->vb : nullptr,
                                          streams_[s].offset + (first_instance + i) * pipeline_->stream_pitch[s], 0);
                }
                dev_->DrawIndexedPrimitive(type, vertex_offset, 0, vertices, first_index + index_offset_, prims);
            }
            return;
        }
        if (instanced) {
            // SM3 hardware instancing: the per-vertex streams repeat
            // instance_count times, the per-instance ones step once each.
            for (uint32_t s = 0; s < 8; ++s) {
                if (!pipeline_->stream_pitch[s]) continue;
                if (pipeline_->stream_instanced[s]) {
                    dev_->SetStreamSourceFreq(s, D3DSTREAMSOURCE_INSTANCEDATA | 1u);
                    D9Buffer* b = streams_[s].buffer ? d9(streams_[s].buffer) : nullptr;
                    dev_->SetStreamSource(s, b ? b->vb : nullptr,
                                          streams_[s].offset + first_instance * pipeline_->stream_pitch[s],
                                          pipeline_->stream_pitch[s]);
                } else {
                    dev_->SetStreamSourceFreq(s, D3DSTREAMSOURCE_INDEXEDDATA | instance_count);
                }
            }
        }
        dev_->DrawIndexedPrimitive(type, vertex_offset, 0, vertices, first_index + index_offset_, prims);
        if (instanced) {
            for (uint32_t s = 0; s < 8; ++s) dev_->SetStreamSourceFreq(s, 1);
        }
    }

private:
    D3DPRIMITIVETYPE primitive(uint32_t count, UINT* prims) const {
        switch (pipeline_->desc.primitive) {
            case Primitive::TriangleList: *prims = count / 3; return D3DPT_TRIANGLELIST;
            case Primitive::TriangleStrip: *prims = count >= 3 ? count - 2 : 0; return D3DPT_TRIANGLESTRIP;
            case Primitive::LineList: *prims = count / 2; return D3DPT_LINELIST;
            case Primitive::LineStrip: *prims = count >= 2 ? count - 1 : 0; return D3DPT_LINESTRIP;
            case Primitive::PointList: *prims = count; return D3DPT_POINTLIST;
        }
        *prims = 0;
        return D3DPT_TRIANGLELIST;
    }

    // Uniform blocks to their constant registers, for the bound pipeline.
    bool flush() {
        if (!pipeline_) return false;
        for (int s = 0; s < 2; ++s) {
            const StageLayout& layout = s == 0 ? pipeline_->vertex : pipeline_->fragment;
            for (const ConstantCopy& copy : layout.copies) {
                if (!(uniforms_dirty_[s] & (1u << copy.slot))) continue;
                const std::vector<float>& data = uniforms_[s][copy.slot];
                const size_t available = data.size() / 4;
                if (size_t(copy.offset) >= available) continue;
                const UINT count = UINT(std::min<size_t>(size_t(copy.count), available - size_t(copy.offset)));
                const float* from = data.data() + size_t(copy.offset) * 4;
                if (s == 0) dev_->SetVertexShaderConstantF(UINT(copy.reg), from, count);
                else dev_->SetPixelShaderConstantF(UINT(copy.reg), from, count);
            }
            uniforms_dirty_[s] = 0;
        }
        // D3D9 puts pixel centres half a pixel off the other APIs'; SPIRV-Cross
        // shifts gl_Position by this much (in clip units, one pixel = 2/size).
        if (pipeline_->vertex.half_pixel >= 0 && pass_.width && pass_.height) {
            const float half[4] = {1.0f / float(pass_.width), 1.0f / float(pass_.height), 0.0f, 0.0f};
            dev_->SetVertexShaderConstantF(UINT(pipeline_->vertex.half_pixel), half, 1);
        }
        return true;
    }

    void download(IDirect3DSurface9* source, uint32_t w, uint32_t h, std::vector<uint8_t>& out) {
        IDirect3DSurface9* sys = nullptr;
        if (FAILED(dev_->CreateOffscreenPlainSurface(w, h, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr))) return;
        D3DLOCKED_RECT locked = {};
        if (SUCCEEDED(dev_->GetRenderTargetData(source, sys)) &&
            SUCCEEDED(sys->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
            out.resize(size_t(w) * h * 4);
            for (uint32_t y = 0; y < h; ++y) {
                const uint8_t* s = static_cast<const uint8_t*>(locked.pBits) + size_t(y) * locked.Pitch;
                uint8_t* d = out.data() + size_t(y) * w * 4;
                for (uint32_t x = 0; x < w; ++x) {
                    d[x * 4 + 0] = s[x * 4 + 2];
                    d[x * 4 + 1] = s[x * 4 + 1];
                    d[x * 4 + 2] = s[x * 4 + 0];
                    d[x * 4 + 3] = 255;
                }
            }
            sys->UnlockRect();
        } else {
            LOG_ERROR("direct3d9: readback failed");
        }
        sys->Release();
    }

    HWND hwnd_ = nullptr;
    bool headless_ = false;
    UINT adapter_ = 0;
    IDirect3D9* d3d_ = nullptr;
    IDirect3DDevice9* dev_ = nullptr;
    D3DCAPS9 caps_ = {};
    D3DPRESENT_PARAMETERS pp_ = {};
    bool in_scene_ = false;
    bool has_depth_ = false;
    bool r32f_filterable_ = false;
    enum class Instancing { Native, AtiInst, Loop } instancing_ = Instancing::Native;
    D9Pass pass_;
    D9Pipeline* pipeline_ = nullptr;
    BufferBinding streams_[8] = {};
    uint32_t index_offset_ = 0;
    std::vector<float> uniforms_[2][4];
    uint32_t uniforms_dirty_[2] = {0xF, 0xF};
    IDirect3DVertexBuffer9* vertex_ids_ = nullptr;
    IDirect3DVertexDeclaration9* vertex_id_decl_ = nullptr;
};

}  // namespace

std::unique_ptr<Device> create(SDL_Window* window, const DeviceConfig& config) {
    auto device = std::make_unique<D3D9Device>();
    if (!device->init(window, config)) return nullptr;
    return device;
}

IDirect3DDevice9* native_device(Device& device) { return static_cast<D3D9Device&>(device).device(); }

}  // namespace d3d9
}  // namespace rhi
