// Skeleton, skinning and rig invariants.
//
// Skinning bugs are uniquely nasty to debug by eye: a wrong inverse bind or a
// transposed multiply produces a mesh that is *nearly* right, or one that
// explodes only when a particular joint rotates. These pin the properties that
// make the whole thing trustworthy.
#include <cmath>
#include <cstdio>

#include "anim/dragon_rig.h"
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

}  // namespace

int main() {
    test_hierarchy();
    test_bind_pose_is_identity();
    test_rotation_moves_children();
    test_weight_normalization();
    test_blended_skinning_preserves_shape();
    test_dragon_rig_builds();
    test_rig_responds_to_flight();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
