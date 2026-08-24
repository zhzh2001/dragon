#include "anim/dragon_rig.h"

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
            mesh.add(position, color, indices, weights);
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
        const uint32_t tip_index = mesh.add(tip, top_color, indices, weights);
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
        leading.push_back(mesh.add(station.leading, WING_EDGE, indices, weights));
        trailing.push_back(mesh.add(station.trailing, WING_COLOR, indices, weights));
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
        j.wing[side][0] = shoulder;
        j.wing[side][1] = elbow;
        j.wing[side][2] = wrist;

        const int hip = skeleton.add_joint(
            "hip" + suffix, j.root,
            offset(Vec3{sign * shape.body_radius * 0.6f, -shape.body_radius * 0.4f, 0.0f}));
        const int knee =
            skeleton.add_joint("knee" + suffix, hip, offset(Vec3{0, -shape.leg_upper, 0}));
        j.leg[side][0] = hip;
        j.leg[side][1] = knee;
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
        std::vector<int> leg{j.leg[side][0], j.leg[side][1]};
        std::vector<float> radii{shape.body_radius * 0.26f, shape.body_radius * 0.17f};
        skin_tube(out_mesh, skeleton, leg, radii, 8, LEG_COLOR, LEG_COLOR, true);
    }

    // Wings.
    skin_wing(out_mesh, skeleton, shape, j.wing[0], 1.0f);
    skin_wing(out_mesh, skeleton, shape, j.wing[1], -1.0f);

    out_mesh.recompute_normals();
    LOG_INFO("dragon rig: %d joints, %zu verts, %zu tris", skeleton.count(),
             out_mesh.vertices.size(), out_mesh.indices.size() / 3);
}

// ---------------------------------------------------------------- rig

void DragonRig::Spring::step(float target, float stiffness, float damping, float dt) {
    // Critically-damped-ish spring, integrated semi-implicitly so it stays
    // stable at large stiffness without needing substeps.
    velocity += (target - angle) * stiffness * dt;
    velocity -= velocity * core::minf(damping * dt, 1.0f);
    angle += velocity * dt;
}

void DragonRig::init(const Skeleton& skeleton, const DragonJoints& joints) {
    skeleton_ = &skeleton;
    joints_ = joints;
    pose_.reset_to_bind(skeleton);

    tail_yaw_.assign(joints.tail.size(), Spring());
    tail_pitch_.assign(joints.tail.size(), Spring());
    neck_yaw_.assign(joints.neck.size(), Spring());
    neck_pitch_.assign(joints.neck.size(), Spring());

    compute_world_matrices(skeleton, pose_, world_);
    compute_skinning_matrices(skeleton, world_, skinning_);
}

void DragonRig::drive_wings(const game::FlightState& state) {
    // The flight model already produced the wing angle, so the wing that is
    // drawn and the thrust that was generated cannot disagree.
    const float base = state.wing_angle;
    const float tuck = state.wing_tuck;
    const float flare = state.wing_brake;

    for (int side = 0; side < 2; ++side) {
        const float sign = side == 0 ? 1.0f : -1.0f;
        // Rotating about local Z raises and lowers the wing, since the wing bones
        // run along X.
        const float shoulder_angle = base * sign;
        // Outboard segments lag, which is what gives the beat its whip.
        const float elbow_angle =
            base * tuning.elbow_ratio * sign * (1.0f - tuning.wing_phase_lag);
        const float wrist_angle =
            base * tuning.wrist_ratio * sign * (1.0f - tuning.wing_phase_lag * 2.0f);

        // Folding sweeps the wing back about local Y and closes the joints.
        const float sweep = core::radians(tuning.tuck_sweep_deg) * tuck * sign;
        const float fold = core::radians(tuning.tuck_fold_deg) * tuck;
        const float flare_angle = core::radians(tuning.brake_flare_deg) * flare;

        Transform shoulder = skeleton_->joint(joints_.wing[side][0]).local_bind;
        shoulder.rotation = core::normalize(
            Quat::from_axis_angle(Vec3::unit_y(), sweep * 0.45f) *
            Quat::from_axis_angle(Vec3::unit_z(), shoulder_angle - flare_angle * sign));
        pose_.local[size_t(joints_.wing[side][0])] = shoulder;

        Transform elbow = skeleton_->joint(joints_.wing[side][1]).local_bind;
        elbow.rotation = core::normalize(
            Quat::from_axis_angle(Vec3::unit_y(), sweep * 0.7f + fold * sign) *
            Quat::from_axis_angle(Vec3::unit_z(), elbow_angle));
        pose_.local[size_t(joints_.wing[side][1])] = elbow;

        Transform wrist = skeleton_->joint(joints_.wing[side][2]).local_bind;
        wrist.rotation = core::normalize(
            Quat::from_axis_angle(Vec3::unit_y(), sweep + fold * 1.3f * sign) *
            Quat::from_axis_angle(Vec3::unit_z(), wrist_angle));
        pose_.local[size_t(joints_.wing[side][2])] = wrist;
    }
}

void DragonRig::drive_chain(const std::vector<int>& chain, std::vector<Spring>& yaw,
                            std::vector<Spring>& pitch, float response,
                            const game::FlightState& state, float dt) {
    // Body angular velocity is the driver: the chain trails whichever way the
    // body is rotating. Deflection accumulates along the chain, so the tip moves
    // furthest -- that is what makes it read as inertia rather than as a bend.
    const float limit = core::radians(tuning.chain_limit_deg);
    for (size_t i = 0; i < chain.size(); ++i) {
        const float depth = std::pow(float(i + 1) / float(chain.size()), tuning.chain_falloff);
        // Yaw follows the body's yaw rate, pitch follows its pitch rate, both
        // opposed because the chain lags behind the turn.
        const float yaw_target =
            core::clampf(-state.angular_velocity.y * response * depth, -limit, limit);
        const float pitch_target =
            core::clampf(-state.angular_velocity.x * response * depth, -limit, limit);

        yaw[i].step(yaw_target, tuning.chain_stiffness, tuning.chain_damping, dt);
        pitch[i].step(pitch_target, tuning.chain_stiffness, tuning.chain_damping, dt);

        Transform local = skeleton_->joint(chain[i]).local_bind;
        local.rotation = core::normalize(Quat::from_axis_angle(Vec3::unit_y(), yaw[i].angle) *
                                         Quat::from_axis_angle(Vec3::unit_x(), pitch[i].angle));
        pose_.local[size_t(chain[i])] = local;
    }
}

void DragonRig::drive_legs(const game::FlightState& state, float dt) {
    // Tucked in flight, extended for landing. Anticipates by extending as the
    // ground gets close rather than waiting for contact.
    const float wants_extend =
        state.grounded || state.ground_clearance < 25.0f ? 1.0f : 0.0f;
    leg_extend_ = core::damp(leg_extend_, wants_extend, 0.22f, dt);

    const float tuck_angle = core::radians(tuning.leg_tuck_deg) * (1.0f - leg_extend_);
    for (int side = 0; side < 2; ++side) {
        Transform hip = skeleton_->joint(joints_.leg[side][0]).local_bind;
        hip.rotation = Quat::from_axis_angle(Vec3::unit_x(), tuck_angle);
        pose_.local[size_t(joints_.leg[side][0])] = hip;

        Transform knee = skeleton_->joint(joints_.leg[side][1]).local_bind;
        knee.rotation = Quat::from_axis_angle(Vec3::unit_x(), -tuck_angle * 1.15f);
        pose_.local[size_t(joints_.leg[side][1])] = knee;
    }
}

void DragonRig::update(const game::FlightState& state, float dt) {
    if (!skeleton_ || dt <= 0.0f) return;

    drive_wings(state);
    drive_chain(joints_.tail, tail_yaw_, tail_pitch_, tuning.tail_response, state, dt);
    drive_chain(joints_.neck, neck_yaw_, neck_pitch_, tuning.neck_response, state, dt);
    drive_legs(state, dt);

    compute_world_matrices(*skeleton_, pose_, world_);
    compute_skinning_matrices(*skeleton_, world_, skinning_);
}

}  // namespace anim
