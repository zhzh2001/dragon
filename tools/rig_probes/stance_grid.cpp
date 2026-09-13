// usage: stance_grid <model.glb> <cfg>... : one summary line per cfg, same loaded mesh.
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
    float lox=1e9f, hix=-1e9f; for (const auto& v : m.vertices) { lox=std::min(lox,v.position.x); hix=std::max(hix,v.position.x); }
    const float scale = 19.0f / (hix - lox);
    const float fwd_z = (j.head >= 0 && s.world_bind(j.head).translation_part().z < s.world_bind(j.root).translation_part().z) ? -1.f : 1.f;
    for (int a = 2; a < argc; ++a) {
        anim::DragonRig rig; rig.init(s, j); rig.set_model_scale(scale);
        anim::load_rig_tuning(rig.tuning, (std::string(argv[1]) + ".rig.cfg").c_str());
        anim::load_rig_tuning(rig.tuning, argv[a]);
        game::FlightState g; g.grounded = true; g.wing_tuck = 1.0f; g.wing_angle = core::radians(18.0f);
        for (int i = 0; i < 300; ++i) rig.update(g, 1.0f / 60.0f);
        const auto& w = rig.world_matrices();
        auto y = [&](int q) { return w[size_t(q)].col[3].y; };
        auto dir = [&](int a_, int b_) { return core::normalize(w[size_t(b_)].col[3].xyz() - w[size_t(a_)].col[3].xyz()); };
        // degrees forward of straight down, forward being the model's facing
        auto fwd = [&](Vec3 d) { return core::degrees(std::atan2(d.z * fwd_z, -d.y)); };
        float hand = 0, foot = 0; int nh = 0, nf = 0;
        for (int f : j.foot_roots) { const std::string n = s.joint(f).name; if (n.find("hand") != std::string::npos) { hand += y(f); ++nh; } else { foot += y(f); ++nf; } }
        // A wyvern's hands are its wing wrists.
        if (nh == 0) for (int side = 0; side < 2; ++side) if (j.wing_root[side].size() >= 2) { hand += y(j.wing_root[side].back()); ++nh; }
        if (nh) hand /= nh; if (nf) foot /= nf;
        // Where the planted wrists sit relative to the hind feet: ahead (+) and outboard (+), metres.
        float ahead = 0.f, wide = 0.f;
        if (j.wing_root[0].size() >= 2 && !j.foot_roots.empty()) {
            Vec3 wr = w[size_t(j.wing_root[0].back())].col[3].xyz();
            Vec3 ft{0,0,0}; for (int f : j.foot_roots) ft = ft + w[size_t(f)].col[3].xyz(); ft = ft * (1.0f / float(j.foot_roots.size()));
            float fx = 0.f; for (int f : j.foot_roots) fx += std::fabs(w[size_t(f)].col[3].x); fx /= float(j.foot_roots.size());
            ahead = (wr.z - ft.z) * fwd_z * scale; wide = (std::fabs(wr.x) - fx) * scale;
        }
        const auto& L = j.leg[0]; const auto& F = j.front_leg[0];
        const Vec3 sp = dir(j.root, j.chest);
        std::printf("%-28s hind-hand %+.2f m | thigh %+.0f shin %+.0f | uarm %+.0f farm %+.0f | spine %+.0f | wrist ahead %+.1f wide %+.1f\n",
            argv[a] + std::string(argv[a]).find_last_of('/') + 1, (foot - hand) * scale,
            L.size() > 1 ? fwd(dir(L[0], L[1])) : 0.f, L.size() > 2 ? fwd(dir(L[1], L[2])) : 0.f,
            F.size() > 1 ? fwd(dir(F[0], F[1])) : 0.f, F.size() > 2 ? fwd(dir(F[1], F[2])) : 0.f,
            core::degrees(std::atan2(sp.y, std::fabs(sp.z))), ahead, wide);
    }
    return 0;
}
