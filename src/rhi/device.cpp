#include "rhi/rhi.h"

#include "rhi/sdlgpu/sdlgpu.h"
#ifdef _WIN32
#include "rhi/d3d9/d3d9_backend.h"
#endif

namespace rhi {

std::unique_ptr<Device> Device::create(SDL_Window* window, const DeviceConfig& config) {
#ifdef _WIN32
    if (config.driver == "direct3d9") return d3d9::create(window, config);
#endif
    return sdlgpu::create(window, config);
}

}  // namespace rhi
