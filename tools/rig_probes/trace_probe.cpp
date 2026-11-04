// Joint paths for arc and spacing charts. Runs a studio scenario through the
// rig the way the app does -- the studio's flight state, its scripted attacks
// and its aim target -- and writes one CSV row per frame: time, the beat's
// phase, the action that fired, and the body-frame position in metres of the
// root, the chest, each neck joint, the head, the tail's middle and tip, the
// right hind leg's joints, the right foreleg's joints and the right wing tip.
// tools/rig_probes/plot_trace.py draws them. Animators read motion this way
// (arcs and spacing): even dots are linear, bunched dots are an ease, a jump
// between two dots is a pop, and parts that peak on the same frame move in
// lockstep where they should overlap.
// usage: trace_probe <model.glb> <scenario> <from_s> <to_s> [cfg [metres_per_unit]] > out.csv
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "anim/dragon_rig.h"
#include "anim/gltf_loader.h"
#include "anim/skeleton.h"
#include "anim/skinned_mesh.h"
#include "game/flight.h"
#include "game/studio.h"
using core::Vec3;

int main(int argc, char** argv) {
    anim::Skeleton s;
    anim::SkinnedMeshData m;
    if (argc < 5 || !anim::load_skinned_gltf(argv[1], s, m).ok) return 1;
    const anim::DragonJoints j = anim::map_dragon_joints(s);
    if (!j.valid()) return 2;
    const float scale = argc > 6 ? float(std::atof(argv[6])) : 13.6773f;
    anim::DragonRig rig;
    rig.init(s, j);
    rig.set_model_scale(scale);
    anim::load_rig_tuning(rig.tuning, (std::string(argv[1]) + ".rig.cfg").c_str());
    if (argc > 5 && argv[5][0] != '-') anim::load_rig_tuning(rig.tuning, argv[5]);
    const auto scenario = game::StudioScenario(std::atoi(argv[2]));
    const float from = float(std::atof(argv[3])), to = float(std::atof(argv[4]));

    std::vector<std::pair<std::string, int>> track{{"root", j.root}, {"chest", j.chest}};
    for (size_t i = 0; i < j.neck.size(); ++i) track.push_back({"neck" + std::to_string(i), j.neck[i]});
    track.push_back({"head", j.head});
    if (!j.tail.empty()) {
        track.push_back({"tail_mid", j.tail[j.tail.size() / 2]});
        track.push_back({"tail_tip", j.tail.back()});
    }
    for (size_t i = 0; i < j.leg[0].size(); ++i) track.push_back({"leg" + std::to_string(i), j.leg[0][i]});
    for (size_t i = 0; i < j.front_leg[0].size(); ++i)
        track.push_back({"arm" + std::to_string(i), j.front_leg[0][i]});
    int wing_tip = j.wing_root[0].back();
    size_t longest = 0;
    for (const auto& f : j.wing_fingers[0])
        if (f.size() > longest) { longest = f.size(); wing_tip = f.back(); }
    track.push_back({"wing_tip", wing_tip});

    std::printf("t,phase,action");
    for (const auto& [name, joint] : track) std::printf(",%s_x,%s_y,%s_z", name.c_str(), name.c_str(), name.c_str());
    std::printf("\n");
    const Vec3 centre{0.0f, 200.0f, 0.0f};
    const float dt = 1.0f / 60.0f;
    float previous = 0.0f;
    for (int i = 0; float(i) * dt <= to; ++i) {
        const float t = float(i) * dt;
        const game::FlightState st = game::studio_state(scenario, t, centre, 0.0f, 4.0f);
        if (scenario == game::StudioScenario::Attack) {
            rig.set_aim_target(game::studio_attack_target(t, centre));
        } else if (scenario == game::StudioScenario::Melee || scenario == game::StudioScenario::Claw ||
                   scenario == game::StudioScenario::Tail) {
            rig.set_aim_target(game::studio_melee_target(t, centre));
        }
        const anim::RigAction action = game::studio_action(scenario, previous, t);
        rig.set_action(action);
        previous = t;
        rig.update(st, dt);
        if (t < from) continue;
        const char* fired = action.bite ? "bite" : action.claw ? "claw" : action.tail ? "tail" : action.fire ? "fire" : "";
        std::printf("%.4f,%.3f,%s", t, st.flap_phase, fired);
        const auto& w = rig.world_matrices();
        for (const auto& [name, joint] : track) {
            const Vec3 p = joint >= 0 ? w[size_t(joint)].col[3].xyz() * scale : Vec3::zero();
            std::printf(",%.4f,%.4f,%.4f", p.x, p.y, p.z);
        }
        std::printf("\n");
    }
}
