// Skeleton, skinning and rig invariants.
//
// Skinning bugs are uniquely nasty to debug by eye: a wrong inverse bind or a
// transposed multiply produces a mesh that is *nearly* right, or one that
// explodes only when a particular joint rotates. These pin the properties that
// make the whole thing trustworthy.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>

#include "anim/animation.h"
#include "anim/dragon_rig.h"
#include "anim/gltf_loader.h"
#include "gfx/static_model.h"
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

float joint_palette_delta(const anim::DragonRig& a, const anim::DragonRig& b, int joint);

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

void test_rig_tuning_profile_round_trip() {
    std::printf("rig tuning profiles round-trip through flat config\n");
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "dragon-rig-tuning-test.cfg";

    anim::RigTuning written;
    written.wing_phase_delay = 0.035f;
    written.wing_downstroke_fraction = 0.42f;
    written.wing_elbow_fold_scale = 0.45f;
    written.wing_wrist_fold_scale = 1.25f;
    written.wing_finger_fold_scale = 1.75f;
    written.wing_flap_fold_deg = 17.0f;
    written.wing_flap_limit_deg = 41.0f;
    written.wing_recovery_fold_deg = 18.0f;
    written.wing_recovery_extend_phase = 0.68f;
    written.leg_brake_extend = 0.7f;
    written.leg_brake_forward_deg = 28.0f;
    written.chain_iterations = 9;
    written.ground_wing_arm_sweep_deg = 71.0f;
    written.ground_hip_deg = -8.0f;
    CHECK(anim::save_rig_tuning(written, path.string().c_str()));

    anim::RigTuning loaded;
    CHECK(anim::load_rig_tuning(loaded, path.string().c_str()));
    CHECK(near(loaded.wing_phase_delay, written.wing_phase_delay));
    CHECK(near(loaded.wing_downstroke_fraction, written.wing_downstroke_fraction));
    CHECK(near(loaded.wing_elbow_fold_scale, written.wing_elbow_fold_scale));
    CHECK(near(loaded.wing_wrist_fold_scale, written.wing_wrist_fold_scale));
    CHECK(near(loaded.wing_finger_fold_scale, written.wing_finger_fold_scale));
    CHECK(near(loaded.wing_flap_fold_deg, written.wing_flap_fold_deg));
    CHECK(near(loaded.wing_flap_limit_deg, written.wing_flap_limit_deg));
    CHECK(near(loaded.wing_recovery_fold_deg, written.wing_recovery_fold_deg));
    CHECK(near(loaded.wing_recovery_extend_phase, written.wing_recovery_extend_phase));
    CHECK(near(loaded.leg_brake_extend, written.leg_brake_extend));
    CHECK(near(loaded.leg_brake_forward_deg, written.leg_brake_forward_deg));
    CHECK(loaded.chain_iterations == written.chain_iterations);
    CHECK(near(loaded.ground_wing_arm_sweep_deg, written.ground_wing_arm_sweep_deg));
    CHECK(near(loaded.ground_hip_deg, written.ground_hip_deg));
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

void test_wing_fold_profile_is_anatomical() {
    std::printf("wing fold controls separate elbow, wrist and fingers\n");
    anim::DragonShape shape;
    Skeleton skeleton;
    anim::DragonJoints joints;
    anim::SkinnedMeshData mesh;
    anim::build_dragon(shape, skeleton, joints, mesh);

    game::FlightState tucked;
    tucked.velocity = Vec3{0.0f, 0.0f, -30.0f};
    tucked.airspeed = 30.0f;
    tucked.ground_clearance = 300.0f;
    tucked.wing_tuck = 1.0f;

    auto settled = [&](const anim::RigTuning& tuning) {
        auto rig = std::make_unique<anim::DragonRig>();
        rig->init(skeleton, joints);
        rig->tuning = tuning;
        for (int i = 0; i < 120; ++i) rig->update(tucked, 1.0f / 60.0f);
        return rig;
    };

    anim::DragonRig baseline;
    baseline.init(skeleton, joints);
    for (int i = 0; i < 120; ++i) baseline.update(tucked, 1.0f / 60.0f);

    anim::RigTuning elbow_open = baseline.tuning;
    elbow_open.wing_elbow_fold_scale = 0.0f;
    const auto elbow_open_rig = settled(elbow_open);
    const int elbow = joints.wing_root[0].back();
    const int finger = joints.wing_fingers[0].front().back();
    CHECK(joint_palette_delta(baseline, *elbow_open_rig, elbow) > 1e-3f);

    anim::RigTuning fingers_closed = baseline.tuning;
    fingers_closed.wing_finger_fold_scale = 1.8f;
    const auto fingers_closed_rig = settled(fingers_closed);
    // Closing the finger must not rotate its parent elbow. This is the
    // distinction the old cumulative profile could not express.
    CHECK(joint_palette_delta(baseline, *fingers_closed_rig, elbow) < 1e-4f);
    CHECK(joint_palette_delta(baseline, *fingers_closed_rig, finger) > 1e-3f);

    // A model may cap a visual stroke without changing the flight state's
    // engine angle or its force model.
    game::FlightState high_flap = tucked;
    high_flap.wing_tuck = 0.0f;
    high_flap.wing_angle = radians(80.0f);
    anim::DragonRig uncapped;
    uncapped.init(skeleton, joints);
    for (int i = 0; i < 60; ++i) uncapped.update(high_flap, 1.0f / 60.0f);
    anim::DragonRig capped;
    capped.init(skeleton, joints);
    capped.tuning.wing_flap_limit_deg = 40.0f;
    for (int i = 0; i < 60; ++i) capped.update(high_flap, 1.0f / 60.0f);
    CHECK(joint_palette_delta(uncapped, capped, joints.wing_root[0].front()) > 1e-3f);
}

void test_wingbeat_phase_articulation() {
    std::printf("wingbeat phase delay leads the elbow and lets the fingers recover\n");
    anim::DragonShape shape;
    Skeleton skeleton;
    anim::DragonJoints joints;
    anim::SkinnedMeshData mesh;
    anim::build_dragon(shape, skeleton, joints, mesh);

    const int shoulder = joints.wing_root[0].front();
    const int elbow = joints.wing_root[0].back();
    const int wrist = joints.wing_fingers[0].front().front();
    const int tip = joints.wing_fingers[0].back().back();

    auto beat_curve = [](float phase, float downstroke_fraction) {
        phase -= std::floor(phase);
        if (phase < 0.0f) phase += 1.0f;
        if (phase < downstroke_fraction) {
            return std::cos(core::PI * phase / downstroke_fraction);
        }
        return -std::cos(core::PI * (phase - downstroke_fraction) /
                         (1.0f - downstroke_fraction));
    };
    auto state_at = [&](float phase, float amplitude = 1.0f, float tuck = 0.0f) {
        game::FlightState state;
        state.velocity = Vec3{0.0f, 0.0f, -30.0f};
        state.airspeed = 30.0f;
        state.ground_clearance = 300.0f;
        state.flap_phase = phase;
        state.flap_amplitude = amplitude;
        state.wing_tuck = tuck;
        // Match the studio beat (4 degree centre, 36 degree half range) so a
        // delayed sample can be compared against the current sample directly.
        const float beating = 4.0f + 36.0f * beat_curve(phase, 0.40f);
        state.wing_angle = radians(9.0f + amplitude * (beating - 9.0f));
        return state;
    };
    auto posed = [&](float phase, const anim::RigTuning& tuning, float amplitude = 1.0f,
                     float tuck = 0.0f) {
        auto rig = std::make_unique<anim::DragonRig>();
        rig->init(skeleton, joints);
        rig->tuning = tuning;
        rig->update(state_at(phase, amplitude, tuck), 1.0f / 60.0f);
        return rig;
    };
    auto local_lift = [](const anim::DragonRig& rig, int joint) {
        return core::rotate(rig.pose().local[size_t(joint)].rotation, Vec3::unit_x()).y;
    };

    // The rest of the beat -- stroke plane, recovery hinges, feathering, body
    // bob -- is switched off so this test isolates the delay and the fan fold.
    // Those terms have their own test below.
    auto beat_only = []() {
        anim::RigTuning t;
        t.stroke_plane_tilt_deg = 0.0f;
        t.recovery_elbow_deg = 0.0f;
        t.recovery_wrist_deg = 0.0f;
        t.recovery_finger_deg = 0.0f;
        t.recovery_droop_deg = 0.0f;
        t.stroke_twist_deg = 0.0f;
        t.beat_heave_m = 0.0f;
        t.beat_pitch_deg = 0.0f;
        t.wing_recovery_extend_phase = 0.0f;
        return t;
    };
    anim::RigTuning delayed = beat_only();
    delayed.wing_phase_lag = 0.0f;
    delayed.outboard_decay = 1.0f;
    delayed.flap_shoulder_deg = 36.0f;
    delayed.wing_phase_delay = 0.06f;
    delayed.upstroke_fold_deg = 0.0f;
    delayed.wing_flap_fold_deg = 0.0f;

    // The local z rotations reach their low point later at each outboard
    // station. This measures temporal order directly rather than looking only
    // at a final amplitude or a static posed screenshot.
    float minimum_phase[3] = {0.0f, 0.0f, 0.0f};
    float minimum_lift[3] = {1e9f, 1e9f, 1e9f};
    const int measured_joints[3] = {shoulder, elbow, wrist};
    for (int sample = 0; sample < 200; ++sample) {
        const float phase = float(sample) / 200.0f;
        const auto rig = posed(phase, delayed);
        for (int station = 0; station < 3; ++station) {
            const float lift = local_lift(*rig, measured_joints[station]);
            if (lift < minimum_lift[station]) {
                minimum_lift[station] = lift;
                minimum_phase[station] = phase;
            }
        }
    }
    std::printf("  low points shoulder %.3f, elbow %.3f, wrist %.3f\n", minimum_phase[0],
                minimum_phase[1], minimum_phase[2]);
    CHECK(minimum_phase[1] > minimum_phase[0] + 0.025f);
    CHECK(minimum_phase[2] > minimum_phase[1] + 0.025f);

    // The recovery profile is a creature of the UPSTROKE: through the
    // downstroke it is byte-for-byte the plain pose path, whatever it is set
    // to. And it no longer needs the phase delay switched on to work -- that
    // gate was an accident, and it left five of the six generated species
    // with no recovery at all because only one profile had set a delay.
    anim::RigTuning plain = beat_only();
    plain.wing_phase_delay = 0.0f;
    const auto legacy = posed(0.31f, plain);
    anim::RigTuning with_recovery = plain;
    with_recovery.wing_recovery_fold_deg = 24.0f;
    with_recovery.wing_recovery_extend_phase = 0.70f;
    const auto disabled = posed(0.31f, with_recovery);
    CHECK(joint_palette_delta(*legacy, *disabled, shoulder) < 1e-5f);
    CHECK(joint_palette_delta(*legacy, *disabled, tip) < 1e-5f);
    CHECK(joint_palette_delta(*posed(0.58f, plain), *posed(0.58f, with_recovery), tip) > 1e-3f);

    // The timed recovery fold belongs to the outer wing, peaks after the fast
    // downstroke, and fades before phase wraps so the membrane can reopen.
    anim::RigTuning recovery = delayed;
    recovery.wing_recovery_fold_deg = 30.0f;
    recovery.wing_recovery_extend_phase = 0.68f;
    const auto no_recovery = posed(0.58f, delayed);
    anim::RigTuning disabled_recovery = recovery;
    disabled_recovery.wing_recovery_extend_phase = 0;
    const auto no_extension = posed(0.58f, disabled_recovery);
    CHECK(joint_palette_delta(*no_recovery, *no_extension, tip) < 1e-5f);
    const auto compact = posed(0.58f, recovery);
    const auto no_recovery_late = posed(0.999f, delayed);
    const auto extended = posed(0.999f, recovery);
    const float compact_delta = joint_palette_delta(*no_recovery, *compact, tip);
    const float elbow_compact_delta = joint_palette_delta(*no_recovery, *compact, elbow);
    const float extended_delta = joint_palette_delta(*no_recovery_late, *extended, tip);
    std::printf("  recovery fold deltas elbow %.4f, tip %.4f, late %.4f\n",
                elbow_compact_delta, compact_delta, extended_delta);
    CHECK(compact_delta > elbow_compact_delta + 1e-3f);
    CHECK(extended_delta < compact_delta * 0.35f);

    // Recovery strength is proportional to active flapping and disappears as
    // the wing is tucked, so a tiny residual amplitude cannot cause a full fold.
    const auto weak = posed(0.58f, recovery, 0.10f);
    const auto weak_no_recovery = posed(0.58f, delayed, 0.10f);
    const auto tucked = posed(0.58f, recovery, 1.0f, 1.0f);
    const auto tucked_no_recovery = posed(0.58f, delayed, 1.0f, 1.0f);
    CHECK(joint_palette_delta(*weak_no_recovery, *weak, tip) < compact_delta * 0.25f);
    CHECK(joint_palette_delta(*tucked_no_recovery, *tucked, tip) < compact_delta * 0.25f);

    // The cycle boundary is continuous, and the two wings keep their tips on
    // their own side of the spine throughout recovery.
    const auto before_wrap = posed(0.999f, recovery);
    const auto after_wrap = posed(0.001f, recovery);
    CHECK(joint_palette_delta(*before_wrap, *after_wrap, tip) < 0.05f);
    const Vec3 right_tip = before_wrap->world_matrices()[size_t(tip)].col[3].xyz();
    const int left_tip_index = joints.wing_fingers[1].back().back();
    const Vec3 left_tip = before_wrap->world_matrices()[size_t(left_tip_index)].col[3].xyz();
    CHECK(right_tip.x > 0.0f);
    CHECK(left_tip.x < 0.0f);
}

// The four things that separate a wingbeat from a wave, each measured on the
// posed skeleton rather than judged from a screenshot: the wing shortens on
// the upstroke, the tip sweeps forward on the downstroke and back on the
// recovery, the hand pitches leading-edge-down while it moves down and up
// while it moves up, and the body rises with the push. Every one is keyed to
// the beat's phase or velocity -- a position-keyed shape is the same going up
// as coming down, which is exactly what a wave is.
void test_wingbeat_is_not_a_wave() {
    std::printf("a wingbeat flexes, sweeps, feathers and lifts the body; a wave does none\n");
    anim::DragonShape shape;
    Skeleton skeleton;
    anim::DragonJoints joints;
    anim::SkinnedMeshData mesh;
    anim::build_dragon(shape, skeleton, joints, mesh);
    const int shoulder = joints.wing_root[0].front();
    const int tip = joints.wing_fingers[0].back().back();

    auto beat_curve = [](float phase, float down) {
        phase -= std::floor(phase);
        if (phase < down) return std::cos(core::PI * phase / down);
        return -std::cos(core::PI * (phase - down) / (1.0f - down));
    };
    auto state_at = [&](float phase, float amplitude) {
        game::FlightState state;
        state.velocity = Vec3{0.0f, 0.0f, -30.0f};
        state.airspeed = 30.0f;
        state.ground_clearance = 300.0f;
        state.flap_phase = phase;
        state.flap_amplitude = amplitude;
        const float beating = 4.0f + 46.0f * beat_curve(phase, 0.40f);
        state.wing_angle = radians(9.0f + amplitude * (beating - 9.0f));
        return state;
    };
    auto posed = [&](float phase, const anim::RigTuning& tuning, float tuck = 0.0f) {
        auto rig = std::make_unique<anim::DragonRig>();
        rig->init(skeleton, joints);
        rig->tuning = tuning;
        game::FlightState s = state_at(phase, 1.0f);
        s.wing_tuck = tuck;
        rig->update(s, 1.0f / 60.0f);
        return rig;
    };
    // Each cue is measured against a control with just that cue switched off,
    // at the same phase, so nothing else in the pose can hide or fake it.
    const anim::RigTuning beat;
    // The membrane carries on one bone-length past the last joint, and that is
    // what the player watches -- on the generated rig the last joint IS the
    // wrist, and measuring its origin would miss the whole hand.
    const int stem = joints.wing_root[0].back();
    const Vec3 tip_bind = skeleton.world_bind(tip).translation_part();
    const Vec3 stem_bind = skeleton.world_bind(stem).translation_part();
    const Vec3 membrane_local =
        transform_point(inverse(skeleton.world_bind(tip)), tip_bind + (tip_bind - stem_bind));
    auto membrane_from_shoulder = [&](const anim::DragonRig& rig) {
        const auto& w = rig.world_matrices();
        return transform_point(w[size_t(tip)], membrane_local) - w[size_t(shoulder)].col[3].xyz();
    };
    // The generated rig faces -Z, so forward is -Z here. Mid-downstroke is
    // where the wing moves fastest; the reversals are where the stroke plane
    // has carried the tip furthest fore and aft.
    const float mid_down = 0.20f, mid_up = 0.66f, bottom = 0.40f, top = 0.0f;

    // 1. Span: the hand is nearer the spine mid-upstroke than mid-downstroke.
    const float down_x = membrane_from_shoulder(*posed(mid_down, beat)).x;
    const float up_x = membrane_from_shoulder(*posed(mid_up, beat)).x;
    std::printf("  reach from shoulder: downstroke %.2f, upstroke %.2f\n", down_x, up_x);
    // The generated rig has no finger ribs, so only the elbow and wrist hinges
    // shorten it; the ribbed assets fold further. A few percent here is a
    // visible shortening there.
    CHECK(up_x < 0.97f * down_x);

    // 2. Stroke plane: with the tilt on, the hand ends the downstroke further
    //    FORWARD and the upstroke further AFT than the same pose without it.
    anim::RigTuning flat = beat;
    flat.stroke_plane_tilt_deg = 0.0f;
    const float bottom_z = membrane_from_shoulder(*posed(bottom, beat)).z;
    const float bottom_flat_z = membrane_from_shoulder(*posed(bottom, flat)).z;
    const float top_z = membrane_from_shoulder(*posed(top, beat)).z;
    const float top_flat_z = membrane_from_shoulder(*posed(top, flat)).z;
    std::printf("  hand z at the bottom %.2f (flat %.2f), at the top %.2f (flat %.2f)\n",
                bottom_z, bottom_flat_z, top_z, top_flat_z);
    CHECK(bottom_z < bottom_flat_z - 0.3f);
    CHECK(top_z > top_flat_z + 0.3f);

    // 3. Feathering: leading edge lower than the untwisted pose while the wing
    //    moves down, higher while it moves up.
    anim::RigTuning untwisted = beat;
    untwisted.stroke_twist_deg = 0.0f;
    auto leading_edge_y = [&](const anim::DragonRig& rig) {
        const Quat q = core::quat_from_matrix(rig.world_matrices()[size_t(tip)]);
        return core::rotate(q, Vec3::forward()).y;
    };
    const float down_edge = leading_edge_y(*posed(mid_down, beat));
    const float down_flat_edge = leading_edge_y(*posed(mid_down, untwisted));
    const float up_edge = leading_edge_y(*posed(mid_up, beat));
    const float up_flat_edge = leading_edge_y(*posed(mid_up, untwisted));
    std::printf("  leading edge y: downstroke %.3f (untwisted %.3f), upstroke %.3f (untwisted %.3f)\n",
                down_edge, down_flat_edge, up_edge, up_flat_edge);
    CHECK(down_edge < down_flat_edge - 0.05f);
    CHECK(up_edge > up_flat_edge + 0.05f);

    // 4. The body answers: higher just after the downstroke than just before
    //    it, and not at all with the heave switched off.
    anim::RigTuning still = beat;
    still.beat_heave_m = 0.0f;
    still.beat_pitch_deg = 0.0f;
    auto root_y = [&](const anim::DragonRig& rig) {
        return rig.world_matrices()[size_t(joints.root)].col[3].xyz().y;
    };
    const float rest_y = root_y(*posed(0.02f, still));
    const float before_y = root_y(*posed(0.02f, beat));
    const float after_y = root_y(*posed(0.52f, beat));
    std::printf("  root y: still %.3f, top of downstroke %.3f, after it %.3f\n", rest_y, before_y,
                after_y);
    CHECK(after_y > before_y + 0.05f);
    CHECK(std::fabs(root_y(*posed(0.52f, still)) - rest_y) < 1e-4f);

    // 5. And none of it survives a tuck: the beat gates all four, so a
    //    tucked dragon with the flap key held poses exactly as it did before.
    anim::RigTuning none = beat;
    none.stroke_plane_tilt_deg = 0.0f;
    none.recovery_elbow_deg = 0.0f;
    none.recovery_wrist_deg = 0.0f;
    none.recovery_finger_deg = 0.0f;
    none.recovery_droop_deg = 0.0f;
    none.stroke_twist_deg = 0.0f;
    none.beat_heave_m = 0.0f;
    none.beat_pitch_deg = 0.0f;
    none.wing_recovery_extend_phase = 0.0f;
    CHECK(joint_palette_delta(*posed(mid_up, beat, 1.0f), *posed(mid_up, none, 1.0f), tip) < 1e-4f);
    CHECK(std::fabs(root_y(*posed(0.52f, beat, 1.0f)) - root_y(*posed(0.52f, none, 1.0f))) < 1e-5f);
}

void test_brake_leg_profile_floats_forward() {
    std::printf("brake leg profile unfolds and floats limbs forward\n");
    anim::DragonShape shape;
    Skeleton skeleton;
    anim::DragonJoints joints;
    anim::SkinnedMeshData mesh;
    anim::build_dragon(shape, skeleton, joints, mesh);

    game::FlightState braking;
    braking.velocity = Vec3{0.0f, 0.0f, -30.0f};
    braking.airspeed = 30.0f;
    braking.ground_clearance = 300.0f;
    braking.wing_brake = 1.0f;

    auto leg_tip = [&](const anim::RigTuning& tuning) {
        anim::DragonRig rig;
        rig.init(skeleton, joints);
        rig.tuning = tuning;
        for (int i = 0; i < 180; ++i) rig.update(braking, 1.0f / 60.0f);
        return rig.world_matrices()[size_t(joints.leg[0].back())].col[3].xyz();
    };

    const Vec3 default_tip = leg_tip(anim::RigTuning{});
    anim::RigTuning profiled;
    profiled.leg_tuck_deg = 42.0f;
    profiled.leg_trail_deg = 14.0f;
    profiled.front_leg_trail_deg = 10.0f;
    profiled.leg_brake_extend = 0.65f;
    profiled.leg_brake_forward_deg = 30.0f;
    const Vec3 profiled_tip = leg_tip(profiled);
    // Engine forward is -Z for the generated rig. The profile's positive
    // forward float must move the tip toward that direction and clear the
    // deeply aft default.
    CHECK(profiled_tip.z < default_tip.z - 0.1f);
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
void test_open_sculpt_jaw_calibration() {
    std::printf("open sculpt jaw closes at rest and never exceeds authored gape\n");
    Skeleton skeleton;
    anim::DragonJoints joints;
    anim::SkinnedMeshData mesh;
    anim::build_dragon(anim::DragonShape{}, skeleton, joints, mesh);
    anim::DragonRig rig;
    rig.init(skeleton, joints);
    rig.tuning.jaw_rest_deg = -16.0f;
    rig.tuning.jaw_open_deg = 16.0f;
    game::FlightState state;
    state.velocity = Vec3{0, 0, -35};
    for (int i = 0; i < 90; ++i) rig.update(state, 1.0f / 60.0f);
    const Quat closed = rig.pose().local[size_t(joints.jaw)].rotation;
    const Quat bind = skeleton.joint(joints.jaw).local_bind.rotation;
    auto angle = [](Quat a, Quat b) {
        return degrees(2 * std::acos(clampf(std::fabs(dot(a, b)), 0, 1)));
    };
    CHECK(std::fabs(angle(closed, bind) - 16.0f) < 0.1f);
    anim::RigAction action;
    action.breath = 1;
    rig.set_action(action);
    float maximum_opening = 0;
    for (int i = 0; i < 180; ++i) {
        rig.update(state, 1.0f / 60.0f);
        const Quat current = rig.pose().local[size_t(joints.jaw)].rotation;
        maximum_opening = std::max(maximum_opening, angle(current, closed));
    }
    CHECK(maximum_opening <= 16.1f);
    CHECK(angle(rig.pose().local[size_t(joints.jaw)].rotation, bind) < 1.1f);
    rig.set_action(anim::RigAction{});
    for (int i = 0; i < 180; ++i) rig.update(state, 1.0f / 60.0f);
    CHECK(angle(rig.pose().local[size_t(joints.jaw)].rotation, closed) < 0.1f);
}

// Every roster creature's jaw must open DOWNWARD on the breath, measured on
// the asset itself, because the opening sign is probed from the bind pose and
// a probe that reads the wrong descendant inverts silently: the mouth then
// clamps shut on the attack and the fire leaves a closed face. Also prints
// the resting gape, since most of these sculpts are authored mouth-open and
// need a jaw_rest_deg in their profile to close at idle.
void test_roster_jaws_open_downward() {
    std::printf("every roster jaw opens downward on the breath\n");
    namespace fs = std::filesystem;
    for (const char* name : {"assets/embercrest.glb", "assets/rimefang.glb",
                             "assets/blightmaw.glb", "assets/ironroot.glb",
                             "assets/stormsail.glb", "assets/tidewrack.glb"}) {
        const fs::path source_root = fs::path(__FILE__).parent_path().parent_path();
        fs::path path = source_root / name;
        if (!fs::is_regular_file(path)) path = fs::path(name);
        if (!fs::is_regular_file(path)) {
            std::printf("  %s absent; skipped\n", name);
            continue;
        }
        Skeleton skeleton;
        anim::SkinnedMeshData mesh;
        if (!anim::load_skinned_gltf(path.string().c_str(), skeleton, mesh).ok) continue;
        const anim::DragonJoints joints = anim::map_dragon_joints(skeleton);
        if (joints.jaw == anim::NO_PARENT || joints.head == anim::NO_PARENT) {
            std::printf("  %s: no jaw mapped\n", name);
            continue;
        }
        anim::DragonRig rig;
        rig.init(skeleton, joints);
        anim::load_rig_tuning(rig.tuning, (path.string() + ".rig.cfg").c_str());
        int tip = joints.jaw;
        for (int i = 0; i < skeleton.count(); ++i) {
            if (skeleton.joint(i).parent == joints.jaw) tip = i;
        }
        game::FlightState state;
        state.velocity = Vec3{0.0f, 0.0f, -30.0f};
        state.airspeed = 30.0f;
        state.ground_clearance = 300.0f;
        // Tip offset from the jaw pivot, in the HEAD's frame, so a raised or
        // lowered neck cannot masquerade as a jaw movement.
        auto tip_in_head = [&](float breath) {
            anim::RigAction action;
            action.breath = breath;
            for (int i = 0; i < 240; ++i) {
                rig.set_action(action);
                rig.update(state, 1.0f / 60.0f);
            }
            const auto& w = rig.world_matrices();
            const Quat head = core::quat_from_matrix(w[size_t(joints.head)]);
            return core::rotate(core::conjugate(head),
                                w[size_t(tip)].col[3].xyz() - w[size_t(joints.jaw)].col[3].xyz());
        };
        const Vec3 bind_tip = core::rotate(
            core::conjugate(core::quat_from_matrix(skeleton.world_bind(joints.head))),
            skeleton.world_bind(tip).translation_part() -
                skeleton.world_bind(joints.jaw).translation_part());
        const Vec3 rest = tip_in_head(0.0f);
        const Vec3 open = tip_in_head(1.0f);
        // "Down" in the head's frame is whatever direction the bind tip is
        // displaced from a line through the head's forward axis; simpler and
        // robust: measure the angle each pose makes with the bind tip about
        // the head's X (the hinge), signed so that opening is positive.
        auto hinge_angle = [&](const Vec3& v) {
            return degrees(std::atan2(v.y, -v.z) - std::atan2(bind_tip.y, -bind_tip.z));
        };
        const float rest_deg = hinge_angle(rest), open_deg = hinge_angle(open);
        std::printf("  %s: jaw '%s' tip '%s' -- rest %+.1f deg from bind, breath %+.1f deg "
                    "(negative = lower)\n",
                    name, skeleton.joint(joints.jaw).name.c_str(),
                    skeleton.joint(tip).name.c_str(), rest_deg, open_deg);
        CHECK(open_deg < rest_deg - 5.0f);
    }
}

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

// The wingtip must actually reach the angle the flight model commanded.
//
// drive_wings normalizes the per-joint contributions so that "the wingtip ends
// up rotated by the flap angle itself, whatever the chain length" -- that is
// what lets one procedural rig drive a two-bone generated wing and a
// seven-joint imported one. The promise is worth a test because it is silently
// breakable: any per-joint attenuation left out of the normalizer scales the
// whole stroke down, and the loss grows with chain length, so the generated rig
// barely notices while a real asset loses a third of its wingbeat. That reads
// as a stiff, shallow flap rather than as a bug.
// How far the wingtip actually travels, as a fraction of the angle commanded.
//
// Flap is a rotation about the body's forward axis, so it moves the tip along
// body up and nothing else does -- sweep and fold rotate about body up itself
// and leave that component alone. Height over the bind-pose span is therefore
// the stroke angle cleanly, where measuring elevation above the horizon would
// be contaminated by the fold shortening the span.
float measured_stroke_fraction(const Skeleton& skeleton, const anim::DragonJoints& joints,
                               const anim::RigTuning& tuning) {
    // The outermost joint of the longest chain on one wing: the tip whose
    // travel the player reads as the size of the wingbeat.
    std::vector<int> tip_chain = joints.wing_root[0];
    size_t longest = 0;
    for (const std::vector<int>& finger : joints.wing_fingers[0]) {
        if (finger.size() > longest && !finger.empty()) {
            longest = finger.size();
            tip_chain = finger;
        }
    }
    const int tip = tip_chain.back();
    const int shoulder = joints.wing_root[0].front();

    // The reference point is one bone-length beyond the outermost joint, not
    // the joint itself: the membrane carries on past the last rib, that is what
    // the player watches, and it is what drive_wings normalizes its leverage
    // to. Carried as a point in the tip joint's own space so the posed position
    // comes straight out of that joint's world matrix, scale and all.
    const int stem = tip_chain.size() >= 2 ? tip_chain[tip_chain.size() - 2] : shoulder;
    const Vec3 tip_bind = skeleton.world_bind(tip).translation_part();
    const Vec3 stem_bind = skeleton.world_bind(stem).translation_part();
    const Vec3 membrane_bind = tip_bind + (tip_bind - stem_bind);
    const Vec3 membrane_local =
        transform_point(inverse(skeleton.world_bind(tip)), membrane_bind);
    const float span = length(membrane_bind -
                              skeleton.world_bind(shoulder).translation_part());
    if (span < 0.01f) return 0.0f;

    auto elevation_deg = [&](float wing_angle_deg) {
        anim::DragonRig rig;
        rig.init(skeleton, joints);
        rig.tuning = tuning;
        rig.tuning.upstroke_fold_deg = 0.0f;
        game::FlightState s;
        s.velocity = Vec3{0.0f, 0.0f, -30.0f};
        s.airspeed = 30.0f;
        s.ground_clearance = 300.0f;
        s.wing_angle = radians(wing_angle_deg);
        s.flap_amplitude = 1.0f;
        for (int i = 0; i < 180; ++i) rig.update(s, 1.0f / 60.0f);
        const Vec3 d = transform_point(rig.world_matrices()[size_t(tip)], membrane_local) -
                       rig.world_matrices()[size_t(shoulder)].col[3].xyz();
        return degrees(std::asin(clampf(d.y / span, -1.0f, 1.0f)));
    };

    // The flight model's own stroke, top of the upbeat to bottom of the
    // downbeat: FlightTuning::flap_up_angle_deg and flap_down_angle_deg. A
    // model profile may cap the visual stroke below that (wing_flap_limit_deg)
    // for a rig whose membranes cross at the full angle, so measure against the
    // stroke this model is actually allowed to fly.
    const game::FlightTuning flight;
    float up = flight.flap_up_angle_deg, down = flight.flap_down_angle_deg;
    if (tuning.wing_flap_limit_deg > 0.0f) {
        up = std::min(up, tuning.wing_flap_limit_deg);
        down = std::max(down, -tuning.wing_flap_limit_deg);
    }
    return (elevation_deg(up) - elevation_deg(down)) / (up - down);
}

void test_wingtip_reaches_the_commanded_flap() {
    std::printf("the wingtip sweeps the angle the flight model commanded\n");
    anim::DragonShape shape;
    Skeleton skeleton;
    anim::DragonJoints joints;
    anim::SkinnedMeshData mesh;
    anim::build_dragon(shape, skeleton, joints, mesh);

    const float fraction = measured_stroke_fraction(skeleton, joints, anim::RigTuning{});
    std::printf("  generated rig: tip travels %.0f%% of the commanded stroke\n",
                double(100.0f * fraction));
    // Distributing the stroke down the chain curls the wing, so the tip follows
    // a chord rather than the full arc and some loss is real animation. What is
    // not acceptable is loss that grows with how many bones a rig happens to
    // have -- that made a long imported wing flap half as far as a short
    // generated one through no decision of anyone\'s. This floor is what the
    // leverage normalizer in drive_wings buys; before it the generated rig sat
    // at 64% and a seven-joint wing at barely half.
    CHECK(fraction > 0.80f);

    // Whatever an imported asset is shaped like, its visible stroke must land
    // in the same band. Chain length is the variable this pins.
    namespace fs = std::filesystem;
    for (const char* name : {"assets/stormsail.glb", "assets/embercrest.glb"}) {
        const fs::path source_root = fs::path(__FILE__).parent_path().parent_path();
        fs::path path = source_root / name;
        if (!fs::is_regular_file(path)) path = fs::path(name);
        if (!fs::is_regular_file(path)) {
            std::printf("  %s absent; skipped\n", name);
            continue;
        }
        Skeleton asset_skeleton;
        anim::SkinnedMeshData asset_mesh;
        const anim::GltfLoadResult loaded =
            anim::load_skinned_gltf(path.string().c_str(), asset_skeleton, asset_mesh);
        if (!loaded.ok) {
            std::printf("  %s failed to load; skipped\n", name);
            continue;
        }
        const anim::DragonJoints asset_joints = anim::map_dragon_joints(asset_skeleton);
        if (asset_joints.wing_root[0].empty()) continue;
        anim::RigTuning asset_tuning;
        anim::load_rig_tuning(asset_tuning, (path.string() + ".rig.cfg").c_str());
        const float asset_fraction =
            measured_stroke_fraction(asset_skeleton, asset_joints, asset_tuning);
        std::printf("  %s: tip travels %.0f%% of the commanded stroke\n", name,
                    double(100.0f * asset_fraction));
        CHECK(asset_fraction > 0.80f);
    }
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

// The aimed fold is a promise about DIRECTIONS: whatever the sculpt's
// membrane plane or the rigger's bone axes, each wing segment ends up pointing
// where the profile says, in the body's frame. That is the whole reason it
// exists -- the angle stow's rotations turned about axes the sculpt chose,
// and the same numbers folded one wing into a hoop and stood another up as a
// sail. Checked on the generated rig, which faces -Z; the direction
// convention is sweep from straight out toward aft, elevation from horizontal.
void test_aimed_fold_points_the_bones() {
    std::printf("the aimed standing fold points each wing segment where the profile says\n");
    anim::DragonShape shape;
    Skeleton skeleton;
    anim::DragonJoints joints;
    anim::SkinnedMeshData mesh;
    anim::build_dragon(shape, skeleton, joints, mesh);
    CHECK(joints.wing_root[0].size() >= 2);

    anim::DragonRig rig;
    rig.init(skeleton, joints);
    rig.tuning.ground_wing_aim = 1.0f;
    rig.tuning.ground_wing_arm_sweep_deg = 70.0f;
    rig.tuning.ground_wing_arm_elev_deg = 25.0f;
    rig.tuning.ground_wing_forearm_sweep_deg = -60.0f;
    rig.tuning.ground_wing_forearm_elev_deg = 50.0f;

    game::FlightState grounded;
    grounded.grounded = true;
    grounded.ground_clearance = 0.0f;
    grounded.wing_tuck = 1.0f;
    for (int i = 0; i < 300; ++i) rig.update(grounded, 1.0f / 60.0f);

    auto bone_direction = [&](int a, int b) {
        const auto& w = rig.world_matrices();
        return normalize(w[size_t(b)].col[3].xyz() - w[size_t(a)].col[3].xyz());
    };
    // The generated rig's head is at -Z, so aft is +Z; side 0 is +X.
    auto expected = [](float sweep_deg, float elev_deg, float out) {
        const float sweep = radians(sweep_deg), elev = radians(elev_deg);
        return Vec3{std::cos(elev) * std::cos(sweep) * out, std::sin(elev),
                    std::cos(elev) * std::sin(sweep)};
    };
    for (int side = 0; side < 2; ++side) {
        const std::vector<int>& root = joints.wing_root[side];
        const float out = skeleton.world_bind(root.back()).translation_part().x >= 0.0f ? 1.0f
                                                                                          : -1.0f;
        const Vec3 arm = bone_direction(root[0], root[1]);
        CHECK(dot(arm, expected(70.0f, 25.0f, out)) > 0.98f);
        if (root.size() >= 3) {
            const Vec3 forearm = bone_direction(root[1], root[2]);
            CHECK(dot(forearm, expected(-60.0f, 50.0f, out)) > 0.98f);
        } else if (!joints.wing_fingers[side].empty()) {
            // A two-bone arm: the forearm runs from the elbow to the finger base.
            const Vec3 forearm = bone_direction(root[1], joints.wing_fingers[side][0][0]);
            CHECK(dot(forearm, expected(-60.0f, 50.0f, out)) > 0.98f);
        }
    }

    // Airborne it is exactly the rig without the aim: the fold belongs to the
    // ground alone.
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
    for (const int joint : joints.wing_root[0]) {
        const Quat a = rig.pose().local[size_t(joint)].rotation;
        const Quat b = bare.pose().local[size_t(joint)].rotation;
        CHECK(std::fabs(dot(a, b)) > 0.9999f);
    }
}

// The stance re-poses a sculpt whose bind pose is not standing, and the one
// thing it must never do is leave a foot in the air: the root is lifted so
// the lowest foot returns to the floor the bind soles stood on, which is the
// height flight.cfg's ground_offset was measured from. Checked on Rimefang,
// whose profile carries the first stance, when the asset is present.
void test_stance_keeps_the_feet_on_the_floor() {
    std::printf("the standing stance keeps every foot on the bind floor\n");
    namespace fs = std::filesystem;
    const fs::path source_root = fs::path(__FILE__).parent_path().parent_path();
    int with_stance = 0;
    for (const char* name : {"assets/rimefang.glb", "assets/blightmaw.glb", "assets/ironroot.glb",
                             "assets/stormsail.glb", "assets/tidewrack.glb"}) {
        fs::path path = source_root / name;
        if (!fs::is_regular_file(path)) path = fs::path(name);
        if (!fs::is_regular_file(path)) {
            std::printf("  %s absent; skipped\n", name);
            continue;
        }
        Skeleton skeleton;
        anim::SkinnedMeshData mesh;
        if (!anim::load_skinned_gltf(path.string().c_str(), skeleton, mesh).ok) continue;
        const anim::DragonJoints joints = anim::map_dragon_joints(skeleton);
        CHECK(!joints.foot_roots.empty());
        anim::DragonRig rig;
        rig.init(skeleton, joints);
        anim::load_rig_tuning(rig.tuning, (path.string() + ".rig.cfg").c_str());
        if (rig.tuning.ground_stance <= 0.5f) {
            std::printf("  %s carries no stance; skipped\n", name);
            continue;
        }
        ++with_stance;
        rig.set_model_scale(15.6f);

        // What the creature stands on: each foot root and every toe under it.
        // A stance that rotates a foot can put a toe below its root, and the
        // rig keeps the LOWEST of these on the floor, so the test measures the
        // same set.
        std::vector<int> standing;
        for (int i = 0; i < skeleton.count(); ++i) {
            for (int p = i; p != anim::NO_PARENT; p = skeleton.joint(p).parent) {
                if (std::find(joints.foot_roots.begin(), joints.foot_roots.end(), p) !=
                    joints.foot_roots.end()) {
                    standing.push_back(i);
                    break;
                }
            }
        }
        float bind_floor = 1e9f;
        for (const int joint : standing) {
            bind_floor = std::min(bind_floor, skeleton.world_bind(joint).translation_part().y);
        }
        // A wyvern stands on its wing wrists too; they join the posed floor,
        // not the bind floor, since the bind pose spreads the wings.
        std::vector<int> posed_standing = standing;
        if (rig.tuning.ground_wing_plant > 0.5f) {
            for (int side = 0; side < 2; ++side) {
                if (joints.wing_root[side].size() >= 2) {
                    posed_standing.push_back(joints.wing_root[side].back());
                }
            }
        }
        game::FlightState grounded;
        grounded.grounded = true;
        grounded.ground_clearance = 0.0f;
        grounded.wing_tuck = 1.0f;
        for (int i = 0; i < 300; ++i) rig.update(grounded, 1.0f / 60.0f);

        // The stance did something: the spine is no longer at its bind pitch.
        const Vec3 bind_spine =
            normalize(skeleton.world_bind(joints.chest).translation_part() -
                      skeleton.world_bind(joints.root).translation_part());
        const auto& w = rig.world_matrices();
        const Vec3 spine = normalize(w[size_t(joints.chest)].col[3].xyz() -
                                     w[size_t(joints.root)].col[3].xyz());
        CHECK(std::fabs(degrees(std::acos(clampf(dot(bind_spine, spine), -1.0f, 1.0f)))) >
              5.0f);

        // Every foot on one floor, and that floor is the bind floor.
        float floor = 1e9f;
        for (const int joint : posed_standing) floor = std::min(floor, w[size_t(joint)].col[3].y);
        // ...plus whatever extra lift the profile asks for (a wyvern's hand
        // hangs below its planted wrist joint), in model units.
        const float expected_floor = bind_floor + rig.tuning.ground_lift_m / 15.6f;
        if (std::fabs(floor - expected_floor) >= 0.01f) {
            std::printf("  %s: floor moved %.3f units\n", name, double(floor - expected_floor));
        }
        CHECK(std::fabs(floor - expected_floor) < 0.01f);
        for (const auto& [foot_name, height] : rig.foot_heights()) {
            if (!(height >= 0.0f && height < 0.15f)) {
                std::printf("  %s: %s is %.2f m off the floor\n", name, foot_name.c_str(),
                            double(height));
            }
            CHECK(height >= 0.0f && height < 0.15f);
        }
    }
    // The profiles under test carry the stance; a rename or a dropped block
    // would otherwise pass by skipping everything.
    if (fs::is_regular_file(source_root / "assets/rimefang.glb")) CHECK(with_stance >= 1);
}

// The rig is re-initialised when the player cycles onto another model. A
// clip is sampled by joint index, so one authored for the previous skeleton
// must not survive into the next: with it, a landed wyvern stood in the
// default asset's idle pose and its jaw turned about a stranger's hinge.
// On a hillside the flight model puts the body on the surface under ONE
// point, so the downhill feet floated and the uphill feet sank. With a ground
// query the rig plants each standing limb on the terrain under it: the body
// lifts and tilts onto the mean contact, and a two-bone solve closes each
// limb's residual. Checked on Rimefang over a ground that slopes across the
// body, so the left and right feet want different heights.
void test_limbs_plant_on_the_terrain() {
    std::printf("each standing limb plants on the terrain under it\n");
    namespace fs = std::filesystem;
    const fs::path source_root = fs::path(__FILE__).parent_path().parent_path();
    fs::path path = source_root / "assets/rimefang.glb";
    if (!fs::is_regular_file(path)) path = fs::path("assets/rimefang.glb");
    if (!fs::is_regular_file(path)) {
        std::printf("  assets/rimefang.glb absent; skipped\n");
        return;
    }
    Skeleton skeleton;
    anim::SkinnedMeshData mesh;
    if (!anim::load_skinned_gltf(path.string().c_str(), skeleton, mesh).ok) return;
    const anim::DragonJoints joints = anim::map_dragon_joints(skeleton);
    CHECK(joints.foot_roots.size() == 4);

    // The bind floor: the lowest foot root or toe.
    float bind_floor = 1e9f;
    for (int i = 0; i < skeleton.count(); ++i) {
        for (int p = i; p != anim::NO_PARENT; p = skeleton.joint(p).parent) {
            if (std::find(joints.foot_roots.begin(), joints.foot_roots.end(), p) !=
                joints.foot_roots.end()) {
                bind_floor = std::min(bind_floor, skeleton.world_bind(i).translation_part().y);
                break;
            }
        }
    }
    const float scale = 15.6f;
    // Ground rising 0.3 m per metre to the +X side: the right feet want to be
    // about 0.5 m higher than the left ones.
    auto ground = [](float x, float) { return 0.3f * x; };

    auto run = [&](float ik_weight, float lift_m) {
        anim::DragonRig rig;
        rig.init(skeleton, joints);
        anim::load_rig_tuning(rig.tuning, (path.string() + ".rig.cfg").c_str());
        rig.tuning.ground_ik = ik_weight;
        rig.tuning.ground_lift_m = lift_m;
        rig.set_model_scale(scale);
        // The body where the flight model would put it over flat ground at
        // the origin: bind floor plus the profile's lift on y = 0.
        const core::Mat4 model_to_world = core::Mat4::trs(
            Vec3{0.0f, -bind_floor * scale + lift_m, 0.0f}, Quat::identity(), Vec3(scale));
        game::FlightState grounded;
        grounded.grounded = true;
        grounded.ground_clearance = 0.0f;
        grounded.wing_tuck = 1.0f;
        float worst = 0.0f;
        for (int i = 0; i < 400; ++i) {
            rig.set_ground(model_to_world, ground);
            rig.update(grounded, 1.0f / 60.0f);
        }
        const auto& w = rig.world_matrices();
        for (const int foot : joints.foot_roots) {
            const Vec3 p = core::transform_point(model_to_world, w[size_t(foot)].col[3].xyz());
            const float above_floor =
                (skeleton.world_bind(foot).translation_part().y - bind_floor) * scale;
            const float wanted = ground(p.x, p.z) + above_floor + lift_m;
            worst = std::max(worst, std::fabs(p.y - wanted));
        }
        return worst;
    };
    const float without = run(0.0f, 0.15f);
    const float with = run(1.0f, 0.15f);
    std::printf("  worst foot off its ground: %.2f m without planting, %.2f m with\n",
                double(without), double(with));
    CHECK(without > 0.15f);  // the slope is real: a level body leaves feet off the ground
    CHECK(with < 0.06f);
}

void test_reinit_drops_the_previous_clip() {
    std::printf("re-initialising the rig on another skeleton drops the previous clip\n");
    anim::DragonShape shape;
    Skeleton skeleton;
    anim::DragonJoints joints;
    anim::SkinnedMeshData mesh;
    anim::build_dragon(shape, skeleton, joints, mesh);
    anim::AnimationClip clip;
    clip.name = "idle";
    clip.duration = 1.0f;
    anim::RotationTrack track;
    track.joint = joints.tail[1];
    track.times = {0.0f, 1.0f};
    track.rotations = {Quat::from_axis_angle(Vec3::unit_x(), radians(30.0f)),
                       Quat::from_axis_angle(Vec3::unit_x(), radians(30.0f))};
    clip.tracks.push_back(track);

    anim::DragonRig rig;
    rig.init(skeleton, joints);
    rig.set_base_clip(&clip, 0.0f);
    CHECK(rig.has_base_clip());
    rig.init(skeleton, joints);
    CHECK(!rig.has_base_clip());
}

void test_optional_embercrest_asset() {
    std::printf("optional Embercrest asset validates when present\n");

    namespace fs = std::filesystem;
    fs::path asset_path;
    const char* override_path = std::getenv("EMBERCREST_TEST_MODEL");
    if (override_path && *override_path) {
        asset_path = fs::path(override_path);
        std::printf("  using EMBERCREST_TEST_MODEL=%s\n", asset_path.string().c_str());
        // An explicit model is a request to validate that exact file. Do not
        // turn a typo or an unavailable build artifact into an optional skip.
        CHECK(fs::is_regular_file(asset_path));
        if (!fs::is_regular_file(asset_path)) return;
    } else {
        const fs::path source_root = fs::path(__FILE__).parent_path().parent_path();
        const fs::path candidates[] = {
            fs::path("assets/embercrest-scripted.glb"),
            fs::path("../assets/embercrest-scripted.glb"),
            source_root / "assets/embercrest-scripted.glb",
        };
        for (const fs::path& candidate : candidates) {
            if (fs::is_regular_file(candidate)) {
                asset_path = candidate;
                break;
            }
        }
        if (asset_path.empty()) {
            std::printf("  assets/embercrest-scripted.glb absent; optional validation skipped\n");
            return;
        }
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

    // A delivered model may carry a model-scoped rig profile beside the asset.
    // Apply it here as the runtime does, so the optional pose checks exercise
    // the shipped tuning rather than the generic defaults.
    const std::string asset_name = asset_path.filename().string();
    // The shipped hero is assets/embercrest.glb; the script-built experiment
    // (embercrest-scripted.glb) ships no rig profile and is not held to this.
    const bool is_textured_embercrest = asset_name == "embercrest.glb";
    const fs::path rig_profile_path = fs::path(asset_path.string() + ".rig.cfg");
    anim::RigTuning model_tuning;
    const bool has_model_tuning =
        fs::is_regular_file(rig_profile_path) &&
        anim::load_rig_tuning(model_tuning, rig_profile_path.string().c_str());
    if (is_textured_embercrest) {
        CHECK(has_model_tuning);
    }

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
        if (has_model_tuning) rig->tuning = model_tuning;
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

    // The textured asset's jaw is a deforming parent with a separate endpoint.
    // Probe the furthest direct child so this remains independent of the
    // endpoint's exact exported name. A palette-wide attack delta can pass
    // while the mouth still closes upward, which is the glitch this catches.
    int jaw_tip = joints.jaw;
    float jaw_reach = 0.0f;
    const Vec3 jaw_bind = skeleton.world_bind(joints.jaw).translation_part();
    for (int i = 0; i < skeleton.count(); ++i) {
        if (skeleton.joint(i).parent != joints.jaw) continue;
        const float reach = distance(skeleton.world_bind(i).translation_part(), jaw_bind);
        if (reach > jaw_reach) {
            jaw_reach = reach;
            jaw_tip = i;
        }
    }
    const float glide_jaw_drop =
        glide_rig->world_matrices()[size_t(joints.head)].col[3].y -
        glide_rig->world_matrices()[size_t(jaw_tip)].col[3].y;
    const float attack_jaw_drop =
        attack_rig->world_matrices()[size_t(joints.head)].col[3].y -
        attack_rig->world_matrices()[size_t(jaw_tip)].col[3].y;
    // The rebuilt textured deliverable explicitly owns a mandible endpoint.
    // The neutral Embercrest asset predates that joint and remains a valid
    // legacy model, so only require the endpoint/direction regression for the
    // named textured asset.
    if (is_textured_embercrest) {
        CHECK(jaw_tip != joints.jaw);
        CHECK(attack_jaw_drop > glide_jaw_drop + 0.01f);
    }
}

// The static loader reads a prop as plain world-space meshes: the tracked
// watchtower is one mesh node, 40 m tall, base at y 0, with normals.
void test_static_gltf_loads_a_prop() {
    std::printf("the static glTF loader reads a tracked prop\n");
    namespace fs = std::filesystem;
    const fs::path path = fs::path(__FILE__).parent_path().parent_path() / "assets/props/watchtower.glb";
    std::vector<gfx::StaticMesh> meshes;
    std::string error;
    CHECK(gfx::load_static_gltf(path.string().c_str(), meshes, &error));
    CHECK(meshes.size() == 1);
    if (meshes.empty()) return;
    const gfx::StaticMesh& m = meshes[0];
    CHECK(!m.data.indices.empty() && m.data.indices.size() % 3 == 0);
    CHECK(std::fabs(m.bounds_max.y - 40.0f) < 1.0f && std::fabs(m.bounds_min.y) < 0.1f);
    bool unit_normals = true;
    for (const auto& v : m.data.vertices) unit_normals &= std::fabs(core::length(v.normal) - 1.0f) < 0.02f;
    CHECK(unit_normals);
    // A missing file is an error, not a crash.
    CHECK(!gfx::load_static_gltf("/nonexistent.glb", meshes, &error));
}

}  // namespace

// The melee gestures move the whole animal, and each direction is a sign
// convention that was wrong at least once in this file's history. Pinned on
// the generated rig, which faces -Z with +X to its right: a claw to the right
// rolls the right wing down and swings the tail left; a tail whip to the
// right sends the tail tip right; a bite drops the nose and surges forward.
void test_melee_gestures_move_the_body() {
    std::printf("melee gestures: the body answers the swing in the right directions\n");
    anim::DragonShape shape;
    Skeleton skeleton;
    anim::DragonJoints joints;
    anim::SkinnedMeshData mesh;
    anim::build_dragon(shape, skeleton, joints, mesh);

    game::FlightState glide;
    glide.velocity = Vec3{0.0f, 0.0f, -30.0f};
    glide.airspeed = 30.0f;
    glide.ground_clearance = 300.0f;

    // Settle, swing, and sample at the strike's peak.
    auto sample = [&](anim::RigAction action, float at) {
        anim::DragonRig rig;
        rig.init(skeleton, joints);
        for (int i = 0; i < 120; ++i) rig.update(glide, 1.0f / 60.0f);
        struct Sample {
            Vec3 root, right_tip, left_tip, tail_tip, right_foot, head;
        };
        auto grab = [&]() {
            const auto& w = rig.world_matrices();
            Sample out;
            out.root = w[size_t(joints.root)].col[3].xyz();
            out.right_tip = w[size_t(joints.wing_fingers[0].front().back())].col[3].xyz();
            out.left_tip = w[size_t(joints.wing_fingers[1].front().back())].col[3].xyz();
            out.tail_tip = w[size_t(joints.tail.back())].col[3].xyz();
            out.right_foot = w[size_t(joints.leg[0].back())].col[3].xyz();
            out.head = w[size_t(joints.head)].col[3].xyz();
            return out;
        };
        const Sample before = grab();
        rig.set_action(action);
        const int frames = int(at * 60.0f + 0.5f);
        for (int i = 0; i < frames; ++i) {
            rig.update(glide, 1.0f / 60.0f);
            action = anim::RigAction{};
        }
        return std::pair<Sample, Sample>(before, grab());
    };
    const anim::RigTuning t;
    const float a = t.gesture_anticipation;

    // Claw, right side, at the strike peak.
    {
        anim::RigAction claw;
        claw.claw = true;
        claw.side = 1.0f;
        const float peak = t.claw_duration * (a + (1.0f - a) * 0.3f);
        const auto [before, at] = sample(claw, peak);
        // Roll into the strike: the right wingtip lower, the left higher.
        CHECK(at.right_tip.y < before.right_tip.y - 0.05f);
        CHECK(at.left_tip.y > before.left_tip.y + 0.05f);
        // The right foot forward (-Z) and out (+X); the tail tip swung LEFT.
        CHECK(at.right_foot.z < before.right_foot.z - 0.1f);
        CHECK(at.right_foot.x > before.right_foot.x + 0.05f);
        CHECK(at.tail_tip.x < before.tail_tip.x - 0.1f);
    }
    // Tail whip, right side, at the strike peak: the tip goes RIGHT, the head
    // swings left.
    {
        anim::RigAction whip;
        whip.tail = true;
        whip.side = 1.0f;
        const float peak = t.tail_duration * (a + (1.0f - a) * 0.3f);
        const auto [before, at] = sample(whip, peak);
        std::printf("  tail tip x %.2f -> %.2f, head x %.2f -> %.2f\n", before.tail_tip.x,
                    at.tail_tip.x, before.head.x, at.head.x);
        CHECK(at.tail_tip.x > before.tail_tip.x + 0.1f);
        CHECK(at.head.x < before.head.x - 0.02f);
    }
    // Bite at the strike peak: the head lower and further forward, the root
    // surged forward (-Z).
    {
        anim::RigAction bite;
        bite.bite = true;
        const float peak = t.bite_duration * (a + (1.0f - a) * 0.3f);
        const auto [before, at] = sample(bite, peak);
        std::printf("  head %.2f,%.2f -> %.2f,%.2f  root z %.2f -> %.2f\n", before.head.y,
                    before.head.z, at.head.y, at.head.z, before.root.z, at.root.z);
        CHECK(at.head.y < before.head.y - 0.05f);
        CHECK(at.head.z < before.head.z - 0.05f);
        CHECK(at.root.z < before.root.z - 0.05f);
    }
}

// Hold the loaded pose until launch, recover without residual root motion,
// and never carry a pending attack onto a newly selected skeleton. Imported
// models exercise the opposite facing convention and their actual root scale.
void test_melee_load_recovery_and_switch() {
    namespace fs = std::filesystem;
    const fs::path root = fs::path(__FILE__).parent_path().parent_path();
    for (const char* name : {"dragon", "embercrest", "rimefang", "frostvein", "blightmaw",
                             "ironroot", "stormsail", "tidewrack", "alt/prowler"}) {
        const fs::path path = root / (std::string("assets/") + name + ".glb");
        if (!fs::exists(path)) continue;
        Skeleton skeleton;
        anim::SkinnedMeshData mesh;
        CHECK(anim::load_skinned_gltf(path.string().c_str(), skeleton, mesh).ok);
        const auto joints = anim::map_dragon_joints(skeleton);
        CHECK(joints.root != anim::NO_PARENT);
        if (joints.root == anim::NO_PARENT) continue;
        float lo = 1e9f, hi = -1e9f;
        for (const auto& v : mesh.vertices) { lo = std::min(lo, v.position.x); hi = std::max(hi, v.position.x); }
        const float scale = 19.0f / (hi - lo);
        game::FlightState state;
        state.velocity = Vec3{0,0,-30}; state.airspeed = 30; state.ground_clearance = 300;
        for (int kind = 0; kind < 3; ++kind) for (float side : {-1.0f, 1.0f}) {
            anim::DragonRig rig;
            rig.init(skeleton, joints); rig.set_model_scale(scale);
            anim::load_rig_tuning(rig.tuning, (path.string()+".rig.cfg").c_str());
            for (int i=0; i<120; ++i) rig.update(state, 1.0f/120.0f);
            const auto rest = rig.world_matrices()[size_t(joints.root)];
            const float duration = kind == 0 ? rig.tuning.bite_duration :
                                   kind == 1 ? rig.tuning.claw_duration : rig.tuning.tail_duration;
            anim::RigAction action;
            action.bite = kind == 0; action.claw = kind == 1; action.tail = kind == 2; action.side = side;
            rig.set_action(action); rig.update(state, 1.0f/120.0f);
            // At the very end of anticipation the old bump was already back
            // near neutral. A loaded body must still be distinctly rotated.
            const int load_frames = int(duration * rig.tuning.gesture_anticipation * 120.0f);
            for (int i=0; i<load_frames; ++i) rig.update(state, 1.0f/120.0f);
            const auto loaded = rig.world_matrices()[size_t(joints.root)];
            const auto relative = core::quat_from_matrix(loaded) * core::conjugate(core::quat_from_matrix(rest));
            CHECK(std::fabs(relative.w) < 0.9995f);
            // Switching cancels the loaded action and its not-yet-fired kick.
            rig.init(skeleton, joints);
            rig.update(state, 1.0f/120.0f);
            const auto reset = rig.world_matrices()[size_t(joints.root)];
            CHECK(core::distance(reset.col[3].xyz(), rest.col[3].xyz()) * scale < 1e-4f);
            CHECK(std::fabs(core::dot(reset.col[0].xyz(), rest.col[0].xyz()) /
                             (core::length(reset.col[0].xyz()) * core::length(rest.col[0].xyz())) - 1.0f) < 1e-4f);
            rig.set_action(action); rig.update(state, 1.0f/120.0f);
            for (int i=0; i<int((duration + 0.1f)*120.0f); ++i) rig.update(state, 1.0f/120.0f);
            const auto recovered = rig.world_matrices()[size_t(joints.root)];
            CHECK(core::distance(recovered.col[3].xyz(), rest.col[3].xyz()) * scale < 1e-4f);
            CHECK(core::length(recovered.col[0].xyz() - rest.col[0].xyz()) < 1e-4f);
        }
    }
}

int main() {
    test_static_gltf_loads_a_prop();
    test_melee_load_recovery_and_switch();
    test_melee_gestures_move_the_body();
    test_hierarchy();
    test_bind_pose_is_identity();
    test_rotation_moves_children();
    test_weight_normalization();
    test_blended_skinning_preserves_shape();
    test_dragon_rig_builds();
    test_rig_tuning_profile_round_trip();
    test_wing_fold_profile_is_anatomical();
    test_wingbeat_phase_articulation();
    test_brake_leg_profile_floats_forward();
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
    test_open_sculpt_jaw_calibration();
    test_roster_jaws_open_downward();
    test_speed_posture();
    test_ground_stance_is_authored();
    test_studio_states_are_consistent();
    test_studio_ground_offset();
    test_wingtip_reaches_the_commanded_flap();
    test_wingbeat_is_not_a_wave();
    test_legs_swing_with_the_frame();
    test_aimed_fold_points_the_bones();
    test_stance_keeps_the_feet_on_the_floor();
    test_reinit_drops_the_previous_clip();
    test_limbs_plant_on_the_terrain();
    test_optional_embercrest_asset();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
