#include "game/prey.h"

#include <cmath>

#include "game/combat.h"
#include "game/hoard_run.h"
#include "game/terrain.h"

namespace game {

using core::Vec3;

namespace {

Vec3 flat(Vec3 v) { return Vec3{v.x, 0.0f, v.z}; }

// Turns `heading` toward `want` by at most `max_angle`, in the ground plane.
Vec3 turn_toward(Vec3 heading, Vec3 want, float max_angle) {
    const Vec3 h = core::normalize_or(flat(heading), Vec3::forward());
    const Vec3 w = core::normalize_or(flat(want), h);
    const float a = std::atan2(h.x, h.z);
    const float b = std::atan2(w.x, w.z);
    float d = b - a;
    while (d > core::PI) d -= core::TWO_PI;
    while (d < -core::PI) d += core::TWO_PI;
    d = core::clampf(d, -max_angle, max_angle);
    const float r = a + d;
    return Vec3{std::sin(r), 0.0f, std::cos(r)};
}

bool edible(const Prey& p) { return p.state != PreyState::Eaten; }
bool living(const Prey& p) { return p.state != PreyState::Eaten && p.state != PreyState::Carcass; }

}  // namespace

PreyClip prey_clip(const Prey& prey, const PreyTuning& tuning) {
    if (prey.state == PreyState::Carcass || prey.state == PreyState::Eaten) return PreyClip::Dead;
    if (prey.speed > 0.45f * tuning.run_speed) return PreyClip::Run;
    if (prey.speed > 0.4f) return PreyClip::Walk;
    return PreyClip::Graze;
}

float PreyHerds::unit() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return float(rng_ & 0xffffffu) / float(0x1000000);
}

void PreyHerds::reset(const std::vector<RunHerd>& herds, const Terrain& terrain, uint32_t seed) {
    clear();
    rng_ = seed ? seed : 1u;
    for (size_t h = 0; h < herds.size(); ++h) {
        homes_.push_back(herds[h].position);
        for (int i = 0; i < herds[h].count; ++i) {
            Prey p;
            const float angle = unit() * core::TWO_PI;
            const float r = std::sqrt(unit()) * tuning.herd_radius * 0.6f;
            p.position = herds[h].position + Vec3{std::cos(angle) * r, 0.0f, std::sin(angle) * r};
            p.position.y = terrain.height_at(p.position.x, p.position.z);
            const float facing = unit() * core::TWO_PI;
            p.heading = Vec3{std::sin(facing), 0.0f, std::cos(facing)};
            p.health = tuning.health;
            p.herd = int(h);
            p.timer = 1.0f + unit() * 4.0f;
            p.clip_time = unit() * 4.0f;
            p.wander = unit() * core::TWO_PI;
            animals_.push_back(p);
        }
    }
}

int PreyHerds::alive() const {
    int n = 0;
    for (const Prey& p : animals_) n += living(p) ? 1 : 0;
    return n;
}

int PreyHerds::nearest(Vec3 from, float max_range) const {
    int best = -1;
    float best_d = max_range * max_range;
    for (size_t i = 0; i < animals_.size(); ++i) {
        if (!edible(animals_[i])) continue;
        const float d = core::length_sq(animals_[i].position - from);
        if (d < best_d) {
            best_d = d;
            best = int(i);
        }
    }
    return best;
}

void PreyHerds::kill(int index) {
    if (index < 0 || size_t(index) >= animals_.size()) return;
    Prey& p = animals_[size_t(index)];
    if (!living(p)) return;
    p.state = PreyState::Carcass;
    p.timer = tuning.carcass_time;
    p.speed = 0.0f;
    p.burnt = core::maxf(p.burnt, 0.6f);
}

void PreyHerds::eat_now(int index, PreyEvents& events) {
    if (index < 0 || size_t(index) >= animals_.size()) return;
    if (animals_[size_t(index)].state == PreyState::Eaten) return;
    eat(animals_[size_t(index)], events, false);
}

void PreyHerds::eat(Prey& prey, PreyEvents& events, bool swoop) {
    prey.state = PreyState::Eaten;
    prey.speed = 0.0f;
    ++events.eaten;
    events.where = prey.position;
    events.by_swoop = events.by_swoop || swoop;
}

PreyEvents PreyHerds::update(float dt, const FlightState& dragon, float dragon_size,
                             const Terrain& terrain) {
    PreyEvents events;
    if (dt <= 0.0f) return events;
    const float water = terrain.settings().water_level;
    const float size = core::maxf(dragon_size, 0.1f);
    for (size_t i = 0; i < animals_.size(); ++i) {
        Prey& p = animals_[i];
        if (p.state == PreyState::Eaten) continue;

        // The swoop, before anything moves: the dragon's body over it.
        const Vec3 to_dragon = dragon.position - p.position;
        const float horizontal = core::length(flat(to_dragon));
        const float above = to_dragon.y;
        if (horizontal < tuning.grab_radius * size && above < tuning.grab_height * size &&
            above > -3.0f) {
            eat(p, events, !dragon.grounded);
            continue;
        }

        if (p.state == PreyState::Carcass) {
            p.timer -= dt;
            if (p.timer <= 0.0f) p.state = PreyState::Eaten;
            continue;
        }

        // Noticing: close, and low enough to be a threat rather than a speck.
        const float height = dragon.position.y - p.position.y;
        const bool seen = horizontal < tuning.notice_range && height < tuning.notice_height;
        const bool close = horizontal < tuning.bolt_range && height < tuning.notice_height;
        const Vec3 home = homes_.empty() ? p.position : homes_[size_t(p.herd) % homes_.size()];

        Vec3 want = p.heading;
        float target_speed = 0.0f;
        if (close || (p.state == PreyState::Flee && seen)) {
            p.state = PreyState::Flee;
            p.timer = 0.0f;
        } else if (seen && p.state != PreyState::Flee) {
            p.state = PreyState::Alert;
        }
        switch (p.state) {
            case PreyState::Graze:
            case PreyState::Walk: {
                p.timer -= dt;
                if (p.timer <= 0.0f) {
                    // Bouts: graze in place, then amble a few metres.
                    const bool walk = p.state == PreyState::Graze && unit() < 0.6f;
                    p.state = walk ? PreyState::Walk : PreyState::Graze;
                    p.timer = walk ? 2.0f + unit() * 3.0f : 3.0f + unit() * 5.0f;
                    p.wander += (unit() - 0.5f) * 2.5f;
                }
                if (p.state == PreyState::Walk) {
                    want = Vec3{std::sin(p.wander), 0.0f, std::cos(p.wander)};
                    // Stray too far and the wander bends home.
                    const Vec3 back = flat(home - p.position);
                    const float out = core::length(back);
                    if (out > tuning.herd_radius) want = core::normalize_or(back, want);
                    target_speed = tuning.walk_speed;
                }
                break;
            }
            case PreyState::Alert:
                // Heads up, facing the threat, still. Calm again once it goes.
                want = flat(to_dragon);
                target_speed = 0.0f;
                if (!seen) {
                    p.timer += dt;
                    if (p.timer > 1.5f) {
                        p.state = PreyState::Graze;
                        p.timer = 2.0f;
                    }
                } else {
                    p.timer = 0.0f;
                }
                break;
            case PreyState::Flee: {
                // Away from the dragon, a little with the herd.
                want = core::normalize_or(flat(p.position - dragon.position), p.heading);
                if (!seen) {
                    p.timer += dt;
                    if (p.timer > tuning.calm_time) {
                        p.state = PreyState::Alert;
                        p.timer = 0.0f;
                    }
                }
                target_speed = tuning.run_speed * (seen ? 1.0f : 0.6f);
                break;
            }
            default: break;
        }

        // Keep apart, and away from water and cliffs ahead.
        for (size_t j = 0; j < animals_.size(); ++j) {
            if (j == i || !living(animals_[j])) continue;
            const Vec3 d = flat(p.position - animals_[j].position);
            const float dist = core::length(d);
            if (dist < tuning.spacing && dist > 1e-3f) want = want + d * (1.5f / dist);
        }
        const Vec3 look = p.position + core::normalize_or(flat(want), p.heading) * 8.0f;
        const float ground_ahead = terrain.height_at(look.x, look.z);
        if (ground_ahead < water + 1.0f || std::fabs(ground_ahead - p.position.y) > 5.0f) {
            // Blocked: turn toward home, which is dry and flat by construction.
            want = core::normalize_or(flat(home - p.position), flat(want) * -1.0f);
        }

        if (core::length_sq(flat(want)) > 1e-6f) {
            p.heading = turn_toward(p.heading, want, tuning.turn_rate * dt);
        }
        const float step = tuning.accel * dt;
        p.speed += core::clampf(target_speed - p.speed, -step * 1.5f, step);
        p.speed = core::maxf(p.speed, 0.0f);
        Vec3 next = p.position + p.heading * (p.speed * dt);
        const float g = terrain.height_at(next.x, next.z);
        if (g < water + 0.5f) {
            next = p.position;  // never into the river
            p.speed = 0.0f;
        }
        p.position = Vec3{next.x, g, next.z};

        // The clip advances with the gait, so a slow walk does not paddle.
        const PreyClip clip = prey_clip(p, tuning);
        const float rate = clip == PreyClip::Run    ? p.speed / core::maxf(tuning.run_speed, 1.0f)
                           : clip == PreyClip::Walk ? p.speed / core::maxf(tuning.walk_speed, 0.1f)
                                                    : 1.0f;
        p.clip_time += dt * core::clampf(rate, 0.3f, 1.6f);
    }
    return events;
}

void PreyHerds::breathe(Vec3 origin, Vec3 axis, float half_angle_rad, float range, float dps,
                        float dt, PreyEvents& events) {
    for (Prey& p : animals_) {
        if (!living(p)) continue;
        const Vec3 body = p.position + Vec3{0.0f, 1.5f * tuning.scale, 0.0f};
        const Vec3 near = body + core::normalize_or(origin - body, Vec3::zero()) * tuning.body();
        if (!point_in_cone(body, origin, axis, half_angle_rad, range) &&
            !point_in_cone(near, origin, axis, half_angle_rad, range)) {
            continue;
        }
        p.health -= dps * dt;
        p.burnt = core::minf(p.burnt + dt * 1.5f, 1.0f);
        if (p.state != PreyState::Flee) {
            p.state = PreyState::Flee;
            p.timer = 0.0f;
        }
        if (p.health <= 0.0f) {
            p.state = PreyState::Carcass;
            p.timer = tuning.carcass_time;
            p.speed = 0.0f;
            p.burnt = 1.0f;
            ++events.killed;
        }
    }
}

void PreyHerds::blast(Vec3 at, float radius, float damage, PreyEvents& events) {
    for (Prey& p : animals_) {
        if (!living(p)) continue;
        const float d = core::distance(p.position + Vec3{0.0f, 1.5f * tuning.scale, 0.0f}, at);
        if (d > radius + tuning.body()) continue;
        p.health -= damage * (1.0f - core::saturate(d / core::maxf(radius + tuning.body(), 0.1f)) * 0.5f);
        p.burnt = core::minf(p.burnt + 0.5f, 1.0f);
        p.state = PreyState::Flee;
        p.timer = 0.0f;
        if (p.health <= 0.0f) {
            p.state = PreyState::Carcass;
            p.timer = tuning.carcass_time;
            p.speed = 0.0f;
            p.burnt = 1.0f;
            ++events.killed;
        }
    }
}

void PreyHerds::bite(Vec3 mouth, Vec3 forward, float reach, float half_angle_rad,
                     PreyEvents& events) {
    // The closest one in the cone: a bite takes one animal, not a herd.
    Prey* best = nullptr;
    float best_d = 1e30f;
    for (Prey& p : animals_) {
        if (!edible(p)) continue;
        const Vec3 body = p.position + Vec3{0.0f, 1.5f * tuning.scale, 0.0f};
        const Vec3 near = body + core::normalize_or(mouth - body, Vec3::zero()) * tuning.body();
        if (!point_in_cone(body, mouth, forward, half_angle_rad, reach) &&
            !point_in_cone(near, mouth, forward, half_angle_rad, reach)) {
            continue;
        }
        const float d = core::distance(body, mouth);
        if (d < best_d) {
            best_d = d;
            best = &p;
        }
    }
    if (best) eat(*best, events, false);
}

}  // namespace game
