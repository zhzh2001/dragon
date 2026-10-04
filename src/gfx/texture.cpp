#include "gfx/texture.h"

#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"
#define STB_DXT_IMPLEMENTATION
#include "stb_dxt.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "core/log.h"
#include "gfx/render_tier.h"

namespace gfx {
namespace {

// sRGB <-> linear, for averaging colour in light rather than in code values.
//
// Both tables are constants, not std::pow at start-up: a chain built on the
// Mac must be the chain built on Windows, byte for byte, since a model pack
// (anim/model_pack.h) baked on one is loaded on the other and the glTF
// route must draw the same frame. libm's pow differs by an ulp between
// platforms, and that ulp flipped the rounding of a few texels (the X550
// drew Embercrest's wing 2/255 off its pack). Generated at double
// precision: LUT[i] is code i in linear light; THRESHOLD[k] is the linear
// value where lround(encode(l) * 255) steps from k to k+1 (code k + 0.5), so
// the search below reproduces the former pow-based encode exactly
// (checked over 100,000 samples when the tables were made).
constexpr float SRGB_LUT[256] = {
    0.000000000e+00f, 3.035269835e-04f, 6.070539671e-04f, 9.105809506e-04f, 1.214107934e-03f,
    1.517634918e-03f, 1.821161901e-03f, 2.124688885e-03f, 2.428215868e-03f, 2.731742852e-03f,
    3.035269835e-03f, 3.346535764e-03f, 3.676507324e-03f, 4.024717018e-03f, 4.391442037e-03f,
    4.776953481e-03f, 5.181516702e-03f, 5.605391624e-03f, 6.048833023e-03f, 6.512090793e-03f,
    6.995410187e-03f, 7.499032043e-03f, 8.023192985e-03f, 8.568125618e-03f, 9.134058702e-03f,
    9.721217320e-03f, 1.032982303e-02f, 1.096009401e-02f, 1.161224518e-02f, 1.228648836e-02f,
    1.298303234e-02f, 1.370208305e-02f, 1.444384360e-02f, 1.520851442e-02f, 1.599629337e-02f,
    1.680737575e-02f, 1.764195449e-02f, 1.850022013e-02f, 1.938236096e-02f, 2.028856306e-02f,
    2.121901038e-02f, 2.217388479e-02f, 2.315336618e-02f, 2.415763245e-02f, 2.518685963e-02f,
    2.624122189e-02f, 2.732089164e-02f, 2.842603950e-02f, 2.955683444e-02f, 3.071344373e-02f,
    3.189603307e-02f, 3.310476657e-02f, 3.433980681e-02f, 3.560131488e-02f, 3.688945040e-02f,
    3.820437160e-02f, 3.954623528e-02f, 4.091519691e-02f, 4.231141062e-02f, 4.373502926e-02f,
    4.518620439e-02f, 4.666508634e-02f, 4.817182423e-02f, 4.970656598e-02f, 5.126945837e-02f,
    5.286064702e-02f, 5.448027644e-02f, 5.612849005e-02f, 5.780543019e-02f, 5.951123816e-02f,
    6.124605423e-02f, 6.301001765e-02f, 6.480326669e-02f, 6.662593864e-02f, 6.847816984e-02f,
    7.036009570e-02f, 7.227185068e-02f, 7.421356838e-02f, 7.618538148e-02f, 7.818742181e-02f,
    8.021982031e-02f, 8.228270713e-02f, 8.437621154e-02f, 8.650046204e-02f, 8.865558629e-02f,
    9.084171118e-02f, 9.305896285e-02f, 9.530746663e-02f, 9.758734714e-02f, 9.989872825e-02f,
    1.022417331e-01f, 1.046164841e-01f, 1.070231030e-01f, 1.094617108e-01f, 1.119324278e-01f,
    1.144353738e-01f, 1.169706678e-01f, 1.195384280e-01f, 1.221387722e-01f, 1.247718176e-01f,
    1.274376804e-01f, 1.301364767e-01f, 1.328683216e-01f, 1.356333297e-01f, 1.384316150e-01f,
    1.412632911e-01f, 1.441284709e-01f, 1.470272665e-01f, 1.499597898e-01f, 1.529261520e-01f,
    1.559264637e-01f, 1.589608351e-01f, 1.620293756e-01f, 1.651321945e-01f, 1.682694002e-01f,
    1.714411007e-01f, 1.746474037e-01f, 1.778884160e-01f, 1.811642442e-01f, 1.844749945e-01f,
    1.878207723e-01f, 1.912016827e-01f, 1.946178304e-01f, 1.980693196e-01f, 2.015562538e-01f,
    2.050787364e-01f, 2.086368701e-01f, 2.122307574e-01f, 2.158605001e-01f, 2.195261997e-01f,
    2.232279573e-01f, 2.269658735e-01f, 2.307400485e-01f, 2.345505822e-01f, 2.383975738e-01f,
    2.422811225e-01f, 2.462013267e-01f, 2.501582847e-01f, 2.541520943e-01f, 2.581828529e-01f,
    2.622506575e-01f, 2.663556048e-01f, 2.704977910e-01f, 2.746773121e-01f, 2.788942635e-01f,
    2.831487404e-01f, 2.874408377e-01f, 2.917706498e-01f, 2.961382708e-01f, 3.005437944e-01f,
    3.049873141e-01f, 3.094689228e-01f, 3.139887134e-01f, 3.185467781e-01f, 3.231432091e-01f,
    3.277780981e-01f, 3.324515363e-01f, 3.371636150e-01f, 3.419144249e-01f, 3.467040564e-01f,
    3.515325995e-01f, 3.564001441e-01f, 3.613067798e-01f, 3.662525956e-01f, 3.712376805e-01f,
    3.762621230e-01f, 3.813260114e-01f, 3.864294338e-01f, 3.915724777e-01f, 3.967552307e-01f,
    4.019777798e-01f, 4.072402119e-01f, 4.125426135e-01f, 4.178850708e-01f, 4.232676700e-01f,
    4.286904966e-01f, 4.341536362e-01f, 4.396571738e-01f, 4.452011945e-01f, 4.507857828e-01f,
    4.564110232e-01f, 4.620769997e-01f, 4.677837961e-01f, 4.735314961e-01f, 4.793201831e-01f,
    4.851499401e-01f, 4.910208498e-01f, 4.969329951e-01f, 5.028864580e-01f, 5.088813209e-01f,
    5.149176654e-01f, 5.209955732e-01f, 5.271151257e-01f, 5.332764040e-01f, 5.394794890e-01f,
    5.457244614e-01f, 5.520114015e-01f, 5.583403896e-01f, 5.647115057e-01f, 5.711248295e-01f,
    5.775804404e-01f, 5.840784179e-01f, 5.906188409e-01f, 5.972017884e-01f, 6.038273389e-01f,
    6.104955708e-01f, 6.172065624e-01f, 6.239603917e-01f, 6.307571363e-01f, 6.375968740e-01f,
    6.444796820e-01f, 6.514056374e-01f, 6.583748173e-01f, 6.653872983e-01f, 6.724431570e-01f,
    6.795424696e-01f, 6.866853124e-01f, 6.938717613e-01f, 7.011018919e-01f, 7.083757799e-01f,
    7.156935005e-01f, 7.230551289e-01f, 7.304607401e-01f, 7.379104088e-01f, 7.454042095e-01f,
    7.529422168e-01f, 7.605245047e-01f, 7.681511472e-01f, 7.758222183e-01f, 7.835377915e-01f,
    7.912979403e-01f, 7.991027380e-01f, 8.069522577e-01f, 8.148465722e-01f, 8.227857544e-01f,
    8.307698768e-01f, 8.387990117e-01f, 8.468732315e-01f, 8.549926081e-01f, 8.631572135e-01f,
    8.713671192e-01f, 8.796223969e-01f, 8.879231179e-01f, 8.962693534e-01f, 9.046611744e-01f,
    9.130986518e-01f, 9.215818563e-01f, 9.301108584e-01f, 9.386857285e-01f, 9.473065367e-01f,
    9.559733532e-01f, 9.646862479e-01f, 9.734452904e-01f, 9.822505503e-01f, 9.911020971e-01f,
    1.000000000e+00f,
};
constexpr float SRGB_THRESHOLD[255] = {
    1.517634918e-04f, 4.552904753e-04f, 7.588174589e-04f, 1.062344442e-03f, 1.365871426e-03f,
    1.669398410e-03f, 1.972925393e-03f, 2.276452377e-03f, 2.579979360e-03f, 2.883506344e-03f,
    3.188300904e-03f, 3.509259350e-03f, 3.848314933e-03f, 4.205748030e-03f, 4.581832741e-03f,
    4.976837250e-03f, 5.391024160e-03f, 5.824650784e-03f, 6.277969427e-03f, 6.751227633e-03f,
    7.244668422e-03f, 7.758530499e-03f, 8.293048455e-03f, 8.848452952e-03f, 9.424970891e-03f,
    1.002282557e-02f, 1.064223685e-02f, 1.128342126e-02f, 1.194659215e-02f, 1.263195981e-02f,
    1.333973160e-02f, 1.407011200e-02f, 1.482330280e-02f, 1.559950311e-02f, 1.639890952e-02f,
    1.722171611e-02f, 1.806811463e-02f, 1.893829446e-02f, 1.983244280e-02f, 2.075074465e-02f,
    2.169338291e-02f, 2.266053845e-02f, 2.365239016e-02f, 2.466911500e-02f, 2.571088806e-02f,
    2.677788263e-02f, 2.787027021e-02f, 2.898822059e-02f, 3.013190190e-02f, 3.130148060e-02f,
    3.249712161e-02f, 3.371898824e-02f, 3.496724235e-02f, 3.624204428e-02f, 3.754355296e-02f,
    3.887192588e-02f, 4.022731918e-02f, 4.160988767e-02f, 4.301978482e-02f, 4.445716284e-02f,
    4.592217266e-02f, 4.741496402e-02f, 4.893568542e-02f, 5.048448422e-02f, 5.206150661e-02f,
    5.366689765e-02f, 5.530080130e-02f, 5.696336045e-02f, 5.865471691e-02f, 6.037501146e-02f,
    6.212438386e-02f, 6.390297287e-02f, 6.571091626e-02f, 6.754835085e-02f, 6.941541251e-02f,
    7.131223618e-02f, 7.323895588e-02f, 7.519570475e-02f, 7.718261504e-02f, 7.919981813e-02f,
    8.124744458e-02f, 8.332562409e-02f, 8.543448553e-02f, 8.757415699e-02f, 8.974476575e-02f,
    9.194643832e-02f, 9.417930042e-02f, 9.644347704e-02f, 9.873909241e-02f, 1.010662700e-01f,
    1.034251327e-01f, 1.058158025e-01f, 1.082384007e-01f, 1.106930482e-01f, 1.131798648e-01f,
    1.156989699e-01f, 1.182504822e-01f, 1.208345198e-01f, 1.234512000e-01f, 1.261006396e-01f,
    1.287829547e-01f, 1.314982609e-01f, 1.342466731e-01f, 1.370283056e-01f, 1.398432722e-01f,
    1.426916860e-01f, 1.455736597e-01f, 1.484893052e-01f, 1.514387341e-01f, 1.544220573e-01f,
    1.574393851e-01f, 1.604908274e-01f, 1.635764935e-01f, 1.666964922e-01f, 1.698509319e-01f,
    1.730399202e-01f, 1.762635645e-01f, 1.795219715e-01f, 1.828152475e-01f, 1.861434984e-01f,
    1.895068294e-01f, 1.929053454e-01f, 1.963391508e-01f, 1.998083496e-01f, 2.033130451e-01f,
    2.068533405e-01f, 2.104293382e-01f, 2.140411405e-01f, 2.176888490e-01f, 2.213725650e-01f,
    2.250923893e-01f, 2.288484224e-01f, 2.326407643e-01f, 2.364695145e-01f, 2.403347723e-01f,
    2.442366365e-01f, 2.481752054e-01f, 2.521505770e-01f, 2.561628489e-01f, 2.602121184e-01f,
    2.642984823e-01f, 2.684220370e-01f, 2.725828787e-01f, 2.767811030e-01f, 2.810168053e-01f,
    2.852900806e-01f, 2.896010235e-01f, 2.939497283e-01f, 2.983362889e-01f, 3.027607989e-01f,
    3.072233515e-01f, 3.117240396e-01f, 3.162629557e-01f, 3.208401921e-01f, 3.254558406e-01f,
    3.301099928e-01f, 3.348027400e-01f, 3.395341729e-01f, 3.443043823e-01f, 3.491134583e-01f,
    3.539614910e-01f, 3.588485700e-01f, 3.637747846e-01f, 3.687402239e-01f, 3.737449765e-01f,
    3.787891310e-01f, 3.838727754e-01f, 3.889959976e-01f, 3.941588852e-01f, 3.993615253e-01f,
    4.046040051e-01f, 4.098864111e-01f, 4.152088298e-01f, 4.205713473e-01f, 4.259740495e-01f,
    4.314170219e-01f, 4.369003499e-01f, 4.424241185e-01f, 4.479884124e-01f, 4.535933162e-01f,
    4.592389142e-01f, 4.649252902e-01f, 4.706525280e-01f, 4.764207111e-01f, 4.822299226e-01f,
    4.880802457e-01f, 4.939717629e-01f, 4.999045567e-01f, 5.058787093e-01f, 5.118943028e-01f,
    5.179514188e-01f, 5.240501388e-01f, 5.301905441e-01f, 5.363727156e-01f, 5.425967342e-01f,
    5.488626805e-01f, 5.551706346e-01f, 5.615206767e-01f, 5.679128866e-01f, 5.743473441e-01f,
    5.808241284e-01f, 5.873433188e-01f, 5.939049942e-01f, 6.005092333e-01f, 6.071561148e-01f,
    6.138457168e-01f, 6.205781175e-01f, 6.273533947e-01f, 6.341716262e-01f, 6.410328894e-01f,
    6.479372614e-01f, 6.548848195e-01f, 6.618756404e-01f, 6.689098006e-01f, 6.759873768e-01f,
    6.831084450e-01f, 6.902730814e-01f, 6.974813617e-01f, 7.047333615e-01f, 7.120291564e-01f,
    7.193688216e-01f, 7.267524320e-01f, 7.341800626e-01f, 7.416517880e-01f, 7.491676827e-01f,
    7.567278210e-01f, 7.643322770e-01f, 7.719811246e-01f, 7.796744376e-01f, 7.874122894e-01f,
    7.951947535e-01f, 8.030219030e-01f, 8.108938110e-01f, 8.188105503e-01f, 8.267721935e-01f,
    8.347788132e-01f, 8.428304815e-01f, 8.509272707e-01f, 8.590692527e-01f, 8.672564993e-01f,
    8.754890821e-01f, 8.837670725e-01f, 8.920905419e-01f, 9.004595613e-01f, 9.088742016e-01f,
    9.173345337e-01f, 9.258406282e-01f, 9.343925555e-01f, 9.429903859e-01f, 9.516341896e-01f,
    9.603240364e-01f, 9.690599963e-01f, 9.778421388e-01f, 9.866705335e-01f, 9.955452497e-01f,
};

const float* srgb_lut() { return SRGB_LUT; }

uint8_t linear_to_srgb(float l) {
    l = std::clamp(l, 0.0f, 1.0f);
    // The count of thresholds at or below l: the code it rounds to.
    const float* end = std::upper_bound(SRGB_THRESHOLD, SRGB_THRESHOLD + 255, l);
    return uint8_t(end - SRGB_THRESHOLD);
}

// One mip step: each texel the mean of its 2x2 source (an odd edge drops its
// last row or column; a 1-texel edge stays 1).
ImageData halve(const ImageData& src, bool srgb) {
    ImageData dst;
    dst.width = std::max(1, src.width / 2);
    dst.height = std::max(1, src.height / 2);
    dst.rgba.resize(size_t(dst.width) * dst.height * 4);
    const float* lut = srgb_lut();
    const int sx = src.width > 1 ? 2 : 1, sy = src.height > 1 ? 2 : 1;
    const float inv = 1.0f / float(sx * sy);
    for (int y = 0; y < dst.height; ++y) {
        for (int x = 0; x < dst.width; ++x) {
            float sum[4] = {0, 0, 0, 0};
            for (int j = 0; j < sy; ++j) {
                const uint8_t* row = &src.rgba[(size_t(y * sy + j) * src.width + size_t(x * sx)) * 4];
                for (int i = 0; i < sx; ++i) {
                    for (int c = 0; c < 4; ++c) {
                        const uint8_t v = row[i * 4 + c];
                        sum[c] += (srgb && c < 3) ? lut[v] : float(v) / 255.0f;
                    }
                }
            }
            uint8_t* out = &dst.rgba[(size_t(y) * dst.width + x) * 4];
            for (int c = 0; c < 4; ++c) {
                const float mean = sum[c] * inv;
                out[c] = (srgb && c < 3) ? linear_to_srgb(mean) : uint8_t(std::lround(mean * 255.0f));
            }
        }
    }
    return dst;
}

}  // namespace

TextureBudget active_texture_budget() {
    TextureBudget budget;
    budget.max_size = active_tier().budget.max_texture_size;
    budget.compress = active_tier().compress_textures;
    return budget;
}

ImageData cap_image(const ImageData& image, uint32_t max_size, bool srgb) {
    ImageData out = image;
    while (max_size > 0 && uint32_t(std::max(out.width, out.height)) > max_size) out = halve(out, srgb);
    return out;
}

std::vector<ImageData> build_mips(const ImageData& image, bool srgb, bool block_aligned) {
    std::vector<ImageData> chain{image};
    while (chain.back().width > 1 || chain.back().height > 1) {
        ImageData next = halve(chain.back(), srgb);
        if (block_aligned && (next.width % 4 != 0 || next.height % 4 != 0)) break;
        chain.push_back(std::move(next));
    }
    return chain;
}

bool has_alpha(const ImageData& image) {
    for (size_t i = 3; i < image.rgba.size(); i += 4) {
        if (image.rgba[i] != 255) return true;
    }
    return false;
}

void swizzle_normal_map(ImageData& image) {
    for (size_t i = 0; i < image.rgba.size(); i += 4) {
        const uint8_t x = image.rgba[i];
        image.rgba[i] = 0;
        image.rgba[i + 2] = 0;
        image.rgba[i + 3] = x;
    }
}

std::vector<uint8_t> encode_bc(const ImageData& image, bool bc3) {
    const int bw = image.width / 4, bh = image.height / 4;
    const size_t block_bytes = bc3 ? 16 : 8;
    std::vector<uint8_t> out(size_t(bw) * bh * block_bytes);
    uint8_t block[64];
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            for (int y = 0; y < 4; ++y) {
                std::memcpy(&block[y * 16], &image.rgba[(size_t(by * 4 + y) * image.width + size_t(bx * 4)) * 4], 16);
            }
            stb_compress_dxt_block(&out[(size_t(by) * bw + bx) * block_bytes], block, bc3 ? 1 : 0,
                                   STB_DXT_HIGHQUAL);
        }
    }
    return out;
}

ImageData decode_image(const uint8_t* bytes, size_t size) {
    ImageData image;
    int channels = 0;
    // Forced to 4 channels: the GPU formats we care about are RGBA, and letting
    // stb decide would mean handling three layouts downstream.
    unsigned char* pixels = stbi_load_from_memory(bytes, int(size), &image.width, &image.height,
                                                  &channels, 4);
    if (!pixels) {
        LOG_ERROR("image decode failed: %s", stbi_failure_reason());
        return {};
    }
    image.rgba.assign(pixels, pixels + size_t(image.width) * size_t(image.height) * 4);
    stbi_image_free(pixels);
    return image;
}

bool PreparedTexture::compressed() const {
    return format == rhi::Format::BC1 || format == rhi::Format::BC1_SRGB || format == rhi::Format::BC3 ||
           format == rhi::Format::BC3_SRGB;
}

void PreparedTexture::trim(uint32_t max_size) {
    if (max_size == 0) return;
    while (levels.size() > 1 && (width > max_size || height > max_size)) {
        levels.erase(levels.begin());
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
}

PreparedTexture prepare_texture(const ImageData& image, TextureKind kind, const TextureBudget& budget,
                                bool compress_allowed) {
    PreparedTexture out;
    if (!image.valid()) return out;
    const bool srgb = kind == TextureKind::Colour;
    out.source_width = image.width;
    out.source_height = image.height;
    ImageData top = cap_image(image, budget.max_size, srgb);
    if (budget.compress && kind == TextureKind::NormalMap) swizzle_normal_map(top);

    // What each kind compresses to, if anything (see TextureBudget).
    rhi::Format block = rhi::Format::Invalid;
    if (budget.compress && compress_allowed && top.width % 4 == 0 && top.height % 4 == 0) {
        switch (kind) {
            case TextureKind::Colour: block = has_alpha(top) ? rhi::Format::BC3_SRGB : rhi::Format::BC1_SRGB; break;
            case TextureKind::NormalMap: block = rhi::Format::BC3; break;
            case TextureKind::Orm: block = rhi::Format::BC1; break;
            case TextureKind::Data: break;
        }
    }
    const bool compress = block != rhi::Format::Invalid;
    const bool bc3 = block == rhi::Format::BC3 || block == rhi::Format::BC3_SRGB;
    out.format = compress ? block : srgb ? rhi::Format::RGBA8_SRGB : rhi::Format::RGBA8;
    out.width = uint32_t(top.width);
    out.height = uint32_t(top.height);
    for (ImageData& m : build_mips(top, srgb, compress)) {
        out.levels.push_back(compress ? encode_bc(m, bc3) : std::move(m.rgba));
    }
    return out;
}

rhi::Texture* upload_prepared(rhi::Device& rhi, const PreparedTexture& prepared, const char* debug_name) {
    if (!prepared.valid()) return nullptr;
    rhi::TextureDesc desc;
    desc.width = prepared.width;
    desc.height = prepared.height;
    desc.mip_levels = uint32_t(prepared.levels.size());
    desc.format = prepared.format;
    // Fixed function (gfx/render_tier.h) has no sRGB sampling: a colour map
    // is read as the 8-bit values it stores and lit in that space, so the
    // preview samples it UNORM too. The bytes are the same either way.
    if (active_tier().fixed_function) {
        if (desc.format == rhi::Format::RGBA8_SRGB) desc.format = rhi::Format::RGBA8;
        if (desc.format == rhi::Format::BC1_SRGB) desc.format = rhi::Format::BC1;
        if (desc.format == rhi::Format::BC3_SRGB) desc.format = rhi::Format::BC3;
    }
    desc.usage = rhi::TEXTURE_SAMPLED;
    rhi::Texture* texture = rhi.create_texture(desc, debug_name);
    if (!texture) return nullptr;
    size_t bytes = 0;
    uint32_t w = prepared.width, h = prepared.height;
    for (size_t level = 0; level < prepared.levels.size(); ++level) {
        const std::vector<uint8_t>& data = prepared.levels[level];
        if (!rhi.upload_texture_level(texture, uint32_t(level), data.data(), uint32_t(data.size()), w, h)) {
            rhi.destroy(texture);
            return nullptr;
        }
        bytes += data.size();
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    const bool bc3 = prepared.format == rhi::Format::BC3 || prepared.format == rhi::Format::BC3_SRGB;
    LOG_INFO("texture '%s': %dx%d -> %ux%u %s, %zu mips, %.1f MB", debug_name, prepared.source_width,
             prepared.source_height, prepared.width, prepared.height,
             prepared.compressed() ? (bc3 ? "BC3" : "BC1") : "RGBA8", prepared.levels.size(),
             double(bytes) / (1024.0 * 1024.0));
    return texture;
}

namespace {

// The budgeted path: capped, mips on the CPU, maybe compressed.
rhi::Texture* create_budgeted_texture(rhi::Device& rhi, const ImageData& image, const char* debug_name,
                                      TextureKind kind, const TextureBudget& budget) {
    // Whether the device takes the block format this kind would get.
    const rhi::Format probe = kind == TextureKind::Colour ? rhi::Format::BC1_SRGB : rhi::Format::BC1;
    const bool bc = rhi.supports_format(probe, rhi::TEXTURE_SAMPLED) &&
                    rhi.supports_format(kind == TextureKind::Colour ? rhi::Format::BC3_SRGB : rhi::Format::BC3,
                                        rhi::TEXTURE_SAMPLED);
    if (budget.compress && !bc && kind != TextureKind::Data) {
        LOG_WARN("texture '%s': this device has no BC formats; kept uncompressed", debug_name);
    }
    return upload_prepared(rhi, prepare_texture(image, kind, budget, bc), debug_name);
}

}  // namespace

rhi::Texture* create_texture_from_image(rhi::Device& rhi, const ImageData& image,
                                        const char* debug_name, TextureKind kind) {
    if (!image.valid()) return nullptr;
    const TextureBudget budget = active_texture_budget();
    if (budget.active()) return create_budgeted_texture(rhi, image, debug_name, kind, budget);
    const bool srgb = kind == TextureKind::Colour;

    uint32_t levels = 1;
    for (uint32_t dimension = uint32_t(image.width > image.height ? image.width : image.height);
         dimension > 1; dimension /= 2) {
        ++levels;
    }

    rhi::TextureDesc desc;
    desc.width = uint32_t(image.width);
    desc.height = uint32_t(image.height);
    desc.mip_levels = levels;
    desc.format = srgb ? rhi::Format::RGBA8_SRGB : rhi::Format::RGBA8;
    // A colour target as well as sampled: SDL generates mips by rendering
    // into the smaller levels, so the texture has to be usable as one.
    desc.usage = rhi::TEXTURE_SAMPLED | rhi::TEXTURE_COLOR_TARGET;
    rhi::Texture* texture = rhi.create_texture(desc, debug_name);
    if (!texture) return nullptr;
    if (!rhi.upload_texture(texture, image.rgba.data(), desc.width, desc.height, levels > 1)) {
        rhi.destroy(texture);
        return nullptr;
    }
    LOG_INFO("texture '%s': %dx%d, %u mips", debug_name, image.width, image.height, levels);
    return texture;
}

rhi::Sampler* create_model_sampler(rhi::Device& rhi) {
    rhi::SamplerDesc desc;
    desc.min_filter = rhi::Filter::Linear;
    desc.mag_filter = rhi::Filter::Linear;
    desc.mip_mode = rhi::MipMode::Linear;
    desc.address_u = desc.address_v = desc.address_w = rhi::Address::Repeat;
    // Grazing angles are the norm on a wing membrane, where anisotropy earns its
    // keep.
    desc.max_anisotropy = 8.0f;
    desc.max_lod = 1000.0f;
    return rhi.create_sampler(desc);
}

}  // namespace gfx
