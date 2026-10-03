// R0: the retro track's spike (docs/PORTING.md, "The retro track").
//
// One 32-bit program that answers the platform questions before the engine
// depends on any answer: does the Mac's i686 MinGW toolchain produce a binary
// that starts on XP, does Direct3D 9 draw a textured, fogged triangle on each
// machine, can the frame be read back the way the game's --screenshot will be
// (GetRenderTargetData), does miniaudio make a sound, and is a gamepad found
// through XInput. Plain Win32 and D3D9, no SDL: SDL3 does not run on XP,
// which is what R6 has to replace.
//
//   r0.exe [--frames N] [--shot out.bmp] [--ff | --vs] [--no-audio] [--adapter N]
//
// Everything it learns goes to stdout and to r0.log beside the executable:
// the adapter, the caps that decide a tier, the XInput DLL found, the audio
// backend, the frame time, the readback.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

namespace {

FILE* g_log = nullptr;

void logf(const char* fmt, ...) {
    char line[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof line, fmt, args);
    va_end(args);
    std::fputs(line, stdout);
    std::fputs("\n", stdout);
    std::fflush(stdout);
    if (g_log) {
        std::fputs(line, g_log);
        std::fputs("\n", g_log);
        std::fflush(g_log);
    }
}

void open_log() {
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/') --n;
    std::strcpy(path + n, "r0.log");
    g_log = std::fopen(path, "w");
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CLOSE) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// ---- the gamepad: XInput, whichever version the system has. XP has none
// unless a DirectX redistributable installed xinput1_3; Vista and later ship
// xinput9_1_0, Windows 8 and later xinput1_4.
struct XInputGamepad {
    WORD buttons;
    BYTE left_trigger, right_trigger;
    SHORT lx, ly, rx, ry;
};
struct XInputState {
    DWORD packet;
    XInputGamepad pad;
};
typedef DWORD(WINAPI* XInputGetStateFn)(DWORD, XInputState*);

XInputGetStateFn load_xinput(const char** which) {
    static const char* names[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
    for (const char* name : names) {
        if (HMODULE dll = LoadLibraryA(name)) {
            if (auto fn = reinterpret_cast<XInputGetStateFn>(GetProcAddress(dll, "XInputGetState"))) {
                *which = name;
                return fn;
            }
        }
    }
    *which = "none";
    return nullptr;
}

// ---- audio: a short rising tone through miniaudio's default backend
// (WASAPI on Vista and later, DirectSound on XP).
struct Tone {
    double phase = 0.0;
    double t = 0.0;
};

void tone_callback(ma_device* device, void* out, const void*, ma_uint32 frames) {
    Tone* tone = static_cast<Tone*>(device->pUserData);
    float* samples = static_cast<float*>(out);
    const double rate = device->sampleRate;
    for (ma_uint32 i = 0; i < frames; ++i) {
        const double hz = 330.0 + 220.0 * std::fmin(tone->t / 0.6, 1.0);
        const double fade = tone->t < 0.6 ? std::fmin(1.0, (0.6 - tone->t) * 8.0) : 0.0;
        const float v = float(std::sin(tone->phase) * 0.2 * fade);
        tone->phase += 2.0 * 3.14159265358979 * hz / rate;
        tone->t += 1.0 / rate;
        for (ma_uint32 c = 0; c < device->playback.channels; ++c) *samples++ = v;
    }
}

// ---- the triangle
struct Vertex {
    float x, y, z;
    DWORD color;
    float u, v;
};
const DWORD VERTEX_FVF = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;

// A column-major-free D3D9 matrix, row vectors as D3D9 wants them.
void perspective_lh(D3DMATRIX* m, float fov_y, float aspect, float zn, float zf) {
    std::memset(m, 0, sizeof *m);
    const float y = 1.0f / std::tan(fov_y * 0.5f);
    m->_11 = y / aspect;
    m->_22 = y;
    m->_33 = zf / (zf - zn);
    m->_34 = 1.0f;
    m->_43 = -zn * zf / (zf - zn);
}

void identity(D3DMATRIX* m) {
    std::memset(m, 0, sizeof *m);
    m->_11 = m->_22 = m->_33 = m->_44 = 1.0f;
}

// vs_2_0 (assembled bytecode, no compiler needed): position through the
// world-view-projection in c0..c3, the colour and uv passed on, and the
// fog factor from the view depth in c4 (x: 1 / range, y: start / range).
//   vs_2_0
//   dcl_position v0 ; dcl_color v1 ; dcl_texcoord v2
//   dp4 oPos.x, v0, c0 ; dp4 oPos.y, v0, c1 ; dp4 oPos.z, v0, c2 ; dp4 oPos.w, v0, c3
//   mov oD0, v1 ; mov oT0, v2
//   dp4 r0.x, v0, c3                      (view depth = clip w)
//   mad r0.x, r0.x, -c4.x, c4.z           (1 - (w - start) / range)
//   max r0.x, r0.x, c4.w ; min oFog, r0.x, c4.z
// Encoded by hand from the D3D9 shader token format; checked by
// D3DXDisassembleShader-equivalent logging of the create result.
const DWORD kVertexShader[] = {
    0xFFFE0200,                                           // vs_2_0
    0x0200001F, 0x80000000, 0x900F0000,                   // dcl_position v0
    0x0200001F, 0x8000000A, 0x900F0001,                   // dcl_color v1
    0x0200001F, 0x80000005, 0x900F0002,                   // dcl_texcoord v2
    0x03000009, 0xC0010000, 0x90E40000, 0xA0E40000,       // dp4 oPos.x, v0, c0
    0x03000009, 0xC0020000, 0x90E40000, 0xA0E40001,       // dp4 oPos.y, v0, c1
    0x03000009, 0xC0040000, 0x90E40000, 0xA0E40002,       // dp4 oPos.z, v0, c2
    0x03000009, 0xC0080000, 0x90E40000, 0xA0E40003,       // dp4 oPos.w, v0, c3
    0x02000001, 0xD00F0000, 0x90E40001,                   // mov oD0, v1
    0x02000001, 0xE00F0000, 0x90E40002,                   // mov oT0, v2
    0x03000009, 0x80010000, 0x90E40000, 0xA0E40003,       // dp4 r0.x, v0, c3
    0x04000004, 0x80010000, 0x80000000, 0xA1000004, 0xA0AA0004,  // mad r0.x, r0.x, -c4.x, c4.z
    0x0300000B, 0x80010000, 0x80000000, 0xA0FF0004,       // max r0.x, r0.x, c4.w
    0x0300000A, 0xC00F0001, 0x80000000, 0xA0AA0004,       // min oFog, r0.x, c4.z
    0x0000FFFF,                                           // end
};

bool write_bmp(const char* path, const uint32_t* bgra, int w, int h, int pitch_px) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const uint32_t image = uint32_t(w * h * 4);
    uint8_t header[54] = {'B', 'M'};
    auto put32 = [&](int at, uint32_t v) { std::memcpy(header + at, &v, 4); };
    put32(2, 54 + image);
    put32(10, 54);
    put32(14, 40);
    put32(18, uint32_t(w));
    put32(22, uint32_t(-h));  // top-down
    header[26] = 1;
    header[28] = 32;
    put32(34, image);
    std::fwrite(header, 1, 54, f);
    for (int y = 0; y < h; ++y) std::fwrite(bgra + size_t(y) * pitch_px, 4, size_t(w), f);
    std::fclose(f);
    return true;
}

const char* format_name(D3DFORMAT f) {
    switch (f) {
        case D3DFMT_X8R8G8B8: return "X8R8G8B8";
        case D3DFMT_A8R8G8B8: return "A8R8G8B8";
        case D3DFMT_R5G6B5: return "R5G6B5";
        case D3DFMT_D24S8: return "D24S8";
        case D3DFMT_D24X8: return "D24X8";
        case D3DFMT_D16: return "D16";
        default: return "other";
    }
}

}  // namespace

int main(int argc, char** argv) {
    int frames = 120;
    const char* shot = "r0.bmp";
    bool force_ff = false, force_vs = false, audio = true;
    UINT adapter = D3DADAPTER_DEFAULT;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!std::strcmp(argv[i], "--ff")) force_ff = true;
        else if (!std::strcmp(argv[i], "--vs")) force_vs = true;
        else if (!std::strcmp(argv[i], "--no-audio")) audio = false;
        else if (!std::strcmp(argv[i], "--adapter") && i + 1 < argc) adapter = UINT(std::atoi(argv[++i]));
    }
    open_log();

    OSVERSIONINFOA os = {};
    os.dwOSVersionInfoSize = sizeof os;
    GetVersionExA(&os);
    logf("r0: Windows %lu.%lu build %lu %s", os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber,
         os.szCSDVersion);
    if (HMODULE ntdll = GetModuleHandleA("ntdll.dll")) {
        if (auto wine = reinterpret_cast<const char* (*)()>(GetProcAddress(ntdll, "wine_get_version")))
            logf("r0: under Wine %s", wine());
    }

    // ---- window
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "R0Spike";
    RegisterClassExA(&wc);
    RECT rect = {0, 0, 640, 480};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExA(0, "R0Spike", "R0 spike", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, wc.hInstance,
                                nullptr);
    if (!hwnd) {
        logf("FAIL: CreateWindowEx (%lu)", GetLastError());
        return 1;
    }
    ShowWindow(hwnd, SW_SHOW);

    // ---- D3D9
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) {
        logf("FAIL: Direct3DCreate9");
        return 1;
    }
    // The display devices behind them: each card's outputs, whether each is
    // on the desktop, and the monitor it sees.
    for (DWORD i = 0;; ++i) {
        DISPLAY_DEVICEA dd = {};
        dd.cb = sizeof dd;
        if (!EnumDisplayDevicesA(nullptr, i, &dd, 0)) break;
        logf("display %s: %s%s%s", dd.DeviceName, dd.DeviceString,
             (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) ? ", on the desktop" : ", not attached",
             (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) ? ", primary" : "");
        DISPLAY_DEVICEA monitor = {};
        monitor.cb = sizeof monitor;
        for (DWORD m = 0; EnumDisplayDevicesA(dd.DeviceName, m, &monitor, 0); ++m)
            logf("    monitor: %s", monitor.DeviceString);
    }
    // D3D9 enumerates displays, not cards: an adapter appears once per output
    // with a desktop on it, so a second card with nothing attached is absent.
    for (UINT a = 0; a < d3d->GetAdapterCount(); ++a) {
        D3DADAPTER_IDENTIFIER9 each = {};
        d3d->GetAdapterIdentifier(a, 0, &each);
        D3DCAPS9 each_caps = {};
        d3d->GetDeviceCaps(a, D3DDEVTYPE_HAL, &each_caps);
        logf("adapter %u: %s (%s) %s, ps_%lu_%lu", a, each.Description, each.Driver, each.DeviceName,
             D3DSHADER_VERSION_MAJOR(each_caps.PixelShaderVersion), D3DSHADER_VERSION_MINOR(each_caps.PixelShaderVersion));
    }
    if (adapter >= d3d->GetAdapterCount()) {
        logf("FAIL: no adapter %u", adapter);
        return 1;
    }
    D3DADAPTER_IDENTIFIER9 id = {};
    d3d->GetAdapterIdentifier(adapter, 0, &id);
    logf("adapter: %s (%s), vendor %04lx device %04lx", id.Description, id.Driver, id.VendorId, id.DeviceId);
    D3DCAPS9 caps = {};
    d3d->GetDeviceCaps(adapter, D3DDEVTYPE_HAL, &caps);
    const unsigned vs_major = D3DSHADER_VERSION_MAJOR(caps.VertexShaderVersion);
    const unsigned vs_minor = D3DSHADER_VERSION_MINOR(caps.VertexShaderVersion);
    const unsigned ps_major = D3DSHADER_VERSION_MAJOR(caps.PixelShaderVersion);
    const unsigned ps_minor = D3DSHADER_VERSION_MINOR(caps.PixelShaderVersion);
    const char* tier = ps_major >= 3 ? "sm3" : ps_major >= 2 ? "sm2" : "ff";
    logf("caps: vs_%u_%u ps_%u_%u, %lu vs constants, max texture %lux%lu, %lu texture stages, "
         "hw t&l %s, table fog %s, vertex fog %s -> tier %s",
         vs_major, vs_minor, ps_major, ps_minor, caps.MaxVertexShaderConst, caps.MaxTextureWidth,
         caps.MaxTextureHeight, caps.MaxTextureBlendStages,
         (caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) ? "yes" : "no",
         (caps.RasterCaps & D3DPRASTERCAPS_FOGTABLE) ? "yes" : "no",
         (caps.RasterCaps & D3DPRASTERCAPS_FOGVERTEX) ? "yes" : "no", tier);
    const bool dxt1 = SUCCEEDED(d3d->CheckDeviceFormat(adapter, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0,
                                                       D3DRTYPE_TEXTURE, D3DFMT_DXT1));
    const bool dxt5 = SUCCEEDED(d3d->CheckDeviceFormat(adapter, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0,
                                                       D3DRTYPE_TEXTURE, D3DFMT_DXT5));
    const bool fp16_rt = SUCCEEDED(d3d->CheckDeviceFormat(adapter, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8,
                                                          D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE,
                                                          D3DFMT_A16B16G16R16F));
    const bool d24 = SUCCEEDED(d3d->CheckDeviceFormat(adapter, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8,
                                                      D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, D3DFMT_D24X8));
    logf("formats: DXT1 %s, DXT5 %s, FP16 render target %s, D24X8 %s", dxt1 ? "yes" : "no",
         dxt5 ? "yes" : "no", fp16_rt ? "yes" : "no", d24 ? "yes" : "no");

    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferWidth = 640;
    pp.BackBufferHeight = 480;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = d24 ? D3DFMT_D24X8 : D3DFMT_D16;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* dev = nullptr;
    const DWORD vp = (caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) ? D3DCREATE_HARDWARE_VERTEXPROCESSING
                                                                      : D3DCREATE_SOFTWARE_VERTEXPROCESSING;
    HRESULT hr = d3d->CreateDevice(adapter, D3DDEVTYPE_HAL, hwnd, vp, &pp, &dev);
    if (FAILED(hr)) {
        logf("FAIL: CreateDevice 0x%08lx", hr);
        return 1;
    }
    logf("device: %s vertex processing, back buffer %s, depth %s",
         vp == D3DCREATE_HARDWARE_VERTEXPROCESSING ? "hardware" : "software", format_name(pp.BackBufferFormat),
         format_name(pp.AutoDepthStencilFormat));

    // A 64x64 checker in two greens, so the texture and its filtering show.
    IDirect3DTexture9* tex = nullptr;
    dev->CreateTexture(64, 64, 0, D3DUSAGE_AUTOGENMIPMAP, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr);
    if (tex) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0))) {
            for (int y = 0; y < 64; ++y) {
                uint32_t* row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch);
                for (int x = 0; x < 64; ++x) row[x] = ((x / 8 + y / 8) & 1) ? 0xFFE0C060 : 0xFF406020;
            }
            tex->UnlockRect(0);
        }
    }

    // A triangle leaning away from the camera, so the fog grades along it.
    const Vertex tri[3] = {
        {-1.6f, -1.0f, 2.0f, 0xFFFFFFFF, 0.0f, 4.0f},
        {1.6f, -1.0f, 2.0f, 0xFFFFC0C0, 4.0f, 4.0f},
        {0.0f, 1.2f, 14.0f, 0xFFC0C0FF, 2.0f, 0.0f},
    };
    IDirect3DVertexShader9* vs = nullptr;
    const bool use_vs = !force_ff && (force_vs || vs_major >= 2);
    if (use_vs) {
        hr = dev->CreateVertexShader(kVertexShader, &vs);
        logf("vs_2_0 bytecode: %s (0x%08lx)", SUCCEEDED(hr) ? "created" : "REJECTED", hr);
        if (FAILED(hr)) vs = nullptr;
    }
    logf("path: %s", vs ? "vs_2_0 + fixed-function pixel stages" : "fixed function");

    D3DMATRIX world, view, proj;
    identity(&world);
    identity(&view);
    perspective_lh(&proj, 1.0f, 640.0f / 480.0f, 0.5f, 100.0f);
    const float fog_start = 3.0f, fog_end = 15.0f;
    const DWORD fog_color = 0xFF8FA8C8;

    const char* xinput_name = "none";
    XInputGetStateFn xinput_get = load_xinput(&xinput_name);
    logf("xinput: %s", xinput_name);

    ma_device audio_device;
    Tone tone;
    bool audio_on = false;
    if (audio) {
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = 2;
        config.sampleRate = 44100;
        config.dataCallback = tone_callback;
        config.pUserData = &tone;
        if (ma_device_init(nullptr, &config, &audio_device) == MA_SUCCESS) {
            ma_device_start(&audio_device);
            audio_on = true;
            logf("audio: %s at %u Hz", ma_get_backend_name(audio_device.pContext->backend),
                 audio_device.sampleRate);
        } else {
            logf("audio: no device");
        }
    }

    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    bool pad_seen = false;
    for (int frame = 0; frame < frames; ++frame) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) frames = frame;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (xinput_get && !pad_seen) {
            XInputState state = {};
            if (xinput_get(0, &state) == ERROR_SUCCESS) {
                pad_seen = true;
                logf("xinput: controller 0 connected (buttons %04x)", state.pad.buttons);
            }
        }

        dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, fog_color, 1.0f, 0);
        dev->BeginScene();
        dev->SetTexture(0, tex);
        dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        dev->SetRenderState(D3DRS_LIGHTING, FALSE);
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        dev->SetRenderState(D3DRS_FOGENABLE, TRUE);
        dev->SetRenderState(D3DRS_FOGCOLOR, fog_color);
        dev->SetFVF(VERTEX_FVF);
        if (vs) {
            // The shader writes oFog; the fixed pipeline blends with it.
            dev->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
            dev->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_NONE);
            dev->SetVertexShader(vs);
            // c0..c3: the transposed world-view-projection (world and view
            // are identity, so it is the projection's columns as rows).
            float c[5][4];
            const float* p = &proj._11;
            for (int r = 0; r < 4; ++r)
                for (int k = 0; k < 4; ++k) c[r][k] = p[k * 4 + r];
            const float range = fog_end - fog_start;
            c[4][0] = 1.0f / range;
            c[4][1] = 0.0f;
            c[4][2] = 1.0f + fog_start / range;  // 1 - (w - start) / range = (1 + start/range) - w/range
            c[4][3] = 0.0f;
            dev->SetVertexShaderConstantF(0, &c[0][0], 5);
        } else {
            dev->SetVertexShader(nullptr);
            dev->SetTransform(D3DTS_WORLD, &world);
            dev->SetTransform(D3DTS_VIEW, &view);
            dev->SetTransform(D3DTS_PROJECTION, &proj);
            const bool table = (caps.RasterCaps & D3DPRASTERCAPS_FOGTABLE) != 0;
            dev->SetRenderState(table ? D3DRS_FOGTABLEMODE : D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
            dev->SetRenderState(D3DRS_FOGSTART, *reinterpret_cast<const DWORD*>(&fog_start));
            dev->SetRenderState(D3DRS_FOGEND, *reinterpret_cast<const DWORD*>(&fog_end));
        }
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, tri, sizeof(Vertex));
        dev->EndScene();

        if (frame == frames - 1) {
            // The game's --screenshot: the back buffer copied to system
            // memory, the frame as rendered rather than a capture of the screen.
            IDirect3DSurface9* back = nullptr;
            IDirect3DSurface9* sys = nullptr;
            dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back);
            dev->CreateOffscreenPlainSurface(640, 480, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr);
            hr = (back && sys) ? dev->GetRenderTargetData(back, sys) : E_FAIL;
            D3DLOCKED_RECT lr;
            if (SUCCEEDED(hr) && SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
                const bool ok = write_bmp(shot, static_cast<const uint32_t*>(lr.pBits), 640, 480, lr.Pitch / 4);
                sys->UnlockRect();
                logf("readback: %s -> %s", ok ? "ok" : "write failed", shot);
            } else {
                logf("FAIL: readback 0x%08lx", hr);
            }
            if (sys) sys->Release();
            if (back) back->Release();
        }
        dev->Present(nullptr, nullptr, nullptr, nullptr);
    }
    QueryPerformanceCounter(&t1);
    const double seconds = double(t1.QuadPart - t0.QuadPart) / double(freq.QuadPart);
    logf("frames: %d in %.2f s (%.0f fps)", frames, seconds, seconds > 0 ? frames / seconds : 0.0);
    if (xinput_get && !pad_seen) logf("xinput: no controller on port 0");

    if (audio_on) {
        while (tone.t < 0.7) Sleep(20);
        ma_device_uninit(&audio_device);
    }
    if (vs) vs->Release();
    if (tex) tex->Release();
    dev->Release();
    d3d->Release();
    DestroyWindow(hwnd);
    logf("r0: done");
    return 0;
}
