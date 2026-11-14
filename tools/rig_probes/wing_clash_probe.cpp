// Wing through body: runs a studio scenario to a time, skins the mesh on the
// CPU and counts the wing's vertices that sit inside the body. A vertex is
// "inside" when the nearest body vertex is within reach and the wing vertex
// lies behind that body vertex's surface normal. Prints the share of wing
// vertices inside and the deepest one, in metres. The dive's folded wing is
// what this was written for: a membrane through the flank is plain in a
// render and invisible in a joint angle.
//
//   wing_clash_probe <model.glb> <scenario> <time_s> [override.cfg]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>
#include "anim/dragon_rig.h"
#include "anim/gltf_loader.h"
#include "anim/skeleton.h"
#include "anim/skinned_mesh.h"
#include "game/studio.h"
using core::Vec3;

int main(int argc, char** argv) {
    anim::Skeleton s;
    anim::SkinnedMeshData m;
    if (argc < 4 || !anim::load_skinned_gltf(argv[1], s, m).ok) return 1;
    const anim::DragonJoints j = anim::map_dragon_joints(s);
    if (!j.valid()) return 2;
    float lox = 1e9f, hix = -1e9f;
    for (const auto& v : m.vertices) { lox = std::fmin(lox, v.position.x); hix = std::fmax(hix, v.position.x); }
    const float scale = 19.0f / (hix - lox);
    anim::DragonRig rig;
    rig.init(s, j);
    rig.set_model_scale(scale);
    anim::load_rig_tuning(rig.tuning, (std::string(argv[1]) + ".rig.cfg").c_str());
    if (argc > 4) anim::load_rig_tuning(rig.tuning, argv[4]);
    const auto scenario = game::StudioScenario(std::atoi(argv[2]));
    const float until = float(std::atof(argv[3]));
    const Vec3 centre{0.0f, 200.0f, 0.0f};
    const float dt = 1.0f / 60.0f;
    for (int i = 0; float(i) * dt <= until; ++i) {
        rig.update(game::studio_state(scenario, float(i) * dt, centre, 0.0f, 4.0f), dt);
    }

    // Wing joints, past the shoulder: the shoulder's own skin blends into the
    // back by design.
    std::vector<int> part(size_t(s.count()), 0);  // 0 body, 1 wing, 2 ignore
    for (int side = 0; side < 2; ++side) {
        for (size_t k = 0; k < j.wing_root[side].size(); ++k) part[size_t(j.wing_root[side][k])] = k ? 1 : 2;
        for (const auto& f : j.wing_fingers[side])
            for (int joint : f) part[size_t(joint)] = 1;
    }
    const auto& skin = rig.skinning_matrices();
    std::vector<Vec3> body_p, body_n, wing_p;
    std::vector<int> body_joint;
    for (const auto& v : m.vertices) {
        int best = 0;
        for (int k = 1; k < 4; ++k) if (v.weights[k] > v.weights[best]) best = k;
        const int owner = part[v.joints[best]];
        if (owner == 2) continue;
        Vec3 p{0, 0, 0}, n{0, 0, 0};
        for (int k = 0; k < 4; ++k) {
            if (v.weights[k] <= 0.0f) continue;
            const core::Mat4& mk = skin[v.joints[k]];
            p = p + core::transform_point(mk, v.position) * v.weights[k];
            n = n + (mk * core::Vec4{v.normal, 0.0f}).xyz() * v.weights[k];
        }
        p = p * scale;
        if (owner == 1) { wing_p.push_back(p); continue; }
        body_p.push_back(p);
        body_n.push_back(core::normalize(n));
        body_joint.push_back(v.joints[best]);
    }
    // A spatial hash of the body's vertices, 0.25 m cells.
    const float cell = 0.25f;
    auto key = [&](int x, int y, int z) { return (int64_t(x) * 73856093) ^ (int64_t(y) * 19349663) ^ (int64_t(z) * 83492791); };
    std::unordered_map<int64_t, std::vector<int>> grid;
    for (size_t i = 0; i < body_p.size(); ++i) {
        const Vec3& p = body_p[i];
        grid[key(int(std::floor(p.x / cell)), int(std::floor(p.y / cell)), int(std::floor(p.z / cell)))].push_back(int(i));
    }
    int inside = 0;
    std::unordered_map<int, int> by_joint;  // which body joint the wing passes into
    float deepest = 0.0f;
    for (const Vec3& w : wing_p) {
        const int cx = int(std::floor(w.x / cell)), cy = int(std::floor(w.y / cell)), cz = int(std::floor(w.z / cell));
        float best = 1e9f;
        int nearest = -1;
        for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) for (int dz = -1; dz <= 1; ++dz) {
            auto it = grid.find(key(cx + dx, cy + dy, cz + dz));
            if (it == grid.end()) continue;
            for (int i : it->second) {
                const float d = core::length(w - body_p[size_t(i)]);
                if (d < best && d < 0.5f) { best = d; nearest = i; }  // the hash can collide
            }
        }
        if (nearest < 0) continue;
        const float depth = -core::dot(w - body_p[size_t(nearest)], body_n[size_t(nearest)]);
        if (depth > 0.02f) {
            ++inside;
            deepest = std::fmax(deepest, depth);
            ++by_joint[body_joint[size_t(nearest)]];
        }
    }
    std::printf("%s  t=%.1f  wing verts %zu  inside body %d (%.1f%%)  deepest %.2f m\n", argv[1], double(until),
                wing_p.size(), inside, 100.0 * inside / double(wing_p.size() ? wing_p.size() : 1), double(deepest));
    for (const auto& [joint, count] : by_joint)
        if (count * 20 >= inside) std::printf("    into %s: %d\n", s.joint(joint).name.c_str(), count);
}
