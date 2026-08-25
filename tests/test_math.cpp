// Math invariants. These exist because a sign error in a quaternion or a
// projection matrix produces a plausible-looking image that is subtly wrong,
// which is far more expensive to debug later than to catch here.
#include <cmath>
#include <cstdio>

#include "core/math.h"

using namespace core;

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

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
bool near(Vec3 a, Vec3 b, float eps = 1e-4f) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

#define CHECK(cond) check((cond), #cond, __LINE__)

void test_vec3() {
    std::printf("vec3\n");
    CHECK(near(dot(Vec3::unit_x(), Vec3::unit_y()), 0.0f));
    CHECK(near(length(Vec3{3, 4, 0}), 5.0f));
    CHECK(near(cross(Vec3::unit_x(), Vec3::unit_y()), Vec3::unit_z()));
    CHECK(near(length(normalize(Vec3{1, 2, 3})), 1.0f));

    // Degenerate normalize must not produce NaN -- gameplay code hits this.
    Vec3 zero_norm = normalize(Vec3::zero());
    CHECK(!std::isnan(zero_norm.x) && !std::isnan(zero_norm.y) && !std::isnan(zero_norm.z));
    CHECK(near(normalize_or(Vec3::zero(), Vec3::unit_y()), Vec3::unit_y()));

    // The engine's handedness convention: right = forward x up.
    CHECK(near(cross(Vec3::forward(), Vec3::up()), Vec3::right()));

    // project + reject should reconstruct the original vector.
    Vec3 v{2, 3, 4}, onto{0, 1, 0};
    CHECK(near(project(v, onto) + reject(v, onto), v));
}

void test_quat_basics() {
    std::printf("quat basics\n");
    CHECK(near(rotate(Quat::identity(), Vec3{1, 2, 3}), Vec3{1, 2, 3}));

    // 90 degrees about +Y takes +X to -Z (right-handed).
    Quat yaw90 = Quat::from_axis_angle(Vec3::unit_y(), HALF_PI);
    CHECK(near(rotate(yaw90, Vec3::unit_x()), Vec3{0, 0, -1}));

    // Rotation preserves length.
    Quat arbitrary = from_euler(radians(23.0f), radians(-51.0f), radians(77.0f));
    CHECK(near(length(rotate(arbitrary, Vec3{1, 2, 3})), length(Vec3{1, 2, 3})));

    // Composing with the conjugate is the identity.
    Vec3 v{0.3f, -0.7f, 0.2f};
    CHECK(near(rotate(conjugate(arbitrary), rotate(arbitrary, v)), v));

    // Unit norm is maintained through composition.
    Quat composed = arbitrary * yaw90 * arbitrary;
    CHECK(near(std::sqrt(dot(composed, composed)), 1.0f, 1e-3f));
}

void test_quat_frames() {
    std::printf("quat frames\n");
    // look_rotation must produce a right-handed basis that agrees with the
    // Vec3::forward/up/right convention.
    Quat q = look_rotation(Vec3::forward(), Vec3::up());
    CHECK(near(quat_forward(q), Vec3::forward()));
    CHECK(near(quat_up(q), Vec3::up()));
    CHECK(near(quat_right(q), Vec3::right()));

    Vec3 dir = normalize(Vec3{1, 0.5f, -2});
    Quat look = look_rotation(dir, Vec3::up());
    CHECK(near(quat_forward(look), dir));
    // The basis stays orthonormal and right-handed.
    CHECK(near(dot(quat_forward(look), quat_right(look)), 0.0f));
    CHECK(near(cross(quat_forward(look), quat_up(look)), quat_right(look)));

    // rotation_between actually maps from -> to, including the antiparallel
    // case, which is the one that produces NaN if handled carelessly.
    Vec3 from = Vec3::forward(), to = Vec3::up();
    CHECK(near(rotate(rotation_between(from, to), from), to));
    CHECK(near(rotate(rotation_between(Vec3::unit_x(), -Vec3::unit_x()), Vec3::unit_x()),
               -Vec3::unit_x()));
}

void test_slerp() {
    std::printf("slerp\n");
    Quat a = Quat::identity();
    Quat b = Quat::from_axis_angle(Vec3::unit_y(), HALF_PI);
    CHECK(near(rotate(slerp(a, b, 0.0f), Vec3::unit_x()), Vec3::unit_x()));
    CHECK(near(rotate(slerp(a, b, 1.0f), Vec3::unit_x()), Vec3{0, 0, -1}));

    // Halfway is a 45 degree turn.
    Vec3 mid = rotate(slerp(a, b, 0.5f), Vec3::unit_x());
    CHECK(near(mid, Vec3{std::cos(radians(45.0f)), 0, -std::sin(radians(45.0f))}));

    // Near-identical inputs must not divide by ~0.
    Quat almost = Quat::from_axis_angle(Vec3::unit_y(), 1e-6f);
    Quat s = slerp(a, almost, 0.5f);
    CHECK(!std::isnan(s.x) && near(std::sqrt(dot(s, s)), 1.0f));

    // Shortest arc: slerping to -q should behave like slerping to q.
    CHECK(near(rotate(slerp(a, -b, 1.0f), Vec3::unit_x()), Vec3{0, 0, -1}));
}

void test_integrate() {
    std::printf("angular integration\n");
    // Integrating a constant angular velocity for a quarter turn.
    Quat q = Quat::identity();
    const float steps = 2000.0f;
    const float dt = (HALF_PI / 1.0f) / steps;  // 1 rad/s for HALF_PI seconds
    for (int i = 0; i < int(steps); ++i) q = integrate(q, Vec3::unit_y(), dt);
    CHECK(near(rotate(q, Vec3::unit_x()), Vec3{0, 0, -1}, 1e-2f));
    // Integration must not let the quaternion drift off the unit sphere.
    CHECK(near(std::sqrt(dot(q, q)), 1.0f));
}

void test_mat4() {
    std::printf("mat4\n");
    Mat4 id = Mat4::identity();
    CHECK(near(transform_point(id, Vec3{1, 2, 3}), Vec3{1, 2, 3}));

    Mat4 t = Mat4::translation(Vec3{5, 0, -2});
    CHECK(near(transform_point(t, Vec3{1, 1, 1}), Vec3{6, 1, -1}));
    // Directions ignore translation.
    CHECK(near(transform_dir(t, Vec3{1, 1, 1}), Vec3{1, 1, 1}));

    // Mat4::from_quat must agree with direct quaternion rotation.
    Quat r = from_euler(radians(30.0f), radians(-20.0f), radians(10.0f));
    Vec3 v{0.4f, -1.2f, 0.8f};
    CHECK(near(transform_dir(Mat4::from_quat(r), v), rotate(r, v)));

    // TRS composes in the documented order: scale, then rotate, then translate.
    Vec3 pos{3, -1, 2}, scale{2, 2, 2};
    Mat4 trs = Mat4::trs(pos, r, scale);
    CHECK(near(transform_point(trs, v), rotate(r, v * scale.x) + pos));
    CHECK(near(trs.translation_part(), pos));

    // Round-trip through the inverse.
    Mat4 inv = inverse(trs);
    CHECK(near(transform_point(inv, transform_point(trs, v)), v, 1e-3f));

    // Matrix multiply order: (A*B) applies B first.
    Mat4 a = Mat4::translation(Vec3{1, 0, 0});
    Mat4 b = Mat4::from_quat(Quat::from_axis_angle(Vec3::unit_y(), HALF_PI));
    CHECK(near(transform_point(a * b, Vec3::unit_x()), Vec3{1, 0, -1}));
}

void test_view_matrices() {
    std::printf("view matrices\n");
    // look_at and view_from_transform must produce the same view.
    Vec3 eye{4, 3, 10};
    Vec3 target{0, 0, 0};
    Mat4 view_a = look_at(eye, target, Vec3::up());
    Quat orient = look_rotation(target - eye, Vec3::up());
    Mat4 view_b = view_from_transform(eye, orient);
    Vec3 probe{1.5f, -2.0f, 3.0f};
    CHECK(near(transform_point(view_a, probe), transform_point(view_b, probe), 1e-3f));

    // The camera sits at the view-space origin, and the target lies down -Z.
    CHECK(near(transform_point(view_a, eye), Vec3::zero(), 1e-3f));
    Vec3 target_vs = transform_point(view_a, target);
    CHECK(near(target_vs.x, 0.0f, 1e-3f) && near(target_vs.y, 0.0f, 1e-3f));
    CHECK(target_vs.z < 0.0f);
}

void test_projection() {
    std::printf("projection\n");
    const float near_plane = 0.1f, far_plane = 1000.0f;
    Mat4 proj = perspective(radians(60.0f), 16.0f / 9.0f, near_plane, far_plane);

    // Standard projection: near maps to 0, far maps to 1 after the w divide.
    Vec4 at_near = proj * Vec4{0, 0, -near_plane, 1};
    CHECK(near(at_near.z / at_near.w, 0.0f, 1e-3f));
    Vec4 at_far = proj * Vec4{0, 0, -far_plane, 1};
    CHECK(near(at_far.z / at_far.w, 1.0f, 1e-3f));

    // Reversed-Z: near maps to 1 and distance tends to 0. This is what the
    // renderer actually uses, with a GREATER depth compare.
    Mat4 rev = perspective_reverse_z(radians(60.0f), 16.0f / 9.0f, near_plane);
    Vec4 rn = rev * Vec4{0, 0, -near_plane, 1};
    CHECK(near(rn.z / rn.w, 1.0f, 1e-3f));
    Vec4 rf = rev * Vec4{0, 0, -100000.0f, 1};
    CHECK(rf.z / rf.w > 0.0f && rf.z / rf.w < 1e-3f);

    // Depth must be monotonically decreasing with distance, or the GREATER
    // compare sorts things backwards.
    Vec4 d10 = rev * Vec4{0, 0, -10.0f, 1};
    Vec4 d20 = rev * Vec4{0, 0, -20.0f, 1};
    CHECK((d10.z / d10.w) > (d20.z / d20.w));

    // A point at the top of the frustum lands at NDC y = +1.
    float half_h = std::tan(radians(30.0f)) * 5.0f;
    Vec4 top = rev * Vec4{0, half_h, -5.0f, 1};
    CHECK(near(top.y / top.w, 1.0f, 1e-3f));
}

void test_damping() {
    std::printf("damping\n");
    // One half-life should close half the gap, regardless of step size.
    CHECK(near(damp(0.0f, 1.0f, 1.0f, 1.0f), 0.5f));
    CHECK(near(damp(0.0f, 1.0f, 0.5f, 1.0f), 0.75f));

    // Frame-rate independence: many small steps must match one large step.
    float coarse = damp(0.0f, 1.0f, 0.3f, 0.5f);
    float fine = 0.0f;
    for (int i = 0; i < 50; ++i) fine = damp(fine, 1.0f, 0.3f, 0.01f);
    CHECK(near(coarse, fine, 1e-4f));

    CHECK(near(damp(0.0f, 1.0f, 0.0f, 0.016f), 1.0f));  // zero half-life snaps
    CHECK(near(move_toward(0.0f, 1.0f, 0.25f), 0.25f));
    CHECK(near(move_toward(0.0f, 1.0f, 5.0f), 1.0f));  // never overshoots
    CHECK(near(move_toward(1.0f, 0.0f, 5.0f), 0.0f));
}

}  // namespace

// Round-tripping a rotation through a matrix. The 180-degree neighbourhood is
// the whole point: the naive w-from-trace formula divides by something
// approaching zero there and returns garbage.
void test_quat_from_matrix() {
    std::printf("quaternion <-> matrix round trip\n");
    const Vec3 axes[5] = {Vec3::unit_x(), Vec3::unit_y(), Vec3::unit_z(),
                          normalize(Vec3{1.0f, 2.0f, -3.0f}), normalize(Vec3{-4.0f, 1.0f, 0.5f})};
    const float angles[7] = {0.0f, 15.0f, 90.0f, 179.0f, 180.0f, 181.0f, 275.0f};
    for (const Vec3& axis : axes) {
        for (const float degrees_angle : angles) {
            const Quat q = Quat::from_axis_angle(axis, radians(degrees_angle));
            const Quat back = quat_from_matrix(Mat4::trs(Vec3{3.0f, -1.0f, 2.0f}, q, Vec3::one()));
            // Compare by action, not by components: q and -q are the same
            // rotation and either is a correct answer.
            for (const Vec3& probe : axes) {
                CHECK(near(rotate(q, probe), rotate(back, probe), 1e-3f));
            }
        }
    }

    // Uniform scale must not leak into the rotation.
    const Quat q = Quat::from_axis_angle(normalize(Vec3{1.0f, 1.0f, 0.0f}), radians(70.0f));
    const Quat scaled = quat_from_matrix(Mat4::trs(Vec3::zero(), q, Vec3(4.0f)));
    CHECK(near(rotate(q, Vec3::unit_z()), rotate(scaled, Vec3::unit_z()), 1e-3f));
    CHECK(near(length(Vec3{scaled.x, scaled.y, scaled.z}) * 0.0f + 1.0f,
               std::sqrt(scaled.x * scaled.x + scaled.y * scaled.y + scaled.z * scaled.z +
                         scaled.w * scaled.w),
               1e-4f));
}

int main() {
    test_quat_from_matrix();
    test_vec3();
    test_quat_basics();
    test_quat_frames();
    test_slerp();
    test_integrate();
    test_mat4();
    test_view_matrices();
    test_projection();
    test_damping();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
