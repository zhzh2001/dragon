// The retro tiers' texture budget (gfx/texture.h): capping, the CPU mip
// chain and block compression must keep a texture's content, only coarser.

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "gfx/texture.h"

static int g_checks = 0, g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(cond)) {                                                    \
            ++g_failures;                                                 \
            std::printf("  FAIL (line %d): %s\n", __LINE__, #cond);       \
        }                                                                 \
    } while (0)

namespace {

gfx::ImageData make(int w, int h, uint8_t (*f)(int x, int y, int c)) {
    gfx::ImageData img;
    img.width = w;
    img.height = h;
    img.rgba.resize(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 4; ++c) img.rgba[(size_t(y) * w + x) * 4 + c] = f(x, y, c);
    return img;
}

// BC1's colour half: two RGB565 endpoints and 2-bit indices (the four-colour
// mode, which stb_dxt always writes for an opaque block).
void decode_bc1_colour(const uint8_t* block, uint8_t out[16][3]) {
    const int c0 = block[0] | block[1] << 8, c1 = block[2] | block[3] << 8;
    auto expand = [](int c, int* rgb) {
        rgb[0] = ((c >> 11) & 31) * 255 / 31;
        rgb[1] = ((c >> 5) & 63) * 255 / 63;
        rgb[2] = (c & 31) * 255 / 31;
    };
    int p[4][3];
    expand(c0, p[0]);
    expand(c1, p[1]);
    for (int k = 0; k < 3; ++k) {
        p[2][k] = (2 * p[0][k] + p[1][k]) / 3;
        p[3][k] = (p[0][k] + 2 * p[1][k]) / 3;
    }
    const uint32_t bits = uint32_t(block[4]) | uint32_t(block[5]) << 8 | uint32_t(block[6]) << 16 |
                          uint32_t(block[7]) << 24;
    for (int i = 0; i < 16; ++i)
        for (int k = 0; k < 3; ++k) out[i][k] = uint8_t(p[(bits >> (2 * i)) & 3][k]);
}

}  // namespace

int main() {
    // Capping: a 4096x2048 map fits a 1024 budget at 1024x512, mean intact.
    {
        const gfx::ImageData big = make(4096, 2048, [](int x, int y, int c) { return uint8_t(c == 3 ? 255 : (x ^ y) & 255); });
        const gfx::ImageData capped = gfx::cap_image(big, 1024, false);
        CHECK(capped.width == 1024 && capped.height == 512);
        double a = 0, b = 0;
        for (size_t i = 0; i < big.rgba.size(); i += 4) a += big.rgba[i];
        for (size_t i = 0; i < capped.rgba.size(); i += 4) b += capped.rgba[i];
        a /= double(big.rgba.size() / 4);
        b /= double(capped.rgba.size() / 4);
        std::printf("  cap: mean %.2f -> %.2f\n", a, b);
        CHECK(std::fabs(a - b) < 1.0);
        CHECK(gfx::cap_image(big, 0, false).width == 4096);  // no cap
    }

    // A colour average is taken in light: black and white make the sRGB
    // code for half the light (188), a data map's average stays 128.
    {
        const gfx::ImageData checker = make(2, 2, [](int x, int y, int c) { return uint8_t(c == 3 ? 255 : ((x + y) & 1) * 255); });
        const gfx::ImageData colour = gfx::cap_image(checker, 1, true);
        const gfx::ImageData data = gfx::cap_image(checker, 1, false);
        std::printf("  2x2 checker: colour %d, data %d\n", colour.rgba[0], data.rgba[0]);
        CHECK(colour.rgba[0] == 188);
        CHECK(data.rgba[0] == 128);
    }

    // Mip chains: to 1x1 for plain texels, to the last 4-aligned level for
    // blocks.
    {
        const gfx::ImageData img = make(1024, 256, [](int, int, int) { return uint8_t(200); });
        const auto full = gfx::build_mips(img, true, false);
        const auto blocks = gfx::build_mips(img, true, true);
        CHECK(full.size() == 11);
        CHECK(full.back().width == 1 && full.back().height == 1);
        CHECK(blocks.size() == 7);  // 1024x256 .. 16x4
        CHECK(blocks.back().width == 16 && blocks.back().height == 4);
        CHECK(full[3].rgba[0] == 200);  // a flat image stays flat
    }

    // Block compression: sizes, opacity, and a smooth gradient within a few
    // code values of the source.
    {
        const gfx::ImageData ramp = make(64, 64, [](int x, int y, int c) {
            return uint8_t(c == 3 ? 255 : c == 0 ? x * 4 : c == 1 ? y * 4 : 128);
        });
        CHECK(!gfx::has_alpha(ramp));
        const std::vector<uint8_t> bc1 = gfx::encode_bc(ramp, false);
        const std::vector<uint8_t> bc3 = gfx::encode_bc(ramp, true);
        CHECK(bc1.size() == 16 * 16 * 8);
        CHECK(bc3.size() == 16 * 16 * 16);
        double err = 0;
        for (int by = 0; by < 16; ++by) {
            for (int bx = 0; bx < 16; ++bx) {
                uint8_t texels[16][3];
                decode_bc1_colour(&bc1[size_t(by * 16 + bx) * 8], texels);
                for (int i = 0; i < 16; ++i) {
                    const int x = bx * 4 + i % 4, y = by * 4 + i / 4;
                    for (int k = 0; k < 3; ++k) err += std::abs(texels[i][k] - ramp.rgba[(size_t(y) * 64 + x) * 4 + k]);
                }
            }
        }
        err /= 64.0 * 64.0 * 3.0;
        std::printf("  BC1 gradient: mean error %.2f code values\n", err);
        CHECK(err < 3.0);
        gfx::ImageData cut = ramp;
        cut.rgba[3] = 0;
        CHECK(gfx::has_alpha(cut));
    }

    std::printf("texture_budget: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
