// Skeleton, skinning and rig invariants.
//
// Skinning bugs are uniquely nasty to debug by eye: a wrong inverse bind or a
// transposed multiply produces a mesh that is *nearly* right, or one that
// explodes only when a particular joint rotates. These pin the properties that
// make the whole thing trustworthy.
#include <cmath>
#include <cstdio>
#include <memory>

#include "anim/animation.h"
#include "anim/dragon_rig.h"
#include "game/studio.h"
#include "anim/skeleton.h"
#include "anim/skinned_mesh.h"

using namespace core;
using anim::Pose;
using anim::Skeleton;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* what, int line) {
    ++g_checks;
    if (!condition) {
        std::printf("  FAIL (line %d): %s\n", line, what);
        ++g_failures;
    }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
bool near(Vec3 a, Vec3 b, float eps = 1e-4f) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

Transform at(Vec3 position) {
    Transform t;
    t.position = position;
    return t;
}

// A three-link chain along -Z, one metre per link.
Skeleton make_chain() {
    Skeleton skeleton;
    const int root = skeleton.add_joint("root", anim::NO_PARENT, at(Vec3::zero()));
    const int mid = skeleton.add_joint("mid", root, at(Vec3{0, 0, -1}));
    skeleton.add_joint("tip", mid, at(Vec3{0, 0, -1}));
    skeleton.finalize();
    return skeleton;
}

void test_hierarchy() {
    std::printf("world bind accumulates down the hierarchy\n");
    const Skeleton skeleton = make_chain();
    CHECK(skeleton.count() == 3);
    CHECK(near(skeleton.world_bind(0).translation_part(), Vec3::zero()));
    CHECK(near(skeleton.world_bind(1).translation_part(), Vec3{0, 0, -1}));
    CHECK(near(skeleton.world_bind(2).translation_part(), Vec3{0, 0, -2}));

    CHECK(skeleton.find("mid") == 1);
    CHECK(skeleton.find("nonexistent") == anim::NO_PARENT);

    // Parents must precede children, which is what lets world transforms resolve
    // in a single forward pass.
    for (int i = 0; i < skeleton.count(); ++i) CHECK(skeleton.joint(i).parent < i);
}

void test_bind_pose_is_identity() {
    std::printf("the bind pose produces identity skinning matrices\n");
    // The single most important property. If it does not hold, a mesh moves the
    // instant it is skinned even though nothing has been animated -- and the
    // error is a subtle offset rather than an obvious explosion.
    const Skeleton skeleton = make_chain();
    Pose pose;
    pose.reset_to_bind(skeleton);

    std::vector<Mat4> world, skinning;
    anim::compute_world_matrices(skeleton, pose, world);
    anim::compute_skinning_matrices(skeleton, world, skinning);

    float worst = 0.0f;
    for (int i = 0; i < skeleton.count(); ++i) {
        // Probe with several points rather than comparing matrices elementwise:
        // this is the property that actually matters to a vertex.
        const Vec3 probes[3] = {Vec3::zero(), Vec3{1.0f, 2.0f, -3.0f}, Vec3{-4.0f, 0.5f, 6.0f}};
        for (const Vec3& p : probes) {
            worst = maxf(worst, length(transform_point(skinning[size_t(i)], p) - p));
        }
    }
    std::printf("  worst deviation from identity: %.6f\n", worst);
    CHECK(worst < 1e-4f);
}

void test_rotation_moves_children() {
    std::printf("rotating a joint carries its children\n");
    const Skeleton skeleton = make_chain();
    Pose pose;
    pose.reset_to_bind(skeleton);
    // Yaw the root 90 degrees: forward is -Z, so the chain should swing onto -X.
    pose.local[0].rotation = Quat::from_axis_angle(Vec3::unit_y(), HALF_PI);

    std::vector<Mat4> world, skinning;
    anim::compute_world_matrices(skeleton, pose, world);
    anim::compute_skinning_matrices(skeleton, world, skinning);

    const Vec3 tip_bind = skeleton.world_bind(2).translation_part();
    const Vec3 tip_posed = transform_point(skinning[2], tip_bind);
    std::printf("  tip %.2f %.2f %.2f -> %.2f %.2f %.2f\n", tip_bind.x, tip_bind.y, tip_bind.z,
                tip_posed.x, tip_posed.y, tip_posed.z);
    CHECK(near(tip_posed, Vec3{-2.0f, 0.0f, 0.0f}, 1e-3f));

    // The root itself must not move, and lengths must be preserved: a rotation
    // that stretches a bone means the inverse bind is wrong.
    CHECK(near(transform_point(skinning[0], Vec3::zero()), Vec3::zero(), 1e-4f));
    CHECK(near(length(tip_posed), length(tip_bind), 1e-3f));

    // A child rotation must not disturb its parent.
    Pose child_rotated;
    child_rotated.reset_to_bind(skeleton);
    child_rotated.local[2].rotation = Quat::from_axis_angle(Vec3::unit_x(), 1.0f);
    anim::compute_world_matrices(skeleton, child_rotated, world);
    anim::compute_skinning_matrices(skeleton, world, skinning);
    const Vec3 mid_bind = skeleton.world_bind(1).translation_part();
    CHECK(near(transform_point(skinning[1], mid_bind), mid_bind, 1e-4f));
}

void test_weight_normalization() {
    std::printf("vertex weights are normalized\n");
    anim::SkinnedMeshData mesh;
    {
        const int joints[4] = {0, 1, -1, -1};
        const float weights[4] = {3.0f, 1.0f, 0.0f, 0.0f};  // deliberately unnormalized
        const uint32_t index = mesh.add(Vec3::zero(), Vec3::one(), Vec2{0.0f, 0.0f}, joints, weights);
        const anim::SkinnedVertex& v = mesh.vertices[index];
        float total = v.weights[0] + v.weights[1] + v.weights[2] + v.weights[3];
        std::printf("  3:1 becomes %.2f:%.2f (sum %.3f)\n", v.weights[0], v.weights[1], total);
        CHECK(near(total, 1.0f));
        CHECK(near(v.weights[0], 0.75f));
        CHECK(near(v.weights[1], 0.25f));
    }
    {
        // No influence at all must not collapse the vertex to the origin.
        const int joints[4] = {-1, -1, -1, -1};
        const float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        const uint32_t index = mesh.add(Vec3{5, 5, 5}, Vec3::one(), Vec2{0.0f, 0.0f}, joints, weights);
        const anim::SkinnedVertex& v = mesh.vertices[index];
        CHECK(near(v.weights[0], 1.0f));
        CHECK(v.joints[0] == 0);
    }
}

void test_blended_skinning_preserves_shape() {
    std::printf("blended skinning does not shrink a bone\n");
    // A vertex weighted equally between two joints is the case that reveals a
    // lerp where a slerp was needed.
    const Skeleton skeleton = make_chain();
    Pose pose;
    pose.reset_to_bind(skeleton);
    pose.local[1].rotation = Quat::from_axis_angle(Vec3::unit_x(), radians(40.0f));

    std::vector<Mat4> world, skinning;
    anim::compute_world_matrices(skeleton, pose, world);
    anim::compute_skinning_matrices(skeleton, world, skinning);

    const Vec3 point{0.0f, 0.4f, -1.0f};  // on the joint between root and mid
    const Vec3 a = transform_point(skinning[0], point);
    const Vec3 b = transform_point(skinning[1], point);
    const Vec3 blended = a * 0.5f + b * 0.5f;
    // Linear blend skinning does shrink at a joint -- that is inherent -- but it
    // must stay close, not collapse.
    const float shrink = length(blended - Vec3{0, 0, -1}) / length(point - Vec3{0, 0, -1});
    std::printf("  radius retained across a 40 deg joint: %.3f\n", shrink);
    CHECK(shrink > 0.9f);
}

void test_dragon_rig_builds() {
    std::printf("the dragon rig builds coherently\n");
    anim::DragonShape shape;
    Skeleton skeleton;
    anim::DragonJoints joints;
    anim::SkinnedMeshData mesh;
    anim::build_dragon(shape, skeleton, joints, mesh);

    std::printf("  %d joints, %zu verts, %zu tris\n", skeleton.count(), mesh.vertices.size(),
                mesh.indices.size() / 3);
    CHECK(skeleton.count() > 15);
    CHECK(skeleton.count() <= anim::MAX_JOINTS);
    CHECK(!mesh.vertices.empty());
    CHECK(mesh.indices.size() % 3 == 0);

    // Every named chain must have resolved.
    CHECK(joints.root != anim::NO_PARENT);
    CHECK(joints.head != anim::NO_PARENT);
    CHECK(joints.neck.size() == size_t(shape.neck_joints));
    CHECK(joints.tail.size() == size_t(shape.tail_joints));
    CHECK(joints.valid());
    for (int side = 0; side < 2; ++side) {
        CHECK(!joints.wing_root[side].empty());
        CHECK(!joints.wing_fingers[side].empty());
        CHECK(!joints.leg[side].empty());
        for (const int bone : joints.wing_root[side]) CHECK(bone != anim::NO_PARENT);
        for (const std::vector<int>& finger : joints.wing_fingers[side]) {
            for (const int bone : finger) CHECK(bone != anim::NO_PARENT);
        }
    }

    // Every joint index referenced by the mesh must exist, and every weight set
    // must sum to one. An out-of-range index reads garbage on the GPU.
    float worst_sum_error = 0.0f;
    int max_index = 0;
    for (const anim::SkinnedVertex& v : mesh.vertices) {
        float sum = 0.0f;
        for (int i = 0; i < 4; ++i) {
            sum += v.weights[i];
            if (v.weights[i] > 0.0f) max_index = int(v.joints[i]) > max_index ? int(v.joints[i])
                                                                             : max_index;
        }
        worst_sum_error = maxf(worst_sum_error, std::fabs(sum - 1.0f));
    }
    std::printf("  worst weight-sum error %.6f, highest joint index %d of %d\n", worst_sum_error,
                max_index, skeleton.count() - 1);
    CHECK(worst_sum_error < 1e-4f);
    CHECK(max_index < skeleton.count());

    // Left and right wings must be mirrored, or the dragon flies crooked.
    const int right_wingtip = joints.wing_fingers[0].back().back();
    const int left_wingtip = joints.wing_fingers[1].back().back();
    const Vec3 right_tip = skeleton.world_bind(right_wingtip).translation_part();
    const Vec3 left_tip = skeleton.world_bind(left_wingtip).translation_part();
    std::printf("  wing tips at x %+.2f and %+.2f\n", right_tip.x, left_tip.x);
    CHECK(near(right_tip.x, -left_tip.x, 1e-3f));
    CHECK(right_tip.x > 0.0f);
}

void test_rig_responds_to_flight() {
    std::printf("the rig responds to flight state\n");
    anim::DragonShape shape;
    Skeleton skeleton;
    anim::DragonJoints joints;
    anim::SkinnedMeshData mesh;
    anim::build_dragon(shape, skeleton, joints, mesh);

    anim::DragonRig rig;
    rig.init(skeleton, joints);

    // Wings must actually move with the flight model's wing angle.
    game::FlightState level;
    level.wing_angle = 0.0f;
    rig.update(level, 1.0f / 60.0f);
    const int wingtip = joints.wing_fingers[0].back().back();
    const Vec3 flat_tip =
        transform_point(rig.skinning_matrices()[size_t(wingtip)],
                        skeleton.world_bind(wingtip).translation_part());

    game::FlightState raised;
    raised.wing_angle = radians(50.0f);
    for (int i = 0; i < 30; ++i) rig.update(raised, 1.0f / 60.0f);
    const Vec3 raised_tip =
        transform_point(rig.skinning_matrices()[size_t(wingtip)],
                        skeleton.world_bind(wingtip).translation_part());

    std::printf("  wrist rises %.2f m when the wing angle goes to 50 deg\n",
                raised_tip.y - flat_tip.y);
    CHECK(raised_tip.y > flat_tip.y + 0.5f);

    // The tail must lag a sustained turn. This is the signature detail of the
    // whole rig, so it gets an assertion rather than a look.
    game::FlightState turning;
    turning.angular_velocity = Vec3{0.0f, 1.0f, 0.0f};  // yawing
    for (int i = 0; i < 120; ++i) rig.update(turning, 1.0f / 60.0f);
    const int tail_tip = joints.tail.back();
    const Vec3 tail_bind = skeleton.world_bind(tail_tip).translation_part();
    const Vec3 tail_posed = transform_point(rig.skinning_matrices()[size_t(tail_tip)], tail_bind);
    std::printf("  tail tip deflects %.2f m laterally under a 1 rad/s yaw\n",
                std::fabs(tail_posed.x - tail_bind.x));
    CHECK(std::fabs(tail_posed.x - tail_bind.x) > 0.25f);

    // And it must settle back, not oscillate forever: a spring chain with the
    // damping wrong rings indefinitely.
    game::FlightState still;
    for (int i = 0; i < 600; ++i) rig.update(still, 1.0f / 60.0f);
    const Vec3 settled = transform_point(rig.skinning_matrices()[size_t(tail_tip)], tail_bind);
    std::printf("  settles back to %.4f m of deflection\n", std::fabs(settled.x - tail_bind.x));
    CHECK(std::fabs(settled.x - tail_bind.x) < 0.05f);

    // Nothing may produce NaN, including from a violent state.
    game::FlightState violent;
    violent.angular_velocity = Vec3{40.0f, -50.0f, 30.0f};
    violent.wing_angle = 12.0f;
    violent.wing_tuck = 1.0f;
    for (int i = 0; i < 200; ++i) rig.update(violent, 1.0f / 60.0f);
    bool finite = true;
    for (const Mat4& m : rig.skinning_matrices()) {
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                if (!std::isfinite(m.col[c][r])) finite = false;
            }
        }
    }
    CHECK(finite);
}

// A clip's job is to reproduce its keys exactly, interpolate between them, and
// hold rather than wrap at the ends of a track. Getting the last one wrong makes
// a limb snap back to its first key on the final frame of every loop.
void test_animation_track_sampling() {
    std::printf("rotation tracks sample and hold correctly\n");
    anim::RotationTrack track;
    track.joint = 3;
    track.times = {0.0f, 1.0f, 2.0f};
    const Quat a = Quat::identity();
    const Quat b = Quat::from_axis_angle(Vec3::unit_y(), radians(90.0f));
    const Quat c = Quat::from_axis_angle(Vec3::unit_y(), radians(180.0f));
    track.rotations = {a, b, c};

    // Keys reproduce exactly.
    CHECK(near(rotate(track.sample(0.0f), Vec3::unit_z()), rotate(a, Vec3::unit_z())));
    CHECK(near(rotate(track.sample(1.0f), Vec3::unit_z()), rotate(b, Vec3::unit_z())));

    // Halfway between two keys is halfway round the arc.
    const Vec3 half = rotate(track.sample(0.5f), Vec3::unit_z());
    CHECK(near(half, rotate(Quat::from_axis_angle(Vec3::unit_y(), radians(45.0f)), Vec3::unit_z()),
               1e-3f));

    // Outside the track, the endpoints hold.
    CHECK(near(rotate(track.sample(-5.0f), Vec3::unit_z()), rotate(a, Vec3::unit_z())));
    CHECK(near(rotate(track.sample(99.0f), Vec3::unit_z()), rotate(c, Vec3::unit_z())));

    // Step tracks jump rather than blend.
    track.step = true;
    CHECK(near(rotate(track.sample(0.5f), Vec3::unit_z()), rotate(a, Vec3::unit_z())));
    CHECK(near(rotate(track.sample(1.75f), Vec3::unit_z()), rotate(b, Vec3::unit_z())));
}

// The bug this pins: track keys are deltas from the joint's rest, composed onto
// the bind rotation. Written as absolute rotations instead, a clip silently
// discards whatever the bind pose held -- for the real asset, the scene
// transform absorbed by the joint under the root, which rolled the whole dragon
// onto its back while every individual bone still moved plausibly.
void test_clip_rest_reproduces_bind() {
    std::printf("a clip at rest reproduces the bind pose exactly\n");
    Skeleton skeleton;
    skeleton.add_joint("root", anim::NO_PARENT, at(Vec3::zero()));
    // A bind rotation nothing in the clip knows about, standing in for the
    // absorbed scene transform on the real asset.
    Transform tilted;
    tilted.position = Vec3{0.0f, 1.0f, 0.0f};
    tilted.rotation = Quat::from_axis_angle(normalize(Vec3{0.3f, 1.0f, 0.2f}), radians(149.0f));
    skeleton.add_joint("body", 0, tilted);
    skeleton.add_joint("limb", 1, at(Vec3{0.0f, 1.0f, 0.0f}));
    skeleton.finalize();

    // A track that holds its rest value, exactly like the real asset's body
    // joint: constant, and therefore invisible to any measure of how much a
    // track *changes* over time.
    anim::AnimationClip clip;
    clip.name = "still";
    clip.duration = 1.0f;
    anim::RotationTrack held;
    held.joint = 1;
    held.times = {0.0f, 1.0f};
    held.rotations = {Quat::identity(), Quat::identity()};
    clip.tracks.push_back(held);

    Pose bind, sampled;
    bind.reset_to_bind(skeleton);
    sampled.reset_to_bind(skeleton);
    clip.sample(0.4f, sampled);

    std::vector<Mat4> bind_world(size_t(skeleton.count()));
    std::vector<Mat4> sampled_world(bind_world.size());
    anim::compute_world_matrices(skeleton, bind, bind_world);
    anim::compute_world_matrices(skeleton, sampled, sampled_world);
    for (size_t j = 0; j < bind_world.size(); ++j) {
        for (int c = 0; c < 4; ++c) {
            CHECK(near(Vec3{bind_world[j].col[c].x, bind_world[j].col[c].y,
                            bind_world[j].col[c].z},
                       Vec3{sampled_world[j].col[c].x, sampled_world[j].col[c].y,
                            sampled_world[j].col[c].z}));
        }
    }

    // A non-identity delta must rotate *relative* to the bind rotation, never
    // replace it -- so the child ends up somewhere the bind rotation still
    // influences.
    clip.tracks[0].rotations = {Quat::from_axis_angle(Vec3::unit_x(), radians(30.0f)),
                                Quat::from_axis_angle(Vec3::unit_x(), radians(30.0f))};
    Pose moved;
    moved.reset_to_bind(skeleton);
    clip.sample(0.4f, moved);
    CHECK(!near(rotate(moved.local[1].rotation, Vec3::unit_y()),
                rotate(bind.local[1].rotation, Vec3::unit_y()), 1e-3f));
    // The delta is applied in the bone's own frame, so bind then delta.
    const Quat expected = normalize(tilted.rotation * clip.tracks[0].rotations[0]);
    CHECK(near(rotate(moved.local[1].rotation, Vec3::unit_y()),
               rotate(expected, Vec3::unit_y())));
}

void test_animation_clip_loops() {
    std::printf("clips loop and only touch the joints they track\n");
    Skeleton skeleton;
    skeleton.add_joint("root", anim::NO_PARENT, at(Vec3::zero()));
    skeleton.add_joint("a", 0, at(Vec3{0.0f, 1.0f, 0.0f}));
    skeleton.add_joint("b", 1, at(Vec3{0.0f, 1.0f, 0.0f}));
    skeleton.finalize();

    anim::AnimationClip clip;
    clip.name = "spin";
    clip.duration = 2.0f;
    anim::RotationTrack track;
    track.joint = 1;
    track.times = {0.0f, 1.0f, 2.0f};
    track.rotations = {Quat::identity(),
                       Quat::from_axis_angle(Vec3::unit_x(), radians(60.0f)),
                       Quat::identity()};
    clip.tracks.push_back(track);
    CHECK(clip.valid());

    Pose pose;
    pose.reset_to_bind(skeleton);
    clip.sample(1.0f, pose);
    // The tracked joint moved...
    CHECK(!near(rotate(pose.local[1].rotation, Vec3::unit_y()), Vec3::unit_y(), 1e-3f));
    // ...and the untracked ones were left at bind, so a clip can be layered
    // under procedural motion without disturbing what it does not animate.
    CHECK(near(rotate(pose.local[2].rotation, Vec3::unit_y()), Vec3::unit_y()));

    // A time one full duration later must give the same pose.
    Pose looped;
    looped.reset_to_bind(skeleton);
    clip.sample(1.0f + clip.duration * 3.0f, looped);
    CHECK(near(rotate(looped.local[1].rotation, Vec3::unit_y()),
               rotate(pose.local[1].rotation, Vec3::unit_y()), 1e-3f));

    // Negative time is as legal as any other: the clock may run backwards.
    Pose backwards;
    backwards.reset_to_bind(skeleton);
    clip.sample(1.0f - clip.duration, backwards);
    CHECK(near(rotate(backwards.local[1].rotation, Vec3::unit_y()),
               rotate(pose.local[1].rotation, Vec3::unit_y()), 1e-3f));
}

// The point of the base-clip layer: joints the rig does not drive should pick up
// the authored motion, while the ones it does drive stay under its control.
void test_base_clip_layers_under_rig() {
    std::printf("an authored clip animates what the rig does not own\n");
    anim::DragonShape shape;
    Skeleton skeleton;
    anim::DragonJoints joints;
    anim::SkinnedMeshData mesh;
    anim::build_dragon(shape, skeleton, joints, mesh);

    // A clip that swings the head, which the rig leaves alone.
    anim::AnimationClip clip;
    clip.name = "nod";
    clip.duration = 1.0f;
    anim::RotationTrack track;
    track.joint = joints.head;
    track.times = {0.0f, 0.5f, 1.0f};
    track.rotations = {Quat::identity(),
                       Quat::from_axis_angle(Vec3::unit_x(), radians(35.0f)),
                       Quat::identity()};
    clip.tracks.push_back(track);

    game::FlightState state;
    state.velocity = Vec3{0.0f, 0.0f, -30.0f};

    anim::DragonRig rig;
    rig.init(skeleton, joints);
    CHECK(!rig.has_base_clip());
    rig.set_base_clip(&clip);
    CHECK(rig.has_base_clip());

    auto head_direction = [&](anim::DragonRig& r) {
        const Mat4& m = r.world_matrices()[size_t(joints.head)];
        return normalize(Vec3{m.col[1].x, m.col[1].y, m.col[1].z});
    };

    rig.update(state, 0.0f);
    const Vec3 start = head_direction(rig);
    rig.update(state, 0.5f);
    const Vec3 mid = head_direction(rig);
    CHECK(!near(start, mid, 1e-2f));

    // Weight zero must restore exactly the un-layered behaviour, so the slider
    // is an honest A/B rather than an approximation of one.
    anim::DragonRig plain;
    plain.init(skeleton, joints);
    plain.update(state, 0.5f);

    anim::DragonRig muted;
    muted.init(skeleton, joints);
    muted.set_base_clip(&clip);
    muted.tuning.base_clip_weight = 0.0f;
    muted.update(state, 0.5f);
    CHECK(near(head_direction(muted), head_direction(plain)));

    // Every matrix stays finite with a clip layered in.
    bool finite = true;
    for (const Mat4& m : rig.skinning_matrices()) {
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                if (!std::isfinite(m.col[c][r])) finite = false;
            }
        }
    }
    CHECK(finite);
}

// The head aim exists for readability: fire leaves along the aim axis, and a
// head pointing elsewhere makes the shot look like it came from nowhere.
void test_head_aims_at_a_target() {
    std::printf("the head turns toward an aim target\n");
    anim::DragonShape shape;
    Skeleton skeleton;
    anim::DragonJoints joints;
    anim::SkinnedMeshData mesh;
    anim::build_dragon(shape, skeleton, joints, mesh);
    CHECK(joints.head != anim::NO_PARENT);

    game::FlightState state;
    state.velocity = Vec3{0.0f, 0.0f, -40.0f};

    // The head's own forward axis is whatever the rig says it is, so measure the
    // rest direction rather than assuming one.
    anim::DragonRig rig;
    rig.init(skeleton, joints);
    for (int i = 0; i < 30; ++i) rig.update(state, 1.0f / 60.0f);

    // Rotating a joint does not move its origin, so the head's *orientation* is
    // what has to be measured -- a neck-to-head offset cannot see this at all.
    // The look axis is derived from the rest pose rather than assumed, exactly
    // because a real rig's bones each point along their own axis.
    const Quat rest_rotation = quat_from_matrix(rig.world_matrices()[size_t(joints.head)]);
    const Vec3 rest_look =
        normalize(rig.head_position() -
                  rig.world_matrices()[size_t(joints.neck.back())].col[3].xyz());
    const Vec3 look_axis_local = normalize(rotate(conjugate(rest_rotation), rest_look));

    auto head_direction = [&](anim::DragonRig& r) {
        return normalize(
            rotate(quat_from_matrix(r.world_matrices()[size_t(joints.head)]), look_axis_local));
    };
    const Vec3 rest = head_direction(rig);

    // A target hard to the right and level. In body space the dragon faces -Z,
    // so this is roughly 50 degrees off the nose.
    const Vec3 target = state.position + Vec3{120.0f, 0.0f, -100.0f};
    const Vec3 wanted = normalize(target - rig.head_position());

    for (int i = 0; i < 60; ++i) {
        rig.set_aim_target(target);
        rig.update(state, 1.0f / 60.0f);
    }
    const Vec3 aimed = head_direction(rig);

    // Closer to the target than it was, and actually moved.
    CHECK(dot(aimed, wanted) > dot(rest, wanted));
    CHECK(!near(aimed, rest, 1e-2f));

    // Releasing the aim lets it come back: set_aim_target is per frame, so not
    // calling it is how "stop looking" is expressed.
    for (int i = 0; i < 120; ++i) rig.update(state, 1.0f / 60.0f);
    CHECK(dot(head_direction(rig), wanted) < dot(aimed, wanted));

    // The limit is respected: even aiming straight backwards must not swivel the
    // head all the way round.
    anim::DragonRig limited;
    limited.init(skeleton, joints);
    limited.tuning.head_aim_max_deg = 25.0f;
    for (int i = 0; i < 30; ++i) limited.update(state, 1.0f / 60.0f);
    const Vec3 limited_rest = head_direction(limited);
    for (int i = 0; i < 120; ++i) {
        limited.set_aim_target(state.position + Vec3{0.0f, 0.0f, 200.0f});  // behind
        limited.update(state, 1.0f / 60.0f);
    }
    const float swing =
        degrees(std::acos(clampf(dot(limited_rest, head_direction(limited)), -1.0f, 1.0f)));
    // The cap is applied per frame against the remaining error, so the head
    // settles at the cap rather than stepping by it. A little slack for the
    // chain, which is also moving.
    CHECK(swing < limited.tuning.head_aim_max_deg + 12.0f);

    bool finite = true;
    for (const Mat4& m : limited.skinning_matrices()) {
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                if (!std::isfinite(m.col[c][r])) finite = false;
            }
        }
    }
    CHECK(finite);
}

// The active flight responses: the tail steers, the neck leads, the wings carry
// load. Each is measured in steady state, where the passive dynamics have
// settled and any deflection left is the active one.
void test_body_responds_to_control_input() {
    std::printf("tail and neck deflect with control input\n");
    anim::DragonShape shape;
    Skeleton skeleton;
    anim::DragonJoints joints;
    anim::SkinnedMeshData mesh;
    anim::build_dragon(shape, skeleton, joints, mesh);

    game::FlightState cruise;
    cruise.velocity = Vec3{0.0f, 0.0f, -30.0f};
    cruise.airspeed = 30.0f;

    auto settled_rig = [&](const game::FlightState& state) {
        auto rig = std::make_unique<anim::DragonRig>();
        rig->init(skeleton, joints);
        for (int i = 0; i < 240; ++i) rig->update(state, 1.0f / 60.0f);
        return rig;
    };
    auto tail_tip = [&](anim::DragonRig& r) {
        return r.world_matrices()[size_t(joints.tail.back())].col[3].xyz();
    };
    auto head_of = [&](anim::DragonRig& r) {
        return r.world_matrices()[size_t(joints.head)].col[3].xyz();
    };

    const auto neutral = settled_rig(cruise);

    // Held right-yaw input: the tail swings left (-X), toward the outside of
    // the commanded turn, even though nothing is rotating yet.
    game::FlightState yawing = cruise;
    yawing.control = Vec3{0.0f, 1.0f, 0.0f};
    const auto ruddered = settled_rig(yawing);
    CHECK(tail_tip(*ruddered).x < tail_tip(*neutral).x - 0.2f);

    // Held nose-up input: the tail drops and the head rises -- elevator and
    // anticipation respectively.
    game::FlightState pulling = cruise;
    pulling.control = Vec3{1.0f, 0.0f, 0.0f};
    const auto flared = settled_rig(pulling);
    CHECK(tail_tip(*flared).y < tail_tip(*neutral).y - 0.1f);
    CHECK(head_of(*flared).y > head_of(*neutral).y + 0.05f);

    // At speed the neck lowers into the wind.
    game::FlightState fast = cruise;
    fast.velocity = Vec3{0.0f, 0.0f, -90.0f};
    fast.airspeed = 90.0f;
    const auto streamlined = settled_rig(fast);
    CHECK(head_of(*streamlined).y < head_of(*neutral).y - 0.02f);
}

void test_wings_carry_the_load() {
    std::printf("wings flex up under g and lean with roll input\n");
    anim::DragonShape shape;
    Skeleton skeleton;
    anim::DragonJoints joints;
    anim::SkinnedMeshData mesh;
    anim::build_dragon(shape, skeleton, joints, mesh);

    game::FlightState cruise;
    cruise.velocity = Vec3{0.0f, 0.0f, -30.0f};
    cruise.airspeed = 30.0f;
    cruise.g_load = 1.0f;

    auto wingtips = [&](const game::FlightState& state, Vec3& left, Vec3& right) {
        anim::DragonRig rig;
        rig.init(skeleton, joints);
        for (int i = 0; i < 240; ++i) rig.update(state, 1.0f / 60.0f);
        const std::vector<int>& chain0 = rig.world_matrices().empty()
                                             ? joints.wing_root[0]
                                             : joints.wing_root[0];
        (void)chain0;
        const int tip0 = joints.wing_fingers[0].front().back();
        const int tip1 = joints.wing_fingers[1].front().back();
        right = rig.world_matrices()[size_t(tip0)].col[3].xyz();
        left = rig.world_matrices()[size_t(tip1)].col[3].xyz();
    };

    Vec3 left_1g, right_1g;
    wingtips(cruise, left_1g, right_1g);

    // A 3 g pull bows both wingtips upward.
    game::FlightState pulling = cruise;
    pulling.g_load = 3.0f;
    Vec3 left_3g, right_3g;
    wingtips(pulling, left_3g, right_3g);
    CHECK(left_3g.y > left_1g.y + 0.1f);
    CHECK(right_3g.y > right_1g.y + 0.1f);

    // Roll input tips the wings the same way around the body axis: one rises,
    // the other falls. That asymmetry is the roll.
    game::FlightState rolling = cruise;
    rolling.control = Vec3{0.0f, 0.0f, 1.0f};
    Vec3 left_roll, right_roll;
    wingtips(rolling, left_roll, right_roll);
    const float delta_left = left_roll.y - left_1g.y;
    const float delta_right = right_roll.y - right_1g.y;
    CHECK(delta_left * delta_right < 0.0f);  // opposite directions
}

void test_idle_clip_fades_with_intensity() {
    std::printf("the ground idle fades as flight gets violent\n");
    anim::DragonShape shape;
    Skeleton skeleton;
    anim::DragonJoints joints;
    anim::SkinnedMeshData mesh;
    anim::build_dragon(shape, skeleton, joints, mesh);

    // A clip that holds the head 40 degrees off bind: any surviving clip weight
    // shows up as head deflection, so the fade is directly measurable.
    anim::AnimationClip clip;
    clip.name = "held";
    clip.duration = 1.0f;
    anim::RotationTrack track;
    track.joint = joints.head;
    track.times = {0.0f, 1.0f};
    track.rotations = {Quat::from_axis_angle(Vec3::unit_x(), radians(40.0f)),
                       Quat::from_axis_angle(Vec3::unit_x(), radians(40.0f))};
    clip.tracks.push_back(track);

    auto head_deflection = [&](const game::FlightState& state) {
        anim::DragonRig rig;
        rig.init(skeleton, joints);
        rig.set_base_clip(&clip);
        for (int i = 0; i < 240; ++i) rig.update(state, 1.0f / 60.0f);
        const Quat head = quat_from_matrix(rig.world_matrices()[size_t(joints.head)]);

        anim::DragonRig bare;
        bare.init(skeleton, joints);
        for (int i = 0; i < 240; ++i) bare.update(state, 1.0f / 60.0f);
        const Quat rest = quat_from_matrix(bare.world_matrices()[size_t(joints.head)]);

        const Vec3 probe = Vec3::unit_y();
        return degrees(std::acos(clampf(dot(rotate(head, probe), rotate(rest, probe)),
                                        -1.0f, 1.0f)));
    };

    game::FlightState calm;
    calm.velocity = Vec3{0.0f, 0.0f, -30.0f};
    calm.airspeed = 30.0f;
    calm.g_load = 1.0f;

    game::FlightState violent = calm;
    violent.velocity = Vec3{0.0f, 0.0f, -100.0f};
    violent.airspeed = 100.0f;
    violent.wing_tuck = 1.0f;
    violent.g_load = 2.5f;

    const float calm_deflection = head_deflection(calm);
    const float violent_deflection = head_deflection(violent);
    // Mostly intact when calm, mostly suppressed when violent.
    CHECK(calm_deflection > 25.0f);
    CHECK(violent_deflection < calm_deflection * 0.55f);

    // On the ground the idle is exactly right, so nothing fades.
    game::FlightState grounded;
    grounded.grounded = true;
    CHECK(head_deflection(grounded) > 30.0f);
}

// The studio's one promise: its states are dynamically consistent, so the rig
// reacts exactly as it would in flight. A scenario whose angular velocity did
// not match its own orientation curve would exercise the chains with forces
// that no real manoeuvre produces.
void test_studio_states_are_consistent() {
    std::printf("studio scenarios are dynamically consistent\n");
    const Vec3 centre{0.0f, 200.0f, 0.0f};

    // Steady left turn: body yaw rate must match v/R for the scripted turn
    // (35 m/s around 90 m), expressed about the banked body's axes.
    const game::FlightState turn =
        game::studio_state(game::StudioScenario::TurnLeft, 3.0f, centre, 0.0f);
    CHECK(near(length(turn.angular_velocity), 35.0f / 90.0f, 0.01f));
    CHECK(near(length(turn.velocity), 35.0f, 0.1f));
    // Velocity is horizontal in a level turn.
    CHECK(std::fabs(turn.velocity.y) < 0.5f);

    // The dive points steeply down and does not rotate.
    const game::FlightState dive =
        game::studio_state(game::StudioScenario::Dive, 2.0f, centre, 0.0f);
    CHECK(dive.velocity.y < -60.0f);
    CHECK(length(dive.angular_velocity) < 0.01f);
    CHECK(near(dive.wing_tuck, 1.0f));

    // The pull-out actually pitches during the pull, and loads more than 2 g.
    const game::FlightState pull =
        game::studio_state(game::StudioScenario::PullOut, 3.8f, centre, 0.0f);
    CHECK(pull.angular_velocity.x > 0.2f);  // nose-up rate
    CHECK(pull.g_load > 2.0f);

    // Every scenario stays finite over a full loop.
    for (int scenario = 0; scenario < int(game::StudioScenario::Count); ++scenario) {
        for (float t = 0.0f; t < 12.0f; t += 0.37f) {
            const game::FlightState s =
                game::studio_state(game::StudioScenario(scenario), t, centre, 100.0f);
            CHECK(std::isfinite(s.velocity.x + s.velocity.y + s.velocity.z +
                                s.angular_velocity.x + s.angular_velocity.y +
                                s.angular_velocity.z + s.g_load));
        }
    }
}

void test_legs_swing_with_the_frame() {
    std::printf("legs are pendulums: outward in a turn, forward under braking\n");
    anim::DragonShape shape;
    Skeleton skeleton;
    anim::DragonJoints joints;
    anim::SkinnedMeshData mesh;
    anim::build_dragon(shape, skeleton, joints, mesh);

    auto foot_after = [&](auto state_at) {
        anim::DragonRig rig;
        rig.init(skeleton, joints);
        for (int i = 0; i < 300; ++i) rig.update(state_at(float(i) / 60.0f), 1.0f / 60.0f);
        return rig.world_matrices()[size_t(joints.leg[0].back())].col[3].xyz();
    };

    game::FlightState cruise;
    cruise.velocity = Vec3{0.0f, 0.0f, -30.0f};
    cruise.airspeed = 30.0f;
    // Well clear of the ground: near it the legs extend for landing and are
    // deliberately excluded from the pendulum.
    cruise.ground_clearance = 300.0f;
    const Vec3 neutral = foot_after([&](float) { return cruise; });

    // A COORDINATED turn must NOT swing the legs laterally: gravity plus
    // centrifugal force point through the body's floor -- that is what
    // coordinated means, and it is why passengers do not lean in a banked
    // aircraft. The legs press harder, they do not deflect.
    const Vec3 coordinated = foot_after([&](float t) {
        return game::studio_state(game::StudioScenario::TurnLeft, t, Vec3::zero(), -500.0f);
    });
    CHECK(std::fabs(coordinated.x - neutral.x) < 0.4f);

    // A flat skidding turn is what swings them: the body yaws and the velocity
    // curves, but there is no bank, so the centripetal force is fully lateral
    // in the body frame and the feet hang toward the outside of the turn.
    // The state has to be dynamically consistent -- the body yawing WITH its
    // velocity -- or the pseudo-force spins through every direction and the
    // measurement means nothing.
    const float skid_rate = 0.45f;
    const Vec3 skidding = foot_after([&](float t) {
        game::FlightState s = cruise;
        const float heading = skid_rate * t;
        s.orientation = Quat::from_axis_angle(Vec3::unit_y(), heading);
        s.velocity = rotate(s.orientation, Vec3::forward()) * 30.0f;
        s.angular_velocity = Vec3{0.0f, skid_rate, 0.0f};
        s.airspeed = 30.0f;
        return s;
    });
    // Turning left (nose swinging left), centrifugal slings the feet right: +X.
    CHECK(skidding.x > neutral.x + 0.05f);

    // Sustained deceleration: feet float forward (-Z), standing-in-a-bus
    // physics. Decelerating for the whole sample, so the pendulum is measured
    // deflected rather than after it has settled back.
    const Vec3 braking = foot_after([&](float t) {
        game::FlightState s = cruise;
        const float v = 62.0f - 6.0f * t;
        s.velocity = Vec3{0.0f, 0.0f, -v};
        s.airspeed = v;
        return s;
    });
    CHECK(braking.z < neutral.z - 0.02f);
}

}  // namespace

int main() {
    test_hierarchy();
    test_bind_pose_is_identity();
    test_rotation_moves_children();
    test_weight_normalization();
    test_blended_skinning_preserves_shape();
    test_dragon_rig_builds();
    test_rig_responds_to_flight();
    test_animation_track_sampling();
    test_animation_clip_loops();
    test_clip_rest_reproduces_bind();
    test_base_clip_layers_under_rig();
    test_head_aims_at_a_target();
    test_body_responds_to_control_input();
    test_wings_carry_the_load();
    test_idle_clip_fades_with_intensity();
    test_studio_states_are_consistent();
    test_legs_swing_with_the_frame();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
