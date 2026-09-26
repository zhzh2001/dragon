#include "game/demo_pilot.h"

#include <cmath>

#include "game/terrain.h"

namespace game {

using core::Vec3;

namespace {

Vec3 horizontal(Vec3 v) { return Vec3{v.x, 0.0f, v.z}; }
float horizontal_distance(Vec3 a, Vec3 b) { return core::length(horizontal(a - b)); }

bool is_dragon(DemoTargetKind kind) {
    return kind == DemoTargetKind::Rival || kind == DemoTargetKind::Hunter;
}

int find_slot(const DemoWorld& world, int slot) {
    for (size_t i = 0; i < world.targets.size(); ++i) {
        if (world.targets[i].slot == slot) return int(i);
    }
    return -1;
}

}  // namespace

const char* demo_state_name(DemoState state) {
    switch (state) {
        case DemoState::Cruise: return "cruise";
        case DemoState::Fight: return "fight";
        case DemoState::Siege: return "siege";
        case DemoState::Land: return "land";
        case DemoState::Walk: return "walk";
        case DemoState::Collect: return "collect";
        case DemoState::TakeOff: return "take off";
        case DemoState::Flee: return "flee";
        case DemoState::Hunt: return "hunt";
    }
    return "?";
}

void DemoPilot::reset(uint32_t seed) {
    state_ = DemoState::Cruise;
    target_ = -1;
    decide_timer_ = 0.0f;
    extend_timer_ = 0.0f;
    go_round_timer_ = 0.0f;
    state_time_ = 0.0f;
    recovering_ = false;
    fight_slot_ = disengaged_slot_ = siege_slot_ = -1;
    relock_timer_ = 0.0f;
    fight_best_health_ = 1.0f;
    fight_since_progress_ = disengage_timer_ = siege_elapsed_ = hunt_rest_timer_ = 0.0f;
    run_in_ = entry_set_ = false;
    skipped_cache_ = Vec3{1e9f, 1e9f, 1e9f};
    siege_shots = 0;
    siege_best_off_axis_deg = 180.0f;
    siege_last_range = 0.0f;
    fighter_.tuning = fight_tuning;
    fighter_.reset(seed);
}

float DemoPilot::ground_ahead(const FlightState& self, const DemoWorld& world) const {
    if (!world.terrain) return -1e9f;
    float ground = world.terrain->height_at(self.position.x, self.position.z);
    for (float look = 1.0f; look <= 3.0f; look += 1.0f) {
        const Vec3 ahead = self.position + self.velocity * look;
        ground = core::maxf(ground, world.terrain->height_at(ahead.x, ahead.z));
    }
    return ground;
}

// Which job, revisited every `decide_interval`. The slot of the chosen target
// is kept in `target_` so the choice survives the target list being rebuilt
// every frame.
void DemoPilot::decide(const FlightState& self, const DemoWorld& world) {
    const DemoState previous = state_;
    const int previous_target = target_;
    // A take-off is finished only when the dragon has climbed out.
    if (previous == DemoState::TakeOff && state_time_ < tuning.takeoff_budget &&
        (self.grounded || self.ground_clearance < tuning.climb_out_height)) {
        // Still on the ground inside a cache's ring: that is a landing, not
        // a take-off to finish.
        const bool on_cache = self.grounded && world.has_cache &&
                              horizontal_distance(self.position, world.cache) < world.cache_radius * 0.6f;
        if (!on_cache) return;
    }
    auto choose = [&](DemoState state, int slot) {
        state_ = state;
        target_ = slot;
    };

    if (self.grounded) {
        if (previous == DemoState::Hunt) hunt_rest_timer_ = tuning.hunt_touchdown_rest;
        const float d = world.has_cache ? horizontal_distance(self.position, world.cache) : 1e9f;
        if (world.has_cache && d < world.cache_radius * 0.6f) {
            choose(DemoState::Collect, -1);
        } else if (world.has_cache && d < tuning.walk_limit && world.cache_guard < 0) {
            choose(DemoState::Walk, -1);
        } else {
            choose(DemoState::TakeOff, -1);
        }
        if (state_ != previous) state_time_ = 0.0f;
        return;
    }

    // A siege in its extension, or a landing already on the glide path, is
    // only broken off by a dragon coming close.
    int nearest_dragon = -1;
    float nearest_dragon_range = 1e9f;
    int hunter = -1;
    float hunter_range = 1e9f;
    int drone = -1;
    float drone_range = 1e9f;
    for (size_t i = 0; i < world.targets.size(); ++i) {
        const DemoTarget& t = world.targets[i];
        const float r = core::distance(self.position, t.position);
        // Broken off from this one: a pursuer that follows does not pull the
        // pilot back into the same stalemate (it gets the opportunistic
        // shots below instead).
        if (t.slot == disengaged_slot_ && disengage_timer_ > 0.0f) continue;
        // On the way to the pass, only what is on top of us.
        if (world.rush && r > tuning.rush_fight_range) continue;
        if (t.kind == DemoTargetKind::Hunter && r < hunter_range) {
            hunter = int(i);
            hunter_range = r;
        }
        if (is_dragon(t.kind)) {
            const float reach = t.dormant ? tuning.fight_dormant_range : tuning.fight_rival_range;
            if (r < reach && r < nearest_dragon_range) {
                nearest_dragon = int(i);
                nearest_dragon_range = r;
            }
        }
        if (t.kind == DemoTargetKind::Drone && r < drone_range) {
            drone = int(i);
            drone_range = r;
        }
    }
    const float threat_range = core::minf(nearest_dragon_range, hunter_range);

    (void)threat_range;
    const bool hurt = world.health_fraction < tuning.flee_health ||
                      (previous == DemoState::Flee && world.health_fraction < tuning.recover_health);
    if (hurt && !(hunter >= 0 && hunter_range < 250.0f)) {
        choose(DemoState::Flee, -1);
    } else if (hunter >= 0 && hunter_range < tuning.fight_hunter_range) {
        choose(DemoState::Fight, world.targets[size_t(hunter)].slot);
    } else if (nearest_dragon >= 0) {
        choose(DemoState::Fight, world.targets[size_t(nearest_dragon)].slot);
    } else if (drone >= 0 && drone_range < tuning.fight_drone_range) {
        choose(DemoState::Fight, world.targets[size_t(drone)].slot);
    } else if (world.in_run && !world.rush && world.has_prey && hunt_rest_timer_ <= 0.0f &&
               horizontal_distance(self.position, world.prey) < tuning.hunt_range &&
               (!world.has_cache ||
                horizontal_distance(self.position, world.prey) <
                    horizontal_distance(self.position, world.cache)) &&
               !(previous == DemoState::Hunt && state_time_ > tuning.hunt_budget)) {
        choose(DemoState::Hunt, -1);
    } else if (world.in_run && !world.rush && world.has_cache && go_round_timer_ <= 0.0f &&
               horizontal_distance(world.cache, skipped_cache_) > 1.0f &&
               horizontal_distance(self.position, world.cache) < tuning.cache_range) {
        if (world.cache_guard >= 0 && size_t(world.cache_guard) < world.targets.size()) {
            choose(DemoState::Siege, world.targets[size_t(world.cache_guard)].slot);
        } else {
            choose(DemoState::Land, -1);
        }
    } else {
        choose(DemoState::Cruise, -1);
    }
    if (state_ != previous || target_ != previous_target) {
        state_time_ = 0.0f;
        if (state_ != DemoState::Siege) extend_timer_ = 0.0f;
        run_in_ = false;
        entry_set_ = false;
    }
}

DemoDecision DemoPilot::update(float dt, const FlightState& self, const DemoWorld& world) {
    state_time_ += dt;
    time_in[int(state_)] += dt;
    go_round_timer_ = core::maxf(go_round_timer_ - dt, 0.0f);
    hunt_rest_timer_ = core::maxf(hunt_rest_timer_ - dt, 0.0f);
    // A hunt that ran out of time rests before the next.
    if (state_ == DemoState::Hunt && state_time_ > tuning.hunt_budget) {
        hunt_rest_timer_ = tuning.hunt_rest;
        decide_timer_ = 0.0f;
    }
    disengage_timer_ = core::maxf(disengage_timer_ - dt, 0.0f);
    if (state_ == DemoState::Fight) {
        const int index = find_slot(world, target_);
        if (index >= 0) {
            const float h = world.targets[size_t(index)].health;
            if (target_ != fight_slot_) {
                fight_slot_ = target_;
                fight_best_health_ = h;
                fight_since_progress_ = 0.0f;
            } else if (h < fight_best_health_ - 0.05f) {
                fight_best_health_ = h;
                fight_since_progress_ = 0.0f;
            } else {
                fight_since_progress_ += dt;
                fight_best_health_ = core::minf(fight_best_health_, h);
            }
            if (fight_since_progress_ > tuning.stalemate_time) {
                disengaged_slot_ = target_;
                disengage_timer_ = tuning.disengage_time;
                fight_slot_ = -1;
                decide_timer_ = 0.0f;
            }
        }
    } else {
        fight_slot_ = -1;
    }
    if (state_ == DemoState::Siege) {
        if (target_ != siege_slot_) {
            siege_slot_ = target_;
            siege_elapsed_ = 0.0f;
        }
        siege_elapsed_ += dt;
        if (siege_elapsed_ > tuning.siege_budget && world.has_cache) {
            skipped_cache_ = world.cache;
            siege_slot_ = -1;
            decide_timer_ = 0.0f;
        }
    }
    decide_timer_ -= dt;
    // A target that has gone (killed, out of the list) ends its job at once.
    const bool target_gone = (state_ == DemoState::Fight || state_ == DemoState::Siege) &&
                             find_slot(world, target_) < 0;
    // On the ground the job follows the ground at once: a landing becomes a
    // walk the frame the feet touch.
    const bool ground_changed =
        state_ != DemoState::TakeOff &&
        self.grounded != (state_ == DemoState::Walk || state_ == DemoState::Collect);
    if (decide_timer_ <= 0.0f || target_gone || ground_changed) {
        decide(self, world);
        decide_timer_ = tuning.decide_interval;
    }

    DemoDecision d = act(dt, self, world);
    relock_timer_ = core::maxf(relock_timer_ - dt, 0.0f);
    const int mark = find_slot(world, target_);
    if (mark >= 0 && world.locked_slot != target_) {
        const Vec3 to = world.targets[size_t(mark)].position - self.position;
        if (relock_timer_ <= 0.0f && core::length(to) < world.lock_range &&
            core::dot(self.forward(), core::normalize_or(to, self.forward())) >
                std::cos(core::radians(world.lock_cone_deg * tuning.relock_cone_fraction))) {
            d.cycle_target = true;
            relock_timer_ = tuning.relock_interval;
        }
        // Do not spend a fireball on a different sticky lock. On the next
        // frame the observed lock confirms whether the tap found our mark.
        if (world.locked_slot >= 0) d.fire = d.breath = false;
    }
    const bool final_approach = state_ == DemoState::Land && world.has_cache &&
                                horizontal_distance(self.position, world.cache) < 300.0f;
    if (!self.grounded && !final_approach && state_ != DemoState::TakeOff) {
        if (self.airspeed < tuning.min_speed) recovering_ = true;
        if (self.airspeed > tuning.recover_speed) recovering_ = false;
    } else {
        recovering_ = false;
    }
    if (recovering_ && self.ground_clearance > 40.0f) {
        d.flight.pitch = -0.7f;
        d.flight.roll = 0.0f;
        d.flight.flap = 1.0f;
        d.flight.brake = 0.0f;
        d.flight.tuck = 0.0f;
        d.maneuver = ManeuverKind::None;
    }
    return d;
}

DemoDecision DemoPilot::act(float dt, const FlightState& self, const DemoWorld& world) {
    switch (state_) {
        case DemoState::Fight: return fight(dt, self, world);
        case DemoState::Siege: return siege(dt, self, world);
        case DemoState::Land: return land(self, world);
        case DemoState::Walk:
        case DemoState::Collect: return walk(self, world);
        case DemoState::Hunt: return hunt(self, world);
        case DemoState::TakeOff: {
            DemoDecision d;
            // Which way to climb out: toward the corridor, unless the ground
            // ahead rises -- then down the slope, into open air. Held level
            // into a rising slope, a dragon that touched down on a valley
            // wall skated 130 m up it, leaping all the way.
            const float here = self.position.y - self.ground_clearance;
            const float rise = ground_ahead(self, world) - here;
            Vec3 heading = core::normalize_or(horizontal(world.waypoint - self.position),
                                              horizontal(self.forward()));
            if (world.terrain && rise > 20.0f) {
                const Vec3 n = world.terrain->normal_at(self.position.x, self.position.z);
                heading = core::normalize_or(horizontal(n), heading);  // downhill
            }
            if (self.grounded) {
                // Turn to the heading on the ground, and leap on the flap's
                // rising edge: beat on and off, so a dragon that touches down
                // again leaps again.
                const Vec3 forward = core::normalize_or(horizontal(self.forward()), Vec3::forward());
                const Vec3 right = core::normalize_or(core::cross(forward, Vec3::up()), Vec3::right());
                const float error = std::atan2(core::dot(heading, right), core::dot(heading, forward));
                d.flight.walk_turn = core::clampf(error * 2.0f, -1.0f, 1.0f);
                d.flight.flap = std::fabs(error) < 0.6f && std::fmod(state_time_, 0.5f) > 0.25f ? 1.0f : 0.0f;
                return d;
            }
            // Airborne: wings beating; the steering's roll to the heading, and
            // the nose level until there is speed to climb on, then a steady
            // climb -- steeper if the ground ahead is rising.
            const FlightInput steer =
                steer_through(self, self.position + heading * 300.0f + Vec3{0.0f, 20.0f, 0.0f}, Vec3::zero(),
                              steering, ground_ahead(self, world));
            d.flight.flap = 1.0f;
            d.boost = self.ground_clearance > tuning.takeoff_boost_height &&
                      self.airspeed < tuning.takeoff_boost_speed;
            // Nearly wings-level until clear of the floor: a bank spends the
            // lift a climb from a standstill does not have.
            const float bank = self.ground_clearance < 25.0f ? 0.15f : 0.5f;
            d.flight.roll = core::clampf(steer.roll, -bank, bank);
            d.flight.yaw = steer.yaw;
            // A modest attitude from the start (level never left the floor:
            // every touchdown bled the speed to friction), more once fast.
            const float climb = rise > 20.0f ? tuning.climb_attitude_rising : tuning.climb_attitude;
            const float pitch_now = std::asin(core::clampf(self.forward().y, -1.0f, 1.0f));
            d.flight.pitch = core::clampf((climb - pitch_now) * 2.5f, -0.6f, 0.6f);
            return d;
        }
        case DemoState::Flee: {
            // On the rush, falling back means forward: to the pass, boosting.
            // Back up the corridor, a hunter followed, and the pilot fell
            // back again, and again -- a 540 s run that never got there.
            if (world.rush) return cruise(self, world, world.waypoint, true);
            // Back to the safe point, then a wide circle over it.
            const Vec3 to = world.safe_point - self.position;
            Vec3 aim = world.safe_point;
            if (core::length(horizontal(to)) < 250.0f) {
                aim = world.safe_point + Vec3{std::cos(state_time_ * 0.25f), 0.0f,
                                              std::sin(state_time_ * 0.25f)} * 300.0f;
            }
            return cruise(self, world, aim, core::length(to) > 600.0f);
        }
        case DemoState::Cruise:
        default: return cruise(self, world, world.waypoint, world.rush);
    }
}

DemoDecision DemoPilot::cruise(const FlightState& self, const DemoWorld& world, Vec3 aim,
                               bool boost) {
    DemoDecision d;
    d.flight = steer_through(self, aim, Vec3::zero(), steering, ground_ahead(self, world));
    d.boost = boost && self.ground_clearance > tuning.takeoff_boost_height &&
              core::dot(self.forward(), core::normalize_or(aim - self.position, self.forward())) >
                  std::cos(core::radians(tuning.cruise_boost_cone_deg));
    // Opportunistic weapons: whatever crosses the nose on the way takes a
    // fireball, the flame close in, and a bite inside reach. Flying on is not
    // flying unarmed.
    for (const DemoTarget& t : world.targets) {
        const Vec3 to = t.position - self.position;
        const float r = core::length(to);
        const float off = std::acos(core::clampf(
            core::dot(self.forward(), core::normalize_or(to, self.forward())), -1.0f, 1.0f));
        if (r < 480.0f && off < core::radians(12.0f)) d.fire = true;
        if (r < 150.0f && off < core::radians(20.0f)) d.breath = true;
        if (r < 28.0f && is_dragon(t.kind)) d.melee = true;
    }
    return d;
}

// Dogfights through a bot pilot aimed at the chosen target. Its fire and
// flame map onto the player's buttons; the player's own aim assist and lock
// then do what they do for a person.
DemoDecision DemoPilot::fight(float dt, const FlightState& self, const DemoWorld& world) {
    const int index = find_slot(world, target_);
    if (index < 0) return cruise(self, world, world.waypoint, false);
    const DemoTarget& t = world.targets[size_t(index)];
    FlightState mark;
    mark.position = t.position;
    mark.velocity = t.velocity;
    mark.airspeed = core::length(t.velocity);
    mark.orientation = core::look_rotation(core::normalize_or(t.velocity, self.forward()),
                                           Vec3::up());
    fighter_.tuning = fight_tuning;
    const BotDecision b =
        fighter_.update(dt, self, mark, true, ground_ahead(self, world), world.health_fraction);
    DemoDecision d;
    d.flight = b.flight;
    d.fire = b.fire;
    d.breath = b.breathe;
    d.melee = b.melee;
    d.boost = b.flight.boost > 0.5f;
    d.flight.boost = 0.0f;  // boost is a combat button for the player
    // Rolls only: a flip is a half loop, and at a drake's speed it ended in a
    // stall at 13 m/s with the mark sitting behind it.
    d.maneuver = b.maneuver == ManeuverKind::Flip ? ManeuverKind::Roll : b.maneuver;
    d.maneuver_direction = b.maneuver_direction;
    return d;
}

// A tower cannot be dogfought: the bot pilot's terrain floor keeps it 130 m
// up, out of reach. So a siege is a strafing pass -- come in high, point the
// nose at the tower, fireballs inside range and the flame close in, pull up
// before the ground or the tower, extend, come round -- the way a person
// takes one out without landing next to it.
DemoDecision DemoPilot::siege(float dt, const FlightState& self, const DemoWorld& world) {
    const int index = find_slot(world, target_);
    if (index < 0) return cruise(self, world, world.waypoint, false);
    const Vec3 tower = world.targets[size_t(index)].position;
    const float ground = ground_ahead(self, world);
    auto ground_at = [&](Vec3 p) {
        return world.terrain ? world.terrain->height_at(p.x, p.z) : tower.y;
    };
    DemoDecision d;

    // Extend: out to the far side and up. That far side is the next entry.
    if (extend_timer_ > 0.0f) {
        extend_timer_ -= dt;
        if (core::length(horizontal(extend_point_ - self.position)) < 120.0f) extend_timer_ = 0.0f;
        d.flight = steer_through(self, extend_point_, Vec3::zero(), steering, ground);
        if (self.airspeed < 45.0f) d.flight.flap = 1.0f;
        if (extend_timer_ <= 0.0f) {
            run_in_ = false;
            entry_set_ = false;
        }
        return d;
    }

    const Vec3 to = tower - self.position;
    const float range = core::length(to);
    const Vec3 from_tower = core::normalize_or(horizontal(self.position - tower), Vec3::forward());

    // Set up: climb to the entry point on this side of the tower, then turn
    // in. A run in that starts close and off-axis is a steep turning dive,
    // and that is how the first builds flew into the ground.
    if (!run_in_) {
        // Frozen when the set-up begins: an entry recomputed from the
        // dragon's own position every frame moved with it, and the dragon
        // circled it for a minute.
        if (!entry_set_) {
            // Eight bearings round the tower; the one over the lowest ground
            // wins, nudged toward the dragon's own side so it does not fly
            // round the tower to reach it. A fixed side put the entry up a
            // canyon wall.
            float best_score = 1e30f;
            for (int k = 0; k < 8; ++k) {
                const float a = core::TWO_PI * float(k) / 8.0f;
                const Vec3 dir{std::cos(a), 0.0f, std::sin(a)};
                const Vec3 p = tower + dir * tuning.siege_entry_range;
                const float score = ground_at(p) - tower.y +
                                    120.0f * (1.0f - core::dot(dir, from_tower));
                if (score < best_score) {
                    best_score = score;
                    entry_point_ = p;
                }
            }
            entry_point_.y = core::maxf(tower.y, ground_at(entry_point_)) + tuning.siege_entry_height;
            entry_set_ = true;
        }
        const float to_entry = core::length(horizontal(entry_point_ - self.position));
        const float off = std::acos(core::clampf(
            core::dot(core::normalize_or(horizontal(self.forward()), Vec3::forward()),
                      core::normalize_or(horizontal(to), Vec3::forward())),
            -1.0f, 1.0f));
        // Commit only facing the tower: committing at the entry while still
        // heading away made the turn back a descending turn, which tripped
        // the pull-up, which extended away again -- forever.
        const bool facing = off < core::radians(35.0f);
        if (facing && (to_entry < 180.0f || range > tuning.siege_entry_range * 0.75f)) {
            run_in_ = true;
        } else {
            // To the entry; once there, a level turn toward the tower.
            const Vec3 aim = to_entry < 180.0f || range > tuning.siege_entry_range
                                 ? Vec3{tower.x, entry_point_.y, tower.z}
                                 : entry_point_;
            d.flight = steer_through(self, aim, Vec3::zero(), steering, ground);
            if (self.airspeed < 42.0f) d.flight.flap = 1.0f;
            return d;
        }
    }

    // Pull up close in, or on where the dive will be in a second and a half.
    const float predicted = self.ground_clearance + core::minf(self.velocity.y, 0.0f) * 1.5f;
    if (range < tuning.siege_pull_up || predicted < tuning.siege_floor) {
        const Vec3 beyond = core::normalize_or(horizontal(to), from_tower * -1.0f);
        extend_point_ = tower + beyond * tuning.siege_entry_range;
        extend_point_.y = core::maxf(tower.y, ground_at(extend_point_)) + tuning.siege_entry_height + 30.0f;
        extend_timer_ = tuning.siege_extend;
        d.flight = steer_through(self, extend_point_, Vec3::zero(), steering, ground);
        d.flight.flap = 1.0f;
        return d;
    }

    // The run in: straight at the tower with the terrain floor lowered, so
    // avoidance does not lift the nose off the target; a weave across the
    // line that fades out in the last stretch, so the shots still go where
    // the nose is; fireballs inside range, the flame close in.
    AutopilotTuning run_in = steering;
    run_in.min_clearance = tuning.siege_floor * 0.6f;
    const Vec3 across = core::normalize_or(core::cross(horizontal(to), Vec3::up()), Vec3::right());
    const float weave = tuning.siege_jink * core::saturate((range - 250.0f) / 250.0f) *
                        std::sin(state_time_ * tuning.siege_jink_rate);
    d.flight = steer_through(self, tower + Vec3{0.0f, 4.0f, 0.0f} + across * weave, Vec3::zero(),
                             run_in, ground);
    d.flight.flap = self.airspeed < 40.0f ? 1.0f : 0.0f;
    // Slow the last stretch a little: more time inside the flame.
    d.flight.brake = range < tuning.siege_breath_range && self.airspeed > 40.0f ? 0.5f : 0.0f;
    // Keep approach speed while lining up. With boost actually mapped to
    // thrust, boosting here rushed past the guard before the nose settled.
    const float off_axis = std::acos(core::clampf(
        core::dot(self.forward(), core::normalize_or(to, self.forward())), -1.0f, 1.0f));
    const bool locked = world.locked_slot == target_;
    d.fire = range < tuning.siege_fire_range &&
             (off_axis < core::radians(tuning.siege_cone_deg) || locked);
    d.breath = range < tuning.siege_breath_range && off_axis < core::radians(20.0f);
    siege_best_off_axis_deg = core::minf(siege_best_off_axis_deg, core::degrees(off_axis));
    siege_last_range = range;
    if (d.fire) ++siege_shots;
    return d;
}

// Down onto the cache: a glide path whose height is a fixed share of the
// distance out, the brake bleeding speed off in the last stretch, and a flare
// over the ring. Overshooting on the ground is fine -- the walk brings it back;
// overshooting in the air goes round.
DemoDecision DemoPilot::land(const FlightState& self, const DemoWorld& world) {
    DemoDecision d;
    const Vec3 to = world.cache - self.position;
    const float out = core::length(horizontal(to));
    const Vec3 ahead = core::normalize_or(horizontal(self.forward()), Vec3::forward());
    if (core::dot(ahead, horizontal(to)) < 0.0f && out > tuning.land_abort) {
        go_round_timer_ = tuning.go_round_time;
        state_ = DemoState::Cruise;
        return cruise(self, world, world.waypoint, false);
    }
    const float height = core::clampf(out * tuning.land_slope, 2.0f, 160.0f);
    AutopilotTuning glide = steering;
    // The terrain floor comes down with the glide path, or avoidance holds
    // the dragon at 90 m over the cache it is trying to land on.
    glide.min_clearance = core::clampf(height * 0.5f, 0.0f, steering.min_clearance);
    glide.cruise_speed = core::minf(steering.cruise_speed, 42.0f);
    d.flight = steer_through(self, world.cache + Vec3{0.0f, height, 0.0f}, Vec3::zero(), glide,
                             ground_ahead(self, world));
    if (out < tuning.land_brake_range) {
        d.flight.flap = self.airspeed < tuning.land_speed - 4.0f && out > 80.0f ? 1.0f : 0.0f;
        d.flight.brake = core::saturate((self.airspeed - tuning.land_speed) / 10.0f);
        d.flight.tuck = 0.0f;
    }
    if (out < 70.0f) {
        // The flare: full brake and the nose a little up.
        d.flight.brake = 1.0f;
        d.flight.pitch = core::maxf(d.flight.pitch, 0.15f);
        d.flight.flap = 0.0f;
    }
    return d;
}

// A low pass through the herd: a landing's glide path that never brakes,
// levelling at the swoop height over the lead of the animal, the flame on as
// it comes into reach. Past it, the pass is over and the hunt rests.
DemoDecision DemoPilot::hunt(const FlightState& self, const DemoWorld& world) {
    if (!world.has_prey) return cruise(self, world, world.waypoint, false);
    const float out = horizontal_distance(self.position, world.prey);
    const float lead = core::clampf(out / core::maxf(self.airspeed, 10.0f), 0.0f, 3.0f);
    const Vec3 mark = world.prey + world.prey_velocity * lead;
    const Vec3 ahead = core::normalize_or(horizontal(self.forward()), Vec3::forward());
    if (core::dot(ahead, horizontal(mark - self.position)) < 0.0f && out < 60.0f) {
        hunt_rest_timer_ = tuning.hunt_rest * 0.25f;  // a quick look round, then again
        decide_timer_ = 0.0f;
    }
    const float height = core::clampf(out * tuning.land_slope, tuning.hunt_height, 160.0f);
    AutopilotTuning glide = steering;
    glide.min_clearance = core::clampf(height * 0.5f, tuning.hunt_floor, steering.min_clearance);
    glide.cruise_speed = core::minf(steering.cruise_speed, 38.0f);
    DemoDecision d;
    d.flight = steer_through(self, mark + Vec3{0.0f, height, 0.0f}, Vec3::zero(), glide,
                             ground_ahead(self, world));
    // Energy and the floor: beat to hold the pass's speed, and pull out if
    // the ground is coming up -- a low pass is only worth it airborne.
    if (self.airspeed < tuning.hunt_speed) d.flight.flap = 1.0f;
    d.flight.brake = 0.0f;
    d.flight.tuck = 0.0f;
    if (self.ground_clearance < tuning.hunt_floor) d.flight.pitch = core::maxf(d.flight.pitch, 0.35f);
    if (out < 140.0f) {
        d.breath = true;
        d.melee = out < 25.0f;
    }
    return d;
}

// On the ground: turn to face the cache, walk into the ring, stand.
DemoDecision DemoPilot::walk(const FlightState& self, const DemoWorld& world) {
    DemoDecision d;
    if (state_ == DemoState::Collect || !world.has_cache) return d;
    const Vec3 to = core::normalize_or(horizontal(world.cache - self.position), Vec3::forward());
    const Vec3 forward = core::normalize_or(horizontal(self.forward()), Vec3::forward());
    const Vec3 right = core::normalize_or(core::cross(forward, Vec3::up()), Vec3::right());
    const float error = std::atan2(core::dot(to, right), core::dot(to, forward));
    d.flight.walk_turn = core::clampf(error * 2.0f, -1.0f, 1.0f);
    const float facing = core::dot(to, forward);
    const float out = horizontal_distance(self.position, world.cache);
    d.flight.walk = facing > 0.85f ? 1.0f : (facing > 0.0f ? 0.3f : 0.0f);
    d.flight.walk = core::minf(d.flight.walk, out / 8.0f);
    return d;
}

}  // namespace game
