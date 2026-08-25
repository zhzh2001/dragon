#pragma once

#include <vector>

#include "anim/animation.h"
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

    // ---- neck and tail dynamics ----
    //
    // The neck and tail are simulated as chains of point masses in the dragon's
    // own frame, subject to the pseudo-forces that frame implies. That is what
    // makes them trail behind a turn, swing wide under centrifugal load, whip on
    // a direction reversal and settle afterwards -- none of which a bend
    // proportional to turn rate can do, because in a steady turn that bend is
    // constant and the animal looks rigid.
    //
    // Spring pulling each segment back toward its bind pose -- the animal's
    // muscle tone. Deflection under an acceleration is roughly a/k, so at 90 a
    // 10 m/s^2 load moved the tail by a tenth of a metre and it read as rigid.
    // At 12 a hard turn still only moved the tail a quarter of a metre on a 19 m
    // dragon, which reads as rigid at chase distance. 6 makes it legible.
    float chain_stiffness = 6.0f;
    float chain_damping = 2.2f;
    // How strongly the frame's own acceleration is felt. 1 is physically
    // faithful; lower tames a very whippy tail.
    float chain_inertia = 1.0f;
    // Gravity's effect, as a fraction of g. A real tail is partly held up by
    // muscle, so full gravity looks dead.
    float chain_gravity = 0.35f;
    // Aerodynamic drag against the relative airflow, which streams the chain aft
    // at speed. Deliberately small: the airflow past a body-fixed point is the
    // full airspeed, so even a modest coefficient is a large force.
    float chain_drag = 0.05f;
    // Ceilings, so a violent attitude cannot blow the simulation up. Without
    // these a 70 rad/s tumble produces accelerations in the tens of thousands
    // and the chain leaves for good.
    float chain_max_acceleration = 400.0f;
    float chain_max_speed = 120.0f;
    // Maximum bend between adjacent segments, so the chain cannot fold through
    // itself.
    float chain_max_bend_deg = 32.0f;
    int chain_iterations = 4;

    // ---- authored base motion ----
    //
    // The rig drives what flight determines -- wings, neck, tail, leg tuck -- and
    // leaves everything else alone. An authored clip underneath supplies the
    // detail nobody wants to write procedurally: toes, jaw, small shifts of the
    // body. Without it the extremities are perfectly still, which reads as
    // uncanny even when the big motions are right.
    float base_clip_weight = 1.0f;
    float base_clip_rate = 1.0f;

    // ---- legs ----
    float leg_tuck_deg = 62.0f;  // folded in flight, extended on the ground
};

// Turns flight state into a pose. Holds the spring-chain state, so it must be
// updated once per frame per dragon and cannot be shared.
class DragonRig {
public:
    void init(const Skeleton& skeleton, const DragonJoints& joints);

    // Metres per model unit. The chain simulation mixes real-world accelerations
    // (gravity, the body's own acceleration) with joint positions, so those have
    // to be in the same units. An imported asset is routinely authored at eight
    // model units per metre, which made every force a factor of eight too weak
    // and the tail look rigid.
    void set_model_scale(float metres_per_unit);

    // Authored motion layered under the procedural pose. Not owned; must outlive
    // the rig. Null disables it.
    void set_base_clip(const AnimationClip* clip) { base_clip_ = clip; }
    bool has_base_clip() const { return base_clip_ && base_clip_->valid(); }
    void update(const game::FlightState& state, float dt);

    const Pose& pose() const { return pose_; }
    const std::vector<core::Mat4>& skinning_matrices() const { return skinning_; }
    // Joint world transforms, for debug drawing the skeleton.
    const std::vector<core::Mat4>& world_matrices() const { return world_; }

    RigTuning tuning;

private:
    // A chain simulated as point masses in the dragon's own frame.
    struct ChainDynamics {
        std::vector<core::Vec3> position;  // body-local, simulated
        std::vector<core::Vec3> velocity;
        std::vector<core::Vec3> rest;      // body-local bind positions
        std::vector<float> segment;        // rest length to the previous point
        bool initialized = false;
    };

    // Applies a rotation about a body-space axis to one joint, composed with
    // that joint's bind rotation rather than replacing it.
    //
    // Both halves matter for an imported rig. Replacing the bind rotation
    // destroys the rest pose, and a real rig's bones point along their own axes
    // -- rotating about the joint's local Z means something different for every
    // bone. Expressing the axis in body terms makes the animation independent of
    // how the skeleton was authored.
    // `onto_current` composes with whatever is already in the pose -- the
    // authored clip -- instead of starting from the bind rotation.
    void rotate_joint(int joint, core::Vec3 body_axis, float angle, bool onto_current = false);
    void rotate_joint(int joint, core::Vec3 axis_a, float angle_a, core::Vec3 axis_b,
                      float angle_b);

    void drive_wings(const game::FlightState& state);
    void setup_chain(ChainDynamics& sim, const std::vector<int>& chain) const;
    // Integrates the chain, then turns the simulated shape back into joint
    // rotations.
    void drive_chain(ChainDynamics& sim, const std::vector<int>& chain,
                     const game::FlightState& state, core::Vec3 frame_acceleration,
                     core::Vec3 angular_acceleration, float dt);
    void drive_legs(const game::FlightState& state, float dt);
    // Applies a body-space rotation to one joint, composed with its bind
    // rotation. `parent_extra` is the rotation already applied to its ancestors,
    // needed so the delta lands in the right frame.
    void rotate_joint_quat(int joint, const core::Quat& delta, const core::Quat& parent_extra);

    const Skeleton* skeleton_ = nullptr;
    DragonJoints joints_;
    // World bind rotation of each joint's parent, inverted. Converts a body-space
    // axis into the space a local rotation is expressed in.
    std::vector<core::Quat> parent_bind_inverse_;
    Pose pose_;
    std::vector<core::Mat4> world_;
    std::vector<core::Mat4> skinning_;

    ChainDynamics tail_sim_, neck_sim_;
    float model_scale_ = 1.0f;
    const AnimationClip* base_clip_ = nullptr;
    float clip_time_ = 0.0f;
    // Previous frame's motion, for deriving the accelerations the chains feel.
    core::Vec3 previous_velocity_ = core::Vec3::zero();
    core::Vec3 previous_angular_velocity_ = core::Vec3::zero();
    bool have_previous_ = false;
    float leg_extend_ = 0.0f;
};

}  // namespace anim
