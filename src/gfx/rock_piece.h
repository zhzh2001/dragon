#pragma once

#include "core/math.h"

namespace gfx {

// The scanned rock kit (assets/props/rockkit/, tools/rockkit.md): eight
// pieces, three levels of detail each, drawn on the modern tier by
// gfx::RockKit. This header is the data the placement needs without a GPU:
// each piece's size, its cliff plane, and the points that must lie under
// the ground for it not to float.
constexpr int ROCK_PIECES = 8;
constexpr int ROCK_LODS = 3;
constexpr int ROCK_CHECK_POINTS = 26;

struct RockPieceInfo {
    const char* name;  // the file stem in assets/props/rockkit/
    bool cliff;        // a shell set into a slope; otherwise a boulder or slab with a flat base
    bool slab;
    core::Vec3 size;   // metres, glTF axes (tools/rockkit.md)
    // A cliff's mounting plane, which the placement lays on the slope: a
    // point on it and its normal toward the face. For cliff_a and cliff_b
    // the file's own 60-degree reference plane (tools/rockkit.md, the point
    // (0, 0, -anchor)); for cliff_c the least-squares plane through its
    // open boundary (every boundary vertex of LOD2), since its reference
    // plane left it 10 to 15 m deep once its foot was buried. Chosen by
    // simulating each piece on planar slopes of 45 to 62 degrees, sunk
    // until its foot is under the ground: 55%, 51% and 45% of the
    // vertices stay above it, with 3 to 8 m of relief at scale 1.
    core::Vec3 mount_centre;
    core::Vec3 mount_normal;
    // Points of LOD2 that must be under the ground: a boulder's base outline
    // and its lowest vertices, a cliff's open rim and its foot. Measured
    // from the files (the boundary edges and the base's hull, farthest-point
    // sampled); the unused tail repeats the first point.
    core::Vec3 check[ROCK_CHECK_POINTS];
    int check_count;
};

inline const RockPieceInfo& rock_piece(int piece) {
    static const RockPieceInfo pieces[ROCK_PIECES] = {
        {"boulder_a", false, false, {3.81f, 2.71f, 5.40f}, {}, {0.0f, 1.0f, 0.0f},
         {{-1.32f, 0.00f, -1.47f}, {1.45f, 0.00f, 2.18f}, {1.70f, 0.00f, -1.78f}, {-0.14f, 0.00f, 0.69f},
          {0.08f, 0.00f, -2.60f}, {-0.66f, 0.00f, -2.05f}, {1.36f, 0.00f, -2.07f}, {0.74f, -0.02f, 0.52f}},
         8},
        {"boulder_b", false, false, {3.45f, 1.91f, 4.40f}, {}, {0.0f, 1.0f, 0.0f},
         {{-1.41f, 0.00f, 0.77f}, {1.67f, 0.00f, 0.20f}, {-0.22f, 0.00f, -1.53f}, {0.10f, 0.00f, 2.17f},
          {0.92f, 0.00f, -1.14f}, {-1.09f, 0.00f, -0.81f}, {1.10f, 0.00f, 1.07f}, {-0.66f, 0.00f, 1.78f}},
         8},
        {"boulder_c", false, false, {3.00f, 2.03f, 2.97f}, {}, {0.0f, 1.0f, 0.0f},
         {{-1.47f, 0.00f, -1.00f}, {1.48f, 0.00f, 0.53f}, {-0.47f, 0.00f, 1.09f}, {0.29f, 0.00f, -1.39f},
          {-0.98f, 0.00f, 0.11f}, {-0.64f, 0.00f, -1.35f}, {0.59f, 0.00f, -0.92f}, {1.05f, 0.00f, 0.80f}},
         8},
        {"slab_a", false, true, {7.00f, 1.74f, 4.21f}, {}, {0.0f, 1.0f, 0.0f},
         {{-2.85f, 0.00f, 0.68f}, {1.29f, 0.00f, 1.14f}, {-0.33f, 0.00f, -1.25f}, {-0.57f, 0.00f, 1.53f},
          {-2.22f, 0.00f, -0.71f}, {0.81f, 0.00f, -0.48f}, {-1.19f, 0.00f, -1.16f}, {1.14f, 0.00f, 0.27f}},
         8},
        {"slab_b", false, true, {4.44f, 1.70f, 8.60f}, {}, {0.0f, 1.0f, 0.0f},
         {{-1.86f, 0.00f, 2.38f}, {0.72f, 0.00f, -3.97f}, {1.57f, 0.00f, 2.16f}, {-1.17f, 0.00f, 4.30f},
          {-1.25f, 0.00f, -3.68f}, {1.57f, 0.00f, -2.51f}, {0.59f, 0.00f, 3.40f}, {-1.70f, 0.00f, 3.49f}},
         8},
        {"cliff_a", true, false, {32.00f, 24.89f, 25.30f}, {0.0f, 0.0f, 4.01f}, {0.0f, 0.5f, 0.866f},
         {{-7.43f, 0.01f, 5.68f}, {7.81f, 24.14f, -10.94f}, {-16.00f, 20.02f, -5.88f}, {15.93f, 2.98f, 3.79f},
          {11.68f, 14.28f, -2.57f}, {-5.15f, 24.78f, -12.33f}, {-16.00f, 9.58f, 0.14f}, {4.47f, 0.75f, 5.35f},
          {-16.00f, 1.07f, 5.06f}, {-13.90f, 24.44f, -12.43f}, {13.25f, 8.71f, 0.67f}, {9.94f, 19.81f, -5.74f},
          {1.09f, 24.85f, -11.88f}, {-1.08f, 0.03f, 5.66f}, {-16.00f, 14.98f, -4.88f}, {11.70f, 0.62f, 5.44f},
          {-16.00f, 4.77f, 2.93f}, {-11.93f, 0.37f, 5.42f}, {-9.79f, 24.78f, -12.13f}, {-16.00f, 23.06f, -9.30f},
          {13.61f, 0.54f, 5.38f}, {0.28f, 0.57f, 5.41f}, {-14.59f, 0.18f, 5.51f}, {9.44f, 0.30f, 5.52f},
          {-3.53f, 0.03f, 5.65f}},
         25},
        {"cliff_b", true, false, {27.99f, 25.53f, 18.29f}, {0.0f, 0.0f, 4.66f}, {0.0f, 0.5f, 0.866f},
         {{0.88f, -0.10f, 8.25f}, {-13.64f, 24.30f, -8.52f}, {11.86f, 20.48f, -3.68f}, {-11.24f, 8.42f, -1.15f},
          {12.50f, 6.86f, 1.74f}, {-0.60f, 25.50f, -6.35f}, {0.14f, 11.01f, 2.65f}, {-7.74f, 3.69f, 6.17f},
          {-14.01f, 15.23f, -6.67f}, {11.09f, 0.44f, 8.06f}, {13.76f, 14.21f, 0.08f}, {7.15f, 25.50f, -6.38f},
          {-7.68f, 25.50f, -7.02f}, {5.82f, 21.35f, -2.28f}, {-7.34f, 6.57f, 1.62f}, {13.33f, 4.19f, 5.93f},
          {-3.94f, 0.44f, 8.04f}, {-13.98f, 11.04f, -3.96f}, {-13.98f, 19.53f, -8.39f}, {5.49f, 0.01f, 8.33f},
          {13.94f, 2.16f, 7.03f}, {-7.02f, 1.51f, 7.46f}, {7.06f, 0.09f, 8.43f}, {-3.33f, 0.01f, 8.32f}},
         24},
        {"cliff_c", true, false, {33.01f, 23.12f, 16.98f}, {-0.43f, 11.10f, -0.62f}, {0.060f, 0.386f, 0.921f},
         {{0.21f, -0.02f, 5.44f}, {15.07f, 21.46f, -7.90f}, {-11.32f, 20.16f, -8.48f}, {-16.47f, 6.79f, 2.63f},
          {16.08f, 4.87f, -0.20f}, {1.79f, 22.99f, -1.08f}, {-9.51f, 0.01f, 4.13f}, {-13.28f, 14.81f, -0.73f},
          {15.75f, 12.78f, -4.62f}, {8.79f, 0.47f, 3.37f}, {-5.89f, 22.50f, -1.73f}, {8.30f, 22.51f, -5.16f},
          {-14.83f, 1.00f, 2.42f}, {14.20f, 0.86f, 2.37f}, {-14.61f, 11.20f, 2.23f}, {-4.97f, 0.18f, 5.41f},
          {-12.80f, 17.17f, -5.35f}, {16.20f, 18.41f, -4.76f}, {16.37f, 8.36f, -2.50f}, {-9.12f, 22.29f, -4.22f},
          {15.28f, 0.73f, 2.16f}, {-14.51f, 0.10f, 3.03f}, {8.13f, 0.17f, 4.38f}, {-6.91f, 0.19f, 3.51f},
          {-10.71f, 0.06f, 3.94f}},
         25},
    };
    return pieces[piece >= 0 && piece < ROCK_PIECES ? piece : 0];
}

// One placed piece. Must match the instance attributes in rock.hlsl and
// rock_depth.hlsl.
struct RockInstance {
    core::Vec4 position_scale;  // xyz the piece's origin (its base centre), w uniform scale
    core::Vec4 rotation;        // a unit quaternion, xyz then w: the piece's axes in the world
    // The ground under the piece as a plane, dot(xyz, p) = w: what the
    // shader fades the contact against (dirt, darkening) at the line where
    // the piece goes into the ground.
    core::Vec4 ground;
    core::Vec4 params;  // x shade, y moss (0..1), z hash 0..1, w unused
};

}  // namespace gfx
