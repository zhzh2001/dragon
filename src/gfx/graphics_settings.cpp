#include "gfx/graphics_settings.h"

#include <cstdlib>
#include <sstream>

#include "gfx/render_tier.h"

namespace gfx {
namespace {

// The levels, Ultra to Very low. The values are what the X550 benchmark
// (docs/PORTING.md, R7) was run against; each step down roughly halves a
// setting's vertex load.
const SettingInfo INFO[SETTING_COUNT] = {
    {"models", "Model detail", {"Full", "20K triangles", "10K triangles", "5K triangles", "2.5K triangles"},
     Applies::Restart},
    // Four levels: a 36 m grid lost the river, which is how a valley is
    // navigated; Very low keeps the 24 m one.
    {"terrain", "Terrain detail", {"6 m grid", "12 m grid", "18 m grid", "24 m grid", nullptr}, Applies::Reload},
    {"trees", "Trees", {"Very high", "High", "Medium", "Low", "Very low"}, Applies::Reload},
    {"grass", "Grass", {"Very high", "High", "Medium", "Low", "Off"}, Applies::Live},
    {"rocks", "Rocks", {"Scanned", "400 triangles", "150 triangles", "80 triangles", "40 triangles"},
     Applies::Reload},
    {"textures", "Textures", {"Full", "1024", "512", "256", "128"}, Applies::Restart},
    {"shadows", "Shadows", {"4096", "2048", "1024", "512", "Off"}, Applies::Reload},
    {"bloom", "Bloom", {"On", nullptr, nullptr, nullptr, "Off"}, Applies::Live},
    // The world's resolution, the HUD's staying whole: on a fill-bound card
    // the setting that moves the frame rate most (docs/PORTING.md, R7).
    {"resolution", "Resolution", {"100%", nullptr, "85%", "70%", "50%"}, Applies::Live},
};

constexpr uint32_t MODEL_TRIANGLES[QUALITY_LEVELS] = {0, 20000, 10000, 5000, 2500};
constexpr uint32_t FAR_TRIANGLES[QUALITY_LEVELS] = {0, 3000, 1500, 800, 400};
constexpr float FAR_DISTANCE[QUALITY_LEVELS] = {0.0f, 150.0f, 100.0f, 80.0f, 60.0f};
constexpr float TERRAIN_SCALE[QUALITY_LEVELS] = {1.0f, 2.0f, 3.0f, 4.0f, 4.0f};
constexpr float TREE_DISTANCE[QUALITY_LEVELS] = {0.0f, 2500.0f, 800.0f, 600.0f, 400.0f};
constexpr float TREE_SPACING[QUALITY_LEVELS] = {1.0f, 1.0f, 1.8f, 2.2f, 2.8f};
constexpr float GRASS_RADIUS[QUALITY_LEVELS] = {1.0f, 0.7f, 0.45f, 0.3f, 0.0f};
constexpr uint32_t ROCK_TRIANGLES[QUALITY_LEVELS] = {0, 400, 150, 80, 40};
constexpr float ROCK_DISTANCE[QUALITY_LEVELS] = {1.0f, 1.0f, 0.5f, 0.4f, 0.3f};
constexpr uint32_t TEXTURE_SIZE[QUALITY_LEVELS] = {0, 1024, 512, 256, 128};
constexpr uint32_t SHADOW_SIZE[QUALITY_LEVELS] = {4096, 2048, 1024, 512, 0};
constexpr float WORLD_SCALE[QUALITY_LEVELS] = {1.0f, 1.0f, 0.85f, 0.7f, 0.5f};

const char* const PRESET_NAMES[QUALITY_LEVELS] = {"ultra", "high", "medium", "low", "very-low"};

// The cheapest level a tier is offered, per setting. Above it the tier's
// cards run out of memory or vertex constants long before the budget
// matters: full 4096^2 maps uncompressed, or 4096^2 of R32F plus depth, on a
// 128 MB card.
int tier_floor(Setting setting, Tier tier) {
    const int t = tier == Tier::Modern ? 0 : tier == Tier::SM3 ? 1 : tier == Tier::SM2 ? 2 : 3;
    switch (setting) {
        case Setting::Models: return t >= 2 ? t - 1 : 0;   // sm2 20K, ff 10K at most
        case Setting::Textures: return t >= 1 ? (t >= 3 ? 2 : 1) : 0;
        case Setting::Shadows: return t;                   // sm3 2048, sm2 1024, ff 512
        default: return 0;
    }
}

// A preset level's level for each setting: the same, except bloom, which is
// on through Medium and off below; shadows, off from Low (the 512 map is
// offered, not preset: on the X550 the lookup was a third of every pixel);
// the terrain, whose Very low is Low; and the resolution, whole through
// Medium, 85% at Low and 70% at Very low (50% is offered, not preset).
int preset_to_level(Setting setting, int preset) {
    if (setting == Setting::Bloom) return preset <= 2 ? 0 : QUALITY_LEVELS - 1;
    if (setting == Setting::Shadows) return preset <= 2 ? preset : QUALITY_LEVELS - 1;
    if (setting == Setting::Resolution) return preset <= 2 ? 0 : preset - 1;
    if (setting == Setting::Terrain) return preset < 3 ? preset : 3;
    return preset;
}

}  // namespace

const SettingInfo& setting_info(Setting setting) { return INFO[int(setting)]; }

const char* preset_name(int level) {
    return level >= 0 && level < QUALITY_LEVELS ? PRESET_NAMES[level] : "custom";
}

bool parse_preset(const std::string& name, int* level) {
    for (int i = 0; i < QUALITY_LEVELS; ++i) {
        if (name == PRESET_NAMES[i]) {
            *level = i;
            return true;
        }
    }
    return false;
}

bool allowed(Setting setting, int level, Tier tier, const DeviceLimits& device) {
    if (level < 0 || level >= QUALITY_LEVELS || !INFO[int(setting)].options[size_t(level)]) return false;
    if (level < tier_floor(setting, tier)) return false;
    switch (setting) {
        case Setting::Textures:
            return TEXTURE_SIZE[level] ? TEXTURE_SIZE[level] <= device.max_texture_size
                                       : device.max_texture_size >= 4096;
        case Setting::Shadows:
            return SHADOW_SIZE[level] <= device.max_texture_size;
        case Setting::Bloom:
            // No float target, no bloom: the LDR tiers grade in each shader.
            // (RenderTier::hdr, spelled out: make() asks for a preset.)
            return level != 0 || tier == Tier::Modern || tier == Tier::SM3;
        default:
            return true;
    }
}

int clamp_level(Setting setting, int level, Tier tier, const DeviceLimits& device) {
    for (int d = 0; d < QUALITY_LEVELS; ++d) {
        if (allowed(setting, level + d, tier, device)) return level + d;
        if (allowed(setting, level - d, tier, device)) return level - d;
    }
    return QUALITY_LEVELS - 1;
}

int default_preset(Tier tier) {
    switch (tier) {
        case Tier::Modern: return 0;
        case Tier::SM3: return 1;
        case Tier::SM2: return 2;
        case Tier::FixedFunction: return 3;
    }
    return 0;
}

GraphicsSettings GraphicsSettings::preset(int level, Tier tier, const DeviceLimits& device) {
    GraphicsSettings g;
    for (int s = 0; s < SETTING_COUNT; ++s) {
        const Setting setting = Setting(s);
        g.level[size_t(s)] = uint8_t(clamp_level(setting, preset_to_level(setting, level), tier, device));
    }
    return g;
}

int GraphicsSettings::preset_level(Tier tier, const DeviceLimits& device) const {
    // Compared after clamping: on SM2, Ultra is Ultra with the textures and
    // shadows the tier allows, not a custom mix.
    for (int p = 0; p < QUALITY_LEVELS; ++p) {
        if (*this == preset(p, tier, device)) return p;
    }
    return -1;
}

std::string GraphicsSettings::serialize() const {
    std::ostringstream out;
    for (int s = 0; s < SETTING_COUNT; ++s) out << INFO[s].key << '=' << int(level[size_t(s)]) << '\n';
    return out.str();
}

void GraphicsSettings::parse(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const int value = std::atoi(line.c_str() + eq + 1);
        for (int s = 0; s < SETTING_COUNT; ++s) {
            if (key == INFO[s].key && value >= 0 && value < QUALITY_LEVELS) level[size_t(s)] = uint8_t(value);
        }
    }
}

ContentBudget budget_for(const GraphicsSettings& g) {
    ContentBudget b;
    const int models = g[Setting::Models];
    b.max_skinned_triangles = MODEL_TRIANGLES[models];
    b.far_skinned_triangles = FAR_TRIANGLES[models];
    b.far_skinned_distance = FAR_DISTANCE[models];
    b.terrain_cell_scale = TERRAIN_SCALE[g[Setting::Terrain]];
    b.tree_draw_distance = TREE_DISTANCE[g[Setting::Trees]];
    b.tree_spacing_scale = TREE_SPACING[g[Setting::Trees]];
    b.grass_radius_scale = GRASS_RADIUS[g[Setting::Grass]];
    b.max_rock_triangles = ROCK_TRIANGLES[g[Setting::Rocks]];
    b.rock_distance_scale = ROCK_DISTANCE[g[Setting::Rocks]];
    b.max_texture_size = TEXTURE_SIZE[g[Setting::Textures]];
    b.shadow_size = SHADOW_SIZE[g[Setting::Shadows]];
    // The terrain is the largest caster; a map at 1024 or below covers too
    // little of it to be worth its triangles.
    b.terrain_casts_shadows = b.shadow_size >= 2048;
    b.bloom = g[Setting::Bloom] == 0;
    b.world_scale = WORLD_SCALE[g[Setting::Resolution]];
    return b;
}

}  // namespace gfx
