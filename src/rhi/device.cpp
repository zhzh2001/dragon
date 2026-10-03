#include "rhi/rhi.h"

#include "rhi/sdlgpu/sdlgpu.h"
#ifdef _WIN32
#include "rhi/d3d9/d3d9_backend.h"
#endif

namespace rhi {

std::unique_ptr<Device> Device::create(SDL_Window* window, const DeviceConfig& config) {
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
#endif
    return sdlgpu::create(window, config);
}

}  // namespace rhi
