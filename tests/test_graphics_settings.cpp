// Graphics settings and presets (gfx/graphics_settings.h): every preset is
// allowed where it is offered, each step down asks for less, a tier never
// offers what it cannot do, and the saved form reads back as written.

#include <cstdio>
#include <cstdlib>

#include "gfx/graphics_settings.h"
#include "gfx/render_tier.h"

static int g_checks = 0, g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(cond)) {                                                    \
            ++g_failures;                                                 \
            std::printf("  FAIL (line %d): %s\n", __LINE__, #cond);       \
        }                                                                 \
    } while (0)

using namespace gfx;

int main() {
    const Tier tiers[] = {Tier::Modern, Tier::SM3, Tier::SM2, Tier::FixedFunction};
    DeviceLimits big;           // a modern card
    DeviceLimits x550;          // an SM2 card
    x550.max_texture_size = 2048;

    for (Tier tier : tiers) {
        for (const DeviceLimits& device : {big, x550}) {
            ContentBudget previous;
            for (int p = 0; p < QUALITY_LEVELS; ++p) {
                const GraphicsSettings g = GraphicsSettings::preset(p, tier, device);
                // Every level a preset picks is one the tier offers, and the
                // preset is recognised as itself.
                for (int s = 0; s < SETTING_COUNT; ++s) CHECK(allowed(Setting(s), g.level[size_t(s)], tier, device));
                CHECK(g.preset_level(tier, device) == p);
                // Each step down asks for no more than the one above.
                const ContentBudget b = budget_for(g);
                if (p > 0) {
                    auto le = [](uint32_t a, uint32_t b) { return (a == 0 ? 1u << 30 : a) <= (b == 0 ? 1u << 30 : b); };
                    CHECK(le(b.max_skinned_triangles, previous.max_skinned_triangles));
                    CHECK(b.terrain_cell_scale >= previous.terrain_cell_scale);
                    CHECK(b.grass_radius_scale <= previous.grass_radius_scale);
                    CHECK(le(b.max_rock_triangles, previous.max_rock_triangles));
                    CHECK(le(b.max_texture_size, previous.max_texture_size));
                    CHECK(b.shadow_size <= previous.shadow_size);
                }
                previous = b;
            }
        }
    }

    // What a tier or device cannot do is not offered.
    CHECK(!allowed(Setting::Bloom, 0, Tier::SM2, big));          // no HDR, no bloom
    CHECK(allowed(Setting::Bloom, 0, Tier::SM3, big));
    CHECK(!allowed(Setting::Textures, 0, Tier::Modern, x550));   // full maps are 4096
    CHECK(!allowed(Setting::Shadows, 0, Tier::Modern, x550));    // a 4096 map
    CHECK(!allowed(Setting::Shadows, 1, Tier::SM2, big));        // 2048 is past SM2's floor
    CHECK(allowed(Setting::Shadows, 2, Tier::SM2, x550));
    CHECK(!allowed(Setting::Terrain, 4, Tier::Modern, big));     // the 36 m grid lost the river
    // Clamping prefers the cheaper side.
    CHECK(clamp_level(Setting::Shadows, 0, Tier::SM2, x550) == 2);

    // The modern tier starts on Ultra, which is the content as authored.
    const ContentBudget ultra = budget_for(GraphicsSettings::preset(0, Tier::Modern, big));
    CHECK(ultra.max_skinned_triangles == 0 && ultra.max_texture_size == 0 && ultra.terrain_cell_scale == 1.0f);
    CHECK(RenderTier::make(Tier::Modern).budget.shadow_size == 4096);
    CHECK(default_preset(Tier::SM2) == 2);

    // Saved and read back; unknown keys and bad values are ignored.
    GraphicsSettings g = GraphicsSettings::preset(3, Tier::SM3, big);
    GraphicsSettings back;
    back.parse(g.serialize() + "colour=7\nmodels=99\n");
    CHECK(back == g);
    int level = -1;
    CHECK(parse_preset("very-low", &level) && level == 4);
    CHECK(!parse_preset("insane", &level));

    std::printf("graphics_settings: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
