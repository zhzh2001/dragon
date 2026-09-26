#include "game/breath.h"

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include <fstream>
#include <iomanip>
#include <string>

#include "core/log.h"
#include "game/element.h"

namespace game {
namespace {

// Name -> member, the same table-driven shape the flight and rig profiles use.
// Vec3 colours are three fields each rather than one parsed triple, so the file
// stays one float per line and the parser stays four lines long.
struct Field {
    const char* name;
    float BreathProfile::*member;
};

struct ScaleField {
    const char* name;
    float BreathScales::*member;
};

const ScaleField SCALE_FIELDS[] = {
    {"range_scale", &BreathScales::range},
    {"angle_scale", &BreathScales::angle},
    {"damage_scale", &BreathScales::damage},
    {"drain_scale", &BreathScales::drain},
};

// Colours addressed component-wise.
struct ColourField {
    const char* name;
    core::Vec3 BreathProfile::*member;
    int component;
};

const ColourField COLOUR_FIELDS[] = {
    {"hot_r", &BreathProfile::hot, 0},   {"hot_g", &BreathProfile::hot, 1},
    {"hot_b", &BreathProfile::hot, 2},   {"cool_r", &BreathProfile::cool, 0},
    {"cool_g", &BreathProfile::cool, 1}, {"cool_b", &BreathProfile::cool, 2},
};

const Field SCALAR_FIELDS[] = {
    {"buoyancy", &BreathProfile::buoyancy},   {"spread", &BreathProfile::spread},
    {"size_start", &BreathProfile::size_start}, {"size_end", &BreathProfile::size_end},
    {"life", &BreathProfile::life},           {"rate", &BreathProfile::rate},
    {"drag", &BreathProfile::drag},           {"brightness", &BreathProfile::brightness},
};

float& component(core::Vec3& v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }

}  // namespace

bool save_breath_profile(const BreathProfile& profile, const char* path) {
    std::ofstream output(path);
    if (!output) {
        LOG_ERROR("could not write breath profile '%s'", path);
        return false;
    }
    output << "# breath profile: what this species' breath does and looks like.\n";
    output << "# Scales multiply the Combat panel's master dials; colours are\n";
    output << "# additive and want to stay mid-value (the tonemap whitens bright).\n";
    output << std::setprecision(6);
    output << "element " << element_name(profile.element) << '\n';
    for (const ScaleField& field : SCALE_FIELDS) {
        output << field.name << ' ' << profile.scales.*field.member << '\n';
    }
    for (const ColourField& field : COLOUR_FIELDS) {
        core::Vec3 value = profile.*field.member;
        output << field.name << ' ' << component(value, field.component) << '\n';
    }
    for (const Field& field : SCALAR_FIELDS) {
        output << field.name << ' ' << profile.*field.member << '\n';
    }
    LOG_INFO("wrote breath profile '%s'", path);
    return true;
}

bool load_breath_profile(BreathProfile& profile, const char* path) {
    size_t size = 0;
    void* data = SDL_LoadFile(path, &size);
    if (!data) return false;  // Absent is the normal case; the defaults stand.
    std::string text(static_cast<char*>(data), size);
    SDL_free(data);

    int applied = 0;
    size_t cursor = 0;
    while (cursor < text.size()) {
        size_t end = text.find('\n', cursor);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(cursor, end - cursor);
        cursor = end + 1;

        if (line.empty() || line[0] == '#') continue;
        const size_t space = line.find(' ');
        if (space == std::string::npos) continue;
        const std::string key = line.substr(0, space);
        if (key == "element") {
            // The one word-valued key.
            std::string word = line.substr(space + 1);
            while (!word.empty() && (word.back() == '\r' || word.back() == ' ')) word.pop_back();
            profile.element = element_from_name(word.c_str(), profile.element);
            ++applied;
            continue;
        }
        const float value = float(SDL_atof(line.c_str() + space + 1));

        bool matched = false;
        for (const ScaleField& field : SCALE_FIELDS) {
            if (key == field.name) {
                profile.scales.*field.member = value;
                matched = true;
                break;
            }
        }
        if (!matched) {
            for (const ColourField& field : COLOUR_FIELDS) {
                if (key == field.name) {
                    component(profile.*field.member, field.component) = value;
                    matched = true;
                    break;
                }
            }
        }
        if (!matched) {
            for (const Field& field : SCALAR_FIELDS) {
                if (key == field.name) {
                    profile.*field.member = value;
                    matched = true;
                    break;
                }
            }
        }
        if (matched) ++applied;
    }
    LOG_INFO("loaded breath profile '%s' (%d field(s))", path, applied);
    return applied > 0;
}

}  // namespace game
