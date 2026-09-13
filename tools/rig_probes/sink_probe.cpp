// Grounded stance: skin the mesh on the CPU and report how far its lowest vertex sits
// below the bind-pose floor, in metres -- the ground_lift_m a profile needs.
#include <cstdio>
#include <string>
#include "anim/gltf_loader.h"
#include "anim/skeleton.h"
#include "anim/skinned_mesh.h"
#include "anim/dragon_rig.h"
#include "game/flight.h"
using core::Vec3;
int main(int argc, char** argv) {
    for (int a = 1; a < argc; ++a) {
        anim::Skeleton s; anim::SkinnedMeshData m;
        if (!anim::load_skinned_gltf(argv[a], s, m).ok) { std::printf("%s: load failed\n", argv[a]); continue; }
        const anim::DragonJoints j = anim::map_dragon_joints(s);
        float lox=1e9f, hix=-1e9f, loy=1e9f; for (const auto& v : m.vertices) { lox=std::min(lox,v.position.x); hix=std::max(hix,v.position.x); loy=std::min(loy,v.position.y); }
        const float scale = 19.0f / (hix - lox);
        anim::DragonRig rig; rig.init(s, j); rig.set_model_scale(scale);
        anim::load_rig_tuning(rig.tuning, (std::string(argv[a]) + ".rig.cfg").c_str());
        game::FlightState g; g.grounded = true; g.wing_tuck = 1.0f; g.wing_angle = core::radians(18.0f);
        for (int i = 0; i < 300; ++i) rig.update(g, 1.0f / 60.0f);
        const auto& skin = rig.skinning_matrices();
        float posed_lo = 1e9f; int lowest = -1;
        for (size_t vi = 0; vi < m.vertices.size(); ++vi) {
            const auto& v = m.vertices[vi];
            Vec3 p{0,0,0};
            for (int k = 0; k < 4; ++k) { if (v.weights[k] <= 0.f) continue; p = p + core::transform_point(skin[v.joints[k]], v.position) * v.weights[k]; }
            if (p.y < posed_lo) { posed_lo = p.y; lowest = int(vi); }
        }
        // Which joint owns the lowest vertex?
        std::string owner = "?"; if (lowest >= 0) { const auto& v = m.vertices[size_t(lowest)]; int best = 0; for (int k = 1; k < 4; ++k) if (v.weights[k] > v.weights[best]) best = k; owner = s.joint(v.joints[best]).name; }
        std::printf("%-34s bind floor y %.3f, posed lowest y %.3f -> sinks %.2f m (lowest vertex on %s); current ground_lift_m %.2f\n", argv[a], loy, posed_lo, (loy - posed_lo) * scale, owner.c_str(), rig.tuning.ground_lift_m);
    }
}
