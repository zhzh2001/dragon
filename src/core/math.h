// Minimal math library for the dragon engine.
// Right-handed, Y-up. Matrices are column-major and stored column-major,
// matching Metal's float4x4 so we can memcpy straight into uniform buffers.
#pragma once

#include <cmath>
#include <cstdint>

namespace core {

constexpr float PI      = 3.14159265358979323846f;
constexpr float TWO_PI  = 6.28318530717958647692f;
constexpr float HALF_PI = 1.57079632679489661923f;
constexpr float DEG2RAD = PI / 180.0f;
constexpr float RAD2DEG = 180.0f / PI;

inline float radians(float deg) { return deg * DEG2RAD; }
inline float degrees(float rad) { return rad * RAD2DEG; }
inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float saturate(float v) { return clampf(v, 0.0f, 1.0f); }
inline float signf(float v) { return v < 0.0f ? -1.0f : (v > 0.0f ? 1.0f : 0.0f); }
inline float sqf(float v) { return v * v; }

// Hermite blend with zero derivative at both ends. The workhorse for every
// gameplay ramp: assist strength, valley walls, fog, blend weights.
inline float smoothstep(float edge0, float edge1, float x) {
    if (edge1 <= edge0) return x < edge0 ? 0.0f : 1.0f;
    float t = clampf((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Remap x from [in0, in1] to [out0, out1], clamped at both ends.
inline float remap(float x, float in0, float in1, float out0, float out1) {
    if (in1 == in0) return out0;
    float t = clampf((x - in0) / (in1 - in0), 0.0f, 1.0f);
    return out0 + (out1 - out0) * t;
}

// Move `cur` toward `target` at most `max_delta`.
inline float move_toward(float cur, float target, float max_delta) {
    float d = target - cur;
    if (std::fabs(d) <= max_delta) return target;
    return cur + signf(d) * max_delta;
}

// Frame-rate independent exponential smoothing.
// `half_life` is the time for the gap to halve. half_life <= 0 snaps.
inline float damp(float cur, float target, float half_life, float dt) {
    if (half_life <= 0.0f) return target;
    float k = std::exp2(-dt / half_life);
    return target + (cur - target) * k;
}

// ---------------------------------------------------------------- vec2

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
    float& operator[](int i) { return (&x)[i]; }
    float  operator[](int i) const { return (&x)[i]; }
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
inline Vec2 operator*(float s, Vec2 a) { return {a.x * s, a.y * s}; }
inline Vec2 operator/(Vec2 a, float s) { return {a.x / s, a.y / s}; }
inline Vec2 operator-(Vec2 a) { return {-a.x, -a.y}; }
inline Vec2& operator+=(Vec2& a, Vec2 b) { a.x += b.x; a.y += b.y; return a; }
inline Vec2& operator-=(Vec2& a, Vec2 b) { a.x -= b.x; a.y -= b.y; return a; }
inline Vec2& operator*=(Vec2& a, float s) { a.x *= s; a.y *= s; return a; }
inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float length_sq(Vec2 v) { return dot(v, v); }
inline float length(Vec2 v) { return std::sqrt(dot(v, v)); }

// ---------------------------------------------------------------- vec3

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    explicit Vec3(float s) : x(s), y(s), z(s) {}
    float& operator[](int i) { return (&x)[i]; }
    float  operator[](int i) const { return (&x)[i]; }

    static Vec3 zero()    { return {0, 0, 0}; }
    static Vec3 one()     { return {1, 1, 1}; }
    static Vec3 unit_x()  { return {1, 0, 0}; }
    static Vec3 unit_y()  { return {0, 1, 0}; }
    static Vec3 unit_z()  { return {0, 0, 1}; }
    // Engine convention: forward is -Z, up is +Y, right is +X.
    static Vec3 forward() { return {0, 0, -1}; }
    static Vec3 up()      { return {0, 1, 0}; }
    static Vec3 right()   { return {1, 0, 0}; }
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(float s, Vec3 a) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline Vec3 operator/(Vec3 a, float s) { return {a.x / s, a.y / s, a.z / s}; }
inline Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
inline Vec3& operator+=(Vec3& a, Vec3 b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
inline Vec3& operator-=(Vec3& a, Vec3 b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; return a; }
inline Vec3& operator*=(Vec3& a, float s) { a.x *= s; a.y *= s; a.z *= s; return a; }
inline Vec3& operator/=(Vec3& a, float s) { a.x /= s; a.y /= s; a.z /= s; return a; }

inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3  cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length_sq(Vec3 v) { return dot(v, v); }
inline float length(Vec3 v) { return std::sqrt(dot(v, v)); }
inline float distance(Vec3 a, Vec3 b) { return length(b - a); }

inline Vec3 normalize(Vec3 v) {
    float len_sq = dot(v, v);
    if (len_sq < 1e-20f) return Vec3::zero();
    return v * (1.0f / std::sqrt(len_sq));
}

// Normalize, falling back to `fallback` for degenerate input. Use this on
// anything derived from gameplay state, where a zero vector is reachable.
inline Vec3 normalize_or(Vec3 v, Vec3 fallback) {
    float len_sq = dot(v, v);
    if (len_sq < 1e-20f) return fallback;
    return v * (1.0f / std::sqrt(len_sq));
}

inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
inline Vec3 minv(Vec3 a, Vec3 b) {
    return {a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z};
}
inline Vec3 maxv(Vec3 a, Vec3 b) {
    return {a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z};
}

// Component of `v` along `onto` (onto need not be normalized).
inline Vec3 project(Vec3 v, Vec3 onto) {
    float d = dot(onto, onto);
    if (d < 1e-20f) return Vec3::zero();
    return onto * (dot(v, onto) / d);
}
// Component of `v` perpendicular to `onto`.
inline Vec3 reject(Vec3 v, Vec3 onto) { return v - project(v, onto); }

inline Vec3 damp(Vec3 cur, Vec3 target, float half_life, float dt) {
    if (half_life <= 0.0f) return target;
    float k = std::exp2(-dt / half_life);
    return target + (cur - target) * k;
}

// ---------------------------------------------------------------- vec4

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    Vec4() = default;
    Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    Vec4(Vec3 v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    Vec3 xyz() const { return {x, y, z}; }
    float& operator[](int i) { return (&x)[i]; }
    float  operator[](int i) const { return (&x)[i]; }
};

inline Vec4 operator+(Vec4 a, Vec4 b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
inline Vec4 operator-(Vec4 a, Vec4 b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
inline Vec4 operator*(Vec4 a, float s) { return {a.x * s, a.y * s, a.z * s, a.w * s}; }
inline float dot(Vec4 a, Vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

// ---------------------------------------------------------------- quat

// Unit quaternion, (x,y,z) vector part + w scalar part.
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    Quat() = default;
    Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    static Quat identity() { return {0, 0, 0, 1}; }

    static Quat from_axis_angle(Vec3 axis, float angle) {
        Vec3 n = normalize_or(axis, Vec3::unit_y());
        float h = angle * 0.5f;
        float s = std::sin(h);
        return {n.x * s, n.y * s, n.z * s, std::cos(h)};
    }
};

inline Quat operator*(Quat a, Quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline Quat operator*(Quat q, float s) { return {q.x * s, q.y * s, q.z * s, q.w * s}; }
inline Quat operator+(Quat a, Quat b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
inline Quat operator-(Quat q) { return {-q.x, -q.y, -q.z, -q.w}; }
inline Quat operator-(Quat a, Quat b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
inline float dot(Quat a, Quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline Quat conjugate(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }

inline Quat normalize(Quat q) {
    float len_sq = dot(q, q);
    if (len_sq < 1e-20f) return Quat::identity();
    return q * (1.0f / std::sqrt(len_sq));
}

// Rotate a vector by a unit quaternion: v + 2w(q x v) + 2(q x (q x v)).
inline Vec3 rotate(Quat q, Vec3 v) {
    Vec3 u{q.x, q.y, q.z};
    Vec3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}

inline Vec3 quat_forward(Quat q) { return rotate(q, Vec3::forward()); }
inline Vec3 quat_up(Quat q)      { return rotate(q, Vec3::up()); }
inline Vec3 quat_right(Quat q)   { return rotate(q, Vec3::right()); }

// Shortest-arc spherical interpolation.
inline Quat slerp(Quat a, Quat b, float t) {
    float d = dot(a, b);
    if (d < 0.0f) { b = -b; d = -d; }
    // Near-parallel: lerp and renormalize to avoid a division by ~0.
    if (d > 0.9995f) return normalize(a + (b - a) * t);
    float theta = std::acos(clampf(d, -1.0f, 1.0f));
    float sin_theta = std::sin(theta);
    float wa = std::sin((1.0f - t) * theta) / sin_theta;
    float wb = std::sin(t * theta) / sin_theta;
    return normalize(a * wa + b * wb);
}
// Integrate an angular velocity (radians/sec, world space) over dt.
inline Quat integrate(Quat q, Vec3 angular_velocity, float dt) {
    Vec3 w = angular_velocity * (dt * 0.5f);
    Quat dq = Quat{w.x, w.y, w.z, 0.0f} * q;
    return normalize(Quat{q.x + dq.x, q.y + dq.y, q.z + dq.z, q.w + dq.w});
}

// Rotation taking `from` to `to` (both are normalized internally).
inline Quat rotation_between(Vec3 from, Vec3 to) {
    Vec3 f = normalize_or(from, Vec3::forward());
    Vec3 t = normalize_or(to, Vec3::forward());
    float d = dot(f, t);
    if (d >= 1.0f - 1e-6f) return Quat::identity();
    if (d <= -1.0f + 1e-6f) {
        // Antiparallel: any perpendicular axis gives a 180 degree turn.
        Vec3 axis = cross(f, Vec3::unit_x());
        if (length_sq(axis) < 1e-8f) axis = cross(f, Vec3::unit_y());
        return Quat::from_axis_angle(normalize(axis), PI);
    }
    Vec3 c = cross(f, t);
    return normalize(Quat{c.x, c.y, c.z, 1.0f + d});
}

// Orientation looking along `forward` with roll resolved toward `up`.
inline Quat look_rotation(Vec3 forward, Vec3 up = Vec3::up()) {
    Vec3 f = normalize_or(forward, Vec3::forward());
    Vec3 r = cross(f, up);
    // forward parallel to up: pick any perpendicular reference.
    if (length_sq(r) < 1e-8f) r = cross(f, Vec3::unit_z());
    if (length_sq(r) < 1e-8f) r = cross(f, Vec3::unit_x());
    r = normalize(r);
    Vec3 u = cross(r, f);

    // Build from the rotation matrix [r, u, -f] using the largest-trace branch
    // for numerical stability.
    float m00 = r.x, m01 = u.x, m02 = -f.x;
    float m10 = r.y, m11 = u.y, m12 = -f.y;
    float m20 = r.z, m21 = u.z, m22 = -f.z;
    float trace = m00 + m11 + m22;
    Quat q;
    if (trace > 0.0f) {
        float s = std::sqrt(trace + 1.0f) * 2.0f;
        q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
    } else if (m00 > m11 && m00 > m22) {
        float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
    }
    return normalize(q);
}

// Aircraft-style Euler angles, in radians. Applied yaw, then pitch, then roll.
inline Quat from_euler(float pitch, float yaw, float roll) {
    Quat qy = Quat::from_axis_angle(Vec3::unit_y(), yaw);
    Quat qx = Quat::from_axis_angle(Vec3::unit_x(), pitch);
    Quat qz = Quat::from_axis_angle(Vec3::unit_z(), roll);
    return normalize(qy * qx * qz);
}

// ---------------------------------------------------------------- mat4

// Column-major storage: col[c][r]. Layout matches Metal float4x4.
struct Mat4 {
    Vec4 col[4];

    Mat4() : col{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}} {}

    static Mat4 identity() { return Mat4(); }

    static Mat4 zero() {
        Mat4 m;
        for (int c = 0; c < 4; ++c) m.col[c] = Vec4{0, 0, 0, 0};
        return m;
    }

    static Mat4 translation(Vec3 t) {
        Mat4 m;
        m.col[3] = Vec4{t, 1.0f};
        return m;
    }

    static Mat4 scaling(Vec3 s) {
        Mat4 m;
        m.col[0].x = s.x;
        m.col[1].y = s.y;
        m.col[2].z = s.z;
        return m;
    }

    static Mat4 from_quat(Quat q) {
        float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        Mat4 m;
        m.col[0] = Vec4{1 - 2 * (yy + zz), 2 * (xy + wz), 2 * (xz - wy), 0};
        m.col[1] = Vec4{2 * (xy - wz), 1 - 2 * (xx + zz), 2 * (yz + wx), 0};
        m.col[2] = Vec4{2 * (xz + wy), 2 * (yz - wx), 1 - 2 * (xx + yy), 0};
        m.col[3] = Vec4{0, 0, 0, 1};
        return m;
    }

    // Scale, then rotate, then translate.
    static Mat4 trs(Vec3 t, Quat r, Vec3 s) {
        Mat4 m = from_quat(r);
        m.col[0] = m.col[0] * s.x;
        m.col[1] = m.col[1] * s.y;
        m.col[2] = m.col[2] * s.z;
        m.col[3] = Vec4{t, 1.0f};
        return m;
    }

    Vec3 translation_part() const { return col[3].xyz(); }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r = Mat4::zero();
    for (int c = 0; c < 4; ++c) {
        for (int i = 0; i < 4; ++i) {
            float bi = b.col[c][i];
            for (int j = 0; j < 4; ++j) r.col[c][j] += a.col[i][j] * bi;
        }
    }
    return r;
}

inline Vec4 operator*(const Mat4& m, Vec4 v) {
    Vec4 r{0, 0, 0, 0};
    for (int c = 0; c < 4; ++c)
        for (int j = 0; j < 4; ++j) r[j] += m.col[c][j] * v[c];
    return r;
}

// Transform a point (w = 1), ignoring any projective divide.
inline Vec3 transform_point(const Mat4& m, Vec3 p) { return (m * Vec4{p, 1.0f}).xyz(); }
// Transform a direction (w = 0), so translation is skipped.
inline Vec3 transform_dir(const Mat4& m, Vec3 d) { return (m * Vec4{d, 0.0f}).xyz(); }

inline Mat4 transpose(const Mat4& m) {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int j = 0; j < 4; ++j) r.col[c][j] = m.col[j][c];
    return r;
}

// Right-handed look-at view matrix.
inline Mat4 look_at(Vec3 eye, Vec3 target, Vec3 up) {
    Vec3 f = normalize_or(target - eye, Vec3::forward());  // forward
    Vec3 r = cross(f, up);
    if (length_sq(r) < 1e-8f) r = cross(f, Vec3::unit_z());
    r = normalize(r);
    Vec3 u = cross(r, f);
    Mat4 m;
    m.col[0] = Vec4{r.x, u.x, -f.x, 0};
    m.col[1] = Vec4{r.y, u.y, -f.y, 0};
    m.col[2] = Vec4{r.z, u.z, -f.z, 0};
    m.col[3] = Vec4{-dot(r, eye), -dot(u, eye), dot(f, eye), 1};
    return m;
}

// View matrix from a camera position and orientation.
inline Mat4 view_from_transform(Vec3 pos, Quat rot) {
    Quat inv = conjugate(rot);
    Mat4 m = Mat4::from_quat(inv);
    Vec3 t = rotate(inv, -pos);
    m.col[3] = Vec4{t, 1.0f};
    return m;
}

// Right-handed perspective projection mapping z into [0, 1] (Metal/Vulkan
// convention), with `fov_y` in radians.
inline Mat4 perspective(float fov_y, float aspect, float z_near, float z_far) {
    float t = 1.0f / std::tan(fov_y * 0.5f);
    Mat4 m = Mat4::zero();
    m.col[0].x = t / aspect;
    m.col[1].y = t;
    m.col[2].z = z_far / (z_near - z_far);
    m.col[2].w = -1.0f;
    m.col[3].z = (z_near * z_far) / (z_near - z_far);
    return m;
}

// Reversed-Z infinite-far perspective: near maps to 1, infinity to 0. Gives
// far better depth precision, which matters for long view distances in flight.
inline Mat4 perspective_reverse_z(float fov_y, float aspect, float z_near) {
    float t = 1.0f / std::tan(fov_y * 0.5f);
    Mat4 m = Mat4::zero();
    m.col[0].x = t / aspect;
    m.col[1].y = t;
    m.col[2].z = 0.0f;
    m.col[2].w = -1.0f;
    m.col[3].z = z_near;
    return m;
}

// Right-handed orthographic projection mapping z into [0, 1].
inline Mat4 ortho(float l, float r, float b, float t, float z_near, float z_far) {
    Mat4 m;
    m.col[0].x = 2.0f / (r - l);
    m.col[1].y = 2.0f / (t - b);
    m.col[2].z = 1.0f / (z_near - z_far);
    m.col[3] = Vec4{-(r + l) / (r - l), -(t + b) / (t - b), z_near / (z_near - z_far), 1.0f};
    return m;
}

// General 4x4 inverse via cofactor expansion.
inline Mat4 inverse(const Mat4& m) {
    const float* a = &m.col[0].x;
    float s0 = a[0] * a[5] - a[4] * a[1];
    float s1 = a[0] * a[6] - a[4] * a[2];
    float s2 = a[0] * a[7] - a[4] * a[3];
    float s3 = a[1] * a[6] - a[5] * a[2];
    float s4 = a[1] * a[7] - a[5] * a[3];
    float s5 = a[2] * a[7] - a[6] * a[3];

    float c5 = a[10] * a[15] - a[14] * a[11];
    float c4 = a[9] * a[15] - a[13] * a[11];
    float c3 = a[9] * a[14] - a[13] * a[10];
    float c2 = a[8] * a[15] - a[12] * a[11];
    float c1 = a[8] * a[14] - a[12] * a[10];
    float c0 = a[8] * a[13] - a[12] * a[9];

    float det = s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
    if (std::fabs(det) < 1e-20f) return Mat4::identity();
    float id = 1.0f / det;

    Mat4 r;
    float* o = &r.col[0].x;
    o[0]  = ( a[5] * c5 - a[6] * c4 + a[7] * c3) * id;
    o[1]  = (-a[1] * c5 + a[2] * c4 - a[3] * c3) * id;
    o[2]  = ( a[13] * s5 - a[14] * s4 + a[15] * s3) * id;
    o[3]  = (-a[9] * s5 + a[10] * s4 - a[11] * s3) * id;
    o[4]  = (-a[4] * c5 + a[6] * c2 - a[7] * c1) * id;
    o[5]  = ( a[0] * c5 - a[2] * c2 + a[3] * c1) * id;
    o[6]  = (-a[12] * s5 + a[14] * s2 - a[15] * s1) * id;
    o[7]  = ( a[8] * s5 - a[10] * s2 + a[11] * s1) * id;
    o[8]  = ( a[4] * c4 - a[5] * c2 + a[7] * c0) * id;
    o[9]  = (-a[0] * c4 + a[1] * c2 - a[3] * c0) * id;
    o[10] = ( a[12] * s4 - a[13] * s2 + a[15] * s0) * id;
    o[11] = (-a[8] * s4 + a[9] * s2 - a[11] * s0) * id;
    o[12] = (-a[4] * c3 + a[5] * c1 - a[6] * c0) * id;
    o[13] = ( a[0] * c3 - a[1] * c1 + a[2] * c0) * id;
    o[14] = (-a[12] * s3 + a[13] * s1 - a[14] * s0) * id;
    o[15] = ( a[8] * s3 - a[9] * s1 + a[10] * s0) * id;
    return r;
}

// ---------------------------------------------------------------- transform

struct Transform {
    Vec3 position = Vec3::zero();
    Quat rotation = Quat::identity();
    Vec3 scale    = Vec3::one();

    Mat4 to_matrix() const { return Mat4::trs(position, rotation, scale); }
    Vec3 forward() const { return quat_forward(rotation); }
    Vec3 up() const { return quat_up(rotation); }
    Vec3 right() const { return quat_right(rotation); }
};

// Compose parent * child.
inline Transform combine(const Transform& parent, const Transform& child) {
    Transform r;
    r.rotation = normalize(parent.rotation * child.rotation);
    r.scale    = parent.scale * child.scale;
    r.position = parent.position + rotate(parent.rotation, child.position * parent.scale);
    return r;
}

}  // namespace core
