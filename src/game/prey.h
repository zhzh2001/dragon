#pragma once

#include <cstdint>
#include <vector>

#include "core/math.h"
#include "game/flight.h"

namespace game {

class Terrain;
struct RunHerd;

// The prey herd (DIRECTION.md row 6): grazers on the valley floor that the
// dragon eats to grow. Not a fight -- they never hit back -- but a hunt:
// they see a low dragon coming and bolt, so a meal is a swoop timed onto a
// running animal, a bite from a walk, or a flame and a pick-up.
//
// Three ways to eat, one per verb the dragon already has:
//   * SWOOP: fly the body low through one and it is snatched. The fastest,
//     and the one that needs flying.
//   * BITE: a bite (C) with one in reach takes it -- from the ground, a walk
//     up on a grazing herd.
//   * FIRE: breath or a fireball kills; the carcass stays where it fell, to
//     be swooped or walked onto. Burning a herd from altitude is safe and
//     slow; the meal still has to be picked up.
//
// Pure simulation, like the hoard run: the app draws them (the grazer prop's
// clips) and tells the run what was eaten.

struct PreyTuning {
    // Top of the panel, because they decide whether the hunt is fun: how
    // far away and how high a dragon is noticed, and how fast they run.
    // A gallop well under a dragon's cruise, or a swoop never has to be
    // flown; well over a glide, or a walking dragon catches everything.
    float run_speed = 17.0f;
    float notice_range = 240.0f;   // m, horizontal
    float notice_height = 80.0f;   // m above them: a dragon higher than this is a speck
    float bolt_range = 150.0f;     // inside this they run, not just look
    float calm_time = 7.0f;        // s out of range before they graze again
    float walk_speed = 2.2f;
    float turn_rate = 2.4f;        // rad/s
    float accel = 9.0f;            // m/s^2
    // How far a grazer strays from its herd's ground while calm.
    float herd_radius = 40.0f;
    float spacing = 6.0f;          // they keep this apart
    float health = 20.0f;
    // The grazer drawn this much larger than the asset's 4.5 m: at that
    // size a herd was, in the playtest's words, "too small to notice" from a
    // dragon's height. The body for fire and bites scales with it.
    float scale = 1.4f;
    float body_radius = 2.2f;
    float body() const { return body_radius * scale; }
    // The swoop: the dragon's body within this of one, horizontally, and no
    // more than `grab_height` above it. Both scale with the dragon's growth.
    float grab_radius = 8.0f;
    float grab_height = 9.0f;
    // What a meal is worth. Growth without hoard; health back.
    float growth = 18.0f;
    float heal = 14.0f;
    // A carcass left by fire stays this long.
    float carcass_time = 60.0f;
};

enum class PreyState : int { Graze, Walk, Alert, Flee, Carcass, Eaten };

struct Prey {
    core::Vec3 position = core::Vec3::zero();  // on the ground
    core::Vec3 heading = core::Vec3::forward();
    float speed = 0.0f;
    PreyState state = PreyState::Graze;
    float timer = 0.0f;      // time left in a graze/walk bout, or since calm
    float clip_time = 0.0f;  // animation phase, seconds into the clip
    float health = 20.0f;
    int herd = 0;
    float burnt = 0.0f;      // 0..1, how charred it shows
    float wander = 0.0f;
};

// Which clip an animal wants, by its gait.
enum class PreyClip : int { Graze, Walk, Run, Dead };
PreyClip prey_clip(const Prey& prey, const PreyTuning& tuning);

struct PreyEvents {
    int eaten = 0;          // meals taken this frame
    core::Vec3 where = core::Vec3::zero();
    bool by_swoop = false;
    int killed = 0;         // by fire, left as carcasses
};

class PreyHerds {
public:
    PreyTuning tuning;

    void reset(const std::vector<RunHerd>& herds, const Terrain& terrain, uint32_t seed);
    void clear() {
        animals_.clear();
        homes_.clear();
    }

    // Behaviour and the swoop. `dragon_size` is the growth scale (1 normal).
    PreyEvents update(float dt, const FlightState& dragon, float dragon_size, const Terrain& terrain);

    // Weapons. Each returns what it did through `events`.
    void breathe(core::Vec3 origin, core::Vec3 axis, float half_angle_rad, float range, float dps,
                 float dt, PreyEvents& events);
    void blast(core::Vec3 at, float radius, float damage, PreyEvents& events);
    // A bite or a strike from a dragon's mouth: takes one in reach whole.
    void bite(core::Vec3 mouth, core::Vec3 forward, float reach, float half_angle_rad,
              PreyEvents& events);

    const std::vector<Prey>& animals() const { return animals_; }
    int alive() const;
    // The nearest animal still worth eating (alive or a carcass), or -1.
    int nearest(core::Vec3 from, float max_range = 1e9f) const;

private:
    void eat(Prey& prey, PreyEvents& events, bool swoop);
    float unit();

    std::vector<Prey> animals_;
    std::vector<core::Vec3> homes_;
    uint32_t rng_ = 1u;
};

}  // namespace game
