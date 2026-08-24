#include "game/course.h"

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include <cstdio>

#include "core/log.h"
#include "game/terrain.h"

using core::Quat;
using core::Vec3;

namespace game {

RingCrossing test_ring(const Ring& ring, Vec3 from, Vec3 to) {
    RingCrossing result;

    const Vec3 normal = ring.normal();
    const float before = core::dot(from - ring.position, normal);
    const float after = core::dot(to - ring.position, normal);

    // Only a crossing from behind to in front counts, so flying back through a
    // checkpoint does not re-trigger it.
    if (before >= 0.0f || after < 0.0f) return result;

    const float denominator = before - after;
    if (std::fabs(denominator) < 1e-6f) return result;
    const float t = core::clampf(before / denominator, 0.0f, 1.0f);
    const Vec3 hit = core::lerp(from, to, t);

    // Distance from the ring's axis, measured in the ring's plane.
    const float offset = core::length(core::reject(hit - ring.position, normal));
    result.crossed_plane = true;
    if (offset <= ring.radius) {
        result.passed = true;
    } else {
        result.miss_distance = offset - ring.radius;
    }
    return result;
}

float Course::path_length() const {
    if (rings.size() < 2) return 0.0f;
    float total = 0.0f;
    for (size_t i = 1; i < rings.size(); ++i) {
        total += core::distance(rings[i - 1].position, rings[i].position);
    }
    if (loop) total += core::distance(rings.back().position, rings.front().position);
    return total;
}

namespace {

// Orients a ring to face along the direction of travel, so you fly through it
// square rather than at a slant.
Ring make_ring(Vec3 position, Vec3 travel_direction, float radius) {
    Ring ring;
    ring.position = position;
    ring.orientation = core::look_rotation(travel_direction, Vec3::up());
    ring.radius = radius;
    return ring;
}

// Places a ring above the terrain, never inside it.
Vec3 place_above_terrain(const Terrain& terrain, float x, float z, float height,
                         float min_clearance) {
    const float ground = terrain.height_at(x, z);
    return Vec3{x, ground + core::maxf(height, min_clearance), z};
}

// Sustainable climb gradient, taken from the flight model rather than guessed:
// flapping adds about 5 m/s of specific energy at roughly 45 m/s forward, so
// about 0.11 rise per unit of horizontal travel. Courses are generated below
// this, because turning costs energy too.
constexpr float FEASIBLE_CLIMB_GRADIENT = 0.075f;

// Caps the rise between consecutive rings to something a dragon can fly.
//
// Worth doing even where a generator looks reasonable: terrain rising under the
// path can push a ring up far faster than the path advances, and a course asking
// for more climb than the dragon can produce is not difficult, it is impossible.
// Terrain clearance still wins over the cap -- a ring buried in a mountain is
// worse than a leg that needs a moment of extra effort.
// Raises rings until the straight line between consecutive rings clears the
// terrain.
//
// Ring positions being individually clear is not enough: the player flies the
// chord between them, and a path that curves around a mountain produces chords
// that pass straight through it. This is what actually made the first Summit
// Climb unflyable -- the autopilot dove toward a ring across the valley and hit
// the mountainside on the way.
//
// The near end of each leg is already settled, so the whole correction goes to
// the far end, scaled by how early along the leg the obstruction appears.
void clear_line_of_flight(std::vector<Vec3>& points, const Terrain& terrain, float clearance) {
    constexpr int SAMPLES = 24;
    for (size_t i = 1; i < points.size(); ++i) {
        float lift = 0.0f;
        for (int step = 1; step < SAMPLES; ++step) {
            const float t = float(step) / float(SAMPLES);
            const Vec3 sample = core::lerp(points[i - 1], points[i], t);
            const float ground = terrain.height_at(sample.x, sample.z) + clearance;
            if (sample.y < ground) lift = core::maxf(lift, (ground - sample.y) / t);
        }
        points[i].y += lift;
    }
}

void limit_climb(std::vector<Vec3>& points, const Terrain& terrain, float min_clearance) {
    for (size_t i = 1; i < points.size(); ++i) {
        const float horizontal = core::length(
            Vec3{points[i].x - points[i - 1].x, 0.0f, points[i].z - points[i - 1].z});
        const float capped = points[i - 1].y + horizontal * FEASIBLE_CLIMB_GRADIENT;
        const float floor_y = terrain.height_at(points[i].x, points[i].z) + min_clearance;
        points[i].y = core::maxf(core::minf(points[i].y, capped), floor_y);
    }
}

}  // namespace

Course make_valley_run(const Terrain& terrain, float half_extent) {
    Course course;
    course.name = "Valley Run";

    // Straight down the valley corridor with a rolling altitude, so the line is
    // about reading the terrain rather than about turning.
    constexpr int COUNT = 12;
    const float span = half_extent * 1.7f;
    const float start_z = -span * 0.5f;
    const float step = span / float(COUNT - 1);

    std::vector<Vec3> points;
    for (int i = 0; i < COUNT; ++i) {
        const float z = start_z + step * float(i);
        const float x = terrain.valley_center_x(z);
        // Every checkpoint must sit above the autopilot's terrain-avoidance
        // floor (AutopilotTuning::min_clearance, 90 m). Below it the controller
        // is asked to descend to a ring and climb away from the ground at the
        // same time, and the conflict is unstable -- it flew fine with jittered
        // frames and touched down with steady ones, which is the signature of a
        // fight rather than a bug.
        const float height = 150.0f + std::sin(float(i) * 0.9f) * 50.0f;
        points.push_back(place_above_terrain(terrain, x, z, height, 100.0f));
    }

    limit_climb(points, terrain, 100.0f);
    clear_line_of_flight(points, terrain, 100.0f);

    for (size_t i = 0; i < points.size(); ++i) {
        // Face each ring along the path through it: toward the next point, or
        // continuing the previous direction at the end.
        Vec3 direction = i + 1 < points.size() ? points[i + 1] - points[i]
                                               : points[i] - points[i - 1];
        course.rings.push_back(make_ring(points[i], direction, 34.0f));
    }
    return course;
}

Course make_canyon_weave(const Terrain& terrain, float half_extent) {
    Course course;
    course.name = "Canyon Weave";

    // Alternating side to side, so it demands real banking and punishes carrying
    // too much speed into a turn.
    //
    // The lateral offset is the difficulty dial here, and it was set too high.
    // At 270 m over 308 m of spacing each leg ran 41 degrees off axis, so every
    // checkpoint needed an 82 degree direction change while lining up on the
    // ring's axis -- past what a bank-limited turn can do cleanly, so the
    // approach became a hunt. 210 m over longer legs keeps it demanding and
    // flyable.
    constexpr int COUNT = 12;
    const float span = half_extent * 1.6f;
    const float start_z = -span * 0.5f;
    const float step = span / float(COUNT - 1);

    std::vector<Vec3> points;
    for (int i = 0; i < COUNT; ++i) {
        const float z = start_z + step * float(i);
        const float side = (i % 2 == 0) ? 1.0f : -1.0f;
        const float x = terrain.valley_center_x(z) + side * 210.0f;
        // This is the lowest and tightest course, and hard turns cost about
        // 4.9 m/s of energy each, so altitude drains across a run. 70 m left
        // only 21 m of clearance at the low point between rings; 170 m leaves
        // room to lose some and recover.
        points.push_back(place_above_terrain(terrain, x, z, 170.0f, 110.0f));
    }

    limit_climb(points, terrain, 110.0f);
    clear_line_of_flight(points, terrain, 110.0f);

    for (size_t i = 0; i < points.size(); ++i) {
        Vec3 direction =
            i + 1 < points.size() ? points[i + 1] - points[i] : points[i] - points[i - 1];
        course.rings.push_back(make_ring(points[i], direction, 30.0f));
    }
    return course;
}

Course make_summit_climb(const Terrain& terrain, float half_extent) {
    Course course;
    course.name = "Summit Climb";

    // A long climb to ridge height, flown *along* the valley rather than around
    // a peak.
    //
    // The first two attempts spiralled a mountain and were both unflyable. The
    // second failure was instructive: rings on a mountainside force steep climbs,
    // and worse, the chord from a ring on one flank to a ring across the valley
    // passes straight through the mountain. Keeping the path over the valley
    // floor makes both constraints satisfiable at once -- the ground under the
    // course stays low, so clearance is cheap and altitude can be spent on the
    // climb itself.
    //
    // It still cannot be flown without flapping, which is the point.
    constexpr int COUNT = 13;
    const float span = half_extent * 1.55f;
    const float start_z = -span * 0.5f;
    const float step = span / float(COUNT - 1);

    std::vector<Vec3> points;
    for (int i = 0; i < COUNT; ++i) {
        const float t = float(i) / float(COUNT - 1);
        const float z = start_z + step * float(i);
        // A gentle weave, so the climb is not a straight line.
        const float x = terrain.valley_center_x(z) + std::sin(t * core::TWO_PI * 1.5f) * 260.0f;
        // 80 m to 620 m above the valley floor: about 0.07 average gradient over
        // the whole path, inside what the dragon can sustain.
        points.push_back(place_above_terrain(terrain, x, z, 110.0f + t * 540.0f, 100.0f));
    }

    limit_climb(points, terrain, 100.0f);
    clear_line_of_flight(points, terrain, 100.0f);

    for (size_t i = 0; i < points.size(); ++i) {
        Vec3 direction =
            i + 1 < points.size() ? points[i + 1] - points[i] : points[i] - points[i - 1];
        course.rings.push_back(make_ring(points[i], direction, 36.0f));
    }
    return course;
}

// ---------------------------------------------------------------- persistence

bool save_course(const Course& course, const char* path) {
    std::string text = "# dragon course\n";
    text += "name " + course.name + "\n";
    text += course.loop ? "loop 1\n" : "loop 0\n";

    char line[256];
    for (const Ring& ring : course.rings) {
        std::snprintf(line, sizeof(line), "ring %.3f %.3f %.3f %.6f %.6f %.6f %.6f %.3f\n",
                      ring.position.x, ring.position.y, ring.position.z, ring.orientation.x,
                      ring.orientation.y, ring.orientation.z, ring.orientation.w, ring.radius);
        text += line;
    }
    if (!SDL_SaveFile(path, text.data(), text.size())) {
        LOG_ERROR("could not write course '%s': %s", path, SDL_GetError());
        return false;
    }
    LOG_INFO("saved course '%s' (%zu rings) -> %s", course.name.c_str(), course.rings.size(),
             path);
    return true;
}

bool load_course(Course& course, const char* path) {
    size_t size = 0;
    void* data = SDL_LoadFile(path, &size);
    if (!data) return false;
    std::string text(static_cast<char*>(data), size);
    SDL_free(data);

    Course loaded;
    size_t cursor = 0;
    while (cursor < text.size()) {
        size_t end = text.find('\n', cursor);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(cursor, end - cursor);
        cursor = end + 1;
        if (line.empty() || line[0] == '#') continue;

        if (line.rfind("name ", 0) == 0) {
            loaded.name = line.substr(5);
        } else if (line.rfind("loop ", 0) == 0) {
            loaded.loop = SDL_atoi(line.c_str() + 5) != 0;
        } else if (line.rfind("ring ", 0) == 0) {
            Ring ring;
            float v[8] = {};
            if (SDL_sscanf(line.c_str() + 5, "%f %f %f %f %f %f %f %f", &v[0], &v[1], &v[2],
                           &v[3], &v[4], &v[5], &v[6], &v[7]) == 8) {
                ring.position = Vec3{v[0], v[1], v[2]};
                ring.orientation = core::normalize(Quat{v[3], v[4], v[5], v[6]});
                ring.radius = v[7];
                loaded.rings.push_back(ring);
            }
        }
    }
    if (loaded.rings.empty()) return false;
    course = std::move(loaded);
    LOG_INFO("loaded course '%s' (%zu rings)", course.name.c_str(), course.rings.size());
    return true;
}

bool BestTimes::load(const char* path) {
    size_t size = 0;
    void* data = SDL_LoadFile(path, &size);
    if (!data) return false;
    std::string text(static_cast<char*>(data), size);
    SDL_free(data);

    entries_.clear();
    size_t cursor = 0;
    while (cursor < text.size()) {
        size_t end = text.find('\n', cursor);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(cursor, end - cursor);
        cursor = end + 1;
        if (line.empty() || line[0] == '#') continue;

        // `seconds<tab>course name` -- time first, so a course name containing
        // spaces needs no quoting.
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        Entry entry;
        entry.seconds = float(SDL_atof(line.substr(0, tab).c_str()));
        entry.course = line.substr(tab + 1);
        if (entry.seconds > 0.0f && !entry.course.empty()) entries_.push_back(entry);
    }
    LOG_INFO("loaded %zu best times", entries_.size());
    return true;
}

bool BestTimes::save(const char* path) const {
    std::string text = "# dragon best times: seconds<tab>course\n";
    char line[64];
    for (const Entry& entry : entries_) {
        std::snprintf(line, sizeof(line), "%.3f\t", entry.seconds);
        text += line;
        text += entry.course;
        text += '\n';
    }
    if (!SDL_SaveFile(path, text.data(), text.size())) {
        LOG_ERROR("could not write best times '%s': %s", path, SDL_GetError());
        return false;
    }
    return true;
}

bool BestTimes::submit(const std::string& course, float seconds) {
    if (seconds <= 0.0f) return false;
    for (Entry& entry : entries_) {
        if (entry.course == course) {
            if (seconds < entry.seconds) {
                entry.seconds = seconds;
                return true;
            }
            return false;
        }
    }
    entries_.push_back({course, seconds});
    return true;  // first time on this course is always a record
}

float BestTimes::best(const std::string& course) const {
    for (const Entry& entry : entries_) {
        if (entry.course == course) return entry.seconds;
    }
    return 0.0f;
}

}  // namespace game
