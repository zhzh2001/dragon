#include "game/wilds.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "core/math.h"

namespace game {

float Wilds::xp_needed() const {
    return tuning.xp_first_level * std::pow(core::maxf(tuning.xp_growth, 1.0f), float(state_.level - 1));
}

void Wilds::gain(float xp, WildsEvents& events) {
    if (xp <= 0.0f || state_.level >= tuning.max_level) return;
    state_.xp += xp;
    while (state_.level < tuning.max_level && state_.xp >= xp_needed()) {
        state_.xp -= xp_needed();
        ++state_.level;
        ++events.levels_gained;
    }
    if (state_.level >= tuning.max_level) state_.xp = 0.0f;
}

WildsEvents Wilds::update(float dt, float effort, bool breathing) {
    WildsEvents events;
    if (dt <= 0.0f) return events;

    const bool was_night = night();
    state_.clock += dt / core::maxf(tuning.day_seconds, 1.0f);
    if (state_.clock >= 1.0f) {
        state_.clock -= 1.0f;
        ++state_.day;
    }
    const bool is_night = night();
    if (is_night && !was_night) events.night_began = true;
    if (!is_night && was_night) {
        events.day_began = true;
        // Awake through it: the night was lived, not slept.
        events.night_survived = true;
        ++state_.nights;
        gain(tuning.xp_night, events);
    }

    const float cost = 1.0f + tuning.effort_hunger * core::saturate(effort) + (breathing ? tuning.breath_hunger : 0.0f);
    state_.hunger = core::saturate(state_.hunger - dt * cost / core::maxf(tuning.hunger_seconds, 1.0f));
    const bool starving_now = starving();
    if (starving_now && !was_starving_) events.starving_began = true;
    was_starving_ = starving_now;
    return events;
}

float Wilds::regen_share() const {
    if (starving()) return 0.0f;
    return state_.hunger >= tuning.fed_above ? 1.0f : core::saturate(tuning.hungry_regen);
}

WildsEvents Wilds::eat(float size) {
    WildsEvents events;
    if (size <= 0.0f) return events;
    // A grazer is two fifths of a full belly.
    state_.hunger = core::saturate(state_.hunger + 0.4f * size);
    was_starving_ = starving();
    ++state_.meals;
    gain(tuning.xp_meal * size, events);
    return events;
}

WildsEvents Wilds::kill() {
    WildsEvents events;
    ++state_.kills;
    gain(tuning.xp_kill, events);
    return events;
}

bool Wilds::found(int id) const { return std::find(state_.finds.begin(), state_.finds.end(), id) != state_.finds.end(); }

bool Wilds::find(int id, WildsEvents& events) {
    if (found(id)) return false;
    state_.finds.push_back(id);
    gain(tuning.xp_find, events);
    return true;
}

void Wilds::fall() {
    state_.xp = 0.0f;
    ++state_.falls;
    if (state_.clock >= tuning.dawn) ++state_.day;
    state_.clock = tuning.dawn;
    state_.hunger = core::saturate(tuning.death_hunger);
    was_starving_ = starving();
}

float Wilds::arc() const {
    const float day_length = core::maxf(tuning.dusk - tuning.dawn, 0.05f);
    if (!night()) return core::saturate((state_.clock - tuning.dawn) / day_length);
    float into = state_.clock - tuning.dusk;
    if (into < 0.0f) into += 1.0f;
    return core::saturate(into / core::maxf(1.0f - day_length, 0.05f));
}

float Wilds::growth() const {
    const float top = float(std::max(tuning.max_level - 1, 1));
    return 4.0f * core::saturate((float(state_.level - 1) + xp_share()) / top);
}

bool Wilds::sleep(WildsEvents& events) {
    // Not in the day's middle half: a dragon sleeps from the evening on.
    const float mid = 0.5f * (tuning.dawn + tuning.dusk);
    const float quarter = 0.25f * (tuning.dusk - tuning.dawn);
    if (state_.clock > mid - quarter && state_.clock < mid + quarter) return false;
    const bool was_night = night();
    // The hours to the next dawn.
    float hours = tuning.dawn - state_.clock;
    if (hours <= 0.0f) {
        hours += 1.0f;
        ++state_.day;
    }
    state_.clock = tuning.dawn;
    state_.hunger = core::saturate(state_.hunger - 0.5f * hours * tuning.day_seconds / core::maxf(tuning.hunger_seconds, 1.0f));
    was_starving_ = starving();
    if (was_night || hours > 0.25f) {
        events.night_survived = true;
        ++state_.nights;
        gain(tuning.xp_night * 0.5f, events);  // slept through: half what a night awake is worth
    }
    events.day_began = true;
    events.saved = true;
    return true;
}

float Wilds::sun_height() const {
    // Dawn and dusk at the horizon, noon overhead, midnight below.
    const float day_length = core::maxf(tuning.dusk - tuning.dawn, 0.05f);
    if (!night()) return std::sin(core::PI * (state_.clock - tuning.dawn) / day_length);
    const float night_length = 1.0f - day_length;
    float into = state_.clock - tuning.dusk;
    if (into < 0.0f) into += 1.0f;
    return -std::sin(core::PI * into / night_length);
}

std::string Wilds::save() const {
    std::ostringstream out;
    out << "clock " << state_.clock << "\n";
    out << "day " << state_.day << "\n";
    out << "hunger " << state_.hunger << "\n";
    out << "xp " << state_.xp << "\n";
    out << "level " << state_.level << "\n";
    out << "meals " << state_.meals << "\n";
    out << "kills " << state_.kills << "\n";
    out << "nights " << state_.nights << "\n";
    out << "seed " << state_.seed << "\n";
    out << "valley " << state_.valley << "\n";
    out << "falls " << state_.falls << "\n";
    for (const int id : state_.finds) out << "find " << id << "\n";
    return out.str();
}

WildsState Wilds::load(const std::string& text) {
    WildsState state;
    std::istringstream in(text);
    std::string key;
    while (in >> key) {
        // The seed is read whole: a float holds 24 bits of it.
        if (key == "seed") {
            unsigned long seed = 0;
            if (!(in >> seed)) break;
            state.seed = uint32_t(seed);
            continue;
        }
        float value = 0.0f;
        if (!(in >> value)) break;
        if (key == "clock") state.clock = core::saturate(value);
        else if (key == "day") state.day = std::max(1, int(value));
        else if (key == "hunger") state.hunger = core::saturate(value);
        else if (key == "xp") state.xp = core::maxf(value, 0.0f);
        else if (key == "level") state.level = std::max(1, int(value));
        else if (key == "meals") state.meals = std::max(0, int(value));
        else if (key == "kills") state.kills = std::max(0, int(value));
        else if (key == "nights") state.nights = std::max(0, int(value));
        else if (key == "valley") state.valley = std::max(0, int(value));
        else if (key == "falls") state.falls = std::max(0, int(value));
        else if (key == "find") state.finds.push_back(int(value));
    }
    if (state.clock >= 1.0f) state.clock = 0.0f;
    return state;
}

}  // namespace game
