#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/math.h"

namespace game {

class Terrain;

// One checkpoint. `orientation`'s forward (-Z) is the direction you fly through
// it, so a ring's transform describes both where it is and which way it faces.
struct Ring {
    core::Vec3 position = core::Vec3::zero();
    core::Quat orientation = core::Quat::identity();
    // Generous by design. A checkpoint you can miss by centimetres is a
    // frustration, not a challenge -- the challenge is the line between rings.
    float radius = 30.0f;

    core::Vec3 normal() const { return core::quat_forward(orientation); }
};

// Result of testing one frame of movement against a ring.
struct RingCrossing {
    bool passed = false;       // through the ring, correct direction
    bool crossed_plane = false;// through the plane but outside the radius
    float miss_distance = 0.0f;// how far outside the radius, in metres
};

// Tests a segment of travel against a ring.
//
// Segment based rather than point based on purpose: at 100 m/s a frame covers
// 1.7 m, and a point-in-volume test would simply miss thin checkpoints. This
// finds where the path crosses the ring's plane and measures the radius there.
RingCrossing test_ring(const Ring& ring, core::Vec3 from, core::Vec3 to);

struct Course {
    std::string name;
    std::vector<Ring> rings;
    // Looping courses re-enter ring 0 after the last one, for lap times.
    bool loop = false;

    // Straight-line length through every ring in order. A lower bound on
    // distance flown, and the basis for a par time.
    float path_length() const;
};

// Procedurally laid out courses, so there is something to fly immediately
// without an authoring session. All three follow the valley the terrain
// generated, so they suit whatever seed is in use.
Course make_valley_run(const Terrain& terrain, float half_extent);
Course make_canyon_weave(const Terrain& terrain, float half_extent);
Course make_summit_climb(const Terrain& terrain, float half_extent);

// Flat text, one ring per line: `ring x y z qx qy qz qw radius`. Same rationale
// as the flight tuning file -- no dependency, diffable, hand-editable.
bool save_course(const Course& course, const char* path);
bool load_course(Course& course, const char* path);

// Best times, keyed by course name. Kept separate from the course itself so
// editing a course does not silently invalidate the file that holds records.
class BestTimes {
public:
    bool load(const char* path);
    bool save(const char* path) const;

    // Returns true if this is a new record.
    bool submit(const std::string& course, float seconds);
    // 0 means no record.
    float best(const std::string& course) const;

private:
    struct Entry {
        std::string course;
        float seconds = 0.0f;
    };
    std::vector<Entry> entries_;
};

}  // namespace game
