#pragma once

// The GLES 2.0 backend (docs/porting/MOBILE.md, M4): the retro tiers on an
// OpenGL ES 2.0 GPU -- the Raspberry Pi 1B's VideoCore IV through Mesa's
// vc4, and ANGLE on the Mac, where it is developed. `--gpu-driver gles`.
//
// Shaders are GLSL ES 1.00 baked by tools/gles/bake_gles.py, compiled by the
// driver at load. Every pass renders upside down in GL's terms, which is the
// D3D layout in memory, so textures, viewports and readbacks are what the
// engine expects everywhere and only the present flips.

#include <memory>

#include "rhi/rhi.h"

namespace rhi {
namespace gles {

// Makes a GLES 2.0 context on `window`, which gfx::Device created with
// SDL_WINDOW_OPENGL and the ES attributes (prepare_window).
std::unique_ptr<Device> create(SDL_Window* window, const DeviceConfig& config);

// Before SDL_Init and the window: on the Mac, point SDL's EGL at ANGLE.
void prepare_video();
// After SDL_Init, before the window: ask for an ES 2.0 context.
void prepare_window();

// Dear ImGui's GL renderer changes state behind the backend's cache: after
// it draws, the cache must be forgotten (editor/imgui_layer.cpp).
void forget_state(Device* device);

}  // namespace gles
}  // namespace rhi
