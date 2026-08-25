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

    setup_chain(tail_sim_, joints.tail);
    // The head rides on the end of the neck, so it is simulated as part of it.
    std::vector<int> neck_with_head = joints.neck;
    if (joints.head != NO_PARENT) neck_with_head.push_back(joints.head);
    setup_chain(neck_sim_, neck_with_head);
    have_previous_ = false;

    compute_world_matrices(skeleton, pose_, world_);
    compute_skinning_matrices(skeleton, world_, skinning_);

    // Which way the head points, measured rather than assumed. Prefer the
    // direction to a child bone -- a snout or jaw -- and fall back to the
    // direction the neck grew, which is where the head faces on any sane rig.
    head_axis_local_ = Vec3::forward();
    if (joints.head != NO_PARENT) {
        const Vec3 head_position = world_[size_t(joints.head)].col[3].xyz();
        Vec3 direction = Vec3::zero();
        for (int i = 0; i < count; ++i) {
            if (skeleton.joint(i).parent != joints.head) continue;
            direction = world_[size_t(i)].col[3].xyz() - head_position;
            break;
        }
        if (core::length_sq(direction) < 1e-8f) {
            const int parent = skeleton.joint(joints.head).parent;
            if (parent != NO_PARENT) {
                direction = head_position - world_[size_t(parent)].col[3].xyz();
            }
        }
        if (core::length_sq(direction) > 1e-8f) {
            const Quat head_world = core::quat_from_matrix(world_[size_t(joints.head)]);
            head_axis_local_ = core::normalize(
                core::rotate(core::conjugate(head_world), core::normalize(direction)));
        }
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
    const float base = state.wing_angle + core::radians(tuning.wing_load_flex_deg) * load_smoothed_;
    const float tuck = state.wing_tuck;
    const float flare = state.wing_brake;
    // Roll lean is deliberately NOT mirrored between sides: the same rotation
    // about the body's forward axis on both wings tips one up and one down,
    // which is exactly the shape that produces a roll.
    const float roll_lean = core::radians(tuning.wing_roll_lean_deg) * state.control.z;

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

        int depth = 0;
        auto apply = [&](int joint, int index_in_chain, int chain_length) {
            if (joint == NO_PARENT) return;
            const float decay = std::pow(tuning.outboard_decay, float(depth)) * normalize;
            const float lag = 1.0f - core::minf(tuning.wing_phase_lag * float(depth), 0.8f);
            const float flap_angle = (base * sign + roll_lean) * decay * lag;

            // Folding sweeps back about local Y and closes progressively toward
            // the tip, which is how a wing actually stows.
            const float progress = chain_length > 1
                                       ? float(index_in_chain) / float(chain_length - 1)
                                       : 1.0f;
            const float sweep = core::radians(tuning.tuck_sweep_deg) * tuck * sign *
                                (0.4f + 0.6f * progress) * normalize;
            const float fold =
                core::radians(tuning.tuck_fold_deg) * tuck * progress * sign * normalize;
            const float flare_angle =
                core::radians(tuning.brake_flare_deg) * flare * sign * normalize;

            // Flap is a rotation about the body's forward axis; sweep is about
            // the body's up axis.
            rotate_joint(joint, Vec3::unit_y(), sweep + fold, Vec3::unit_z(),
                         flap_angle - flare_angle);
            ++depth;
        };

        const std::vector<int>& root = joints_.wing_root[side];
        for (size_t i = 0; i < root.size(); ++i) apply(root[i], int(i), int(root.size()));

        // Every finger restarts from the shared root's depth, so they fold
        // together rather than fanning out unevenly.
        const int root_depth = depth;
        for (const std::vector<int>& finger : joints_.wing_fingers[side]) {
            depth = root_depth;
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
                            Vec3 angular_acceleration, core::Vec2 steer_deg, float dt) {
    if (!sim.initialized || chain.size() < 2) return;

    // Active steering: rotate the chain's target shape about its root. The
    // spring then pulls the simulation toward the deflected shape, so the
    // deflection eases in, overshoots and settles exactly like every passive
    // motion -- one integrator, one look.
    std::vector<Vec3> target = sim.rest;
    if (std::fabs(steer_deg.x) > 1e-3f || std::fabs(steer_deg.y) > 1e-3f) {
        const Quat bias =
            core::Quat::from_axis_angle(Vec3::unit_y(), core::radians(steer_deg.y)) *
            core::Quat::from_axis_angle(Vec3::unit_x(), core::radians(steer_deg.x));
        for (size_t i = 1; i < target.size(); ++i) {
            target[i] = target[0] + core::rotate(bias, sim.rest[i] - target[0]);
        }
    }

    const Vec3 omega = state.angular_velocity;

    // Gravity, expressed in the dragon's frame.
    const Vec3 gravity_local =
        core::rotate(core::conjugate(state.orientation), Vec3{0.0f, -9.81f, 0.0f}) *
        tuning.chain_gravity;

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

        Vec3 acceleration = gravity_local +
                            (centrifugal + euler + coriolis + linear) * tuning.chain_inertia;

        // Spring back toward the (possibly steered) target shape, so the chain
        // has a shape to return to rather than dangling.
        acceleration += (target[i] - r) * tuning.chain_stiffness;
        acceleration -= v * tuning.chain_damping;

        // Drag against the relative airflow, which damps oscillation and
        // streams the chain aft at speed.
        const Vec3 relative = airflow_local - v;
        acceleration += relative * tuning.chain_drag;

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
    const float max_bend = std::cos(core::radians(tuning.chain_max_bend_deg));
    for (int iteration = 0; iteration < tuning.chain_iterations; ++iteration) {
        for (size_t i = 1; i < count; ++i) {
            Vec3 direction = sim.position[i] - sim.position[i - 1];
            const float length = core::length(direction);
            if (length < 1e-5f) {
                direction = core::normalize_or(sim.rest[i] - sim.rest[i - 1], Vec3::forward());
            } else {
                direction = direction / length;
            }

            if (i >= 2) {
                // Limit the angle against the previous segment.
                const Vec3 previous = core::normalize_or(
                    sim.position[i - 1] - sim.position[i - 2], direction);
                if (core::dot(previous, direction) < max_bend) {
                    // Rotate the direction back toward the previous segment
                    // until it is inside the cone.
                    const Vec3 axis = core::cross(previous, direction);
                    if (core::length_sq(axis) > 1e-8f) {
                        direction = core::rotate(
                            core::Quat::from_axis_angle(core::normalize(axis),
                                                        core::radians(tuning.chain_max_bend_deg)),
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

void DragonRig::drive_legs(const game::FlightState& state, float dt) {
    // Tucked in flight, extended for landing. Anticipates by extending as the
    // ground gets close rather than waiting for contact.
    const float wants_extend =
        state.grounded || state.ground_clearance < 25.0f ? 1.0f : 0.0f;
    leg_extend_ = core::damp(leg_extend_, wants_extend, 0.22f, dt);

    const float tuck_angle = core::radians(tuning.leg_tuck_deg) * (1.0f - leg_extend_);
    for (int side = 0; side < 2; ++side) {
        // Alternating sign down the chain, so the leg folds like a knee rather
        // than curling into a spiral.
        float sign = 1.0f;
        for (const int joint : joints_.leg[side]) {
            // Added to the authored pose rather than replacing it, so the feet
            // keep whatever motion the clip gives them while still tucking.
            rotate_joint(joint, Vec3::unit_x(), tuck_angle * sign, true);
            sign *= -1.15f;
        }
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

void DragonRig::update(const game::FlightState& state, float dt) {
    if (!skeleton_ || dt <= 0.0f) return;

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
    // The clip is a ground idle, so it fades as flight works the body harder: a
    // real animal goes tense and still in a dive, and toes curling at 100 m/s
    // read as someone else's animation playing on the wrong creature.
    const float clip_weight =
        tuning.base_clip_weight *
        (1.0f - core::saturate(tuning.clip_flight_fade) * intensity_smoothed_);
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
    const core::Vec2 tail_steer{
        state.control.x * tuning.tail_elevator_deg,
        -(state.control.y + 0.5f * state.control.z) * tuning.tail_rudder_deg};

    // The neck leads the manoeuvre and lowers into the wind at speed.
    const float streamline =
        core::saturate(state.airspeed / core::maxf(tuning.streamline_speed, 1.0f));
    const core::Vec2 neck_steer{
        state.control.x * tuning.neck_lead_deg - streamline * tuning.neck_streamline_deg, 0.0f};

    drive_chain(tail_sim_, joints_.tail, state, frame_acceleration, angular_acceleration,
                tail_steer, dt);
    drive_chain(neck_sim_, neck_with_head, state, frame_acceleration, angular_acceleration,
                neck_steer, dt);
    drive_legs(state, dt);

    compute_world_matrices(*skeleton_, pose_, world_);
    // The head aim needs posed world matrices to measure against, and changing
    // the head changes its subtree, so the matrices are rebuilt afterwards. Two
    // passes over the hierarchy is nothing next to the skinning it feeds.
    if (aim_active_ && joints_.head != NO_PARENT) {
        aim_head(state);
        compute_world_matrices(*skeleton_, pose_, world_);
    }
    compute_skinning_matrices(*skeleton_, world_, skinning_);
    aim_active_ = false;
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
    const Vec3 head_position = world_[head].col[3].xyz();
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

    LOG_INFO("mapped rig: neck %zu, tail %zu, wing root %zu/%zu, fingers %zu/%zu, legs %zu/%zu",
             j.neck.size(), j.tail.size(), j.wing_root[0].size(), j.wing_root[1].size(),
             j.wing_fingers[0].size(), j.wing_fingers[1].size(), j.leg[0].size(),
             j.leg[1].size());
    return j;
}

}  // namespace anim
