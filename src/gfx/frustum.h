#pragma once

#include "core/math.h"

namespace gfx {

// The six clip planes of a [0, 1] clip-depth projection (Gribb/Hartmann),
// each normalised, as (normal, d) with inside being dot(n, p) + d >= 0. Row i
// of the column-major matrix is col[c][i]. Valid for the reversed-Z main
// projection and the conventional shadow ortho alike: both keep z in [0, w].
struct Frustum {
    core::Vec4 planes[6];
    int count = 0;

    explicit Frustum(const core::Mat4& m) {
        auto row = [&](int i) {
            return core::Vec4{m.col[0][i], m.col[1][i], m.col[2][i], m.col[3][i]};
        };
        const core::Vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
        auto add = [&](core::Vec4 p) {
            const float len = core::length(p.xyz());
            if (len < 1e-6f) return;
            planes[count++] = core::Vec4{p.x / len, p.y / len, p.z / len, p.w / len};
        };
        add(core::Vec4{r3.x + r0.x, r3.y + r0.y, r3.z + r0.z, r3.w + r0.w});  // left
        add(core::Vec4{r3.x - r0.x, r3.y - r0.y, r3.z - r0.z, r3.w - r0.w});  // right
        add(core::Vec4{r3.x + r1.x, r3.y + r1.y, r3.z + r1.z, r3.w + r1.w});  // bottom
        add(core::Vec4{r3.x - r1.x, r3.y - r1.y, r3.z - r1.z, r3.w - r1.w});  // top
        add(r2);                                                                // z >= 0
        add(core::Vec4{r3.x - r2.x, r3.y - r2.y, r3.z - r2.z, r3.w - r2.w});  // z <= w
    }

    bool sees(core::Vec3 centre, float radius) const {
        for (int i = 0; i < count; ++i) {
            const core::Vec4& p = planes[i];
            if (p.x * centre.x + p.y * centre.y + p.z * centre.z + p.w < -radius) return false;
        }
        return true;
    }
};

}  // namespace gfx
