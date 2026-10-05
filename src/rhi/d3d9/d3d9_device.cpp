#include "rhi/d3d9/d3d9_backend.h"
#include "rhi/d3d9/d3d9_probe.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
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
    // A dynamic vertex buffer is in D3DPOOL_DEFAULT, which a device reset
    // (fullscreen, Alt-Tab) empties: its data is written here first and
    // copied in, so a reset can copy it in again.
    std::vector<uint8_t> shadow;
    uint32_t mapped = 0;
};

struct D9Texture {
    Format format = Format::Invalid;
    uint32_t width = 0, height = 0, levels = 1;
    // The size the D3D9 objects were made at: the width and height, or on a
    // card that takes only powers of two the next ones up, drawn into at
    // the corner (D3D9Device::pow2_only_).
    uint32_t alloc_w = 0, alloc_h = 0;
    uint8_t usage = 0;
    IDirect3DTexture9* texture = nullptr;  // sampled, or a colour target
    IDirect3DSurface9* depth = nullptr;    // a depth target's surface
    IDirect3DSurface9* surface = nullptr;  // the back buffer, for frame_target()
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

// One texture stage of a fixed-function recipe (tools/d3d9/bake_d3d9.py,
// FF_RECIPES): D3DTEXTUREOP and D3DTA values, and which fragment texture
// slot the stage samples (-1: none).
struct FfStage {
    DWORD color_op = D3DTOP_DISABLE, color_arg1 = D3DTA_DIFFUSE, color_arg2 = D3DTA_CURRENT;
    DWORD alpha_op = D3DTOP_DISABLE, alpha_arg1 = D3DTA_DIFFUSE, alpha_arg2 = D3DTA_CURRENT;
    int texture_slot = -1;
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
    // The fixed-function tier (docs/PORTING.md R5): no pixel shader; the
    // vertex program writes oT0, oD0 and oFog, and these stages finish the
    // pixel. `ff_disabled` is a pass the tier never draws (the shadow map).
    bool ff = false;
    bool ff_disabled = false;
    std::vector<FfStage> ff_stages;
    float ff_alpha_test = -1.0f;      // the alpha reference, or none
    int ff_fog_color = -1;            // the fog colour's register in the scene block
    int ff_output_grade = -1;         // the grade's (x exposure)
    int ff_slot_stage[16];            // fragment texture slot -> stage, or -1
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

// The fixed-function recipe: {"ff": {"disabled": true}} or {"ff": {"stages":
// [[cop, ca1, ca2, aop, aa1, aa2, slot], ...], "alpha_test": 0.45,
// "fog_color": 16, "output_grade": 38}}.
bool parse_ff(const std::string& json, D9Pipeline& p) {
    const size_t at = json.find("\"ff\"");
    if (at == std::string::npos) return false;
    size_t open = json.find('{', at);
    int depth = 0;
    size_t end = open;
    for (; end < json.size(); ++end) {
        if (json[end] == '{') ++depth;
        if (json[end] == '}' && --depth == 0) break;
    }
    const std::string body = json.substr(open, end - open + 1);
    p.ff = true;
    for (int& st : p.ff_slot_stage) st = -1;
    if (body.find("\"disabled\"") != std::string::npos && body.find("true", body.find("\"disabled\"")) != std::string::npos) {
        p.ff_disabled = true;
        return true;
    }
    const size_t stages = body.find("\"stages\"");
    if (stages != std::string::npos) {
        size_t pos = body.find('[', stages) + 1;
        const char* c = body.c_str();
        while (true) {
            const size_t open_inner = body.find('[', pos);
            const size_t close_outer = body.find(']', pos);
            if (open_inner == std::string::npos || open_inner > close_outer) break;
            long v[7] = {};
            char* next = const_cast<char*>(c + open_inner + 1);
            for (long& value : v) {
                value = std::strtol(next, &next, 10);
                if (const char* comma = std::strchr(next, ',')) next = const_cast<char*>(comma) + 1;
            }
            FfStage stage;
            stage.color_op = DWORD(v[0]);
            stage.color_arg1 = DWORD(v[1]);
            stage.color_arg2 = DWORD(v[2]);
            stage.alpha_op = DWORD(v[3]);
            stage.alpha_arg1 = DWORD(v[4]);
            stage.alpha_arg2 = DWORD(v[5]);
            stage.texture_slot = int(v[6]);
            if (stage.texture_slot >= 0 && stage.texture_slot < 16) p.ff_slot_stage[stage.texture_slot] = int(p.ff_stages.size());
            p.ff_stages.push_back(stage);
            pos = body.find(']', open_inner) + 1;
        }
    }
    const auto number = [&](const char* key, float fallback) {
        const size_t k = body.find(key);
        return k == std::string::npos ? fallback : float(std::atof(body.c_str() + body.find(':', k) + 1));
    };
    p.ff_alpha_test = number("\"alpha_test\"", -1.0f);
    p.ff_fog_color = int(number("\"fog_color\"", -1.0f));
    p.ff_output_grade = int(number("\"output_grade\"", -1.0f));
    return true;
}

D3DFORMAT to_d3d(Format f) {
    switch (f) {
        case Format::RGBA8:
        case Format::RGBA8_SRGB: return D3DFMT_A8R8G8B8;
        case Format::RGBA16F: return D3DFMT_A16B16G16R16F;
        case Format::RGB565: return D3DFMT_R5G6B5;
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

// DRAGON_D3D9_PROFILE: time each kind of backend call and log the totals at
// exit, to find where a slow CPU's frame goes (the G41's E3300 is the target).
enum ProfileSlot { PROF_FRAME, PROF_PASS, PROF_PIPELINE, PROF_BUFFERS, PROF_TEXTURES, PROF_UPLOAD, PROF_UNIFORMS,
                   PROF_DRAW, PROF_SLOTS };
const char* const PROFILE_NAMES[PROF_SLOTS] = {"end_frame", "begin_pass", "bind_pipeline", "bind buffers",
                                               "bind_textures", "map/commit", "uniforms", "draw calls"};
struct Profile {
    bool on = false;
    double seconds[PROF_SLOTS] = {};
    uint64_t calls[PROF_SLOTS] = {};
    LARGE_INTEGER frequency = {};
    // Per pipeline: draws and primitives (instances counted), where an SM2
    // card's vertex units go.
    struct Work {
        uint64_t draws = 0;
        uint64_t primitives = 0;
        // Pixels that passed the depth test and were written, from an
        // occlusion query around each draw: where a fill-bound card's pixel
        // shader time goes. Alpha-tested pixels that were killed are not in it.
        uint64_t pixels = 0;
        // Vertices run through software vertex processing: each is sent to
        // the card transformed, and on a PCI card that traffic is the frame.
        uint64_t vertices = 0;
    };
    std::vector<std::pair<std::string, Work>> work;
    uint64_t from = 0;    // the frame counting starts at
    uint64_t frame = 0;   // frames ended so far
    // Before `from`, everything counted so far is dropped at each frame's end.
    void restart() {
        std::fill(std::begin(seconds), std::end(seconds), 0.0);
        std::fill(std::begin(calls), std::end(calls), 0);
        work.clear();
    }
    Work& find(const std::string& name) {
        for (auto& w : work) {
            if (w.first == name) return w.second;
        }
        work.push_back({name, Work{}});
        return work.back().second;
    }
    void count(const std::string& name, uint64_t primitives) {
        Work& w = find(name);
        ++w.draws;
        w.primitives += primitives;
    }
    // Queries issued this frame, read back at its end; spare ones are kept.
    std::vector<std::pair<std::string, IDirect3DQuery9*>> pending;
    std::vector<IDirect3DQuery9*> spare;
};
struct ProfileScope {
    Profile& profile;
    int slot;
    LARGE_INTEGER start = {};
    ProfileScope(Profile& p, int s) : profile(p), slot(s) {
        if (profile.on) QueryPerformanceCounter(&start);
    }
    ~ProfileScope() {
        if (!profile.on) return;
        LARGE_INTEGER end;
        QueryPerformanceCounter(&end);
        profile.seconds[slot] += double(end.QuadPart - start.QuadPart) / double(profile.frequency.QuadPart);
        ++profile.calls[slot];
    }
};
#define PROFILE(slot) ProfileScope profile_scope_(profile_, slot)

class D3D9Device final : public Device {
public:
    bool init(SDL_Window* window, const DeviceConfig& config) {
        headless_ = config.headless;
        adapter_ = config.adapter;
        profile_.on = SDL_getenv("DRAGON_D3D9_PROFILE") != nullptr;
        // DRAGON_D3D9_PROFILE=N (past 1): count from frame N only, for one
        // stretch of a headless run (DRAGON_FRAME_PROFILE=N takes the same).
        if (profile_.on) profile_.from = uint64_t(std::max(0, SDL_atoi(SDL_getenv("DRAGON_D3D9_PROFILE"))));
        QueryPerformanceFrequency(&profile_.frequency);
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
        LOG_INFO("direct3d9: at most %lu primitives a draw, vertex index %lu", caps_.MaxPrimitiveCount,
                 caps_.MaxVertexIndex);
        ff_ = config.fixed_function;
        if (!ff_ && D3DSHADER_VERSION_MAJOR(caps_.PixelShaderVersion) < 2) {
            LOG_ERROR("direct3d9: this backend needs shader model 2 (--tier sm2) or 3, or --tier ff; the card has less");
            return false;
        }
        // The vertex programs are vs_2_0 at least. A card without them (the
        // fixed-function tier's) runs them through D3D9's software vertex
        // processing -- mixed mode where the card has transform and lighting
        // of its own, software alone where it has none (the TNT class).
        // DRAGON_D3D9_SOFTWARE_VP=1 forces that path on a modern card.
        // DRAGON_D3D9_SOFTWARE_VP=1 forces software processing on a modern
        // card (mixed mode where it has T&L), =2 a pure software device,
        // what a card without T&L gets.
        const char* force_sw = SDL_getenv("DRAGON_D3D9_SOFTWARE_VP");
        const bool has_vs2 = D3DSHADER_VERSION_MAJOR(caps_.VertexShaderVersion) >= 2;
        software_vp_ = !has_vs2 || (force_sw && (force_sw[0] == '1' || force_sw[0] == '2'));
        mixed_vp_ = software_vp_ && (caps_.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) && !(force_sw && force_sw[0] == '2');

        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        pp_.Windowed = TRUE;
        pp_.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp_.BackBufferFormat = D3DFMT_X8R8G8B8;
        pp_.BackBufferWidth = UINT(std::max(w, 1));
        pp_.BackBufferHeight = UINT(std::max(h, 1));
        pp_.hDeviceWindow = hwnd_;
        pp_.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
        // DRAGON_D3D9_VSYNC=0: present at once, tearing, instead of on the
        // next refresh -- for measuring a frame's real cost.
        if (const char* vsync = SDL_getenv("DRAGON_D3D9_VSYNC"); vsync && vsync[0] == '0') {
            pp_.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        }
        // Exclusive fullscreen at the window's size: the card scans out the
        // game's own mode, not a 1024x768 desktop, and a present flips
        // rather than copying into the desktop. On the G41's M6, whose
        // memory is the limit, that was 25 -> 19.7 ms at 640x480 Minimal,
        // and a 16-bit mode 18.9 (docs/PORTING.md, "R6, as built").
        color16_ = config.color16;
        fullscreen_ = config.fullscreen && !headless_;
        if (fullscreen_) {
            pp_.Windowed = FALSE;
            if (color16_ && SUCCEEDED(d3d_->CheckDeviceType(adapter_, D3DDEVTYPE_HAL, D3DFMT_R5G6B5, D3DFMT_R5G6B5, FALSE)))
                pp_.BackBufferFormat = D3DFMT_R5G6B5;
            // A borderless window over the mode, or the frame's border
            // and title bar sit on top of the picture.
            SetWindowLongW(hwnd_, GWL_STYLE, LONG(WS_POPUP | WS_VISIBLE));
            SetWindowPos(hwnd_, HWND_TOP, 0, 0, w, h, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
        // Two back buffers: the D3D9 counterpart of the SDL backend's
        // mailbox. With one, a frame that misses a refresh waits for the
        // next, so the X550's 34 ms frames played at 50 (20 fps against the
        // headless benchmark's 29). DRAGON_D3D9_BACKBUFFERS=1 for the old
        // behaviour, to measure the difference.
        pp_.BackBufferCount = 2;
        if (const char* n = SDL_getenv("DRAGON_D3D9_BACKBUFFERS")) pp_.BackBufferCount = UINT(std::clamp(SDL_atoi(n), 1, 3));
        // FPU_PRESERVE: without it D3D9 drops the FPU to single precision for
        // the whole process, under the flight model and the terrain.
        const DWORD processing = !software_vp_ ? D3DCREATE_HARDWARE_VERTEXPROCESSING
                                 : mixed_vp_    ? D3DCREATE_MIXED_VERTEXPROCESSING
                                                : D3DCREATE_SOFTWARE_VERTEXPROCESSING;
        const DWORD flags = processing | D3DCREATE_FPU_PRESERVE;
        HRESULT hr = d3d_->CreateDevice(adapter_, D3DDEVTYPE_HAL, hwnd_, flags, &pp_, &dev_);
        if (FAILED(hr) && fullscreen_) {
            // No such mode (a window size the card cannot display): windowed.
            LOG_WARN("direct3d9: no fullscreen %ux%u mode (0x%08lx); windowed", pp_.BackBufferWidth, pp_.BackBufferHeight,
                     static_cast<unsigned long>(hr));
            fullscreen_ = false;
            pp_.Windowed = TRUE;
            pp_.BackBufferFormat = D3DFMT_X8R8G8B8;
            SetWindowLongW(hwnd_, GWL_STYLE, LONG(WS_OVERLAPPEDWINDOW | WS_VISIBLE));
            SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
            hr = d3d_->CreateDevice(adapter_, D3DDEVTYPE_HAL, hwnd_, flags, &pp_, &dev_);
        }
        if (fullscreen_) {
            LOG_INFO("direct3d9: fullscreen %ux%u, %s", pp_.BackBufferWidth, pp_.BackBufferHeight,
                     pp_.BackBufferFormat == D3DFMT_R5G6B5 ? "16-bit" : "32-bit");
        }
        if (FAILED(hr)) {
            LOG_ERROR("direct3d9: CreateDevice failed (0x%08lx)%s", static_cast<unsigned long>(hr),
                      hr == D3DERR_INVALIDCALL ? " -- no desktop session? (tools/x99/run_interactive.ps1)" : "");
            return false;
        }

        device_defaults();
        if (software_vp_) {
            LOG_INFO("direct3d9: vertex programs in software (%s)", mixed_vp_ ? "mixed mode" : "no hardware T&L");
        }

        // The vertex index a fullscreen triangle reads (VERTEX_ID_INPUT).
        dev_->CreateVertexBuffer(sizeof(float) * 6, D3DUSAGE_WRITEONLY | buffer_usage(), 0, D3DPOOL_MANAGED, &vertex_ids_,
                                 nullptr);
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
        // StretchRect out of a texture is a DirectX 9 driver feature: the
        // M6's Omega driver (a DX7-era design) lacks it, and the world's
        // stretch and the present silently drew nothing there.
        // DRAGON_D3D9_NO_STRETCHRECT=1 takes the quad path on any card.
        stretch_from_textures_ = (caps_.DevCaps2 & D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES) != 0 &&
                                 !(SDL_getenv("DRAGON_D3D9_NO_STRETCHRECT") && SDL_getenv("DRAGON_D3D9_NO_STRETCHRECT")[0] == '1');
        if (!stretch_from_textures_) LOG_INFO("direct3d9: no StretchRect from textures; stretching with a textured quad");
        pow2_only_ = ((caps_.TextureCaps & D3DPTEXTURECAPS_POW2) && !(caps_.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL)) ||
                     SDL_getenv("DRAGON_D3D9_POW2") != nullptr;
        if (pow2_only_) LOG_INFO("direct3d9: textures at powers of two only; targets padded to them");
        r32f_filterable_ = SUCCEEDED(d3d_->CheckDeviceFormat(adapter_, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8,
                                                             D3DUSAGE_QUERY_FILTER, D3DRTYPE_TEXTURE, D3DFMT_R32F));
        LOG_INFO("direct3d9: R32F shadow map %s", r32f_filterable_ ? "filtered" : "point-sampled (not filterable)");
        // Instancing (the foliage): SM3 hardware has stream frequencies. ATI's
        // SM2 parts (R300 on) expose the same thing behind a FOURCC switch,
        // 'INST', turned on through D3DRS_POINTSIZE. Anything else draws an
        // instanced batch one instance at a time.
        const DWORD inst = MAKEFOURCC('I', 'N', 'S', 'T');
        if (software_vp_) {
            // Software vertex processing has no stream frequencies.
            instancing_ = Instancing::Loop;
        } else if (D3DSHADER_VERSION_MAJOR(caps_.VertexShaderVersion) >= 3) {
            instancing_ = Instancing::Native;
        } else if (SUCCEEDED(d3d_->CheckDeviceFormat(adapter_, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0,
                                                     D3DRTYPE_SURFACE, D3DFORMAT(inst)))) {
            instancing_ = Instancing::AtiInst;
            dev_->SetRenderState(D3DRS_POINTSIZE, inst);  // past the cache; again after a reset
        } else {
            instancing_ = Instancing::Loop;
        }
        LOG_INFO("direct3d9: instancing %s", instancing_ == Instancing::Native  ? "native (SM3)"
                                              : instancing_ == Instancing::AtiInst ? "through ATI's INST switch"
                                                                                   : "emulated, a draw per instance");
        return vertex_ids_ && vertex_id_decl_;
    }

    ~D3D9Device() override {
        if (profile_.on && profile_.calls[PROF_FRAME]) {
            const double frames = double(profile_.calls[PROF_FRAME]);
            for (int i = 0; i < PROF_SLOTS; ++i) {
                LOG_INFO("direct3d9 profile: %-14s %7.3f ms/frame, %6.0f calls/frame", PROFILE_NAMES[i],
                         profile_.seconds[i] * 1000.0 / frames, double(profile_.calls[i]) / frames);
            }
            for (const auto& w : profile_.work) {
                LOG_INFO("direct3d9 work: %-22s %7.1f draws/frame, %9.0f primitives/frame, %9.0f pixels/frame, %8.0f vertices/frame",
                         w.first.c_str(), double(w.second.draws) / frames, double(w.second.primitives) / frames,
                         double(w.second.pixels) / frames, double(w.second.vertices) / frames);
            }
        }
        for (IDirect3DQuery9* q : profile_.spare) q->Release();
        release(backbuffer_.surface);
        release(vertex_id_decl_);
        release(vertex_ids_);
        release(dev_);
        release(d3d_);
    }

    IDirect3DDevice9* device() const { return dev_; }

    // What this backend sets once and a reset puts back to D3D9's defaults.
    void device_defaults() {
        if (mixed_vp_) dev_->SetSoftwareVertexProcessing(TRUE);
        if (instancing_ == Instancing::AtiInst) dev_->SetRenderState(D3DRS_POINTSIZE, MAKEFOURCC('I', 'N', 'S', 'T'));
        // 16-bit colour bands a sky gradient into stripes without it.
        if (color16_) dev_->SetRenderState(D3DRS_DITHERENABLE, TRUE);
    }

    // A render target's D3D9 objects, from its description: at creation,
    // and again after a reset.
    HRESULT create_target(D9Texture* t) {
        // A card that takes only power-of-two textures (D3DPTEXTURECAPS_POW2
        // without NONPOW2CONDITIONAL: the DX6 and DX7 parts, a Rage XL among
        // them) refused the 320x240 world target outright. The target is
        // made at the next powers of two and drawn into at its corner; a
        // depth surface likewise, as D3D9 wants it no smaller than the
        // colour target it is used with.
        const auto up = [&](uint32_t v) {
            if (!pow2_only_) return v;
            uint32_t p = 1;
            while (p < v) p <<= 1;
            return p;
        };
        t->alloc_w = up(t->width);
        t->alloc_h = up(t->height);
        if (t->usage & TEXTURE_DEPTH_TARGET) {
            HRESULT hr = dev_->CreateDepthStencilSurface(t->alloc_w, t->alloc_h, D3DFMT_D24X8, D3DMULTISAMPLE_NONE, 0, TRUE,
                                                         &t->depth, nullptr);
            if (SUCCEEDED(hr) && t->depth_as_color) {
                hr = dev_->CreateTexture(t->alloc_w, t->alloc_h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT,
                                         &t->texture, nullptr);
            }
            return hr;
        }
        // DRAGON_D3D9_NO_RT_TEXTURES=1: as a card that cannot render into a
        // texture at all (gfx::Device then draws straight into the back buffer).
        if (no_rt_textures_) return D3DERR_NOTAVAILABLE;
        return dev_->CreateTexture(t->alloc_w, t->alloc_h, 1, D3DUSAGE_RENDERTARGET, to_d3d(t->format), D3DPOOL_DEFAULT,
                                   &t->texture, nullptr);
    }

    HRESULT create_vertex_buffer(D9Buffer* b) {
        const DWORD usage = (software_vp_ ? 0 : D3DUSAGE_WRITEONLY) | buffer_usage() | (b->dynamic ? D3DUSAGE_DYNAMIC : 0);
        return dev_->CreateVertexBuffer(b->size, usage, 0, buffer_pool(b->dynamic), &b->vb, nullptr);
    }
    // Software vertex processing reads the buffers with the CPU, and a
    // D3DPOOL_DEFAULT buffer can sit in the card's memory, across a PCI bus
    // the CPU reads at a few MB/s: system memory then. (Measured on the
    // G41's M6, it made no difference, static buffers included -- that
    // driver keeps them in system memory already -- but it is what the
    // runtime documents, and a system-memory buffer survives a reset.)
    D3DPOOL buffer_pool(bool dynamic) const {
        if (software_vp_ && dynamic) return D3DPOOL_SYSTEMMEM;
        return dynamic ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED;
    }

    void fill_dynamic(D9Buffer* b, uint32_t size) {
        void* mapped = nullptr;
        if (!b->vb || size == 0 || FAILED(b->vb->Lock(0, size, &mapped, D3DLOCK_DISCARD))) return;
        std::memcpy(mapped, b->shadow.data(), size);
        b->vb->Unlock();
    }

    void set_reset_hooks(void (*before)(), void (*after)()) {
        before_reset_ = before;
        after_reset_ = after;
    }
    // Reset needs every D3DPOOL_DEFAULT resource released first: the render
    // targets and the dynamic buffers, which are made again afterwards from
    // what this backend kept of them. A target's contents are redrawn by
    // the next frame; a buffer's are copied back from its shadow. The
    // device's state is back at D3D9's defaults, so the caches forget theirs.
    bool reset() {
        if (before_reset_) before_reset_();
        release(backbuffer_.surface);
        for (D9Texture* t : targets_) {
            release(t->texture);
            release(t->depth);
        }
        for (D9Buffer* b : dynamic_) release(b->vb);
        for (auto& [name, query] : profile_.pending) query->Release();
        for (IDirect3DQuery9* query : profile_.spare) query->Release();
        profile_.pending.clear();
        profile_.spare.clear();
        const HRESULT hr = dev_->Reset(&pp_);
        if (FAILED(hr)) {  // still lost: again next frame
            if (after_reset_) after_reset_();
            return false;
        }
        for (D9Texture* t : targets_) {
            if (FAILED(create_target(t))) LOG_ERROR("direct3d9: a %ux%u target failed after a reset", t->width, t->height);
        }
        for (D9Buffer* b : dynamic_) {
            if (SUCCEEDED(create_vertex_buffer(b))) fill_dynamic(b, b->mapped);
        }
        device_defaults();
        forget_state();
        for (BufferBinding& s : streams_) s = {};
        if (after_reset_) after_reset_();
        LOG_INFO("direct3d9: device reset (%zu targets, %zu dynamic buffers)", targets_.size(), dynamic_.size());
        return true;
    }

    // Everything the caches believe about device state, forgotten: the next
    // bind and draw set it all again.
    void forget_state() {
        std::fill(std::begin(render_state_known_), std::end(render_state_known_), false);
        for (auto& stage : sampler_state_known_) std::fill(std::begin(stage), std::end(stage), false);
        for (auto& stage : stage_states_) stage.clear();
        pipeline_ = nullptr;
        bound_index_ = nullptr;
        uniforms_dirty_[0] = uniforms_dirty_[1] = 0xF;
        // The states ImGui sets that no pipeline here does, back to D3D9's
        // defaults -- the scissor above all, or the next frame's world is
        // clipped to the last panel's rectangle.
        dev_->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        dev_->SetRenderState(D3DRS_CLIPPING, TRUE);
        dev_->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
        dev_->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
        dev_->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    }

    Backend backend() const override { return Backend::Direct3D9; }
    uint32_t max_texture_size() const override { return uint32_t(caps_.MaxTextureWidth); }
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
        // A fullscreen device is lost when it loses the focus (Alt-Tab) and
        // stays lost until the window is back in front; a windowed one, on
        // a mode change or a locked workstation. Wait, then reset.
        const HRESULT coop = dev_->TestCooperativeLevel();
        if (coop == D3DERR_DEVICELOST || (coop == D3DERR_DEVICENOTRESET && !reset())) {
            Sleep(50);
            return FrameStatus::Skip;
        }
        if (!headless_) {
            RECT rect = {};
            GetClientRect(hwnd_, &rect);
            const uint32_t w = uint32_t(std::max<LONG>(rect.right - rect.left, 0));
            const uint32_t h = uint32_t(std::max<LONG>(rect.bottom - rect.top, 0));
            if (w == 0 || h == 0) return FrameStatus::Skip;
            present_w_ = w;
            present_h_ = h;
            if (present_width) *present_width = w;
            if (present_height) *present_height = h;
        }
        dev_->BeginScene();
        in_scene_ = true;
        // The back buffer as this frame's frame_target(), when it is the
        // window's size (the device is not reset on a resize, so after one
        // it is not, and the UI goes through its own target again).
        release(backbuffer_.surface);
        if (!headless_ && pp_.BackBufferWidth == present_w_ && pp_.BackBufferHeight == present_h_ &&
            SUCCEEDED(dev_->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer_.surface))) {
            backbuffer_.width = pp_.BackBufferWidth;
            backbuffer_.height = pp_.BackBufferHeight;
            backbuffer_.format = Format::RGBA8;
            backbuffer_.usage = TEXTURE_COLOR_TARGET;
        }
        return FrameStatus::Ready;
    }

    Texture* frame_target() override {
        return backbuffer_.surface ? reinterpret_cast<Texture*>(&backbuffer_) : nullptr;
    }

    void end_frame(Texture* source, uint32_t width, uint32_t height, std::vector<uint8_t>* readback) override {
        PROFILE(PROF_FRAME);
        if (!in_scene_) return;
        D9Texture* src = source ? d9(source) : nullptr;
        IDirect3DSurface9* src_surface = nullptr;
        if (src && src->texture) src->texture->GetSurfaceLevel(0, &src_surface);
        // Drawn straight into the back buffer: nothing to copy.
        if (src && src == &backbuffer_) src_surface = nullptr;
        if (src_surface && !headless_) {
            IDirect3DSurface9* back = nullptr;
            if (SUCCEEDED(dev_->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back))) {
                RECT rect = {0, 0, LONG(width), LONG(height)};
                if (!stretch_from_textures_ || !stretch_ok(dev_->StretchRect(src_surface, &rect, back, nullptr, D3DTEXF_LINEAR))) {
                    D3DSURFACE_DESC bd = {};
                    back->GetDesc(&bd);
                    quad_stretch(src, width, height, back, bd.Width, bd.Height);
                }
                back->Release();
            }
        }
        dev_->EndScene();
        in_scene_ = false;
        if (profile_.on) {
            collect_pixel_queries();
            if (++profile_.frame < profile_.from) profile_.restart();
        }
        if (readback && src_surface) download(src_surface, width, height, *readback);
        if (src_surface) src_surface->Release();
        release(backbuffer_.surface);
        if (!headless_) dev_->Present(nullptr, nullptr, nullptr, nullptr);
        else wait_idle();
    }

    void blit(Texture* source, uint32_t sw, uint32_t sh, Texture* dest, uint32_t dw, uint32_t dh) override {
        D9Texture* s = source ? d9(source) : nullptr;
        D9Texture* d = dest ? d9(dest) : nullptr;
        if (!s || !d || !s->texture || (!d->texture && !d->surface)) return;
        IDirect3DSurface9* from = nullptr;
        s->texture->GetSurfaceLevel(0, &from);
        IDirect3DSurface9* to = target_surface(d);
        if (from && to) {
            RECT src_rect = {0, 0, LONG(sw), LONG(sh)};
            RECT dst_rect = {0, 0, LONG(dw), LONG(dh)};
            if (!stretch_from_textures_ || !stretch_ok(dev_->StretchRect(from, &src_rect, to, &dst_rect, D3DTEXF_LINEAR)))
                quad_stretch(s, sw, sh, to, dw, dh);
        }
        release(from);
        release(to);
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
            track(buffer, Kind::Buffer, size);
            return reinterpret_cast<Buffer*>(buffer);
        }
        if (buffer->dynamic) {
            buffer->shadow.assign(size, 0);
            dynamic_.push_back(buffer);
        }
        if (FAILED(create_vertex_buffer(buffer))) {
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
        track(buffer, Kind::Buffer, size);
        return reinterpret_cast<Buffer*>(buffer);
    }

    void destroy(Buffer* buffer) override {
        if (buffer && d9(buffer) == bound_index_) bound_index_ = nullptr;
        if (!buffer) return;
        D9Buffer* b = d9(buffer);
        untrack(b);
        dynamic_.erase(std::remove(dynamic_.begin(), dynamic_.end(), b), dynamic_.end());
        release(b->vb);
        release(b->ib);
        delete b;
    }

    void* map_upload(Buffer* buffer, uint32_t size) override {
        PROFILE(PROF_UPLOAD);
        if (!buffer || size == 0) return nullptr;
        D9Buffer* b = d9(buffer);
        if (b->usage == BufferUsage::Index) {
            if (b->index_data.size() < size) b->index_data.resize(size);
            release(b->ib);  // rebuilt at the next bind
            return b->index_data.data();
        }
        if (b->dynamic) {
            if (size > b->shadow.size()) return nullptr;
            b->mapped = size;
            return b->shadow.data();
        }
        void* mapped = nullptr;
        if (!b->vb || FAILED(b->vb->Lock(0, size, &mapped, 0))) return nullptr;
        return mapped;
    }

    void commit_upload(Buffer* buffer, uint32_t) override {
        PROFILE(PROF_UPLOAD);
        if (!buffer) return;
        D9Buffer* b = d9(buffer);
        if (b->dynamic) {
            fill_dynamic(b, b->mapped);
            return;
        }
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
        if (desc.usage & (TEXTURE_DEPTH_TARGET | TEXTURE_COLOR_TARGET)) {
            t->depth_as_color = (desc.usage & TEXTURE_DEPTH_TARGET) && (desc.usage & TEXTURE_SAMPLED);
            t->levels = 1;
            hr = create_target(t);
            targets_.push_back(t);
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
        track(t, Kind::Texture, texture_bytes(desc));
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
        untrack(t);
        targets_.erase(std::remove(targets_.begin(), targets_.end(), t), targets_.end());
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
        const std::string ps_code = ff_ ? std::string() : read_file(base + ".ps30");
        const std::string json = read_file(base + ".d3d9.json");
        auto* p = new D9Pipeline;
        p->desc = d;
        if (ff_ && !json.empty() && parse_ff(json, *p) && p->ff_disabled) {
            // A pass the tier never draws: a pipeline that draws nothing.
            track(p, Kind::Pipeline, 0);
            return reinterpret_cast<Pipeline*>(p);
        }
        if (vs_code.empty() || (!ff_ && ps_code.empty()) || json.empty() || (ff_ && !p->ff)) {
            LOG_ERROR("[%s] no D3D9 shader at %s.{vs30,%sd3d9.json} (tools/d3d9/bake_d3d9.py%s)", d.name.c_str(),
                      base.c_str(), ff_ ? "" : "ps30,", ff_ ? " --tier ff" : "");
            delete p;
            return nullptr;
        }
        p->vertex = parse_layout(json, "vertex");
        p->fragment = parse_layout(json, "fragment");
        if (FAILED(dev_->CreateVertexShader(reinterpret_cast<const DWORD*>(vs_code.data()), &p->vs)) ||
            (!ff_ && FAILED(dev_->CreatePixelShader(reinterpret_cast<const DWORD*>(ps_code.data()), &p->ps)))) {
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
        track(p, Kind::Pipeline, 0);
        return reinterpret_cast<Pipeline*>(p);
    }

    void destroy(Pipeline* pipeline) override {
        if (!pipeline) return;
        D9Pipeline* p = d9(pipeline);
        if (pipeline_ == p) pipeline_ = nullptr;
        untrack(p);
        release(p->vs);
        release(p->ps);
        release(p->decl);
        delete p;
    }

    bool compiles_hlsl() const override { return false; }

    uint64_t driver_available() const override { return dev_ ? uint64_t(dev_->GetAvailableTextureMem()) : 0; }

    std::vector<std::string> baked_shader_files(const ShaderSource& shaders) const override {
        const std::string base = baked_base(shaders);
        return {base + ".vs30", base + ".ps30"};
    }

    // ---- passes
    Pass* begin_pass(const PassDesc& desc) override {
        PROFILE(PROF_PASS);
        D9Texture* color = desc.color ? d9(desc.color) : nullptr;
        D9Texture* depth = desc.depth ? d9(desc.depth) : nullptr;
        // A depth-only pass on a sampled depth target draws into its R32F
        // stand-in (D9Texture::depth_as_color).
        D9Texture* target = color ? color : (depth && depth->depth_as_color ? depth : nullptr);
        IDirect3DSurface9* surface = target ? target_surface(target) : nullptr;
        if (!surface) {
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
        PROFILE(PROF_PIPELINE);
        D9Pipeline* p = d9(pipeline);
        if (!p) return;
        pipeline_ = p;
        if (p->ff_disabled) return;
        const PipelineDesc& d = p->desc;
        dev_->SetVertexShader(p->vs);
        dev_->SetPixelShader(p->ps);  // null on the fixed-function tier: the stages below finish the pixel
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
        set_render_state(D3DRS_CULLMODE, cull);
        set_render_state(D3DRS_FILLMODE, d.fill == Fill::Wireframe ? D3DFILL_WIREFRAME : D3DFILL_SOLID);
        const bool test = d.depth_test && has_depth_;
        set_render_state(D3DRS_ZENABLE, test || (d.depth_write && has_depth_) ? D3DZB_TRUE : D3DZB_FALSE);
        set_render_state(D3DRS_ZFUNC, test ? to_d3d(d.depth_compare) : D3DCMP_ALWAYS);
        set_render_state(D3DRS_ZWRITEENABLE, d.depth_write && has_depth_ ? TRUE : FALSE);
        set_render_state(D3DRS_ALPHABLENDENABLE, d.blend != Blend::Opaque);
        if (d.blend == Blend::Additive) {
            set_render_state(D3DRS_SRCBLEND, D3DBLEND_ONE);
            set_render_state(D3DRS_DESTBLEND, D3DBLEND_ONE);
            set_render_state(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
        } else if (d.blend == Blend::Alpha) {
            set_render_state(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            set_render_state(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            set_render_state(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
            set_render_state(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
            set_render_state(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA);
        }
        set_render_state(D3DRS_BLENDOP, D3DBLENDOP_ADD);
        set_render_state(D3DRS_LIGHTING, FALSE);
        set_render_state(D3DRS_COLORWRITEENABLE, 0xF);
        if (p->ff) {
            // The recipe's stages, then the first unused one disabled, which
            // ends the cascade. Every stage reads texture coordinate set 0:
            // the vertex program writes one.
            DWORD stage = 0;
            for (const FfStage& st : p->ff_stages) {
                set_stage_state(stage, D3DTSS_COLOROP, st.color_op);
                set_stage_state(stage, D3DTSS_COLORARG1, st.color_arg1);
                set_stage_state(stage, D3DTSS_COLORARG2, st.color_arg2);
                set_stage_state(stage, D3DTSS_ALPHAOP, st.alpha_op);
                set_stage_state(stage, D3DTSS_ALPHAARG1, st.alpha_arg1);
                set_stage_state(stage, D3DTSS_ALPHAARG2, st.alpha_arg2);
                set_stage_state(stage, D3DTSS_TEXCOORDINDEX, 0);
                ++stage;
            }
            set_stage_state(stage, D3DTSS_COLOROP, D3DTOP_DISABLE);
            set_stage_state(stage, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
            // The vertex program's oFog is the factor (D3DRS_FOGTABLEMODE
            // none); the colour comes with the scene block (flush).
            set_render_state(D3DRS_FOGENABLE, TRUE);
            set_render_state(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
            set_render_state(D3DRS_FOGVERTEXMODE, D3DFOG_NONE);
            set_render_state(D3DRS_RANGEFOGENABLE, FALSE);
            set_render_state(D3DRS_ALPHATESTENABLE, p->ff_alpha_test >= 0.0f);
            if (p->ff_alpha_test >= 0.0f) {
                set_render_state(D3DRS_ALPHAREF, DWORD(std::lround(p->ff_alpha_test * 255.0f)));
                set_render_state(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
            }
        } else {
            set_render_state(D3DRS_FOGENABLE, FALSE);
            set_render_state(D3DRS_ALPHATESTENABLE, FALSE);
        }
        uniforms_dirty_[0] = uniforms_dirty_[1] = 0xF;  // a new layout: push every block again
    }

    void bind_vertex_buffers(Pass*, uint32_t first_slot, const BufferBinding* bindings, uint32_t count) override {
        PROFILE(PROF_BUFFERS);
        for (uint32_t i = 0; i < count && first_slot + i < 8; ++i) {
            const uint32_t slot = first_slot + i;
            streams_[slot] = bindings[i];
            D9Buffer* b = bindings[i].buffer ? d9(bindings[i].buffer) : nullptr;
            const uint32_t pitch = pipeline_ ? pipeline_->stream_pitch[slot] : 0;
            dev_->SetStreamSource(slot, b ? b->vb : nullptr, bindings[i].offset, pitch);
        }
    }

    void bind_index_buffer(Pass*, const BufferBinding& binding, IndexSize size) override {
        PROFILE(PROF_BUFFERS);
        D9Buffer* b = binding.buffer ? d9(binding.buffer) : nullptr;
        if (!b) return;
        if (!b->ib || b->ib_size != size) {
            release(b->ib);
            const D3DFORMAT format = size == IndexSize::U32 ? D3DFMT_INDEX32 : D3DFMT_INDEX16;
            const UINT bytes = UINT(b->index_data.size());
            if (FAILED(dev_->CreateIndexBuffer(bytes, (software_vp_ ? 0 : D3DUSAGE_WRITEONLY) | buffer_usage(), format, buffer_pool(false), &b->ib,
                                               nullptr)))
                return;
            void* mapped = nullptr;
            if (SUCCEEDED(b->ib->Lock(0, bytes, &mapped, 0))) {
                std::memcpy(mapped, b->index_data.data(), bytes);
                b->ib->Unlock();
            }
            b->ib_size = size;
        }
        index_offset_ = binding.offset / (size == IndexSize::U32 ? 4 : 2);
        bound_index_ = b;
        dev_->SetIndices(b->ib);
    }

    void bind_fragment_textures(Pass*, uint32_t first_slot, const TextureBinding* bindings, uint32_t count) override {
        PROFILE(PROF_TEXTURES);
        for (uint32_t i = 0; i < count && first_slot + i < 16; ++i) {
            DWORD stage = first_slot + i;
            if (pipeline_ && pipeline_->ff) {
                // A fixed-function recipe samples its slots from its own
                // stages; a slot no stage reads is not bound at all.
                const int mapped = pipeline_->ff_slot_stage[stage];
                if (mapped < 0) continue;
                stage = DWORD(mapped);
            }
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
            set_sampler_state(stage, D3DSAMP_MINFILTER, filter(sd.min_filter));
            set_sampler_state(stage, D3DSAMP_MAGFILTER, point_only ? D3DTEXF_POINT
                                                                         : (sd.mag_filter == Filter::Linear ? D3DTEXF_LINEAR
                                                                                                            : D3DTEXF_POINT));
            const bool mips = t && t->levels > 1 && sd.max_lod > 0.0f;
            set_sampler_state(stage, D3DSAMP_MIPFILTER,
                                  !mips ? D3DTEXF_NONE : sd.mip_mode == MipMode::Linear ? D3DTEXF_LINEAR : D3DTEXF_POINT);
            set_sampler_state(stage, D3DSAMP_MAXANISOTROPY,
                                  aniso ? DWORD(std::min<float>(sd.max_anisotropy, float(caps_.MaxAnisotropy))) : 1);
            set_sampler_state(stage, D3DSAMP_ADDRESSU, to_d3d(sd.address_u));
            set_sampler_state(stage, D3DSAMP_ADDRESSV, to_d3d(sd.address_v));
            set_sampler_state(stage, D3DSAMP_ADDRESSW, to_d3d(sd.address_w));
            set_sampler_state(stage, D3DSAMP_SRGBTEXTURE, t && t->srgb() ? TRUE : FALSE);
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
        if (pipeline_ && pipeline_->ff_disabled) return;
        if (!flush()) return;
        UINT prims = 0;
        const D3DPRIMITIVETYPE type = primitive(vertex_count, &prims);
        if (profile_.on) {
            profile_.count(pipeline_->desc.name, prims);
            profile_.find(pipeline_->desc.name).vertices += vertex_count;
        }
        if (!prims) return;
        IDirect3DQuery9* query = begin_pixel_query();
        {
            PROFILE(PROF_DRAW);
            dev_->DrawPrimitive(type, first_vertex, prims);
        }
        end_pixel_query(query);
    }

    void draw_indexed(Pass*, uint32_t index_count, uint32_t instance_count, uint32_t first_index,
                      int32_t vertex_offset, uint32_t first_instance) override {
        if (pipeline_ && pipeline_->ff_disabled) return;
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
        if (profile_.on) profile_.count(pipeline_->desc.name, uint64_t(prims) * (instanced ? instance_count : 1));
        IDirect3DQuery9* query = begin_pixel_query();
        struct EndQuery {
            D3D9Device* self;
            IDirect3DQuery9* query;
            ~EndQuery() { self->end_pixel_query(query); }
        } end_query{this, query};
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
                draw_indexed_chunked(type, vertex_offset, vertices, first_index + index_offset_, prims);
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
        draw_indexed_chunked(type, vertex_offset, vertices, first_index + index_offset_, prims);
        if (instanced) {
            for (uint32_t s = 0; s < 8; ++s) dev_->SetStreamSourceFreq(s, 1);
        }
    }

private:
    // A colour target's surface, with a reference: a texture's top level, or
    // the back buffer's own surface.
    IDirect3DSurface9* target_surface(D9Texture* t) {
        IDirect3DSurface9* surface = nullptr;
        if (t->surface) {
            surface = t->surface;
            surface->AddRef();
        } else if (t->texture) {
            t->texture->GetSurfaceLevel(0, &surface);
        }
        return surface;
    }

    // A driver can claim D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES and still
    // fail the call: the M6's Omega 2.6.87 on XP does, and the frame was
    // uninitialised memory under the HUD. The first failure turns the quad
    // path on for good.
    bool stretch_ok(HRESULT hr) {
        if (SUCCEEDED(hr)) return true;
        LOG_WARN("direct3d9: StretchRect failed (0x%08lx) although the caps allow it; stretching with a textured quad",
                 static_cast<unsigned long>(hr));
        stretch_from_textures_ = false;
        return false;
    }

    // StretchRect's job on a card whose driver cannot stretch out of a
    // texture: the source's top-left sw x sh drawn over all of `target` as
    // one pre-transformed, textured quad through fixed function, which every
    // D3D9 device has. The state it sets goes through the caches, and the
    // next bind_pipeline restores shaders, declaration and streams.
    void quad_stretch(D9Texture* source, uint32_t sw, uint32_t sh, IDirect3DSurface9* target, uint32_t dw, uint32_t dh) {
        if (!source || !source->texture || !target || !dw || !dh) return;
        dev_->SetRenderTarget(0, target);
        dev_->SetDepthStencilSurface(nullptr);
        D3DVIEWPORT9 viewport = {0, 0, dw, dh, 0.0f, 1.0f};
        dev_->SetViewport(&viewport);
        dev_->SetVertexShader(nullptr);
        dev_->SetPixelShader(nullptr);
        dev_->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
        set_render_state(D3DRS_ZENABLE, D3DZB_FALSE);
        set_render_state(D3DRS_ZWRITEENABLE, FALSE);
        set_render_state(D3DRS_CULLMODE, D3DCULL_NONE);
        set_render_state(D3DRS_FILLMODE, D3DFILL_SOLID);
        set_render_state(D3DRS_ALPHABLENDENABLE, FALSE);
        set_render_state(D3DRS_ALPHATESTENABLE, FALSE);
        set_render_state(D3DRS_FOGENABLE, FALSE);
        set_render_state(D3DRS_LIGHTING, FALSE);
        set_render_state(D3DRS_COLORWRITEENABLE, 0xF);
        set_stage_state(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        set_stage_state(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        set_stage_state(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        set_stage_state(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        set_stage_state(0, D3DTSS_TEXCOORDINDEX, 0);
        set_stage_state(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        set_stage_state(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        dev_->SetTexture(0, source->texture);
        set_sampler_state(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        set_sampler_state(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        set_sampler_state(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        set_sampler_state(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        set_sampler_state(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        set_sampler_state(0, D3DSAMP_SRGBTEXTURE, FALSE);
        // Pixel centres: D3D9 samples at the texel's corner, hence the half
        // pixel taken off the positions.
        const float u = float(sw) / float(source->alloc_w ? source->alloc_w : source->width);
        const float v = float(sh) / float(source->alloc_h ? source->alloc_h : source->height);
        const float x1 = float(dw) - 0.5f, y1 = float(dh) - 0.5f;
        struct Vertex {
            float x, y, z, rhw, u, v;
        } quad[4] = {{-0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f},
                     {x1, -0.5f, 0.0f, 1.0f, u, 0.0f},
                     {-0.5f, y1, 0.0f, 1.0f, 0.0f, v},
                     {x1, y1, 0.0f, 1.0f, u, v}};
        dev_->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex));
        dev_->SetTexture(0, nullptr);
        pipeline_ = nullptr;
    }

    // DRAGON_D3D9_PROFILE's pixel count: an occlusion query around one draw.
    IDirect3DQuery9* begin_pixel_query() {
        if (!profile_.on || !pipeline_) return nullptr;
        IDirect3DQuery9* query = nullptr;
        if (!profile_.spare.empty()) {
            query = profile_.spare.back();
            profile_.spare.pop_back();
        } else if (FAILED(dev_->CreateQuery(D3DQUERYTYPE_OCCLUSION, &query))) {
            return nullptr;
        }
        query->Issue(D3DISSUE_BEGIN);
        return query;
    }
    void end_pixel_query(IDirect3DQuery9* query) {
        if (!query) return;
        query->Issue(D3DISSUE_END);
        profile_.pending.push_back({pipeline_->desc.name, query});
    }
    void collect_pixel_queries() {
        for (auto& [name, query] : profile_.pending) {
            DWORD pixels = 0;
            while (query->GetData(&pixels, sizeof pixels, D3DGETDATA_FLUSH) == S_FALSE) SwitchToThread();
            profile_.find(name).pixels += pixels;
            profile_.spare.push_back(query);
        }
        profile_.pending.clear();
    }

    // D3D9 passes every Set call to the driver, redundant or not, and on a
    // 2005 CPU those calls are the frame. A cache drops the repeats.
    void set_render_state(D3DRENDERSTATETYPE state, DWORD value) {
        if (state < 256) {
            if (render_state_known_[state] && render_state_[state] == value) return;
            render_state_known_[state] = true;
            render_state_[state] = value;
        }
        dev_->SetRenderState(state, value);
    }
    void set_stage_state(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value) {
        if (stage >= 8) return;
        auto& cache = stage_states_[stage];
        auto it = cache.find(type);
        if (it != cache.end() && it->second == value) return;
        cache[type] = value;
        dev_->SetTextureStageState(stage, type, value);
    }
    // Buffers software vertex processing reads must say so at creation.
    DWORD buffer_usage() const { return software_vp_ ? D3DUSAGE_SOFTWAREPROCESSING : 0; }

    void set_sampler_state(DWORD stage, D3DSAMPLERSTATETYPE type, DWORD value) {
        if (stage < 16 && type < 16) {
            if (sampler_state_known_[stage][type] && sampler_state_[stage][type] == value) return;
            sampler_state_known_[stage][type] = true;
            sampler_state_[stage][type] = value;
        }
        dev_->SetSamplerState(stage, type, value);
    }
    DWORD render_state_[256] = {};
    bool render_state_known_[256] = {};
    DWORD sampler_state_[16][16] = {};
    bool sampler_state_known_[16][16] = {};

    // A card caps the primitives one draw may carry (MaxPrimitiveCount: ATI's
    // R300 and R400 parts allow 1,048,575, and the terrain is 1.39 million
    // triangles), so a bigger list draw is issued in pieces.
    void draw_indexed_chunked(D3DPRIMITIVETYPE type, INT base_vertex, UINT vertices, UINT start_index, UINT prims) {
        PROFILE(PROF_DRAW);
        // The vertex range the draw touches. Hardware ignores it, but D3D9's
        // software vertex pipeline runs the vertex program over every vertex
        // in it: with the whole terrain buffer named for each of its 264
        // chunks, a frame transformed eleven million vertices and took 35 s.
        // So under software processing the range is the indices' real
        // min..max, read once from the index data the backend keeps and
        // remembered per draw range.
        UINT min_index = 0;
        if (software_vp_ && bound_index_) {
            const uint64_t key = (uint64_t(start_index) << 32) ^ uint64_t(prims) ^ (uint64_t(reinterpret_cast<uintptr_t>(bound_index_)) << 7);
            auto it = index_ranges_.find(key);
            if (it == index_ranges_.end()) {
                const UINT per_prim_all = type == D3DPT_TRIANGLELIST ? 3 : type == D3DPT_LINELIST ? 2 : prims ? (type == D3DPT_TRIANGLESTRIP ? prims + 2 : 1) : 1;
                const size_t count = type == D3DPT_TRIANGLESTRIP ? size_t(prims) + 2 : size_t(prims) * per_prim_all;
                uint32_t lo = UINT32_MAX, hi = 0;
                const std::vector<uint8_t>& data = bound_index_->index_data;
                if (bound_index_->ib_size == IndexSize::U32) {
                    const uint32_t* idx = reinterpret_cast<const uint32_t*>(data.data());
                    const size_t n = data.size() / 4;
                    for (size_t i = start_index; i < start_index + count && i < n; ++i) { lo = std::min(lo, idx[i]); hi = std::max(hi, idx[i]); }
                } else {
                    const uint16_t* idx = reinterpret_cast<const uint16_t*>(data.data());
                    const size_t n = data.size() / 2;
                    for (size_t i = start_index; i < start_index + count && i < n; ++i) { lo = std::min<uint32_t>(lo, idx[i]); hi = std::max<uint32_t>(hi, idx[i]); }
                }
                if (lo == UINT32_MAX) { lo = 0; hi = vertices ? vertices - 1 : 0; }
                it = index_ranges_.emplace(key, std::make_pair(lo, hi)).first;
            }
            min_index = it->second.first;
            vertices = it->second.second - it->second.first + 1;
        }
        if (profile_.on && pipeline_) profile_.find(pipeline_->desc.name).vertices += vertices;
        const UINT limit = caps_.MaxPrimitiveCount ? caps_.MaxPrimitiveCount : prims;
        const UINT per_prim = type == D3DPT_TRIANGLELIST ? 3 : type == D3DPT_LINELIST ? 2 : 0;
        if (prims <= limit || per_prim == 0) {
            dev_->DrawIndexedPrimitive(type, base_vertex, min_index, vertices, start_index, prims);
            return;
        }
        for (UINT done = 0; done < prims; done += limit) {
            dev_->DrawIndexedPrimitive(type, base_vertex, min_index, vertices, start_index + done * per_prim,
                                       std::min(limit, prims - done));
        }
    }

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
        PROFILE(PROF_UNIFORMS);
        if (!pipeline_) return false;
        if (pipeline_->ff && pipeline_->ff_fog_color >= 0 && (uniforms_dirty_[0] & 1u)) {
            // D3DRS_FOGCOLOR: the scene's fog colour through the tier's
            // display encode (scene_common.hlsl, ff_finish): exposure, gamma.
            const std::vector<float>& scene = uniforms_[0][0];
            const size_t at = size_t(pipeline_->ff_fog_color) * 4;
            if (at + 3 < scene.size()) {
                const size_t grade = size_t(std::max(pipeline_->ff_output_grade, 0)) * 4;
                const float exposure = pipeline_->ff_output_grade >= 0 && grade < scene.size() ? scene[grade] : 1.0f;
                float rgba[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                for (int c = 0; c < 3; ++c) rgba[c] = std::pow(std::clamp(scene[at + size_t(c)] * exposure, 0.0f, 1.0f), 1.0f / 2.2f);
                set_render_state(D3DRS_FOGCOLOR, to_color(rgba));
            }
        }
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
        // At the source's own size: GetRenderTargetData wants them equal, and
        // a power-of-two target is larger than what was drawn in it.
        D3DSURFACE_DESC desc = {};
        source->GetDesc(&desc);
        if (FAILED(dev_->CreateOffscreenPlainSurface(desc.Width, desc.Height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys,
                                                     nullptr)))
            return;
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
    Profile profile_;
    IDirect3D9* d3d_ = nullptr;
    IDirect3DDevice9* dev_ = nullptr;
    D3DCAPS9 caps_ = {};
    bool ff_ = false;           // the fixed-function tier (DeviceConfig)
    bool software_vp_ = false;  // vertex programs run by D3D9's software pipeline
    bool fullscreen_ = false;
    bool color16_ = false;
    bool pow2_only_ = false;      // render targets at powers of two (create_target)
    bool no_rt_textures_ = SDL_getenv("DRAGON_D3D9_NO_RT_TEXTURES") != nullptr;
    // What a reset empties and this backend makes again (reset()).
    std::vector<D9Texture*> targets_;
    std::vector<D9Buffer*> dynamic_;
    void (*before_reset_)() = nullptr;
    void (*after_reset_)() = nullptr;
    bool mixed_vp_ = false;     // ... in mixed mode (the card has T&L of its own)
    std::unordered_map<DWORD, DWORD> stage_states_[8];
    D9Buffer* bound_index_ = nullptr;  // the index buffer SetIndices holds
    std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> index_ranges_;  // software VP: a draw range's min..max index
    D3DPRESENT_PARAMETERS pp_ = {};
    bool in_scene_ = false;
    bool has_depth_ = false;
    bool r32f_filterable_ = false;
    bool stretch_from_textures_ = true;  // D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES
    D9Texture backbuffer_;               // frame_target(): the back buffer's surface, this frame
    uint32_t present_w_ = 0, present_h_ = 0;
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
void forget_state(Device& device) { static_cast<D3D9Device&>(device).forget_state(); }
void set_reset_hooks(Device& device, void (*before)(), void (*after)()) {
    static_cast<D3D9Device&>(device).set_reset_hooks(before, after);
}

ShaderCaps probe_shader_caps(uint32_t adapter) {
    ShaderCaps caps;
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) return caps;
    D3DCAPS9 c = {};
    if (adapter < d3d->GetAdapterCount() && SUCCEEDED(d3d->GetDeviceCaps(adapter, D3DDEVTYPE_HAL, &c))) {
        caps.valid = true;
        caps.vertex_shader = D3DSHADER_VERSION_MAJOR(c.VertexShaderVersion);
        caps.pixel_shader = D3DSHADER_VERSION_MAJOR(c.PixelShaderVersion);
        caps.hardware_tnl = (c.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) != 0;
    }
    d3d->Release();
    return caps;
}

}  // namespace d3d9
}  // namespace rhi
