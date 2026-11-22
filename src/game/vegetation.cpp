#include "game/vegetation.h"

#include <cmath>

#include "core/log.h"
#include "game/terrain.h"
#include "gfx/noise_lattice.h"

using core::Vec3;
using core::Vec4;
using gfx::GrassKind;
using gfx::TreeKind;

namespace game {

namespace {

// A stateless hash of a grid cell, so every query of the same cell agrees
// without storing anything. Decorrelated values per cell.
// How much darker a plant fully under a crown is: the terrain's ambient
// loses about this much there (terrain.hlsl, the crown shade), and a blade's
// whole colour is all the instance can scale.
constexpr float kCrownShadeOnGrass = 0.4f;

struct CellHash {
    uint32_t state;
    CellHash(int ix, int iz, uint32_t seed) {
        uint32_t h = uint32_t(ix) * 0x8da6b343u ^ uint32_t(iz) * 0xd8163841u ^ seed * 0xcb1ab31fu;
        h ^= h >> 15;
        h *= 0x2c1b3c6du;
        h ^= h >> 12;
        h *= 0x297a2d39u;
        h ^= h >> 15;
        state = h | 1u;
    }
    float next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return float(state & 0xffffffu) / float(0xffffff);
    }
};

// How far a plant sinks so its downhill side meets the slope.
float sink_for(Vec3 normal, float slope_sink) {
    return (1.0f - core::saturate(normal.y)) * slope_sink;
}

}  // namespace

size_t Vegetation::tree_count() const {
    size_t total = 0;
    for (const auto& list : trees_) total += list.size();
    return total;
}

size_t Vegetation::rock_count() const {
    size_t n = 0;
    for (const auto& list : rocks_) n += list.size();
    for (const auto& list : kit_rocks_) n += list.size();
    return n;
}

// Rock kinds, as rocks.glb names them: 0 boulder, 1 big boulder, 2 slab,
// 3 crag, 4 scree cluster, 5 outcrop.
void Vegetation::place_rocks(const Terrain& terrain, const VegetationSettings& settings) {
    for (auto& list : rocks_) list.clear();
    for (auto& list : kit_rocks_) list.clear();
    kit_extra_sink_ = 0.0f;
    clearings_.clear();
    landmarks_.clear();
    if (!settings.rocks) return;
    if (settings.rock_kit) {
        place_kit(terrain, settings);
        return;
    }
    const TerrainSettings& t = terrain.settings();
    const float extent = t.half_extent * core::maxf(settings.rock_extent, 0.1f);
    const float spacing = core::maxf(settings.rock_spacing, 8.0f);
    const int cells = int((2.0f * extent) / spacing);
    for (int iz = 0; iz < cells; ++iz) {
        for (int ix = 0; ix < cells; ++ix) {
            CellHash hash(ix, iz, settings.seed * 7u + 3u);
            const float x = -extent + (float(ix) + hash.next()) * spacing;
            const float z = -extent + (float(iz) + hash.next()) * spacing;
            const float height = terrain.height_at(x, z);
            if (height < t.water_level + 1.5f) continue;
            const Vec3 normal = terrain.normal_at(x, z);
            const float slope = 1.0f - normal.y;
            // Steep faces carry the most, the floor a sprinkle; a broad noise
            // gathers them into fields and bare stretches.
            const float field = core::saturate(0.6f + forest_.fbm(x / 300.0f - 40.0f, z / 300.0f + 11.0f, 2));
            const float keep = core::lerpf(settings.rock_floor_cover, settings.rock_slope_cover,
                                           core::smoothstep(0.06f, 0.4f, slope)) * (0.4f + field);
            if (hash.next() > keep) continue;
            const float pick = hash.next();
            int kind = 0;
            float scale = 0.7f + 0.9f * hash.next();
            if (slope > 0.35f) {
                kind = pick < 0.25f ? 5 : pick < 0.5f ? 3 : pick < 0.8f ? 1 : 2;
                if (kind == 5) scale = 0.8f + 1.4f * hash.next();
            } else if (slope > 0.12f) {
                kind = pick < 0.4f ? 4 : pick < 0.7f ? 0 : 1;
            } else {
                kind = pick < 0.6f ? 0 : pick < 0.8f ? 2 : 4;
            }
            PlantInstance rock;
            // Sunk by the slope, so the downhill edge does not float.
            rock.position_scale = core::Vec4{x, height - (0.25f + 3.0f * slope) * scale, z, scale};
            rock.params = core::Vec4{hash.next() * core::TWO_PI, 0.8f + 0.35f * hash.next(),
                                     hash.next() * core::TWO_PI, 0.0f};
            rocks_[kind].push_back(rock);
        }
    }
}

namespace {

// The kit's pieces by role (gfx/rock_piece.h order).
// boulder_a-c are scans; boulder_d-f are Tripo pieces (gfx/rock_piece.h).
constexpr int KIT_BOULDERS[6] = {0, 1, 2, 11, 12, 13};
constexpr int KIT_BOULDER_COUNT = 6;
// slab_a (3) is left out: its scan has a hollow under one side that reads
// as a black cave at any sink (docs/SHOWCASE.md, S6).
constexpr int KIT_SLABS[2] = {4, 28};  // slab_b and the Tripo slab_c
constexpr int KIT_CLIFFS[3] = {5, 6, 7};
// The Tripo crags (a tor, a ridge crest) and the scree cone.
// And since 2026-11-20 a pinnacle, two outcrops and a talus mound.
constexpr int KIT_CRAGS[5] = {8, 9, 24, 25, 26};
constexpr int KIT_CRAG_COUNT = 5;
constexpr int KIT_SCREE[2] = {10, 27};
// The Tripo wall crags, crag_a to crag_e (tools/rockkit.md, "Wall crags").
constexpr int KIT_WALLS[5] = {14, 15, 16, 17, 18};
constexpr int KIT_WALL_COUNT = 5;
// The landmarks (tools/landmarks.md), and the ground each asks for: the
// steepest it stands on (1 - the normal's y) and its height over the valley
// floor, from and to, in metres.
struct LandmarkRule {
    int piece;
    float max_slope;
    float above_from, above_to;
};
constexpr LandmarkRule KIT_LANDMARKS[5] = {
    {19, 0.14f, 25.0f, 280.0f},  // tower_ruin: a bench or a shoulder over the floor
    {20, 0.12f, 2.0f, 160.0f},   // arch_ruin: the floor and the low benches
    {21, 0.07f, 2.0f, 130.0f},   // stone_circle: a flat meadow
    {22, 0.20f, 2.0f, 300.0f},   // monolith
    {23, 0.26f, 50.0f, 340.0f},  // cairn: shoulders and crests
};
constexpr int KIT_LANDMARK_COUNT = 5;

// A piece's radius on the ground at scale 1: half its larger footprint side.
float footprint_radius(int piece) {
    const core::Vec3 size = gfx::rock_piece(piece).size;
    return 0.5f * core::maxf(size.x, size.z);
}

core::Vec4 to_vec4(core::Quat q) { return core::Vec4{q.x, q.y, q.z, q.w}; }

}  // namespace

// Lowers `rock` until every check point of its piece is at least `margin`
// under the ground; returns how far it had to go (0 when it already was).
// A cliff's rim points above its foot (2 m up the piece) may stand up to
// `rim_tolerance` over the ground: its face stands only 2 to 5 m proud of
// its own rim, and burying the whole rim buried the face with it.
static float bury_check_points(const Terrain& terrain, gfx::RockInstance& rock, int piece, float margin,
                               float rim_tolerance = 0.0f) {
    const gfx::RockPieceInfo& info = gfx::rock_piece(piece);
    const core::Quat q{rock.rotation.x, rock.rotation.y, rock.rotation.z, rock.rotation.w};
    const core::Vec3 origin = rock.position_scale.xyz();
    float excess = 0.0f;
    for (int i = 0; i < info.check_count; ++i) {
        const core::Vec3 p = origin + core::rotate(q, info.check[i] * rock.position_scale.w);
        const float allowed = info.cliff && info.check[i].y > 2.0f ? rim_tolerance : -margin;
        excess = core::maxf(excess, p.y - (terrain.height_at(p.x, p.z) + allowed));
    }
    rock.position_scale.y -= excess;
    return excess;
}

// The kit (gfx/rock_piece.h), placed to read as geology. Boulders and slabs
// gather in loose groups -- a large one, a few small ones beside it, more of
// them downhill -- each sunk 15 to 30% of its height and leaning part of
// the way to the slope. Cliff pieces stand only on the steep faces, their
// reference plane laid on the slope averaged over their footprint, their
// face (+Z) out along the fall line, sunk until the rim and the foot are
// under the ground. Every piece is then lowered until its check points are
// all under the terrain, so nothing floats however the ground curves.
void Vegetation::place_kit(const Terrain& terrain, const VegetationSettings& settings) {
    const TerrainSettings& t = terrain.settings();
    const float extent = t.half_extent * core::maxf(settings.rock_extent, 0.1f);
    const float floor = t.valley_floor;
    // Moss and grass on what faces up, near the floor (rock.hlsl): the
    // amount by height, from the floor to a few hundred metres up.
    const auto moss_at = [&](float y, float roll) {
        return (1.0f - core::smoothstep(floor + 60.0f, floor + 320.0f, y)) * (0.45f + 0.55f * roll);
    };
    const auto ground_plane = [&](float x, float z) {
        const core::Vec3 n = terrain.normal_at(x, z);
        const core::Vec3 p{x, terrain.height_at(x, z), z};
        return core::Vec4{n.x, n.y, n.z, core::dot(n, p)};
    };

    struct Footprint {
        float x, z, r;
    };
    std::vector<Footprint> cliffs;
    float cliff_sink_sum = 0.0f;

    // ---- wall crags, first: rows along the steep faces. A crag stands
    // upright, its face (+Z) down the fall line, and sinks until its base
    // outline is under the ground. On a face of 45 degrees and more that
    // puts its back half inside the slope and leaves its face standing its
    // whole height over the ground at its foot, so a row is a cliff band and
    // not rocks set on a slope. Everything else keeps out of the footprints.
    size_t wall_count = 0, wall_rows = 0;
    // Where each wall crag's face meets the ground, and each landmark's
    // base: scree and rubble go there once the pieces that place them exist.
    struct Foot {
        float x, z, radius;
        core::Vec3 downhill;
    };
    std::vector<Foot> wall_feet, landmark_sites;
    if (settings.walls) {
        const float spacing = core::maxf(settings.wall_spacing, 60.0f);
        const int cells = int((2.0f * extent) / spacing);
        for (int iz = 0; iz < cells; ++iz) {
            for (int ix = 0; ix < cells; ++ix) {
                CellHash hash(ix, iz, settings.seed * 23u + 29u);
                const float jx = hash.next();
                const float jz = hash.next();
                const float keep = hash.next();
                const float length_roll = hash.next();
                const float side_roll = hash.next();
                float x = -extent + (float(ix) + 0.15f + 0.7f * jx) * spacing;
                float z = -extent + (float(iz) + 0.15f + 0.7f * jz) * spacing;
                if (keep > settings.wall_cover) continue;
                const int length = 5 + int(length_roll * float(std::max(settings.wall_length - 5, 0)) + 0.5f);
                int misses = 0;
                const float side = side_roll < 0.5f ? -1.0f : 1.0f;
                int last_piece = -1;
                bool row_started = false;
                for (int i = 0; i < length; ++i) {
                    const float pick_roll = hash.next();
                    const float scale_roll = hash.next();
                    const float yaw_roll = hash.next();
                    const float shade_roll = hash.next();
                    const float moss_roll = hash.next();
                    const float id_roll = hash.next();
                    if (std::fabs(x) > extent || std::fabs(z) > extent) break;
                    const float height = terrain.height_at(x, z);
                    const core::Vec3 n = terrain.normal_at(x, z);
                    const float flat = std::sqrt(n.x * n.x + n.z * n.z);
                    if (flat < 0.05f) break;
                    const core::Vec3 downhill{n.x / flat, 0.0f, n.z / flat};
                    const core::Vec3 strike{downhill.z * side, 0.0f, -downhill.x * side};
                    // A gap in the face: step over it, twice at most.
                    if (height < floor + settings.wall_above_floor || height > settings.crag_scree_ceiling ||
                        1.0f - n.y < settings.wall_slope) {
                        if (++misses > 2) break;
                        x += strike.x * 30.0f;
                        z += strike.z * 30.0f;
                        continue;
                    }
                    int pick = int(pick_roll * float(KIT_WALL_COUNT) * 0.999f);
                    if (KIT_WALLS[pick] == last_piece) pick = (pick + 1) % KIT_WALL_COUNT;
                    const int piece = KIT_WALLS[pick];
                    last_piece = piece;
                    const gfx::RockPieceInfo& info = gfx::rock_piece(piece);
                    const float scale = settings.wall_scale * (0.75f + 0.5f * scale_roll);
                    // About the vertical by yaw, +Z goes to (sin, 0, cos).
                    const float yaw = std::atan2(downhill.x, downhill.z) + (yaw_roll - 0.5f) * core::radians(36.0f);
                    gfx::RockInstance rock;
                    rock.position_scale = core::Vec4{x, height, z, scale};
                    const core::Vec3 lean = core::normalize_or(core::lerp(core::Vec3::up(), n, settings.wall_lean), core::Vec3::up());
                    rock.rotation = to_vec4(core::normalize(core::rotation_between(core::Vec3::up(), lean) *
                                                            core::Quat::from_axis_angle(core::Vec3::up(), yaw)));
                    const float extra = bury_check_points(terrain, rock, piece, 0.3f);
                    kit_extra_sink_ = core::maxf(kit_extra_sink_, extra);
                    rock.ground = ground_plane(x, z);
                    rock.params = core::Vec4{0.92f + 0.16f * shade_roll, moss_at(height, moss_roll) * 0.6f, id_roll, 0.0f};
                    if (wall_count == 0)
                        LOG_INFO("rocks: first wall crag %s at (%.0f, %.0f, %.0f), scale %.2f, sunk %.1f m, facing (%.2f, %.2f)",
                                 info.name, double(x), double(height), double(z), double(scale), double(extra),
                                 double(downhill.x), double(downhill.z));
                    kit_rocks_[piece].push_back(rock);
                    cliffs.push_back(Footprint{x, z, footprint_radius(piece) * scale});
                    clearings_.push_back(Clearing{x, z, footprint_radius(piece) * scale * 0.8f});
                    // The base is buried to its downhill edge: the face's foot.
                    const float front = 0.5f * info.size.z * scale;
                    wall_feet.push_back(Foot{x + downhill.x * front, z + downhill.z * front, 0.5f * info.size.x * scale, downhill});
                    ++wall_count;
                    row_started = true;
                    // The next crag: along the contour, overlapping this one.
                    const float step = info.size.x * scale * settings.wall_step;
                    x += strike.x * step;
                    z += strike.z * step;
                }
                if (row_started) ++wall_rows;
            }
        }
    }
    const size_t wall_footprints = cliffs.size();

    // ---- cliff pieces: the boulders keep out of their footprints.
    {
        const float spacing = core::maxf(settings.cliff_spacing, 24.0f);
        const int cells = int((2.0f * extent) / spacing);
        for (int iz = 0; iz < cells; ++iz) {
            for (int ix = 0; ix < cells; ++ix) {
                CellHash hash(ix, iz, settings.seed * 13u + 5u);
                const float jx = hash.next();
                const float jz = hash.next();
                const float x = -extent + (float(ix) + 0.2f + 0.6f * jx) * spacing;
                const float z = -extent + (float(iz) + 0.2f + 0.6f * jz) * spacing;
                const float keep = hash.next();
                // Gathered into bands by the same broad noise as the
                // boulders, so the faces are not dotted on a grid.
                const float field = core::saturate(0.6f + forest_.fbm(x / 260.0f + 17.0f, z / 260.0f - 5.0f, 2));
                if (keep > settings.cliff_cover * (0.2f + 1.2f * field * field)) continue;
                const float height = terrain.height_at(x, z);
                if (height < t.water_level + 4.0f) continue;
                bool in_wall = false;
                for (size_t w = 0; w < wall_footprints; ++w) {
                    const Footprint& f = cliffs[w];
                    if ((x - f.x) * (x - f.x) + (z - f.z) * (z - f.z) < 1.2f * f.r * f.r) in_wall = true;
                }
                const int piece = KIT_CLIFFS[int(hash.next() * 2.999f)];
                const float scale_roll = hash.next();
                const float scale = settings.cliff_scale * (0.8f + 0.4f * scale_roll);
                const float half_width = 0.5f * gfx::rock_piece(piece).size.x * scale;
                // The slope over the footprint: a plane fitted to the
                // ground's heights on a 5 x 5 grid a piece's width across,
                // so a curved face sinks the piece no more than it must.
                double sum_h = 0.0, sum_hx = 0.0, sum_hz = 0.0, sum_xx = 0.0;
                for (int gz = -2; gz <= 2; ++gz) {
                    for (int gx = -2; gx <= 2; ++gx) {
                        const float dx = float(gx) * 0.5f * half_width;
                        const float dz = float(gz) * 0.5f * half_width;
                        const double h = double(terrain.height_at(x + dx, z + dz));
                        sum_h += h;
                        sum_hx += h * double(dx);
                        sum_hz += h * double(dz);
                        sum_xx += double(dx) * double(dx);
                    }
                }
                const float fitted = float(sum_h / 25.0);
                const float gradient_x = float(sum_hx / sum_xx);
                const float gradient_z = float(sum_hz / sum_xx);
                const core::Vec3 n = core::normalize(core::Vec3{-gradient_x, 1.0f, -gradient_z});
                if (1.0f - n.y < settings.cliff_slope || in_wall) continue;
                // A cliff is steep all across, not a steep patch on a shoulder.
                if (1.0f - terrain.normal_at(x, z).y < settings.cliff_slope * 0.8f) continue;
                const float yaw_roll = hash.next();
                const core::Vec3 strike = core::normalize_or(core::cross(core::Vec3::up(), n), core::Vec3::right());
                const core::Vec3 upslope = core::cross(n, strike);
                // The piece's mounting plane (gfx::RockPieceInfo) onto the
                // slope: its frame (x along the plane, up the plane, the
                // plane's normal) to the slope's (strike, up-slope, normal).
                // look_rotation(-n, u) is the rotation whose columns are
                // (cross(u, n), u, n).
                const gfx::RockPieceInfo& info = gfx::rock_piece(piece);
                const core::Vec3 mn = core::normalize(info.mount_normal);
                const core::Vec3 mx = core::normalize(core::Vec3::right() - mn * mn.x);
                const core::Vec3 my = core::cross(mn, mx);
                const core::Quat world = core::look_rotation(-n, upslope);
                const core::Quat piece_frame = core::look_rotation(-mn, my);
                const core::Quat jitter = core::Quat::from_axis_angle(n, (yaw_roll - 0.5f) * core::radians(24.0f));
                const core::Quat q = core::normalize(jitter * world * core::conjugate(piece_frame));
                // The plane's centre on the ground, `cliff_bury` under it;
                // then sunk until every rim and foot point is under the
                // ground, which is as deep as the piece must go and no deeper
                // (its face stands only 2 to 5 m out of its own rim).
                const core::Vec3 site{x, fitted, z};
                const core::Vec3 centre = core::rotate(q, info.mount_centre * scale);
                gfx::RockInstance rock;
                const core::Vec3 origin = site - centre - n * settings.cliff_bury;
                rock.position_scale = core::Vec4{origin.x, origin.y, origin.z, scale};
                rock.rotation = to_vec4(q);
                const float extra = bury_check_points(terrain, rock, piece, 0.15f, settings.cliff_rim_tolerance);
                cliff_sink_sum += extra;
                // A piece that had to sink by more than a third of its
                // height did not fit this ground; leave the face bare.
                if (extra > 0.35f * info.size.y * scale) continue;
                kit_extra_sink_ = core::maxf(kit_extra_sink_, extra);
                // The fitted plane is the ground the piece is set into.
                rock.ground = core::Vec4{n.x, n.y, n.z, core::dot(n, site)};
                const float shade_roll = hash.next();
                const float moss_roll = hash.next();
                const float hash_roll = hash.next();
                rock.params = core::Vec4{0.92f + 0.16f * shade_roll, moss_at(height, moss_roll) * 0.5f, hash_roll, 0.0f};
                kit_rocks_[piece].push_back(rock);
                cliffs.push_back(Footprint{x, z, half_width * 1.1f});
            }
        }
    }

    // ---- crags and scree (the Tripo pieces): upright, turned about the
    // vertical only, sunk a share of their height and then until their base
    // outline is under the ground. Crags stand on shoulders and crests, high
    // over the floor; scree lies where gentler ground meets a steep face
    // above it. Both keep the boulders out of their footprints.
    const auto place_upright = [&](int piece, float x, float z, float scale, float yaw, float sink_share,
                                   float tilt, float shade_roll, float moss_roll, float hash_roll) {
        const gfx::RockPieceInfo& info = gfx::rock_piece(piece);
        const float height = terrain.height_at(x, z);
        const core::Vec3 n = terrain.normal_at(x, z);
        const core::Vec3 up = core::normalize_or(core::lerp(core::Vec3::up(), n, tilt), core::Vec3::up());
        const core::Quat q = core::normalize(core::rotation_between(core::Vec3::up(), up) *
                                             core::Quat::from_axis_angle(core::Vec3::up(), yaw));
        gfx::RockInstance rock;
        rock.position_scale = core::Vec4{x, height - sink_share * info.size.y * scale, z, scale};
        rock.rotation = to_vec4(q);
        const float extra = bury_check_points(terrain, rock, piece, 0.1f);
        // Sunk by more than a third of its height on this ground: leave it out.
        if (extra > 0.35f * info.size.y * scale) return false;
        kit_extra_sink_ = core::maxf(kit_extra_sink_, extra);
        rock.ground = ground_plane(x, z);
        rock.params = core::Vec4{0.92f + 0.16f * shade_roll, moss_at(height, moss_roll), hash_roll, 0.0f};
        // The first of each kind, for a --cam on it.
        if (kit_rocks_[piece].empty())
            LOG_INFO("rocks: first %s at (%.0f, %.0f, %.0f), scale %.2f", info.name, double(x), double(height),
                     double(z), double(scale));
        kit_rocks_[piece].push_back(rock);
        cliffs.push_back(Footprint{x, z, footprint_radius(piece) * scale});
        return true;
    };
    size_t crag_count = 0, scree_count = 0;
    {
        const float crag_spacing = core::maxf(settings.crag_spacing, 30.0f);
        const int crag_cells = int((2.0f * extent) / crag_spacing);
        for (int iz = 0; iz < crag_cells; ++iz) {
            for (int ix = 0; ix < crag_cells; ++ix) {
                CellHash hash(ix, iz, settings.seed * 17u + 9u);
                const float jx = hash.next();
                const float jz = hash.next();
                const float x = -extent + (float(ix) + 0.15f + 0.7f * jx) * crag_spacing;
                const float z = -extent + (float(iz) + 0.15f + 0.7f * jz) * crag_spacing;
                const float keep = hash.next();
                const float pick = hash.next();
                const float scale_roll = hash.next();
                const float yaw = hash.next() * core::TWO_PI;
                const float shade = hash.next();
                const float moss = hash.next();
                const float id = hash.next();
                if (keep > settings.crag_cover) continue;
                const float height = terrain.height_at(x, z);
                if (height < floor + settings.crag_above_floor) continue;
                if (height > settings.crag_scree_ceiling) continue;
                const float slope = 1.0f - terrain.normal_at(x, z).y;
                if (slope < settings.crag_slope_min || slope > settings.crag_slope_max) continue;
                bool clear = true;
                for (const Footprint& f : cliffs) {
                    if ((x - f.x) * (x - f.x) + (z - f.z) * (z - f.z) < 4.0f * f.r * f.r) clear = false;
                }
                if (!clear) continue;
                const int piece = KIT_CRAGS[int(pick * float(KIT_CRAG_COUNT) * 0.999f)];
                const float scale = settings.crag_scale * (0.75f + 0.5f * scale_roll);
                if (place_upright(piece, x, z, scale, yaw, settings.crag_sink, 0.0f, shade, moss, id)) ++crag_count;
            }
        }
        const float scree_spacing = core::maxf(settings.scree_spacing, 20.0f);
        const int scree_cells = int((2.0f * extent) / scree_spacing);
        for (int iz = 0; iz < scree_cells; ++iz) {
            for (int ix = 0; ix < scree_cells; ++ix) {
                CellHash hash(ix, iz, settings.seed * 19u + 13u);
                const float jx = hash.next();
                const float jz = hash.next();
                const float x = -extent + (float(ix) + jx) * scree_spacing;
                const float z = -extent + (float(iz) + jz) * scree_spacing;
                const float keep = hash.next();
                const float scale_roll = hash.next();
                const float yaw = hash.next() * core::TWO_PI;
                const float shade = hash.next();
                const float moss = hash.next();
                const float id = hash.next();
                if (keep > settings.scree_cover) continue;
                const float scree_height = terrain.height_at(x, z);
                if (scree_height < t.water_level + 3.0f || scree_height > settings.crag_scree_ceiling) continue;
                const core::Vec3 n = terrain.normal_at(x, z);
                const float slope = 1.0f - n.y;
                if (slope < 0.04f || slope > 0.3f) continue;
                // The face it fell from: steep ground a few tens of metres uphill.
                const core::Vec2 flat{n.x, n.z};
                const float flat_len = std::sqrt(flat.x * flat.x + flat.y * flat.y);
                if (flat_len < 1e-3f) continue;
                const core::Vec2 uphill = flat * (-1.0f / flat_len);
                bool below_face = false;
                for (float d = 15.0f; d <= 60.0f; d += 15.0f) {
                    const float ux = x + uphill.x * d;
                    const float uz = z + uphill.y * d;
                    if (1.0f - terrain.normal_at(ux, uz).y > settings.cliff_slope * 0.85f) below_face = true;
                }
                if (!below_face) continue;
                bool clear = true;
                for (const Footprint& f : cliffs) {
                    if ((x - f.x) * (x - f.x) + (z - f.z) * (z - f.z) < f.r * f.r) clear = false;
                }
                if (!clear) continue;
                const float scale = settings.scree_scale * (0.7f + 0.6f * scale_roll);
                if (place_upright(KIT_SCREE[id < 0.5f ? 0 : 1], x, z, scale, yaw, settings.scree_sink, 0.8f, shade, moss, id))
                    ++scree_count;
            }
        }
    }

    // ---- scree at the foot of the wall crags: a talus mound or two where
    // each face meets the ground, leaning with the slope. A crag on clean
    // grass was an object set on a hillside (the blind critic's seventh
    // round: "the grass runs right up to them").
    for (size_t i = 0; i < wall_feet.size(); ++i) {
        const Foot& foot = wall_feet[i];
        CellHash hash(int(i), 3, settings.seed * 43u + 7u);
        for (int k = 0; k < 2; ++k) {
            const float keep = hash.next();
            const float along = hash.next();
            const float down = hash.next();
            const float scale_roll = hash.next();
            const float yaw = hash.next() * core::TWO_PI;
            const float shade = hash.next();
            const float moss = hash.next();
            const float id = hash.next();
            if (keep > settings.foot_scree * (k == 0 ? 1.0f : 0.7f)) continue;
            const int piece = KIT_SCREE[id < 0.5f ? 0 : 1];
            const float scale = settings.foot_scree_scale * (0.7f + 0.7f * scale_roll);
            const float reach = footprint_radius(piece) * scale;
            const core::Vec3 strike{foot.downhill.z, 0.0f, -foot.downhill.x};
            const float side = (along - 0.5f) * 1.4f * foot.radius;
            const float out = reach * (0.15f + 0.5f * down);
            const float x = foot.x + strike.x * side + foot.downhill.x * out;
            const float z = foot.z + strike.z * side + foot.downhill.z * out;
            if (terrain.height_at(x, z) < t.water_level + 2.0f) continue;
            if (place_upright(piece, x, z, scale, yaw, 0.3f, 0.85f, shade, moss, id)) ++scree_count;
        }
    }

    // ---- landmarks: one a cell at most, on the first of a few tries in
    // the cell whose ground suits its kind. Upright, sunk until its base is
    // under the ground; trees and boulders keep clear of it.
    size_t landmark_count = 0;
    if (settings.landmarks) {
        const float spacing = core::maxf(settings.landmark_spacing, 150.0f);
        const int cells = int((2.0f * extent) / spacing);
        for (int iz = 0; iz < cells; ++iz) {
            for (int ix = 0; ix < cells; ++ix) {
                CellHash hash(ix, iz, settings.seed * 37u + 41u);
                const float keep = hash.next();
                const float kind_roll = hash.next();
                const float scale_roll = hash.next();
                const float yaw = hash.next() * core::TWO_PI;
                const float shade = hash.next();
                const float moss = hash.next();
                const float id = hash.next();
                if (keep > settings.landmark_cover) continue;
                const LandmarkRule& rule = KIT_LANDMARKS[int(kind_roll * float(KIT_LANDMARK_COUNT) * 0.999f)];
                const float scale = settings.landmark_scale * (0.85f + 0.3f * scale_roll);
                const float radius = footprint_radius(rule.piece) * scale;
                // Most of a cell is mountain: many tries, each two draws.
                for (int attempt = 0; attempt < 60; ++attempt) {
                    const float jx = hash.next();
                    const float jz = hash.next();
                    const float x = -extent + (float(ix) + 0.1f + 0.8f * jx) * spacing;
                    const float z = -extent + (float(iz) + 0.1f + 0.8f * jz) * spacing;
                    const float height = terrain.height_at(x, z);
                    if (height < t.water_level + 3.0f) continue;
                    if (height < floor + rule.above_from || height > floor + rule.above_to) continue;
                    // Gentle under the whole footprint, not at its centre alone.
                    float steepest = 1.0f - terrain.normal_at(x, z).y;
                    for (int corner = 0; corner < 4; ++corner) {
                        const float cx = x + ((corner & 1) ? radius : -radius) * 0.7f;
                        const float cz = z + ((corner & 2) ? radius : -radius) * 0.7f;
                        steepest = core::maxf(steepest, 1.0f - terrain.normal_at(cx, cz).y);
                        if (terrain.height_at(cx, cz) < t.water_level + 1.0f) steepest = 1.0f;
                    }
                    if (steepest > rule.max_slope) continue;
                    bool clear = true;
                    for (const Footprint& f : cliffs) {
                        const float reach = f.r + radius;
                        if ((x - f.x) * (x - f.x) + (z - f.z) * (z - f.z) < reach * reach) clear = false;
                    }
                    if (!clear) continue;
                    if (!place_upright(rule.piece, x, z, scale, yaw, 0.02f, 0.0f, shade, moss * 0.4f, id)) continue;
                    clearings_.push_back(Clearing{x, z, radius * settings.landmark_clearing});
                    landmark_sites.push_back(Foot{x, z, radius, core::Vec3::up()});
                    landmarks_.push_back(Landmark{x, z, radius, rule.piece});
                    ++landmark_count;
                    break;
                }
            }
        }
    }

    // ---- boulder groups.
    const float spacing = core::maxf(settings.rock_group_spacing, 12.0f);
    const int cells = int((2.0f * extent) / spacing);
    const auto in_cliff = [&](float x, float z) {
        for (const Footprint& f : cliffs) {
            if ((x - f.x) * (x - f.x) + (z - f.z) * (z - f.z) < f.r * f.r) return true;
        }
        return false;
    };
    // Rubble lies against a crag's foot and a landmark's base, inside their
    // footprints: it skips the test that keeps the groups out of them.
    bool rubble = false;
    // One rock: oriented, sunk, checked, pushed. `slope_n` the ground's normal.
    const auto place_one = [&](int piece, float x, float z, float scale, float yaw, float lean_roll, float lean_dir,
                               float sink_roll, float shade_roll, float moss_roll, float hash_roll) {
        const float height = terrain.height_at(x, z);
        if (height < t.water_level + 1.0f) return;
        if (!rubble && in_cliff(x, z)) return;
        const core::Vec3 n = terrain.normal_at(x, z);
        const core::Vec3 up = core::normalize_or(core::lerp(core::Vec3::up(), n, settings.rock_tilt), core::Vec3::up());
        // A few degrees of its own lean, so a group is not one tilt.
        const core::Vec3 wobble_axis{std::cos(lean_dir), 0.0f, std::sin(lean_dir)};
        const core::Quat wobble = core::Quat::from_axis_angle(wobble_axis, (lean_roll - 0.5f) * core::radians(14.0f));
        const core::Quat q = core::normalize(wobble * core::rotation_between(core::Vec3::up(), up) *
                                             core::Quat::from_axis_angle(core::Vec3::up(), yaw));
        const gfx::RockPieceInfo& info = gfx::rock_piece(piece);
        const float sink = info.size.y * scale *
                           core::lerpf(settings.rock_sink_min, settings.rock_sink_max, sink_roll);
        gfx::RockInstance rock;
        rock.position_scale = core::Vec4{x, height - sink, z, scale};
        rock.rotation = to_vec4(q);
        const float extra = bury_check_points(terrain, rock, piece, 0.04f * scale + 0.02f);
        // Sunk past 60% of its height to bury the base: on this ground the
        // piece would be a pebble on a bump; leave it out.
        if (sink + extra > 0.6f * info.size.y * scale) return;
        kit_extra_sink_ = core::maxf(kit_extra_sink_, extra);
        rock.ground = ground_plane(x, z);
        rock.params = core::Vec4{0.9f + 0.2f * shade_roll, moss_at(height, moss_roll), hash_roll, 0.0f};
        kit_rocks_[piece].push_back(rock);
    };
    for (int iz = 0; iz < cells; ++iz) {
        for (int ix = 0; ix < cells; ++ix) {
            CellHash hash(ix, iz, settings.seed * 7u + 3u);
            const float jx = hash.next();
            const float jz = hash.next();
            const float x = -extent + (float(ix) + jx) * spacing;
            const float z = -extent + (float(iz) + jz) * spacing;
            const float height = terrain.height_at(x, z);
            if (height < t.water_level + 1.5f) continue;
            const Vec3 normal = terrain.normal_at(x, z);
            const float slope = 1.0f - normal.y;
            const float field = core::saturate(0.6f + forest_.fbm(x / 300.0f - 40.0f, z / 300.0f + 11.0f, 2));
            const float keep = core::lerpf(settings.rock_group_floor_cover, settings.rock_group_slope_cover,
                                           core::smoothstep(0.06f, 0.4f, slope)) * (0.4f + field);
            const float keep_roll = hash.next();
            if (keep_roll > keep) continue;
            // The large one: slabs on the gentler ground, boulders anywhere.
            const float pick = hash.next();
            const bool slab = slope < 0.2f && pick < 0.4f;
            const float which = hash.next();
            const int large = slab ? KIT_SLABS[int(which * 1.999f)] : KIT_BOULDERS[int(which * (float(KIT_BOULDER_COUNT) - 0.001f))];
            const float scale_roll = hash.next();
            const float large_scale = settings.rock_large_scale * (0.75f + 0.6f * scale_roll);
            const float yaw = hash.next() * core::TWO_PI;
            const float lean = hash.next();
            const float lean_dir = hash.next() * core::TWO_PI;
            const float sink = hash.next();
            const float shade = hash.next();
            const float moss = hash.next();
            const float id = hash.next();
            place_one(large, x, z, large_scale, yaw, lean, lean_dir, sink, shade, moss, id);
            // The small ones beside it, more of them downhill where they
            // rolled, on steeper ground more of them.
            const float count_roll = hash.next();
            const int small = int(count_roll * (2.0f * settings.rock_group_size + 1.0f) * (0.7f + slope));
            const core::Vec2 downhill = core::Vec2{normal.x, normal.z} * (1.0f / core::maxf(std::sqrt(normal.x * normal.x + normal.z * normal.z), 1e-3f));
            const float radius = footprint_radius(large) * large_scale;
            for (int i = 0; i < small; ++i) {
                const float angle = hash.next() * core::TWO_PI;
                const float reach = hash.next();
                const float piece_roll = hash.next();
                const int piece = KIT_BOULDERS[int(piece_roll * (float(KIT_BOULDER_COUNT) - 0.001f))];
                const float small_roll = hash.next();
                const float small_scale = large_scale * (0.22f + 0.3f * small_roll);
                const float distance = radius * (0.9f + settings.rock_group_spread * reach) +
                                       footprint_radius(piece) * small_scale;
                core::Vec2 offset{std::cos(angle) * distance, std::sin(angle) * distance};
                // Downhill bias: on a slope the spread leans down the fall line.
                offset = offset + downhill * (distance * 0.8f * core::saturate(slope * 3.0f));
                const float small_yaw = hash.next() * core::TWO_PI;
                const float small_lean = hash.next();
                const float small_dir = hash.next() * core::TWO_PI;
                const float small_sink = hash.next();
                const float small_shade = hash.next();
                const float small_moss = hash.next();
                const float small_id = hash.next();
                place_one(piece, x + offset.x, z + offset.y, small_scale, small_yaw, small_lean, small_dir, small_sink,
                          small_shade, small_moss, small_id);
            }
        }
    }
    // ---- rubble: blocks spilled down the fall line under each wall crag,
    // large near the face and small further out, and small blocks about a
    // landmark's base.
    rubble = true;
    for (size_t i = 0; i < wall_feet.size(); ++i) {
        const Foot& foot = wall_feet[i];
        CellHash hash(int(i), 5, settings.seed * 47u + 11u);
        const core::Vec3 strike{foot.downhill.z, 0.0f, -foot.downhill.x};
        for (int k = 0; k < settings.foot_rubble; ++k) {
            const float along = hash.next();
            const float down = hash.next();
            const float piece_roll = hash.next();
            const float scale_roll = hash.next();
            const float yaw = hash.next() * core::TWO_PI;
            const float lean = hash.next();
            const float lean_dir = hash.next() * core::TWO_PI;
            const float sink = hash.next();
            const float shade = hash.next();
            const float moss = hash.next();
            const float id = hash.next();
            const int piece = KIT_BOULDERS[int(piece_roll * (float(KIT_BOULDER_COUNT) - 0.001f))];
            const float out = down * down * foot.radius * 1.6f;
            // Smaller the further it rolled.
            const float scale = settings.rock_large_scale * (0.5f + 1.3f * scale_roll) * (1.0f - 0.55f * down);
            const float side = (along - 0.5f) * 2.0f * foot.radius;
            place_one(piece, foot.x + strike.x * side + foot.downhill.x * out, foot.z + strike.z * side + foot.downhill.z * out,
                      scale, yaw, lean, lean_dir, sink, shade, moss, id);
        }
    }
    for (size_t i = 0; i < landmark_sites.size(); ++i) {
        const Foot& site = landmark_sites[i];
        CellHash hash(int(i), 9, settings.seed * 53u + 13u);
        for (int k = 0; k < settings.landmark_rubble; ++k) {
            const float angle = hash.next() * core::TWO_PI;
            const float reach = hash.next();
            const float piece_roll = hash.next();
            const float scale_roll = hash.next();
            const float yaw = hash.next() * core::TWO_PI;
            const float lean = hash.next();
            const float lean_dir = hash.next() * core::TWO_PI;
            const float sink = hash.next();
            const float shade = hash.next();
            const float moss = hash.next();
            const float id = hash.next();
            const int piece = KIT_BOULDERS[int(piece_roll * (float(KIT_BOULDER_COUNT) - 0.001f))];
            const float distance = site.radius * (0.5f + 0.7f * reach * reach);
            const float scale = settings.rock_large_scale * (0.3f + 0.6f * scale_roll * scale_roll);
            place_one(piece, site.x + std::cos(angle) * distance, site.z + std::sin(angle) * distance, scale, yaw, lean,
                      lean_dir, sink, shade, moss * 0.5f, id);
        }
    }
    rubble = false;

    size_t total = 0;
    for (const auto& list : kit_rocks_) total += list.size();
    const size_t shells = cliffs.size() - crag_count - scree_count - wall_count - landmark_count;
    LOG_INFO("rocks: %zu kit pieces (%zu cliff pieces, sunk %.2f m on average to bury their rims; %zu crags, %zu scree, "
             "%zu wall crags in %zu rows, %zu landmarks), worst extra sink %.2f m",
             total, shells, double(cliff_sink_sum / float(shells > 0 ? shells : 1)), crag_count, scree_count, wall_count,
             wall_rows, landmark_count, double(kit_extra_sink_));
}

void Vegetation::plant(const Terrain& terrain, const VegetationSettings& settings) {
    for (auto& list : trees_) list.clear();
    forest_ = core::Noise(settings.seed * 31u + 11u);
    place_rocks(terrain, settings);
    if (!settings.trees) return;

    const TerrainSettings& t = terrain.settings();
    const float extent = t.half_extent * core::maxf(settings.skirt_trees, 1.0f) - 30.0f;
    const float spacing = core::maxf(settings.tree_spacing, 3.0f);
    const float floor = t.valley_floor;
    const float treeline = floor + settings.treeline_above_floor;
    const int cells = int((2.0f * extent) / spacing);

    // In a gathered stand's core (stand_contrast) a site can take a second
    // tree, so the stand closes; the sprinkle never asks for one, and the
    // second's draws come after the first's, so the retro tiers' forest is
    // unchanged draw for draw.
    const int sites = settings.stand_contrast > 0.0f ? 2 : 1;
    for (int iz = 0; iz < cells; ++iz) {
        for (int ix = 0; ix < cells; ++ix) {
            CellHash hash(ix, iz, settings.seed);
            for (int site = 0; site < sites; ++site) {
            const float x = -extent + (float(ix) + hash.next()) * spacing;
            const float z = -extent + (float(iz) + hash.next()) * spacing;

            // Two scales of cover: broad stands and clearings, and a finer
            // clumping inside them, so a stand is groves and gaps rather than
            // an even sprinkle. Both read from the air.
            const float stands = forest_.fbm(x / 420.0f, z / 420.0f, 3);
            const float clumps = forest_.fbm(x / 55.0f + 17.0f, z / 55.0f - 9.0f, 2);
            // Past the playable edge the cover thins to the skirt share, over
            // a band wide enough that the edge is not a ruler line.
            const float outside =
                core::saturate((core::maxf(std::fabs(x), std::fabs(z)) - t.half_extent) / 150.0f);
            const float sprinkled = core::saturate((stands + 0.55f) * 0.95f) * core::saturate(0.55f + clumps * 1.4f);
            // Gathered: a stand is closed forest to its edge and a clearing
            // is open, the groves inside a stand thinning but never to bare
            // ground. Seen from the air, the sprinkle read as single trees on
            // a lawn everywhere; the target's forests are masses.
            const float gathered = core::smoothstep(-0.1f, 0.12f, stands) *
                                   core::lerpf(0.45f, 1.6f, core::smoothstep(-0.25f, 0.25f, clumps));
            const float density = core::lerpf(sprinkled, gathered, core::saturate(settings.stand_contrast)) *
                                  settings.forest_cover * 2.0f *
                                  core::lerpf(1.0f, core::saturate(settings.skirt_cover), outside);
            if (hash.next() > density - float(site)) continue;

            const float height = terrain.height_at(x, z);
            if (height < t.water_level + 2.0f || height > treeline) continue;
            const Vec3 normal = terrain.normal_at(x, z);
            if (normal.y < settings.tree_max_slope) continue;
            // Not inside a landmark or a wall crag (the modern tier's kit;
            // the list is empty on the others, and no draw is skipped).
            bool cleared = false;
            for (const Clearing& c : clearings_) {
                if ((x - c.x) * (x - c.x) + (z - c.z) * (z - c.z) < c.r * c.r) cleared = true;
            }
            if (cleared) continue;
            // Thin out toward the treeline instead of stopping at a ruler line.
            const float to_line = (treeline - height) / 90.0f;
            if (hash.next() > core::saturate(to_line + 0.1f)) continue;

            // Which kind. Broadleaves on the warm low ground, spruce through the
            // middle, pines high and on the rougher ground, and a few dead snags
            // in the last stretch below the treeline where nothing is happy.
            // Mixed everywhere, with the mix shifting by altitude: the floor is
            // half broadleaf, the middle is spruce country, the top is pines.
            // A band that is all one kind reads as a plantation.
            const float above = height - floor;
            const float band = core::saturate(above / settings.treeline_above_floor);  // 0 floor, 1 line
            const float p_broadleaf = core::lerpf(0.5f, 0.0f, core::saturate(band * 2.2f));
            const float p_pine = core::lerpf(0.08f, 0.65f, core::saturate((band - 0.3f) * 1.8f));
            const float pick = hash.next();
            TreeKind kind = TreeKind::Spruce;
            if (pick < p_broadleaf) {
                kind = TreeKind::Broadleaf;
            } else if (pick < p_broadleaf + p_pine) {
                kind = TreeKind::Pine;
            }
            if (to_line < 0.9f && hash.next() < 0.35f * (1.0f - to_line)) kind = TreeKind::Dead;
            if (normal.y < 0.86f && hash.next() < 0.12f) kind = TreeKind::Dead;

            PlantInstance tree;
            // Size spread is wide on purpose: an even-aged plantation is the
            // most artificial forest there is.
            float scale = 0.7f + 1.0f * hash.next();
            if (kind == TreeKind::Dead) scale *= 0.85f;
            const float y = height - 0.3f - sink_for(normal, settings.slope_sink);
            tree.position_scale = Vec4{x, y, z, scale};
            // w is the warm push (toward the palette's plant_warm): a forest
            // reads as many trees when its colour varies tree by tree, and
            // broadleaf turns yellow far more than a spruce does.
            const float warm_range = kind == TreeKind::Broadleaf ? 1.0f
                                     : kind == TreeKind::Pine    ? 0.45f
                                                                 : 0.25f;
            tree.params = Vec4{core::TWO_PI * hash.next(), 0.7f + 0.6f * hash.next(),
                               core::TWO_PI * hash.next(), warm_range * hash.next()};
            trees_[int(kind)].push_back(tree);
            }
        }
    }
    LOG_INFO("vegetation: %zu trees (%zu spruce, %zu pine, %zu broadleaf, %zu dead; spacing %.0f m, "
             "treeline %.0f m)",
             tree_count(), trees_[0].size(), trees_[1].size(), trees_[2].size(), trees_[3].size(),
             spacing, treeline);
}

void Vegetation::build_crown_shade(float half, int n, const float crown_radius[gfx::TREE_KINDS]) {
    crown_shade_.assign(size_t(n) * size_t(n), 0);
    crown_shade_n_ = n;
    crown_shade_half_ = half;
    if (n < 2 || half <= 0.0f) return;
    const float texel = 2.0f * half / float(n);
    std::vector<float> open(crown_shade_.size(), 1.0f);  // the share of the sky left
    for (int k = 0; k < gfx::TREE_KINDS; ++k) {
        for (const PlantInstance& tree : trees_[k]) {
            // A little past the crown's edge: sky shade has no hard rim.
            const float radius = crown_radius[k] * tree.position_scale.w * 1.6f;
            if (radius <= 0.0f) continue;
            const float cx = (tree.position_scale.x + half) / texel - 0.5f;
            const float cz = (tree.position_scale.z + half) / texel - 0.5f;
            const int reach = int(std::ceil(radius / texel));
            for (int j = int(cz) - reach; j <= int(cz) + reach + 1; ++j) {
                for (int i = int(cx) - reach; i <= int(cx) + reach + 1; ++i) {
                    if (i < 0 || j < 0 || i >= n || j >= n) continue;
                    const float dx = (float(i) - cx) * texel, dz = (float(j) - cz) * texel;
                    const float d2 = (dx * dx + dz * dz) / (radius * radius);
                    if (d2 >= 1.0f) continue;
                    // Flat across the crown, falling off past it. Squared, it
                    // was dark only at the trunk, which the crown hides from
                    // any view above it.
                    const float w = 1.0f - core::smoothstep(0.3f, 1.0f, d2);
                    open[size_t(j) * size_t(n) + size_t(i)] *= 1.0f - 0.9f * w;
                }
            }
        }
    }
    for (size_t i = 0; i < open.size(); ++i) crown_shade_[i] = uint8_t(std::lrint((1.0f - open[i]) * 255.0f));
}

float Vegetation::crown_shade_at(float x, float z) const {
    const int n = crown_shade_n_;
    if (n < 2 || crown_shade_half_ <= 0.0f) return 0.0f;
    const float texel = 2.0f * crown_shade_half_ / float(n);
    const float fx = (x + crown_shade_half_) / texel - 0.5f;
    const float fz = (z + crown_shade_half_) / texel - 0.5f;
    if (fx < 0.0f || fz < 0.0f || fx >= float(n - 1) || fz >= float(n - 1)) return 0.0f;
    const int i = int(fx), j = int(fz);
    const float u = fx - float(i), v = fz - float(j);
    auto at = [&](int a, int b) { return float(crown_shade_[size_t(b) * size_t(n) + size_t(a)]) / 255.0f; };
    return core::lerpf(core::lerpf(at(i, j), at(i + 1, j), u), core::lerpf(at(i, j + 1), at(i + 1, j + 1), u), v);
}

void Vegetation::grass_around(const Terrain& terrain, const VegetationSettings& settings,
                              Vec3 centre,
                              std::vector<PlantInstance> (&out)[gfx::GRASS_KINDS]) const {
    for (auto& list : out) list.clear();
    if (!settings.grass) return;

    const TerrainSettings& t = terrain.settings();
    const float spacing = core::maxf(settings.grass_spacing, 0.5f);
    const float radius = settings.grass_radius;
    const float ceiling = t.valley_floor + settings.grass_above_floor;
    const int ix0 = int(std::floor((centre.x - radius) / spacing));
    const int ix1 = int(std::ceil((centre.x + radius) / spacing));
    const int iz0 = int(std::floor((centre.z - radius) / spacing));
    const int iz1 = int(std::ceil((centre.z + radius) / spacing));
    const float limit = t.half_extent * core::maxf(settings.skirt_trees, 1.0f) - 10.0f;
    size_t total = 0;

    for (int iz = iz0; iz <= iz1; ++iz) {
        for (int ix = ix0; ix <= ix1; ++ix) {
            if (total >= settings.max_grass) return;
            CellHash hash(ix, iz, settings.seed ^ 0x5bd1e995u);
            // Patchy, not a lawn: a fine noise leaves bare ground between the
            // patches.
            const float x = (float(ix) + hash.next()) * spacing;
            const float z = (float(iz) + hash.next()) * spacing;
            if (std::fabs(x) > limit || std::fabs(z) > limit) continue;
            const float dx = x - centre.x, dz = z - centre.z;
            if (dx * dx + dz * dz > radius * radius) continue;
            const float patch = forest_.fbm(x / 18.0f + 3.0f, z / 18.0f + 5.0f, 2);
            if (hash.next() > core::saturate(0.55f + patch * 1.6f)) continue;

            const float height = terrain.height_at(x, z);
            if (height < t.water_level + 0.5f || height > ceiling) continue;
            const Vec3 normal = terrain.normal_at(x, z);
            if (normal.y < settings.grass_max_slope) continue;

            // Reeds crowd the waterside, bushes are scattered, tufts are the rest.
            const float pick = hash.next();
            GrassKind kind = GrassKind::Tuft;
            // Within 14 m of the water's height, which on the canal's long
            // banks was beside it. A braided river's floor starts 3 m up,
            // and 14 m took in the whole meadow: one plant in nine there was
            // a tall yellow reed. Within 5 m.
            const float near_water =
                core::saturate(1.0f - (height - t.water_level) / (t.river_braid > 0.0f ? 5.0f : 14.0f));
            // A braided river's gravel stands up to two metres over the
            // water, and little grows on it: one plant in five, a reed.
            if (t.river_braid > 0.0f && height < t.water_level + 2.2f && pick > 0.2f) continue;
            if (pick < near_water * 0.8f) {
                kind = GrassKind::Reed;
            } else if (pick > 0.93f) {
                kind = GrassKind::Bush;
            }

            PlantInstance plant;
            const float scale = kind == GrassKind::Bush ? 0.6f + 0.8f * hash.next()
                                                        : 0.75f + 0.7f * hash.next();
            plant.position_scale =
                Vec4{x, height - 0.05f - sink_for(normal, settings.slope_sink * 0.5f), z, scale};
            plant.params = Vec4{core::TWO_PI * hash.next(), 0.75f + 0.5f * hash.next(),
                                core::TWO_PI * hash.next(), 0.5f * hash.next()};
            // Under a crown it has less sky, as the ground under it has.
            plant.params.y *= 1.0f - kCrownShadeOnGrass * crown_shade_at(x, z);
            out[int(kind)].push_back(plant);
            ++total;
        }
    }

    // The sward: one patch a cell on a finer grid of its own, no gaps, where
    // the ground is grass. Its colour follows the ground's: the same patch
    // noise the terrain shader turns its grass dry by (terrain.hlsl,
    // terrain_albedo), as the warm push.
    if (!settings.sward || settings.sward_radius <= 0.0f) return;
    const float step = core::maxf(settings.sward_spacing, 0.8f);
    const float reach = settings.sward_radius;
    // From higher than its reach none of it shows: a dragon in flight pays
    // nothing for it.
    if (centre.y - terrain.height_at(centre.x, centre.z) > reach) return;
    const int sx0 = int(std::floor((centre.x - reach) / step));
    const int sx1 = int(std::ceil((centre.x + reach) / step));
    const int sz0 = int(std::floor((centre.z - reach) / step));
    const int sz1 = int(std::ceil((centre.z + reach) / step));
    std::vector<PlantInstance>& sward = out[int(GrassKind::Sward)];
    for (int iz = sz0; iz <= sz1; ++iz) {
        for (int ix = sx0; ix <= sx1; ++ix) {
            if (sward.size() >= settings.max_sward) return;
            CellHash hash(ix, iz, settings.seed ^ 0x7f4a7c15u);
            const float x = (float(ix) + hash.next()) * step;
            const float z = (float(iz) + hash.next()) * step;
            if (std::fabs(x) > limit || std::fabs(z) > limit) continue;
            const float dx = x - centre.x, dz = z - centre.z;
            if (dx * dx + dz * dz > reach * reach) continue;
            const float height = terrain.height_at(x, z);
            // Above the gravel at the waterline, which is bare.
            if (height < t.water_level + 2.5f || height > ceiling) continue;
            const Vec3 normal = terrain.normal_at(x, z);
            if (normal.y < settings.grass_max_slope + 0.08f) continue;
            const float dry = core::smoothstep(0.35f, 0.75f, gfx::fbm(x * 0.0055f, z * 0.0055f));
            PlantInstance plant;
            const float yaw = core::TWO_PI * hash.next();
            const float scale = 0.85f + 0.4f * hash.next();
            const float phase = core::TWO_PI * hash.next();
            plant.position_scale = Vec4{x, height - 0.03f, z, scale};
            plant.params = Vec4{yaw, (0.95f + 0.2f * dry) * (1.0f - kCrownShadeOnGrass * crown_shade_at(x, z)), phase,
                                0.75f * dry};
            sward.push_back(plant);
        }
    }
}

}  // namespace game
