#include "anim/dragon_rig.h"
#include "anim/gltf_loader.h"
#include "anim/skinned_mesh.h"
#include "game/studio.h"
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc != 3) return 1;
    const std::string model = argv[1];
    anim::Skeleton skeleton;
    anim::SkinnedMeshData mesh;
    if (!anim::load_skinned_gltf(model.c_str(), skeleton, mesh).ok) return 2;
    auto joints = anim::map_dragon_joints(skeleton);
    float lo = 0, hi = 0;
    for (const auto& v : mesh.vertices) {
        lo = std::min(lo, v.position.x);
        hi = std::max(hi, v.position.x);
    }
    game::FlightTuning flight;
    if (!game::load_tuning(flight, (model + ".flight.cfg").c_str())) return 3;
    auto* out = std::fopen(argv[2], "wb");
    if (!out) return 4;
    for (int j = 0; j < skeleton.count(); ++j) {
        std::fprintf(out, "%s ", skeleton.joint(j).name.c_str());
    }
    std::fputc('\n', out);
    for (auto scenario : {game::StudioScenario::Flap, game::StudioScenario::Dive, game::StudioScenario::Grounded}) {
        anim::DragonRig rig;
        rig.init(skeleton, joints);
        rig.set_model_scale(19.f / (hi - lo));
        if (!anim::load_rig_tuning(rig.tuning, (model + ".rig.cfg").c_str())) return 5;
        for (int i = 1; i <= 300; ++i) {
            float t = float(i) / 60.f;
            auto state = game::studio_state(scenario, t, {0, 200, 0}, 0, flight.ground_offset, &flight);
            rig.update(state, 1.f / 60.f);
            const auto& matrices = rig.skinning_matrices();
            std::fwrite(matrices.data(), sizeof(core::Mat4), matrices.size(), out);
        }
    }
    return std::fclose(out);
}
