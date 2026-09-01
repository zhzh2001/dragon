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
    const float tuck = state.wing_tuck;
    const float flare = state.wing_brake;
    // Sweep and fold go AFT and twist is washout (leading edge down): both are
    // rotations whose sense depends on which way the model faces.
    const float aft = model_forward_z_;
    // Past cruise the wings sweep back and part-fold on their own -- a stoop
    // is a shape speed makes, not only a button. The speed posture fills in
    // whatever the tuck has not already taken.
    const float speed_factor =
        core::smoothstep(tuning.sweep_speed_start, tuning.sweep_speed_full, state.airspeed);
    const float speed_share = speed_factor * (1.0f - tuck);
    const float sweep_deg = tuning.tuck_sweep_deg * tuck + tuning.speed_sweep_deg * speed_share -
                            tuning.load_forward_sweep_deg * core::maxf(load_smoothed_, 0.0f);
    const float fold_deg = tuning.tuck_fold_deg * tuck + tuning.speed_fold_deg * speed_share;
    const float droop_deg =
        tuning.tuck_droop_deg * (tuck + speed_share * tuning.speed_sweep_deg /
                                            core::maxf(tuning.tuck_sweep_deg, 1.0f));
    const float base = state.wing_angle + core::radians(tuning.wing_load_flex_deg) * load_smoothed_ -
                       core::radians(droop_deg);
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
    const float upstroke = core::smoothstep(core::radians(20.0f), core::radians(50.0f), base);
    const float upstroke_fold = core::radians(tuning.upstroke_fold_deg) * upstroke;

    for (int side = 0; side < 2; ++side) {
        const float sign = side == 0 ? 1.0f : -1.0f;

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

        // On the upstroke the flap redistributes outboard: the humerus barely
        // elevates and the wrist leads, which is how a real bird raises its
        // wings -- and it is what keeps the two inner membranes from crossing
        // above the spine at the top of the beat. Total tip rotation is
        // unchanged; only the shape of the wing changes.
        const float shoulder_cut = 0.65f * upstroke;
        auto flap_share_at = [&](size_t k) {
            return std::pow(tuning.outboard_decay, float(k)) *
                   (k < 2 ? 1.0f - shoulder_cut : 1.0f);
        };
        float flap_total = 0.0f;
        for (size_t k = 0; k < root_len + longest_finger; ++k) flap_total += flap_share_at(k);
        const float flap_normalize = flap_total > 1e-4f ? 1.0f / flap_total : 1.0f;

        int depth = 0;
        int finger_index = -1;  // -1 while walking the shared root
        auto apply = [&](int joint, int index_in_chain, int chain_length) {
            if (joint == NO_PARENT) return;
            const float lag = 1.0f - core::minf(tuning.wing_phase_lag * float(depth), 0.8f);
            const float flap_angle = (base * sign + roll_lean) *
                                     flap_share_at(size_t(depth)) * flap_normalize * lag;

            // Folding sweeps back about local Y and closes progressively toward
            // the tip, which is how a wing actually stows.
            const float progress = chain_length > 1
                                       ? float(index_in_chain) / float(chain_length - 1)
                                       : 1.0f;
            const float sweep = core::radians(sweep_deg) * sign * aft *
                                (0.4f + 0.6f * progress) * normalize;
            const float fold = (core::radians(fold_deg) +
                                upstroke_fold * progress) * progress * sign * aft * normalize;
            const float flare_angle =
                core::radians(tuning.brake_flare_deg) * flare * sign * normalize;

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
                          sign * normalize;
                finger_twist = twist * progress * normalize;
            }

            // Flap is a rotation about the body's forward axis; sweep is about
            // the body's up axis.
            rotate_joint(joint, Vec3::unit_y(), sweep + fold, Vec3::unit_z(),
                         flap_angle - flare_angle + flutter);
            if (std::fabs(finger_twist) > 1e-5f) {
                rotate_joint(joint, Vec3::unit_x(), finger_twist, true);
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
    const float stiffness = tuning.chain_stiffness * feel.stiffness * tone;
    const float damping =
        tuning.chain_damping * feel.damping * std::sqrt(feel.stiffness * tone);

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

        // Spring back toward the (possibly steered) target shape, so the chain
        // has a shape to return to rather than dangling.
        acceleration += (target[i] - r) * stiffness;
        acceleration -= v * damping;

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
                    // spring fights a phantom momentum forever.
                    sim.velocity[i] = sim.velocity[i] * 0.5f;
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
                const float allowed = rest_bend + core::radians(tuning.chain_max_bend_deg);
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
    const float tuck_angle = core::radians(tuning.leg_tuck_deg) * airborne;
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
        desired = desired * (1.0f - leg_extend_);

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
        const float trail = core::radians(tuning.leg_trail_deg) * airborne *
                            (1.0f - 0.7f * state.wing_tuck);
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
                    rotate_joint(joint, Vec3::unit_x(), trail_angle * aft + swing.x, true);
                    rotate_joint(joint, Vec3::unit_z(), swing.y, true);
                    first = false;
                }
                sign *= -1.15f;
            }
        };
        drive_limb(joints_.leg[side], trail);
        drive_limb(joints_.front_leg[side],
                   core::radians(tuning.front_leg_trail_deg) * airborne);
    }
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
    if (base_clip_ && base_clip_->valid() && clip_weight > 0.001f) {
        clip_time_ += dt * tuning.base_clip_rate;
        if (clip_weight >= 0.999f) {
            base_clip_->sample(clip_time_, pose_);
        } else {
            Pose clip_pose;
            clip_pose.reset_to_bind(*skeleton_);
            base_clip_->sample(clip_time_, clip_pose);
            blend_poses(pose_, clip_pose, clip_weight, pose_);
        }
    }

    drive_wings(state);

    std::vector<int> neck_with_head = joints_.neck;
    if (joints_.head != NO_PARENT) neck_with_head.push_back(joints_.head);

    // The tail steers. Yaw and roll input swing it toward the outside of the
    // commanded turn (a rudder pushing the tail across the airflow); pitch
    // input works it as an elevator, dropping the tail as the nose rises. The
    // control positions are already smoothed by the flight model.
    core::Vec2 tail_steer{
        state.control.x * tuning.tail_elevator_deg,
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

    // Named fields, not positional braces: a positional initializer here once
    // silently dropped the neck's brace, aero gate and articulation range when
    // the struct grew, and every fix routed through them became dead code.
    ChainFeel tail_feel;
    tail_feel.damping = tuning.tail_damping_scale;
    tail_feel.range_deg = tuning.tail_range_deg;

    ChainFeel neck_feel;
    // A breathing neck is tensed: it holds the flame steady. A spitting neck
    // is a strike: several times stiffer for the gesture, which is what makes
    // an overdamped, heavy neck fast enough to rear and whip inside half a
    // second instead of absorbing the impulse.
    const float spitting = spit_time_ < tuning.spit_duration ? 1.0f : 0.0f;
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

    // Feet: first anchor them to the posed legs (needs world matrices), then
    // the relaxed hang and claw curl compose on top.
    compute_world_matrices(*skeleton_, pose_, world_);
    const float airborne = 1.0f - leg_extend_;
    attach_feet(airborne);
    if (airborne > 0.001f) {
        // Hang and curl were settled by eye on the +Z-facing asset.
        const float root_angle = core::radians(tuning.foot_hang_deg) * airborne * model_forward_z_;
        // Talons open on the attack: the relaxed curl gives way to a spread.
        const float curl_angle = core::radians(tuning.toe_curl_deg -
                                               tuning.attack_toe_spread_deg * breath_smoothed_) *
                                 airborne * model_forward_z_;
        for (const auto& [joint, depth] : foot_joints_) {
            rotate_joint(joint, Vec3::unit_x(), depth == 0 ? root_angle : curl_angle, true);
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
    jaw_open_ = core::maxf(breath_smoothed_, spit_open);
    if (joints_.jaw != NO_PARENT && jaw_open_ > 1e-3f) {
        // A faint chatter on top of the breath -- the mouth is not a hatch.
        const float chatter = 1.0f + 0.06f * breath_smoothed_ *
                                         std::sin(core::TWO_PI * 9.0f * time_);
        rotate_joint(joints_.jaw, Vec3::unit_x(),
                     jaw_open_sign_ * core::radians(tuning.jaw_open_deg) * jaw_open_ * chatter,
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

// Follows a chain down while each joint has exactly one child.
std::vector<int> descend_single(const Skeleton& skeleton, int start) {
    std::vector<int> chain;
    int current = start;
    while (current != NO_PARENT) {
        chain.push_back(current);
        const std::vector<int> children = children_of(skeleton, current);
        if (children.size() != 1) break;
        current = children[0];
    }
    return chain;
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

    // Wings. Side comes from the bind position's X sign rather than from the
    // name: riggers label sides from the creature's point of view or the
    // viewer's, inconsistently, and this model calls its +X wing "_L".
    const std::vector<int> wing_candidates =
        collect(skeleton, {"w_c", "wing", "shoulder"}, {"_end"});
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

        // Shared arm first: descend while there is exactly one child. Where it
        // branches, each branch is a finger.
        j.wing_root[side] = descend_single(skeleton, best);
        const int branch_point = j.wing_root[side].back();
        for (const int finger_base : children_of(skeleton, branch_point)) {
            j.wing_fingers[side].push_back(descend_single(skeleton, finger_base));
        }
        // A wing with no branches (a simple three-bone arm) keeps its last bone
        // as a single "finger", so downstream code always has one.
        if (j.wing_fingers[side].empty() && j.wing_root[side].size() > 1) {
            j.wing_fingers[side].push_back({j.wing_root[side].back()});
            j.wing_root[side].pop_back();
        }
    }

    // Legs, again sided by bind position.
    const std::vector<int> leg_candidates =
        collect(skeleton, {"oberschenkel", "thigh", "upperleg", "hip", "femur"}, {"_end"});
    for (int side = 0; side < 2; ++side) {
        for (const int candidate : leg_candidates) {
            if ((subtree_mean_x(skeleton, candidate) > 0.0f) != (side == 0)) continue;
            j.leg[side] = descend_single(skeleton, candidate);
            break;
        }
    }

    // Forelegs: shoulder-rooted chains. "ik_" is deliberately NOT excluded here
    // -- on this asset the deforming forearm bone is named ik_underarm.
    const std::vector<int> arm_candidates =
        collect(skeleton, {"upper_arm", "oberarm", "foreleg"}, {"_end"});
    for (int side = 0; side < 2; ++side) {
        for (const int candidate : arm_candidates) {
            if ((subtree_mean_x(skeleton, candidate) > 0.0f) != (side == 0)) continue;
            j.front_leg[side] = descend_single(skeleton, candidate);
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

    // Foot roots: this asset parents each foot straight to the body (IK
    // targets), so they are found by name and never as leg descendants. Nested
    // matches are toes and belong to their root's subtree, not this list.
    const std::vector<int> foot_candidates =
        collect(skeleton, {"hand", "food", "foot", "fuss", "paw"}, {"_end", "ik_", "target"});
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
