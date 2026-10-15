// CPU bench for the mobile track (docs/porting/MOBILE.md, "Measured first").
//
// The same source runs on the Mac, the Pi 1B (armv6 Linux) and an Android
// phone (a static arm64 binary under adb), single-threaded, so the ratios
// between them are measured rather than guessed from clock speeds:
//
//   sim     the game's own simulation (src/game, compiled through
//           ../game-mcu's overlay as dragon_sim does): a hoard run flown by
//           the demo pilot, and an arena match against four bots, at the
//           desktop's 60 Hz step
//   raster  game-mcu's 320x240 software renderer drawing those frames: the
//           Pi's no-GPU route (M3)
//   mem     memcpy and a dependent-load chase: bandwidth and latency
//   flops   a scalar float multiply-add chain and a scalar loop over arrays
//
// Usage: cpu_bench [--frames N] [--quick]

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "render/target.h"
#include "sim/game.h"

namespace {

using namespace mcu;

uint8_t g_color[kPixels];
uint16_t g_depth[kPixels];

double now_ms() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch()).count();
}

struct Series {
    std::vector<double> v;
    void add(double x) { v.push_back(x); }
    double mean() const {
        double s = 0;
        for (double x : v) s += x;
        return v.empty() ? 0 : s / double(v.size());
    }
    double pct(double p) const {
        if (v.empty()) return 0;
        std::vector<double> s = v;
        std::sort(s.begin(), s.end());
        return s[size_t(p * double(s.size() - 1))];
    }
};

void bench_game(const char* name, const GameOptions& options, int frames) {
    static Game game;  // large; off the stack, and reused between modes
    game.init(options);
    Target target{g_color, g_depth};
    Series update, render;
    double tris = 0, pixels = 0;
    const float dt = 1.0f / 60.0f;
    const Controls idle;
    for (int f = 0; f < frames; ++f) {
        const double t0 = now_ms();
        game.update(dt, idle);
        const double t1 = now_ms();
        game.render(target);
        const double t2 = now_ms();
        game.set_render_ms(float(t2 - t1));
        update.add(t1 - t0);
        render.add(t2 - t1);
        tris += game.stats().triangles_drawn;
        pixels += game.stats().pixels_written;
    }
    std::printf("%-6s sim    mean %7.3f ms  p50 %7.3f  p99 %7.3f  max %7.3f  (%d frames at 60 Hz)\n", name,
                update.mean(), update.pct(0.5), update.pct(0.99), update.pct(1.0), frames);
    std::printf("%-6s raster mean %7.3f ms  p50 %7.3f  p99 %7.3f  max %7.3f  (%.0f tris, %.0f px written a frame)\n",
                name, render.mean(), render.pct(0.5), render.pct(0.99), render.pct(1.0), tris / frames,
                pixels / frames);
    const game::FlightState& s = game.player();
    std::printf("%-6s after %.0f s: pos (%.0f, %.0f, %.0f) speed %.1f m/s, %d bots\n", name, frames * dt,
                s.position.x, s.position.y, s.position.z, s.airspeed, game.bots());
}

volatile uint32_t g_sink;

void bench_memory() {
    const size_t bytes = 32u << 20;
    std::vector<uint8_t> a(bytes, 1), b(bytes, 2);
    double best = 1e9;
    for (int r = 0; r < 5; ++r) {
        const double t0 = now_ms();
        std::memcpy(b.data(), a.data(), bytes);
        best = std::min(best, now_ms() - t0);
        g_sink = b[r * 4096];
    }
    std::printf("mem    memcpy %.0f MB/s (32 MiB, best of 5)\n", double(bytes) / (1 << 20) / (best / 1000.0));

    // Dependent loads over 16 MiB in a random cycle: DRAM latency.
    const size_t n = (16u << 20) / sizeof(uint32_t);
    std::vector<uint32_t> next(n);
    for (size_t i = 0; i < n; ++i) next[i] = uint32_t(i);
    uint32_t s = 12345;
    for (size_t i = n - 1; i > 0; --i) {  // Sattolo: one cycle through all
        s = s * 1664525u + 1013904223u;
        const size_t j = s % i;
        std::swap(next[i], next[j]);
    }
    const int hops = 2'000'000;
    uint32_t p = 0;
    const double t0 = now_ms();
    for (int i = 0; i < hops; ++i) p = next[p];
    const double ms = now_ms() - t0;
    g_sink = p;
    std::printf("mem    latency %.1f ns a dependent load (16 MiB random cycle)\n", ms * 1e6 / hops);
}

void bench_flops() {
    // Four independent multiply-add chains: what a scalar FPU retires.
    float x0 = 1.0f, x1 = 1.1f, x2 = 1.2f, x3 = 1.3f;
    const float m = 0.999999f, c = 0.000001f;
    const int iters = 20'000'000;
    double t0 = now_ms();
    for (int i = 0; i < iters; ++i) {
        x0 = x0 * m + c;
        x1 = x1 * m + c;
        x2 = x2 * m + c;
        x3 = x3 * m + c;
    }
    double ms = now_ms() - t0;
    g_sink = uint32_t(x0 + x1 + x2 + x3);
    std::printf("flops  %.0f Mflop/s scalar mul+add (4 chains)\n", 8.0 * iters / (ms * 1000.0));

    // A 4x4 matrix times 64K vec4s from memory: the shape of vertex work.
    const int nv = 65536;
    std::vector<float> in(nv * 4, 0.5f), out(nv * 4);
    float mat[16];
    for (int i = 0; i < 16; ++i) mat[i] = 0.1f * float(i);
    const int reps = 40;
    t0 = now_ms();
    for (int r = 0; r < reps; ++r) {
        for (int v = 0; v < nv; ++v) {
            const float* a = &in[v * 4];
            float* o = &out[v * 4];
            for (int k = 0; k < 4; ++k)
                o[k] = mat[k] * a[0] + mat[4 + k] * a[1] + mat[8 + k] * a[2] + mat[12 + k] * a[3];
        }
        in[r] = out[r * 3];
    }
    ms = now_ms() - t0;
    g_sink = uint32_t(out[7]);
    std::printf("flops  %.1f M vertices/s through a 4x4 transform\n", double(nv) * reps / (ms * 1000.0));
}

// CPU skinning, as the ff tier does it where a vertex program cannot (the
// Pi's VC4 links no 4-bone palette shader): position and normal, four bones
// a vertex from a 60-bone palette of 3x4 matrices.
void bench_skin() {
    const int nv = 4096, bones = 60;
    struct In {
        float p[3], n[3], w[4];
        uint8_t b[4];
    };
    std::vector<In> in(nv);
    std::vector<float> out(nv * 6), pal(bones * 12);
    for (int i = 0; i < nv; ++i) {
        In& v = in[i];
        v.p[0] = float(i % 64), v.p[1] = float(i / 64), v.p[2] = 0.5f;
        v.n[0] = 0.0f, v.n[1] = 0.0f, v.n[2] = 1.0f;
        v.w[0] = 0.4f, v.w[1] = 0.3f, v.w[2] = 0.2f, v.w[3] = 0.1f;
        for (int k = 0; k < 4; ++k) v.b[k] = uint8_t((i * 7 + k * 13) % bones);
    }
    for (int b = 0; b < bones; ++b)
        for (int k = 0; k < 12; ++k) pal[b * 12 + k] = (k % 5 == 0) ? 1.0f : 0.01f * float(b);
    const int reps = 50;
    const double t0 = now_ms();
    for (int r = 0; r < reps; ++r) {
        pal[r % (bones * 12)] += 0.001f;
        for (int i = 0; i < nv; ++i) {
            const In& v = in[i];
            float m[12] = {};
            for (int k = 0; k < 4; ++k) {
                const float* b = &pal[v.b[k] * 12];
                for (int e = 0; e < 12; ++e) m[e] += b[e] * v.w[k];
            }
            float* o = &out[i * 6];
            for (int row = 0; row < 3; ++row) {
                o[row] = m[row * 4] * v.p[0] + m[row * 4 + 1] * v.p[1] + m[row * 4 + 2] * v.p[2] + m[row * 4 + 3];
                o[3 + row] = m[row * 4] * v.n[0] + m[row * 4 + 1] * v.n[1] + m[row * 4 + 2] * v.n[2];
            }
        }
    }
    const double ms = now_ms() - t0;
    g_sink = uint32_t(out[11]);
    std::printf("skin   %.3f ms per 1000 vertices (4 bones, position and normal)\n", ms / reps / (nv / 1000.0));
}

}  // namespace

int main(int argc, char** argv) {
    int frames = 3600;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atoi(argv[++i]);
        if (!std::strcmp(argv[i], "--quick")) frames = 600;
    }
    const double t0 = now_ms();
    bench_flops();
    bench_memory();
    bench_skin();
    GameOptions run;
    run.autopilot = true;
    bench_game("run", run, frames);
    GameOptions arena;
    arena.autopilot = true;
    arena.arena = true;
    arena.bots = 4;
    bench_game("arena", arena, frames);
    std::printf("total  %.1f s\n", (now_ms() - t0) / 1000.0);
    return 0;
}
