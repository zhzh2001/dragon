#include "anim/dragon_rig.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "core/log.h"

using core::Quat;
using core::Transform;
using core::Vec3;

namespace anim {
namespace {

constexpr Vec3 BODY_COLOR{0.30f, 0.21f, 0.26f};
constexpr Vec3 BELLY_COLOR{0.50f, 0.41f, 0.32f};
constexpr Vec3 HEAD_COLOR{0.55f, 0.28f, 0.22f};
constexpr Vec3 WING_COLOR{0.36f, 0.24f, 0.28f};
constexpr Vec3 WING_EDGE{0.58f, 0.38f, 0.31f};
constexpr Vec3 TAIL_COLOR{0.25f, 0.18f, 0.23f};
constexpr Vec3 LEG_COLOR{0.28f, 0.20f, 0.24f};

Transform offset(Vec3 position) {
    Transform t;
    t.position = position;
    return t;
}

// The flight model owns the beat clock, but a model-specific rig may sample
// that clock a little behind at the wrist and fingers. Keep the same unequal
// downstroke/recovery easing here so the visual delay preserves the physical
// beat shape instead of turning it into a sine wave.
float wrapped_phase(float phase) {
    phase -= std::floor(phase);
    return phase < 0.0f ? phase + 1.0f : phase;
}

float wingbeat_curve(float phase, float downstroke_fraction) {
    const float down = core::clampf(downstroke_fraction, 0.05f, 0.95f);
    phase = wrapped_phase(phase);
    if (phase < down) {
        const float t = phase / down;
        return std::cos(t * core::PI);  // +1 down to -1
    }
    const float t = (phase - down) / (1.0f - down);
    return -std::cos(t * core::PI);  // -1 back up to +1
}

// d(wingbeat_curve)/d(phase), normalized so the downstroke's peak rate is -1.
// Negative while the wing moves down. The recovery peaks at down/(1-down),
// because the slower half-stroke moves the same distance in more time.
float wingbeat_rate(float phase, float downstroke_fraction) {
    const float down = core::clampf(downstroke_fraction, 0.05f, 0.95f);
    phase = wrapped_phase(phase);
    if (phase < down) {
        return -std::sin(core::PI * phase / down);
    }
    const float t = (phase - down) / (1.0f - down);
    return std::sin(t * core::PI) * down / (1.0f - down);
}

// A tube of `segments` rings skinned along a chain of joints.
//
// Each ring is bound to the two nearest joints in the chain, weighted by how far
// along the span it sits. Two influences are enough for a tube and keep the
// deformation predictable -- four would let distant joints pull at a ring and
// produce creases nobody asked for.
void skin_tube(SkinnedMeshData& mesh, const Skeleton& skeleton, const std::vector<int>& chain,
               const std::vector<float>& radii, int sides, Vec3 top_color, Vec3 bottom_color,
               bool cap_end) {
    if (chain.size() < 2 || radii.size() != chain.size()) return;

    std::vector<uint32_t> ring_starts;
    for (size_t link = 0; link < chain.size(); ++link) {
        const core::Mat4 bind = skeleton.world_bind(chain[link]);
        const Vec3 centre = bind.translation_part();
        // The chain runs along the joint's local Z, so the ring is spanned by its
        // local X and Y.
        const Vec3 right = core::normalize_or(core::transform_dir(bind, Vec3::unit_x()),
                                             Vec3::unit_x());
        const Vec3 up = core::normalize_or(core::transform_dir(bind, Vec3::unit_y()), Vec3::up());

        ring_starts.push_back(uint32_t(mesh.vertices.size()));
        for (int side = 0; side < sides; ++side) {
            const float angle = core::TWO_PI * float(side) / float(sides);
            const Vec3 position =
                centre + (right * std::cos(angle) + up * std::sin(angle)) * radii[link];
            const float blend = core::saturate(std::sin(angle) * 0.5f + 0.5f);
            const Vec3 color = core::lerp(bottom_color, top_color, blend);

            // Blend toward the next joint over the second half of each segment,
            // so a ring sitting near a joint boundary is influenced by both.
            const int joint_a = chain[link];
            const int joint_b = chain[link + 1 < chain.size() ? link + 1 : link];
            const int indices[4] = {joint_a, joint_b, -1, -1};
            const float weights[4] = {0.75f, 0.25f, 0.0f, 0.0f};
            mesh.add(position, color, core::Vec2{0.0f, 0.0f}, indices, weights);
        }
    }

    for (size_t link = 0; link + 1 < ring_starts.size(); ++link) {
        for (int side = 0; side < sides; ++side) {
            const uint32_t next_side = uint32_t((side + 1) % sides);
            const uint32_t a = ring_starts[link] + uint32_t(side);
            const uint32_t b = ring_starts[link + 1] + uint32_t(side);
            const uint32_t c = ring_starts[link + 1] + next_side;
            const uint32_t d = ring_starts[link] + next_side;
            mesh.indices.push_back(a);
            mesh.indices.push_back(b);
            mesh.indices.push_back(c);
            mesh.indices.push_back(a);
            mesh.indices.push_back(c);
            mesh.indices.push_back(d);
        }
    }

    if (cap_end) {
        const int last = chain.back();
        const core::Mat4 bind = skeleton.world_bind(last);
        const Vec3 tip = bind.translation_part() +
                         core::normalize_or(core::transform_dir(bind, Vec3::unit_z()),
                                            Vec3::unit_z()) *
                             radii.back() * 1.6f;
        const int indices[4] = {last, -1, -1, -1};
        const float weights[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        const uint32_t tip_index = mesh.add(tip, top_color, core::Vec2{0.5f, 0.5f}, indices, weights);
        for (int side = 0; side < sides; ++side) {
            const uint32_t next_side = uint32_t((side + 1) % sides);
            mesh.indices.push_back(ring_starts.back() + uint32_t(side));
            mesh.indices.push_back(ring_starts.back() + next_side);
            mesh.indices.push_back(tip_index);
        }
    }
}

// Wing membrane: a fan of panels spanning shoulder, elbow, wrist and tip, with
// each vertex bound to the wing bone it sits on. This is what makes the wing
// fold rather than merely rotate.
void skin_wing(SkinnedMeshData& mesh, const Skeleton& skeleton, const DragonShape& shape,
               const int (&bones)[3], float side) {
    struct Station {
        Vec3 leading;
        Vec3 trailing;
        int bone;
    };
    std::vector<Station> stations;

    // Root at the shoulder, then one station per joint out to the tip.
    const Vec3 shoulder = skeleton.world_bind(bones[0]).translation_part();
    const Vec3 elbow = skeleton.world_bind(bones[1]).translation_part();
    const Vec3 wrist = skeleton.world_bind(bones[2]).translation_part();
    const Vec3 tip = wrist + Vec3{shape.wing_hand * side, 0.0f, 0.0f};

    const float root_chord = shape.wing_chord;
    stations.push_back({shoulder + Vec3{0, 0, -root_chord * 0.5f},
                        shoulder + Vec3{0, 0, root_chord * 0.5f}, bones[0]});
    stations.push_back({elbow + Vec3{0, 0, -root_chord * 0.42f},
                        elbow + Vec3{0, 0, root_chord * 0.62f}, bones[1]});
    stations.push_back({wrist + Vec3{0, 0, -root_chord * 0.26f},
                        wrist + Vec3{0, 0, root_chord * 0.58f}, bones[2]});
    stations.push_back({tip + Vec3{0, 0, root_chord * 0.10f},
                       tip + Vec3{0, 0, root_chord * 0.42f}, bones[2]});

    std::vector<uint32_t> leading, trailing;
    for (size_t i = 0; i < stations.size(); ++i) {
        const Station& station = stations[i];
        // Blend with the bone inboard of this station. Binding each station
        // rigidly to one bone makes a folding wing crease at every joint; two
        // influences let it curve.
        const int inboard = i > 0 ? stations[i - 1].bone : station.bone;
        const int indices[4] = {station.bone, inboard, -1, -1};
        const float weights[4] = {0.72f, 0.28f, 0.0f, 0.0f};
        leading.push_back(mesh.add(station.leading, WING_EDGE, core::Vec2{0.0f, float(i) / 3.0f}, indices, weights));
        trailing.push_back(mesh.add(station.trailing, WING_COLOR, core::Vec2{1.0f, float(i) / 3.0f}, indices, weights));
    }

    for (size_t i = 0; i + 1 < stations.size(); ++i) {
        const uint32_t l0 = leading[i], l1 = leading[i + 1];
        const uint32_t t0 = trailing[i], t1 = trailing[i + 1];
        // Both windings: a membrane is seen from above and below.
        const uint32_t quads[2][4] = {{l0, l1, t1, t0}, {l0, t0, t1, l1}};
        const int order = side > 0.0f ? 0 : 1;
        for (int pass = 0; pass < 2; ++pass) {
            const uint32_t* q = quads[(order + pass) % 2];
            mesh.indices.push_back(q[0]);
            mesh.indices.push_back(q[1]);
            mesh.indices.push_back(q[2]);
            mesh.indices.push_back(q[0]);
            mesh.indices.push_back(q[2]);
            mesh.indices.push_back(q[3]);
        }
    }
}

}  // namespace

void build_dragon(const DragonShape& shape, Skeleton& out_skeleton, DragonJoints& out_joints,
                  SkinnedMeshData& out_mesh) {
    out_skeleton = Skeleton();
    out_joints = DragonJoints();
    out_mesh = SkinnedMeshData();

    Skeleton& skeleton = out_skeleton;
    DragonJoints& j = out_joints;

    // Root at the hips. Forward is -Z, so the body runs from +Z (tail) to
    // -Z (head) and every chain advances along its own local -Z.
    j.root = skeleton.add_joint("root", NO_PARENT, offset(Vec3::zero()));
    j.chest = skeleton.add_joint("chest", j.root, offset(Vec3{0, 0, -shape.body_length * 0.55f}));

    // Neck, evenly divided, then the head.
    int parent = j.chest;
    const float neck_step = shape.neck_length / float(shape.neck_joints);
    for (int i = 0; i < shape.neck_joints; ++i) {
        parent = skeleton.add_joint("neck" + std::to_string(i), parent,
                                    offset(Vec3{0, 0, -neck_step}));
        j.neck.push_back(parent);
    }
    j.head = skeleton.add_joint("head", parent, offset(Vec3{0, 0, -neck_step * 0.9f}));
    // A lower jaw with a tip, so the attack posture has a mouth to open and
    // something to measure the opening direction against.
    j.jaw = skeleton.add_joint("jaw", j.head, offset(Vec3{0, -0.35f, -0.5f}));
    skeleton.add_joint("jaw_tip", j.jaw, offset(Vec3{0, -0.1f, -1.2f}));

    // Tail, backwards from the root.
    parent = j.root;
    const float tail_step = shape.tail_length / float(shape.tail_joints);
    for (int i = 0; i < shape.tail_joints; ++i) {
        parent = skeleton.add_joint("tail" + std::to_string(i), parent,
                                    offset(Vec3{0, 0, tail_step}));
        j.tail.push_back(parent);
    }

    // Wings and legs, right side first.
    for (int side = 0; side < 2; ++side) {
        const float sign = side == 0 ? 1.0f : -1.0f;
        const std::string suffix = side == 0 ? "_r" : "_l";

        const int shoulder = skeleton.add_joint(
            "shoulder" + suffix, j.chest,
            offset(Vec3{sign * shape.body_radius * 0.8f, shape.body_radius * 0.35f, 0.0f}));
        const int elbow = skeleton.add_joint("elbow" + suffix, shoulder,
                                             offset(Vec3{sign * shape.wing_upper, 0, 0}));
        const int wrist = skeleton.add_joint("wrist" + suffix, elbow,
                                             offset(Vec3{sign * shape.wing_fore, 0, 0}));
        j.wing_root[side] = {shoulder, elbow};
        j.wing_fingers[side] = {{wrist}};

        const int hip = skeleton.add_joint(
            "hip" + suffix, j.root,
            offset(Vec3{sign * shape.body_radius * 0.6f, -shape.body_radius * 0.4f, 0.0f}));
        const int knee =
            skeleton.add_joint("knee" + suffix, hip, offset(Vec3{0, -shape.leg_upper, 0}));
        j.leg[side] = {hip, knee};
    }

    skeleton.finalize();

    // ---- mesh ----

    // Body: root through chest into the first neck joint, so the shoulders are
    // covered by a continuous tube.
    std::vector<int> body_chain{j.root, j.chest};
    std::vector<float> body_radii{shape.body_radius * 0.72f, shape.body_radius};
    if (!j.neck.empty()) {
        body_chain.push_back(j.neck.front());
        body_radii.push_back(shape.body_radius * 0.52f);
    }
    skin_tube(out_mesh, skeleton, body_chain, body_radii, 12, BODY_COLOR, BELLY_COLOR, false);

    // Neck tapering into the head.
    std::vector<int> neck_chain = j.neck;
    neck_chain.push_back(j.head);
    std::vector<float> neck_radii;
    for (size_t i = 0; i < neck_chain.size(); ++i) {
        const float t = float(i) / float(neck_chain.size() - 1);
        // Taper along the neck, then widen again at the skull. A monotonic taper
        // reads as a tentacle rather than a head on a neck.
        const float taper = core::lerpf(0.50f, 0.24f, core::saturate(t * 1.25f));
        const float skull = core::smoothstep(0.72f, 1.0f, t) * 0.30f;
        neck_radii.push_back(shape.body_radius * (taper + skull));
    }
    skin_tube(out_mesh, skeleton, neck_chain, neck_radii, 10, HEAD_COLOR, HEAD_COLOR, true);

    // Tail tapering to a point.
    std::vector<float> tail_radii;
    for (size_t i = 0; i < j.tail.size(); ++i) {
        const float t = float(i) / float(j.tail.size() - 1);
        tail_radii.push_back(shape.body_radius * core::lerpf(0.60f, 0.07f, t));
    }
    skin_tube(out_mesh, skeleton, j.tail, tail_radii, 10, TAIL_COLOR, TAIL_COLOR, true);

    // Legs.
    for (int side = 0; side < 2; ++side) {
        std::vector<float> radii{shape.body_radius * 0.26f, shape.body_radius * 0.17f};
        skin_tube(out_mesh, skeleton, j.leg[side], radii, 8, LEG_COLOR, LEG_COLOR, true);
    }

    // Wings.
    for (int side = 0; side < 2; ++side) {
        const int bones[3] = {j.wing_root[side][0], j.wing_root[side][1],
                              j.wing_fingers[side][0][0]};
        skin_wing(out_mesh, skeleton, shape, bones, side == 0 ? 1.0f : -1.0f);
    }

    out_mesh.recompute_normals();
    LOG_INFO("dragon rig: %d joints, %zu verts, %zu tris", skeleton.count(),
             out_mesh.vertices.size(), out_mesh.indices.size() / 3);
}

namespace {

// Keep the rig config flat and hand-editable, like the flight config. The
// pointer-to-member table is shared by save and load so a field cannot be
// persisted in one direction and silently ignored in the other.
struct RigField {
    const char* name;
    float RigTuning::*float_member;
    int RigTuning::*int_member;
};

#define RIG_FLOAT_FIELD(name) {#name, &RigTuning::name, nullptr}
#define RIG_INT_FIELD(name) {#name, nullptr, &RigTuning::name}
const RigField RIG_FIELDS[] = {
    RIG_FLOAT_FIELD(flap_shoulder_deg),
    RIG_FLOAT_FIELD(wing_phase_lag),
    RIG_FLOAT_FIELD(wing_phase_delay),
    RIG_FLOAT_FIELD(wing_downstroke_fraction),
    RIG_FLOAT_FIELD(outboard_decay),
    RIG_FLOAT_FIELD(tuck_sweep_deg),
    RIG_FLOAT_FIELD(tuck_fold_deg),
    RIG_FLOAT_FIELD(tuck_droop_deg),
    RIG_FLOAT_FIELD(brake_flare_deg),
    RIG_FLOAT_FIELD(brake_body_pitch_deg),
    RIG_FLOAT_FIELD(brake_raise_deg),
    RIG_FLOAT_FIELD(brake_protract_deg),
    RIG_FLOAT_FIELD(brake_tail_drop_deg),
    RIG_FLOAT_FIELD(brake_bank_relief),
    RIG_FLOAT_FIELD(wing_elbow_fold_scale),
    RIG_FLOAT_FIELD(wing_wrist_fold_scale),
    RIG_FLOAT_FIELD(wing_finger_fold_scale),
    RIG_FLOAT_FIELD(wing_flap_fold_deg),
    RIG_FLOAT_FIELD(wing_flap_limit_deg),
    RIG_FLOAT_FIELD(wing_recovery_fold_deg),
    RIG_FLOAT_FIELD(wing_recovery_extend_phase),
    RIG_FLOAT_FIELD(upstroke_fold_deg),
    RIG_FLOAT_FIELD(stroke_plane_tilt_deg),
    RIG_FLOAT_FIELD(recovery_elbow_deg),
    RIG_FLOAT_FIELD(recovery_wrist_deg),
    RIG_FLOAT_FIELD(recovery_finger_deg),
    RIG_FLOAT_FIELD(recovery_droop_deg),
    RIG_FLOAT_FIELD(stroke_twist_deg),
    RIG_FLOAT_FIELD(beat_heave_m),
    RIG_FLOAT_FIELD(beat_heave_lag),
    RIG_FLOAT_FIELD(beat_pitch_deg),
    RIG_FLOAT_FIELD(ground_stow_sweep_deg),
    RIG_FLOAT_FIELD(ground_stow_fold_deg),
    RIG_FLOAT_FIELD(ground_stow_wrist_deg),
    RIG_FLOAT_FIELD(ground_stow_finger_deg),
    RIG_FLOAT_FIELD(ground_stow_close_deg),
    RIG_FLOAT_FIELD(ground_stow_elbow_scale),
    RIG_FLOAT_FIELD(ground_stow_tuck_share),
    RIG_FLOAT_FIELD(ground_stow_converge_deg),
    RIG_FLOAT_FIELD(ground_wing_aim),
    RIG_FLOAT_FIELD(ground_wing_arm_sweep_deg),
    RIG_FLOAT_FIELD(ground_wing_arm_elev_deg),
    RIG_FLOAT_FIELD(ground_wing_forearm_sweep_deg),
    RIG_FLOAT_FIELD(ground_wing_forearm_elev_deg),
    RIG_FLOAT_FIELD(ground_wing_hand_sweep_deg),
    RIG_FLOAT_FIELD(ground_wing_hand_elev_deg),
    RIG_FLOAT_FIELD(ground_wing_fan_deg),
    RIG_FLOAT_FIELD(ground_stance),
    RIG_FLOAT_FIELD(ground_body_pitch_deg),
    RIG_FLOAT_FIELD(ground_hip_deg),
    RIG_FLOAT_FIELD(ground_knee_deg),
    RIG_FLOAT_FIELD(ground_ankle_deg),
    RIG_FLOAT_FIELD(ground_shoulder_deg),
    RIG_FLOAT_FIELD(ground_elbow_deg),
    RIG_FLOAT_FIELD(ground_wrist_deg),
    RIG_FLOAT_FIELD(ground_leg_splay_deg),
    RIG_FLOAT_FIELD(ground_arm_splay_deg),
    RIG_FLOAT_FIELD(ground_feet_level),
    RIG_FLOAT_FIELD(ground_wing_plant),
    RIG_FLOAT_FIELD(ground_neck_pitch_deg),
    RIG_FLOAT_FIELD(ground_tail_pitch_deg),
    RIG_FLOAT_FIELD(ground_lift_m),
    RIG_FLOAT_FIELD(ground_ik),
    RIG_FLOAT_FIELD(stride_length_m),
    RIG_FLOAT_FIELD(stride_lift_m),
    RIG_FLOAT_FIELD(standing_speed),
    RIG_FLOAT_FIELD(ground_ik_tilt_max_deg),
    RIG_FLOAT_FIELD(chain_stiffness),
    RIG_FLOAT_FIELD(chain_damping),
    RIG_FLOAT_FIELD(chain_inertia),
    RIG_FLOAT_FIELD(chain_gravity),
    RIG_FLOAT_FIELD(chain_drag),
    RIG_FLOAT_FIELD(chain_drag_v2),
    RIG_FLOAT_FIELD(chain_axial_response),
    RIG_FLOAT_FIELD(chain_max_acceleration),
    RIG_FLOAT_FIELD(chain_max_speed),
    RIG_FLOAT_FIELD(chain_max_bend_deg),
    RIG_FLOAT_FIELD(chain_limit_stiffness),
    RIG_FLOAT_FIELD(chain_limit_damping),
    RIG_INT_FIELD(chain_iterations),
    RIG_FLOAT_FIELD(chain_tone),
    RIG_FLOAT_FIELD(chain_load_tone_accel),
    RIG_FLOAT_FIELD(neck_stiffness_scale),
    RIG_FLOAT_FIELD(neck_gravity_scale),
    RIG_FLOAT_FIELD(neck_inertia_scale),
    RIG_FLOAT_FIELD(neck_damping_scale),
    RIG_FLOAT_FIELD(tail_damping_scale),
    RIG_FLOAT_FIELD(tail_tip_stiffness),
    RIG_FLOAT_FIELD(neck_range_deg),
    RIG_FLOAT_FIELD(tail_range_deg),
    RIG_FLOAT_FIELD(tail_rudder_deg),
    RIG_FLOAT_FIELD(tail_elevator_deg),
    RIG_FLOAT_FIELD(neck_lead_deg),
    RIG_FLOAT_FIELD(neck_streamline_deg),
    RIG_FLOAT_FIELD(streamline_speed),
    RIG_FLOAT_FIELD(wing_load_flex_deg),
    RIG_FLOAT_FIELD(wing_roll_lean_deg),
    RIG_FLOAT_FIELD(base_clip_weight),
    RIG_FLOAT_FIELD(base_clip_rate),
    RIG_FLOAT_FIELD(clip_air_weight),
    RIG_FLOAT_FIELD(clip_flight_fade),
    RIG_FLOAT_FIELD(head_aim_blend),
    RIG_FLOAT_FIELD(head_aim_max_deg),
    RIG_FLOAT_FIELD(neck_aim_share),
    RIG_FLOAT_FIELD(neck_aim_max_deg),
    RIG_FLOAT_FIELD(jaw_open_deg),
    RIG_FLOAT_FIELD(jaw_rest_deg),
    RIG_FLOAT_FIELD(first_person_up),
    RIG_FLOAT_FIELD(first_person_back),
    RIG_FLOAT_FIELD(spit_recoil_deg),
    RIG_FLOAT_FIELD(spit_duration),
    RIG_FLOAT_FIELD(spit_impulse),
    RIG_FLOAT_FIELD(bite_lunge_deg),
    RIG_FLOAT_FIELD(bite_duration),
    RIG_FLOAT_FIELD(bite_impulse),
    RIG_FLOAT_FIELD(claw_swing_deg),
    RIG_FLOAT_FIELD(claw_duration),
    RIG_FLOAT_FIELD(claw_out_deg),
    RIG_FLOAT_FIELD(tail_whip_deg),
    RIG_FLOAT_FIELD(tail_duration),
    RIG_FLOAT_FIELD(tail_impulse),
    RIG_FLOAT_FIELD(gesture_anticipation),
    RIG_FLOAT_FIELD(gesture_body_pitch_deg),
    RIG_FLOAT_FIELD(gesture_body_roll_deg),
    RIG_FLOAT_FIELD(gesture_body_yaw_deg),
    RIG_FLOAT_FIELD(gesture_sway_m),
    RIG_FLOAT_FIELD(gesture_surge_m),
    RIG_FLOAT_FIELD(gesture_wing_deg),
    RIG_FLOAT_FIELD(gesture_neck_deg),
    RIG_FLOAT_FIELD(gesture_tail_counter_deg),
    RIG_FLOAT_FIELD(breath_neck_thrust_deg),
    RIG_FLOAT_FIELD(breath_neck_tone),
    RIG_FLOAT_FIELD(breath_tremor_deg),
    RIG_FLOAT_FIELD(attack_toe_spread_deg),
    RIG_FLOAT_FIELD(speed_sweep_deg),
    RIG_FLOAT_FIELD(speed_fold_deg),
    RIG_FLOAT_FIELD(sweep_speed_start),
    RIG_FLOAT_FIELD(sweep_speed_full),
    RIG_FLOAT_FIELD(flutter_deg),
    RIG_FLOAT_FIELD(flutter_speed_start),
    RIG_FLOAT_FIELD(brake_buffet_deg),
    RIG_FLOAT_FIELD(load_twist_deg),
    RIG_FLOAT_FIELD(load_forward_sweep_deg),
    RIG_FLOAT_FIELD(leg_tuck_deg),
    RIG_FLOAT_FIELD(leg_sway_response),
    RIG_FLOAT_FIELD(leg_sway_max_deg),
    RIG_FLOAT_FIELD(leg_sway_stiffness),
    RIG_FLOAT_FIELD(leg_sway_damping),
    RIG_FLOAT_FIELD(leg_trail_deg),
    RIG_FLOAT_FIELD(front_leg_trail_deg),
    RIG_FLOAT_FIELD(leg_brake_extend),
    RIG_FLOAT_FIELD(leg_brake_forward_deg),
    RIG_FLOAT_FIELD(leg_posture_sway),
    RIG_FLOAT_FIELD(leg_dive_trail_deg),
    RIG_FLOAT_FIELD(foot_hang_deg),
    RIG_FLOAT_FIELD(toe_curl_deg),
    RIG_FLOAT_FIELD(foot_follow),
};
#undef RIG_FLOAT_FIELD
#undef RIG_INT_FIELD

}  // namespace

bool save_rig_tuning(const RigTuning& tuning, const char* path) {
    std::ofstream output(path);
    if (!output) {
        LOG_ERROR("could not write rig tuning '%s'", path);
        return false;
    }

    output << "# dragon rig tuning\n";
    output << std::setprecision(6);
    for (const RigField& field : RIG_FIELDS) {
        output << field.name << ' ';
        if (field.float_member != nullptr) {
            output << tuning.*field.float_member;
        } else {
            output << tuning.*field.int_member;
        }
        output << '\n';
    }
    if (!output) {
        LOG_ERROR("could not finish rig tuning '%s'", path);
        return false;
    }
    LOG_INFO("saved rig tuning -> %s", path);
    return true;
}

bool load_rig_tuning(RigTuning& tuning, const char* path) {
    std::ifstream input(path);
    if (!input) return false;

    int applied = 0;
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream parser(line);
        std::string key;
        float value = 0.0f;
        if (!(parser >> key >> value) || !std::isfinite(value)) continue;

        for (const RigField& field : RIG_FIELDS) {
            if (key != field.name) continue;
            if (field.float_member != nullptr) {
                tuning.*field.float_member = value;
            } else {
                tuning.*field.int_member = static_cast<int>(value);
            }
            ++applied;
            break;
        }
    }
    LOG_INFO("loaded %d rig tuning values from %s", applied, path);
    return applied > 0;
}

// ---------------------------------------------------------------- rig

void DragonRig::set_model_scale(float metres_per_unit) {
    model_scale_ = metres_per_unit > 1e-6f ? metres_per_unit : 1.0f;
    // Rest positions are cached in metres, so the chains must be rebuilt.
    if (skeleton_) {
        setup_chain(tail_sim_, joints_.tail);
        std::vector<int> neck_with_head = joints_.neck;
        if (joints_.head != NO_PARENT) neck_with_head.push_back(joints_.head);
        setup_chain(neck_sim_, neck_with_head);
    }
}

void DragonRig::init(const Skeleton& skeleton, const DragonJoints& joints) {
    skeleton_ = &skeleton;
    joints_ = joints;
    pose_.reset_to_bind(skeleton);
    // A clip belongs to the skeleton it was authored on. The rig is
    // re-initialised when the player cycles onto another model, and the
    // previous model's idle used to survive that: on the ground the authored
    // stance wins the whole body, so a wyvern landed after the default asset
    // stood in the default asset's pose sampled by joint INDEX -- a reverted
    // stance and a jaw rotating about whatever that index happened to be.
    // The caller sets a clip again if the new model has one.
    base_clip_ = nullptr;
    clip_hold_time_ = -1.0f;
    clip_time_ = 0.0f;
    action_ = RigAction{};
    spit_time_ = bite_time_ = claw_time_ = tail_time_ = 1e9f;
    breath_smoothed_ = jaw_open_ = 0.0f;

    // Accumulate world bind rotations, then keep each joint's parent's inverse.
    // A local rotation is expressed in the parent's frame, so that inverse is
    // what turns a body-space axis into a usable one.
    const int count = skeleton.count();
    std::vector<Quat> world_rotation(size_t(count), Quat::identity());
    parent_bind_inverse_.assign(size_t(count), Quat::identity());
    for (int i = 0; i < count; ++i) {
        const Quat local = skeleton.joint(i).local_bind.rotation;
        const int parent = skeleton.joint(i).parent;
        world_rotation[size_t(i)] =
            parent == NO_PARENT ? core::normalize(local)
                                : core::normalize(world_rotation[size_t(parent)] * local);
        parent_bind_inverse_[size_t(i)] =
            parent == NO_PARENT ? Quat::identity()
                                : core::conjugate(world_rotation[size_t(parent)]);
    }

    // Leverage of each wing joint over the wingtip, measured once from the bind
    // pose. The representative path is the shared root followed by the longest
    // finger, which is the same walk drive_wings normalizes over. The reference
    // point is one bone-length beyond the last joint, because the membrane
    // carries on past it -- taking the last joint's own origin would say the
    // outermost joint has no leverage at all, when what it actually moves is
    // the membrane hanging off it.
    for (int side = 0; side < 2; ++side) {
        wing_flap_leverage_[side].clear();
        std::vector<int> path = joints.wing_root[side];
        const std::vector<int>* longest = nullptr;
        for (const std::vector<int>& finger : joints.wing_fingers[side]) {
            if (!longest || finger.size() > longest->size()) longest = &finger;
        }
        if (longest) path.insert(path.end(), longest->begin(), longest->end());
        if (path.size() < 2) continue;

        std::vector<Vec3> position;
        position.reserve(path.size());
        for (const int joint : path) {
            position.push_back(skeleton.world_bind(joint).translation_part());
        }
        const Vec3 tip = position.back() + (position.back() - position[position.size() - 2]);
        // The moment arm is the part of the joint-to-tip vector PERPENDICULAR to
        // the flap axis, which is the body's fore-aft axis. Body up and the
        // model's own up coincide here and the facing yaw leaves Z on Z, so the
        // component to drop is simply z. Using the straight-line distance
        // instead credits a swept or bent wing with leverage it does not have
        // about that axis, and the normalizer then hands the outboard joints
        // too much of the stroke: the two generated assets reached only about
        // two thirds of the commanded angle while the straight generated rig
        // reached all of it.
        auto arm = [](Vec3 from, Vec3 to) {
            const Vec3 d = to - from;
            return core::length(Vec3{d.x, d.y, 0.0f});
        };
        const float span = arm(position.front(), tip);
        if (span < 1e-4f) continue;
        for (const Vec3& p : position) {
            wing_flap_leverage_[side].push_back(arm(p, tip) / span);
        }
    }

    // The axis each wing folds about: the normal of the plane fitted through
    // that wing's bind-pose joints. Body up is only the right hinge for a wing
    // bound level, and a sculpt whose membranes drape aft-down is not -- see
    // wing_fold_axis_. Undriven leaves below the mapped chains are included:
    // they are the real finger tips and they carry most of the membrane, so
    // leaving them out tilts the fit toward the arm.
    for (int side = 0; side < 2; ++side) {
        std::vector<bool> in_wing(size_t(count), false);
        for (const int joint : joints.wing_root[side]) in_wing[size_t(joint)] = true;
        for (const std::vector<int>& finger : joints.wing_fingers[side]) {
            for (const int joint : finger) in_wing[size_t(joint)] = true;
        }
        // Descendants inherit membership; joints are topologically sorted, so
        // one forward pass carries it all the way down.
        for (int i = 0; i < count; ++i) {
            const int parent = skeleton.joint(i).parent;
            if (parent != NO_PARENT && in_wing[size_t(parent)]) in_wing[size_t(i)] = true;
        }

        std::vector<Vec3> sample;
        for (int i = 0; i < count; ++i) {
            if (!in_wing[size_t(i)]) continue;
            const Vec3 p = skeleton.world_bind(i).translation_part();
            // Helpers that bind at the model origin carry no position -- the
            // default asset's shoulder chain is two of them -- and would drag
            // the fit toward the body centre.
            if (core::length_sq(p) < 1e-4f) continue;
            sample.push_back(p);
        }
        if (sample.size() < 3) continue;

        Vec3 centre = Vec3::zero();
        for (const Vec3& p : sample) centre = centre + p;
        centre = centre * (1.0f / float(sample.size()));
        float cov[3][3] = {};
        for (const Vec3& p : sample) {
            const Vec3 d = p - centre;
            const float v[3] = {d.x, d.y, d.z};
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) cov[r][c] += v[r] * v[c];
            }
        }
        // The plane normal is the smallest eigenvector of the covariance.
        // Iterating (trace*I - C) amplifies exactly that one, which is enough
        // here and saves carrying a 3x3 eigensolver for one call site. Seeded
        // with body up so a wing that really is level keeps the old axis.
        const float trace = cov[0][0] + cov[1][1] + cov[2][2];
        Vec3 normal = Vec3::unit_y();
        for (int iteration = 0; iteration < 32; ++iteration) {
            const Vec3 n = normal;
            normal = Vec3{trace * n.x - (cov[0][0] * n.x + cov[0][1] * n.y + cov[0][2] * n.z),
                          trace * n.y - (cov[1][0] * n.x + cov[1][1] * n.y + cov[1][2] * n.z),
                          trace * n.z - (cov[2][0] * n.x + cov[2][1] * n.y + cov[2][2] * n.z)};
            if (core::length_sq(normal) < 1e-12f) {
                normal = Vec3::unit_y();
                break;
            }
            normal = core::normalize(normal);
        }
        // Up, not down: the fold's sign convention is written for an axis that
        // points the same way body up does.
        wing_fold_axis_[side] = normal.y < 0.0f ? normal * -1.0f : normal;
    }

    // Feet and toes, with depth below their root, for the hanging curl. The
    // *_end_* leaves are export artifacts with no skin weights; harmless to
    // rotate, cheaper to skip.
    foot_joints_.clear();
    for (const int root : joints.foot_roots) {
        for (int i = 0; i < count; ++i) {
            if (skeleton.joint(i).name.find("_end_") != std::string::npos) continue;
            int depth = 0;
            int parent = i;
            while (parent != NO_PARENT && parent != root) {
                parent = skeleton.joint(parent).parent;
                ++depth;
            }
            if (parent == root) foot_joints_.emplace_back(i, depth);
        }
        foot_joints_.emplace_back(root, 0);
    }

    // What the creature stands on, and the floor it stood on when bound. The
    // stance re-poses the legs and then lifts the root so the lowest of these
    // comes back to this height; the toes are included because a foot rotated
    // by the stance may put a toe below its root.
    stance_foot_joints_.clear();
    for (const auto& [joint, depth] : foot_joints_) stance_foot_joints_.push_back(joint);
    stance_floor_bind_ = 0.0f;
    for (size_t i = 0; i < stance_foot_joints_.size(); ++i) {
        const float y = skeleton.world_bind(stance_foot_joints_[i]).translation_part().y;
        stance_floor_bind_ = i == 0 ? y : core::minf(stance_floor_bind_, y);
    }

    // Re-anchor each foot to the nearest leg-chain end. The feet are IK targets
    // parented to the body: without this, posing the legs leaves every foot
    // nailed to its bind position in space. Matching by bind distance rather
    // than by name survives this asset's naming (Hand_* for the front feet).
    // Helpers binding at the origin carry no meaningful anchor and are skipped.
    foot_attach_.clear();
    std::vector<int> anchors;
    for (int side = 0; side < 2; ++side) {
        if (!joints.leg[side].empty()) anchors.push_back(joints.leg[side].back());
        if (!joints.front_leg[side].empty()) anchors.push_back(joints.front_leg[side].back());
    }
    for (const int foot : joints.foot_roots) {
        const Vec3 foot_position = skeleton.world_bind(foot).translation_part();
        if (core::length_sq(foot_position) < 1e-4f || anchors.empty()) continue;
        // A foot that already hangs off its leg needs no re-anchoring -- the
        // hierarchy carries it. Only the body-parented IK-target kind does.
        bool on_a_leg = false;
        for (int p = skeleton.joint(foot).parent; p != NO_PARENT; p = skeleton.joint(p).parent) {
            for (const int anchor : anchors) on_a_leg = on_a_leg || anchor == p;
        }
        if (on_a_leg) continue;
        int best = anchors.front();
        float best_distance = 1e9f;
        for (const int anchor : anchors) {
            const float distance =
                core::distance(skeleton.world_bind(anchor).translation_part(), foot_position);
            if (distance < best_distance) {
                best_distance = distance;
                best = anchor;
            }
        }
        FootAttach attach;
        attach.foot = foot;
        attach.anchor = best;
        const Quat anchor_rotation = core::quat_from_matrix(skeleton.world_bind(best));
        const Quat foot_rotation = core::quat_from_matrix(skeleton.world_bind(foot));
        attach.offset = core::rotate(core::conjugate(anchor_rotation),
                                     foot_position -
                                         skeleton.world_bind(best).translation_part());
        attach.rotation = core::normalize(core::conjugate(anchor_rotation) * foot_rotation);
        foot_attach_.push_back(attach);
    }

    setup_chain(tail_sim_, joints.tail);
    // The head rides on the end of the neck, so it is simulated as part of it.
    std::vector<int> neck_with_head = joints.neck;
    if (joints.head != NO_PARENT) neck_with_head.push_back(joints.head);
    setup_chain(neck_sim_, neck_with_head);
    have_previous_ = false;

    compute_world_matrices(skeleton, pose_, world_);
    compute_skinning_matrices(skeleton, world_, skinning_);

    // Which way the model faces, from the bind pose: the head lies forward of
    // the tail on any creature. The same rule the app uses to yaw the asset
    // into the engine's -Z convention, so the two agree by construction.
    model_forward_z_ = -1.0f;
    {
        const int aft = !joints.tail.empty() ? joints.tail.back()
                        : (!joints.neck.empty() ? joints.neck.front() : NO_PARENT);
        if (joints.head != NO_PARENT && aft != NO_PARENT) {
            const float head_z = skeleton.world_bind(joints.head).translation_part().z;
            const float aft_z = skeleton.world_bind(aft).translation_part().z;
            model_forward_z_ = head_z > aft_z ? 1.0f : -1.0f;
        }
    }
    body_to_model_ = model_forward_z_ > 0.0f
                         ? Quat::from_axis_angle(Vec3::unit_y(), core::PI)
                         : Quat::identity();

    // Which way the head points: the model's forward axis, carried into the
    // head's local frame. A rest pose faces forward; measuring the direction to
    // a child bone instead picked this asset's skull bone, which points 33
    // degrees UP from the snout, and the aim then pitched the head down by
    // exactly that much.
    head_axis_local_ = Vec3{0.0f, 0.0f, model_forward_z_};
    if (joints.head != NO_PARENT) {
        const Quat head_world = core::quat_from_matrix(world_[size_t(joints.head)]);
        head_axis_local_ = core::normalize(
            core::rotate(core::conjugate(head_world), Vec3{0.0f, 0.0f, model_forward_z_}));
    }

    // Which way the jaw opens, measured rather than assumed: rotate it a little
    // about body X and see whether its tip drops. A jaw opens downward on any
    // creature; which local rotation does that is the rigger's business. The
    // tip is the descendant furthest FORWARD of the pivot -- the deepest one
    // on this asset is the tongue, which points back into the mouth and gave
    // the opposite answer.
    jaw_open_sign_ = 1.0f;
    if (joints.jaw != NO_PARENT) {
        int tip = joints.jaw;
        float tip_reach = -1e9f;
        const Vec3 pivot = world_[size_t(joints.jaw)].col[3].xyz();
        for (int i = 0; i < count; ++i) {
            int p = skeleton.joint(i).parent;
            while (p != NO_PARENT && p != joints.jaw) p = skeleton.joint(p).parent;
            if (p != joints.jaw) continue;
            const float reach = (world_[size_t(i)].col[3].xyz() - pivot).z * model_forward_z_;
            if (reach > tip_reach) {
                tip = i;
                tip_reach = reach;
            }
        }
        const Vec3 before = world_[size_t(tip)].col[3].xyz();
        rotate_joint(joints.jaw, Vec3::unit_x(), core::radians(10.0f));
        std::vector<core::Mat4> probe;
        compute_world_matrices(skeleton, pose_, probe);
        const Vec3 after = probe[size_t(tip)].col[3].xyz();
        if (after.y > before.y) jaw_open_sign_ = -1.0f;
        pose_.reset_to_bind(skeleton);
    }
}

void DragonRig::rotate_joint(int joint, Vec3 body_axis, float angle, bool onto_current) {
    if (joint == NO_PARENT || size_t(joint) >= pose_.local.size()) return;
    const Vec3 axis = core::rotate(parent_bind_inverse_[size_t(joint)], body_axis);
    // Composing onto the current pose keeps the authored clip's motion and adds
    // to it; starting from bind replaces the clip for joints the rig owns.
    Transform local =
        onto_current ? pose_.local[size_t(joint)] : skeleton_->joint(joint).local_bind;
    local.rotation = core::normalize(Quat::from_axis_angle(axis, angle) * local.rotation);
    pose_.local[size_t(joint)] = local;
}

void DragonRig::rotate_joint(int joint, Vec3 axis_a, float angle_a, Vec3 axis_b, float angle_b) {
    if (joint == NO_PARENT || size_t(joint) >= pose_.local.size()) return;
    const Quat& to_parent = parent_bind_inverse_[size_t(joint)];
    const Quat a = Quat::from_axis_angle(core::rotate(to_parent, axis_a), angle_a);
    const Quat b = Quat::from_axis_angle(core::rotate(to_parent, axis_b), angle_b);
    Transform local = skeleton_->joint(joint).local_bind;
    local.rotation = core::normalize(a * b * local.rotation);
    pose_.local[size_t(joint)] = local;
}

void DragonRig::drive_wings(const game::FlightState& state) {
    // The flight model already produced the wing angle, so the wing that is
    // drawn and the thrust that was generated cannot disagree. On top of it,
    // load flex: the wings bow upward under g, which is what makes a hard pull
    // look like it costs something. load_smoothed_ is maintained in update().
    const float flare = state.wing_brake;
    // Sweep and fold go AFT and twist is washout (leading edge down): both are
    // rotations whose sense depends on which way the model faces.
    const float aft = model_forward_z_;
    // The brake posture. Bank relief: a braking turn is a lean, not a sit-up,
    // so the roll input eases the pitch-up and the protraction and makes the
    // protraction asymmetric below. The root pitch rides on the gesture
    // layer's pitch, which drive_gestures() reset just before this and the
    // root application after drive_wings() reads (pitch + is nose up).
    const float bank = core::clampf(state.control.z, -1.0f, 1.0f);
    const float bank_relief =
        1.0f - core::saturate(tuning.brake_bank_relief) * std::fabs(bank);
    gesture_pitch_ += core::radians(tuning.brake_body_pitch_deg) * flare * bank_relief;
    // Past cruise the wings sweep back and part-fold on their own -- a stoop
    // is a shape speed makes, not only a button. The speed posture fills in
    // whatever the tuck has not already taken.
    const float speed_factor =
        core::smoothstep(tuning.sweep_speed_start, tuning.sweep_speed_full, state.airspeed);
    // Standing closes further than any tuck should: the stow is added here so
    // the flight angles stay free to be as open as a stoop needs.
    const float stow = ground_contact_ * standing_share(state.airspeed);
    // On the ground a species may hand the wing from the tuck to the stow --
    // a wyvern plants its wrists, and the stoop fold is the wrong start.
    const float tuck = state.wing_tuck *
                       core::lerpf(1.0f, core::saturate(tuning.ground_stow_tuck_share), stow);
    const float speed_share = speed_factor * (1.0f - tuck);
    const float sweep_deg = tuning.tuck_sweep_deg * tuck + tuning.speed_sweep_deg * speed_share -
                            tuning.load_forward_sweep_deg * core::maxf(load_smoothed_, 0.0f) +
                            tuning.ground_stow_sweep_deg * stow + gesture_wing_sweep_deg_;
    const float fold_deg = tuning.tuck_fold_deg * tuck + tuning.speed_fold_deg * speed_share +
                           tuning.ground_stow_fold_deg * stow;
    const float droop_deg =
        tuning.tuck_droop_deg * (tuck + speed_share * tuning.speed_sweep_deg /
                                            core::maxf(tuning.tuck_sweep_deg, 1.0f));
    float flap_angle = state.wing_angle;
    if (tuning.wing_flap_limit_deg > 0.0f) {
        const float limit = core::radians(tuning.wing_flap_limit_deg);
        flap_angle = core::clampf(flap_angle, -limit, limit);
    }
    // The raise is a posture term like the load flex, so the flap ceiling
    // does not eat it.
    const float non_flap_base_common =
        core::radians(tuning.wing_load_flex_deg) * load_smoothed_ - core::radians(droop_deg) +
        core::radians(tuning.brake_raise_deg) * flare;
    const float base = flap_angle + non_flap_base_common;
    // Membrane flutter: the outer wing buffets at speed and shudders in a
    // flare. Two incommensurate frequencies so it never reads as a metronome,
    // squared speed factor so cruise is calm and a dive is alive.
    const float flutter_speed = core::smoothstep(tuning.flutter_speed_start,
                                                 tuning.flutter_speed_start + 45.0f,
                                                 state.airspeed);
    const float flutter_amplitude =
        core::radians(tuning.flutter_deg) * flutter_speed * flutter_speed +
        core::radians(tuning.brake_buffet_deg) * flare;
    // Under g the tips wash out: leading edge down, shedding load outboard.
    const float twist =
        core::radians(tuning.load_twist_deg) * core::maxf(load_smoothed_, 0.0f) * aft;
    // Roll lean is deliberately NOT mirrored between sides: the same rotation
    // about the body's forward axis on both wings tips one up and one down,
    // which is exactly the shape that produces a roll. Positive control.z is
    // roll right, so the right wing (engine +X) goes down.
    const float roll_lean =
        core::radians(tuning.wing_roll_lean_deg) * state.control.z * model_forward_z_;
    // Upstroke flex: as the wing rises past ~20 degrees the wrist folds in --
    // real bird kinematics, and it keeps two raised wings from crossing over
    // the spine at the top of the beat.
    // Declared here, scaled by the recovery envelope once that is known below.
    float upstroke = core::smoothstep(core::radians(20.0f), core::radians(50.0f), base);
    // A positive delay means the outer joint is still on the previous part of
    // the beat while the shoulder has already changed direction. Reconstruct
    // only the cyclic component here; tuck, load flex and steering stay tied to
    // the current frame so the visible model remains in the same flight state.
    const bool phase_articulation = tuning.wing_phase_delay > 1e-5f &&
                                    state.flap_amplitude > 1e-4f;
    const float beat_strength = core::saturate(state.flap_amplitude) *
                                (1.0f - core::saturate(state.wing_tuck));
    // Everything keyed to the beat's phase or velocity -- the recovery flex,
    // the feathering twist, the stroke plane -- is gated on there being a beat
    // at all, so a glide, a tuck and the ground stow see none of it. It used
    // to be gated on the phase DELAY being enabled as well, which was an
    // accident of history: the delay is one thing the beat clock is used for,
    // not the switch for the others.
    const bool beat_active = beat_strength > 1e-4f;
    const bool recovery_active = beat_active && tuning.wing_recovery_extend_phase > 1e-5f;
    const float current_phase = wrapped_phase(state.flap_phase);
    const float downstroke_fraction =
        core::clampf(tuning.wing_downstroke_fraction, 0.05f, 0.95f);
    const float current_beat =
        phase_articulation ? wingbeat_curve(current_phase, downstroke_fraction) : 0.0f;
    // The stroke plane: how much of the wing's elevation becomes fore-aft
    // sweep at the shoulder. A yaw about body up, forward when the wing is
    // down, so the tip draws a tilted crescent instead of a vertical line.
    const float stroke_plane =
        std::tan(core::radians(core::clampf(tuning.stroke_plane_tilt_deg, -60.0f, 60.0f))) *
        beat_strength;
    // The recovery envelope: 0 through the downstroke, 1 mid-upstroke, 0 again
    // before the top. The flex comes in fast -- a bat reaches peak flexion a
    // fifth of the way into its upstroke -- holds through mid-recovery, and is
    // gone well before the top so the downstroke starts on a taut wing.
    auto recovery_envelope = [&](float phase) {
        if (!recovery_active) return 0.0f;
        const float compact_start = downstroke_fraction * 0.90f;
        const float compact_end = downstroke_fraction + (1.0f - downstroke_fraction) * 0.25f;
        const float extend_start =
            core::clampf(tuning.wing_recovery_extend_phase, downstroke_fraction, 0.99f);
        const float extend_end = extend_start + (1.0f - extend_start) * 0.60f;
        return beat_strength * core::smoothstep(compact_start, compact_end, phase) *
               (1.0f - core::smoothstep(extend_start, extend_end, phase));
    };
    // Mid-upstroke belongs to the phase-keyed flex. The two position-keyed
    // shapers below -- the upstroke fan fold and the shoulder cut -- are keyed
    // to elevation, which is the same going up as coming down, and left at
    // full strength they fought the flex: the shoulder cut hands the raised
    // wing's elevation to the wrist and fingers, which hooks the hand UP at
    // exactly the moment the recovery wants it hanging. They now yield while
    // the flex is in and return as it extends, so the wing flicks open at the
    // top and the raised membranes still stay clear of the spine there.
    const float recovery_now = recovery_envelope(current_phase);
    upstroke *= 1.0f - recovery_now;
    const float upstroke_fold = core::radians(tuning.upstroke_fold_deg) * upstroke;
    const float flap_fold = core::radians(tuning.wing_flap_fold_deg) * upstroke;

    for (int side = 0; side < 2; ++side) {
        const float sign = side == 0 ? 1.0f : -1.0f;
        // Protraction in the flare, less on the inside (low) wing of a banked
        // brake and more on the high one. Negative sweep is forward.
        const float protract_asymmetry =
            1.0f - 0.5f * core::saturate(tuning.brake_bank_relief) * bank * sign;
        const float side_sweep_deg =
            sweep_deg - tuning.brake_protract_deg * flare * bank_relief * protract_asymmetry;

        // Walk outward from the shoulder, then continue into every finger.
        //
        // Rotations down a chain ADD UP, so applying the flap angle at each bone
        // makes the total bend depend on how many bones the rig happens to have.
        // The generated rig has two per wing and the imported one has five, which
        // put the imported dragon's wings in a steep V at rest. Contributions are
        // normalized so the wingtip ends up rotated by the flap angle itself,
        // whatever the chain length.
        const size_t root_len = joints_.wing_root[side].size();
        size_t longest_finger = 1;
        for (const std::vector<int>& finger : joints_.wing_fingers[side]) {
            longest_finger = finger.size() > longest_finger ? finger.size() : longest_finger;
        }
        float decay_total = 0.0f;
        for (size_t k = 0; k < root_len + longest_finger; ++k) {
            decay_total += std::pow(tuning.outboard_decay, float(k));
        }
        const float normalize = decay_total > 1e-4f ? 1.0f / decay_total : 1.0f;

        // Sweep and fold need their OWN normalizers, and for a long time they
        // borrowed the flap's. That normalizer is 1 / sum of decay^k, which is
        // right only for shares that are themselves decay^k. The sweep's shares
        // are (0.4 + 0.6 * progress) and the fold's are progress * anatomical
        // scale -- neither decays -- so the tip received far more rotation than
        // was asked for: a commanded 88 degrees of tuck sweep arrived as 111 on
        // a three-plus-one wing and 131 on a four-plus-two. Past about 90 the
        // tip is rotated behind straight-back and the folded wing points
        // inboard at the opposite flank, which is why the two downloaded assets
        // put 71% and 99% of one wing's membrane inside the other's when
        // tucked. Each term is now divided by the sum of its own shares along
        // the representative chain, so a commanded angle is the angle the tip
        // actually closes through.
        //
        // `progress` runs 0..1 within each chain, so the walk is the shared
        // root followed by the longest finger -- the same path the flap
        // normalizes over.
        auto progress_at = [](size_t index, size_t length) {
            return length > 1 ? float(index) / float(length - 1) : 1.0f;
        };
        auto fold_share = [&](size_t index, size_t length, bool finger) {
            float share = progress_at(index, length);
            if (finger) {
                share *= core::maxf(tuning.wing_finger_fold_scale, 0.0f);
            } else if (index > 0) {
                const bool wrist = root_len >= 3 && index + 1 == root_len;
                share *= core::maxf(
                    wrist ? tuning.wing_wrist_fold_scale : tuning.wing_elbow_fold_scale, 0.0f);
            }
            return share;
        };
        // The fold normalizes over the progressive share ALONE, with the
        // anatomical scales left out. They stay absolute multipliers on top, so
        // closing the fingers harder does not quietly open the elbow to
        // compensate -- separating those three stations is the whole point of
        // the profile, and a shared normalizer would couple them back together.
        // A profile that leaves the scales at one therefore folds through
        // exactly the commanded angle, and one that opens the elbow folds
        // through less, which is what opening the elbow means.
        float sweep_total = 0.0f, fold_total = 0.0f;
        for (size_t i = 0; i < root_len; ++i) {
            sweep_total += 0.4f + 0.6f * progress_at(i, root_len);
            fold_total += progress_at(i, root_len);
        }
        for (size_t i = 0; i < longest_finger; ++i) {
            sweep_total += 0.4f + 0.6f * progress_at(i, longest_finger);
            fold_total += progress_at(i, longest_finger);
        }
        const float sweep_normalize = sweep_total > 1e-4f ? 1.0f / sweep_total : 1.0f;
        const float fold_normalize = fold_total > 1e-4f ? 1.0f / fold_total : 1.0f;
        // The flare has no progressive share at all -- every joint gets the
        // same angle -- so its total is simply the joint count.
        const float flare_normalize =
            1.0f / core::maxf(float(root_len + longest_finger), 1.0f);

        // On the upstroke the flap redistributes outboard: the humerus barely
        // elevates and the wrist leads, which is how a real bird raises its
        // wings -- and it is what keeps the two inner membranes from crossing
        // above the spine at the top of the beat. Total tip rotation is
        // unchanged; only the shape of the wing changes.
        const float shoulder_cut = 0.65f * upstroke;
        // `wing_phase_lag` attenuates each successive outboard joint, and for a
        // long time it was applied AFTER this normalizer instead of inside it,
        // so the tip reached only as much of the commanded angle as the lag
        // happened to leave.
        //
        // Both decay and lag are keyed to how far OUT the joint is, not to how
        // many joints precede it. Raising decay to the power of the joint index
        // means a wing described with more bones concentrates its flap further
        // inboard and the tip travels less -- the same shape, animated
        // differently, purely because of how the rigger subdivided the arm.
        // Adding the wyvern's outer finger ribs to the chain cost it a fifth of
        // its stroke that way. The station is distance from the shoulder as a
        // fraction of the span, scaled to a three-bone reference so the old
        // exponents keep their meaning.
        const std::vector<float>& leverage = wing_flap_leverage_[side];
        auto flap_leverage_at = [&](size_t k) {
            return k < leverage.size() ? leverage[k] : 0.0f;
        };
        auto flap_weight_at = [&](size_t k) {
            const float lag = 1.0f - core::minf(tuning.wing_phase_lag * float(k), 0.8f);
            return std::pow(tuning.outboard_decay, float(k)) *
                   (k < 2 ? 1.0f - shoulder_cut : 1.0f) * lag;
        };
        // Normalize by how far each joint actually MOVES the wingtip, not by
        // how much it rotates it. Summing the angles makes the tip bone's
        // orientation come out right while its position travels a fraction of
        // the arc: an outboard joint pivots close to the tip, so the same
        // degree of rotation there displaces the membrane far less than it does
        // at the shoulder -- and the upstroke's shoulder cut deliberately moves
        // the weight out to exactly where that leverage is worst. Between them
        // the visible stroke was 64% of the commanded angle on the generated
        // rig and less on a long imported wing, which is most of what read as a
        // shallow, stiff wingbeat. Leverage comes from the bind pose in init().
        float flap_total = 0.0f;
        for (size_t k = 0; k < root_len + longest_finger; ++k) {
            flap_total += flap_weight_at(k) * flap_leverage_at(k);
        }
        const float flap_normalize = flap_total > 1e-4f ? 1.0f / flap_total : 1.0f;

        int depth = 0;
        int finger_index = -1;  // -1 while walking the shared root
        const int finger_count = int(joints_.wing_fingers[side].size());
        auto apply = [&](int joint, int index_in_chain, int chain_length) {
            if (joint == NO_PARENT) return;
            const float delay = core::clampf(tuning.wing_phase_delay, 0.0f, 0.25f) *
                                float(depth);
            const float local_phase = wrapped_phase(current_phase - delay);
            const float delayed_beat =
                phase_articulation ? wingbeat_curve(local_phase, downstroke_fraction) : 0.0f;
            const float delayed_flap =
                phase_articulation
                    ? core::radians(tuning.flap_shoulder_deg) *
                          core::saturate(state.flap_amplitude) * (delayed_beat - current_beat)
                    : 0.0f;
            // The gesture layer's brace is a posture term like the load flex.
            float local_base = base + delayed_flap + gesture_wing_raise_[side];
            if (tuning.wing_flap_limit_deg > 0.0f) {
                // The ceiling applies to the complete delayed cyclic sample,
                // while load flex and droop remain additive posture terms.
                const float limit = core::radians(tuning.wing_flap_limit_deg);
                const float non_flap_base = non_flap_base_common + gesture_wing_raise_[side];
                const float cyclic_base =
                    core::clampf(local_base - non_flap_base, -limit, limit);
                local_base = cyclic_base + non_flap_base;
            }
            // Standing stow: the wrist rises, the fingers hang. Spread evenly
            // along each chain so the station named reaches the angle named,
            // and added to the flap rather than blended with it -- both are
            // elevation about the same fore-aft axis.
            const float stow_total = finger_index >= 0 ? tuning.ground_stow_finger_deg
                                                       : tuning.ground_stow_wrist_deg;
            const float stow_angle = core::radians(stow_total) * stow /
                                     core::maxf(float(chain_length), 1.0f);

            // Standing, the stow owns the elevation. The flight terms --
            // the wing angle, the load flex, the roll lean, the flare below
            // -- fade out with ground contact, because a standing wing must
            // not depend on what the flight model happens to report there:
            // the studio bench pinned the wing angle at 18 degrees and the
            // game supplies the 9-degree glide dihedral less a load flex
            // from a zero-g reading on the ground, and that 13-degree gap
            // was enough to turn a stow tuned on the bench into two raised
            // sails in the game. Now the bench and the game agree by
            // construction, and a profile's stow angles are the whole pose.
            const float flight_share = 1.0f - stow;
            const float flap_angle = (local_base * sign + roll_lean) *
                                         flap_weight_at(size_t(depth)) * flap_normalize *
                                         flight_share +
                                     stow_angle * sign;

            // Folding closes progressively toward the tip, which is how a wing
            // actually stows. A generated wing has shoulder -> elbow -> hand,
            // while an imported wing commonly has shoulder -> elbow -> wrist
            // followed by several finger chains, so the fold is scaled by the
            // anatomical station: a long finger chain can close without forcing
            // the elbow through the torso. The all-ones defaults leave the
            // original progressive profile.
            const float progress = progress_at(size_t(index_in_chain), size_t(chain_length));
            const float fold_progress =
                fold_share(size_t(index_in_chain), size_t(chain_length), finger_index >= 0);
            const float sweep = core::radians(side_sweep_deg) * sign * aft *
                                (0.4f + 0.6f * progress) * sweep_normalize;
            // Recovery compacts the outer wing first and then lets it reopen
            // before the phase wraps. Multiplying by the extension envelope
            // makes the timed term zero at both ends of the cycle, avoiding a
            // snap when phase goes from 1 back to 0. The depth weighting keeps
            // the shoulder broad while the wrist and fingers do the compacting.
            const float max_depth =
                float(std::max<size_t>(root_len + longest_finger, 1u) - 1u);
            const float recovery_depth =
                max_depth > 0.0f ? core::saturate(float(depth) / max_depth) : 0.0f;
            // The envelope every recovery term rides, sampled at this joint's
            // DELAYED phase so the flex travels outboard like the flap does.
            const float recovery = recovery_envelope(local_phase);
            const float recovery_fold =
                core::radians(tuning.wing_recovery_fold_deg) * recovery * recovery_depth;
            // Which hinge is this joint? The wrist is the last root joint on a
            // three-plus-bone arm; on a two-bone arm (the generated rig) the
            // finger bases sit at the wrist and play that part instead.
            const bool root_joint = finger_index < 0;
            const bool has_wrist_bone = root_len >= 3;
            const bool is_wrist_hinge =
                (root_joint && has_wrist_bone && size_t(index_in_chain + 1) == root_len) ||
                (!root_joint && !has_wrist_bone && index_in_chain == 0);
            const bool is_elbow_hinge = root_joint && index_in_chain > 0 && !is_wrist_hinge;
            const bool is_finger_rib = !root_joint && !is_wrist_hinge;
            // Ribs beyond the wrist share the finger fold evenly, so the tip
            // closes through the named angle whatever the rib count.
            const float rib_count = core::maxf(
                float(has_wrist_bone ? chain_length : chain_length - 1), 1.0f);
            // Hinges, positive aft like the fold. Not normalized and not
            // progressive: they are angles at named joints, the same way the
            // standing zigzag is, because that is what a flexing arm is. They
            // turn about the ARM'S up (body up carried through the posed
            // shoulder), not about the membrane plane's normal the tuck folds
            // about: on a sculpt whose membrane drapes 44 degrees, "aft in the
            // plane" is also "up", and the hand hooked skyward at mid-upstroke
            // by more than the droop below could bring it back. A wrist flexes
            // level with the arm; where the hand sits vertically is the droop's
            // job alone, so the two knobs stay independent on every asset.
            float recovery_hinge = 0.0f;
            if (is_elbow_hinge) recovery_hinge = core::radians(tuning.recovery_elbow_deg);
            if (is_wrist_hinge) recovery_hinge = core::radians(tuning.recovery_wrist_deg);
            if (is_finger_rib) recovery_hinge = core::radians(tuning.recovery_finger_deg) / rib_count;
            recovery_hinge *= recovery;
            // The hand droops below the arm on the recovery: an elevation
            // AGAINST the flap at the wrist, carried on down the ribs. The
            // wrist takes 40%, the ribs the rest between them.
            float droop_share = 0.0f;
            if (is_wrist_hinge) droop_share = 0.4f;
            if (is_finger_rib) droop_share = 0.6f / rib_count;
            const float recovery_droop =
                core::radians(tuning.recovery_droop_deg) * recovery * droop_share;
            // Feathering: leading edge down while the wing moves down, up
            // while it moves up, by the wing's speed. Same stations as the
            // droop, so the hand pitches as one surface and the arm stays bone.
            float twist_share = 0.0f;
            if (is_wrist_hinge) twist_share = 0.35f;
            if (is_finger_rib) twist_share = 0.65f / rib_count;
            const float stroke_twist =
                beat_active ? -core::radians(tuning.stroke_twist_deg) * beat_strength *
                                  wingbeat_rate(local_phase, downstroke_fraction) * aft *
                                  twist_share
                            : 0.0f;
            // The zigzag closure is NOT normalized and NOT progressive: it is
            // two opposed hinge angles at two named joints, which is what a
            // wing shutting actually is. Everything else here is a fan.
            float close = 0.0f;
            if (stow > 1e-4f && index_in_chain > 0 && finger_index < 0) {
                const bool is_wrist = root_len >= 3 &&
                                      size_t(index_in_chain + 1) == root_len;
                close = core::radians(tuning.ground_stow_close_deg) * stow *
                        (is_wrist ? 1.0f : -tuning.ground_stow_elbow_scale);
            }
            // The fan shuts: each rib swings toward the innermost one, by a
            // share of the full closure. Applied at the finger's BASE so the
            // whole rib and its membrane come with it.
            if (stow > 1e-4f && finger_index > 0 && index_in_chain == 0 &&
                finger_count > 1) {
                close -= core::radians(tuning.ground_stow_converge_deg) * stow *
                         float(finger_index) / float(finger_count - 1);
            }
            const float fold = (core::radians(fold_deg) + upstroke_fold * progress +
                                flap_fold + recovery_fold) *
                                   fold_progress * sign * aft * fold_normalize +
                               close * sign * aft;
            const float flare_angle = core::radians(tuning.brake_flare_deg) * flare * sign *
                                      flare_normalize * flight_share;

            // Flutter and twist live on the fingers only -- the arm is bone.
            // Flutter grows toward the tip (progress squared: the membrane
            // moves, the wrist barely), each finger on its own phase.
            float flutter = 0.0f;
            float finger_twist = 0.0f;
            if (finger_index >= 0) {
                const float phase = float(finger_index) * 1.9f + float(side) * 0.7f;
                flutter = flutter_amplitude * progress * progress *
                          (std::sin(core::TWO_PI * 13.0f * time_ + phase) +
                           0.6f * std::sin(core::TWO_PI * 21.7f * time_ + 1.7f * phase)) *
                          sign * normalize * flight_share;
                finger_twist = twist * progress * normalize * flight_share;
            }

            // Flap is a rotation about the body's forward axis; sweep and fold
            // are the same in-plane rotation about the wing's own hinge, which
            // is body up only for a wing bound level.
            rotate_joint(joint, wing_fold_axis_[side], sweep + fold, Vec3::unit_z(),
                         flap_angle - flare_angle + flutter - recovery_droop * sign);
            // Yaw about the arm's up, composed onto the posed joint so an
            // already-elevated wing swings fore and aft level with itself.
            // Positive is aft on either side of either facing. Two things
            // live here: the stroke plane at the shoulder alone -- the whole
            // wing yaws by a share of its elevation, forward when down, which
            // is what the shoulder of a flying animal does while the outboard
            // joints only ride -- and the recovery hinge at its own joint.
            float yaw_aft = recovery_hinge;
            if (depth == 0) {
                yaw_aft += (local_base - non_flap_base_common - gesture_wing_raise_[side]) * stroke_plane;
            }
            if (std::fabs(yaw_aft) > 1e-5f) {
                rotate_joint(joint, Vec3::unit_y(), yaw_aft * model_forward_z_ * sign, true);
            }
            const float total_twist = finger_twist + stroke_twist;
            if (std::fabs(total_twist) > 1e-5f) {
                rotate_joint(joint, Vec3::unit_x(), total_twist, true);
            }
            ++depth;
        };

        const std::vector<int>& root = joints_.wing_root[side];
        for (size_t i = 0; i < root.size(); ++i) apply(root[i], int(i), int(root.size()));

        // Every finger restarts from the shared root's depth, so they fold
        // together rather than fanning out unevenly.
        const int root_depth = depth;
        for (const std::vector<int>& finger : joints_.wing_fingers[side]) {
            depth = root_depth;
            ++finger_index;
            for (size_t i = 0; i < finger.size(); ++i) {
                apply(finger[i], int(i), int(finger.size()));
            }
        }
    }
}

// Which sign of a chain's steer.y sends it toward the body's RIGHT (+X).
// Measured on the generated rig by test_anim's gesture test; flip here if a
// future steer convention change moves it.
constexpr float GESTURE_YAW_SIGN = 1.0f;

namespace {
// Keep the loaded pose until the strike takes over. A separate sine bump
// returned to neutral BEFORE striking, making two unrelated nods. Smoothstep
// also gives zero endpoint velocity, so recovery does not stop at a wall.
float gesture_wind(float u, float a) {
    if (u < 0.0f || u >= 1.0f || a <= 1e-5f) return 0.0f;
    const float peak = a + (1.0f - a) * 0.3f;
    return core::smoothstep(0.0f, a, u) * (1.0f - core::smoothstep(a, peak, u));
}
float gesture_strike(float u, float a) {
    if (u < a || u >= 1.0f) return 0.0f;
    const float v = (u - a) / core::maxf(1.0f - a, 1e-3f);
    return v < 0.3f ? core::smoothstep(0.0f, 0.3f, v)
                    : 1.0f - core::smoothstep(0.3f, 1.0f, v);
}
// Overlap the extremities behind the torso, but still finish at u == 1.
float gesture_trail(float u, float a, float lag) {
    return gesture_strike((u - lag) / (1.0f - lag), a);
}
}  // namespace

// The rest of the animal answering a swing. Computed once per frame, before
// the wings, the body beat and the chains, which each add their share.
void DragonRig::drive_gestures() {
    gesture_sway_ = 0.0f;
    gesture_pitch_ = gesture_roll_ = gesture_yaw_ = gesture_surge_ = 0.0f;
    gesture_wing_raise_[0] = gesture_wing_raise_[1] = 0.0f;
    gesture_wing_sweep_deg_ = gesture_neck_pitch_deg_ = gesture_neck_yaw_deg_ = 0.0f;
    gesture_tail_yaw_deg_ = gesture_limb_rake_ = gesture_tail_whip_ = 0.0f;
    const float a = core::clampf(tuning.gesture_anticipation, 0.0f, 0.6f);
    const float body_pitch = core::radians(tuning.gesture_body_pitch_deg);
    const float body_roll = core::radians(tuning.gesture_body_roll_deg);
    const float body_yaw = core::radians(tuning.gesture_body_yaw_deg);
    const float wing = core::radians(tuning.gesture_wing_deg);

    // Bite: rear up, then down and forward into it, wings up then back.
    if (bite_time_ < tuning.bite_duration) {
        const float u = bite_time_ / core::maxf(tuning.bite_duration, 1e-3f);
        const float wind = gesture_wind(u, a);
        const float strike = gesture_strike(u, a);
        gesture_pitch_ += body_pitch * (0.4f * wind - strike);
        gesture_surge_ += tuning.gesture_surge_m * (strike - 0.3f * wind);
        gesture_wing_raise_[0] += wing * (wind - 0.3f * strike);
        gesture_wing_raise_[1] += wing * (wind - 0.3f * strike);
        gesture_wing_sweep_deg_ += tuning.gesture_wing_deg * gesture_trail(u, a, 0.08f);
        gesture_neck_pitch_deg_ += tuning.bite_lunge_deg * (0.45f * wind - strike);
    }
    // Claw: away, then roll and yaw into the strike side; the near wing
    // drops, the far one rises, the head dips to the mark, the tail balances.
    if (claw_time_ < tuning.claw_duration) {
        const float u = claw_time_ / core::maxf(tuning.claw_duration, 1e-3f);
        const float wind = gesture_wind(u, a);
        const float strike = gesture_strike(u, a);
        const float side = claw_side_;
        gesture_roll_ += body_roll * side * (strike - 0.4f * wind);
        gesture_yaw_ += body_yaw * side * (strike - 0.65f * wind);
        gesture_pitch_ -= body_pitch * 0.3f * strike;
        gesture_sway_ += tuning.gesture_sway_m * side * (strike - 0.3f * wind);
        gesture_surge_ += tuning.gesture_surge_m * 0.35f * (strike - 0.3f * wind);
        const int near = side >= 0.0f ? 0 : 1;
        const float brace = gesture_trail(u, a, 0.08f) - 0.35f * wind;
        gesture_wing_raise_[near] -= wing * brace;
        gesture_wing_raise_[1 - near] += wing * 0.6f * brace;
        gesture_neck_yaw_deg_ += tuning.gesture_neck_deg * side * strike;
        gesture_neck_pitch_deg_ -= tuning.gesture_neck_deg * 0.5f * strike;
        gesture_tail_yaw_deg_ -= tuning.gesture_tail_counter_deg * side *
                                 (gesture_trail(u, a, 0.12f) - 0.4f * wind);
        gesture_limb_rake_ = gesture_trail(u, a, 0.04f) - 0.22f * wind;
    }
    // Tail: coil toward the mark, then the body counter-turns as the tail
    // whips across; the head swings the other way, the near wing dips.
    if (tail_time_ < tuning.tail_duration) {
        const float u = tail_time_ / core::maxf(tuning.tail_duration, 1e-3f);
        const float wind = gesture_wind(u, a);
        const float strike = gesture_strike(u, a);
        const float side = tail_side_;
        gesture_yaw_ += body_yaw * 1.6f * side * (0.65f * wind - strike);
        gesture_roll_ += body_roll * 0.6f * side * strike;
        gesture_neck_yaw_deg_ -= tuning.gesture_neck_deg * side * strike;
        const int near = side >= 0.0f ? 0 : 1;
        gesture_wing_raise_[near] -= wing * 0.6f * strike;
        gesture_wing_raise_[1 - near] += wing * 0.4f * strike;
        gesture_tail_whip_ = side * (gesture_trail(u, a, 0.12f) - 0.5f * wind);
    }
}

void DragonRig::drive_body_beat(const game::FlightState& state) {
    // The body rises on the downstroke and sinks on the recovery, and the nose
    // lifts a little with each push. Visual only, on the root joint: the
    // flight model's position is the truth the camera and the combat read, and
    // a chase camera that bobbed with every beat would be unwatchable. Gated
    // on the beat and on being airborne, so a standing or gliding dragon is
    // exactly where the flight model put it.
    const int root = joints_.root;
    if (root == NO_PARENT || size_t(root) >= pose_.local.size()) return;
    const float beat = core::saturate(state.flap_amplitude) *
                       (1.0f - core::saturate(state.wing_tuck)) * (1.0f - ground_contact_);
    if (beat <= 1e-4f) return;
    const float down = core::clampf(tuning.wing_downstroke_fraction, 0.05f, 0.95f);
    const float phase = wrapped_phase(state.flap_phase);
    // wingbeat_curve is +1 with the wing at the top, so its negative is the
    // body's height: lowest as the downstroke begins, highest after it ends.
    // The lag is the body still rising on its momentum when the wing reverses.
    const float heave = -wingbeat_curve(phase - tuning.beat_heave_lag, down);
    const float pitch = -wingbeat_curve(phase - 0.5f * tuning.beat_heave_lag, down);
    // Root translation is in the model's own units; the tuning is in metres.
    Transform& local = pose_.local[size_t(root)];
    local.position += Vec3::unit_y() * (tuning.beat_heave_m * heave * beat / model_scale_);
    // Nose up in engine terms is a rotation about model X whose sign follows
    // the facing, the same convention the neck and tail steer use.
    rotate_joint(root, Vec3::unit_x(),
                 core::radians(tuning.beat_pitch_deg) * pitch * beat * -model_forward_z_, true);
}

void DragonRig::setup_chain(ChainDynamics& sim, const std::vector<int>& chain) const {
    sim = ChainDynamics();
    if (chain.size() < 2 || !skeleton_) return;
    // Simulated in metres, so the forces and the geometry agree. Only the
    // resulting directions are used, so the choice of unit does not otherwise
    // matter -- but mixing two of them does.
    for (const int joint : chain) {
        sim.rest.push_back(skeleton_->world_bind(joint).translation_part() * model_scale_);
    }
    sim.position = sim.rest;
    sim.velocity.assign(sim.rest.size(), Vec3::zero());
    sim.segment.assign(sim.rest.size(), 0.0f);
    for (size_t i = 1; i < sim.rest.size(); ++i) {
        sim.segment[i] = core::distance(sim.rest[i - 1], sim.rest[i]);
    }
    sim.initialized = true;
}

void DragonRig::rotate_joint_quat(int joint, const Quat& delta, const Quat& parent_extra) {
    if (joint == NO_PARENT || size_t(joint) >= pose_.local.size()) return;
    // `delta` is a world-space rotation to add at this joint, but the ancestors
    // have already been rotated by `parent_extra`. Expressing delta in the
    // pre-rotation frame keeps the two from compounding twice.
    const Quat in_parent_frame =
        core::normalize(core::conjugate(parent_extra) * delta * parent_extra);
    // parent_bind_inverse_ maps a body-space axis into the parent's frame;
    // conjugating the whole rotation by it is the quaternion equivalent of what
    // rotate_joint does with a single axis.
    const Quat body_to_parent = parent_bind_inverse_[size_t(joint)];
    const Quat delta_parent =
        core::normalize(body_to_parent * in_parent_frame * core::conjugate(body_to_parent));

    Transform local = skeleton_->joint(joint).local_bind;
    local.rotation = core::normalize(delta_parent * local.rotation);
    pose_.local[size_t(joint)] = local;
}

void DragonRig::drive_chain(ChainDynamics& sim, const std::vector<int>& chain,
                            const game::FlightState& state, Vec3 frame_acceleration,
                            Vec3 angular_acceleration, core::Vec2 steer_deg, ChainFeel feel,
                            float dt) {
    if (!sim.initialized || chain.size() < 2) return;

    // Muscle tone: the animal tenses with the manoeuvre. Damping rises with the
    // square root of the same factor, keeping the response near critically
    // damped instead of increasingly ringy as it stiffens.
    const float tone = 1.0f + tuning.chain_tone * intensity_smoothed_;
    const float base_stiffness = tuning.chain_stiffness * feel.stiffness * tone;
    const float base_damping =
        tuning.chain_damping * feel.damping * std::sqrt(feel.stiffness * tone);
    // Taper toward the tip: the same load bends the end more than the base,
    // so the chain curves instead of pivoting. Damping follows the square
    // root of stiffness to stay near critical along the whole length.
    const float segments_total = float(std::max<size_t>(sim.position.size(), 2) - 1);
    auto taper_at = [&](size_t i) {
        return core::lerpf(1.0f, core::clampf(feel.tip_stiffness, 0.05f, 1.0f),
                           float(i) / segments_total);
    };

    // Active steering: curl the chain's target shape. The full deflection is
    // spread down the chain, each segment rotated a little more than the one
    // before it, because that is how a tail moves -- it curves, it does not
    // hinge at the root like a door. The spring then pulls the simulation
    // toward the curled shape, so the deflection eases in, overshoots and
    // settles exactly like every passive motion -- one integrator, one look.
    std::vector<Vec3> target = sim.rest;
    if (std::fabs(steer_deg.x) > 1e-3f || std::fabs(steer_deg.y) > 1e-3f) {
        const float segments = float(target.size() - 1);
        const Quat per_segment =
            core::Quat::from_axis_angle(Vec3::unit_y(), core::radians(steer_deg.y) / segments) *
            core::Quat::from_axis_angle(Vec3::unit_x(), core::radians(steer_deg.x) / segments);
        Quat cumulative = Quat::identity();
        for (size_t i = 1; i < target.size(); ++i) {
            cumulative = core::normalize(per_segment * cumulative);
            target[i] = target[i - 1] + core::rotate(cumulative, sim.rest[i] - sim.rest[i - 1]);
        }
    }

    const Vec3 omega = state.angular_velocity;
    // The per-vertebra bend limit is normalised to a four-segment chain: a
    // seven-segment tail with the same per-joint limit could take its whole
    // bend in two joints and did -- a hinge at the base with a straight boom
    // beyond it, which read as robotic. The same total bend, spread along the
    // length.
    const float per_joint_bend = core::radians(tuning.chain_max_bend_deg) *
                                 core::minf(1.0f, 4.0f / float(std::max<size_t>(sim.position.size(), 2) - 1));

    // Gravity, expressed in the dragon's frame.
    const Vec3 gravity_local =
        core::rotate(core::conjugate(state.orientation), Vec3{0.0f, -9.81f, 0.0f}) *
        (tuning.chain_gravity * feel.gravity);

    // Airflow in the dragon's frame, for drag. A tail streams backwards at speed
    // for the same reason a windsock does.
    const Vec3 airflow_local = core::rotate(core::conjugate(state.orientation), -state.velocity);

    const size_t count = sim.position.size();
    for (size_t i = 1; i < count; ++i) {  // index 0 is pinned to the body
        const Vec3 r = sim.position[i];
        const Vec3 v = sim.velocity[i];

        // Pseudo-forces of a rotating, accelerating reference frame. These are
        // what a tail actually feels, and they are why it swings outward in a
        // turn rather than merely lagging.
        const Vec3 centrifugal = -core::cross(omega, core::cross(omega, r));
        const Vec3 euler = -core::cross(angular_acceleration, r);
        const Vec3 coriolis = -2.0f * core::cross(omega, v);
        const Vec3 linear = -frame_acceleration;

        // Axial inertial force is mostly suppressed: transverse forces bend a
        // spine, axial compression only buckles it, and muscle resists exactly
        // that. Without this a braking dragon's neck folded under its chest.
        const Vec3 along_chain =
            core::normalize_or(sim.position[i] - sim.position[i - 1], Vec3::forward());
        Vec3 inertial = (centrifugal + euler + coriolis + linear) *
                        (tuning.chain_inertia * feel.inertia);
        const Vec3 axial = along_chain * core::dot(inertial, along_chain);
        inertial += axial * (tuning.chain_axial_response - 1.0f);

        Vec3 acceleration = gravity_local + inertial;

        // Load tone: the muscle holding this point tenses with what it carries.
        const float load_tone =
            1.0f + core::length(inertial) / core::maxf(tuning.chain_load_tone_accel, 0.1f);

        // Spring back toward the (possibly steered) target shape, so the chain
        // has a shape to return to rather than dangling.
        const float taper = taper_at(i);
        acceleration += (target[i] - r) * (base_stiffness * taper * load_tone);
        acceleration -= v * (base_damping * std::sqrt(taper * load_tone));

        // Soft joint limit. The hard bend clamp below is a wall, and a tail tip
        // arriving at a wall at 25 m/s stops in one frame -- the "abrupt"
        // tail. Muscle and ligament resist progressively instead: from half
        // the allowed bend the joint feels a restoring pull toward the
        // straightened position and its velocity into the limit is damped,
        // so the clamp is only ever reached slowly, if at all.
        if (i >= 2) {
            const Vec3 previous_dir =
                core::normalize_or(sim.position[i - 1] - sim.position[i - 2], along_chain);
            const Vec3 target_previous = core::normalize_or(
                target[i - 1] - target[i - 2], previous_dir);
            const Vec3 target_direction = core::normalize_or(target[i] - target[i - 1], along_chain);
            const float rest_bend = std::acos(core::clampf(
                core::dot(target_previous, target_direction), -1.0f, 1.0f));
            const float bend = std::acos(core::clampf(core::dot(previous_dir, along_chain), -1.0f, 1.0f));
            const float excess = bend - rest_bend;
            const float soft_start = 0.5f * per_joint_bend;
            if (excess > soft_start && per_joint_bend > 1e-4f) {
                const float over = core::saturate((excess - soft_start) / (per_joint_bend - soft_start));
                // Where this point would sit with the excess reduced to the
                // soft start: rotate the segment back toward the previous one.
                const Vec3 axis = core::cross(along_chain, previous_dir);
                Vec3 eased_dir = along_chain;
                if (core::length_sq(axis) > 1e-8f) {
                    eased_dir = core::rotate(core::Quat::from_axis_angle(core::normalize(axis),
                                                                          excess - soft_start),
                                             along_chain);
                }
                const Vec3 eased = sim.position[i - 1] + eased_dir * sim.segment[i];
                const Vec3 correction = eased - r;
                acceleration += correction * (tuning.chain_limit_stiffness * over);
                const Vec3 n = core::normalize_or(correction, Vec3::zero());
                const float into = core::dot(v, n);
                if (into < 0.0f) acceleration -= n * into * (tuning.chain_limit_damping * over);
            }
        }

        // Drag against the relative airflow: a linear term that damps slow
        // motion plus the physical v^2 term. Slender-body drag acts across the
        // chain, not along it -- flow along a neck pointed into the wind
        // produces almost nothing, flow across a hanging tail is what aligns
        // it. Without the decomposition, cruise airflow along the neck was a
        // 9 m/s^2 force folding it backwards. The quadratic normal term is why
        // the tail hangs at a hover and pulls dead straight in a dive without
        // either posture being authored.
        const Vec3 relative = airflow_local - v;
        const Vec3 along = along_chain;
        const Vec3 normal_flow = relative - along * core::dot(relative, along);
        const Vec3 axial_flow = relative - normal_flow;
        // The v^2 term only where it is restoring. A segment pointing
        // downstream (the tail) is aerodynamically stable and the flow
        // straightens it; one pointing upstream (the neck) is the arrow flying
        // backwards -- unstable, and left to physics it flutters metres wide at
        // dive speed. A real animal holds an upstream limb with muscle, so the
        // destabilizing aero is suppressed rather than simulated.
        const float downstream =
            core::saturate(core::dot(core::normalize_or(relative, Vec3::zero()), along)) *
            feel.aero;
        acceleration += normal_flow * (tuning.chain_drag + downstream *
                                       core::length(normal_flow) * tuning.chain_drag_v2);
        // A sliver of axial drag for damping; a real slender body has ~10x less.
        acceleration += axial_flow * tuning.chain_drag * 0.1f;

        // Clamp before integrating: pseudo-forces grow with the square of
        // angular velocity, so a tumble would otherwise be unbounded.
        const float acceleration_magnitude = core::length(acceleration);
        if (acceleration_magnitude > tuning.chain_max_acceleration) {
            acceleration *= tuning.chain_max_acceleration / acceleration_magnitude;
        }

        Vec3 next_velocity = v + acceleration * dt;
        const float speed = core::length(next_velocity);
        if (speed > tuning.chain_max_speed) {
            next_velocity *= tuning.chain_max_speed / speed;
        }
        if (!std::isfinite(next_velocity.x) || !std::isfinite(next_velocity.y) ||
            !std::isfinite(next_velocity.z)) {
            next_velocity = Vec3::zero();
        }

        sim.velocity[i] = next_velocity;
        sim.position[i] = r + next_velocity * dt;
        if (!std::isfinite(sim.position[i].x) || !std::isfinite(sim.position[i].y) ||
            !std::isfinite(sim.position[i].z)) {
            sim.position[i] = sim.rest[i];
            sim.velocity[i] = Vec3::zero();
        }
    }

    // The root of the chain never moves relative to the body.
    sim.position[0] = sim.rest[0];
    sim.velocity[0] = Vec3::zero();

    // Constraints: keep the segments their original length, and stop the chain
    // folding back through itself.
    for (int iteration = 0; iteration < tuning.chain_iterations; ++iteration) {
        // The articulation range is a TOTAL budget spent walking out the chain,
        // not a per-segment allowance -- per-segment, seven neck links at 30
        // degrees each still folded the head 169 degrees backwards.
        float range_budget = core::radians(feel.range_deg);
        for (size_t i = 1; i < count; ++i) {
            Vec3 direction = sim.position[i] - sim.position[i - 1];
            const float length = core::length(direction);
            if (length < 1e-5f) {
                direction = core::normalize_or(sim.rest[i] - sim.rest[i - 1], Vec3::forward());
            } else {
                direction = direction / length;
            }

            {
                // Range of motion: clamp against the (steered) target shape,
                // spending the shared budget. This is the muscle's hard limit --
                // whatever kick or attractor the dynamics find, the head cannot
                // deviate further from the rest line than the whole chain's
                // budget allows.
                const Vec3 target_direction = core::normalize_or(
                    target[i] - target[i - 1], direction);
                const float deviation = std::acos(core::clampf(
                    core::dot(target_direction, direction), -1.0f, 1.0f));
                if (deviation > range_budget) {
                    const Vec3 axis = core::cross(target_direction, direction);
                    if (core::length_sq(axis) > 1e-8f) {
                        direction = core::rotate(
                            core::Quat::from_axis_angle(core::normalize(axis), range_budget),
                            target_direction);
                    } else {
                        direction = target_direction;
                    }
                    // Kill the velocity that drove past the limit, or the
                    // spring fights a phantom momentum forever -- but only the
                    // component into the clamp; halving everything stopped the
                    // whole tail dead in one frame.
                    const Vec3 clamp_normal = core::normalize_or(
                        (sim.position[i - 1] + direction * sim.segment[i]) - sim.position[i],
                        Vec3::zero());
                    const float into = core::dot(sim.velocity[i], clamp_normal);
                    if (into < 0.0f) sim.velocity[i] -= clamp_normal * into;
                    range_budget = 0.0f;
                } else {
                    range_budget -= deviation;
                }
#ifdef CHAIN_CLAMP_DEBUG
                if (i == 1) {
                    const Vec3 rest_dir =
                        core::normalize_or(sim.rest[i] - sim.rest[i - 1], Vec3::forward());
                    const Vec3 target_dir =
                        core::normalize_or(target[i] - target[i - 1], rest_dir);
                    const float target_off = core::degrees(std::acos(core::clampf(
                        core::dot(target_dir, rest_dir), -1.0f, 1.0f)));
                    if (target_off > 20.0f) {
                        std::printf("TARGET OFF seg1: %.1f deg  steer=(%.1f, %.1f)\n",
                                    target_off, steer_deg.x, steer_deg.y);
                    }
                }
#endif
            }

            if (i >= 2) {
                // Limit the angle against the previous segment -- on top of
                // whatever bend the (steered) rest shape already has there.
                // An absolute limit fought this asset's own resting neck,
                // whose vertebrae sit at 27-29 degrees to each other: it
                // straightened the neck at rest and clamped away most of any
                // steer, which read as a stiff neck no slider could fix.
                const Vec3 previous = core::normalize_or(
                    sim.position[i - 1] - sim.position[i - 2], direction);
                const Vec3 target_previous = core::normalize_or(
                    target[i - 1] - target[i - 2], previous);
                const Vec3 target_direction = core::normalize_or(
                    target[i] - target[i - 1], direction);
                const float rest_bend = std::acos(core::clampf(
                    core::dot(target_previous, target_direction), -1.0f, 1.0f));
                // Hard safety net behind the soft limit above: a tail that
                // still gets here does so slowly.
                const float allowed = rest_bend + per_joint_bend;
                if (core::dot(previous, direction) < std::cos(allowed)) {
                    // Rotate the direction back toward the previous segment
                    // until it is inside the cone.
                    const Vec3 axis = core::cross(previous, direction);
                    if (core::length_sq(axis) > 1e-8f) {
                        direction = core::rotate(
                            core::Quat::from_axis_angle(core::normalize(axis), allowed),
                            previous);
                    } else {
                        direction = previous;
                    }
                }
            }
            sim.position[i] = sim.position[i - 1] + direction * sim.segment[i];
        }
    }

    // ---- turn the simulated shape back into joint rotations ----
    //
    // Walk outward, tracking the rotation already applied to the ancestors so
    // each joint only has to account for its own segment.
    Quat accumulated = Quat::identity();
    for (size_t i = 0; i + 1 < count; ++i) {
        const Vec3 bind_direction =
            core::normalize_or(sim.rest[i + 1] - sim.rest[i], Vec3::forward());
        const Vec3 current = core::rotate(accumulated, bind_direction);
        const Vec3 target =
            core::normalize_or(sim.position[i + 1] - sim.position[i], current);

        const Quat delta = core::rotation_between(current, target);
        rotate_joint_quat(chain[i], delta, accumulated);
        accumulated = core::normalize(delta * accumulated);
    }
}

void DragonRig::drive_legs(const game::FlightState& state, Vec3 frame_acceleration,
                           Vec3 angular_acceleration, float dt) {
    // Tucked in flight, extended for landing. Anticipates by extending as the
    // ground gets close rather than waiting for contact.
    const float wants_extend =
        state.grounded || state.ground_clearance < 25.0f ? 1.0f : 0.0f;
    leg_extend_ = core::damp(leg_extend_, wants_extend, 0.22f, dt);

    // The pendulum: each leg hangs in the effective gravity of the dragon's
    // frame -- true gravity plus the pseudo-forces of rotation and acceleration
    // at the hip -- held toward its pose by a muscle spring. Standing-in-a-bus
    // physics: braking floats the legs forward, a turn slings them outward.
    const Vec3 gravity_local =
        core::rotate(core::conjugate(state.orientation), Vec3{0.0f, -9.81f, 0.0f});
    const Vec3 omega = state.angular_velocity;
    const float max_swing = core::radians(tuning.leg_sway_max_deg);

    const float airborne = 1.0f - leg_extend_;
    // Braking is a transition toward landing posture: let the knees unfold a
    // little and give the whole limb a forward float. Both are model-scoped so
    // an imported quadruped can stay clear of its torso without changing the
    // generated dragon's established pose.
    const float brake = core::saturate(state.wing_brake);
    const float dive = core::saturate(state.wing_tuck);
    const float posture_sway = core::lerpf(1.0f, core::saturate(tuning.leg_posture_sway),
                                           core::maxf(brake, dive));
    const float brake_extend = core::saturate(tuning.leg_brake_extend) * brake;
    const float flight_fold = airborne * (1.0f - brake_extend);
    const float tuck_angle = core::radians(tuning.leg_tuck_deg) * flight_fold;
    const float brake_forward = core::radians(tuning.leg_brake_forward_deg) * brake * airborne;
    for (int side = 0; side < 2; ++side) {
        if (joints_.leg[side].empty()) continue;

        const Vec3 hip = skeleton_->world_bind(joints_.leg[side].front()).translation_part() *
                         model_scale_;
        const Vec3 centrifugal = -core::cross(omega, core::cross(omega, hip));
        const Vec3 euler = -core::cross(angular_acceleration, hip);
        const Vec3 effective =
            gravity_local + (centrifugal + euler - frame_acceleration) * tuning.leg_sway_response;

        // Where the pendulum would hang: angles of the effective gravity off
        // body-down. atan2 against the downward component keeps them stable
        // even when the frame briefly outweighs gravity.
        const float down = core::maxf(-effective.y, 3.0f);
        core::Vec2 desired{core::clampf(std::atan2(-effective.z, down), -max_swing, max_swing),
                           core::clampf(std::atan2(effective.x, down), -max_swing, max_swing)};
        // Planted feet do not swing.
        // A deliberate reach/stow braces the hip. Previously a full passive
        // swing added up to 40 degrees on top of the brake's forward reach,
        // while gravity pulled tucked dive legs back toward a vertical hang.
        // Filter the target, not the resulting pose, so releasing tuck/brake
        // keeps the spring's momentum instead of snapping the limb.
        desired = desired * (airborne * posture_sway);

        core::Vec2& swing = leg_swing_[side];
        core::Vec2& velocity = leg_swing_velocity_[side];
        velocity += (desired - swing) * (tuning.leg_sway_stiffness * dt);
        velocity = velocity * core::maxf(1.0f - tuning.leg_sway_damping * dt, 0.0f);
        swing += velocity * dt;
        if (!std::isfinite(swing.x) || !std::isfinite(swing.y)) {
            swing = core::Vec2{0.0f, 0.0f};
            velocity = core::Vec2{0.0f, 0.0f};
        }

        // The whole limb trails aft at the hip -- a flying quadruped presses its
        // legs back along the body, it does not dangle them like landing gear --
        // then the fold bends the knee, and the pendulum swing rides on top.
        // Everything composes onto the authored pose.
        // Trail backs off as the tuck deepens: fold plus full trail rotated the
        // thigh ~100 degrees in a dive, pointing the shin up and parking the
        // anchored feet above the wings. A stoop stows the legs under the
        // body, not rotated past it.
        const float trail = core::radians(tuning.leg_trail_deg) * flight_fold *
                            (1.0f - 0.7f * dive) +
                            core::radians(tuning.leg_dive_trail_deg) * dive * flight_fold;
        // Trail and fold were settled by eye on the +Z-facing asset; the aft
        // factor keeps them aft on a model facing the other way. The pendulum
        // swing is already in model space and needs no help.
        const float aft = model_forward_z_;
        auto drive_limb = [&](const std::vector<int>& chain, float trail_angle) {
            float sign = 1.0f;
            bool first = true;
            for (const int joint : chain) {
                rotate_joint(joint, Vec3::unit_x(), tuck_angle * sign * aft, true);
                if (first) {
                    // `leg_brake_forward_deg` is positive in engine-forward
                    // terms. It opposes the aft trail after conversion to the
                    // model's measured facing, so +Z-facing imported assets
                    // and the generated -Z asset agree.
                    rotate_joint(joint, Vec3::unit_x(),
                                 trail_angle * aft - brake_forward * aft + swing.x, true);
                    rotate_joint(joint, Vec3::unit_z(), swing.y, true);
                    first = false;
                }
                sign *= -1.15f;
            }
        };
        drive_limb(joints_.leg[side], trail);
        drive_limb(joints_.front_leg[side],
                   (core::radians(tuning.front_leg_trail_deg) +
                    core::radians(tuning.leg_dive_trail_deg) * dive) * flight_fold);

        // The claw: the near limb rakes forward and down and returns. The
        // foreleg where there is one, the hind leg on a wyvern. Side 0 is
        // the right (+X), matching RigAction::side.
        const bool this_side = (side == 0) == (claw_side_ >= 0.0f);
        if (this_side && std::fabs(gesture_limb_rake_) > 1e-4f) {
            const float rake = gesture_limb_rake_;
            const std::vector<int>& limb =
                joints_.front_leg[side].empty() ? joints_.leg[side] : joints_.front_leg[side];
            if (!limb.empty()) {
                // Forward is against the trail, hence the negative aft; the
                // second joint unfolds so the foot reaches rather than tucks.
                rotate_joint(limb.front(), Vec3::unit_x(),
                             -core::radians(tuning.claw_swing_deg) * rake * aft, true);
                // Out to the side: +X is the right, and a limb hanging down
                // rotated about +Z moves its tip toward +X.
                rotate_joint(limb.front(), Vec3::unit_z(),
                             (side == 0 ? 1.0f : -1.0f) * core::radians(tuning.claw_out_deg) * rake,
                             true);
                if (limb.size() > 1) {
                    rotate_joint(limb[1], Vec3::unit_x(),
                                 core::radians(tuning.claw_swing_deg) * 0.5f * rake * aft, true);
                }
            }
        }
    }
}

void DragonRig::aim_bone(int joint, int child, Vec3 target, float weight) {
    if (joint == NO_PARENT || child == NO_PARENT || weight <= 1e-5f) return;
    if (size_t(joint) >= world_.size() || size_t(child) >= world_.size()) return;
    const Vec3 origin = world_[size_t(joint)].col[3].xyz();
    const Vec3 current = world_[size_t(child)].col[3].xyz() - origin;
    if (core::length_sq(current) < 1e-10f || core::length_sq(target) < 1e-10f) return;
    // The model-space rotation that turns the bone onto its target, re-expressed
    // in the parent's frame so it can be composed onto the local rotation: the
    // joint's new world rotation is delta * world, and local = parent^-1 * world.
    const Quat delta = core::rotation_between(core::normalize(current), core::normalize(target));
    const int parent = skeleton_->joint(joint).parent;
    const Quat parent_rotation =
        parent == NO_PARENT ? Quat::identity() : core::quat_from_matrix(world_[size_t(parent)]);
    const Quat world_rotation = core::quat_from_matrix(world_[size_t(joint)]);
    const Quat aimed =
        core::normalize(core::conjugate(parent_rotation) * delta * world_rotation);
    Transform& local = pose_.local[size_t(joint)];
    local.rotation = core::normalize(core::slerp(local.rotation, aimed, core::saturate(weight)));
    // Everything outboard moved with it; the next bone is aimed from where it
    // now is.
    compute_world_matrices(*skeleton_, pose_, world_);
}

void DragonRig::drive_stance(float stow) {
    if (stow <= 1e-4f || !skeleton_) return;
    // A positive rotation about model X swings a hanging limb toward -Z, which
    // is forward on a model facing -Z and aft on one facing +Z.
    const float forward = -model_forward_z_;
    const float aft_z = -model_forward_z_;

    // ---- body and legs: angles composed onto the bind pose ----
    const float stance = stow * core::saturate(tuning.ground_stance);
    if (stance > 1e-4f) {
        // Nose up is a rotation about model X whose sign follows the facing --
        // the beat pitch convention.
        if (std::fabs(tuning.ground_body_pitch_deg) > 1e-3f) {
            rotate_joint(joints_.root, Vec3::unit_x(),
                         core::radians(tuning.ground_body_pitch_deg) * stance * -model_forward_z_,
                         true);
        }
        auto swing_limb = [&](const std::vector<int>& chain, const float* angles_deg,
                              float splay_deg) {
            for (size_t i = 0; i < chain.size() && i < 3; ++i) {
                if (std::fabs(angles_deg[i]) < 1e-3f) continue;
                rotate_joint(chain[i], Vec3::unit_x(),
                             core::radians(angles_deg[i]) * forward * stance, true);
            }
            // Lateral swing about model Z: a positive angle moves a hanging
            // limb toward +X, so the side the limb is on decides the sign.
            if (!chain.empty() && std::fabs(splay_deg) > 1e-3f) {
                const float x = skeleton_->world_bind(chain.front()).translation_part().x;
                const float outward = x >= 0.0f ? 1.0f : -1.0f;
                rotate_joint(chain.front(), Vec3::unit_z(),
                             core::radians(splay_deg) * outward * stance, true);
            }
        };
        const float hind[3] = {tuning.ground_hip_deg, tuning.ground_knee_deg,
                               tuning.ground_ankle_deg};
        const float fore[3] = {tuning.ground_shoulder_deg, tuning.ground_elbow_deg,
                               tuning.ground_wrist_deg};
        for (int side = 0; side < 2; ++side) {
            swing_limb(joints_.leg[side], hind, tuning.ground_leg_splay_deg);
            swing_limb(joints_.front_leg[side], fore, tuning.ground_arm_splay_deg);
        }
        // Neck and tail against the body pitch. Nose-up positive is the same
        // rotation the body pitch uses; the tail is the same axis, and "up"
        // for a chain running aft is the opposite sense of the same turn.
        auto curve_chain = [&](const std::vector<int>& chain, float total_deg, float up_sign) {
            if (chain.empty() || std::fabs(total_deg) < 1e-3f) return;
            const float per_joint =
                core::radians(total_deg) * stance * up_sign / float(chain.size());
            for (const int joint : chain) rotate_joint(joint, Vec3::unit_x(), per_joint, true);
        };
        curve_chain(joints_.neck, tuning.ground_neck_pitch_deg, -model_forward_z_);
        curve_chain(joints_.tail, tuning.ground_tail_pitch_deg, model_forward_z_);
    }

    // ---- the wing, aimed segment by segment ----
    const float aim = stow * core::saturate(tuning.ground_wing_aim);
    const bool level_feet = stance > 1e-4f && tuning.ground_feet_level > 1e-4f;
    if (aim > 1e-4f || level_feet) compute_world_matrices(*skeleton_, pose_, world_);
    if (aim > 1e-4f) {
        for (int side = 0; side < 2; ++side) {
            const std::vector<int>& root = joints_.wing_root[side];
            if (root.size() < 2) continue;
            // Which way is out: the side the wing's bind wrist sits on.
            const float x = skeleton_->world_bind(root.back()).translation_part().x;
            const float out = x >= 0.0f ? 1.0f : -1.0f;
            auto direction = [&](float sweep_deg, float elev_deg) {
                const float sweep = core::radians(sweep_deg);
                const float elev = core::radians(elev_deg);
                return Vec3{std::cos(elev) * std::cos(sweep) * out, std::sin(elev),
                            std::cos(elev) * std::sin(sweep) * aft_z};
            };
            // Upper arm, then the forearm; a root with more bones than that
            // treats every bone past the first as forearm.
            aim_bone(root[0], root[1],
                     direction(tuning.ground_wing_arm_sweep_deg, tuning.ground_wing_arm_elev_deg),
                     aim);
            for (size_t i = 1; i + 1 < root.size(); ++i) {
                aim_bone(root[i], root[i + 1],
                         direction(tuning.ground_wing_forearm_sweep_deg,
                                   tuning.ground_wing_forearm_elev_deg),
                         aim);
            }
            // A two-bone arm (the generated rig) has no wrist bone: its forearm
            // runs from the elbow to the finger base, and is aimed the same.
            if (root.size() == 2 && !joints_.wing_fingers[side].empty() &&
                !joints_.wing_fingers[side][0].empty()) {
                aim_bone(root[1], joints_.wing_fingers[side][0][0],
                         direction(tuning.ground_wing_forearm_sweep_deg,
                                   tuning.ground_wing_forearm_elev_deg),
                         aim);
            }
            // The hand: every rib straight along its own direction, each one
            // hanging a little lower than the last, so the fan shuts into
            // pleats. On a two-bone arm the finger base is the wrist and the
            // same rule folds it.
            int rib = 0;
            for (const std::vector<int>& finger : joints_.wing_fingers[side]) {
                const Vec3 hand = direction(
                    tuning.ground_wing_hand_sweep_deg,
                    tuning.ground_wing_hand_elev_deg - tuning.ground_wing_fan_deg * float(rib));
                for (size_t i = 0; i + 1 < finger.size(); ++i) {
                    aim_bone(finger[i], finger[i + 1], hand, aim);
                }
                ++rib;
            }
        }
    }

    // ---- the feet back on the floor ----
    if (level_feet && !stance_foot_joints_.empty() && joints_.root != NO_PARENT) {
        float floor = 0.0f;
        for (size_t i = 0; i < stance_foot_joints_.size(); ++i) {
            const float y = world_[size_t(stance_foot_joints_[i])].col[3].y;
            floor = i == 0 ? y : core::minf(floor, y);
        }
        // A creature that stands on its wings has its wrists on the floor too.
        // Their bind height is NOT part of the bind floor: a wyvern binds
        // wings spread, and the stance is what brings the wrists down.
        if (tuning.ground_wing_plant > 0.5f) {
            for (int side = 0; side < 2; ++side) {
                if (joints_.wing_root[side].size() < 2) continue;
                floor = core::minf(floor, world_[size_t(joints_.wing_root[side].back())].col[3].y);
            }
        }
        // The root has no parent on every rig this drives, so its position is
        // model space; if it ever had one, the lift goes in that parent's frame.
        const int root = joints_.root;
        const int parent = skeleton_->joint(root).parent;
        Vec3 lift = Vec3::unit_y() *
                    ((stance_floor_bind_ - floor + tuning.ground_lift_m / model_scale_) * stance);
        if (parent != NO_PARENT) {
            lift = core::rotate(core::conjugate(core::quat_from_matrix(world_[size_t(parent)])),
                                lift);
        }
        pose_.local[size_t(root)].position += lift;
    }

    // Then the terrain under each foot, if the app told us where it is. On
    // ground contact rather than the stance weight: a sculpt that stands in
    // its bind pose has no stance and still lands on hillsides.
    plant_limbs(stow);
}

void DragonRig::rotate_joint_about(int joint, Vec3 axis_model, float angle) {
    if (joint == NO_PARENT || size_t(joint) >= world_.size() || std::fabs(angle) < 1e-6f) return;
    if (core::length_sq(axis_model) < 1e-12f) return;
    const Quat delta = Quat::from_axis_angle(core::normalize(axis_model), angle);
    const int parent = skeleton_->joint(joint).parent;
    const Quat parent_rotation =
        parent == NO_PARENT ? Quat::identity() : core::quat_from_matrix(world_[size_t(parent)]);
    const Quat world_rotation = core::quat_from_matrix(world_[size_t(joint)]);
    pose_.local[size_t(joint)].rotation =
        core::normalize(core::conjugate(parent_rotation) * delta * world_rotation);
    compute_world_matrices(*skeleton_, pose_, world_);
}

float DragonRig::standing_share(float airspeed) const {
    const float full = core::maxf(tuning.standing_speed, 0.5f);
    return core::saturate(2.0f - airspeed / full);
}

void DragonRig::plant_limbs(float stance) {
    const float weight = stance * core::saturate(tuning.ground_ik);
    if (weight <= 1e-4f || !ground_height_ || !skeleton_ || joints_.root == NO_PARENT) return;

    // The standing limbs: a two-bone chain (a, b) ending in the contact joint
    // c -- the foot root under the leg, the hand under the arm, the wrist of a
    // planted wing. The foot beyond the ankle rides the shin as posed.
    struct Limb {
        int a, b, c;
        bool fore;
        int side;
    };
    std::vector<Limb> limbs;
    auto foot_root_under = [&](int chain_end) {
        for (const int foot : joints_.foot_roots) {
            for (int p = foot; p != NO_PARENT; p = skeleton_->joint(p).parent) {
                if (p == chain_end) return foot;
            }
        }
        return chain_end;
    };
    for (int side = 0; side < 2; ++side) {
        const std::vector<int>& leg = joints_.leg[side];
        if (leg.size() >= 2) limbs.push_back({leg[0], leg[1], foot_root_under(leg.back()), false, side});
        const std::vector<int>& arm = joints_.front_leg[side];
        if (arm.size() >= 2) limbs.push_back({arm[0], arm[1], foot_root_under(arm.back()), true, side});
        const std::vector<int>& wing = joints_.wing_root[side];
        if (tuning.ground_wing_plant > 0.5f && wing.size() >= 3) {
            limbs.push_back({wing[0], wing[1], wing.back(), true, side});
        }
    }
    if (limbs.empty()) return;

    const core::Mat4 world_to_model = core::inverse(model_to_world_);
    // One metre of world up, in model units and model space.
    const Vec3 up_model = (world_to_model * core::Vec4{Vec3::up(), 0.0f}).xyz();
    if (core::length_sq(up_model) < 1e-12f) return;
    compute_world_matrices(*skeleton_, pose_, world_);
    auto contact_world = [&](const Limb& l) {
        return core::transform_point(model_to_world_, world_[size_t(l.c)].col[3].xyz());
    };
    // How far the contact must rise (metres, world up) to sit on the terrain
    // under it. Feet stand a little above the mesh sole (ground_lift_m); a
    // planted wrist is the contact itself.
    auto contact_error = [&](const Limb& l) {
        const Vec3 p = contact_world(l);
        const float above_floor =
            l.c == joints_.wing_root[0].back() || l.c == joints_.wing_root[1].back()
                ? 0.0f
                : (skeleton_->world_bind(l.c).translation_part().y - stance_floor_bind_) *
                      model_scale_;
        return ground_height_(p.x, p.z) + above_floor + tuning.ground_lift_m - p.y;
    };

    // ---- the body onto the mean contact ----
    // Lift by the mean error; pitch by the fore/hind difference over their
    // spacing; roll by the left/right difference. Smoothed, because a foot
    // crossing a terrain triangle edge must not snap the whole animal, and
    // clamped, because a boulder under one foot should bend a leg.
    float sum = 0.0f;
    float fore_sum = 0.0f, hind_sum = 0.0f, right_sum = 0.0f, left_sum = 0.0f;
    Vec3 fore_pos{}, hind_pos{}, right_pos{}, left_pos{};
    int fore_n = 0, hind_n = 0, right_n = 0, left_n = 0;
    const Vec3 right_world = core::normalize_or(
        (model_to_world_ * core::Vec4{Vec3::unit_x(), 0.0f}).xyz(), Vec3::right());
    for (const Limb& l : limbs) {
        const float e = contact_error(l);
        const Vec3 p = contact_world(l);
        sum += e;
        if (l.fore) { fore_sum += e; fore_pos = fore_pos + p; ++fore_n; }
        else { hind_sum += e; hind_pos = hind_pos + p; ++hind_n; }
        const float side = core::dot(p - core::transform_point(model_to_world_, world_[size_t(joints_.root)].col[3].xyz()), right_world);
        if (side >= 0.0f) { right_sum += e; right_pos = right_pos + p; ++right_n; }
        else { left_sum += e; left_pos = left_pos + p; ++left_n; }
    }
    const float mean = sum / float(limbs.size());
    float pitch = 0.0f, roll = 0.0f;
    const float tilt_max = core::radians(core::maxf(tuning.ground_ik_tilt_max_deg, 0.0f));
    if (fore_n > 0 && hind_n > 0) {
        const Vec3 f = fore_pos * (1.0f / float(fore_n)), h = hind_pos * (1.0f / float(hind_n));
        const float span = core::length(Vec3{f.x - h.x, 0.0f, f.z - h.z});
        if (span > 0.5f) {
            pitch = core::clampf(std::atan2(fore_sum / float(fore_n) - hind_sum / float(hind_n), span),
                                 -tilt_max, tilt_max);
        }
    }
    if (right_n > 0 && left_n > 0) {
        const Vec3 r = right_pos * (1.0f / float(right_n)), l = left_pos * (1.0f / float(left_n));
        const float span = core::length(Vec3{r.x - l.x, 0.0f, r.z - l.z});
        if (span > 0.5f) {
            roll = core::clampf(std::atan2(right_sum / float(right_n) - left_sum / float(left_n), span),
                                -tilt_max, tilt_max);
        }
    }
    plant_lift_ = core::damp(plant_lift_, mean, 0.12f, plant_dt_);
    plant_pitch_ = core::damp(plant_pitch_, pitch, 0.12f, plant_dt_);
    plant_roll_ = core::damp(plant_roll_, roll, 0.12f, plant_dt_);
    {
        const int root = joints_.root;
        const int parent = skeleton_->joint(root).parent;
        Vec3 lift = up_model * (plant_lift_ * weight);
        if (parent != NO_PARENT) {
            lift = core::rotate(core::conjugate(core::quat_from_matrix(world_[size_t(parent)])), lift);
        }
        pose_.local[size_t(root)].position += lift;
        // Nose up is a rotation about model X whose sign follows the facing;
        // a positive rotation about model Z raises the +X side.
        rotate_joint(root, Vec3::unit_x(), plant_pitch_ * weight * -model_forward_z_, true);
        rotate_joint(root, Vec3::unit_z(), plant_roll_ * weight, true);
        compute_world_matrices(*skeleton_, pose_, world_);
    }

    // ---- each limb onto its own contact ----
    // Two-bone: bend the middle joint about the limb's own plane until the
    // chain's length matches the reach, then aim the chain at the target.
    // Twice, since each step disturbs the other a little.
    //
    // Walking, each limb also gets its step: diagonal pairs share a phase,
    // lifted and swung forward in the first half of the cycle, planted and
    // carried back in the second. The step is added to the per-limb target
    // only, never to the body placement above, so the body rides level.
    const float metre = core::length(up_model);
    const Vec3 up_dir = up_model * (1.0f / metre);
    const Vec3 forward_model =
        core::normalize_or(core::rotate(body_to_model_, Vec3::forward()), Vec3::unit_z()) * metre;
    const float walking = walk_amount_;
    for (const Limb& l : limbs) {
        float lift_m = 0.0f;
        Vec3 swing = Vec3::zero();
        if (walking > 1e-3f) {
            const bool first_pair = (l.side == 0) != l.fore;  // hind 0 with fore 1
            const float phase = walk_phase_ + (first_pair ? 0.0f : core::PI);
            lift_m = tuning.stride_lift_m * core::maxf(std::sin(phase), 0.0f) * walking;
            swing = forward_model * (-std::cos(phase) * 0.5f * tuning.stride_length_m * walking);
        }
        const Vec3 goal = world_[size_t(l.c)].col[3].xyz() + swing;
        for (int iteration = 0; iteration < 2; ++iteration) {
            const float e = contact_error(l) + lift_m;
            const Vec3 c = world_[size_t(l.c)].col[3].xyz();
            Vec3 across = goal - c;
            across = across - up_dir * core::dot(across, up_dir);
            if (std::fabs(e) < 0.005f && core::length_sq(across) < 1e-6f) break;
            const Vec3 a = world_[size_t(l.a)].col[3].xyz();
            const Vec3 b = world_[size_t(l.b)].col[3].xyz();
            const Vec3 target = c + up_model * e + across;
            const float l1 = core::length(b - a), l2 = core::length(c - b);
            if (l1 < 1e-5f || l2 < 1e-5f) break;
            const float reach = core::clampf(core::length(target - a),
                                             std::fabs(l1 - l2) * 1.02f + 1e-4f, (l1 + l2) * 0.995f);
            const Vec3 ba = a - b, bc = c - b;
            const float current = std::acos(core::clampf(core::dot(ba, bc) / (l1 * l2), -1.0f, 1.0f));
            const float wanted = std::acos(core::clampf((l1 * l1 + l2 * l2 - reach * reach) /
                                                            (2.0f * l1 * l2), -1.0f, 1.0f));
            Vec3 axis = core::cross(ba, bc);
            if (core::length_sq(axis) < 1e-10f) axis = core::cross(ba, up_model);
            // Rotating bc about cross(ba, bc) by a positive angle opens the
            // angle between them.
            rotate_joint_about(l.b, axis, (wanted - current) * weight);
            aim_bone(l.a, l.c, target - world_[size_t(l.a)].col[3].xyz(), weight);
        }
    }
}

std::vector<std::pair<std::string, float>> DragonRig::foot_heights() const {
    std::vector<std::pair<std::string, float>> out;
    if (!skeleton_ || world_.empty()) return out;
    std::vector<int> standing = joints_.foot_roots;
    if (tuning.ground_wing_plant > 0.5f) {
        for (int side = 0; side < 2; ++side) {
            if (joints_.wing_root[side].size() >= 2) standing.push_back(joints_.wing_root[side].back());
        }
    }
    float floor = 0.0f;
    for (size_t i = 0; i < standing.size(); ++i) {
        const float y = world_[size_t(standing[i])].col[3].y;
        floor = i == 0 ? y : core::minf(floor, y);
    }
    for (const int joint : standing) {
        out.emplace_back(skeleton_->joint(joint).name,
                         (world_[size_t(joint)].col[3].y - floor) * model_scale_);
    }
    return out;
}

float DragonRig::flight_intensity(const game::FlightState& state) const {
    // Whichever signal is working the body hardest wins. Max rather than sum:
    // a fast, hard-turning dive should read as 1, not 3.
    const float speed = core::saturate((state.airspeed - 28.0f) / 45.0f);
    const float load = core::saturate(std::fabs(state.g_load - 1.0f) / 1.5f);
    const float turning = core::saturate(core::length(state.angular_velocity) / 1.2f);
    float intensity = core::maxf(core::maxf(speed, load), turning);
    intensity = core::maxf(intensity, state.wing_tuck);
    intensity = core::maxf(intensity, state.wing_brake);
    // On the ground nothing is working: the idle is exactly right there.
    return state.grounded ? 0.0f : intensity;
}

// Moves each body-parented foot root to the end of its posed leg, as if it were
// parented there, blended by `airborne` so the authored planted stance wins on
// the ground. Needs world matrices for the current pose; leaves them stale.
void DragonRig::attach_feet(float airborne) {
    const float follow = core::saturate(tuning.foot_follow) * airborne;
    if (follow <= 0.001f) return;
    for (const FootAttach& attach : foot_attach_) {
        const int parent = skeleton_->joint(attach.foot).parent;
        if (parent == NO_PARENT) continue;

        const core::Mat4& anchor_world = world_[size_t(attach.anchor)];
        const Quat anchor_rotation = core::quat_from_matrix(anchor_world);
        const Vec3 target_position =
            anchor_world.col[3].xyz() + core::rotate(anchor_rotation, attach.offset);
        const Quat target_rotation = core::normalize(anchor_rotation * attach.rotation);

        const core::Mat4& parent_world = world_[size_t(parent)];
        const Quat parent_rotation = core::quat_from_matrix(parent_world);
        const Quat parent_inverse = core::conjugate(parent_rotation);
        // The parent is the body joint that absorbs the scene's SCALE (that is
        // how this asset's bind pose reconciles -- see the loader notes), and a
        // local position lives in the parent's scaled space. Dropping the
        // divide sent every foot to within a metre of the origin.
        const Vec3 scale{core::length(parent_world.col[0].xyz()),
                         core::length(parent_world.col[1].xyz()),
                         core::length(parent_world.col[2].xyz())};
        Vec3 local_position =
            core::rotate(parent_inverse, target_position - parent_world.col[3].xyz());
        local_position.x /= core::maxf(scale.x, 1e-6f);
        local_position.y /= core::maxf(scale.y, 1e-6f);
        local_position.z /= core::maxf(scale.z, 1e-6f);
        const Quat local_rotation = core::normalize(parent_inverse * target_rotation);

        Transform& local = pose_.local[size_t(attach.foot)];
        local.position = core::lerp(local.position, local_position, follow);
        local.rotation = core::normalize(core::slerp(local.rotation, local_rotation, follow));
    }
}

game::FlightState DragonRig::model_frame(const game::FlightState& engine) const {
    // conj(orientation) takes world into the body frame; composing the asset's
    // facing onto it takes world straight into model space, where the bones
    // are. Body-frame vectors the flight model already resolved come across
    // with the same rotation.
    game::FlightState model = engine;
    model.orientation =
        core::normalize(engine.orientation * core::conjugate(body_to_model_));
    model.angular_velocity = core::rotate(body_to_model_, engine.angular_velocity);
    return model;
}

void DragonRig::update(const game::FlightState& engine_state, float dt) {
    if (!skeleton_ || dt <= 0.0f) return;

    // Everything below works in model space, where the bones are.
    const game::FlightState state = model_frame(engine_state);

    // Accelerations are what the chains actually respond to, and the flight model
    // reports velocities, so they are differenced here.
    Vec3 frame_acceleration = Vec3::zero();
    Vec3 angular_acceleration = Vec3::zero();
    if (have_previous_) {
        const Vec3 world_acceleration = (state.velocity - previous_velocity_) / dt;
        frame_acceleration = core::rotate(core::conjugate(state.orientation), world_acceleration);
        angular_acceleration = (state.angular_velocity - previous_angular_velocity_) / dt;
    }
    previous_velocity_ = state.velocity;
    previous_angular_velocity_ = state.angular_velocity;
    have_previous_ = true;
    time_ += dt;

    // Attack state. The breath eases in fast and out slower (a mouth snaps
    // open and relaxes shut); the spit is a clock from the last fireball.
    breath_smoothed_ = core::damp(breath_smoothed_, core::saturate(action_.breath),
                                  action_.breath > breath_smoothed_ ? 0.06f : 0.2f, dt);
    if (action_.fire) {
        spit_time_ = 0.0f;
        // The snap: kick the neck's points up and back, more toward the head.
        // Engine body frame (up +Y, aft +Z), carried into model space like
        // every other frame vector.
        const Vec3 kick = core::rotate(body_to_model_, Vec3{0.0f, 0.6f, 0.8f}) *
                          tuning.spit_impulse;
        const size_t points = neck_sim_.velocity.size();
        for (size_t i = 1; i < points; ++i) {
            const float progress = float(i) / float(points - 1);
            neck_sim_.velocity[i] += kick * progress;
        }
    } else {
        spit_time_ += dt;
    }
    action_.fire = false;  // an edge, consumed
    if (action_.bite) {
        bite_time_ = 0.0f;
    } else {
        bite_time_ += dt;
    }
    const float bite_launch = tuning.bite_duration * core::clampf(tuning.gesture_anticipation, 0.0f, 0.6f);
    if ((action_.bite && bite_launch <= 0.0f) ||
        (!action_.bite && bite_time_ >= bite_launch && bite_time_ - dt < bite_launch)) {
        // Release the neck AFTER loading, not on the input edge.
        const Vec3 kick = core::rotate(body_to_model_, Vec3{0.0f, -0.35f, -0.94f}) *
                          tuning.bite_impulse;
        const size_t points = neck_sim_.velocity.size();
        for (size_t i = 1; i < points; ++i) {
            const float progress = float(i) / float(points - 1);
            neck_sim_.velocity[i] += kick * progress;
        }
    }
    action_.bite = false;
    if (action_.claw) {
        claw_time_ = 0.0f;
        claw_side_ = action_.side >= 0.0f ? 1.0f : -1.0f;
    } else {
        claw_time_ += dt;
    }
    action_.claw = false;
    if (action_.tail) {
        tail_time_ = 0.0f;
        tail_side_ = action_.side >= 0.0f ? 1.0f : -1.0f;
    } else {
        tail_time_ += dt;
    }
    const float tail_launch = tuning.tail_duration * core::clampf(tuning.gesture_anticipation, 0.0f, 0.6f);
    if ((action_.tail && tail_launch <= 0.0f) ||
        (!action_.tail && tail_time_ >= tail_launch && tail_time_ - dt < tail_launch)) {
        // The whip: kick the tail's points sideways toward the mark, more
        // toward the tip. Engine body frame (right +X), carried into model
        // space like every other frame vector.
        const Vec3 kick = core::rotate(body_to_model_, Vec3{tail_side_, 0.15f, 0.0f}) *
                          tuning.tail_impulse;
        const size_t points = tail_sim_.velocity.size();
        for (size_t i = 1; i < points; ++i) {
            const float progress = float(i) / float(points - 1);
            tail_sim_.velocity[i] += kick * progress * progress;
        }
    }
    action_.tail = false;

    // Wing load flex reads the g excess, smoothed because g_load is assembled
    // from this frame's forces and single-frame spikes would make the wings
    // twitch. Clamped low because negative g beyond a gentle unload folds the
    // wings under the body, which reads as broken rather than as pushing over.
    load_smoothed_ = core::damp(load_smoothed_,
                                core::clampf(state.g_load - 1.0f, -0.6f, 2.5f), 0.12f, dt);
    intensity_smoothed_ = core::damp(intensity_smoothed_, flight_intensity(state), 0.35f, dt);

    // Authored motion first: it fills in every joint the procedural rig does not
    // own, and the rig then overrides the ones flight determines.
    pose_.reset_to_bind(*skeleton_);
    // The clip is a GROUND idle: full strength standing (leg_extend_ is the
    // smoothed on-the-ground signal), a trace in a calm glide, gone entirely
    // under hard flight. Toes gripping ground at 100 m/s read as someone else's
    // animation playing on the wrong creature.
    const float airborne_weight =
        tuning.clip_air_weight *
        (1.0f - core::saturate(tuning.clip_flight_fade) * intensity_smoothed_);
    const float clip_weight =
        tuning.base_clip_weight * core::lerpf(airborne_weight, 1.0f, leg_extend_);
    ground_contact_ = core::damp(ground_contact_, state.grounded ? 1.0f : 0.0f, 0.3f, dt);
    const bool has_clip = base_clip_ && base_clip_->valid();
    if (has_clip) {
        clip_time_ += dt * tuning.base_clip_rate;
        const float sample_time = clip_hold_time_ >= 0.0f ? clip_hold_time_ : clip_time_;
        // The full authored pose is kept for the ground handover below.
        clip_pose_.reset_to_bind(*skeleton_);
        base_clip_->sample(sample_time, clip_pose_);
        if (clip_weight >= 0.999f) {
            pose_ = clip_pose_;
        } else if (clip_weight > 0.001f) {
            blend_poses(pose_, clip_pose_, clip_weight, pose_);
        }
    }

    // Growth: the shoulder's scale carries the whole wing with it.
    if (std::fabs(wing_growth - 1.0f) > 1e-4f) {
        for (int side = 0; side < 2; ++side) {
            if (joints_.wing_root[side].empty()) continue;
            Transform& shoulder = pose_.local[size_t(joints_.wing_root[side][0])];
            shoulder.scale = shoulder.scale * wing_growth;
        }
    }
    drive_gestures();
    drive_wings(state);
    drive_body_beat(state);
    // The gesture layer on the root: pitch, roll and yaw in engine terms,
    // and the surge along the body's forward axis. Same sign conventions as
    // the body beat's pitch and the wings' roll lean.
    if (joints_.root != NO_PARENT && size_t(joints_.root) < pose_.local.size()) {
        if (std::fabs(gesture_pitch_) > 1e-5f) {
            rotate_joint(joints_.root, Vec3::unit_x(), gesture_pitch_ * -model_forward_z_, true);
        }
        if (std::fabs(gesture_roll_) > 1e-5f) {
            rotate_joint(joints_.root, Vec3::unit_z(), gesture_roll_ * model_forward_z_, true);
        }
        if (std::fabs(gesture_yaw_) > 1e-5f) {
            rotate_joint(joints_.root, Vec3::unit_y(), -gesture_yaw_, true);
        }
        if (std::fabs(gesture_sway_) > 1e-5f) {
            pose_.local[size_t(joints_.root)].position +=
                core::rotate(body_to_model_, Vec3::right()) * (gesture_sway_ / model_scale_);
        }
        if (std::fabs(gesture_surge_) > 1e-5f) {
            Transform& local = pose_.local[size_t(joints_.root)];
            local.position += core::rotate(body_to_model_, Vec3::forward()) *
                              (gesture_surge_ / model_scale_);
        }
    }

    std::vector<int> neck_with_head = joints_.neck;
    if (joints_.head != NO_PARENT) neck_with_head.push_back(joints_.head);

    // The tail steers. Yaw and roll input swing it toward the outside of the
    // commanded turn (a rudder pushing the tail across the airflow); pitch
    // input works it as an elevator, dropping the tail as the nose rises. The
    // control positions are already smoothed by the flight model.
    core::Vec2 tail_steer{
        state.control.x * tuning.tail_elevator_deg +
            core::saturate(state.wing_brake) * tuning.brake_tail_drop_deg,
        -(state.control.y + 0.5f * state.control.z) * tuning.tail_rudder_deg};

    // The neck leads the manoeuvre and lowers into the wind at speed -- and a
    // deliberate tuck streamlines regardless of how fast the dive is yet, the
    // way a stooping raptor commits to the shape before the speed arrives.
    const float streamline =
        core::maxf(core::saturate(state.airspeed / core::maxf(tuning.streamline_speed, 1.0f)),
                   state.wing_tuck * 0.9f);
    core::Vec2 neck_steer{
        state.control.x * tuning.neck_lead_deg - streamline * tuning.neck_streamline_deg, 0.0f};

    // The neck carries its share of the aim. The head finishes the job in
    // aim_head(), measured against wherever the chain actually put it, so the
    // two never fight: the neck curls toward the mark through the spring, the
    // head snaps the rest of the way.
    if (aim_active_ && !joints_.neck.empty()) {
        // Measured from the body origin rather than the neck root: the mark
        // is hundreds of metres out and the neck root a couple of metres in,
        // and the head corrects the residual anyway.
        // In the ENGINE body frame (forward -Z), like the control inputs the
        // steer is added to; the pitch sign below converts once for both.
        const Vec3 to_target = core::rotate(core::conjugate(engine_state.orientation),
                                            aim_target_ - engine_state.position);
        if (core::length_sq(to_target) > 1e-4f) {
            // Yaw about Y and pitch about X of the body's forward axis, the
            // same convention the tail's rudder and elevator use.
            const float yaw = core::degrees(std::atan2(-to_target.x, -to_target.z));
            const float pitch = core::degrees(std::atan2(
                to_target.y, std::sqrt(to_target.x * to_target.x + to_target.z * to_target.z)));
            const float limit = tuning.neck_aim_max_deg;
            neck_steer.x += core::clampf(pitch * tuning.neck_aim_share, -limit, limit);
            neck_steer.y += core::clampf(yaw * tuning.neck_aim_share, -limit, limit);
        }
    }

    // Breath: the neck thrusts into the stream. Spit: it rears back and whips
    // forward -- one sine cycle, decaying, through the same spring as
    // everything else, so the recoil overshoots and settles like a neck.
    neck_steer.x -= breath_smoothed_ * tuning.breath_neck_thrust_deg;
    if (spit_time_ < tuning.spit_duration) {
        const float u = spit_time_ / core::maxf(tuning.spit_duration, 1e-3f);
        neck_steer.x += tuning.spit_recoil_deg * std::sin(core::TWO_PI * u) * (1.0f - u);
    }
    // The gesture layer's share of the neck and the tail (drive_gestures):
    // the bite's rear-and-lunge, the head dipping to a claw's mark or
    // swinging against a tail whip, the tail balancing a claw or whipping.
    // Steer pitch is + head up here, like the breath's thrust is -; steer
    // yaw follows the rudder's sign, measured once against the generated rig
    // in test_anim and fixed by GESTURE_YAW_SIGN.
    neck_steer.x += gesture_neck_pitch_deg_;
    neck_steer.y += GESTURE_YAW_SIGN * gesture_neck_yaw_deg_;
    tail_steer.y += GESTURE_YAW_SIGN * (gesture_tail_yaw_deg_ + tuning.tail_whip_deg * gesture_tail_whip_);

    // Named fields, not positional braces: a positional initializer here once
    // silently dropped the neck's brace, aero gate and articulation range when
    // the struct grew, and every fix routed through them became dead code.
    ChainFeel tail_feel;
    tail_feel.damping = tuning.tail_damping_scale;
    tail_feel.range_deg = tuning.tail_range_deg;
    tail_feel.tip_stiffness = tuning.tail_tip_stiffness;

    ChainFeel neck_feel;
    // A breathing neck is tensed: it holds the flame steady. A spitting neck
    // is a strike: several times stiffer for the gesture, which is what makes
    // an overdamped, heavy neck fast enough to rear and whip inside half a
    // second instead of absorbing the impulse.
    const float spitting =
        (spit_time_ < tuning.spit_duration || bite_time_ < tuning.bite_duration) ? 1.0f : 0.0f;
    neck_feel.stiffness = tuning.neck_stiffness_scale *
                          (1.0f + tuning.breath_neck_tone * breath_smoothed_ + 6.0f * spitting);
    neck_feel.gravity = tuning.neck_gravity_scale;
    neck_feel.inertia = tuning.neck_inertia_scale;
    neck_feel.damping = tuning.neck_damping_scale;
    neck_feel.aero = 0.1f;
    neck_feel.range_deg = tuning.neck_range_deg;

    // Steer pitch is specified in engine terms (+ raises the head, drops the
    // tail) and applied about the model's X axis, which points the other way
    // on a model facing +Z.
    const float pitch_sign = -model_forward_z_;
    tail_steer.x *= pitch_sign;
    neck_steer.x *= pitch_sign;

    drive_chain(tail_sim_, joints_.tail, state, frame_acceleration, angular_acceleration,
                tail_steer, tail_feel, dt);
    drive_chain(neck_sim_, neck_with_head, state, frame_acceleration, angular_acceleration,
                neck_steer, neck_feel, dt);
    drive_legs(state, frame_acceleration, angular_acceleration, dt);
    // The standing stance, on top of everything the flight pose left: the same
    // "on the ground and at rest" signal the wing stow fades in with.
    plant_dt_ = dt;
    {
        // The walk clock: one full cycle carries each foot a stride forward
        // and back, so it advances by pi per stride of ground covered.
        const float ground_speed =
            state.grounded ? core::length(Vec3{state.velocity.x, 0.0f, state.velocity.z}) : 0.0f;
        walk_amount_ = core::damp(walk_amount_, core::saturate(ground_speed / 1.5f), 0.12f, dt);
        walk_phase_ = std::fmod(walk_phase_ + core::PI * ground_speed /
                                                  core::maxf(tuning.stride_length_m, 0.2f) * dt,
                                core::TWO_PI);
    }
    drive_stance(ground_contact_ * standing_share(state.airspeed));

    // Feet: first anchor them to the posed legs (needs world matrices), then
    // the relaxed hang and claw curl compose on top.
    compute_world_matrices(*skeleton_, pose_, world_);
    const float airborne = 1.0f - leg_extend_;
    attach_feet(airborne);
    if (airborne > 0.001f) {
        // Hang and curl were settled by eye on the +Z-facing asset.
        const float root_angle = core::radians(tuning.foot_hang_deg) * airborne * model_forward_z_;
        // Talons open on the attack: the relaxed curl gives way to a spread.
        float clawing = 0.0f;
        if (claw_time_ < tuning.claw_duration) {
            clawing = std::sin(core::PI * claw_time_ / core::maxf(tuning.claw_duration, 1e-3f));
        }
        const float curl_angle =
            core::radians(tuning.toe_curl_deg -
                          tuning.attack_toe_spread_deg * core::maxf(breath_smoothed_, clawing)) *
            airborne * model_forward_z_;
        for (const auto& [joint, depth] : foot_joints_) {
            rotate_joint(joint, Vec3::unit_x(), depth == 0 ? root_angle : curl_angle, true);
        }
    }

    // On the ground the authored stance wins the WHOLE body -- wings, tail,
    // legs, neck -- not just the joints the rig never owned. A wyvern folds its
    // wings into forelegs on the ground and a stick-straight tail sim cannot
    // know that; the artist did. Contact only: the approach is the rig's.
    if (has_clip && ground_contact_ > 0.001f && tuning.base_clip_weight > 0.001f) {
        // A quadruped keeps its wings with the rig even here: it stands on its
        // legs, and this asset's authored fold drapes the membranes to the
        // ground. A wyvern has no other forelegs -- its wings ARE the stance.
        const bool quadruped = !joints_.front_leg[0].empty() || !joints_.front_leg[1].empty();
        std::vector<Transform> rig_wings;
        std::vector<int> wing_joints;
        if (quadruped) {
            for (int side = 0; side < 2; ++side) {
                for (const int j : joints_.wing_root[side]) wing_joints.push_back(j);
                for (const auto& finger : joints_.wing_fingers[side]) {
                    for (const int j : finger) wing_joints.push_back(j);
                }
            }
            for (const int j : wing_joints) rig_wings.push_back(pose_.local[size_t(j)]);
        }
        blend_poses(pose_, clip_pose_, ground_contact_ * core::saturate(tuning.base_clip_weight),
                    pose_);
        for (size_t k = 0; k < wing_joints.size(); ++k) {
            pose_.local[size_t(wing_joints[k])] = rig_wings[k];
        }
    }

    compute_world_matrices(*skeleton_, pose_, world_);
    // The head aim needs posed world matrices to measure against, and changing
    // the head changes its subtree, so the matrices are rebuilt afterwards. Two
    // passes over the hierarchy is nothing next to the skinning it feeds.
    if (aim_active_ && joints_.head != NO_PARENT) {
        aim_head(state);
    }
    // Jaw and tremor after the aim: the aim measures the head and would
    // correct the tremor straight back out.
    drive_attack(airborne);
    compute_world_matrices(*skeleton_, pose_, world_);
    compute_skinning_matrices(*skeleton_, world_, skinning_);
    aim_active_ = false;
}

void DragonRig::drive_attack(float airborne) {
    // Jaw: open with the breath, and a quick gape for the spit that shuts as
    // the round leaves -- open through the rear-back, closed by the whip.
    float spit_open = 0.0f;
    if (spit_time_ < tuning.spit_duration) {
        const float u = spit_time_ / core::maxf(tuning.spit_duration, 1e-3f);
        spit_open = u < 0.7f ? std::sin(core::PI * u / 0.7f) : 0.0f;
    }
    // Bite: the jaws open through the wind-up and the lunge out, snap shut
    // at the lunge's peak, stay shut on the way back.
    float bite_open = 0.0f;
    if (bite_time_ < tuning.bite_duration) {
        const float u = bite_time_ / core::maxf(tuning.bite_duration, 1e-3f);
        const float a = core::clampf(tuning.gesture_anticipation, 0.0f, 0.6f);
        const float peak = a + (1.0f - a) * 0.3f;  // where gesture_strike tops out
        bite_open = u < peak ? std::sin(core::HALF_PI * u / core::maxf(peak, 1e-3f))
                             : (u < peak + 0.12f ? std::cos(core::HALF_PI * (u - peak) / 0.12f) : 0.0f);
    }
    jaw_open_ = core::maxf(core::maxf(breath_smoothed_, spit_open), bite_open);
    if (joints_.jaw != NO_PARENT) {
        // A faint chatter on top of the breath -- the mouth is not a hatch.
        const float chatter = 1.0f + 0.06f * breath_smoothed_ *
                                         std::sin(core::TWO_PI * 9.0f * time_);
        const float opening = core::saturate(jaw_open_ * chatter);
        rotate_joint(joints_.jaw, Vec3::unit_x(),
                     jaw_open_sign_ * core::radians(tuning.jaw_rest_deg +
                                                    tuning.jaw_open_deg * opening),
                     true);
    }

    // Breath tremor on the head: effort, not a wobble. Tiny, two frequencies,
    // on both head axes with different phases.
    if (joints_.head != NO_PARENT && breath_smoothed_ > 1e-3f) {
        const float amplitude = core::radians(tuning.breath_tremor_deg) * breath_smoothed_;
        const float yaw = amplitude * (std::sin(core::TWO_PI * 11.0f * time_) +
                                       0.5f * std::sin(core::TWO_PI * 17.3f * time_ + 1.1f));
        const float pitch = amplitude * 0.7f *
                            (std::sin(core::TWO_PI * 12.7f * time_ + 2.3f) +
                             0.5f * std::sin(core::TWO_PI * 19.1f * time_));
        rotate_joint(joints_.head, Vec3::unit_y(), yaw, true);
        rotate_joint(joints_.head, Vec3::unit_x(), pitch, true);
    }
    (void)airborne;
}

core::Vec3 DragonRig::head_position() const {
    if (joints_.head == NO_PARENT || size_t(joints_.head) >= world_.size()) return Vec3::zero();
    return world_[size_t(joints_.head)].col[3].xyz();
}

void DragonRig::aim_head(const game::FlightState& state) {
    const size_t head = size_t(joints_.head);
    const int parent = skeleton_->joint(joints_.head).parent;
    if (parent == NO_PARENT) return;

    // Everything is done in model space, which is what world_ is expressed in;
    // the aim target arrives in world space, so it comes back through the body
    // transform first.
    const Vec3 target_body =
        core::rotate(core::conjugate(state.orientation), aim_target_ - state.position);

    const Quat head_world = core::quat_from_matrix(world_[head]);
    // Metres, like the target: the head sits a few model units from the
    // origin, and at eight units to the metre that is a visible aim error on
    // a close target.
    const Vec3 head_position = world_[head].col[3].xyz() * model_scale_;
    const Vec3 current = core::normalize_or(core::rotate(head_world, head_axis_local_),
                                            Vec3::forward());
    const Vec3 desired = core::normalize_or(target_body - head_position, current);

    // Clamped, then eased. A neck that can swivel to any angle stops reading as
    // a neck, and snapping to the target loses the sense of a creature choosing
    // to look.
    Quat turn = core::rotation_between(current, desired);
    const float angle = 2.0f * std::acos(core::clampf(std::fabs(turn.w), -1.0f, 1.0f));
    const float limit = core::radians(tuning.head_aim_max_deg);
    float blend = core::saturate(tuning.head_aim_blend);
    if (angle > limit && angle > 1e-4f) blend *= limit / angle;
    turn = core::slerp(Quat::identity(), turn, blend);

    // world_new = turn * world_old, and world = parent_world * local, so the
    // turn has to be carried into the parent's frame before it can be applied to
    // the local rotation.
    const Quat parent_world = core::quat_from_matrix(world_[size_t(parent)]);
    const Quat parent_inverse = core::conjugate(parent_world);
    Transform local = pose_.local[head];
    local.rotation = core::normalize(parent_inverse * turn * parent_world * local.rotation);
    pose_.local[head] = local;
}

}  // namespace anim

// ---------------------------------------------------------------- name mapping

namespace anim {

namespace {

std::string lowered(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    return out;
}

bool contains_any(const std::string& haystack, const std::vector<const char*>& needles) {
    for (const char* needle : needles) {
        if (haystack.find(needle) != std::string::npos) return true;
    }
    return false;
}

std::vector<int> collect(const Skeleton& skeleton, const std::vector<const char*>& include,
                         const std::vector<const char*>& exclude = {}) {
    std::vector<int> found;
    for (int i = 0; i < skeleton.count(); ++i) {
        const std::string name = lowered(skeleton.joint(i).name);
        if (contains_any(name, include) && !contains_any(name, exclude)) found.push_back(i);
    }
    return found;
}

// Orders a set of joints into a parent-to-child chain. Anything not reachable
// from the base is dropped, so a stray match cannot corrupt the chain.
std::vector<int> order_chain(const Skeleton& skeleton, const std::vector<int>& members) {
    std::vector<int> chain;
    if (members.empty()) return chain;

    auto in_members = [&](int index) {
        for (const int m : members) {
            if (m == index) return true;
        }
        return false;
    };

    int base = NO_PARENT;
    for (const int m : members) {
        if (!in_members(skeleton.joint(m).parent)) {
            base = m;
            break;
        }
    }
    if (base == NO_PARENT) return chain;

    int current = base;
    while (current != NO_PARENT) {
        chain.push_back(current);
        int next = NO_PARENT;
        for (const int m : members) {
            if (skeleton.joint(m).parent == current) {
                next = m;
                break;
            }
        }
        current = next;
    }
    return chain;
}

std::vector<int> children_of(const Skeleton& skeleton, int parent) {
    std::vector<int> found;
    for (int i = 0; i < skeleton.count(); ++i) {
        if (skeleton.joint(i).parent == parent) found.push_back(i);
    }
    return found;
}

// Mean X of a joint and everything below it.
//
// Which side a wing belongs to cannot be read from its root bone: on this asset
// both wing roots sit at x = 0.06, on the centreline, and the side only becomes
// apparent out at the finger bones. The subtree tells the truth.
float subtree_mean_x(const Skeleton& skeleton, int root) {
    float total = 0.0f;
    int count = 0;
    std::vector<int> stack{root};
    while (!stack.empty()) {
        const int current = stack.back();
        stack.pop_back();
        total += skeleton.world_bind(current).translation_part().x;
        ++count;
        for (int i = 0; i < skeleton.count(); ++i) {
            if (skeleton.joint(i).parent == current) stack.push_back(i);
        }
    }
    return count > 0 ? total / float(count) : 0.0f;
}

int ancestor_depth(const Skeleton& skeleton, int joint) {
    int depth = 0;
    while (joint != NO_PARENT) {
        joint = skeleton.joint(joint).parent;
        ++depth;
    }
    return depth;
}

int subtree_size(const Skeleton& skeleton, int root) {
    int count = 0;
    std::vector<int> stack{root};
    while (!stack.empty()) {
        const int current = stack.back();
        stack.pop_back();
        ++count;
        for (const int child : children_of(skeleton, current)) stack.push_back(child);
    }
    return count;
}

// Children that carry a chain rather than a bare leaf (an *_end* export
// artifact, a lone corrective).
std::vector<int> significant_children(const Skeleton& skeleton, int joint) {
    std::vector<int> found;
    for (const int child : children_of(skeleton, joint)) {
        if (subtree_size(skeleton, child) >= 2) found.push_back(child);
    }
    return found;
}

// Where a chain genuinely branches: the children that are peers of the
// largest. Fingers off a hand are peers; an elbow corrective hanging off the
// upper arm is not -- the arm carries the whole wing beyond it, so the main
// line continues and the corrective is left alone.
std::vector<int> branches_of(const Skeleton& skeleton, int joint) {
    std::vector<int> children = significant_children(skeleton, joint);
    int largest = 0;
    for (const int child : children) largest = std::max(largest, subtree_size(skeleton, child));
    std::vector<int> peers;
    for (const int child : children) {
        if (subtree_size(skeleton, child) * 3 >= largest) peers.push_back(child);
    }
    return peers;
}

// Drops leading chain joints that sit almost on top of the next one -- a
// "TailBase" or control bone parked on the root, not a vertebra. A 26 cm stub
// at the head of an 8 m tail can swing through 180 degrees inside one frame
// for almost no cost, and the length constraints then whip the whole chain
// after it: the one-frame kick the Prowler's tail showed at every reversal.
void trim_stub_base(const Skeleton& skeleton, std::vector<int>& chain) {
    while (chain.size() >= 3) {
        float total = 0.0f;
        for (size_t i = 1; i < chain.size(); ++i) {
            total += core::distance(skeleton.world_bind(chain[i]).translation_part(),
                                    skeleton.world_bind(chain[i - 1]).translation_part());
        }
        const float mean = total / float(chain.size() - 1);
        const float first = core::distance(skeleton.world_bind(chain[1]).translation_part(),
                                           skeleton.world_bind(chain[0]).translation_part());
        if (first < 0.25f * mean) {
            chain.erase(chain.begin());
        } else {
            break;
        }
    }
}

// Follows a chain down its main line. Helpers hanging off the chain do not end
// it; a genuine branch -- the fingers off a hand, the toes off a foot -- does.
// `stop_at` ends the chain before a joint the caller wants to own separately.
std::vector<int> descend_main(const Skeleton& skeleton, int start,
                              const std::vector<int>& stop_at = {},
                              bool extend_into_leaf = false) {
    std::vector<int> chain;
    int current = start;
    while (current != NO_PARENT) {
        bool stop = false;
        for (const int s : stop_at) stop = stop || s == current;
        if (stop && !chain.empty()) break;
        chain.push_back(current);
        const std::vector<int> next = branches_of(skeleton, current);
        if (next.size() != 1) {
            // A chain that ran out of SIGNIFICANT children may still have one
            // more real bone below it: `significant_children` needs a subtree of
            // two, so any bone whose child is a leaf ends one bone early. That
            // rule was written for the first asset, whose leaves are genuine
            // `_end` export artifacts carrying no vertices -- and it is wrong
            // for every other asset here. The wyvern's four finger ribs
            // (`wing_finger_Nb`) are leaves holding roughly half the membrane,
            // and the Prowler's third phalanges are leaves too; all of them rode
            // rigidly on their parent, which is why a finger could only swing at
            // its base and the membrane had no knuckle to furl at.
            //
            // Only when the chain has ALREADY terminated and there is exactly
            // one child: a lone leaf beside a real branch is a corrective (the
            // Prowler's elbow helpers), and treating it as a branch would end
            // the arm at the shoulder.
            if (extend_into_leaf && next.empty()) {
                const std::vector<int> leaves = children_of(skeleton, current);
                if (leaves.size() == 1 &&
                    skeleton.joint(leaves[0]).name.find("_end") == std::string::npos) {
                    chain.push_back(leaves[0]);
                }
            }
            break;
        }
        current = next[0];
    }
    return chain;
}

// Names that mark rig plumbing rather than the animal: IK targets, pole
// vectors, controllers, corrective chains. Riggers spell them many ways; these
// cover the assets seen so far.
const std::vector<const char*>& helper_names() {
    static const std::vector<const char*> names{"_end", "ik", "pole", "cont", "target", "chain",
                                                "roll", "fly", "muscle", "kneecap"};
    return names;
}

}  // namespace

DragonJoints map_dragon_joints(const Skeleton& skeleton) {
    DragonJoints j;

    for (int i = 0; i < skeleton.count(); ++i) {
        if (skeleton.joint(i).parent == NO_PARENT) {
            j.root = i;
            break;
        }
    }

    // Chest: whichever body bone the wings and neck hang off. Named variously,
    // so several spellings are tried.
    const std::vector<int> chest = collect(skeleton, {"breast", "chest", "spine", "torso"});
    j.chest = chest.empty() ? j.root : chest.back();

    // "skin" and "ik" bones share the neck's name but are helpers, not the chain.
    j.neck = order_chain(skeleton, collect(skeleton, {"neck"}, {"skin", "ik_", "_end"}));
    const std::vector<int> head = collect(skeleton, {"head"}, {"ik", "_end", "target"});
    j.head = head.empty() ? NO_PARENT : head.front();
    j.tail = order_chain(skeleton, collect(skeleton, {"tail"}, {"cont", "_end"}));
    trim_stub_base(skeleton, j.tail);
    trim_stub_base(skeleton, j.neck);

    // Wings. Side comes from the bind position's X sign rather than from the
    // name: riggers label sides from the creature's point of view or the
    // viewer's, inconsistently, and this model calls its +X wing "_L".
    const std::vector<int> wing_candidates =
        collect(skeleton, {"w_c", "wing", "shoulder"}, helper_names());
    for (int side = 0; side < 2; ++side) {
        int best = NO_PARENT;
        int best_depth = 0;
        for (const int candidate : wing_candidates) {
            if ((subtree_mean_x(skeleton, candidate) > 0.0f) != (side == 0)) continue;
            // Prefer the candidate nearest the root: the shared base of the
            // wing, not a bone partway along it.
            const int depth = ancestor_depth(skeleton, candidate);
            if (best == NO_PARENT || depth < best_depth) {
                best = candidate;
                best_depth = depth;
            }
        }
        if (best == NO_PARENT) continue;

        // Shared arm first: descend the main line. Where it genuinely
        // branches, each branch is a finger.
        j.wing_root[side] = descend_main(skeleton, best, {}, true);
        const int branch_point = j.wing_root[side].back();
        for (const int finger_base : branches_of(skeleton, branch_point)) {
            j.wing_fingers[side].push_back(descend_main(skeleton, finger_base, {}, true));
        }
        // A wing with no branches (a simple three-bone arm) keeps its last bone
        // as a single "finger", so downstream code always has one.
        if (j.wing_fingers[side].empty() && j.wing_root[side].size() > 1) {
            j.wing_fingers[side].push_back({j.wing_root[side].back()});
            j.wing_root[side].pop_back();
        }
    }

    // Feet are found first: a leg chain ends where the foot begins, so the foot
    // can be re-anchored or left alone as its rig demands. Nothing inside a
    // wing is a foot, whatever it is called (this rig's wing hands are "Hand").
    const std::vector<int> foot_candidates_all =
        collect(skeleton, {"hand", "food", "foot", "fuss", "paw"}, helper_names());
    std::vector<int> foot_candidates;
    for (const int candidate : foot_candidates_all) {
        bool in_wing = false;
        for (int side = 0; side < 2; ++side) {
            const int wing_base = j.wing_root[side].empty() ? NO_PARENT : j.wing_root[side].front();
            for (int p = candidate; p != NO_PARENT && wing_base != NO_PARENT; p = skeleton.joint(p).parent) {
                if (p == wing_base) in_wing = true;
            }
        }
        if (!in_wing) foot_candidates.push_back(candidate);
    }

    // Legs, again sided by bind position. Several bones may match (a thigh
    // base, a corrective chain, the thigh itself): the one that grows the
    // longest chain is the leg.
    const std::vector<int> leg_candidates =
        collect(skeleton, {"oberschenkel", "thigh", "upperleg", "hip", "femur"}, helper_names());
    for (int side = 0; side < 2; ++side) {
        for (const int candidate : leg_candidates) {
            if ((subtree_mean_x(skeleton, candidate) > 0.0f) != (side == 0)) continue;
            std::vector<int> chain = descend_main(skeleton, candidate, foot_candidates);
            if (chain.size() > j.leg[side].size()) j.leg[side] = chain;
        }
    }

    // Forelegs: shoulder-rooted chains. "ik_" is deliberately NOT excluded here
    // -- on this asset the deforming forearm bone is named ik_underarm.
    const std::vector<int> arm_candidates =
        collect(skeleton, {"upper_arm", "oberarm", "foreleg"}, {"_end"});
    for (int side = 0; side < 2; ++side) {
        for (const int candidate : arm_candidates) {
            if ((subtree_mean_x(skeleton, candidate) > 0.0f) != (side == 0)) continue;
            j.front_leg[side] = descend_main(skeleton, candidate, foot_candidates);
            // descend_single happily walks into export-artifact leaves.
            while (!j.front_leg[side].empty() &&
                   skeleton.joint(j.front_leg[side].back()).name.find("_end_") !=
                       std::string::npos) {
                j.front_leg[side].pop_back();
            }
            break;
        }
    }

    // Jaw: by name if the rigger named it, otherwise the parent of the lower
    // lip -- on this asset the jaw is an anonymous "Bone_024" whose children
    // are the lower lip and the tongue. Either way it must sit under the head.
    {
        auto under_head = [&](int joint) {
            for (int p = joint; p != NO_PARENT; p = skeleton.joint(p).parent) {
                if (p == j.head) return true;
            }
            return false;
        };
        std::vector<int> jaw = collect(skeleton, {"jaw", "kiefer", "mandib"}, {"_end", "ik_", "cont"});
        for (const int candidate : jaw) {
            if (under_head(candidate)) {
                j.jaw = candidate;
                break;
            }
        }
        if (j.jaw == NO_PARENT) {
            const std::vector<int> lip = collect(skeleton, {"lower_lip", "lowerlip", "unterlippe"},
                                                 {"_end"});
            for (const int candidate : lip) {
                const int parent = skeleton.joint(candidate).parent;
                if (parent != NO_PARENT && parent != j.head && under_head(parent)) {
                    j.jaw = parent;
                    break;
                }
            }
        }
    }

    // Foot roots. One asset parents each foot straight to the body (IK
    // targets); another hangs them off the shin. Either way they are found by
    // name; nested matches are toes and belong to their root's subtree.
    for (const int candidate : foot_candidates) {
        bool nested = false;
        for (int p = skeleton.joint(candidate).parent; p != NO_PARENT;
             p = skeleton.joint(p).parent) {
            for (const int other : foot_candidates) {
                if (other == p) nested = true;
            }
        }
        if (!nested) j.foot_roots.push_back(candidate);
    }

    LOG_INFO("mapped rig: neck %zu, tail %zu, wing root %zu/%zu, fingers %zu/%zu, legs %zu/%zu, "
             "front legs %zu/%zu, feet %zu, jaw %s",
             j.neck.size(), j.tail.size(), j.wing_root[0].size(), j.wing_root[1].size(),
             j.wing_fingers[0].size(), j.wing_fingers[1].size(), j.leg[0].size(),
             j.leg[1].size(), j.front_leg[0].size(), j.front_leg[1].size(),
             j.foot_roots.size(), j.jaw == NO_PARENT ? "none" : skeleton.joint(j.jaw).name.c_str());
    return j;
}

}  // namespace anim
