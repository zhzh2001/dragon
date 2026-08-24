#pragma once

#include <vector>

#include "game/course.h"
#include "game/flight.h"

namespace game {

enum class RunPhase {
    Ready,     // waiting to cross the first ring; the clock is stopped
    Running,   // on the clock
    Finished,  // crossed the last ring
};

// One recorded pose. Enough to redraw the dragon exactly, including its wings,
// so a ghost is visibly flying rather than sliding along a path.
struct GhostSample {
    float time = 0.0f;
    core::Vec3 position = core::Vec3::zero();
    core::Quat orientation = core::Quat::identity();
    float wing_angle = 0.0f;
    float wing_tuck = 0.0f;
    // Recorded so the ghost's neck and tail lag through its turns exactly as the
    // living dragon's do. Without it a replay banks like a rigid model.
    core::Vec3 angular_velocity = core::Vec3::zero();
};

// A completed run, replayable.
struct GhostRun {
    std::vector<GhostSample> samples;
    // Elapsed time at each ring, for split comparison.
    std::vector<float> ring_times;
    float duration = 0.0f;

    bool valid() const { return samples.size() > 1; }
    // Interpolated pose at `time`, clamped to the ends of the recording.
    GhostSample pose_at(float time) const;
};

// Time-trial run state for one course.
//
// A flying start rather than a countdown: the clock begins when you cross the
// first ring, so you choose your own entry speed and line. That makes the start
// part of the skill instead of dead time before it.
class Rally {
public:
    void set_course(Course course);
    const Course& course() const { return course_; }

    // Back to Ready. Keeps the best ghost and best time.
    void restart();

    void update(const FlightState& state, float dt);

    RunPhase phase() const { return phase_; }
    float elapsed() const { return elapsed_; }
    int next_ring_index() const { return next_ring_; }
    const Ring* next_ring() const;
    int rings_passed() const { return next_ring_; }

    // Elapsed time at each ring passed so far this run.
    const std::vector<float>& splits() const { return splits_; }
    // Split delta against the best run at the last ring, in seconds. Negative
    // means ahead. Zero if there is nothing to compare against.
    float last_split_delta() const { return last_split_delta_; }

    const GhostRun& best_ghost() const { return best_ghost_; }
    bool has_ghost() const { return best_ghost_.valid(); }
    // Ghost pose for the current moment of this run, if there is a ghost.
    bool ghost_pose(GhostSample& out) const;

    float best_time() const { return best_time_; }
    void set_best_time(float seconds) { best_time_ = seconds; }
    bool last_run_was_record() const { return last_run_was_record_; }
    float last_run_time() const { return last_run_time_; }

    // ---- one-frame feedback, for HUD and audio ----
    bool just_passed_ring() const { return just_passed_; }
    bool just_missed_ring() const { return just_missed_; }
    float last_miss_distance() const { return last_miss_distance_; }
    bool just_finished() const { return just_finished_; }

    // How often the ghost is sampled. 30 Hz is plenty: playback interpolates,
    // and a full run costs a few tens of kilobytes.
    float ghost_sample_rate = 30.0f;

private:
    void begin_run();
    void finish_run();

    Course course_;
    RunPhase phase_ = RunPhase::Ready;
    float elapsed_ = 0.0f;
    int next_ring_ = 0;
    std::vector<float> splits_;
    float last_split_delta_ = 0.0f;

    // Previous position, so ring tests can use the segment travelled rather
    // than a point sample.
    core::Vec3 previous_position_ = core::Vec3::zero();
    bool has_previous_ = false;

    GhostRun recording_;
    GhostRun best_ghost_;
    float ghost_timer_ = 0.0f;

    float best_time_ = 0.0f;
    float last_run_time_ = 0.0f;
    bool last_run_was_record_ = false;

    bool just_passed_ = false;
    bool just_missed_ = false;
    bool just_finished_ = false;
    float last_miss_distance_ = 0.0f;
};

// Formats seconds as m:ss.mmm, which is how a time trial is read.
std::string format_time(float seconds);

}  // namespace game
