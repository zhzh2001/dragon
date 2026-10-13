// tris: what a triangle costs the driver and the card, with no vertex work.
//
//   tris.exe [OUT.txt]
//   tris.exe OUT.txt PRIM SECONDS   one case (pos+col+uv, tiny, textured)
//                                   drawn over and over, to profile it
//
// swvp.exe found the G550 under XP drawing a 128x128 grid at 5.6 us a
// triangle with only 0.5 ms of the 180 ms frame inside the draw calls:
// the time is the driver feeding the card, not D3D9's vertex processing.
// This takes D3D9's vertex pipeline out altogether (pre-transformed
// XYZRHW vertices, which the runtime hands the driver as they are) and
// draws the same triangles each way that might matter to such a driver:
//   - the primitive: an indexed list, a plain list (three vertices a
//     triangle), strips one per row (one vertex a triangle), the rows as
//     one strip joined by degenerate triangles ("joined"), and that strip
//     indexed ("ix-strp");
//   - where the vertices come from: user pointers (the *UP calls) or
//     default-pool vertex and index buffers ("vb-"), which a DX7 driver may
//     allocate where the card can fetch them itself;
//   - how many draws carry them: one, 64 or 1,024 ("/64", "/1k"), the
//     latter about 18 triangles a draw, as a Unity scene's are;
//   - the vertex: position and colour (20 bytes), plus a uv (28), plus the
//     specular whose alpha is vertex fog (32);
//   - fog and the texture stage on or off;
//   - tiny triangles (fill costs nothing) or ones covering the target.
// Each case is timed to the end of the frame on the card and printed as
// microseconds a triangle and a vertex sent. The end of the frame is an
// event query, or where the driver has none (the G550) a clear of a 16x16
// render target read back after the draws: the card finishes in order, so
// that waits for every triangle. Reading back the whole target instead, as
// this did first, cost the G550 about 50 ms a frame of uncached reads over
// PCI, which went into every figure. "none" draws nothing: the frame's
// fixed cost, to subtract. REPEAT=N in the environment draws the grid N
// times a frame. Built with the XP toolchain beside swvp.exe (build.sh).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>

#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

const int GRID = 96;  // vertices a side: 18,050 triangles
const int FRAMES = 6;
const int W = 320, H = 240;

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

// The widest vertex; the narrower formats are its prefix.
struct Vertex {
    float x, y, z, rhw;
    DWORD diffuse, specular;
    float u, v;
};

struct Format {
    const char* name;
    DWORD fvf;
};

}  // namespace

const char* const kPrimNames[] = {"indexed", "list",     "strips",   "joined",  "ix-strp", "vb-ix",
                                   "vb-list", "vb-ix/64", "vb-ix/1k", "list/1k", "none"};
const int kPrims = 11;

int main(int argc, char** argv) {
    g_out = fopen(argc > 1 ? argv[1] : "tris.txt", "w");
    // Loop mode: one case for SECONDS, for a profiler to watch.
    int loop_prim = -1;
    double loop_seconds = 0;
    if (argc > 3) {
        for (int i = 0; i < kPrims; ++i)
            if (!std::strcmp(argv[2], kPrimNames[i])) loop_prim = i;
        loop_seconds = std::atof(argv[3]);
        if (loop_prim < 0) {
            say("unknown primitive %s\n", argv[2]);
            return 1;
        }
    }
    WNDCLASSA wc = {};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "tris";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("tris", "tris", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, W + 20, H + 40, nullptr, nullptr,
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
    say("adapter: %s, driver %s %u.%u.%u.%u\n", id.Description, id.Driver, HIWORD(id.DriverVersion.HighPart),
        LOWORD(id.DriverVersion.HighPart), HIWORD(id.DriverVersion.LowPart), LOWORD(id.DriverVersion.LowPart));
    say("caps: Caps2 %08lx DevCaps %08lx PrimitiveMiscCaps %08lx RasterCaps %08lx TextureCaps %08lx\n", caps.Caps2,
        caps.DevCaps, caps.PrimitiveMiscCaps, caps.RasterCaps, caps.TextureCaps);
    say("caps: dynamic textures %s, max texture %lux%lu, stages %lu/%lu, max primitives %lu, guard band %.0f..%.0f\n",
        (caps.Caps2 & D3DCAPS2_DYNAMICTEXTURES) ? "yes" : "no", caps.MaxTextureWidth, caps.MaxTextureHeight,
        caps.MaxSimultaneousTextures, caps.MaxTextureBlendStages, caps.MaxPrimitiveCount, caps.GuardBandLeft,
        caps.GuardBandRight);

    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.BackBufferWidth = W;
    pp.BackBufferHeight = H;
    pp.hDeviceWindow = hwnd;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D16;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* dev = nullptr;
    HRESULT hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
                                   &pp, &dev);
    if (FAILED(hr)) {
        say("CreateDevice failed 0x%08lx\n", hr);
        return 1;
    }
    IDirect3DQuery9* done = nullptr;
    dev->CreateQuery(D3DQUERYTYPE_EVENT, &done);
    IDirect3DSurface9* back = nullptr;
    IDirect3DSurface9* readback = nullptr;
    dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back);
    D3DSURFACE_DESC bd = {};
    back->GetDesc(&bd);
    dev->CreateOffscreenPlainSurface(W, H, bd.Format, D3DPOOL_SYSTEMMEM, &readback, nullptr);
    IDirect3DSurface9* marker = nullptr;
    IDirect3DSurface9* marker_sys = nullptr;
    if (!done && SUCCEEDED(dev->CreateRenderTarget(16, 16, bd.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &marker, nullptr)))
        dev->CreateOffscreenPlainSurface(16, 16, bd.Format, D3DPOOL_SYSTEMMEM, &marker_sys, nullptr);
    const bool full_readback = getenv("FULL_READBACK") != nullptr;
    say("frame end: %s\n", done                       ? "event query"
                            : marker_sys && !full_readback ? "a 16x16 target cleared and read back"
                                                          : "back buffer readback");
    const char* repeat_env = getenv("REPEAT");
    const int repeat = repeat_env && atoi(repeat_env) > 0 ? atoi(repeat_env) : 1;
    if (repeat > 1) say("the grid drawn %d times a frame\n", repeat);
    auto finish = [&] {
        if (done) {
            done->Issue(D3DISSUE_END);
            while (done->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) {
            }
        } else if (marker_sys && !full_readback) {
            dev->SetRenderTarget(0, marker);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF00FF, 1, 0);
            dev->GetRenderTargetData(marker, marker_sys);
            D3DLOCKED_RECT r;
            if (SUCCEEDED(marker_sys->LockRect(&r, nullptr, D3DLOCK_READONLY))) marker_sys->UnlockRect();
            dev->SetRenderTarget(0, back);
        } else if (readback) {
            dev->GetRenderTargetData(back, readback);
            D3DLOCKED_RECT r;
            if (SUCCEEDED(readback->LockRect(&r, nullptr, D3DLOCK_READONLY))) readback->UnlockRect();
        }
    };

    IDirect3DTexture9* tex = nullptr;
    dev->CreateTexture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr);
    if (tex) {
        D3DLOCKED_RECT r;
        if (SUCCEEDED(tex->LockRect(0, &r, nullptr, 0))) {
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x)
                    reinterpret_cast<DWORD*>(static_cast<char*>(r.pBits) + y * r.Pitch)[x] =
                        ((x ^ y) & 8) ? 0xFFFFFFFF : 0xFF808080;
            tex->UnlockRect(0);
        }
    }

    // The grid in screen space: a small square in the middle (tiny
    // triangles) or stretched over the whole target (large ones).
    auto grid = [&](bool large) {
        std::vector<Vertex> v(GRID * GRID);
        const float cell_x = large ? float(W) / (GRID - 1) : 0.6f, cell_y = large ? float(H) / (GRID - 1) : 0.6f;
        const float x0 = large ? 0.0f : W * 0.5f - cell_x * GRID * 0.5f, y0 = large ? 0.0f : H * 0.5f - cell_y * GRID * 0.5f;
        for (int z = 0; z < GRID; ++z)
            for (int x = 0; x < GRID; ++x) {
                Vertex& o = v[z * GRID + x];
                o.x = x0 + x * cell_x;
                o.y = y0 + z * cell_y;
                o.z = 0.5f;
                o.rhw = 1.0f;
                o.diffuse = 0xFFCC6633;
                o.specular = 0x80000000;  // fog factor half in alpha
                o.u = x / 8.0f;
                o.v = z / 8.0f;
            }
        return v;
    };
    std::vector<WORD> list;  // indexed list
    for (int z = 0; z + 1 < GRID; ++z)
        for (int x = 0; x + 1 < GRID; ++x) {
            const WORD a = WORD(z * GRID + x), b = WORD(a + 1), c = WORD(a + GRID), d = WORD(c + 1);
            const WORD quad[6] = {a, c, b, b, c, d};
            list.insert(list.end(), quad, quad + 6);
        }
    const UINT tris = UINT(list.size() / 3);

    const Format formats[] = {{"pos+col", D3DFVF_XYZRHW | D3DFVF_DIFFUSE},
                              {"pos+col+uv", D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1},
                              {"pos+col+spec+uv", D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1}};
    say("%-6s %-16s %-9s %-4s %-4s %9s %8s %8s %8s\n", "size", "vertex", "prim", "tex", "fog", "ms/frame", "tris",
        "us/tri", "us/vert");
    for (int large = 0; large < 2; ++large) {
        const std::vector<Vertex> verts = grid(large != 0);
        for (const Format& f : formats) {
            // Each format's vertices packed at its own stride.
            const bool has_spec = (f.fvf & D3DFVF_SPECULAR) != 0, has_uv = (f.fvf & D3DFVF_TEX1) != 0;
            const UINT stride = 20 + (has_spec ? 4 : 0) + (has_uv ? 8 : 0);
            auto pack = [&](const Vertex& v, std::vector<BYTE>& out) {
                const size_t at = out.size();
                out.resize(at + stride);
                BYTE* p = out.data() + at;
                std::memcpy(p, &v, 20);
                p += 20;
                if (has_spec) {
                    std::memcpy(p, &v.specular, 4);
                    p += 4;
                }
                if (has_uv) std::memcpy(p, &v.u, 8);
            };
            std::vector<BYTE> indexed, flat;
            for (const Vertex& v : verts) pack(v, indexed);
            for (WORD i : list) pack(verts[i], flat);
            // Strips: one a row pair, alternating the two rows.
            std::vector<std::vector<BYTE>> strips(GRID - 1);
            for (int z = 0; z + 1 < GRID; ++z)
                for (int x = 0; x < GRID; ++x) {
                    pack(verts[z * GRID + x], strips[z]);
                    pack(verts[(z + 1) * GRID + x], strips[z]);
                }
            // The rows as one strip, joined by repeating the last vertex of
            // a row and the first of the next (four degenerate triangles a
            // join, which keeps the winding): one draw, as a batch would be.
            std::vector<BYTE> joined;
            std::vector<WORD> strip_indices;  // the same, indexed
            for (int z = 0; z + 1 < GRID; ++z) {
                if (z > 0) strip_indices.push_back(WORD(z * GRID));
                for (int x = 0; x < GRID; ++x) {
                    strip_indices.push_back(WORD(z * GRID + x));
                    strip_indices.push_back(WORD((z + 1) * GRID + x));
                }
                if (z + 2 < GRID) strip_indices.push_back(WORD((z + 1) * GRID + GRID - 1));
            }
            for (WORD i : strip_indices) pack(verts[i], joined);
            const UINT joined_prims = UINT(strip_indices.size()) - 2;
            // The same vertices and indices in default-pool buffers.
            IDirect3DVertexBuffer9* vb_indexed = nullptr;
            IDirect3DVertexBuffer9* vb_flat = nullptr;
            IDirect3DIndexBuffer9* ib = nullptr;
            auto fill_vb = [&](const std::vector<BYTE>& from, IDirect3DVertexBuffer9** to) {
                if (FAILED(dev->CreateVertexBuffer(UINT(from.size()), D3DUSAGE_WRITEONLY, f.fvf, D3DPOOL_DEFAULT, to,
                                                   nullptr)))
                    return;
                void* p = nullptr;
                if (SUCCEEDED((*to)->Lock(0, 0, &p, 0))) {
                    std::memcpy(p, from.data(), from.size());
                    (*to)->Unlock();
                }
            };
            fill_vb(indexed, &vb_indexed);
            fill_vb(flat, &vb_flat);
            if (SUCCEEDED(dev->CreateIndexBuffer(UINT(list.size() * 2), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT,
                                                 &ib, nullptr))) {
                void* p = nullptr;
                if (SUCCEEDED(ib->Lock(0, 0, &p, 0))) {
                    std::memcpy(p, list.data(), list.size() * 2);
                    ib->Unlock();
                }
            }
            // The indexed list cut into N runs of whole triangles, each with
            // the vertex range it reaches (what software vertex processing
            // would touch).
            struct Run {
                UINT first_index, prims, min_vertex, vertices;
            };
            auto cut = [&](UINT n) {
                std::vector<Run> runs;
                for (UINT i = 0; i < n; ++i) {
                    const UINT t0 = tris * i / n, t1 = tris * (i + 1) / n;
                    if (t1 == t0) continue;
                    WORD lo = 0xFFFF, hi = 0;
                    for (UINT k = t0 * 3; k < t1 * 3; ++k) {
                        lo = list[k] < lo ? list[k] : lo;
                        hi = list[k] > hi ? list[k] : hi;
                    }
                    runs.push_back({t0 * 3, t1 - t0, lo, UINT(hi - lo + 1)});
                }
                return runs;
            };
            const std::vector<Run> runs64 = cut(64), runs1k = cut(1024);
            auto run_vertices = [](const std::vector<Run>& runs) {
                double n = 0;
                for (const Run& r : runs) n += r.vertices;
                return n;
            };
            const int prim_count = loop_prim >= 0 ? 1 : kPrims;
            for (int prim_at = 0; prim_at < prim_count; ++prim_at) {
                const int prim = loop_prim >= 0 ? loop_prim : prim_at;
                if (loop_prim >= 0 && (large || f.fvf != formats[1].fvf)) continue;
                for (int tex_on = 0; tex_on < (has_uv ? 2 : 1); ++tex_on) {
                    if (loop_prim >= 0 && !tex_on) continue;
                    for (int fog_on = 0; fog_on < (has_spec ? 2 : 1); ++fog_on) {
                        dev->SetFVF(f.fvf);
                        dev->SetRenderState(D3DRS_LIGHTING, FALSE);
                        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
                        dev->SetRenderState(D3DRS_ZENABLE, TRUE);
                        dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
                        dev->SetRenderState(D3DRS_FOGENABLE, fog_on ? TRUE : FALSE);
                        dev->SetRenderState(D3DRS_FOGCOLOR, 0x8090A0);
                        dev->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
                        dev->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_NONE);
                        dev->SetTexture(0, tex_on ? tex : nullptr);
                        dev->SetTextureStageState(0, D3DTSS_COLOROP, tex_on ? D3DTOP_MODULATE : D3DTOP_SELECTARG2);
                        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
                        dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
                        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
                        dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
                        dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
                        double total = 0;
                        HRESULT drawn = S_OK;
                        int frames = FRAMES;
                        const double loop_start = now_us();
                        for (int frame = -1; frame < frames; ++frame) {
                            if (loop_prim >= 0) frames = now_us() - loop_start < loop_seconds * 1e6 ? frame + 2 : frame + 1;
                            const double t0 = now_us();
                            dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0x203040, 1, 0);
                            dev->BeginScene();
                            for (int pass = 0; pass < repeat; ++pass) {
                            if (prim == 10) {
                            } else if (prim == 0) {
                                drawn = dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, GRID * GRID, tris, list.data(),
                                                                    D3DFMT_INDEX16, indexed.data(), stride);
                            } else if (prim == 1) {
                                drawn = dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, tris, flat.data(), stride);
                            } else if (prim == 3) {
                                drawn = dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, joined_prims, joined.data(), stride);
                            } else if (prim == 4) {
                                drawn = dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLESTRIP, 0, GRID * GRID, joined_prims,
                                                                    strip_indices.data(), D3DFMT_INDEX16, indexed.data(),
                                                                    stride);
                            } else if (prim == 5 || prim == 7 || prim == 8) {
                                if (!vb_indexed || !ib) {
                                    drawn = E_FAIL;
                                } else {
                                    dev->SetStreamSource(0, vb_indexed, 0, stride);
                                    dev->SetIndices(ib);
                                    if (prim == 5) {
                                        drawn = dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, GRID * GRID, 0, tris);
                                    } else {
                                        for (const Run& r : prim == 7 ? runs64 : runs1k)
                                            drawn = dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, r.min_vertex, r.vertices,
                                                                              r.first_index, r.prims);
                                    }
                                }
                            } else if (prim == 6) {
                                if (!vb_flat) {
                                    drawn = E_FAIL;
                                } else {
                                    dev->SetStreamSource(0, vb_flat, 0, stride);
                                    drawn = dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, tris);
                                }
                            } else if (prim == 9) {
                                for (const Run& r : runs1k)
                                    drawn = dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, r.prims, flat.data() + r.first_index * stride,
                                                                 stride);
                            } else {
                                for (const auto& s : strips) {
                                    drawn = dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, UINT(s.size() / stride) - 2, s.data(),
                                                                 stride);
                                }
                            }
                            }
                            dev->EndScene();
                            finish();
                            if (frame >= 0) total += now_us() - t0;
                            dev->Present(nullptr, nullptr, nullptr, nullptr);
                        }
                        const double ms = total / frames / 1000.0;
                        const UINT drawn_tris = prim == 10 ? 0 : tris * repeat;
                        const double sent = repeat * (prim == 0 || prim == 4 || prim == 5 ? double(GRID * GRID)
                                            : prim == 1 || prim == 6 || prim == 9 ? tris * 3.0
                                            : prim == 2                           ? (GRID - 1) * GRID * 2.0
                                            : prim == 7                           ? run_vertices(runs64)
                                            : prim == 8                           ? run_vertices(runs1k)
                                                                                  : double(strip_indices.size()));
                        say("%-6s %-16s %-9s %-4s %-4s %9.2f %8u %8.3f %8.3f%s", large ? "large" : "tiny", f.name,
                            kPrimNames[prim], tex_on ? "on" : "off", fog_on ? "on" : "off", ms, drawn_tris,
                            drawn_tris ? ms * 1000 / drawn_tris : 0.0, drawn_tris ? ms * 1000 / sent : 0.0, FAILED(drawn) ? "  (draw failed)" : "");
                        if (loop_prim >= 0) say("  (%d frames)", frames);
                        say("\n");
                    }
                }
            }
            if (vb_indexed) vb_indexed->Release();
            if (vb_flat) vb_flat->Release();
            if (ib) ib->Release();
        }
    }
    if (g_out) fclose(g_out);
    return 0;
}
