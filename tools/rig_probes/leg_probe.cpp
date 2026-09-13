// Glide: where do the hind feet end up? usage: leg_probe <model.glb> <cfg>...
#include <cstdio>
#include <string>
#include <cmath>
#include "anim/gltf_loader.h"
#include "anim/skeleton.h"
#include "anim/skinned_mesh.h"
#include "anim/dragon_rig.h"
#include "game/flight.h"
using core::Vec3;
int main(int argc, char** argv) {
    anim::Skeleton s; anim::SkinnedMeshData m;
    if (argc < 3 || !anim::load_skinned_gltf(argv[1], s, m).ok) return 1;
    const anim::DragonJoints j = anim::map_dragon_joints(s);
    float lox=1e9f, hix=-1e9f, loy=1e9f; for (const auto& v : m.vertices) { lox=std::min(lox,v.position.x); hix=std::max(hix,v.position.x); loy=std::min(loy,v.position.y); }
    const float scale = 19.0f / (hix - lox);
    const float fwd_z = (j.head >= 0 && s.world_bind(j.head).translation_part().z < s.world_bind(j.root).translation_part().z) ? -1.f : 1.f;
    for (int a = 2; a < argc; ++a) {
        anim::DragonRig rig; rig.init(s, j); rig.set_model_scale(scale);
        anim::load_rig_tuning(rig.tuning, (std::string(argv[1]) + ".rig.cfg").c_str());
        anim::load_rig_tuning(rig.tuning, argv[a]);
        game::FlightState g; g.velocity = Vec3{0,0,-30}; g.airspeed = 30; g.ground_clearance = 300; g.wing_angle = core::radians(9.0f);
        for (int i = 0; i < 240; ++i) rig.update(g, 1.0f / 60.0f);
        const auto& w = rig.world_matrices();
        auto P = [&](int q) { return w[size_t(q)].col[3].xyz(); };
        auto fwd = [&](Vec3 d) { return core::degrees(std::atan2(d.z * fwd_z, -d.y)); };
        const auto& L = j.leg[0];
        if (L.size() < 2) { std::printf("%s: no hind leg\n", argv[a]); continue; }
        Vec3 hip = P(L[0]), knee = P(L[1]), ankle = L.size() > 2 ? P(L[2]) : knee;
        int foot = -1; for (int f : j.foot_roots) { const std::string n = s.joint(f).name; if (n.find("foot") != std::string::npos && P(f).x * hip.x > 0) foot = f; }
        Vec3 ft = foot >= 0 ? P(foot) : ankle;
        // body belly line: lowest body vertex is not known here; use the root height as reference
        Vec3 root = P(j.root);
        std::printf("%-26s thigh %+4.0f shin %+4.0f | foot vs hip: %+.2f m up, %+.2f m aft | foot vs root %+.2f m up\n",
            argv[a] + std::string(argv[a]).find_last_of('/') + 1,
            fwd(core::normalize(knee - hip)), fwd(core::normalize(ankle - knee)),
            (ft.y - hip.y) * scale, (ft.z - hip.z) * -fwd_z * scale, (ft.y - root.y) * scale);
    }
}
