#include "game/hoard_run.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include "game/combat.h"
#include "game/terrain.h"

namespace game {

using core::Vec3;

namespace {

// The same xorshift the bot placement uses: the layout must be the same for
// the same seed on every machine, which rules out std::random_device and
// leaves std::mt19937 as more than the job needs.
struct Rng {
    uint32_t state;
    explicit Rng(uint32_t seed) : state(seed ? seed : 1u) {}
    float unit() {  // [0, 1)
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return float(state & 0xffffffu) / float(0x1000000);
    }
    float signed_unit() { return unit() * 2.0f - 1.0f; }
    float range(float lo, float hi) { return lo + (hi - lo) * unit(); }
};

Vec3 horizontal(Vec3 v) { return Vec3{v.x, 0.0f, v.z}; }

// Point on the spine at fraction `f` of its length, and the spine direction
// there.
void sample_spine(const std::vector<Vec3>& spine, float f, Vec3& point, Vec3& direction) {
    if (spine.size() < 2) {
        point = spine.empty() ? Vec3::zero() : spine[0];
        direction = Vec3::forward();
        return;
    }
    float total = 0.0f;
    for (size_t i = 1; i < spine.size(); ++i) total += core::distance(spine[i - 1], spine[i]);
    float target = core::clampf(f, 0.0f, 1.0f) * total;
    for (size_t i = 1; i < spine.size(); ++i) {
        const float segment = core::distance(spine[i - 1], spine[i]);
        if (target <= segment || i + 1 == spine.size()) {
            const float t = segment > 1e-4f ? core::clampf(target / segment, 0.0f, 1.0f) : 0.0f;
            point = core::lerp(spine[i - 1], spine[i], t);
            direction = core::normalize_or(spine[i] - spine[i - 1], Vec3::forward());
            return;
        }
        target -= segment;
    }
    point = spine.back();
    direction = core::normalize_or(spine.back() - spine[spine.size() - 2], Vec3::forward());
}

}  // namespace

float RunLayout::length() const {
    float total = 0.0f;
    for (size_t i = 1; i < spine.size(); ++i) total += core::distance(spine[i - 1], spine[i]);
    return total;
}

float RunLayout::fraction_at(Vec3 position) const {
    if (spine.size() < 2) return 0.0f;
    float best = 1e30f;
    float best_along = 0.0f;
    float along = 0.0f;
    for (size_t i = 1; i < spine.size(); ++i) {
        const Vec3 a = spine[i - 1], b = spine[i];
        const Vec3 ab = b - a;
        const float len_sq = core::length_sq(ab);
        const float t = len_sq > 1e-6f ? core::saturate(core::dot(position - a, ab) / len_sq) : 0.0f;
        const Vec3 nearest = a + ab * t;
        const float d = core::length_sq(horizontal(position - nearest));
        if (d < best) {
            best = d;
            best_along = along + std::sqrt(len_sq) * t;
        }
        along += std::sqrt(len_sq);
    }
    return along > 1e-4f ? best_along / along : 0.0f;
}

RunLayout generate_run_layout(const Terrain& terrain, float half_extent,
                              const HoardRunSettings& settings) {
    RunLayout layout;
    layout.seed = settings.seed;
    Rng rng(settings.seed * 2654435761u + 12345u);
    const TerrainSettings& ts = terrain.settings();

    // The spine: the valley's own corridor, head to pass, the same span the
    // valley course flies, at a flying height above the floor.
    constexpr int SPINE_POINTS = 24;
    const float span = half_extent * 1.7f;
    const float start_z = -span * 0.5f;
    const float step = span / float(SPINE_POINTS - 1);
    for (int i = 0; i < SPINE_POINTS; ++i) {
        const float z = start_z + step * float(i);
        const float x = terrain.valley_center_x(z);
        const float ground = core::maxf(terrain.height_at(x, z), ts.water_level);
        layout.spine.push_back(Vec3{x, ground + 150.0f, z});
    }

    layout.start = layout.spine[0] + Vec3{0.0f, 30.0f, 0.0f};
    layout.start_look = layout.spine[1];

    // The pass gate stands at the far end, facing along the corridor, low
    // enough that crossing it means having come down the valley rather than
    // over the ridge.
    {
        const Vec3 end = layout.spine.back();
        const Vec3 before = layout.spine[layout.spine.size() - 2];
        const float ground = terrain.height_at(end.x, end.z);
        layout.gate.position = Vec3{end.x, ground + 110.0f, end.z};
        layout.gate.orientation = core::look_rotation(core::normalize_or(horizontal(end - before),
                                                                         Vec3::forward()));
        layout.gate.radius = settings.pass_radius;
    }

    // Caches: low, in the corridor, on ground flat enough to land on. Spaced
    // by fraction of the spine with a little jitter, so every run has one
    // early, one in the middle and one near the pass -- and the far one pays
    // most.
    for (int i = 0; i < settings.caches; ++i) {
        const float f = core::clampf((float(i) + 0.5f + 0.3f * rng.signed_unit()) /
                                         float(core::maxf(float(settings.caches), 1.0f)),
                                     0.08f, 0.9f);
        Vec3 spine_point, direction;
        sample_spine(layout.spine, f, spine_point, direction);
        const Vec3 side = core::normalize_or(core::cross(direction, Vec3::up()), Vec3::right());
        RunCache best;
        float best_flat = -1.0f;
        for (int attempt = 0; attempt < 12; ++attempt) {
            const float offset = (rng.unit() < 0.5f ? -1.0f : 1.0f) *
                                 rng.range(20.0f, settings.cache_offset_max);
            const Vec3 candidate = spine_point + side * offset;
            const float ground = terrain.height_at(candidate.x, candidate.z);
            if (ground < ts.water_level + 3.0f) continue;
            const float flat = terrain.normal_at(candidate.x, candidate.z).y;
            if (flat > best_flat) {
                best_flat = flat;
                best.position = Vec3{candidate.x, ground, candidate.z};
            }
        }
        if (best_flat < 0.0f) {
            // Nothing dry beside the spine here: put it on the valley centre.
            best.position = Vec3{spine_point.x, terrain.height_at(spine_point.x, spine_point.z),
                                 spine_point.z};
        }
        best.value = settings.cache_value * (1.0f + 0.5f * f);
        layout.caches.push_back(best);
    }

    // Ground defences: on the slopes to either side of the corridor,
    // alternating sides, out where a flight down the middle is inside their
    // range and a flight along the far ridge is not.
    for (int i = 0; i < settings.defences; ++i) {
        const float f = core::clampf((float(i) + 0.5f + 0.2f * rng.signed_unit()) /
                                         float(core::maxf(float(settings.defences), 1.0f)),
                                     0.12f, 0.92f);
        Vec3 spine_point, direction;
        sample_spine(layout.spine, f, spine_point, direction);
        const Vec3 side = core::normalize_or(core::cross(direction, Vec3::up()), Vec3::right());
        const float sign = (i % 2 == 0) ? 1.0f : -1.0f;
        RunDefence defence;
        bool placed = false;
        for (int attempt = 0; attempt < 8 && !placed; ++attempt) {
            const float offset = sign * rng.range(settings.defence_offset_min,
                                                  settings.defence_offset_max);
            const Vec3 candidate = spine_point + side * offset;
            const float ground = terrain.height_at(candidate.x, candidate.z);
            if (ground < ts.water_level + 3.0f) continue;
            // Not on a peak far above the corridor: a tower nobody flies near
            // is scenery.
            if (ground > spine_point.y + 120.0f) continue;
            defence.position = Vec3{candidate.x, ground + 8.0f, candidate.z};
            placed = true;
        }
        if (!placed) {
            defence.position = Vec3{spine_point.x, terrain.height_at(spine_point.x, spine_point.z) + 8.0f,
                                    spine_point.z};
        }
        layout.defences.push_back(defence);
    }

    // Rivals: over the corridor at posts spaced down its length, facing back
    // toward the head, where the player comes from.
    for (int i = 0; i < settings.rivals; ++i) {
        const float f = core::clampf((float(i) + 1.0f + 0.25f * rng.signed_unit()) /
                                         float(settings.rivals + 1),
                                     0.15f, 0.9f);
        Vec3 spine_point, direction;
        sample_spine(layout.spine, f, spine_point, direction);
        const Vec3 side = core::normalize_or(core::cross(direction, Vec3::up()), Vec3::right());
        RunRival rival;
        rival.position = spine_point + side * (rng.signed_unit() * 140.0f) + Vec3{0.0f, 40.0f, 0.0f};
        rival.facing = direction * -1.0f;
        layout.rivals.push_back(rival);
    }

    return layout;
}

// ---- records ----

bool RunRecords::load(const char* path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string key;
    while (in >> key) {
        if (key == "runs") in >> runs;
        else if (key == "banked") in >> banked;
        else if (key == "best_hoard") in >> best_hoard;
        else if (key == "best_time") in >> best_time;
        else if (key == "last_hoard") in >> last_hoard;
        else {
            std::string skip;
            in >> skip;
        }
    }
    return true;
}

bool RunRecords::save(const char* path) const {
    std::ofstream out(path);
    if (!out) return false;
    out << "runs " << runs << "\n";
    out << "banked " << banked << "\n";
    out << "best_hoard " << best_hoard << "\n";
    out << "best_time " << best_time << "\n";
    out << "last_hoard " << last_hoard << "\n";
    return true;
}

bool RunRecords::submit(const RunResult& result) {
    ++runs;
    last_hoard = result.hoard;
    bool record = false;
    if (result.banked) {
        ++banked;
        if (result.hoard > best_hoard) {
            best_hoard = result.hoard;
            record = true;
        }
        if (best_time <= 0.0f || result.time < best_time) best_time = result.time;
    }
    return record;
}

// ---- the run ----

void HoardRun::start(const Terrain& terrain, float half_extent) {
    layout_ = generate_run_layout(terrain, half_extent, settings);
    phase_ = HoardPhase::Flying;
    hoard_ = 0.0f;
    elapsed_ = 0.0f;
    distance_ = 0.0f;
    kills_ = 0;
    hunters_ = 0;
    collecting_ = -1;
    hunter_pending_ = false;
    just_collected_ = just_banked_ = just_lost_ = false;
    have_previous_ = false;
}

void HoardRun::abandon() {
    phase_ = HoardPhase::Idle;
    collecting_ = -1;
    hunter_pending_ = false;
    just_collected_ = just_banked_ = just_lost_ = false;
}

const char* HoardRun::phase_name() const {
    switch (phase_) {
        case HoardPhase::Idle: return "idle";
        case HoardPhase::Flying: return "flying";
        case HoardPhase::Banked: return "banked";
        case HoardPhase::Lost: return "lost";
    }
    return "?";
}

int HoardRun::caches_collected() const {
    int n = 0;
    for (const RunCache& cache : layout_.caches) n += cache.collected ? 1 : 0;
    return n;
}

float HoardRun::collect_progress() const {
    if (collecting_ < 0 || size_t(collecting_) >= layout_.caches.size()) return 0.0f;
    return layout_.caches[size_t(collecting_)].progress;
}

int HoardRun::nearest_cache(Vec3 from) const {
    int best = -1;
    float best_d = 1e30f;
    for (size_t i = 0; i < layout_.caches.size(); ++i) {
        if (layout_.caches[i].collected) continue;
        const float d = core::length_sq(layout_.caches[i].position - from);
        if (d < best_d) {
            best_d = d;
            best = int(i);
        }
    }
    return best;
}

void HoardRun::engage_rival(int index) {
    if (index >= 0 && size_t(index) < layout_.rivals.size()) layout_.rivals[size_t(index)].engaged = true;
}

Vec3 HoardRun::next_waypoint(Vec3 from) const {
    const float f = layout_.fraction_at(from);
    // The first spine point more than a segment ahead of where we are.
    const float total = layout_.length();
    float along = 0.0f;
    for (size_t i = 1; i < layout_.spine.size(); ++i) {
        along += core::distance(layout_.spine[i - 1], layout_.spine[i]);
        if (total > 1e-4f && along / total > f + 0.04f) return layout_.spine[i];
    }
    return layout_.gate.position;
}

bool HoardRun::take_hunter_request() {
    const bool pending = hunter_pending_;
    hunter_pending_ = false;
    return pending;
}

void HoardRun::finish(bool banked) {
    phase_ = banked ? HoardPhase::Banked : HoardPhase::Lost;
    if (banked) just_banked_ = true;
    else just_lost_ = true;
    collecting_ = -1;
}

RunResult HoardRun::result() const {
    RunResult r;
    r.seed = layout_.seed;
    r.banked = phase_ == HoardPhase::Banked;
    r.carried = hoard_;
    r.hoard = r.banked ? hoard_ : 0.0f;
    r.caches = caches_collected();
    r.kills = kills_;
    r.hunters = hunters_;
    r.time = elapsed_;
    r.distance = distance_;
    return r;
}

void HoardRun::update(float dt, const FlightState& player, bool player_alive,
                      const CombatEvents& events) {
    just_collected_ = just_banked_ = just_lost_ = false;
    if (phase_ != HoardPhase::Flying) return;

    elapsed_ += dt;
    kills_ += events.kills;
    if (have_previous_) distance_ += core::distance(player.position, previous_position_);

    // Death ends the run and the hoard is lost.
    if (events.player_died || !player_alive) {
        finish(false);
        previous_position_ = player.position;
        have_previous_ = true;
        return;
    }

    // Rivals wake as the player comes down the corridor.
    for (RunRival& rival : layout_.rivals) {
        if (!rival.engaged && core::distance(player.position, rival.position) < settings.engage_range) {
            rival.engaged = true;
        }
    }

    // The dragonslayers.
    if (elapsed_ > settings.pressure_after && hunters_ < settings.max_hunters &&
        elapsed_ - settings.pressure_after >= float(hunters_) * settings.pressure_interval) {
        ++hunters_;
        hunter_pending_ = true;
    }

    // Collecting: grounded inside a cache's radius for the collect time.
    collecting_ = -1;
    for (size_t i = 0; i < layout_.caches.size(); ++i) {
        RunCache& cache = layout_.caches[i];
        if (cache.collected) continue;
        const float d = core::length(horizontal(player.position - cache.position));
        const bool inside = player.grounded && d <= settings.cache_radius;
        if (inside) {
            collecting_ = int(i);
            cache.progress += dt / core::maxf(settings.collect_time, 0.05f);
            if (cache.progress >= 1.0f) {
                cache.progress = 1.0f;
                cache.collected = true;
                hoard_ += cache.value;
                just_collected_ = true;
                collecting_ = -1;
            }
        } else if (cache.progress > 0.0f) {
            cache.progress = core::maxf(cache.progress - 2.0f * dt / core::maxf(settings.collect_time, 0.05f),
                                        0.0f);
        }
    }

    // The pass: a segment test like a checkpoint, so a fast crossing cannot
    // step over the plane between frames.
    if (have_previous_) {
        const RingCrossing crossing = test_ring(layout_.gate, previous_position_, player.position);
        if (crossing.passed) finish(true);
    }
    previous_position_ = player.position;
    have_previous_ = true;
}

}  // namespace game
