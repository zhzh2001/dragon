#include "game/element.h"

#include <cctype>
#include <cstring>

namespace game {

namespace {

const char* const NAMES[ELEMENT_COUNT] = {"fire", "frost", "blight", "storm", "tide", "stone"};
const char* const STATUS_NAMES[ELEMENT_COUNT] = {"burning", "chilled",  "corroded",
                                                 "shocked", "drenched", "staggered"};

bool same_name(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b) {
        if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) {
            return false;
        }
    }
    return *a == *b;
}

}  // namespace

const char* element_name(Element element) {
    const int i = int(element);
    return i >= 0 && i < ELEMENT_COUNT ? NAMES[i] : "none";
}

const char* element_status_name(Element element) {
    const int i = int(element);
    return i >= 0 && i < ELEMENT_COUNT ? STATUS_NAMES[i] : "";
}

Element element_from_name(const char* name, Element fallback) {
    if (!name) return fallback;
    for (int i = 0; i < ELEMENT_COUNT; ++i) {
        if (same_name(name, NAMES[i])) return Element(i);
    }
    return fallback;
}

core::Vec3 element_colour(Element element) {
    switch (element) {
        case Element::Fire: return {1.00f, 0.45f, 0.10f};
        case Element::Frost: return {0.35f, 0.78f, 1.00f};
        case Element::Blight: return {0.62f, 0.90f, 0.18f};
        case Element::Storm: return {0.70f, 0.50f, 1.00f};
        case Element::Tide: return {0.15f, 0.80f, 0.70f};
        case Element::Stone: return {0.80f, 0.60f, 0.38f};
        default: return {0.85f, 0.85f, 0.85f};
    }
}

core::Vec3 element_hide(Element element) {
    // Recolour targets (see ModelUniforms::recolour): above 1 is a push.
    switch (element) {
        case Element::Fire: return {2.1f, 0.55f, 0.3f};
        case Element::Frost: return {0.7f, 1.3f, 2.1f};
        case Element::Blight: return {1.1f, 1.8f, 0.45f};
        case Element::Storm: return {1.3f, 0.8f, 2.1f};
        case Element::Tide: return {0.45f, 1.6f, 1.5f};
        case Element::Stone: return {1.5f, 1.2f, 0.85f};
        default: return {1.0f, 1.0f, 1.0f};
    }
}

BreathProfile element_breath(Element element) {
    BreathProfile b;  // fire: the code default
    switch (element) {
        case Element::Fire: break;
        case Element::Frost:  // frostvein: a frost jet whose mist sinks and lingers
            b.scales = {0.9f, 1.25f, 0.9f, 1.0f};
            b.hot = {0.45f, 1.3f, 2.0f};
            b.cool = {0.06f, 0.3f, 0.85f};
            b.buoyancy = -7.0f;
            b.spread = 0.17f;
            b.size_start = 2.0f;
            b.size_end = 7.5f;
            b.life = 1.2f;
            b.rate = 300.0f;
            b.drag = 2.1f;
            b.brightness = 0.8f;
            break;
        case Element::Blight:  // blightmaw: a slow corrosive cloud
            b.scales = {0.8f, 1.4f, 0.8f, 0.8f};
            b.hot = {1.5f, 1.9f, 0.35f};
            b.cool = {0.3f, 0.55f, 0.08f};
            b.buoyancy = 2.0f;
            b.spread = 0.3f;
            b.size_start = 3.0f;
            b.size_end = 13.0f;
            b.life = 1.9f;
            b.rate = 220.0f;
            b.drag = 2.6f;
            b.brightness = 0.72f;
            break;
        case Element::Storm:  // stormsail: a narrow arc
            b.scales = {1.5f, 0.55f, 1.25f, 1.35f};
            b.hot = {1.15f, 0.7f, 2.4f};
            b.cool = {0.35f, 0.06f, 1.1f};
            b.buoyancy = 1.0f;
            b.spread = 0.04f;
            b.size_start = 1.2f;
            b.size_end = 2.6f;
            b.life = 0.55f;
            b.rate = 520.0f;
            b.drag = 0.9f;
            b.brightness = 0.8f;
            break;
        case Element::Tide:  // tidewrack: a pressurised jet of brine
            b.scales = {1.15f, 0.8f, 1.0f, 0.9f};
            b.hot = {0.22f, 1.45f, 1.1f};
            b.cool = {0.03f, 0.42f, 0.3f};
            b.buoyancy = -13.0f;
            b.spread = 0.14f;
            b.size_start = 2.2f;
            b.size_end = 7.0f;
            b.life = 1.0f;
            b.rate = 380.0f;
            b.drag = 1.8f;
            b.brightness = 0.62f;
            break;
        case Element::Stone:  // ironroot: a heavy blast of hot grit
            b.scales = {0.7f, 1.3f, 1.15f, 1.1f};
            b.hot = {1.55f, 0.85f, 0.28f};
            b.cool = {0.4f, 0.22f, 0.1f};
            b.buoyancy = -4.0f;
            b.spread = 0.18f;
            b.size_start = 3.2f;
            b.size_end = 10.0f;
            b.life = 1.2f;
            b.rate = 300.0f;
            b.drag = 2.4f;
            b.brightness = 0.62f;
            break;
        default: break;
    }
    b.element = element;
    return b;
}

float Status::weaken(const ElementTuning& tuning) const {
    return drench > 0.0f ? 1.0f - core::saturate(tuning.drench_weaken) : 1.0f;
}

float Status::slow(const ElementTuning& tuning) const {
    if (frozen > 0.0f) return 1.0f;
    return core::saturate(chill) * core::saturate(tuning.chill_slow);
}

bool Status::any() const {
    return burn > 0.0f || chill > 0.01f || frozen > 0.0f || corrode > 0.0f || shock > 0.0f ||
           drench > 0.0f || stagger > 0.01f;
}

float element_damage_scale(Element attack, Element defender, const Status& status,
                           const ElementTuning& tuning) {
    float scale = attack == defender && attack != Element::None
                      ? core::saturate(tuning.same_resist)
                      : 1.0f;
    if (status.corrode > 0.0f) scale *= 1.0f + core::maxf(tuning.corrode_vulnerability, 0.0f);
    return scale;
}

StatusReport apply_element(Status& status, Element attack, Element defender, float weight,
                           bool player, const ElementTuning& tuning) {
    StatusReport report;
    if (weight <= 0.0f || attack == defender || attack == Element::None) return report;
    switch (attack) {
        case Element::Fire:
            // Water puts fire out: a drenched body does not catch.
            if (status.drench > 0.0f) break;
            report.burned = status.burn <= 0.0f;
            status.burn = core::maxf(status.burn, tuning.burn_time);
            break;
        case Element::Frost:
            if (status.frozen > 0.0f) break;  // no stacking a freeze on a freeze
            status.chill += tuning.chill_per_hit * weight;
            status.since_chill = 0.0f;
            if (status.chill >= 1.0f) {
                status.chill = 0.0f;
                status.frozen = player ? tuning.player_freeze_time : tuning.freeze_time;
                report.froze = true;
            }
            break;
        case Element::Blight:
            report.corroded = status.corrode <= 0.0f;
            status.corrode = core::maxf(status.corrode, tuning.corrode_time);
            break;
        case Element::Storm:
            report.shocked = true;
            status.shock = core::maxf(status.shock, tuning.shock_jam);
            break;
        case Element::Tide:
            report.drenched = status.drench <= 0.0f;
            report.doused = status.burn > 0.0f;
            status.burn = 0.0f;
            status.drench = core::maxf(status.drench, tuning.drench_time);
            break;
        case Element::Stone:
            status.stagger += tuning.stagger_per_hit * weight;
            status.since_stagger = 0.0f;
            if (status.stagger >= 1.0f) {
                status.stagger = 0.0f;
                report.staggered = true;
            }
            break;
        default: break;
    }
    return report;
}

float tick_status(Status& status, float dt, const ElementTuning& tuning) {
    if (dt <= 0.0f) return 0.0f;
    float damage = 0.0f;
    if (status.burn > 0.0f) {
        damage += tuning.burn_dps * core::minf(dt, status.burn);
        status.burn = core::maxf(status.burn - dt, 0.0f);
    }
    if (status.corrode > 0.0f) {
        damage += tuning.corrode_dps * core::minf(dt, status.corrode);
        status.corrode = core::maxf(status.corrode - dt, 0.0f);
    }
    status.frozen = core::maxf(status.frozen - dt, 0.0f);
    status.shock = core::maxf(status.shock - dt, 0.0f);
    status.drench = core::maxf(status.drench - dt, 0.0f);
    // Build-ups recover only once the pressure stops, so a sustained stream
    // always gets there and a scattered one never does.
    status.since_chill += dt;
    status.since_stagger += dt;
    if (status.since_chill > 0.5f) status.chill = core::maxf(status.chill - tuning.chill_thaw * dt, 0.0f);
    if (status.since_stagger > 0.5f) {
        status.stagger = core::maxf(status.stagger - tuning.stagger_recover * dt, 0.0f);
    }
    return damage;
}

}  // namespace game
