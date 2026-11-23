// A rib across the spine: runs the studio's flap for two beats and prints,
// for each rib of each wing, how close its skin comes to the body's centre
// plane (model x = 0), in metres at the engine's scale: the nearest of the
// vertices the rib's joints past its first own, skinned on the CPU. Negative
// is across it.
// The skin and not the joint: a hooked tip reaches well past its last
// joint. A recovery fold that narrows the hand can carry the innermost
// rib's tip over the back, and a render from behind shows it only on the
// frame it happens (storm3, artifacts/storm3/README.md, "Second pass").
//
//   rib_centre_probe <model.glb> [override.cfg]
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

int main(int argc, char** argv) {
    anim::Skeleton s;
    anim::SkinnedMeshData m;
    if (argc < 2 || !anim::load_skinned_gltf(argv[1], s, m).ok) return 1;
    const anim::DragonJoints j = anim::map_dragon_joints(s);
    if (!j.valid()) return 2;
    float lox = 1e9f, hix = -1e9f;
    for (const auto& v : m.vertices) { lox = std::fmin(lox, v.position.x); hix = std::fmax(hix, v.position.x); }
    anim::DragonRig rig;
    rig.init(s, j);
    anim::load_rig_tuning(rig.tuning, (std::string(argv[1]) + ".rig.cfg").c_str());
    if (argc > 2) anim::load_rig_tuning(rig.tuning, argv[2]);
    const float span = rig.tuning.model_span_m > 0.0f ? rig.tuning.model_span_m : 19.0f;
    const float scale = span / (hix - lox);
    rig.set_model_scale(scale);
    // The model's own stroke: the studio builds its beat from the flight
    // tuning, and a profile's angles decide how far the wings rise.
    game::FlightTuning flight;
    game::load_tuning(flight, (std::string(argv[1]) + ".flight.cfg").c_str());
    const core::Vec3 centre{0.0f, 200.0f, 0.0f};
    const float dt = 1.0f / 60.0f;
    std::vector<float> nearest[2];
    std::vector<float> at[2];
    // Which rib owns each joint: side * 16 + rib, or -1.
    std::vector<int> rib_of(size_t(s.count()), -1);
    for (int side = 0; side < 2; ++side) {
        for (size_t f = 0; f < j.wing_fingers[side].size(); ++f) {
            // Past the rib's first bone: that one also carries the membrane
            // at the wing's root, which lies beside the spine in every pose.
            const std::vector<int>& rib = j.wing_fingers[side][f];
            for (size_t k = rib.size() > 1 ? 1 : 0; k < rib.size(); ++k) rib_of[size_t(rib[k])] = side * 16 + int(f);
        }
    }
    struct Owned { size_t vertex; int side; int rib; };
    std::vector<Owned> owned;
    for (size_t v = 0; v < m.vertices.size(); ++v) {
        int best = 0;
        for (int k = 1; k < 4; ++k) if (m.vertices[v].weights[k] > m.vertices[v].weights[best]) best = k;
        const int rib = rib_of[m.vertices[v].joints[best]];
        if (rib >= 0) owned.push_back({v, rib / 16, rib % 16});
    }
    for (int side = 0; side < 2; ++side) {
        nearest[side].assign(j.wing_fingers[side].size(), 1e9f);
        at[side].assign(j.wing_fingers[side].size(), 0.0f);
    }
    for (int i = 0; i < 360; ++i) {
        const float t = float(i) * dt;
        const game::FlightState state = game::studio_state(game::StudioScenario::Flap, t, centre, 0.0f, 4.0f, &flight);
        rig.update(state, dt);
        if (i < 240) continue;  // settle, then two beats
        const auto& skin = rig.skinning_matrices();
        float out[2];
        for (int side = 0; side < 2; ++side) {
            // Which side of the body this wing is bound on.
            out[side] = s.world_bind(j.wing_root[side].back()).translation_part().x >= 0.0f ? 1.0f : -1.0f;
        }
        for (const Owned& o : owned) {
            const auto& v = m.vertices[o.vertex];
            core::Vec3 p{0, 0, 0};
            for (int k = 0; k < 4; ++k) {
                if (v.weights[k] > 0.0f) p = p + core::transform_point(skin[v.joints[k]], v.position) * v.weights[k];
            }
            const float x = p.x * out[o.side] * scale;
            if (x < nearest[o.side][size_t(o.rib)]) {
                nearest[o.side][size_t(o.rib)] = x;
                at[o.side][size_t(o.rib)] = state.flap_phase - std::floor(state.flap_phase);
            }
        }
    }
    for (int side = 0; side < 2; ++side) {
        for (size_t f = 0; f < nearest[side].size(); ++f) {
            std::printf("side %d rib %zu: nearest the centre plane %+.3f m at phase %.2f\n", side, f,
                        double(nearest[side][f]), double(at[side][f]));
        }
    }
    return 0;
}
