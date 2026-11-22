#pragma once

// The wilds: a second mode beside the hoard run (docs/DIRECTION.md, "The
// wilds"). The run is a fifteen-minute descent with an end. The wilds has
// none: one dragon lives in a valley, eats to stay alive, grows by what it
// does, finds what is there, and sleeps at a nest that keeps its progress.
// It is the frame the larger mechanics the user asked for (magic,
// followers, other species) are meant to hang on, and this file is that
// frame's first version: the rules only, no renderer and no app.
//
//   hunger   falls with time and faster with effort; prey fills it. Fed,
//            the dragon mends as it does in a fight (the regeneration
//            after a lull); hungry, slowly; starving, not at all, and it
//            weakens.
//   growth   experience from eating, killing, finding and surviving. The
//            levels run along the hoard run's growth (drake to ancient),
//            so a level is size, health, reach and the stages' abilities.
//   the day  a clock the app turns into the sun. Night is when the valley
//            is dangerous, and a night lived through is worth something.
//   finds    places that count once.
//   sleep    landed and unthreatened: the night passes and the state is
//            saved. A fall costs the progress toward the next level and
//            wakes the dragon at the valley head at the next dawn.
//
// Every number is a dial (WildsTuning), because none of them has been
// played yet.

#include <cstdint>
#include <string>
#include <vector>

namespace game {

struct WildsTuning {
    float day_seconds = 720.0f;       // one day and night, in seconds of play
    float dawn = 0.25f;               // the day's fractions: the sun rises at dawn,
    float dusk = 0.75f;               // and sets at dusk
    float hunger_seconds = 420.0f;    // from full to empty, gliding
    float effort_hunger = 1.6f;       // times that, more, at full effort (flapping, boosting)
    float breath_hunger = 0.5f;       // and more again while the breath is held
    float fed_above = 0.5f;           // fed: the fight's regeneration runs whole
    float hungry_regen = 0.25f;       // the share of it that runs below that
    float starve_per_second = 0.012f; // of full health, while empty
    float xp_first_level = 100.0f;    // experience for the second level
    float xp_growth = 1.35f;          // each level needs this many times the last
    int max_level = 20;
    float xp_meal = 12.0f;            // times the meal's size (a grazer is 1)
    float xp_kill = 40.0f;            // a rival or a tower
    float xp_find = 60.0f;            // a place found
    float xp_night = 80.0f;           // a night lived through
    float starve_floor = 0.1f;        // of full health: starving weakens to here, and does not kill
    float death_hunger = 0.5f;        // the belly a dragon wakes with after a fall
};

// What happened this step, for the app to show and sound.
struct WildsEvents {
    int levels_gained = 0;
    bool starving_began = false;
    bool night_began = false;
    bool day_began = false;
    bool night_survived = false;
    bool saved = false;  // the state changed in a way a save should keep
};

// Everything a save holds.
struct WildsState {
    float clock = 0.3f;     // the day's fraction, 0 midnight, 0.5 noon
    int day = 1;
    float hunger = 0.8f;    // 1 full, 0 empty
    float xp = 0.0f;        // toward the next level
    int level = 1;
    int meals = 0;
    int kills = 0;
    int nights = 0;
    std::vector<int> finds; // ids of places found, in the order found
    uint32_t seed = 0;      // the wilds' first valley
    int valley = 0;         // how many passes in the dragon last slept
    int falls = 0;          // times it fell
};

class Wilds {
public:
    WildsTuning tuning;

    void reset(const WildsState& state = WildsState{}) {
        state_ = state;
        was_starving_ = state_.hunger <= 0.0f;
    }
    const WildsState& state() const { return state_; }

    // One step of play. `effort` is 0 gliding to 1 flapping or boosting,
    // `breathing` whether the breath is held. Returns what happened.
    WildsEvents update(float dt, float effort, bool breathing);

    // The share of full health lost a second: zero unless starving. The
    // app takes it from the fight's health, down to the floor.
    float starve_rate() const { return starving() ? tuning.starve_per_second : 0.0f; }
    // The share of the fight's own regeneration that runs: whole when fed,
    // a part when hungry, none when starving.
    float regen_share() const;

    // A meal of `size` (a grazer is 1, a rabbit less, a great beast more).
    WildsEvents eat(float size);
    WildsEvents kill();
    // A place found. False when it was found before.
    bool find(int id, WildsEvents& events);
    bool found(int id) const;

    // A fall: the experience toward the next level is lost, the clock
    // goes to the next dawn, and the dragon wakes half fed.
    void fall();

    // Sleep: the clock goes to dawn, hunger falls by what the
    // hours slept would have cost at rest (halved: sleep is cheap), and a
    // night slept through counts as survived. False when it is not dark
    // enough to sleep (the day's middle half).
    bool sleep(WildsEvents& events);

    bool night() const { return state_.clock < tuning.dawn || state_.clock >= tuning.dusk; }
    bool starving() const { return state_.hunger <= 0.0f; }
    // The sun's height as a fraction, -1 at midnight to 1 at noon.
    float sun_height() const;
    // The share of the daylight gone, 0 at dawn to 1 at dusk; of the night
    // gone while it is night.
    float arc() const;
    // Growth on the run's scale (0 a drake to 4 an ancient), from the level:
    // the app sizes and arms the dragon by it.
    float growth() const;
    // Experience the next level needs, and the share of it held.
    float xp_needed() const;
    float xp_share() const { return state_.level >= tuning.max_level ? 1.0f : state_.xp / xp_needed(); }

    // A save as text, "key value" a line, and back. Unknown keys are
    // skipped, so an older save loads in a newer build.
    std::string save() const;
    static WildsState load(const std::string& text);
    WildsState& mutable_state() { return state_; }

private:
    void gain(float xp, WildsEvents& events);

    WildsState state_;
    bool was_starving_ = false;
};

}  // namespace game
