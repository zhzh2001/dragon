#pragma once

#include <string>
#include <vector>

#include "anim/skeleton.h"

namespace anim {

// One animated property of one joint.
//
// Only rotation is kept. Skeletal animation is almost entirely rotational, and
// applying translation channels would import the clip's root motion -- the
// dragon walking out from under itself -- as well as risking stretched bones on
// a rig whose bind pose we have already reconstructed.
// Rotations are stored as deltas from the joint's rest rotation, applied onto
// whatever the pose already holds. Absolute rotations would only be correct if
// the skeleton's bind pose used exactly the same convention as the file's node
// hierarchy, and for a real asset that is not a safe assumption.
struct RotationTrack {
    int joint = NO_PARENT;
    std::vector<float> times;
    std::vector<core::Quat> rotations;
    bool step = false;  // step interpolation rather than linear

    core::Quat sample(float time) const;
};

// A clip of authored motion, sampled onto a pose.
struct AnimationClip {
    std::string name;
    float duration = 0.0f;
    std::vector<RotationTrack> tracks;

    bool valid() const { return duration > 0.0f && !tracks.empty(); }

    // Composes sampled rotations onto `pose`, leaving joints the clip does not
    // mention untouched -- so it layers over a bind pose rather than replacing
    // it wholesale. Call it on a pose freshly reset to bind.
    void sample(float time, Pose& pose) const;
};

}  // namespace anim
