// GPU bench for the mobile track (docs/porting/MOBILE.md, "Measured first").
//
// GLES 2.0 only, so one source runs on every GPU the track names: the Pi 1B's
// VideoCore IV (Broadcom's firmware driver, or Mesa's vc4), the phones'
// Adrenos, the VisionFive 2's PowerVR. Everything renders offscreen into a
// framebuffer object (RGBA8 colour, 16-bit depth) from a 1x1 pbuffer context,
// so no display is involved and the same numbers come from every platform.
//
// Each test draws "frames" (bind, clear, draw, flush) for a fixed time and
// prints ms a frame and the rate that matters for it:
//
//   clear       colour and depth clear alone: the per-frame floor of a tiler
//   fill_flat   32 full-screen quads, one colour: raw fill rate
//   fill_tex    the same, one bilinear mipmapped texture
//   fill_ff     the ff tier's pixel: texture x vertex colour, fog mixed in
//   fill_blend  fill_tex alpha-blended: particles and smoke (every fill test
//               blends -- see quads() -- so this differs only in its factors)
//   fill_heavy  a modern-tier pixel: three textures, a normal, a specular
//               pow and exponential fog
//   tris        a 64K-vertex lit grid in small triangles: vertex rate
//   skinned     the same grid skinned on the GPU, four bones a vertex from a
//               24-bone palette: the creatures' vertex shader
//   draws       2000 draw calls of one quad, a uniform each: driver cost
//   scene       an ff-tier-like frame: a 32K-triangle terrain with fog, 400
//               alpha-tested tree cards, a 6K-triangle skinned creature
//
// Usage: gles_bench [WIDTH HEIGHT] [--seconds S] [--only NAME] [--grid N]
//
// --grid sets the vertex test's grid (N x N vertices, default 256: 64K
// vertices and 130K triangles a draw).
//
// Platforms (one #define): GB_ANDROID (EGL pbuffer), GB_BRCM (the Pi's
// firmware driver, /opt/vc), GB_MESA (EGL on a surfaceless or GBM display).

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef GB_BRCM
#include <bcm_host.h>
#endif
#ifdef GB_MESA
#include <EGL/eglext.h>
#endif

static int W = 640, H = 480;
static double g_seconds = 2.0;

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void die(const char* what) {
    fprintf(stderr, "gles_bench: %s (egl 0x%x, gl 0x%x)\n", what, eglGetError(), glGetError());
    exit(1);
}

// ---- context ----------------------------------------------------------------

static EGLDisplay g_dpy;

static void init_egl(void) {
#ifdef GB_BRCM
    bcm_host_init();
#endif
#ifdef GB_MESA
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    g_dpy = get_platform ? get_platform(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL)
                         : EGL_NO_DISPLAY;
    if (g_dpy == EGL_NO_DISPLAY) g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#else
    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#endif
    EGLint major, minor;
    if (!eglInitialize(g_dpy, &major, &minor)) die("eglInitialize");
    const EGLint attrs[] = {EGL_SURFACE_TYPE,
#ifdef GB_MESA
                            0,
#else
                            EGL_PBUFFER_BIT,
#endif
                            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                            EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLConfig config;
    EGLint n = 0;
    if (!eglChooseConfig(g_dpy, attrs, &config, 1, &n) || n < 1) die("eglChooseConfig");
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext ctx = eglCreateContext(g_dpy, config, EGL_NO_CONTEXT, ctx_attrs);
    if (ctx == EGL_NO_CONTEXT) die("eglCreateContext");
#ifdef GB_MESA
    EGLSurface surf = EGL_NO_SURFACE;
#else
    const EGLint pb[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    EGLSurface surf = eglCreatePbufferSurface(g_dpy, config, pb);
    if (surf == EGL_NO_SURFACE) die("eglCreatePbufferSurface");
#endif
    if (!eglMakeCurrent(g_dpy, surf, surf, ctx)) die("eglMakeCurrent");
    printf("egl %d.%d  %s | %s | %s\n", major, minor, glGetString(GL_VENDOR), glGetString(GL_RENDERER),
           glGetString(GL_VERSION));
}

// ---- resources --------------------------------------------------------------

static GLuint g_fbo;

static void init_target(void) {
    GLuint color, depth;
    glGenTextures(1, &color);
    glBindTexture(GL_TEXTURE_2D, color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, W, H);
    glGenFramebuffers(1, &g_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) die("framebuffer incomplete");
    glViewport(0, 0, W, H);
}

// A 256x256 RGBA texture with a full mip chain: noise with an alpha cut-out.
static GLuint make_texture(uint32_t seed) {
    const int n = 256;
    uint8_t* px = malloc(n * n * 4);
    for (int i = 0; i < n * n; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const int x = i % n, y = i / n;
        const float dx = (x - 128) / 128.0f, dy = (y - 128) / 128.0f;
        px[i * 4 + 0] = (uint8_t)(seed >> 24);
        px[i * 4 + 1] = (uint8_t)(128 + (x ^ y) % 64);
        px[i * 4 + 2] = (uint8_t)(seed >> 16);
        px[i * 4 + 3] = dx * dx + dy * dy < 0.8f ? 255 : 0;
    }
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, n, n, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    free(px);
    return t;
}

static GLuint compile(GLenum type, const char* src, int must) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        if (!must) {
            printf("(a shader did not compile: %s)\n", log);
            return 0;
        }
        fprintf(stderr, "%s\n--\n%s\n", src, log);
        die("shader compile");
    }
    return s;
}

// Attributes are bound by name to fixed slots: 0 pos, 1 uv, 2 normal/colour,
// 3 bone indices, 4 bone weights.
static GLuint try_program(const char* vs, const char* fs, int must) {
    GLuint p = glCreateProgram();
    const GLuint v = compile(GL_VERTEX_SHADER, vs, must), f = compile(GL_FRAGMENT_SHADER, fs, must);
    if (!v || !f) return 0;
    glAttachShader(p, v);
    glAttachShader(p, f);
    glBindAttribLocation(p, 0, "a_pos");
    glBindAttribLocation(p, 1, "a_uv");
    glBindAttribLocation(p, 2, "a_nrm");
    glBindAttribLocation(p, 3, "a_bones");
    glBindAttribLocation(p, 4, "a_weights");
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "%s\n", log);
        if (must) die("program link");
        printf("(a shader did not link: %s)\n", log);
        return 0;
    }
    glUseProgram(p);
    for (int i = 0; i < 3; ++i) {
        char name[8];
        snprintf(name, sizeof name, "u_tex%d", i);
        const GLint loc = glGetUniformLocation(p, name);
        if (loc >= 0) glUniform1i(loc, i);
    }
    return p;
}

static GLuint program(const char* vs, const char* fs) { return try_program(vs, fs, 1); }

// Vertex layout: pos3 uv2 nrm3 bones4 weights4 = 16 floats.
enum { kStride = 16 };

typedef struct {
    GLuint vbo, ibo;
    int indices;
} Mesh;

static Mesh upload(const float* v, int nv, const uint16_t* idx, int ni) {
    Mesh m = {0, 0, ni};
    glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nv * kStride * 4, v, GL_STATIC_DRAW);
    glGenBuffers(1, &m.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)ni * 2, idx, GL_STATIC_DRAW);
    return m;
}

static void bind_mesh(const Mesh* m) {
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo);
    const int sizes[5] = {3, 2, 3, 4, 4}, offs[5] = {0, 3, 5, 8, 12};
    for (int i = 0; i < 5; ++i) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, sizes[i], GL_FLOAT, GL_FALSE, kStride * 4, (void*)(intptr_t)(offs[i] * 4));
    }
}

static void draw_mesh(const Mesh* m) { glDrawElements(GL_TRIANGLES, m->indices, GL_UNSIGNED_SHORT, 0); }

// An n x n vertex grid over [x0,x1]x[y0,y1] at depth z, with a little relief.
static Mesh make_grid(int n, float x0, float y0, float x1, float y1, float z, int bones) {
    float* v = calloc((size_t)n * n * kStride, 4);
    uint16_t* idx = malloc((size_t)(n - 1) * (n - 1) * 6 * 2);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            float* p = v + (j * n + i) * kStride;
            const float u = (float)i / (n - 1), t = (float)j / (n - 1);
            p[0] = x0 + (x1 - x0) * u;
            p[1] = y0 + (y1 - y0) * t;
            p[2] = z + 0.05f * sinf(u * 17.0f) * cosf(t * 13.0f);
            p[3] = u * 8.0f;
            p[4] = t * 8.0f;
            p[5] = 0.2f * sinf(u * 9.0f);
            p[6] = 0.3f;
            p[7] = 1.0f;
            for (int k = 0; k < 4; ++k) p[8 + k] = (float)((i + j + k * 5) % bones);
            p[12] = 0.4f, p[13] = 0.3f, p[14] = 0.2f, p[15] = 0.1f;
        }
    int k = 0;
    for (int j = 0; j + 1 < n; ++j)
        for (int i = 0; i + 1 < n; ++i) {
            const uint16_t a = (uint16_t)(j * n + i), b = a + 1, c = a + n, d = c + 1;
            idx[k++] = a, idx[k++] = b, idx[k++] = c;
            idx[k++] = b, idx[k++] = d, idx[k++] = c;
        }
    Mesh m = upload(v, n * n, idx, k);
    free(v);
    free(idx);
    return m;
}

// ---- shaders ----------------------------------------------------------------

static const char* kVsQuad =
    "attribute vec3 a_pos; attribute vec2 a_uv; attribute vec3 a_nrm;\n"
    "uniform vec4 u_offset;\n"
    "varying vec2 v_uv; varying vec4 v_col; varying float v_fog;\n"
    "void main() {\n"
    "  v_uv = a_uv * 2.0 + u_offset.xy; v_col = vec4(0.6 + a_nrm, 1.0); v_fog = a_pos.y * 0.5 + 0.5;\n"
    "  gl_Position = vec4(a_pos.xy * (1.0 - u_offset.w), a_pos.z + u_offset.z, 1.0);\n"
    "}\n";

static const char* kFsFlat =
    "precision mediump float; uniform vec4 u_color;\n"
    "void main() { gl_FragColor = u_color; }\n";

static const char* kFsTex =
    "precision mediump float; uniform sampler2D u_tex0; varying vec2 v_uv;\n"
    "void main() { gl_FragColor = texture2D(u_tex0, v_uv); }\n";

static const char* kFsFF =
    "precision mediump float; uniform sampler2D u_tex0; uniform vec4 u_fog;\n"
    "varying vec2 v_uv; varying vec4 v_col; varying float v_fog;\n"
    "void main() { vec4 c = texture2D(u_tex0, v_uv) * v_col;\n"
    "  gl_FragColor = vec4(mix(c.rgb, u_fog.rgb, v_fog), c.a); }\n";

static const char* kFsHeavy =
    "precision mediump float; uniform sampler2D u_tex0, u_tex1, u_tex2; uniform vec4 u_fog;\n"
    "varying vec2 v_uv; varying vec4 v_col; varying float v_fog;\n"
    "void main() {\n"
    "  vec4 albedo = texture2D(u_tex0, v_uv);\n"
    "  vec3 n = normalize(texture2D(u_tex1, v_uv * 3.1).xyz * 2.0 - 1.0 + v_col.xyz * 0.2);\n"
    "  vec4 detail = texture2D(u_tex2, v_uv * 7.3);\n"
    "  vec3 l = normalize(vec3(0.3, 0.8, 0.5)), v = normalize(vec3(0.0, 0.2, 1.0));\n"
    "  float ndl = max(dot(n, l), 0.0);\n"
    "  float spec = pow(max(dot(n, normalize(l + v)), 0.0), 32.0);\n"
    "  vec3 c = albedo.rgb * detail.rgb * (ndl * vec3(1.0, 0.95, 0.9) + vec3(0.25, 0.3, 0.4)) + spec * 0.4;\n"
    "  float fog = 1.0 - exp(-v_fog * 2.0);\n"
    "  gl_FragColor = vec4(mix(c, u_fog.rgb, fog), albedo.a);\n"
    "}\n";

static const char* kVsLit =
    "attribute vec3 a_pos; attribute vec2 a_uv; attribute vec3 a_nrm;\n"
    "uniform mat4 u_mvp;\n"
    "varying vec2 v_uv; varying vec4 v_col; varying float v_fog;\n"
    "void main() {\n"
    "  vec4 p = u_mvp * vec4(a_pos, 1.0);\n"
    "  vec3 n = normalize(a_nrm);\n"
    "  v_col = vec4(vec3(max(dot(n, vec3(0.3, 0.8, 0.5)), 0.0) + 0.3), 1.0);\n"
    "  v_uv = a_uv; v_fog = clamp(p.w * 0.002, 0.0, 1.0);\n"
    "  gl_Position = p;\n"
    "}\n";

// Four bones from a 24-bone palette of 3x4 matrices (72 vec4 uniforms, inside
// GLES 2.0's guaranteed 128).
static const char* kVsSkinned =
    "attribute vec3 a_pos; attribute vec2 a_uv; attribute vec3 a_nrm;\n"
    "attribute vec4 a_bones; attribute vec4 a_weights;\n"
    "uniform mat4 u_mvp; uniform vec4 u_bones[72];\n"
    "varying vec2 v_uv; varying vec4 v_col; varying float v_fog;\n"
    "vec3 xf(int b, vec4 p) { return vec3(dot(u_bones[b*3], p), dot(u_bones[b*3+1], p), dot(u_bones[b*3+2], p)); }\n"
    "void main() {\n"
    "  vec4 p = vec4(a_pos, 1.0), n = vec4(a_nrm, 0.0);\n"
    "  vec3 sp = xf(int(a_bones.x), p) * a_weights.x + xf(int(a_bones.y), p) * a_weights.y\n"
    "          + xf(int(a_bones.z), p) * a_weights.z + xf(int(a_bones.w), p) * a_weights.w;\n"
    "  vec3 sn = xf(int(a_bones.x), n) * a_weights.x + xf(int(a_bones.y), n) * a_weights.y;\n"
    "  vec4 c = u_mvp * vec4(sp, 1.0);\n"
    "  v_col = vec4(vec3(max(dot(normalize(sn), vec3(0.3, 0.8, 0.5)), 0.0) + 0.3), 1.0);\n"
    "  v_uv = a_uv; v_fog = clamp(c.w * 0.002, 0.0, 1.0);\n"
    "  gl_Position = c;\n"
    "}\n";

// The same with two bones, the normal from the first: for a driver that
// limits indexed uniform reads (VC4 does them through the texture unit).
static const char* kVsSkinned2 =
    "attribute vec3 a_pos; attribute vec2 a_uv; attribute vec3 a_nrm;\n"
    "attribute vec4 a_bones; attribute vec4 a_weights;\n"
    "uniform mat4 u_mvp; uniform vec4 u_bones[72];\n"
    "varying vec2 v_uv; varying vec4 v_col; varying float v_fog;\n"
    "vec3 xf(int b, vec4 p) { return vec3(dot(u_bones[b*3], p), dot(u_bones[b*3+1], p), dot(u_bones[b*3+2], p)); }\n"
    "void main() {\n"
    "  vec4 p = vec4(a_pos, 1.0);\n"
    "  vec3 sp = xf(int(a_bones.x), p) * a_weights.x + xf(int(a_bones.y), p) * (1.0 - a_weights.x);\n"
    "  vec4 c = u_mvp * vec4(sp, 1.0);\n"
    "  v_col = vec4(vec3(max(dot(a_nrm, vec3(0.3, 0.8, 0.5)), 0.0) + 0.3), 1.0);\n"
    "  v_uv = a_uv; v_fog = clamp(c.w * 0.002, 0.0, 1.0);\n"
    "  gl_Position = c;\n"
    "}\n";

static const char* kFsCutout =
    "precision mediump float; uniform sampler2D u_tex0; uniform vec4 u_fog;\n"
    "varying vec2 v_uv; varying vec4 v_col; varying float v_fog;\n"
    "void main() { vec4 c = texture2D(u_tex0, v_uv) * v_col; if (c.a < 0.5) discard;\n"
    "  gl_FragColor = vec4(mix(c.rgb, u_fog.rgb, v_fog), 1.0); }\n";

// ---- the harness ------------------------------------------------------------

typedef void (*DrawFn)(void);

static uint8_t g_probe[4];

static void run(const char* name, DrawFn draw, double work, const char* unit, const char* only) {
    if (only && strcmp(only, name)) return;
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    for (int i = 0; i < 5; ++i) {  // warm-up: shader compiles, uploads, clocks
        draw();
        glFlush();
    }
    glFinish();
    int frames = 0;
    const double t0 = now_s();
    double t;
    do {
        draw();
        glFlush();
        ++frames;
        if ((frames & 3) == 0) glFinish();  // keep the queue from running ahead
        t = now_s();
    } while (t - t0 < g_seconds);
    glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, g_probe);
    const double ms = (now_s() - t0) * 1000.0 / frames;
    if (unit)
        printf("%-10s %8.2f ms  %9.1f %s\n", name, ms, work / (ms / 1000.0) / 1e6, unit);
    else
        printf("%-10s %8.2f ms\n", name, ms);
    fflush(stdout);
    const GLenum err = glGetError();
    if (err) printf("  gl error 0x%x\n", err);
}

static Mesh g_quad, g_grid, g_skin_grid, g_terrain, g_cards, g_creature;
static GLuint g_tex[3];
static GLuint p_flat, p_tex, p_ff, p_heavy, p_lit, p_skinned, p_cutout, p_lit_ff;
static float g_frame;

static void clear_frame(void) {
    glClearColor(0.4f, 0.5f, 0.6f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void set_offset(GLuint p, int layer) {
    glUniform4f(glGetUniformLocation(p, "u_offset"), 0.013f * layer + g_frame * 0.001f, 0.007f * layer, 0.0f, 0.0f);
}

// Every layer is blended. A GPU with hidden-surface removal (Apple's, the
// VisionFive 2's PowerVR) shades only the top one of a stack of opaque
// layers, and measured 131 Gpix/s here; blended, each layer must be shaded.
// Blending is free on the Adreno and on VC4 (fill_tex opaque matched
// fill_blend on both, before this).
static void quads(GLuint p, int layers) {
    clear_frame();
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glUseProgram(p);
    glUniform4f(glGetUniformLocation(p, "u_color"), 0.2f, 0.7f, 0.3f, 1.0f);
    glUniform4f(glGetUniformLocation(p, "u_fog"), 0.6f, 0.7f, 0.8f, 1.0f);
    bind_mesh(&g_quad);
    for (int i = 0; i < layers; ++i) {
        set_offset(p, i);
        draw_mesh(&g_quad);
    }
    glDisable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    g_frame += 1.0f;
}

// Enough full-screen layers that the frame's fixed cost (the clear, the
// tile resolve) is a small part of a fill test.
enum { kLayers = 32 };

static void t_clear(void) { clear_frame(); }
static void t_fill_flat(void) { quads(p_flat, kLayers); }
static void t_fill_tex(void) { quads(p_tex, kLayers); }
static void t_fill_ff(void) { quads(p_ff, kLayers); }
static void t_fill_blend(void) {
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    quads(p_tex, kLayers);
}
static void t_fill_heavy(void) {
    for (int i = 0; i < 3; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, g_tex[i]);
    }
    glActiveTexture(GL_TEXTURE0);
    quads(p_heavy, kLayers / 2);
}

static void perspective(float* m, float aspect) {
    // A camera 3 units back looking down -Z at the [-1,1] grids, column-major.
    const float f = 1.0f / tanf(0.5f), n = 0.5f, fa = 3000.0f;
    memset(m, 0, 16 * 4);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (fa + n) / (n - fa);
    m[11] = -1.0f;
    // Times a view translation of -3 along z: column 3 is P * (0, 0, -3, 1).
    m[14] = 2.0f * fa * n / (n - fa) - 3.0f * m[10];
    m[15] = 3.0f;
}

static void set_mvp(GLuint p) {
    float m[16];
    perspective(m, (float)W / (float)H);
    glUniformMatrix4fv(glGetUniformLocation(p, "u_mvp"), 1, GL_FALSE, m);
    glUniform4f(glGetUniformLocation(p, "u_fog"), 0.6f, 0.7f, 0.8f, 1.0f);
}

static void set_bones(GLuint p) {
    static float b[72 * 4];
    for (int i = 0; i < 24; ++i) {
        const float a = 0.02f * i + g_frame * 0.001f, c = cosf(a), s = sinf(a);
        float* r = b + i * 12;
        r[0] = c, r[1] = -s, r[2] = 0, r[3] = 0.001f * i;
        r[4] = s, r[5] = c, r[6] = 0, r[7] = 0;
        r[8] = 0, r[9] = 0, r[10] = 1, r[11] = 0;
    }
    glUniform4fv(glGetUniformLocation(p, "u_bones"), 72, b);
}

static void t_tris(void) {
    clear_frame();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glUseProgram(p_lit);
    set_mvp(p_lit);
    bind_mesh(&g_grid);
    for (int i = 0; i < 4; ++i) draw_mesh(&g_grid);
    g_frame += 1.0f;
}

static void t_skinned(void) {
    clear_frame();
    glEnable(GL_DEPTH_TEST);
    glUseProgram(p_skinned);
    set_mvp(p_skinned);
    set_bones(p_skinned);
    bind_mesh(&g_skin_grid);
    for (int i = 0; i < 4; ++i) draw_mesh(&g_skin_grid);
    g_frame += 1.0f;
}

static void t_draws(void) {
    clear_frame();
    glDisable(GL_DEPTH_TEST);
    glUseProgram(p_flat);
    bind_mesh(&g_quad);
    // A quad 1% of the screen's width: the cost is the call, not the pixels.
    glUniform4f(glGetUniformLocation(p_flat, "u_offset"), 0.0f, 0.0f, 0.0f, 0.99f);
    const GLint loc = glGetUniformLocation(p_flat, "u_color");
    for (int i = 0; i < 2000; ++i) {
        glUniform4f(loc, (float)(i & 255) / 255.0f, 0.5f, 0.5f, 1.0f);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
    }
}

static void t_scene(void) {
    clear_frame();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glBindTexture(GL_TEXTURE_2D, g_tex[0]);
    glUseProgram(p_lit_ff);
    set_mvp(p_lit_ff);
    bind_mesh(&g_terrain);
    draw_mesh(&g_terrain);
    glUseProgram(p_cutout);
    set_mvp(p_cutout);
    bind_mesh(&g_cards);
    draw_mesh(&g_cards);
    glUseProgram(p_skinned);
    set_mvp(p_skinned);
    set_bones(p_skinned);
    bind_mesh(&g_creature);
    draw_mesh(&g_creature);
    g_frame += 1.0f;
}

// 400 upright cards scattered over the terrain, two crossed quads each.
static Mesh make_cards(void) {
    const int n = 400;
    float* v = calloc((size_t)n * 8 * kStride, 4);
    uint16_t* idx = malloc((size_t)n * 12 * 2);
    uint32_t seed = 7;
    for (int c = 0; c < n; ++c) {
        seed = seed * 1664525u + 1013904223u;
        float x = (float)(seed >> 8) / 16777216.0f * 1.8f - 0.9f;
        seed = seed * 1664525u + 1013904223u;
        float z = -(float)(seed >> 8) / 16777216.0f * 6.0f;
        const float s = 0.08f, y0 = -0.5f;
        for (int q = 0; q < 2; ++q)
            for (int k = 0; k < 4; ++k) {
                float* p = v + ((c * 2 + q) * 4 + k) * kStride;
                const float side = (k & 1) ? s : -s, up = (k & 2) ? 2.0f * s : 0.0f;
                p[0] = x + (q ? 0.0f : side);
                p[1] = y0 + up;
                p[2] = z + (q ? side : 0.0f);
                p[3] = (k & 1) ? 1.0f : 0.0f;
                p[4] = (k & 2) ? 0.0f : 1.0f;
                p[7] = 1.0f;
            }
        for (int q = 0; q < 2; ++q) {
            const uint16_t b = (uint16_t)((c * 2 + q) * 4);
            uint16_t* o = idx + (c * 2 + q) * 6;
            o[0] = b, o[1] = b + 1, o[2] = b + 2, o[3] = b + 1, o[4] = b + 3, o[5] = b + 2;
        }
    }
    Mesh m = upload(v, n * 8, idx, n * 12);
    free(v);
    free(idx);
    return m;
}

int main(int argc, char** argv) {
    const char* only = NULL;
    const char* skin_name = "";
    int grid = 256, info = 0;
    int pos = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--seconds") && i + 1 < argc)
            g_seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--info"))
            info = 1;
        else if (!strcmp(argv[i], "--grid") && i + 1 < argc)
            grid = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--only") && i + 1 < argc)
            only = argv[++i];
        else if (pos == 0)
            W = atoi(argv[i]), ++pos;
        else if (pos == 1)
            H = atoi(argv[i]), ++pos;
    }
    init_egl();
    if (info) {
        // What a GLES 2.0 backend can count on (docs/porting/MOBILE.md, M4).
        static const struct { GLenum e; const char* name; } kLimits[] = {
            {GL_MAX_TEXTURE_SIZE, "max texture size"},
            {GL_MAX_VERTEX_ATTRIBS, "vertex attributes"},
            {GL_MAX_VERTEX_UNIFORM_VECTORS, "vertex uniform vectors"},
            {GL_MAX_FRAGMENT_UNIFORM_VECTORS, "fragment uniform vectors"},
            {GL_MAX_VARYING_VECTORS, "varying vectors"},
            {GL_MAX_TEXTURE_IMAGE_UNITS, "fragment texture units"},
            {GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, "vertex texture units"},
            {GL_MAX_RENDERBUFFER_SIZE, "max renderbuffer size"},
        };
        for (size_t i = 0; i < sizeof kLimits / sizeof kLimits[0]; ++i) {
            GLint v = 0;
            glGetIntegerv(kLimits[i].e, &v);
            printf("%-26s %d\n", kLimits[i].name, v);
        }
        printf("extensions: %s\n", glGetString(GL_EXTENSIONS));
        return 0;
    }
    init_target();
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    printf("target %dx%d, %.1f s a test\n", W, H, g_seconds);
    fflush(stdout);

    for (int i = 0; i < 3; ++i) g_tex[i] = make_texture(1234u + (uint32_t)i * 77u);
    glBindTexture(GL_TEXTURE_2D, g_tex[0]);
    {
        // pos3 uv2 nrm3 bones4 weights4
        static const float q[4 * kStride] = {
            -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
            1,  -1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
            -1, 1,  0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
            1,  1,  0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
        };
        static const uint16_t qi[6] = {0, 1, 2, 1, 3, 2};
        g_quad = upload(q, 4, qi, 6);
    }
    // 256x256 = 64K vertices, 130K triangles in a quarter of the screen.
    g_grid = make_grid(grid, -0.5f, -0.5f, 0.5f, 0.5f, 0.0f, 24);
    g_skin_grid = make_grid(grid, -0.5f, -0.5f, 0.5f, 0.5f, 0.0f, 24);
    // Terrain: 128x128 cells (32K triangles) laid as a floor running away.
    g_terrain = make_grid(129, -4.0f, -4.0f, 4.0f, 4.0f, 0.0f, 1);
    g_cards = make_cards();
    // Creature: 6K triangles (a 56x56 grid), small on screen.
    g_creature = make_grid(56, -0.3f, -0.3f, 0.3f, 0.3f, 0.5f, 24);

    p_flat = program(kVsQuad, kFsFlat);
    p_tex = program(kVsQuad, kFsTex);
    p_ff = program(kVsQuad, kFsFF);
    p_heavy = program(kVsQuad, kFsHeavy);
    p_lit = program(kVsLit, kFsTex);
    p_lit_ff = program(kVsLit, kFsFF);
    p_skinned = try_program(kVsSkinned, kFsFF, 0);
    skin_name = "skinned";
    if (!p_skinned) {
        p_skinned = try_program(kVsSkinned2, kFsFF, 0);
        skin_name = "skinned2";
    }
    if (!p_skinned) {
        p_skinned = program(kVsLit, kFsFF);
        skin_name = "rigid";
    }
    p_cutout = program(kVsLit, kFsCutout);

    // The terrain grid is built upright; tilt it into a floor by its MVP
    // instead of by a second shader: swap y and z in its vertex buffer.
    {
        const int n = 129;
        float* v = calloc((size_t)n * n * kStride, 4);
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                float* p = v + (j * n + i) * kStride;
                const float u = (float)i / (n - 1), t = (float)j / (n - 1);
                p[0] = -4.0f + 8.0f * u;
                p[1] = -0.6f + 0.15f * sinf(u * 11.0f) * cosf(t * 7.0f);
                p[2] = 1.5f - 9.0f * t;
                p[3] = u * 32.0f;
                p[4] = t * 32.0f;
                p[5] = 0.1f, p[6] = 1.0f, p[7] = 0.1f;
            }
        glBindBuffer(GL_ARRAY_BUFFER, g_terrain.vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)n * n * kStride * 4, v, GL_STATIC_DRAW);
        free(v);
    }

    const double px = (double)W * H;
    run("clear", t_clear, px, "Mpix/s", only);
    run("fill_flat", t_fill_flat, px * kLayers, "Mpix/s", only);
    run("fill_tex", t_fill_tex, px * kLayers, "Mpix/s", only);
    run("fill_ff", t_fill_ff, px * kLayers, "Mpix/s", only);
    run("fill_blend", t_fill_blend, px * kLayers, "Mpix/s", only);
    run("fill_heavy", t_fill_heavy, px * (kLayers / 2), "Mpix/s", only);
    run("tris", t_tris, 4.0 * g_grid.indices / 3, "Mtri/s", only);
    run(skin_name, t_skinned, 4.0 * g_skin_grid.indices / 3, "Mtri/s", only);
    run("draws", t_draws, 2000e3, "kdraw/s", only);
    run("scene", t_scene, 0, NULL, only);
    printf("probe %d %d %d %d\n", g_probe[0], g_probe[1], g_probe[2], g_probe[3]);
    return 0;
}
