#pragma once

#include "core/math.h"

namespace gfx {

// The rock kit (assets/props/rockkit/, tools/rockkit.md): eight pieces
// from Poly Haven scans, six from Tripo (tools/rockkit.md, "Tripo
// pieces") and five Tripo wall crags ("Wall crags and more rocks"; their
// entries are printed by tools/rockkit_table.py), three levels of detail each, drawn on the modern tier by
// gfx::RockKit. This header is the data the placement needs without a GPU:
// each piece's size, its cliff plane, and the points that must lie under
// the ground for it not to float.
constexpr int ROCK_PIECES = 19;
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
        // The Tripo pieces (assets/props/rockkit/pieces.json): a tor and a
        // ridge crest (crags), a scree cone, three boulders. Each stands
        // upright on its flat base and turns only about the vertical; the
        // check points are its base outline from LOD2.
        {"tor_a", false, false, {22.01f, 24.01f, 22.75f}, {}, {0.0f, 1.0f, 0.0f},
         {{2.71f, -0.12f, 8.96f}, {-3.18f, 0.50f, -11.14f}, {-10.45f, 0.40f, 1.89f}, {8.14f, 0.50f, -3.24f},
          {-6.20f, 0.08f, 10.40f}, {-9.97f, 0.55f, -5.76f}, {8.25f, 0.70f, 4.14f}, {1.04f, 0.04f, -5.73f},
          {7.57f, 0.01f, -8.90f}, {-2.59f, 0.11f, 7.45f}, {10.88f, 0.35f, 0.75f}, {-8.89f, 0.42f, 5.59f},
          {7.03f, 0.39f, 0.58f}, {5.02f, 0.58f, 5.99f}, {-9.29f, 0.62f, -2.47f}, {5.43f, 0.57f, -6.49f},
          {-5.73f, 0.08f, 7.43f}, {8.81f, 0.17f, -6.10f}, {-7.81f, 0.57f, 2.72f}, {-3.45f, 0.35f, -8.55f},
          {-0.79f, 0.14f, -10.54f}, {7.43f, 0.67f, 6.58f}, {-3.82f, -0.08f, 10.94f}, {1.36f, 0.11f, 6.99f},
          {3.00f, -0.01f, -7.11f}, {-8.36f, 0.45f, -7.30f}},
         26},
        {"ridge_a", false, false, {40.03f, 16.05f, 26.27f}, {}, {0.0f, 1.0f, 0.0f},
         {{-19.28f, -0.06f, -6.03f}, {20.06f, 0.12f, 9.26f}, {4.47f, 0.30f, -5.84f}, {-6.25f, 0.42f, 7.43f},
          {6.36f, 0.22f, 11.65f}, {-9.02f, 0.33f, -12.21f}, {12.07f, 0.09f, -0.66f}, {-14.08f, 0.35f, 0.74f},
          {-2.28f, 0.46f, -8.86f}, {14.08f, 0.21f, 12.75f}, {16.88f, 0.25f, 3.72f}, {-15.21f, 0.27f, -12.98f},
          {0.75f, 0.39f, 9.08f}, {-10.44f, 0.12f, 4.10f}, {-17.47f, 0.40f, -1.38f}, {-2.49f, 0.15f, 7.14f},
          {9.93f, 0.05f, 12.83f}, {-18.71f, 0.12f, -9.59f}, {-5.67f, 0.09f, -11.56f}, {18.79f, 0.27f, 6.32f},
          {5.41f, 0.16f, -2.96f}, {17.69f, 0.22f, 11.10f}, {0.54f, 0.08f, -8.17f}, {-12.50f, 0.02f, -12.69f},
          {15.80f, 0.47f, 1.43f}, {-7.08f, 0.02f, 5.10f}},
         26},
        {"scree_a", false, false, {20.01f, 7.03f, 20.33f}, {}, {0.0f, 1.0f, 0.0f},
         {{6.91f, 0.01f, 5.09f}, {-8.14f, 0.06f, -5.29f}, {5.54f, 0.11f, -7.86f}, {-6.11f, 0.15f, 7.29f},
          {-1.73f, 0.08f, -9.80f}, {1.15f, 0.20f, 9.64f}, {3.52f, 0.07f, -1.67f}, {-8.75f, 0.16f, 1.13f},
          {9.38f, 0.21f, -3.02f}, {8.38f, 0.08f, 1.31f}, {-2.51f, 0.21f, 8.77f}, {1.59f, 0.15f, -8.41f},
          {-7.53f, 0.17f, 4.41f}, {-8.85f, 0.02f, -2.02f}, {-6.38f, 0.16f, -7.15f}, {8.81f, 0.16f, -0.70f},
          {-4.09f, 0.21f, 7.39f}, {8.76f, 0.22f, 3.27f}, {-0.35f, 0.09f, -8.45f}, {2.52f, 0.06f, 8.43f},
          {-2.04f, 0.04f, -8.25f}, {4.00f, 0.18f, -7.70f}, {-0.31f, 0.07f, 9.22f}, {3.01f, 0.23f, -8.89f},
          {-8.78f, 0.19f, -0.56f}, {1.00f, 0.11f, 8.34f}},
         26},
        {"boulder_d", false, false, {4.51f, 2.60f, 4.54f}, {}, {0.0f, 1.0f, 0.0f},
         {{-1.70f, 0.00f, -0.84f}, {1.39f, 0.00f, 0.89f}, {1.09f, 0.07f, -1.55f}, {-0.49f, 0.00f, 1.49f},
          {-1.83f, 0.01f, 0.31f}, {1.53f, 0.03f, -0.18f}, {0.06f, 0.04f, -1.73f}, {0.24f, 0.06f, 1.54f},
          {1.34f, 0.05f, -1.22f}},
         9},
        {"boulder_e", false, false, {5.01f, 3.99f, 5.10f}, {}, {0.0f, 1.0f, 0.0f},
         {{0.40f, -0.00f, -1.64f}, {1.40f, 0.48f, 1.05f}, {2.01f, 0.43f, -1.19f}, {-0.74f, 0.45f, -1.83f},
          {1.39f, 0.17f, -0.24f}, {0.18f, 0.40f, -2.18f}, {1.02f, 0.09f, 0.86f}, {0.88f, 0.29f, -1.54f},
          {1.58f, 0.35f, -1.37f}, {1.27f, 0.15f, 0.20f}, {1.62f, 0.14f, -0.62f}, {1.80f, 0.36f, -0.91f},
          {1.02f, 0.34f, 1.11f}},
         13},
        {"boulder_f", false, false, {3.23f, 3.08f, 4.00f}, {}, {0.0f, 1.0f, 0.0f},
         {{-1.21f, 0.02f, -1.98f}, {0.59f, 0.06f, 1.90f}, {1.14f, 0.02f, -1.05f}, {-1.57f, 0.08f, -0.20f},
          {0.74f, 0.05f, 0.28f}, {-0.60f, 0.10f, 0.79f}, {-0.24f, 0.07f, -0.59f}, {-1.30f, 0.03f, -0.97f},
          {-0.19f, 0.07f, 1.45f}, {0.62f, 0.12f, -1.46f}, {1.07f, 0.07f, 1.50f}, {-1.09f, 0.12f, -0.56f},
          {-0.75f, 0.03f, -1.97f}, {0.83f, 0.11f, 0.65f}},
         14},
        {"crag_a", false, false, {49.86f, 50.05f, 55.78f}, {}, {0.0f, 1.0f, 0.0f},
         {{21.81f, -0.25f, -14.91f}, {-17.63f, 0.26f, 3.67f}, {-8.24f, 0.60f, -24.56f}, {10.09f, 0.34f, 10.09f},
          {6.18f, 0.59f, -14.88f}, {-5.75f, 0.96f, 13.04f}, {-14.12f, 0.49f, -12.40f}, {13.20f, 1.22f, -4.04f},
          {23.58f, 0.17f, -2.93f}, {0.85f, 0.13f, -23.93f}, {14.26f, 0.96f, -13.02f}, {-15.86f, 0.58f, -5.04f},
          {15.45f, 0.28f, 3.08f}, {-13.50f, 1.06f, 9.89f}, {25.13f, 0.13f, -9.15f}, {18.99f, 0.04f, -6.91f},
          {5.00f, 0.18f, 13.40f}, {-10.98f, 0.05f, -17.13f}, {8.03f, 0.40f, -9.53f}, {-3.76f, 0.83f, -27.47f},
          {3.45f, 0.14f, -19.41f}, {16.71f, 1.21f, 7.94f}, {20.26f, 1.24f, 2.74f}, {-10.15f, 0.26f, 13.24f},
          {-16.33f, 0.79f, -0.51f}, {-1.41f, 0.67f, 13.24f}},
         26},
        {"crag_b", false, false, {60.16f, 58.03f, 65.22f}, {}, {0.0f, 1.0f, 0.0f},
         {{-28.21f, -0.11f, -13.37f}, {28.12f, 1.91f, 14.61f}, {-13.17f, 0.75f, 32.72f}, {11.56f, 0.52f, -27.99f},
          {-23.31f, -0.09f, 12.06f}, {26.10f, 0.05f, -10.15f}, {10.82f, 0.41f, 28.08f}, {-11.00f, 0.49f, -26.99f},
          {-29.24f, 0.04f, 1.06f}, {-22.43f, 1.73f, 24.20f}, {-2.86f, 0.89f, 26.18f}, {0.35f, 0.26f, -31.10f},
          {28.77f, 1.82f, 0.55f}, {17.73f, 0.09f, 20.95f}, {-18.34f, 0.24f, -23.27f}, {17.66f, -0.05f, -23.16f},
          {23.85f, 0.88f, -17.41f}, {4.00f, 0.14f, 29.64f}, {28.20f, 0.59f, 7.30f}, {-28.38f, 1.88f, -6.88f},
          {-23.56f, 0.43f, -19.51f}, {-6.55f, 0.28f, -31.56f}, {-21.46f, 0.27f, 18.06f}, {-17.58f, 0.09f, 27.77f},
          {-27.67f, 1.78f, 6.30f}, {16.07f, 0.31f, 26.28f}},
         26},
        {"crag_c", false, false, {53.42f, 54.07f, 50.70f}, {}, {0.0f, 1.0f, 0.0f},
         {{-19.43f, -0.01f, -14.77f}, {24.71f, 0.91f, 7.35f}, {-10.84f, 1.62f, 20.19f}, {11.17f, 0.74f, -23.16f},
          {8.45f, 0.20f, 17.43f}, {-14.17f, 0.02f, 1.71f}, {23.17f, 0.26f, -9.84f}, {-4.68f, 1.00f, -19.89f},
          {-0.51f, 0.51f, 21.37f}, {17.20f, 0.83f, 14.49f}, {26.52f, 0.93f, -0.81f}, {-17.72f, 0.48f, -6.71f},
          {-12.39f, 0.21f, 12.27f}, {3.25f, 0.12f, -20.95f}, {20.44f, 1.40f, -17.22f}, {-11.16f, 0.71f, -16.97f},
          {-5.96f, 0.85f, 22.00f}, {3.92f, 1.72f, 22.24f}, {20.50f, 0.38f, 11.54f}, {25.38f, 0.15f, -4.94f},
          {-15.09f, 0.25f, 5.72f}, {6.80f, 0.34f, -22.80f}, {11.77f, 0.98f, 15.32f}, {-11.61f, 0.80f, 16.44f},
          {25.10f, 0.07f, 2.70f}, {13.48f, 0.14f, -20.33f}},
         26},
        {"crag_d", false, false, {40.08f, 47.03f, 34.81f}, {}, {0.0f, 1.0f, 0.0f},
         {{14.94f, -0.05f, 2.09f}, {-14.34f, 0.07f, -10.21f}, {-2.99f, 0.65f, 16.60f}, {2.32f, 0.12f, -13.70f},
          {-12.48f, 0.26f, 6.76f}, {7.93f, 0.03f, 11.46f}, {14.15f, 0.14f, -7.14f}, {-11.62f, 1.01f, -2.62f},
          {-5.66f, 0.08f, -13.05f}, {-7.53f, 1.19f, 11.34f}, {8.13f, 0.06f, -13.20f}, {3.45f, 0.17f, 14.72f},
          {14.92f, 0.11f, 7.53f}, {-13.85f, 1.45f, 2.56f}, {-4.04f, -0.01f, 12.16f}, {10.72f, 0.12f, -10.44f},
          {14.10f, 0.30f, -1.31f}, {-1.03f, 0.37f, -13.63f}, {-12.42f, 0.93f, 9.45f}, {17.15f, 0.47f, 3.66f},
          {10.48f, 0.16f, 12.36f}, {13.98f, 0.27f, 5.01f}, {-9.75f, 0.06f, 10.40f}, {1.02f, 0.46f, 15.64f},
          {0.34f, 0.41f, -11.53f}, {-13.18f, 1.23f, -0.81f}},
         26},
        {"crag_e", false, false, {61.74f, 52.03f, 43.48f}, {}, {0.0f, 1.0f, 0.0f},
         {{18.78f, 0.08f, 12.13f}, {-25.83f, 1.07f, -9.58f}, {9.92f, 1.37f, -20.97f}, {-12.99f, 0.31f, 19.33f},
          {29.97f, 0.93f, -8.09f}, {-7.62f, 0.43f, -15.32f}, {3.18f, 0.35f, 14.92f}, {-27.56f, 0.51f, 13.13f},
          {-29.54f, 1.62f, 1.61f}, {20.79f, 0.45f, -18.27f}, {29.75f, 0.21f, 1.76f}, {0.07f, 0.85f, -17.46f},
          {-18.62f, 0.64f, -12.22f}, {-22.55f, 1.12f, 18.22f}, {-6.08f, 1.39f, 17.81f}, {9.39f, 0.50f, 11.47f},
          {24.21f, 1.48f, 7.84f}, {5.54f, 0.21f, -18.04f}, {-27.65f, 0.50f, -5.23f}, {25.38f, 0.22f, -17.19f},
          {-1.35f, 0.90f, 15.70f}, {-14.50f, 0.26f, -12.38f}, {-19.29f, 0.96f, 20.64f}, {-27.95f, 0.59f, 4.89f},
          {15.22f, 0.51f, 11.26f}, {-4.19f, 0.48f, -16.36f}},
         26},
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
