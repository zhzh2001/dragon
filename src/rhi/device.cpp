#include "rhi/rhi.h"

#ifndef DRAGON_XP
#include "rhi/sdlgpu/sdlgpu.h"
#endif
#ifdef DRAGON_GLES
#include "rhi/gles/gles_backend.h"
#endif
#ifdef _WIN32
#ifndef DRAGON_XP
#include "rhi/d3d11/d3d11_backend.h"
#endif
#include "rhi/d3d9/d3d9_backend.h"
#endif

namespace rhi {

std::unique_ptr<Device> Device::create(SDL_Window* window, const DeviceConfig& config) {
#ifdef DRAGON_GLES
    // "gles": OpenGL ES 2.0 (rhi/gles), the retro tiers on a GPU with no
    // Vulkan -- the Pi 1B's VideoCore IV, ANGLE on the Mac.
    if (config.driver == "gles") return gles::create(window, config);
#endif
#ifdef _WIN32
    // "direct3d9", or "direct3d9:N" for D3D9 adapter N -- one per display
    // output with a desktop, as tools/r0's spike lists them.
    if (config.driver.rfind("direct3d9", 0) == 0) {
        DeviceConfig d3d9_config = config;
        const size_t colon = config.driver.find(':');
        d3d9_config.adapter = colon == std::string::npos ? 0 : uint32_t(std::stoul(config.driver.substr(colon + 1)));
        d3d9_config.driver = "direct3d9";
        return d3d9::create(window, d3d9_config);
    }
#ifdef DRAGON_XP
    // The XP build has D3D9 alone (docs/PORTING.md, R6): any other name, or
    // none, is the D3D9 backend on the default adapter.
    DeviceConfig d3d9_config = config;
    d3d9_config.driver = "direct3d9";
    return d3d9::create(window, d3d9_config);
#else
    // "direct3d11", or "direct3d11:N" for DXGI adapter N (rhi/d3d11).
    if (config.driver.rfind("direct3d11", 0) == 0) {
        DeviceConfig d3d11_config = config;
        const size_t colon = config.driver.find(':');
        d3d11_config.adapter = colon == std::string::npos ? 0 : uint32_t(std::stoul(config.driver.substr(colon + 1)));
        d3d11_config.driver = "direct3d11";
        return d3d11::create(window, d3d11_config);
    }
#endif
#endif
#ifndef DRAGON_XP
    return sdlgpu::create(window, config);
#endif
}

uint64_t texture_bytes(const TextureDesc& desc) {
    // Bytes per 4x4 block for the compressed formats, per texel otherwise.
    uint32_t block = 0, texel = 4;
    switch (desc.format) {
        case Format::BC1:
        case Format::BC1_SRGB: block = 8; break;
        case Format::BC3:
        case Format::BC3_SRGB: block = 16; break;
        case Format::RGBA16F: texel = 8; break;
        case Format::RGB565:
        case Format::D16: texel = 2; break;
        default: break;
    }
    uint64_t total = 0;
    uint32_t w = desc.width, h = desc.height;
    for (uint32_t level = 0; level < (desc.mip_levels ? desc.mip_levels : 1); ++level) {
        total += block ? uint64_t((w + 3) / 4) * ((h + 3) / 4) * block : uint64_t(w) * h * texel;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    return total;
}

ResourceStats Device::stats() const {
    ResourceStats s;
    for (const auto& [handle, t] : live_) {
        switch (t.kind) {
            case Kind::Buffer: ++s.buffers; s.buffer_bytes += t.bytes; break;
            case Kind::Texture: ++s.textures; s.texture_bytes += t.bytes; break;
            case Kind::Pipeline: ++s.pipelines; break;
        }
    }
    s.driver_available = driver_available();
    return s;
}

void Device::track(const void* handle, Kind kind, uint64_t bytes) {
    if (handle) live_[handle] = Tracked{kind, bytes};
}

void Device::untrack(const void* handle) {
    if (handle) live_.erase(handle);
}

}  // namespace rhi
