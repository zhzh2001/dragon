#include "game/rally.h"

#include <cstdio>

#include "core/log.h"

using core::Quat;
using core::Vec3;

namespace game {

GhostSample GhostRun::pose_at(float time) const {
    if (samples.empty()) return GhostSample();
    if (samples.size() == 1 || time <= samples.front().time) return samples.front();
    if (time >= samples.back().time) return samples.back();

    // Linear scan from a proportional guess. Samples are evenly spaced in time,
    // so this lands within a sample or two and there is no need for a search.
    size_t index = size_t(core::clampf(time / core::maxf(duration, 1e-4f), 0.0f, 1.0f) *
                          float(samples.size() - 1));
    while (index + 1 < samples.size() && samples[index + 1].time < time) ++index;
    while (index > 0 && samples[index].time > time) --index;

    const size_t next = index + 1 < samples.size() ? index + 1 : index;
    const GhostSample& a = samples[index];
    const GhostSample& b = samples[next];
    const float span = b.time - a.time;
    const float t = span > 1e-6f ? core::clampf((time - a.time) / span, 0.0f, 1.0f) : 0.0f;

    GhostSample out;
    out.time = time;
    out.position = core::lerp(a.position, b.position, t);
    out.orientation = core::slerp(a.orientation, b.orientation, t);
    out.wing_angle = core::lerpf(a.wing_angle, b.wing_angle, t);
    out.wing_tuck = core::lerpf(a.wing_tuck, b.wing_tuck, t);
    out.angular_velocity = core::lerp(a.angular_velocity, b.angular_velocity, t);
    return out;
}

void Rally::set_course(Course course) {
    course_ = std::move(course);
    best_ghost_ = GhostRun();
    best_time_ = 0.0f;
    last_run_time_ = 0.0f;
    last_run_was_record_ = false;
    restart();
}

void Rally::restart() {
    phase_ = RunPhase::Ready;
    elapsed_ = 0.0f;
    next_ring_ = 0;
    splits_.clear();
    last_split_delta_ = 0.0f;
    has_previous_ = false;
    recording_ = GhostRun();
    ghost_timer_ = 0.0f;
    just_passed_ = just_missed_ = just_finished_ = false;
    last_miss_distance_ = 0.0f;
}

const Ring* Rally::next_ring() const {
    if (next_ring_ < 0 || size_t(next_ring_) >= course_.rings.size()) return nullptr;
    return &course_.rings[size_t(next_ring_)];
}

void Rally::begin_run() {
    phase_ = RunPhase::Running;
    elapsed_ = 0.0f;
    ghost_timer_ = 0.0f;
    recording_ = GhostRun();
}

void Rally::finish_run() {
    phase_ = RunPhase::Finished;
    last_run_time_ = elapsed_;
    recording_.duration = elapsed_;

    last_run_was_record_ = best_time_ <= 0.0f || elapsed_ < best_time_;
    if (last_run_was_record_) {
        best_time_ = elapsed_;
        // The ghost is always the best run, never merely the last one --
        // otherwise it stops being something to chase.
        best_ghost_ = recording_;
        LOG_INFO("'%s' new best: %s", course_.name.c_str(), format_time(elapsed_).c_str());
    } else {
        LOG_INFO("'%s' finished in %s (best %s)", course_.name.c_str(),
                 format_time(elapsed_).c_str(), format_time(best_time_).c_str());
    }
    just_finished_ = true;
}

void Rally::update(const FlightState& state, float dt) {
    just_passed_ = just_missed_ = just_finished_ = false;

    if (course_.rings.empty()) return;

    const Vec3 position = state.position;
    if (!has_previous_) {
        previous_position_ = position;
        has_previous_ = true;
        return;
    }
    const Vec3 from = previous_position_;
    previous_position_ = position;

    if (phase_ == RunPhase::Finished) return;

    if (phase_ == RunPhase::Running) {
        elapsed_ += dt;

        // Record the ghost at a fixed rate rather than per frame, so a recording
        // is frame-rate independent and playback is comparable across sessions.
        const float interval = 1.0f / core::maxf(ghost_sample_rate, 1.0f);
        ghost_timer_ += dt;
        if (recording_.samples.empty() || ghost_timer_ >= interval) {
            ghost_timer_ = 0.0f;
            GhostSample sample;
            sample.time = elapsed_;
            sample.position = position;
            sample.orientation = state.orientation;
            sample.wing_angle = state.wing_angle;
            sample.wing_tuck = state.wing_tuck;
            sample.angular_velocity = state.angular_velocity;
            recording_.samples.push_back(sample);
        }
    }

    // Only the next ring is live. Order matters, and letting a later ring
    // trigger would let a run skip the hard part.
    const Ring* target = next_ring();
    if (!target) return;

    const RingCrossing crossing = test_ring(*target, from, position);
    if (crossing.passed) {
        if (phase_ == RunPhase::Ready) begin_run();

        splits_.push_back(elapsed_);
        // Compare against the same ring on the best run.
        last_split_delta_ = 0.0f;
        if (best_ghost_.valid() && size_t(next_ring_) < best_ghost_.ring_times.size()) {
            last_split_delta_ = elapsed_ - best_ghost_.ring_times[size_t(next_ring_)];
        }
        recording_.ring_times.push_back(elapsed_);

        ++next_ring_;
        just_passed_ = true;
        LOG_INFO("ring %d/%zu at %s", next_ring_, course_.rings.size(),
                 format_time(elapsed_).c_str());

        if (size_t(next_ring_) >= course_.rings.size()) {
            if (course_.loop) {
                next_ring_ = 0;
            } else {
                finish_run();
            }
        }
    } else if (crossing.crossed_plane) {
        just_missed_ = true;
        last_miss_distance_ = crossing.miss_distance;
    }
}

bool Rally::ghost_pose(GhostSample& out) const {
    if (!best_ghost_.valid()) return false;
    // In Ready the ghost holds its own start pose, so it is visible and ready to
    // launch rather than absent until the clock starts.
    const float time = phase_ == RunPhase::Running ? elapsed_ : 0.0f;
    if (phase_ == RunPhase::Running && time > best_ghost_.duration) return false;
    out = best_ghost_.pose_at(time);
    return true;
}

std::string format_time(float seconds) {
    if (seconds <= 0.0f) return "--:--.---";
    const int minutes = int(seconds) / 60;
    const float remainder = seconds - float(minutes * 60);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d:%06.3f", minutes, remainder);
    return buffer;
}

}  // namespace game
