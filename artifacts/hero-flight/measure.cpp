// Sample the final studio pose at the engine's 60 Hz update rate.
// Pass a clip path or a dash, then the effort bias and three source tip offsets.
#include "anim/dragon_rig.h"
#include "anim/gltf_loader.h"
#include "anim/skinned_mesh.h"
#include "game/studio.h"

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    if (argc != 12) return 1;
    anim::Skeleton skeleton;
    anim::SkinnedMeshData mesh;
    const std::string model = "assets/embercrest-hero.glb";
    if (!anim::load_skinned_gltf(model.c_str(), skeleton, mesh).ok) return 1;
    auto joints = anim::map_dragon_joints(skeleton);
    anim::DragonRig rig;
    rig.init(skeleton, joints);
    const float scale = 1.0004f;
    rig.set_model_scale(scale);
    if (!anim::load_rig_tuning(rig.tuning, (model + ".rig.cfg").c_str())) return 1;
    game::FlightTuning flight;
    if (!game::load_tuning(flight, (model + ".flight.cfg").c_str())) return 1;
    flight.flap_effort_bias = std::atof(argv[2]);
    std::vector<anim::AnimationClip> clips;
    if (std::string(argv[1]) != "-") {
        clips = anim::load_clip_file(argv[1], skeleton);
        if (clips.size() != 2 || clips[0].name != "flap" || clips[1].name != "flap_climb") return 1;
        rig.set_flight_clips(&clips[0], &clips[1]);
    }
    core::Vec3 offset[3];
    for (int i = 0; i < 3; ++i) {
        offset[i] = {float(std::atof(argv[3 + i*3])), float(std::atof(argv[4 + i*3])),
                     float(std::atof(argv[5 + i*3]))};
    }
    const char* ends[] = {"1c","2b","3b"};
    std::printf("frame,phase,arm_deg,wrist_y,wrist_forward,tip1_y,tip2_y,tip3_y,span_m\n");
    constexpr float dt = 1.0f / 60;
    for (int frame = 1; frame <= 255; ++frame) {
        auto state = game::studio_state(game::StudioScenario::Flap, frame*dt, {0,200,0},
                                        0, flight.ground_offset, &flight);
        rig.update(state, dt);
        if (frame < 204) continue;
        const auto& world = rig.world_matrices();
        auto at = [&](const std::string& name) { return world[skeleton.find("wing_" + name)]; };
        const core::Vec3 root = at("root_r").translation_part();
        const core::Vec3 arm = at("arm_r").translation_part() - root;
        const core::Vec3 wrist = at("wrist_r").translation_part() - root;
        std::printf("%d,%.6f,%.5f,%.5f,%.5f",frame,(frame-204)/51.0,
                    core::degrees(std::atan2(arm.y,arm.x)),wrist.y*scale,-wrist.z*scale);
        float span = 0;
        for (int i = 0; i < 3; ++i) {
            const std::string finger = std::string("finger_") + ends[i];
            core::Vec3 right = core::transform_point(at(finger + "_r"), offset[i]);
            std::printf(",%.5f",(right.y-root.y)*scale);
            if (i == 0) {
                auto left_offset = offset[i];
                left_offset.x *= -1;
                span = (right.x-core::transform_point(at(finger + "_l"),left_offset).x)*scale;
            }
        }
        std::printf(",%.5f\n",span);
    }
}
