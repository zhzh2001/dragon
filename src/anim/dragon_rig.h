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
    float tuck_sweep_deg = 88.0f;
    float tuck_fold_deg = 52.0f;
    // Tucked wings also pull down against the flanks. Sweep and fold both act
    // in the horizontal plane; without the droop the folded wing stays at glide
    // dihedral and the membrane drapes below the body -- half-folded, not a
    // stoop.
    float tuck_droop_deg = 22.0f;
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
    // Aerodynamic drag against the relative airflow. The linear term damps slow
    // motion; the quadratic term is the real physics -- drag grows with the
    // square of airspeed -- and it is what makes the tail hang at a hover,
    // stream level at cruise and pull dead straight in a dive, three postures
    // from one force law instead of one linear compromise between them.
    float chain_drag = 0.04f;
    float chain_drag_v2 = 0.010f;
    // Ceilings, so a violent attitude cannot blow the simulation up. Without
    // these a 70 rad/s tumble produces accelerations in the tens of thousands
    // and the chain leaves for good.
    float chain_max_acceleration = 400.0f;
    float chain_max_speed = 120.0f;
    // Maximum bend between adjacent segments, so the chain cannot fold through
    // itself.
    float chain_max_bend_deg = 32.0f;
    int chain_iterations = 4;

    // ---- flight response ----
    //
    // The chains above are passive -- they lag, swing and settle. These are the
    // active responses: a flying animal steers with its tail, leads a manoeuvre
    // with its head, and its wings visibly carry the load. Without them the body
    // reads as a fuselage that happens to have dynamics bolted on.
    //
    // Tail as a control surface, driven by the smoothed control positions the
    // flight model already exposes. Deflection with yaw and roll input swings it
    // toward the outside of the commanded turn; pitch input works it as an
    // elevator, tail dropping as the nose rises.
    float tail_rudder_deg = 22.0f;
    float tail_elevator_deg = 14.0f;
    // The neck leads: nose-up input curls the head up before the body follows.
    // Anticipation, the oldest animation principle there is.
    float neck_lead_deg = 10.0f;
    // And at speed the neck lowers into the wind. Full effect at
    // `streamline_speed` and above.
    float neck_streamline_deg = 8.0f;
    float streamline_speed = 60.0f;
    // Wings bow upward under load: degrees of extra dihedral per g above 1.
    // The one signal that makes a hard pull look like it costs something.
    float wing_load_flex_deg = 7.0f;
    // Asymmetric wing lean with roll input -- both wings rotate the same way
    // about the body axis, which is exactly what produces a roll.
    float wing_roll_lean_deg = 9.0f;

    // ---- authored base motion ----
    //
    // The rig drives what flight determines -- wings, neck, tail, leg tuck -- and
    // leaves everything else alone. An authored clip underneath supplies the
    // detail nobody wants to write procedurally: toes, jaw, small shifts of the
    // body. Without it the extremities are perfectly still, which reads as
    // uncanny even when the big motions are right.
    float base_clip_weight = 1.0f;
    float base_clip_rate = 1.0f;
    // How much of the clip survives hard flight. The clip is a ground idle --
    // toes curling, jaw working, small shifts of weight -- which is right in a
    // calm glide and absurd in a 100 m/s dive, where a real animal goes tense
    // and still. 0 keeps the idle at full strength always; 1 removes it entirely
    // at full intensity.
    float clip_flight_fade = 0.7f;

    // ---- head aim ----
    //
    // While attacking, the head turns toward the target. This is readability as
    // much as flourish: the fire leaves along the aim axis, and a head pointing
    // somewhere else makes the shot look like it came from nowhere, which reads
    // as the animation fighting the aim.
    float head_aim_blend = 0.85f;
    float head_aim_max_deg = 60.0f;

    // ---- legs ----
    float leg_tuck_deg = 62.0f;  // folded in flight, extended on the ground
    // The legs are pendulums. They hang from the hips and feel the same frame
    // pseudo-forces the chains do, held by a muscle spring: they swing outward
    // in a turn, trail under acceleration and float forward under braking. A
    // leg that stays rigidly perpendicular to the wings through a hard turn is
    // the single clearest tell that the body is a fuselage.
    float leg_sway_response = 1.0f;
    float leg_sway_max_deg = 26.0f;
    float leg_sway_stiffness = 16.0f;
    float leg_sway_damping = 6.0f;
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

    // Where the head should look, in world space. Cleared every frame it is not
    // set, so the head falls back to the chain simulation when not attacking.
    void set_aim_target(core::Vec3 world_point) {
        aim_target_ = world_point;
        aim_active_ = true;
    }
    void clear_aim_target() { aim_active_ = false; }

    // Where the mouth actually is, in world space -- the head joint's origin
    // after animation. For drawing anything that should issue from it.
    core::Vec3 head_position() const;

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
    // 0 calm glide .. 1 flat out: how hard the flight state is working the body.
    float flight_intensity(const game::FlightState& state) const;
    void setup_chain(ChainDynamics& sim, const std::vector<int>& chain) const;
    // Integrates the chain, then turns the simulated shape back into joint
    // rotations.
    // `steer_deg` actively deflects the chain's target shape: x pitches it about
    // the body's X axis, y swings it about Y. The spring then pulls the chain
    // toward the deflected shape, so steering composes with the passive
    // dynamics instead of overwriting them.
    void drive_chain(ChainDynamics& sim, const std::vector<int>& chain,
                     const game::FlightState& state, core::Vec3 frame_acceleration,
                     core::Vec3 angular_acceleration, core::Vec2 steer_deg, float dt);
    void drive_legs(const game::FlightState& state, core::Vec3 frame_acceleration,
                    core::Vec3 angular_acceleration, float dt);
    // Turns the head toward `aim_target_`, after the chains have posed it.
    void aim_head(const game::FlightState& state);
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
    // Smoothed g deviation and flight intensity, so wing flex and the clip fade
    // ease rather than jitter with every force spike.
    float load_smoothed_ = 0.0f;
    float intensity_smoothed_ = 0.0f;
    // Pendulum state per leg: x swing about body X (fore-aft), y about body Z
    // (lateral), in radians.
    core::Vec2 leg_swing_[2] = {};
    core::Vec2 leg_swing_velocity_[2] = {};
    float model_scale_ = 1.0f;
    const AnimationClip* base_clip_ = nullptr;
    core::Vec3 aim_target_ = core::Vec3::zero();
    bool aim_active_ = false;
    // The head's own forward axis, in its local frame, measured from the bind
    // pose. A rig's bones each point along their own axis, so this cannot be
    // assumed.
    core::Vec3 head_axis_local_ = core::Vec3::forward();
    float clip_time_ = 0.0f;
    // Previous frame's motion, for deriving the accelerations the chains feel.
    core::Vec3 previous_velocity_ = core::Vec3::zero();
    core::Vec3 previous_angular_velocity_ = core::Vec3::zero();
    bool have_previous_ = false;
    float leg_extend_ = 0.0f;
};

}  // namespace anim
