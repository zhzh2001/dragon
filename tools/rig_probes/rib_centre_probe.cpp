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
// Then the other end of the stroke: how far the wing's skin hangs below the
// belly's lowest point, and how near the centre plane it comes while below
// it. A wing that hangs metres under the body covers the legs from the side
// and reads as "too low"; one near the centre plane there has wrapped under
// the belly (feedback 0035, tide2).
//
// A keyframed wingbeat is played when the model has one
// (`<dir>/clips/<stem>.flight.glb`), or the file `--clip` names; `--no-clip`
// is the procedural beat. `--climb` is the deeper beat (effort 1).
//
//   rib_centre_probe <model.glb> [--clip file.glb | --no-clip] [--climb] [override.cfg]
#include <cmath>
#include <cstdio>
#include <cstring>
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
    anim::load_rig_tuning(rig.tuning, anim::profile_path(argv[1], ".rig.cfg").c_str());
    const std::string model = argv[1];
    const size_t slash = model.find_last_of('/');
    const std::string dir = slash == std::string::npos ? std::string(".") : model.substr(0, slash);
    std::string stem = slash == std::string::npos ? model : model.substr(slash + 1);
    if (stem.size() > 4) stem.resize(stem.size() - 4);
    std::string clip_path = dir + "/clips/" + stem + ".flight.glb";
    bool climb = false;
    for (int a = 2; a < argc; ++a) {
        if (!std::strcmp(argv[a], "--clip") && a + 1 < argc) clip_path = argv[++a];
        else if (!std::strcmp(argv[a], "--no-clip")) clip_path.clear();
        else if (!std::strcmp(argv[a], "--climb")) climb = true;
        else anim::load_rig_tuning(rig.tuning, argv[a]);
    }
    std::vector<anim::AnimationClip> clips;
    if (!clip_path.empty()) clips = anim::load_clip_file(clip_path.c_str(), s);
    const anim::AnimationClip* beat[2] = {nullptr, nullptr};
    for (const anim::AnimationClip& c : clips) {
        if (c.name == "flap") beat[0] = &c;
        if (c.name == "flap_climb") beat[1] = &c;
    }
    rig.set_flight_clips(beat[0], beat[1]);
    std::printf("beat: %s%s\n", rig.has_flight_clips() ? "keyframed, " : "procedural",
                rig.has_flight_clips() ? clip_path.c_str() : "");
    const float span = rig.tuning.model_span_m > 0.0f ? rig.tuning.model_span_m : 19.0f;
    const float scale = span / (hix - lox);
    rig.set_model_scale(scale);
    // The model's own stroke: the studio builds its beat from the flight
    // tuning, and a profile's angles decide how far the wings rise.
    game::FlightTuning flight;
    game::load_tuning(flight, anim::profile_path(argv[1], ".flight.cfg").c_str());
    if (climb) flight.flap_effort_bias = 1.0f;
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
    // What each joint is part of: 1 a wing (side in bit 4), 2 a limb, the
    // neck, the head or the tail, 0 the trunk. A child takes its parent's.
    std::vector<int> part(size_t(s.count()), 0);
    for (int side = 0; side < 2; ++side) {
        for (int w : j.wing_root[side]) part[size_t(w)] = 1 + side * 16;
        for (const std::vector<int>& rib : j.wing_fingers[side]) for (int w : rib) part[size_t(w)] = 1 + side * 16;
        for (int l : j.leg[side]) part[size_t(l)] = 2;
        for (int l : j.front_leg[side]) part[size_t(l)] = 2;
    }
    for (int f : j.foot_roots) part[size_t(f)] = 2;
    for (int n : j.neck) part[size_t(n)] = 2;
    for (int t : j.tail) part[size_t(t)] = 2;
    if (j.head >= 0) part[size_t(j.head)] = 2;
    for (int i = 0; i < s.count(); ++i) {
        const int parent = s.joint(i).parent;
        if (part[size_t(i)] == 0 && parent >= 0) part[size_t(i)] = part[size_t(parent)];
    }
    struct Owned { size_t vertex; int side; int rib; };
    std::vector<Owned> owned;
    std::vector<Owned> wing;
    std::vector<size_t> trunk;
    for (size_t v = 0; v < m.vertices.size(); ++v) {
        int best = 0;
        for (int k = 1; k < 4; ++k) if (m.vertices[v].weights[k] > m.vertices[v].weights[best]) best = k;
        const int rib = rib_of[m.vertices[v].joints[best]];
        if (rib >= 0) owned.push_back({v, rib / 16, rib % 16});
        const int p = part[m.vertices[v].joints[best]];
        if (p == 0) trunk.push_back(v);
        if ((p & 15) == 1) wing.push_back({v, p / 16, 0});
    }
    float lowest = 1e9f, lowest_at = 0.0f, under = 1e9f, under_at = 0.0f;
    float highest = -1e9f, highest_at = 0.0f;
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
        const auto skinned = [&](size_t index) {
            const auto& v = m.vertices[index];
            core::Vec3 p{0, 0, 0};
            for (int k = 0; k < 4; ++k) {
                if (v.weights[k] > 0.0f) p = p + core::transform_point(skin[v.joints[k]], v.position) * v.weights[k];
            }
            return p;
        };
        const float phase = state.flap_phase - std::floor(state.flap_phase);
        float belly = 1e9f, back = -1e9f;
        for (size_t v : trunk) {
            const float y = skinned(v).y;
            belly = std::fmin(belly, y);
            back = std::fmax(back, y);
        }
        for (const Owned& o : wing) {
            const core::Vec3 p = skinned(o.vertex);
            const float below = (p.y - belly) * scale;
            if (below < lowest) lowest = below, lowest_at = phase;
            const float above = (p.y - back) * scale;
            if (above > highest) highest = above, highest_at = phase;
            // Under the belly: how near the centre plane, of what hangs
            // more than half a metre below it.
            const float x = p.x * out[o.side] * scale;
            if (below < -0.5f && x < under) under = x, under_at = phase;
        }
        for (const Owned& o : owned) {
            const core::Vec3 p = skinned(o.vertex);
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
    std::printf("wing's highest skin: %+.2f m above the back at phase %.2f\n", double(highest), double(highest_at));
    std::printf("wing's lowest skin: %+.2f m from the belly at phase %.2f (negative is below it)\n", double(lowest),
                double(lowest_at));
    if (under < 1e8f) {
        std::printf("under the belly: nearest the centre plane %+.2f m at phase %.2f\n", double(under), double(under_at));
    } else {
        std::printf("under the belly: no wing skin more than 0.5 m below it\n");
    }
    return 0;
}
