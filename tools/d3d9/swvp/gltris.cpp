// gltris: tris.exe's grid through the OpenGL driver instead of Direct3D.
//
//   gltris.exe [OUT.txt]
//
// tris.exe found the G550's Direct3D driver at about 3 us a triangle and
// 0.8 a vertex whatever the fill, with the time spent in the kernel. The
// same card has a second, separately written driver: Matrox's OpenGL ICD.
// If it draws the same triangles much faster, the D3D driver is what is
// slow, not the card or the bridge in front of it. The grid, the sizes and
// the texture are tris.exe's (an orthographic projection puts the vertices
// where tris.exe's pre-transformed ones are), drawn each way OpenGL 1.1
// has: immediate mode, glDrawElements, glDrawArrays, strips a row, a
// display list (which an ICD may keep where the card can fetch it), and
// glDrawElements in 1,024 pieces; "none" draws nothing, the frame's fixed
// cost. Each case is timed to glFinish; REPEAT=N in the environment draws
// the grid N times a frame. Prints
// the pixel format's flags first: a generic format is Microsoft's software
// renderer, and its numbers mean nothing about the card. Built with the XP
// toolchain beside tris.exe (build.sh).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

const int GRID = 96;
const int FRAMES = 6;
const int W = 320, H = 240;

FILE* g_out = nullptr;
void say(const char* fmt, ...) {
    char line[2048];
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

struct Vertex {
    float x, y, z;
    GLubyte rgba[4];
    float u, v;
};

}  // namespace

int main(int argc, char** argv) {
    g_out = fopen(argc > 1 ? argv[1] : "gltris.txt", "w");
    WNDCLASSA wc = {};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "gltris";
    RegisterClassA(&wc);
    RECT rc = {0, 0, W, H};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowA("gltris", "gltris", WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0,
                              rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, wc.hInstance, nullptr);
    HDC dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 16;
    const int pf = ChoosePixelFormat(dc, &pfd);
    if (!pf || !SetPixelFormat(dc, pf, &pfd)) {
        say("no pixel format (%d), error %lu\n", pf, GetLastError());
        return 1;
    }
    DescribePixelFormat(dc, pf, sizeof pfd, &pfd);
    const bool generic = (pfd.dwFlags & PFD_GENERIC_FORMAT) && !(pfd.dwFlags & PFD_GENERIC_ACCELERATED);
    say("pixel format %d: flags %08lx, colour %u, depth %u -- %s\n", pf, pfd.dwFlags, pfd.cColorBits, pfd.cDepthBits,
        generic ? "GENERIC (Microsoft's software renderer)" : "the driver's ICD");
    HGLRC glrc = wglCreateContext(dc);
    if (!glrc || !wglMakeCurrent(dc, glrc)) {
        say("wglCreateContext/MakeCurrent failed %lu\n", GetLastError());
        return 1;
    }
    say("GL_VENDOR %s\nGL_RENDERER %s\nGL_VERSION %s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER),
        glGetString(GL_VERSION));
    say("GL_EXTENSIONS %s\n", glGetString(GL_EXTENSIONS));

    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, W, H, 0, -1, 1);  // y down, as tris.exe's screen coordinates
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glShadeModel(GL_SMOOTH);

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    std::vector<GLubyte> texels(64 * 64 * 4);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const GLubyte c = ((x ^ y) & 8) ? 255 : 128;
            GLubyte* t = &texels[(y * 64 + x) * 4];
            t[0] = t[1] = t[2] = c;
            t[3] = 255;
        }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    std::vector<GLushort> list;
    for (int z = 0; z + 1 < GRID; ++z)
        for (int x = 0; x + 1 < GRID; ++x) {
            const GLushort a = GLushort(z * GRID + x), b = GLushort(a + 1), c = GLushort(a + GRID), d = GLushort(c + 1);
            const GLushort quad[6] = {a, c, b, b, c, d};
            list.insert(list.end(), quad, quad + 6);
        }
    const GLsizei tris = GLsizei(list.size() / 3);
    const char* names[] = {"immediate", "elements", "arrays", "strips", "dlist", "elem/1k", "none"};
    const char* repeat_env = getenv("REPEAT");
    const int repeat = repeat_env && atoi(repeat_env) > 0 ? atoi(repeat_env) : 1;
    if (repeat > 1) say("the grid drawn %d times a frame\n", repeat);
    say("%-6s %-10s %-4s %9s %8s %8s %8s\n", "size", "method", "tex", "ms/frame", "tris", "us/tri", "us/vert");
    for (int large = 0; large < 2; ++large) {
        std::vector<Vertex> verts(GRID * GRID);
        const float cell_x = large ? float(W) / (GRID - 1) : 0.6f, cell_y = large ? float(H) / (GRID - 1) : 0.6f;
        const float x0 = large ? 0.0f : W * 0.5f - cell_x * GRID * 0.5f, y0 = large ? 0.0f : H * 0.5f - cell_y * GRID * 0.5f;
        for (int z = 0; z < GRID; ++z)
            for (int x = 0; x < GRID; ++x) {
                Vertex& o = verts[z * GRID + x];
                o.x = x0 + x * cell_x;
                o.y = y0 + z * cell_y;
                o.z = 0;
                o.rgba[0] = 0xCC;
                o.rgba[1] = 0x66;
                o.rgba[2] = 0x33;
                o.rgba[3] = 0xFF;
                o.u = x / 8.0f;
                o.v = z / 8.0f;
            }
        std::vector<Vertex> flat;
        for (GLushort i : list) flat.push_back(verts[i]);
        std::vector<std::vector<Vertex>> strips(GRID - 1);
        for (int z = 0; z + 1 < GRID; ++z)
            for (int x = 0; x < GRID; ++x) {
                strips[z].push_back(verts[z * GRID + x]);
                strips[z].push_back(verts[(z + 1) * GRID + x]);
            }
        auto point_at = [](const std::vector<Vertex>& v) {
            glVertexPointer(3, GL_FLOAT, sizeof(Vertex), &v[0].x);
            glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), v[0].rgba);
            glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), &v[0].u);
        };
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        point_at(verts);
        const GLuint dlist = glGenLists(1);
        glNewList(dlist, GL_COMPILE);
        glDrawElements(GL_TRIANGLES, GLsizei(list.size()), GL_UNSIGNED_SHORT, list.data());
        glEndList();
        for (int method = 0; method < 7; ++method) {
            for (int tex_on = 0; tex_on < 2; ++tex_on) {
                if (tex_on) glEnable(GL_TEXTURE_2D);
                else glDisable(GL_TEXTURE_2D);
                double total = 0;
                for (int frame = -1; frame < FRAMES; ++frame) {
                    const double t0 = now_us();
                    glClearColor(0.125f, 0.19f, 0.25f, 1);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    for (int pass = 0; pass < repeat; ++pass) {
                    if (method == 6) {
                    } else if (method == 0) {
                        glBegin(GL_TRIANGLES);
                        for (GLushort i : list) {
                            const Vertex& v = verts[i];
                            glColor4ubv(v.rgba);
                            glTexCoord2f(v.u, v.v);
                            glVertex3f(v.x, v.y, v.z);
                        }
                        glEnd();
                    } else if (method == 1) {
                        point_at(verts);
                        glDrawElements(GL_TRIANGLES, GLsizei(list.size()), GL_UNSIGNED_SHORT, list.data());
                    } else if (method == 2) {
                        point_at(flat);
                        glDrawArrays(GL_TRIANGLES, 0, GLsizei(flat.size()));
                    } else if (method == 3) {
                        for (const auto& s : strips) {
                            point_at(s);
                            glDrawArrays(GL_TRIANGLE_STRIP, 0, GLsizei(s.size()));
                        }
                    } else if (method == 4) {
                        glCallList(dlist);
                    } else {
                        point_at(verts);
                        for (int i = 0; i < 1024; ++i) {
                            const GLsizei t0i = GLsizei(tris * i / 1024), t1i = GLsizei(tris * (i + 1) / 1024);
                            if (t1i > t0i)
                                glDrawElements(GL_TRIANGLES, (t1i - t0i) * 3, GL_UNSIGNED_SHORT, list.data() + t0i * 3);
                        }
                    }
                    }
                    glFinish();
                    if (frame >= 0) total += now_us() - t0;
                    SwapBuffers(dc);
                }
                const double ms = total / FRAMES / 1000.0;
                const int drawn_tris = method == 6 ? 0 : tris * repeat;
                const double sent = repeat * (method == 2 ? tris * 3.0 : method == 3 ? (GRID - 1) * GRID * 2.0
                                    : method == 0                          ? tris * 3.0
                                                                           : double(GRID * GRID));
                say("%-6s %-10s %-4s %9.2f %8d %8.3f %8.3f  (gl error %04x)\n", large ? "large" : "tiny", names[method],
                    tex_on ? "on" : "off", ms, drawn_tris, drawn_tris ? ms * 1000 / drawn_tris : 0.0,
                    drawn_tris ? ms * 1000 / sent : 0.0, glGetError());
            }
        }
        glDeleteLists(dlist, 1);
    }
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(glrc);
    ReleaseDC(hwnd, dc);
    if (g_out) fclose(g_out);
    return 0;
}
