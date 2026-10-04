#pragma once

// The fixed-function tier's lighting, on the CPU (docs/PORTING.md R5).
//
// In the FIXED_FUNCTION shader variant the vertex stage lights each vertex
// and finishes it into the diffuse colour the stages modulate
// (scene_common.hlsl, ff_lit). For geometry that never moves under a sun
// that never moves -- the terrain -- that is the same number every frame,
// and on a card without hardware T&L every frame recomputes it through
// D3D9's software vertex pipeline: 55K vertices through a 158-slot program.
// So the terrain's diffuse is computed here once, at build, into the vertex
// colour, and the terrain's fixed-function vertex program only transforms
// and fogs. These functions are the HLSL ones line for line, so the bake is
// the shader's own value (tests/test_prelit.cpp holds them to each other
// through the shader's reference points).

#include "core/math.h"
#include "gfx/scene_uniforms.h"

namespace gfx::prelit {

// scene_common.hlsl: direct_sun(normal, 1), ambient_light, translucent_sun.
core::Vec3 direct_sun(const SceneUniforms& scene, core::Vec3 normal);
core::Vec3 ambient_light(const SceneUniforms& scene, core::Vec3 normal);
core::Vec3 translucent_sun(const SceneUniforms& scene, core::Vec3 normal, float translucency);
// common.hlsl: ldr_encode_fast.
core::Vec3 ldr_encode_fast(core::Vec3 c, core::Vec4 grade);
// scene_common.hlsl: ff_albedo, ff_lit (the rgb).
core::Vec3 ff_albedo(core::Vec3 linear_color);
core::Vec3 ff_lit(const SceneUniforms& scene, core::Vec3 albedo_linear, core::Vec3 normal, float translucency);

// sky.hlsl's FIXED_FUNCTION vertex stage: the analytic sky along `ray`,
// display-encoded (scene_common.hlsl, sky_color then ff_display).
core::Vec3 sky_diffuse(const SceneUniforms& scene, core::Vec3 ray);

// terrain.hlsl's FIXED_FUNCTION vertex stage: the material blend at a
// vertex (`patches` is fbm(xz * 0.0055), the vertex's uv.x) times its tint,
// lit and finished.
core::Vec3 terrain_diffuse(const SceneUniforms& scene, core::Vec3 position, core::Vec3 normal, float patches,
                           core::Vec3 tint);

}  // namespace gfx::prelit
