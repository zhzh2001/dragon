// Skeleton, skinning and rig invariants.
//
// Skinning bugs are uniquely nasty to debug by eye: a wrong inverse bind or a
// transposed multiply produces a mesh that is *nearly* right, or one that
// explodes only when a particular joint rotates. These pin the properties that
// make the whole thing trustworthy.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>

#include "anim/animation.h"
#include "anim/dragon_rig.h"
#include "anim/gltf_loader.h"
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

    // The tail must whip when a turn begins. At a steady spin rate with no
    // airflow the correct steady state is a radial tail with NO lateral offset
    // (Coriolis needs motion, centrifugal is radial), so the thing to assert is
    // the transient: the Euler-force kick at onset throws the tip sideways.
    game::FlightState turning;
    turning.angular_velocity = Vec3{0.0f, 1.0f, 0.0f};  // yawing
    const int tail_tip = joints.tail.back();
    const Vec3 tail_bind = skeleton.world_bind(tail_tip).translation_part();
    float peak_whip = 0.0f;
    for (int i = 0; i < 120; ++i) {
        rig.update(turning, 1.0f / 60.0f);
        const Vec3 posed =
            transform_point(rig.skinning_matrices()[size_t(tail_tip)], tail_bind);
        peak_whip = maxf(peak_whip, std::fabs(posed.x - tail_bind.x));
    }
    std::printf("  tail tip whips %.2f m at the onset of a 1 rad/s yaw\n", peak_whip);
    CHECK(peak_whip > 0.25f);

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
    limited.tuning.neck_aim_share = 0.0f;  // the neck's share is tested separately
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
    calm.ground_clearance = 300.0f;

    game::FlightState violent = calm;
    violent.velocity = Vec3{0.0f, 0.0f, -100.0f};
    violent.airspeed = 100.0f;
    violent.wing_tuck = 1.0f;
    violent.g_load = 2.5f;

    const float calm_deflection = head_deflection(calm);
    const float violent_deflection = head_deflection(violent);
    // The idle is a GROUND clip: airborne only a trace survives even in a calm
    // glide, and violence removes that too.
    CHECK(calm_deflection > 2.0f);
    CHECK(calm_deflection < 15.0f);
    CHECK(violent_deflection < calm_deflection * 0.5f);

    // On the ground the idle is exactly right, so nothing fades.
    game::FlightState grounded;
    grounded.grounded = true;
    CHECK(head_deflection(grounded) > 30.0f);
}

// The neck carries part of the aim: an animal looking well off its body turns
// the whole neck and finishes with the head. The tell is the neck's base-to-head
// line, which a head-only aim would never move.
void test_neck_shares_the_aim() {
    std::printf("the neck carries a share of the aim\n");
    anim::DragonShape shape;
    Skeleton skeleton;
    anim::DragonJoints joints;
    anim::SkinnedMeshData mesh;
    anim::build_dragon(shape, skeleton, joints, mesh);

    game::FlightState state;
    state.velocity = Vec3{0.0f, 0.0f, -40.0f};

    auto neck_line = [&](anim::DragonRig& r) {
        const Vec3 base = r.world_matrices()[size_t(joints.neck.front())].col[3].xyz();
        const Vec3 head = r.world_matrices()[size_t(joints.head)].col[3].xyz();
        return normalize(head - base);
    };

    anim::DragonRig head_only;
    head_only.init(skeleton, joints);
    head_only.tuning.neck_aim_share = 0.0f;
    anim::DragonRig shared;
    shared.init(skeleton, joints);
    shared.tuning.neck_aim_share = 0.6f;

    // Target 50 degrees to the left of the nose (toward -X, forward is -Z).
    const Vec3 target = state.position + Vec3{-153.0f, 0.0f, -129.0f};
    for (int i = 0; i < 180; ++i) {
        head_only.set_aim_target(target);
        head_only.update(state, 1.0f / 60.0f);
        shared.set_aim_target(target);
        shared.update(state, 1.0f / 60.0f);
    }
    // The shared neck's line swings toward the target; the head-only neck's
    // line barely moves (the chain sim has no reason to).
    CHECK(neck_line(shared).x < -0.15f);
    CHECK(neck_line(shared).x < neck_line(head_only).x - 0.1f);

    // Let go and it comes back.
    for (int i = 0; i < 240; ++i) shared.update(state, 1.0f / 60.0f);
    CHECK(std::fabs(neck_line(shared).x) < 0.08f);
}

// The mouth is what says the creature is doing it: the jaw opens on the
// breath, gapes and shuts on a spit, and the spit rears the neck back.
void test_attack_posture() {
    std::printf("jaw opens on breath and spit, spit recoils the neck\n");
    anim::DragonShape shape;
    Skeleton skeleton;
    anim::DragonJoints joints;
    anim::SkinnedMeshData mesh;
    anim::build_dragon(shape, skeleton, joints, mesh);
    CHECK(joints.jaw != anim::NO_PARENT);

    game::FlightState state;
    state.velocity = Vec3{0.0f, 0.0f, -35.0f};

    anim::DragonRig rig;
    rig.init(skeleton, joints);
    // Jaw tip in model space: the deepest joint under the jaw.
    int tip = joints.jaw;
    for (int i = 0; i < skeleton.count(); ++i) {
        if (skeleton.joint(i).parent == joints.jaw) tip = i;
    }
    // The jaw's opening, as the tip's drop below the head -- relative, so the
    // neck's own motion (the breath thrusts it) does not leak into the
    // measurement.
    auto tip_drop = [&](anim::DragonRig& r) {
        return r.world_matrices()[size_t(joints.head)].col[3].y -
               r.world_matrices()[size_t(tip)].col[3].y;
    };
    // And the jaw joint's own rotation away from bind, in degrees.
    auto jaw_angle = [&](anim::DragonRig& r) {
        const Quat posed = r.pose().local[size_t(joints.jaw)].rotation;
        const Quat bind = skeleton.joint(joints.jaw).local_bind.rotation;
        return degrees(2.0f * std::acos(clampf(std::fabs(dot(posed, bind)), -1.0f, 1.0f)));
    };
    auto head_y = [&](anim::DragonRig& r) {
        return r.world_matrices()[size_t(joints.head)].col[3].y;
    };

    for (int i = 0; i < 60; ++i) rig.update(state, 1.0f / 60.0f);
    const float closed_drop = tip_drop(rig);
    const float head_rest = head_y(rig);
    CHECK(rig.jaw_open() < 0.01f);
    CHECK(jaw_angle(rig) < 0.5f);

    // Breath: the jaw opens (tip drops) by the tuned angle and stays open.
    anim::RigAction breathe;
    breathe.breath = 1.0f;
    rig.set_action(breathe);
    for (int i = 0; i < 60; ++i) rig.update(state, 1.0f / 60.0f);
    CHECK(rig.jaw_open() > 0.9f);
    CHECK(tip_drop(rig) > closed_drop + 0.1f);
    CHECK(std::fabs(jaw_angle(rig) - rig.tuning.jaw_open_deg) < 3.0f);

    // Release: it shuts again.
    rig.set_action(anim::RigAction{});
    for (int i = 0; i < 90; ++i) rig.update(state, 1.0f / 60.0f);
    CHECK(rig.jaw_open() < 0.05f);
    CHECK(jaw_angle(rig) < 1.5f);

    // Spit: one edge. The jaw gapes then shuts within the gesture, and the
    // neck rears back (head rises) before settling.
    anim::RigAction spit;
    spit.fire = true;
    rig.set_action(spit);
    float peak_open = 0.0f;
    float peak_head = head_rest;
    for (int i = 0; i < 12; ++i) {
        rig.update(state, 1.0f / 60.0f);
        peak_open = std::max(peak_open, rig.jaw_open());
        peak_head = std::max(peak_head, head_y(rig));
    }
    CHECK(peak_open > 0.8f);
    // The edge is consumed: a second update with the same stored action must
    // not fire again.
    CHECK(!rig.action().fire);
    for (int i = 0; i < 20; ++i) {
        rig.update(state, 1.0f / 60.0f);
        peak_head = std::max(peak_head, head_y(rig));
    }
    CHECK(peak_head > head_rest + 0.03f);
    (void)closed_drop;
    CHECK(rig.jaw_open() < 0.05f);
    for (int i = 0; i < 120; ++i) rig.update(state, 1.0f / 60.0f);
    CHECK(std::fabs(head_y(rig) - head_rest) < 0.05f);
}

// Speed shapes the wing on its own: past cruise the tips sweep aft even with
// no tuck held -- the same way the tuck sweeps them -- and the membrane
// flutters. Cruise stays dead calm.
void test_speed_posture() {
    std::printf("wings sweep with speed and the tips flutter in a dive\n");
    anim::DragonShape shape;
    Skeleton skeleton;
    anim::DragonJoints joints;
    anim::SkinnedMeshData mesh;
    anim::build_dragon(shape, skeleton, joints, mesh);

    // A point out on the membrane beyond the last finger joint, so the joint's
    // own rotation (flutter, twist) moves it -- a joint's origin is only moved
    // by its ancestors.
    const int wingtip = joints.wing_fingers[0].back().back();
    const Vec3 membrane = skeleton.world_bind(wingtip).translation_part() + Vec3{2.0f, 0.0f, 0.0f};
    auto tip_of = [&](anim::DragonRig& r) {
        return transform_point(r.skinning_matrices()[size_t(wingtip)], membrane);
    };
    auto fly = [&](anim::DragonRig& r, float speed, float tuck, int frames) {
        game::FlightState s;
        s.velocity = Vec3{0.0f, 0.0f, -speed};
        s.airspeed = speed;
        s.wing_angle = radians(9.0f);
        s.wing_tuck = tuck;
        for (int i = 0; i < frames; ++i) r.update(s, 1.0f / 60.0f);
    };

    anim::DragonRig cruise;
    cruise.init(skeleton, joints);
    fly(cruise, 30.0f, 0.0f, 60);
    const Vec3 cruise_tip = tip_of(cruise);
    fly(cruise, 30.0f, 0.0f, 1);
    // Calm at cruise: consecutive frames agree.
    CHECK(near(tip_of(cruise), cruise_tip, 1e-3f));

    anim::DragonRig tucked;
    tucked.init(skeleton, joints);
    fly(tucked, 30.0f, 1.0f, 120);
    const Vec3 tucked_tip = tip_of(tucked);
    // The tuck sweeps AFT: the generated rig faces -Z, so aft is +Z.
    CHECK(tucked_tip.z > cruise_tip.z + 1.0f);

    anim::DragonRig dive;
    dive.init(skeleton, joints);
    dive.tuning.flutter_deg = 0.0f;  // steady, for the sweep measurement
    fly(dive, 95.0f, 0.0f, 120);
    const Vec3 dive_tip = tip_of(dive);
    // Speed alone sweeps the same way as the tuck, part of the way.
    const float tuck_sweep = tucked_tip.z - cruise_tip.z;
    const float speed_sweep = dive_tip.z - cruise_tip.z;
    CHECK(speed_sweep > 0.15f * tuck_sweep);
    CHECK(speed_sweep < 0.9f * tuck_sweep);

    // And alive: with the flutter on, the membrane moves between frames at
    // speed.
    anim::DragonRig alive;
    alive.init(skeleton, joints);
    fly(alive, 95.0f, 0.0f, 120);
    float travel = 0.0f;
    Vec3 previous = tip_of(alive);
    for (int i = 0; i < 30; ++i) {
        fly(alive, 95.0f, 0.0f, 1);
        travel += length(tip_of(alive) - previous);
        previous = tip_of(alive);
    }
    CHECK(travel > 0.05f);

    // With the speed posture dialled to zero, speed changes nothing.
    anim::DragonRig still;
    still.init(skeleton, joints);
    still.tuning.speed_sweep_deg = 0.0f;
    still.tuning.speed_fold_deg = 0.0f;
    still.tuning.flutter_deg = 0.0f;
    fly(still, 95.0f, 0.0f, 120);
    CHECK(near(tip_of(still), cruise_tip, 0.05f));
}

// On the ground the authored stance wins the joints the rig owns in flight --
// wings included on a rig with no forelegs (a wyvern stands on its wings; the
// generated greybox has hind legs only, so it counts as one). And a held clip
// is a pose, not a loop.
void test_ground_stance_is_authored() {
    std::printf("the authored stance takes the body on the ground\n");
    anim::DragonShape shape;
    Skeleton skeleton;
    anim::DragonJoints joints;
    anim::SkinnedMeshData mesh;
    anim::build_dragon(shape, skeleton, joints, mesh);
    CHECK(joints.front_leg[0].empty());  // no forelegs: the wings are the stance

    // A clip that curls the tail's second joint and lifts a wing root: both are
    // joints the rig rebuilds in flight.
    const int tail_joint = joints.tail[1];
    const int wing_joint = joints.wing_root[0].front();
    anim::AnimationClip clip;
    clip.name = "stance";
    clip.duration = 1.0f;
    for (const int joint : {tail_joint, wing_joint}) {
        anim::RotationTrack track;
        track.joint = joint;
        track.times = {0.0f, 0.5f, 1.0f};
        track.rotations = {Quat::from_axis_angle(Vec3::unit_x(), radians(40.0f)),
                           Quat::from_axis_angle(Vec3::unit_x(), radians(10.0f)),
                           Quat::from_axis_angle(Vec3::unit_x(), radians(40.0f))};
        clip.tracks.push_back(track);
    }
    auto angle_from_bind = [&](anim::DragonRig& r, int joint) {
        const Quat posed = r.pose().local[size_t(joint)].rotation;
        const Quat bind = skeleton.joint(joint).local_bind.rotation;
        return degrees(2.0f * std::acos(clampf(std::fabs(dot(posed, bind)), -1.0f, 1.0f)));
    };

    // Held at t = 0.5: the tail joint sits at 10 degrees, and stays there.
    anim::DragonRig rig;
    rig.init(skeleton, joints);
    rig.set_base_clip(&clip, 0.5f);

    game::FlightState grounded;
    grounded.grounded = true;
    grounded.ground_clearance = 0.0f;
    grounded.wing_tuck = 1.0f;
    for (int i = 0; i < 240; ++i) rig.update(grounded, 1.0f / 60.0f);
    CHECK(std::fabs(angle_from_bind(rig, tail_joint) - 10.0f) < 1.0f);
    const float held = angle_from_bind(rig, tail_joint);
    for (int i = 0; i < 30; ++i) rig.update(grounded, 1.0f / 60.0f);
    CHECK(std::fabs(angle_from_bind(rig, tail_joint) - held) < 0.1f);
    // With no forelegs the wing is the stance too: the clip's 10 degrees, not
    // the rig's tuck.
    CHECK(std::fabs(angle_from_bind(rig, wing_joint) - 10.0f) < 1.0f);

    // Airborne, the tail is the chain's again: it matches a rig that never had
    // the clip, flown the same way.
    anim::DragonRig bare;
    bare.init(skeleton, joints);
    game::FlightState glide;
    glide.velocity = Vec3{0.0f, 0.0f, -30.0f};
    glide.airspeed = 30.0f;
    glide.ground_clearance = 300.0f;
    for (int i = 0; i < 300; ++i) {
        rig.update(glide, 1.0f / 60.0f);
        bare.update(glide, 1.0f / 60.0f);
    }
    CHECK(std::fabs(angle_from_bind(rig, tail_joint) - angle_from_bind(bare, tail_joint)) < 1.0f);
    CHECK(std::fabs(angle_from_bind(rig, wing_joint) - angle_from_bind(bare, wing_joint)) < 1.0f);
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

void test_studio_ground_offset() {
    std::printf("studio grounded pose uses the supplied ground offset\n");
    const Vec3 centre{12.0f, 200.0f, -7.0f};
    const float ground_y = 37.5f;

    // Keep the API's historical default for callers that do not have a model
    // tuning file, while allowing an asset-specific body height in the app.
    const game::FlightState legacy =
        game::studio_state(game::StudioScenario::Grounded, 0.0f, centre, ground_y);
    const game::FlightState embercrest =
        game::studio_state(game::StudioScenario::Grounded, 0.0f, centre, ground_y, 3.15f);
    CHECK(near(legacy.position.y, ground_y + 2.5f));
    CHECK(near(embercrest.position.y, ground_y + 3.15f));
    CHECK(near(embercrest.position.x, centre.x));
    CHECK(near(embercrest.position.z, centre.z));
    CHECK(near(embercrest.ground_clearance, 0.0f));
    CHECK(embercrest.grounded);
}

void test_legs_swing_with_the_frame() {
    std::printf("legs are pendulums: outward in a turn, forward under braking\n");
    anim::DragonShape shape;
    Skeleton skeleton;
    anim::DragonJoints joints;
    anim::SkinnedMeshData mesh;
    anim::build_dragon(shape, skeleton, joints, mesh);

    // Trail zeroed: the pendulum's response directions are only unambiguous
    // when the limb hangs, and the trail posture differs between rigs. The
    // trail itself is checked separately, by displacement.
    auto foot_after = [&](auto state_at) {
        anim::DragonRig rig;
        rig.init(skeleton, joints);
        rig.tuning.leg_trail_deg = 0.0f;
        rig.tuning.front_leg_trail_deg = 0.0f;
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

    // The trail itself: airborne legs sit well away from where they stand.
    auto foot_with_trail = [&](float trail_deg) {
        anim::DragonRig rig;
        rig.init(skeleton, joints);
        rig.tuning.leg_trail_deg = trail_deg;
        for (int i = 0; i < 300; ++i) rig.update(cruise, 1.0f / 60.0f);
        return rig.world_matrices()[size_t(joints.leg[0].back())].col[3].xyz();
    };
    CHECK(length(foot_with_trail(38.0f) - foot_with_trail(0.0f)) > 0.3f);
}

bool finite_palette(const anim::DragonRig& rig) {
    for (const Mat4& matrix : rig.skinning_matrices()) {
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                if (!std::isfinite(matrix.col[c][r])) return false;
            }
        }
    }
    return true;
}

float palette_delta(const anim::DragonRig& a, const anim::DragonRig& b) {
    const size_t count = std::min(a.skinning_matrices().size(), b.skinning_matrices().size());
    float delta = 0.0f;
    for (size_t j = 0; j < count; ++j) {
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                delta = std::max(delta, std::fabs(a.skinning_matrices()[j].col[c][r] -
                                                  b.skinning_matrices()[j].col[c][r]));
            }
        }
    }
    return delta;
}

float joint_palette_delta(const anim::DragonRig& a, const anim::DragonRig& b, int joint) {
    if (joint < 0 || size_t(joint) >= a.skinning_matrices().size() ||
        size_t(joint) >= b.skinning_matrices().size()) {
        return 0.0f;
    }
    const Mat4& lhs = a.skinning_matrices()[size_t(joint)];
    const Mat4& rhs = b.skinning_matrices()[size_t(joint)];
    float delta = 0.0f;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            delta = std::max(delta, std::fabs(lhs.col[c][r] - rhs.col[c][r]));
        }
    }
    return delta;
}

void test_optional_embercrest_asset() {
    std::printf("optional Embercrest asset validates when present\n");

    namespace fs = std::filesystem;
    const fs::path source_root = fs::path(__FILE__).parent_path().parent_path();
    const fs::path candidates[] = {
        fs::path("assets/embercrest.glb"),
        fs::path("../assets/embercrest.glb"),
        source_root / "assets/embercrest.glb",
    };
    fs::path asset_path;
    for (const fs::path& candidate : candidates) {
        if (fs::is_regular_file(candidate)) {
            asset_path = candidate;
            break;
        }
    }
    if (asset_path.empty()) {
        std::printf("  assets/embercrest.glb absent; optional validation skipped\n");
        return;
    }

    Skeleton skeleton;
    anim::SkinnedMeshData mesh;
    const anim::GltfLoadResult loaded =
        anim::load_skinned_gltf(asset_path.string().c_str(), skeleton, mesh);
    CHECK(loaded.ok);
    if (!loaded.ok) return;
    std::printf("  loaded %d joints, %zu verts, %zu tris, bind error %.6f\n",
                loaded.joint_count, loaded.vertex_count, loaded.triangle_count,
                loaded.bind_pose_error);
    CHECK(loaded.bind_pose_error <= 0.01f);
    CHECK(loaded.joint_count == skeleton.count());
    CHECK(!mesh.vertices.empty());
    CHECK(!mesh.indices.empty());

    const anim::DragonJoints joints = anim::map_dragon_joints(skeleton);
    CHECK(joints.root != anim::NO_PARENT);
    CHECK(joints.chest != anim::NO_PARENT);
    CHECK(joints.head != anim::NO_PARENT);
    CHECK(joints.jaw != anim::NO_PARENT);
    CHECK(!joints.neck.empty());
    CHECK(!joints.tail.empty());
    CHECK(!joints.wing_root[0].empty());
    CHECK(!joints.wing_root[1].empty());
    CHECK(!joints.wing_fingers[0].empty());
    CHECK(!joints.wing_fingers[1].empty());
    // Embercrest is a four-legged dragon: the two hind-leg and two foreleg
    // chains are all expected, but their exact lengths depend on the authored
    // rig and are intentionally not fixed here.
    CHECK(!joints.leg[0].empty());
    CHECK(!joints.leg[1].empty());
    CHECK(!joints.front_leg[0].empty());
    CHECK(!joints.front_leg[1].empty());

    const Vec3 extent = loaded.bounds_max - loaded.bounds_min;
    const float model_scale = extent.x > 0.1f ? 19.0f / extent.x : 1.0f;

    game::FlightState glide;
    glide.velocity = Vec3{0.0f, 0.0f, -30.0f};
    glide.airspeed = 30.0f;
    glide.ground_clearance = 300.0f;
    glide.g_load = 1.0f;
    glide.wing_angle = radians(8.0f);

    game::FlightState flap = glide;
    flap.wing_angle = radians(44.0f);
    flap.flap_amplitude = 1.0f;
    flap.flap_phase = 0.25f;

    game::FlightState tuck = glide;
    tuck.velocity = Vec3{0.0f, 0.0f, -95.0f};
    tuck.airspeed = 95.0f;
    tuck.wing_tuck = 1.0f;

    game::FlightState yawing = glide;
    yawing.control = Vec3{0.0f, 1.0f, 0.0f};

    game::FlightState ground;
    ground.grounded = true;
    ground.ground_clearance = 0.0f;
    ground.wing_tuck = 1.0f;

    auto posed = [&](const game::FlightState& state, const anim::RigAction& action =
                                             anim::RigAction{}) {
        auto rig = std::make_unique<anim::DragonRig>();
        rig->init(skeleton, joints);
        rig->set_model_scale(model_scale);
        rig->set_action(action);
        for (int frame = 0; frame < 120; ++frame) rig->update(state, 1.0f / 60.0f);
        return rig;
    };

    const auto glide_rig = posed(glide);
    const auto flap_rig = posed(flap);
    const auto tuck_rig = posed(tuck);
    const auto yawing_rig = posed(yawing);
    const auto ground_rig = posed(ground);
    anim::RigAction attack;
    attack.breath = 1.0f;
    attack.fire = true;
    const auto attack_rig = posed(glide, attack);

    CHECK(finite_palette(*glide_rig));
    CHECK(finite_palette(*flap_rig));
    CHECK(finite_palette(*tuck_rig));
    CHECK(finite_palette(*yawing_rig));
    CHECK(finite_palette(*attack_rig));
    CHECK(finite_palette(*ground_rig));

    // Compare posed palettes rather than assuming the authored bone axes. This
    // catches a mapper that found names but failed to drive the imported chains.
    const int wing_tip = joints.wing_fingers[0].front().back();
    const int tail_tip = joints.tail.back();
    CHECK(!glide_rig->skinning_matrices().empty());
    const float wing_flap_delta = joint_palette_delta(*glide_rig, *flap_rig, wing_tip);
    const float wing_tuck_delta = joint_palette_delta(*glide_rig, *tuck_rig, wing_tip);
    const float tail_yaw_delta = joint_palette_delta(*glide_rig, *yawing_rig, tail_tip);
    const float attack_delta = palette_delta(*glide_rig, *attack_rig);
    const float ground_delta = palette_delta(*glide_rig, *ground_rig);
    std::printf("  response deltas wing flap %.5f, wing tuck %.5f, tail yaw %.5f, "
                "attack %.5f, ground %.5f, jaw %.3f\n",
                wing_flap_delta, wing_tuck_delta, tail_yaw_delta, attack_delta,
                ground_delta, attack_rig->jaw_open());
    CHECK(wing_flap_delta > 1e-3f);
    CHECK(wing_tuck_delta > 1e-3f);
    CHECK(tail_yaw_delta > 1e-3f);
    CHECK(attack_delta > 1e-3f);
    CHECK(ground_delta > 1e-3f);
    CHECK(wing_tip >= 0 && wing_tip < skeleton.count());
    CHECK(tail_tip >= 0 && tail_tip < skeleton.count());
    CHECK(attack_rig->jaw_open() > 0.5f);
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
    test_neck_shares_the_aim();
    test_attack_posture();
    test_speed_posture();
    test_ground_stance_is_authored();
    test_studio_states_are_consistent();
    test_studio_ground_offset();
    test_legs_swing_with_the_frame();
    test_optional_embercrest_asset();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
