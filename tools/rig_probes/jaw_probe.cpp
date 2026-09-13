// For each model: grounded, breath 0 vs 1 -- does the jaw tip drop in the head frame?
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
        if (j.jaw < 0 || j.head < 0) { std::printf("%s: no jaw\n", argv[a]); continue; }
        int tip = j.jaw; for (int i = 0; i < s.count(); ++i) if (s.joint(i).parent == j.jaw) tip = i;
        for (int grounded = 0; grounded < 2; ++grounded) {
            anim::DragonRig rig; rig.init(s, j);
            anim::load_rig_tuning(rig.tuning, (std::string(argv[a]) + ".rig.cfg").c_str());
            rig.set_model_scale(15.6f);
            game::FlightState st;
            if (grounded) { st.grounded = true; st.wing_tuck = 1.0f; } else { st.velocity = Vec3{0,0,-30}; st.airspeed = 30; st.ground_clearance = 300; }
            auto tip_in_head = [&](float breath) {
                anim::RigAction act; act.breath = breath;
                for (int i = 0; i < 240; ++i) { rig.set_action(act); rig.update(st, 1.0f/60.0f); }
                const auto& w = rig.world_matrices();
                const core::Quat head = core::quat_from_matrix(w[size_t(j.head)]);
                return core::rotate(core::conjugate(head), w[size_t(tip)].col[3].xyz() - w[size_t(j.jaw)].col[3].xyz());
            };
            const Vec3 rest = tip_in_head(0.0f);
            const core::Quat rest_local = rig.pose().local[size_t(j.jaw)].rotation;
            const Vec3 open = tip_in_head(1.0f);
            const core::Quat open_local = rig.pose().local[size_t(j.jaw)].rotation;
            const core::Quat bind_local = s.joint(j.jaw).local_bind.rotation;
            auto ang = [](core::Quat a_, core::Quat b_){ return core::degrees(2.0f*std::acos(std::min(1.0f, std::fabs(core::dot(a_,b_))))); };
            std::printf("   jaw local angle from bind: rest %.1f deg, open %.1f deg; jaw open %.2f\n", ang(rest_local,bind_local), ang(open_local,bind_local), rig.jaw_open());
            const Vec3 bind_tip = core::rotate(core::conjugate(core::quat_from_matrix(s.world_bind(j.head))), s.world_bind(tip).translation_part() - s.world_bind(j.jaw).translation_part());
            std::printf("%-36s %s: tip.y bind %+.3f rest %+.3f open %+.3f  -> opening %s by %.3f\n", argv[a], grounded ? "GROUND" : "flight", bind_tip.y, rest.y, open.y, open.y < rest.y ? "DOWN" : "UP", std::fabs(open.y - rest.y));
        }
    }
}
