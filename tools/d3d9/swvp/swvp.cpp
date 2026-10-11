// swvp: what D3D9's software vertex processing costs on this machine.
//
//   swvp.exe [OUT.txt] [SPIN_SECONDS]
//
// With SPIN_SECONDS it repeats the vs_2_0 / MANAGED / one-draw case for that
// long instead, for tools/d3d9/swvp/sampler.exe to watch.
//
// The game's fixed-function and sm2 tiers run their vertex programs in
// D3D9's own software pipeline on a card without vertex shaders, and the
// same frame cost 28 ms under XP and 2 s under Windows 10 on the Atom D525's
// GMA 3150 (docs/PORTING.md, "A second machine"). This takes the game out of
// it: one window, one software-vertex-processing device, a 128x128 grid
// drawn with each combination of
//   - the vertex stage: fixed function (no program) or a vs_2_0 program
//     about the size of the sm2 terrain's (swvp_vs.hlsl),
//   - the buffer pool: MANAGED (the game's static buffers), SYSTEMMEM (its
//     dynamic ones), DEFAULT, or none (DrawIndexedPrimitiveUP),
//   - the batching: the whole grid in one draw, or one draw per row pair,
// plus "ff-terrain": fixed function set up as the game's ff terrain program
// would be -- a float3 vertex colour, texture coordinates generated from the
// camera-space position through a texture matrix, range-based EXP2 vertex
// fog -- with a readback that the float3 colour arrived,
// and prints the microseconds per vertex and per draw for each, measured
// to the GPU's end of each frame. Built with the XP toolchain, so the same
// exe runs on XP, 7 and 10 (D3D9 needs the desktop session: run it from the
// agent or tools/x99/run_interactive.ps1, not from SSH).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#include "swvp_vs.h"

namespace {

const int GRID = 128;  // vertices a side
const int FRAMES = 8;  // timed frames per case

struct Vertex {
    float pos[3], normal[3], uv[2], colour[3];
};

FILE* g_out = nullptr;
void say(const char* fmt, ...) {
    char line[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof line, fmt, args);
    va_end(args);
    fputs(line, stdout);
    if (g_out) {
        fputs(line, g_out);
        fflush(g_out);
    }
}

double now_us() {
    static LARGE_INTEGER freq = {};
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart) * 1e6 / double(freq.QuadPart);
}

LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcA(h, m, w, l); }

// A column-major-free row-vector projection: x, y into a small rect in the
// middle of the screen, so fill costs nothing and nothing is clipped.
void projection(float* m) {
    std::memset(m, 0, 16 * sizeof(float));
    // The vs_2_0 program does mul(float4(pos,1), mvp), so the matrix goes in
    // transposed (the constant registers hold its columns).
    const float s = 1.0f / GRID;
    const float rows[4][4] = {{s, 0, 0, -0.5f}, {0, s, 0, -0.5f}, {0, 0, 0.001f, 0.5f}, {0, 0, 0, 1}};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) m[r * 4 + c] = rows[r][c];
}

}  // namespace

int main(int argc, char** argv) {
    g_out = fopen(argc > 1 ? argv[1] : "swvp.txt", "w");
    OSVERSIONINFOA os = {sizeof os};
    GetVersionExA(&os);
    say("swvp: Windows %lu.%lu build %lu\n", os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber);

    WNDCLASSA wc = {};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "swvp";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("swvp", "swvp", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 340, 280, nullptr, nullptr,
                              wc.hInstance, nullptr);

    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) {
        say("Direct3DCreate9 failed\n");
        return 1;
    }
    D3DADAPTER_IDENTIFIER9 id = {};
    d3d->GetAdapterIdentifier(0, 0, &id);
    D3DCAPS9 caps = {};
    d3d->GetDeviceCaps(0, D3DDEVTYPE_HAL, &caps);
    say("adapter: %s, driver %s %u.%u.%u.%u, vs %lu.%lu, hw T&L %s, max vertex index %lu\n", id.Description, id.Driver,
        HIWORD(id.DriverVersion.HighPart), LOWORD(id.DriverVersion.HighPart), HIWORD(id.DriverVersion.LowPart),
        LOWORD(id.DriverVersion.LowPart), D3DSHADER_VERSION_MAJOR(caps.VertexShaderVersion),
        D3DSHADER_VERSION_MINOR(caps.VertexShaderVersion),
        (caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) ? "yes" : "no", caps.MaxVertexIndex);

    // The whole run on one device: software vertex processing always, and
    // where the card has T&L of its own (the G41's M6, any DX7 T&L part)
    // again on a hardware device, where fixed function runs on the card.
    auto bench = [&](DWORD processing, const char* label) -> int {
    const bool software = processing == D3DCREATE_SOFTWARE_VERTEXPROCESSING;
    say("\n== %s vertex processing\n", label);
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferWidth = 320;
    pp.BackBufferHeight = 240;
    pp.hDeviceWindow = hwnd;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* dev = nullptr;
    HRESULT hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, hwnd, processing | D3DCREATE_FPU_PRESERVE, &pp, &dev);
    if (FAILED(hr)) {
        say("CreateDevice failed 0x%08lx\n", hr);
        return 1;
    }

    // The grid, its indices, and the per-row-pair ranges.
    std::vector<Vertex> verts(GRID * GRID);
    for (int z = 0; z < GRID; ++z)
        for (int x = 0; x < GRID; ++x) {
            Vertex& v = verts[z * GRID + x];
            v.pos[0] = float(x);
            v.pos[1] = float(z);
            v.pos[2] = float((x * 7 + z * 13) % 17);
            v.normal[0] = 0.3f;
            v.normal[1] = 0.9f;
            v.normal[2] = 0.3f;
            v.uv[0] = x / float(GRID);
            v.uv[1] = z / float(GRID);
            v.colour[0] = 0.8f;
            v.colour[1] = 0.4f;
            v.colour[2] = 0.2f;
        }
    std::vector<WORD> indices;
    for (int z = 0; z + 1 < GRID; ++z)
        for (int x = 0; x + 1 < GRID; ++x) {
            const WORD a = WORD(z * GRID + x), b = WORD(a + 1), c = WORD(a + GRID), d = WORD(c + 1);
            WORD quad[6] = {a, c, b, b, c, d};
            indices.insert(indices.end(), quad, quad + 6);
        }
    const UINT tris = UINT(indices.size() / 3);
    const UINT row_tris = (GRID - 1) * 2;

    const D3DVERTEXELEMENT9 elements[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                          {0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
                                          {0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                          D3DDECL_END()};
    IDirect3DVertexDeclaration9* decl = nullptr;
    dev->CreateVertexDeclaration(elements, &decl);
    // The game's vertex: the colour as three floats, which fixed function
    // must accept for the ff tier to keep its vertex formats.
    const D3DVERTEXELEMENT9 terrain_elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
        {0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        {0, 32, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
        D3DDECL_END()};
    IDirect3DVertexDeclaration9* terrain_decl = nullptr;
    say("float3 COLOR0 declaration: %s\n",
        SUCCEEDED(dev->CreateVertexDeclaration(terrain_elements, &terrain_decl)) ? "created" : "refused");
    IDirect3DTexture9* white = nullptr;
    say("white texture: 0x%08lx\n", dev->CreateTexture(4, 4, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &white, nullptr));
    if (white) {
        D3DLOCKED_RECT r;
        if (SUCCEEDED(white->LockRect(0, &r, nullptr, 0))) {
            for (int y = 0; y < 4; ++y) std::memset(static_cast<char*>(r.pBits) + y * r.Pitch, 0xFF, 16);
            white->UnlockRect(0);
        }
    }
    IDirect3DVertexShader9* vs = nullptr;
    if (FAILED(dev->CreateVertexShader(reinterpret_cast<const DWORD*>(kVertexProgram), &vs))) say("no vertex program\n");

    float mvp[16];
    projection(mvp);
    const float sun[4] = {0.3f, 0.9f, 0.3f, 0}, fog[4] = {0.0f, 0.001f, 0, 0};
    // Fixed function: the same transform through the matrices, lighting off.
    D3DMATRIX world = {}, ident = {};
    for (int i = 0; i < 4; ++i) ident.m[i][i] = 1;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) world.m[r][c] = mvp[c * 4 + r];  // row-vector form
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    D3DMATRIX tex_scale = {};
    tex_scale.m[0][0] = tex_scale.m[1][1] = 1.0f / 16;
    tex_scale.m[2][2] = tex_scale.m[3][3] = 1;
    const float fog_density = 0.0005f;
    // fixed function as the ff terrain program, or plain (lighting off, no
    // texture, no fog), for each stage the loop below draws.
    auto ff_terrain = [&](bool on, bool fogged) {
        dev->SetTexture(0, on ? white : nullptr);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, on ? D3DTOP_MODULATE : D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, on ? D3DTA_TEXTURE : D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, on ? D3DTSS_TCI_CAMERASPACEPOSITION : 0);
        dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, on ? D3DTTFF_COUNT2 : D3DTTFF_DISABLE);
        dev->SetTransform(D3DTS_TEXTURE0, &tex_scale);
        dev->SetRenderState(D3DRS_FOGENABLE, on && fogged);
        dev->SetRenderState(D3DRS_FOGCOLOR, 0x8090A0);
        dev->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_EXP2);
        dev->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
        dev->SetRenderState(D3DRS_RANGEFOGENABLE, TRUE);
        dev->SetRenderState(D3DRS_FOGDENSITY, *reinterpret_cast<const DWORD*>(&fog_density));
    };

    // The end of a frame on the GPU: an event query, or where the driver has
    // none (the GMA 3150's XP driver), a copy of the back buffer back.
    IDirect3DQuery9* done = nullptr;
    dev->CreateQuery(D3DQUERYTYPE_EVENT, &done);
    IDirect3DSurface9* back = nullptr;
    IDirect3DSurface9* readback = nullptr;
    if (!done) {
        dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back);
        dev->CreateOffscreenPlainSurface(320, 240, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &readback, nullptr);
    }
    say("frame end: %s\n", done ? "event query" : "back buffer readback");

    // What fixed function accepts here: the colour as float3 (the game's
    // vertices) or D3DCOLOR, with and without texture coordinates generated
    // from the camera-space position, with and without vertex fog. Each
    // draws the grid once and reads the middle pixel back: the colour
    // 0.8, 0.4, 0.2 is cc6633 (fog off).
    // In FVF order (position, normal, diffuse, texture coordinates): XP's
    // runtime reads a fixed-function declaration as the FVF it stands for, so
    // a colour declared after the uv was read from the uv's bytes, as black.
    struct PackedVertex {
        float pos[3], normal[3];
        DWORD colour;
        float uv[2];
    };
    std::vector<PackedVertex> packed(verts.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        std::memcpy(packed[i].pos, verts[i].pos, 12);
        std::memcpy(packed[i].normal, verts[i].normal, 12);
        packed[i].colour = 0xFFCC6633;
        std::memcpy(packed[i].uv, verts[i].uv, 8);
    }
    const D3DVERTEXELEMENT9 packed_elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
        {0, 24, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
        {0, 28, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        D3DDECL_END()};
    IDirect3DVertexDeclaration9* packed_decl = nullptr;
    dev->CreateVertexDeclaration(packed_elements, &packed_decl);
    bool float3_ok = false, packed_ok = false;
    {
        IDirect3DSurface9* bb = nullptr;
        IDirect3DSurface9* sys = nullptr;
        dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
        dev->CreateOffscreenPlainSurface(320, 240, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr);
        for (int combo = 0; combo < 10; ++combo) {
            // 8 and 9: the colour alone (no texture stage), float3 and D3DCOLOR.
            const bool diffuse_only = combo >= 8;
            const bool use_packed = combo & 1, texgen = !diffuse_only && (combo & 2), fogged = !diffuse_only && (combo & 4);
            IDirect3DVertexDeclaration9* d = use_packed ? packed_decl : terrain_decl;
            if (!d) continue;
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0);
            dev->BeginScene();
            dev->SetVertexDeclaration(d);
            dev->SetVertexShader(nullptr);
            dev->SetTransform(D3DTS_WORLD, &world);
            dev->SetTransform(D3DTS_VIEW, &ident);
            dev->SetTransform(D3DTS_PROJECTION, &ident);
            ff_terrain(true, fogged);
            if (diffuse_only) ff_terrain(false, false);
            if (!texgen) {
                dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
                dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
            }
            const HRESULT drawn = use_packed
                ? dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, GRID * GRID, tris, indices.data(), D3DFMT_INDEX16,
                                              packed.data(), sizeof(PackedVertex))
                : dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, GRID * GRID, tris, indices.data(), D3DFMT_INDEX16,
                                              verts.data(), sizeof(Vertex));
            dev->EndScene();
            D3DLOCKED_RECT r = {};
            DWORD pixel = 0xDEADBEEF;
            if (bb && sys && SUCCEEDED(dev->GetRenderTargetData(bb, sys)) &&
                SUCCEEDED(sys->LockRect(&r, nullptr, D3DLOCK_READONLY))) {
                pixel = reinterpret_cast<const DWORD*>(static_cast<const char*>(r.pBits) + 120 * r.Pitch)[160] & 0xFFFFFF;
                sys->UnlockRect();
            }
            say("ff check: colour %-8s %-12s texgen %-3s fog %-3s -> draw 0x%08lx, middle pixel %06lx\n",
                use_packed ? "D3DCOLOR" : "float3", diffuse_only ? "diffuse only" : "x texture", texgen ? "on" : "off",
                fogged ? "on" : "off", drawn, pixel);
            if (texgen && fogged && SUCCEEDED(drawn)) (use_packed ? packed_ok : float3_ok) = true;
        }
        ff_terrain(false, false);
        if (sys) sys->Release();
        if (bb) bb->Release();
    }
    // The timed ff-terrain stage takes whichever colour this runtime draws.
    const bool ff_packed = !float3_ok && packed_ok;
    if (!float3_ok && !packed_ok) {
        if (terrain_decl) terrain_decl->Release();
        terrain_decl = nullptr;
    }
    say("ff-terrain timing uses: %s\n", terrain_decl ? (ff_packed ? "D3DCOLOR" : "float3") : "nothing (refused)");

    struct Pool {
        const char* name;
        D3DPOOL pool;
        bool up;
    } pools[] = {{"MANAGED", D3DPOOL_MANAGED, false},
                 {"SYSTEMMEM", D3DPOOL_SYSTEMMEM, false},
                 {"DEFAULT", D3DPOOL_DEFAULT, false},
                 {"UP", D3DPOOL_SYSTEMMEM, true}};

    const double spin = argc > 2 ? atof(argv[2]) : 0;
    say("%-9s %-10s %-6s %9s %9s %10s %9s %9s\n", "stage", "pool", "draws", "ms/frame", "draws/f", "verts/f",
        "us/vert", "us/draw");
    const char* stage_names[] = {"fixed", "vs_2_0", "ff-terr"};
    for (int program = spin > 0 ? 1 : 0; program < 3; ++program) {
        if (program == 2 && (!terrain_decl || spin > 0)) break;
        if (program == 1 && !vs) continue;
        for (const Pool& p : pools) {
            if (spin > 0 && p.pool != D3DPOOL_MANAGED) continue;
            IDirect3DVertexBuffer9* vb = nullptr;
            IDirect3DIndexBuffer9* ib = nullptr;
            if (!p.up) {
                const DWORD usage = software ? D3DUSAGE_SOFTWAREPROCESSING : D3DUSAGE_WRITEONLY;
                const bool pk = program == 2 && ff_packed;
                const UINT stride = pk ? sizeof(PackedVertex) : sizeof(Vertex);
                if (FAILED(dev->CreateVertexBuffer(UINT(verts.size() * stride), usage, 0, p.pool, &vb, nullptr)) ||
                    FAILED(dev->CreateIndexBuffer(UINT(indices.size() * 2), usage, D3DFMT_INDEX16, p.pool, &ib, nullptr))) {
                    say("%-9s %-10s buffers refused\n", stage_names[program], p.name);
                    if (vb) vb->Release();
                    continue;
                }
                void* m = nullptr;
                vb->Lock(0, 0, &m, 0);
                if (pk) std::memcpy(m, packed.data(), packed.size() * sizeof(PackedVertex));
                else std::memcpy(m, verts.data(), verts.size() * sizeof(Vertex));
                vb->Unlock();
                ib->Lock(0, 0, &m, 0);
                std::memcpy(m, indices.data(), indices.size() * 2);
                ib->Unlock();
            }
            for (int batching = 0; batching < (spin > 0 ? 1 : 2); ++batching) {
                const bool rows = batching == 1;
                double total = 0, in_draws = 0;
                UINT draws = 0, vertices = 0;
                const double spin_end = now_us() + spin * 1e6;
                int frames = FRAMES;
                for (int frame = -1; frame < frames; ++frame) {  // frame -1 warms up
                    if (spin > 0 && frame == frames - 1 && now_us() < spin_end) ++frames;
                    const double t0 = now_us();
                    double draw_time = 0;
                    UINT f_draws = 0, f_verts = 0;
                    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0x203040, 1, 0);
                    dev->BeginScene();
                    dev->SetVertexDeclaration(program == 2 ? (ff_packed ? packed_decl : terrain_decl) : decl);
                    ff_terrain(program == 2, true);
                    if (program == 1) {
                        dev->SetVertexShader(vs);
                        dev->SetVertexShaderConstantF(0, mvp, 4);
                        dev->SetVertexShaderConstantF(4, sun, 1);
                        dev->SetVertexShaderConstantF(5, fog, 1);
                    } else {
                        dev->SetVertexShader(nullptr);
                        dev->SetTransform(D3DTS_WORLD, &world);
                        dev->SetTransform(D3DTS_VIEW, &ident);
                        dev->SetTransform(D3DTS_PROJECTION, &ident);
                    }
                    if (!p.up) {
                        dev->SetStreamSource(0, vb, 0, program == 2 && ff_packed ? sizeof(PackedVertex) : sizeof(Vertex));
                        dev->SetIndices(ib);
                    }
                    const UINT pieces = rows ? GRID - 1 : 1;
                    for (UINT r = 0; r < pieces; ++r) {
                        const UINT first_tri = rows ? r * row_tris : 0;
                        const UINT count = rows ? row_tris : tris;
                        const UINT min_index = rows ? r * GRID : 0;
                        const UINT nverts = rows ? 2 * GRID : GRID * GRID;
                        const double d0 = now_us();
                        if (p.up) {
                            // UP takes the vertices from min_index on as its own base.
                            std::vector<WORD> local(indices.begin() + first_tri * 3, indices.begin() + (first_tri + count) * 3);
                            for (WORD& i : local) i = WORD(i - min_index);
                            if (program == 2 && ff_packed)
                                dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, nverts, count, local.data(), D3DFMT_INDEX16,
                                                            &packed[min_index], sizeof(PackedVertex));
                            else
                                dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, nverts, count, local.data(), D3DFMT_INDEX16,
                                                            &verts[min_index], sizeof(Vertex));
                        } else {
                            dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, min_index, nverts, first_tri * 3, count);
                        }
                        draw_time += now_us() - d0;
                        ++f_draws;
                        f_verts += nverts;
                    }
                    dev->EndScene();
                    if (done) {
                        done->Issue(D3DISSUE_END);
                        while (done->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) {
                        }
                    } else if (back && readback) {
                        dev->GetRenderTargetData(back, readback);
                    }
                    dev->Present(nullptr, nullptr, nullptr, nullptr);
                    MSG msg;
                    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
                    if (frame < 0) continue;
                    total += now_us() - t0;
                    in_draws += draw_time;
                    draws += f_draws;
                    vertices += f_verts;
                }
                const double ms = total / frames / 1000.0;
                say("%-9s %-10s %-6s %9.2f %9u %10u %9.3f %9.1f   (in draw calls %.2f ms)\n", stage_names[program],
                    p.name, rows ? "rows" : "one", ms, draws / frames, vertices / frames, total / vertices,
                    total / draws, in_draws / frames / 1000.0);
            }
            if (vb) vb->Release();
            if (ib) ib->Release();
        }
    }
    if (done) done->Release();
    if (back) back->Release();
    if (readback) readback->Release();
    if (vs) vs->Release();
    if (terrain_decl) terrain_decl->Release();
    if (packed_decl) packed_decl->Release();
    if (white) white->Release();
    decl->Release();
    dev->Release();
    return 0;
    };
    int status = bench(D3DCREATE_SOFTWARE_VERTEXPROCESSING, "software");
    if (caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) bench(D3DCREATE_HARDWARE_VERTEXPROCESSING, "hardware");
    d3d->Release();
    if (g_out) fclose(g_out);
    return status;
}
