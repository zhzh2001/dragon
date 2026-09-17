// How much do the hind legs MOVE through a manoeuvre? Runs a studio scenario
// through the rig and prints the range of the thigh's fore-aft and lateral
// angles over the run, plus the foot's travel. usage: leg_swing_probe <model.glb> <scenario> <cfg>...
#include <cstdio>
#include <string>
#include <cmath>
#include "anim/gltf_loader.h"
#include "anim/skeleton.h"
#include "anim/skinned_mesh.h"
#include "anim/dragon_rig.h"
#include "game/flight.h"
#include "game/studio.h"
using core::Vec3;
int main(int argc, char** argv) {
    anim::Skeleton s; anim::SkinnedMeshData m;
    if (argc < 4 || !anim::load_skinned_gltf(argv[1], s, m).ok) return 1;
    const anim::DragonJoints j = anim::map_dragon_joints(s);
    float lox=1e9f, hix=-1e9f; for (const auto& v : m.vertices) { lox=std::min(lox,v.position.x); hix=std::max(hix,v.position.x); }
    const float scale = 19.0f / (hix - lox);
    const float fwd_z = (j.head >= 0 && s.world_bind(j.head).translation_part().z < s.world_bind(j.root).translation_part().z) ? -1.f : 1.f;
    const int scenario = std::atoi(argv[2]);
    for (int a = 3; a < argc; ++a) {
        anim::DragonRig rig; rig.init(s, j); rig.set_model_scale(scale);
        anim::load_rig_tuning(rig.tuning, (std::string(argv[1]) + ".rig.cfg").c_str());
        anim::load_rig_tuning(rig.tuning, argv[a]);
        const Vec3 centre{0.0f, 200.0f, 0.0f};
        float fa_min = 1e9f, fa_max = -1e9f, lat_min = 1e9f, lat_max = -1e9f, foot_travel = 0.0f;
        Vec3 last_foot{}; bool have_last = false;
        const auto& L = j.leg[0];
        for (int i = 0; i < 600; ++i) {
            const float t = float(i) / 60.0f;
            game::FlightState st = game::studio_state(game::StudioScenario(scenario), t, centre, 0.0f, 4.0f);
            rig.update(st, 1.0f / 60.0f);
            if (i < 120 || L.size() < 2) continue;  // let the springs settle
            const auto& w = rig.world_matrices();
            const Vec3 hip = w[size_t(L[0])].col[3].xyz(), knee = w[size_t(L[1])].col[3].xyz();
            const Vec3 d = core::normalize(knee - hip);
            const float fa = core::degrees(std::atan2(d.z * fwd_z, -d.y));   // + forward
            const float lat = core::degrees(std::atan2(d.x * (hip.x >= 0 ? 1.f : -1.f), -d.y));  // + outward
            fa_min = std::min(fa_min, fa); fa_max = std::max(fa_max, fa); lat_min = std::min(lat_min, lat); lat_max = std::max(lat_max, lat);
            const Vec3 foot = w[size_t(L.back())].col[3].xyz();
            if (have_last) foot_travel += core::length(foot - last_foot) * scale;
            last_foot = foot; have_last = true;
        }
        std::printf("%-26s scenario %d: thigh fore-aft %+5.0f..%+5.0f (range %3.0f)  lateral %+5.0f..%+5.0f (range %3.0f)  foot path %.1f m over 8 s\n",
            argv[a] + std::string(argv[a]).find_last_of('/') + 1, scenario, fa_min, fa_max, fa_max - fa_min, lat_min, lat_max, lat_max - lat_min, foot_travel);
    }
}
