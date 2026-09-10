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
    std::vector<int> leg[2];                        // hind leg, hip outward
    std::vector<int> front_leg[2];                  // foreleg, shoulder outward
    // Foot roots (this asset parents every foot straight to the body -- an IK
    // rig's world-space targets), each with its toe chains hanging beneath.
    std::vector<int> foot_roots;
    // The lower jaw, if the rig has one. Opens for the breath and the spit.
    int jaw = NO_PARENT;

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
    // Higher lag makes the wing whip through the beat rather than rotate as a
    // rigid plank -- the tip visibly trails the shoulder.
    float wing_phase_lag = 0.26f;
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
    // Folding is distributed by anatomy rather than by one uniform rotation.
    // The values scale the shared fold at the elbow, wrist and fingers. A
    // value of 1 preserves the original progressive profile; an imported wing
    // can, for example, keep the elbow open while closing the wrist and hand.
    float wing_elbow_fold_scale = 1.0f;
    float wing_wrist_fold_scale = 1.0f;
    float wing_finger_fold_scale = 1.0f;
    // Extra hand folding while the wing is raised. The existing upstroke fold
    // remains the broad default; this opt-in term is useful for a stiff,
    // long-fingered imported wing. Zero preserves the old motion exactly.
    float wing_flap_fold_deg = 0.0f;
    // Visual ceiling for the flap angle. Zero disables the ceiling, preserving
    // the historical behaviour and leaving the flight engine's force model
    // untouched. This is an animation guard for rigs whose membranes cross at
    // the engine's full stroke.
    float wing_flap_limit_deg = 0.0f;
    // On the upstroke the wrist flexes and the wing part-folds -- real bird
    // kinematics, and what keeps the two raised wings from crossing over the
    // spine at the top of the beat.
    float upstroke_fold_deg = 24.0f;

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
    // How much of an axial (along-the-chain) inertial force the chain feels.
    // Transverse forces bend a spine; axial compression only buckles it, and
    // muscle resists exactly that -- a braking dragon's neck under full axial
    // pseudo-force folded under its chest.
    float chain_axial_response = 0.15f;

    // Ceilings, so a violent attitude cannot blow the simulation up. Without
    // these a 70 rad/s tumble produces accelerations in the tens of thousands
    // and the chain leaves for good.
    float chain_max_acceleration = 400.0f;
    float chain_max_speed = 120.0f;
    // Maximum bend between adjacent segments, so the chain cannot fold through
    // itself. Tight: a spine can bend a long way in total, but never sharply at
    // one vertebra -- and an accordioned tail was exactly what a looser limit
    // produced under hard manoeuvres.
    float chain_max_bend_deg = 20.0f;
    // The soft joint limit in front of that wall: a restoring pull (1/s^2 per
    // metre of over-bend) and damping of motion into the limit (1/s), both
    // fading in from half the allowed bend. A tail tip meeting a hard clamp
    // at 25 m/s stopped in one frame; this is what makes it ease instead.
    float chain_limit_stiffness = 60.0f;
    float chain_limit_damping = 12.0f;
    int chain_iterations = 4;
    // Muscle tone: chains stiffen with flight intensity, the way an animal
    // tenses under load. At tone 2 a full-intensity manoeuvre triples the
    // stiffness, which is what keeps the tail a rudder instead of a streamer in
    // a dive or a hard pull.
    float chain_tone = 2.0f;
    // Muscle also tenses against the load it is actually carrying: stiffness
    // (and damping, by its square root) scale with 1 + |inertial
    // acceleration| / this, per point. Without it a spring sized to look
    // legible at 1 g is flung to its constraints by a 4 g reversal and snaps
    // back -- the "abrupt" tail. With it the deflection saturates at roughly
    // this acceleration over the base stiffness: a held tail, not a flail.
    float chain_load_tone_accel = 8.0f;
    // The neck is muscle wrapped around a spine and carries the head the animal
    // aims with; it is far stiffer and far better supported than the tail.
    // Scales applied on top of the shared chain parameters.
    // At metre scale the frame's pseudo-forces (a 17 m/s^2 brake surge, a 3 g
    // pull) overwhelm a soft spring: deflection is roughly a/k, and 2.5x left
    // the head a metre out of line on a 2.4 m neck.
    float neck_stiffness_scale = 6.0f;
    float neck_gravity_scale = 0.35f;
    // The neck braces against frame accelerations rather than flailing with
    // them -- the head must stay a stable platform for the eyes -- and it is
    // heavy: overdamped, so it moves slowly and settles without ringing.
    float neck_inertia_scale = 0.18f;
    float neck_damping_scale = 2.2f;
    float tail_damping_scale = 1.25f;
    // The tail's spring thins toward the tip: muscle is thick at the base and
    // a whisker at the end. With one stiffness per point, a uniform load moved
    // every point by the same a/k and the constraints turned that into a rigid
    // rod pivoting at the root -- exactly the "robotic" tail a straight-rested
    // asset showed. Tip stiffness as a fraction of the base's; the deflection
    // then grows toward the tip, which is what a tail does.
    float tail_tip_stiffness = 0.25f;
    // Hard articulation limits, total deviation from the rest shape. The neck
    // is tight -- big head turns are the aim system's job, not the sim's; the
    // tail keeps room to whip.
    float neck_range_deg = 30.0f;
    float tail_range_deg = 80.0f;

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
    float tail_rudder_deg = 16.0f;
    float tail_elevator_deg = 8.0f;
    // The neck leads: nose-up input curls the head up before the body follows.
    // Anticipation, the oldest animation principle there is.
    float neck_lead_deg = 6.0f;
    // And at speed the neck lowers into the wind. Full effect at
    // `streamline_speed` and above.
    float neck_streamline_deg = 5.0f;
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
    // The clip is a GROUND idle -- toes gripping, jaw working, weight shifting
    // -- so it belongs on the ground. Airborne it is nearly gone: a trace
    // survives in a calm glide so the extremities are not dead still, and even
    // that fades to nothing as flight gets violent.
    float clip_air_weight = 0.15f;
    float clip_flight_fade = 1.0f;

    // ---- head aim ----
    //
    // While attacking, the head turns toward the target. This is readability as
    // much as flourish: the fire leaves along the aim axis, and a head pointing
    // somewhere else makes the shot look like it came from nowhere, which reads
    // as the animation fighting the aim.
    float head_aim_blend = 0.85f;
    float head_aim_max_deg = 60.0f;
    // The NECK carries part of the aim, not just the head: an animal that
    // looks 40 degrees off its body turns its whole neck and finishes with the
    // head. Fraction of the aim angle steered into the neck chain (the spring
    // eases it in), capped so the neck never corkscrews.
    float neck_aim_share = 0.55f;
    float neck_aim_max_deg = 35.0f;

    // ---- attack posture ----
    //
    // What the body does when it uses its weapons. Fire that leaves a closed,
    // still mouth reads as a particle effect stapled to a model; the jaw, the
    // neck and the claws are what say the CREATURE is doing it.
    float jaw_open_deg = 26.0f;
    // Fireball: the neck rears back and whips forward, a spit. Peak deflection
    // and the duration of the whole gesture.
    float spit_recoil_deg = 30.0f;
    float spit_duration = 0.5f;
    // And a muscular impulse: velocity kicked into the neck at the moment of
    // firing (m/s at the head, up and back), because a steer alone asks an
    // overdamped neck to move through its spring, and a spit is a snap.
    float spit_impulse = 4.0f;
    // Breath: the neck thrusts forward and down into the stream, stiffens
    // (a tensed neck holds the flame steady) and trembles faintly with the
    // effort. The tremor is on the head only, after the aim.
    float breath_neck_thrust_deg = 14.0f;
    float breath_neck_tone = 1.5f;
    float breath_tremor_deg = 0.7f;
    // Talons open while attacking -- the claws come out.
    float attack_toe_spread_deg = 12.0f;

    // ---- speed and load posture ----
    //
    // A stoop is not only a control input: past cruise the wings sweep back and
    // part-fold on their own, a falcon's shape, whether or not the tuck is
    // held. Blends to the full tuck angles as the tuck is applied.
    float speed_sweep_deg = 30.0f;
    float speed_fold_deg = 14.0f;
    float sweep_speed_start = 45.0f;
    float sweep_speed_full = 95.0f;
    // Membrane flutter: the outer wing buffets at speed, and shudders in a
    // flare. Amplitude at the tip; it grows with the square of the speed
    // factor so cruise is dead calm and a dive is alive.
    float flutter_deg = 2.0f;
    float flutter_speed_start = 55.0f;
    float brake_buffet_deg = 3.0f;
    // Under g the tips wash out (leading edge down, shedding load) and the
    // wings come forward a little, the flare shape of a bird pulling hard.
    float load_twist_deg = 4.0f;
    float load_forward_sweep_deg = 4.0f;

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
    // In flight the legs trail: the whole limb rotates aft at the hip, then the
    // fold bends the knee. Trailing is what a flying quadruped actually does --
    // legs pressed back along the body -- where a pure fold leaves them dangling
    // beneath it like landing gear.
    float leg_trail_deg = 38.0f;
    float front_leg_trail_deg = 30.0f;
    // During an air-brake a flying animal begins to unfold its landing limbs
    // and lets them float into the airflow. The defaults are zero so existing
    // models retain the original tucked pose; a model config can opt in to a
    // partial extension and an engine-forward hip offset.
    float leg_brake_extend = 0.0f;
    float leg_brake_forward_deg = 0.0f;
    // In flight the feet hang: ankle dropped, claws part-curled -- a perched
    // bird's relaxed foot, not a planted one. This asset parents its feet to the
    // body, so nothing else would ever move them once the ground idle fades.
    float foot_hang_deg = 30.0f;
    float toe_curl_deg = 16.0f;
    // How strongly the feet ride their legs in flight. This asset's feet are IK
    // targets parented to the BODY: pose the legs however you like, the feet
    // stay nailed to their bind position in space -- the standing-in-air look.
    // In flight each foot is re-anchored to the end of its leg chain; on the
    // ground the authored planted stance wins.
    float foot_follow = 1.0f;
};

// Flat `key value` text, one field per line. A model may opt into a rig profile
// by placing `<model>.rig.cfg` beside its glTF; absent or unknown keys leave the
// built-in defaults untouched.
bool save_rig_tuning(const RigTuning& tuning, const char* path);
bool load_rig_tuning(RigTuning& tuning, const char* path);

// What the dragon is doing with its weapons this frame, for the attack
// posture. Flight already arrives through FlightState; this is the rest.
struct RigAction {
    float breath = 0.0f;  // 0..1, flame held
    bool fire = false;    // edge: a fireball left this frame
    float boost = 0.0f;   // 0..1
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
    // Weapon use, read by the next update(). Persists until set again, so a
    // caller that stops attacking must say so.
    void set_action(const RigAction& action) { action_ = action; }
    const RigAction& action() const { return action_; }
    // How open the jaw is, 0..1, after the last update. For probes and tests.
    float jaw_open() const { return jaw_open_; }

    // Where the mouth actually is, in world space -- the head joint's origin
    // after animation. For drawing anything that should issue from it.
    core::Vec3 head_position() const;

    // Authored motion layered under the procedural pose. Not owned; must outlive
    // the rig. Null disables it. `hold_at` >= 0 freezes the clip at that time
    // -- for an asset with no idle, the last frame of its landing is a
    // standing pose, and a held pose beats a landing replayed on loop.
    void set_base_clip(const AnimationClip* clip, float hold_at = -1.0f) {
        base_clip_ = clip;
        clip_hold_time_ = hold_at;
    }
    bool has_base_clip() const { return base_clip_ && base_clip_->valid(); }
    void update(const game::FlightState& state, float dt);

    // A chain simulated as point masses in the dragon's own frame.
    struct ChainDynamics {
        std::vector<core::Vec3> position;  // body-local, simulated
        std::vector<core::Vec3> velocity;
        std::vector<core::Vec3> rest;      // body-local bind positions
        std::vector<float> segment;        // rest length to the previous point
        bool initialized = false;
    };

    const Pose& pose() const { return pose_; }
    const std::vector<core::Mat4>& skinning_matrices() const { return skinning_; }
    // Joint world transforms, for debug drawing the skeleton.
    const std::vector<core::Mat4>& world_matrices() const { return world_; }
    // Raw chain states, for probes: the rig's output should be a faithful
    // reconstruction of these, and a probe that can see both can tell a broken
    // constraint from a broken reconstruction.
    const ChainDynamics& neck_sim() const { return neck_sim_; }
    const ChainDynamics& tail_sim() const { return tail_sim_; }

    RigTuning tuning;

private:

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
    // The jaw, the breath tremor and the talons -- after the aim, because the
    // aim would otherwise correct the tremor away.
    void drive_attack(float airborne);
    // 0 calm glide .. 1 flat out: how hard the flight state is working the body.
    float flight_intensity(const game::FlightState& state) const;
    // Per-chain feel on top of the shared parameters.
    struct ChainFeel {
        float stiffness = 1.0f;
        float gravity = 1.0f;
        // How much of the frame's pseudo-forces the chain feels. Below 1 the
        // animal braces: a neck held rigid against a deceleration instead of
        // buckling under the chest.
        float inertia = 1.0f;
        // Extra damping on top of the shared value. Above critical the chain
        // moves slowly and settles without ringing -- the feel of a heavy,
        // muscular limb rather than a spring.
        float damping = 1.0f;
        // How much v^2 aerodynamic streaming this chain is allowed. Per-chain,
        // NOT per-instant: gating on the momentary direction created a trap
        // where a surge that folded the neck backward made it "downstream" and
        // the drag then pinned it folded under the body like a windsock. A
        // neck is never a windsock; muscle owns it at every angle.
        float aero = 1.0f;
        // Range of motion: no segment may deviate further than this from its
        // (steered) rest direction, whatever the forces say. Muscle has a
        // range, and every long-run failure mode ends outside it.
        float range_deg = 178.0f;
        // Stiffness at the far end as a fraction of the base's. 1 is uniform
        // (a neck carrying a head it must hold still); below 1 the chain
        // bends progressively rather than pivoting as a rod.
        float tip_stiffness = 1.0f;
    };
    void setup_chain(ChainDynamics& sim, const std::vector<int>& chain) const;
    // Integrates the chain, then turns the simulated shape back into joint
    // rotations.
    // `steer_deg` actively deflects the chain's target shape: x pitches it about
    // the body's X axis, y swings it about Y. The spring then pulls the chain
    // toward the deflected shape, so steering composes with the passive
    // dynamics instead of overwriting them.
    void drive_chain(ChainDynamics& sim, const std::vector<int>& chain,
                     const game::FlightState& state, core::Vec3 frame_acceleration,
                     core::Vec3 angular_acceleration, core::Vec2 steer_deg, ChainFeel feel,
                     float dt);
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
    // Every joint under a foot root, with its depth below the root -- the toes,
    // for the hanging curl.
    std::vector<std::pair<int, int>> foot_joints_;
    // A body-parented foot root re-anchored to the end of a leg chain, with its
    // bind-pose offset expressed in that anchor's frame.
    struct FootAttach {
        int foot = NO_PARENT;
        int anchor = NO_PARENT;
        core::Vec3 offset = core::Vec3::zero();
        core::Quat rotation = core::Quat::identity();
    };
    std::vector<FootAttach> foot_attach_;
    void attach_feet(float airborne);
    float model_scale_ = 1.0f;
    const AnimationClip* base_clip_ = nullptr;
    float clip_hold_time_ = -1.0f;
    // The authored pose as sampled this frame, before the rig overrode it, and
    // how much of it wins: on the ground the artist's stance is the whole
    // body, not just the toes. Smoothed ground CONTACT, not proximity -- the
    // approach still belongs to the rig.
    Pose clip_pose_;
    float ground_contact_ = 0.0f;
    core::Vec3 aim_target_ = core::Vec3::zero();
    bool aim_active_ = false;
    // The head's own forward axis, in its local frame, measured from the bind
    // pose. A rig's bones each point along their own axis, so this cannot be
    // assumed.
    core::Vec3 head_axis_local_ = core::Vec3::forward();
    // Which way the MODEL faces in its own space: +1 for +Z (this asset), -1
    // for -Z (the engine's convention and the generated rig). Measured from
    // the bind pose. Every world quantity the rig reads -- gravity, airflow,
    // the frame's pseudo-forces, the aim target -- arrives in the engine's
    // body frame, and every bone lives in model space; body_to_model_ is the
    // one rotation between them. Skipping it was invisible on the generated
    // rig (the frames coincide) and quietly inverted pitch steering, the
    // fore-aft pseudo-forces and the head aim on the imported dragon.
    float model_forward_z_ = -1.0f;
    core::Quat body_to_model_ = core::Quat::identity();
    // The flight state re-expressed so that conj(orientation) lands in model
    // space rather than the engine's body frame.
    game::FlightState model_frame(const game::FlightState& state) const;
    // Attack state. spit_time_ counts up from the last fireball; a large
    // value means none is in progress.
    RigAction action_;
    float breath_smoothed_ = 0.0f;
    float spit_time_ = 1e9f;
    float jaw_open_ = 0.0f;
    // Which way a positive body-X rotation of the jaw moves it: +1 opens, -1
    // closes. Measured at init, because the jaw bone points wherever the
    // rigger left it.
    float jaw_open_sign_ = 1.0f;
    // Running time for the flutter oscillators.
    float time_ = 0.0f;
    float clip_time_ = 0.0f;
    // Previous frame's motion, for deriving the accelerations the chains feel.
    core::Vec3 previous_velocity_ = core::Vec3::zero();
    core::Vec3 previous_angular_velocity_ = core::Vec3::zero();
    bool have_previous_ = false;
    float leg_extend_ = 0.0f;
};

}  // namespace anim
