#pragma once

// Graphics settings and presets (docs/PORTING.md, R7).
//
// The render tier (gfx/render_tier.h) is what the hardware can do: HDR or
// not, per-pixel or per-vertex lighting, shader constants. These are how much
// it is asked to draw: model detail, terrain grid, plants, rocks, textures,
// shadows. Each setting is a level, 0 (Ultra) to 4 (Very low), into its own
// table, and a preset sets every setting to the same level. A setting is a
// content choice, not a shader choice, so one preset renders the same on
// every backend that runs its tier -- within the compilers' tolerance
// (tools/golden/golden.py --cross-compiler).
//
// A level a tier or device cannot do is not offered (`allowed`): bloom
// without HDR, a texture past the card's largest, a shadow map an SM2 card's
// memory does not hold. Presets clamp to the nearest allowed level.

#include <array>
#include <cstdint>
#include <string>

namespace gfx {

enum class Tier : uint8_t;

enum class Setting : uint8_t { Models, Terrain, Trees, Grass, Rocks, Textures, Shadows, Bloom, Resolution, Count };
constexpr int SETTING_COUNT = int(Setting::Count);
constexpr int QUALITY_LEVELS = 5;

// What a setting needs to change: drawn next frame, re-planted or re-meshed
// at once, or loaded again at start-up.
enum class Applies : uint8_t { Live, Reload, Restart };

struct SettingInfo {
    const char* key;     // in the settings file and on the command line
    const char* label;   // in the Graphics panel
    std::array<const char*, QUALITY_LEVELS> options;
    Applies applies;
};
const SettingInfo& setting_info(Setting setting);

const char* preset_name(int level);  // "ultra", "high", "medium", "low", "very-low"
bool parse_preset(const std::string& name, int* level);

// What the device offers beyond its tier.
struct DeviceLimits {
    uint32_t max_texture_size = 16384;
};

struct GraphicsSettings {
    std::array<uint8_t, SETTING_COUNT> level{};

    uint8_t& operator[](Setting s) { return level[size_t(s)]; }
    uint8_t operator[](Setting s) const { return level[size_t(s)]; }
    bool operator==(const GraphicsSettings& o) const { return level == o.level; }

    // Every setting at `preset`'s level, clamped to what is allowed.
    static GraphicsSettings preset(int level, Tier tier, const DeviceLimits& device);
    // The preset these settings are on this tier and device, or -1 for a
    // custom mix.
    int preset_level(Tier tier, const DeviceLimits& device) const;

    // "models=2 terrain=2 ..." and back; unknown keys are ignored.
    std::string serialize() const;
    void parse(const std::string& text);
};

// Whether `level` of `setting` is offered on this tier and device.
bool allowed(Setting setting, int level, Tier tier, const DeviceLimits& device);
// The nearest allowed level to `level`, preferring the cheaper side.
int clamp_level(Setting setting, int level, Tier tier, const DeviceLimits& device);
// The preset a tier starts on when nothing is saved or asked for.
int default_preset(Tier tier);

// The concrete budget the levels mean, which the loaders and renderers read
// (through RenderTier, gfx/render_tier.h).
struct ContentBudget {
    uint32_t max_skinned_triangles = 0;  // 0: the model's own
    uint32_t far_skinned_triangles = 0;  // 0: no distance LOD
    float far_skinned_distance = 0.0f;
    float terrain_cell_scale = 1.0f;
    float tree_draw_distance = 0.0f;     // 0: the setting's own
    float tree_spacing_scale = 1.0f;
    float grass_radius_scale = 1.0f;     // 0: no grass
    uint32_t max_rock_triangles = 0;     // 0: the scan's own
    float rock_distance_scale = 1.0f;
    uint32_t max_texture_size = 0;       // 0: the file's own
    uint32_t shadow_size = 4096;         // 0: no shadows
    bool terrain_casts_shadows = true;
    bool bloom = true;
    float world_scale = 1.0f;            // of the window, per axis (gfx::Device)
};
ContentBudget budget_for(const GraphicsSettings& settings);

}  // namespace gfx
