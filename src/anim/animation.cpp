#include "anim/animation.h"

namespace anim {

core::Quat RotationTrack::sample(float time) const {
    if (rotations.empty()) return core::Quat::identity();
    if (rotations.size() == 1 || time <= times.front()) return rotations.front();
    if (time >= times.back()) return rotations.back();

    // Binary search for the key at or before `time`. Clips here have a few
    // hundred keys per track, so a scan would be needlessly slow with 700 tracks
    // sampled every frame.
    size_t low = 0, high = times.size() - 1;
    while (high - low > 1) {
        const size_t mid = (low + high) / 2;
        if (times[mid] <= time) {
            low = mid;
        } else {
            high = mid;
        }
    }

    if (step) return rotations[low];
    const float span = times[high] - times[low];
    const float t = span > 1e-6f ? (time - times[low]) / span : 0.0f;
    return core::slerp(rotations[low], rotations[high], t);
}

void AnimationClip::sample(float time, Pose& pose) const {
    if (duration <= 0.0f) return;
    // Loop.
    float local = std::fmod(time, duration);
    if (local < 0.0f) local += duration;

    for (const RotationTrack& track : tracks) {
        if (track.joint < 0 || size_t(track.joint) >= pose.local.size()) continue;
        core::Quat& rotation = pose.local[size_t(track.joint)].rotation;
        rotation = core::normalize(rotation * track.sample(local));
    }
}

}  // namespace anim
