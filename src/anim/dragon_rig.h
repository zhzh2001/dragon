#pragma once

#include <vector>

#include "anim/skeleton.h"
#include "anim/skinned_mesh.h"
#include "game/flight.h"

namespace anim {

// Proportions of the generated dragon, in metres.
struct DragonShape {
    // First pass had a 23 m wingspan on a 7 m body, which read as an albatross
    // rather than a dragon: the animal disappeared between its own wings.
    float body_length = 7.5f;
    float body_radius = 1.55f;
    float neck_length = 5.2f;
    int neck_joints = 5;
    float tail_length = 8.5f;
    int tail_joints = 7;

    float wing_upper = 3.5f;  // shoulder to elbow
    float wing_fore = 3.2f;   // elbow to wrist
    float wing_hand = 2.7f;   // wrist to tip
    float wing_chord = 5.2f;  // membrane depth at the root

    float leg_upper = 1.5f;
    float leg_lower = 1.6f;
};

// Named joint indices, resolved once so the animation code never does string
// lookups per frame.
struct DragonJoints {
    int root = NO_PARENT;
    int chest = NO_PARENT;
    std::vector<int> neck;  // base to head
    int head = NO_PARENT;
    std::vector<int> tail;  // base to tip

    // Side 0 is right (+X).
    //
    // Wings are a shared root chain plus any number of finger chains, not a
    // fixed shoulder/elbow/wrist. A real membrane wing has three or four fingers
    // sharing one arm, and a generated placeholder has one -- the same structure
    // describes both, so the animation code does not care which it is driving.
    std::vector<int> wing_root[2];                 // shoulder outward
    std::vector<std::vector<int>> wing_fingers[2];  // each finger, base to tip
    std::vector<int> leg[2];                        // hip outward

    bool valid() const {
        return root != NO_PARENT && !wing_root[0].empty() && !wing_root[1].empty();
    }
};

// Identifies the named chains in an arbitrary skeleton by matching bone names.
//
// This is what lets an imported rig be driven by the same procedural animation as
// the generated one. Matching is case-insensitive substring matching against
// several spellings, because riggers name things in whatever language and
// convention they please -- this model, for instance, uses German for its legs.
DragonJoints map_dragon_joints(const Skeleton& skeleton);

// Builds the skeleton and a matching skinned mesh.
//
// Generated rather than loaded so the animation system can be exercised without
// depending on an asset. The loader path exists for a real model later; the rig
// below is what actually gets driven, and it does not care where the skeleton
// came from.
void build_dragon(const DragonShape& shape, Skeleton& out_skeleton, DragonJoints& out_joints,
                  SkinnedMeshData& out_mesh);

// How the rig responds to flight. All live-tunable.
struct RigTuning {
    // ---- wings ----
    float flap_shoulder_deg = 52.0f;
    // Each segment lags the one inboard of it, which is what gives a wingbeat
    // its whip instead of looking like a hinged plank.
    float wing_phase_lag = 0.16f;
    // How much of the flap each successive outboard segment keeps. Below 1 the
    // shoulder does most of the work, which is what a wing actually does.
    float outboard_decay = 0.68f;
    // Folding: how far the wing sweeps back and closes when tucked.
    float tuck_sweep_deg = 78.0f;
    float tuck_fold_deg = 62.0f;
    float brake_flare_deg = 30.0f;

    // ---- neck and tail spring chains ----
    // The signature detail: neck and tail lag the body's rotation, so a turn
    // reads as the whole animal committing rather than a rigid model banking.
    float chain_stiffness = 42.0f;
    float chain_damping = 9.0f;
    // Radians of deflection per rad/s of body angular velocity.
    float tail_response = 0.55f;
    float neck_response = 0.30f;
    // Later joints deflect more, accumulating down the chain.
    float chain_falloff = 1.25f;
    float chain_limit_deg = 26.0f;

    // ---- legs ----
    float leg_tuck_deg = 62.0f;  // folded in flight, extended on the ground
};

// Turns flight state into a pose. Holds the spring-chain state, so it must be
// updated once per frame per dragon and cannot be shared.
class DragonRig {
public:
    void init(const Skeleton& skeleton, const DragonJoints& joints);
    void update(const game::FlightState& state, float dt);

    const Pose& pose() const { return pose_; }
    const std::vector<core::Mat4>& skinning_matrices() const { return skinning_; }
    // Joint world transforms, for debug drawing the skeleton.
    const std::vector<core::Mat4>& world_matrices() const { return world_; }

    RigTuning tuning;

private:
    // One axis of one spring joint.
    struct Spring {
        float angle = 0.0f;
        float velocity = 0.0f;
        void step(float target, float stiffness, float damping, float dt);
    };

    // Applies a rotation about a body-space axis to one joint, composed with
    // that joint's bind rotation rather than replacing it.
    //
    // Both halves matter for an imported rig. Replacing the bind rotation
    // destroys the rest pose, and a real rig's bones point along their own axes
    // -- rotating about the joint's local Z means something different for every
    // bone. Expressing the axis in body terms makes the animation independent of
    // how the skeleton was authored.
    void rotate_joint(int joint, core::Vec3 body_axis, float angle);
    void rotate_joint(int joint, core::Vec3 axis_a, float angle_a, core::Vec3 axis_b,
                      float angle_b);

    void drive_wings(const game::FlightState& state);
    void drive_chain(const std::vector<int>& chain, std::vector<Spring>& yaw,
                     std::vector<Spring>& pitch, float response, const game::FlightState& state,
                     float dt);
    void drive_legs(const game::FlightState& state, float dt);

    const Skeleton* skeleton_ = nullptr;
    DragonJoints joints_;
    // World bind rotation of each joint's parent, inverted. Converts a body-space
    // axis into the space a local rotation is expressed in.
    std::vector<core::Quat> parent_bind_inverse_;
    Pose pose_;
    std::vector<core::Mat4> world_;
    std::vector<core::Mat4> skinning_;

    std::vector<Spring> tail_yaw_, tail_pitch_;
    std::vector<Spring> neck_yaw_, neck_pitch_;
    float leg_extend_ = 0.0f;
};

}  // namespace anim
