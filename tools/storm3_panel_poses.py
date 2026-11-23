"""Sample Storm3's skinning matrices with the studio rig at 60 Hz."""
from pathlib import Path
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / 'build-rig-cache'
DEPS = Path('~/src/game-claude/build/_deps')
PROBE = r'''
#include <cstdio>
#include <cstdlib>
#include <string>
#include "anim/dragon_rig.h"
#include "anim/gltf_loader.h"
#include "anim/skinned_mesh.h"
#include "game/studio.h"

int main(int argc, char** argv) {
    if (argc != 5) return 1;
    const auto scenario = game::StudioScenario(std::atoi(argv[1]));
    const int frames = std::atoi(argv[2]);
    anim::Skeleton skeleton;
    anim::SkinnedMeshData mesh;
    const auto loaded = anim::load_skinned_gltf("assets/storm3.glb", skeleton, mesh);
    if (!loaded.ok) return 1;
    anim::DragonRig rig;
    rig.init(skeleton, anim::map_dragon_joints(skeleton));
    anim::load_rig_tuning(rig.tuning, "assets/storm3.glb.rig.cfg");
    rig.set_model_scale(rig.tuning.model_span_m / (loaded.bounds_max.x - loaded.bounds_min.x));
    const auto clips = anim::load_clip_file("assets/clips/storm3.flight.glb", skeleton);
    const anim::AnimationClip *cruise = nullptr, *climb = nullptr;
    for (const auto& clip : clips) {
        if (clip.name == "flap") cruise = &clip;
        if (clip.name == "flap_climb") climb = &clip;
    }
    if (!cruise || !climb) return 2;
    if (std::string(argv[3]) == "clip") rig.set_flight_clips(cruise, climb);
    game::FlightTuning flight;
    game::load_tuning(flight, "assets/storm3.glb.flight.cfg");
    auto names = std::fopen("build-rig-cache/joints.txt", "w");
    auto output = std::fopen(argv[4], "wb");
    if (!names || !output) return 3;
    for (int i = 0; i < skeleton.count(); ++i) {
        std::fprintf(names, "%s\n", skeleton.joint(i).name.c_str());
    }
    std::fclose(names);
    const float dt = 1.0f / 60.0f;
    float t = 0.0f;
    for (int frame = 1; frame <= frames; ++frame) {
        const float previous = t;
        t += dt;
        const auto state = game::studio_state(scenario, t, {0, 200, 0}, 0, flight.ground_offset, &flight);
        rig.set_action(game::studio_action(scenario, previous, t));
        rig.update(state, dt);
        const auto& matrices = rig.skinning_matrices();
        if (std::fwrite(matrices.data(), sizeof(core::Mat4), matrices.size(), output) != matrices.size()) return 4;
    }
    return std::fclose(output) != 0;
}
'''


def sample():
    CACHE.mkdir(exist_ok=True)
    source = CACHE / 'panel_poses.cpp'
    binary = CACHE / 'panel_poses'
    source.write_text(PROBE)
    block = (ROOT / 'CMakeLists.txt').read_text().split('add_executable(test_anim', 1)[1].split(')', 1)[0]
    sources = re.findall(r'src/[a-z_/]+\.cpp', block)
    subprocess.run(['clang++', '-std=c++20', '-O2', '-Isrc', '-I/opt/homebrew/include',
                    '-I' + str(DEPS / 'cgltf-src'), '-I' + str(DEPS / 'stb-src'),
                    str(source), *sources, '-L/opt/homebrew/lib', '-lSDL3', '-o', str(binary)],
                   cwd=ROOT, check=True, env={**os.environ, 'TMPDIR': str(CACHE)})
    for name, scene, frames in [('beat', 1, 254), ('procedural', 1, 254), ('stand', 9, 200),
                                ('glide', 0, 200), ('dive', 5, 300), ('turn', 2, 260)]:
        motion = 'procedural' if name == 'procedural' else 'clip'
        subprocess.run([str(binary), str(scene), str(frames), motion, str(CACHE / (name + '.bin'))],
                       cwd=ROOT, check=True)


if __name__ == '__main__':
    sample()
