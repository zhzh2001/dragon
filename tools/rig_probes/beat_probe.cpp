// What does the whole body do through one wingbeat? Runs a studio scenario
// through the rig and prints, per frame of the last two beats, the wing tip's
// elevation at the shoulder, and how far the chest, the head and the tail tip
// move from their mean -- in percent of the body's length (nose to tail tip in
// the bind pose). A beat that moves only the wings reads as a rigid body under
// two hinges; this is how much the rest of the animal answers.
// usage: beat_probe <model.glb> <scenario> [cfg [metres_per_unit]]
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
    if (argc < 3 || !anim::load_skinned_gltf(argv[1], s, m).ok) return 1;
    const anim::DragonJoints j = anim::map_dragon_joints(s);
    if (!j.valid() || j.head < 0 || j.tail.empty() || j.chest < 0) return 2;
    anim::DragonRig rig;
    rig.init(s, j);
    // The game's alignment scale for the model (its log: "alignment scale"),
    // metres per unit; the rig's metre-valued dials divide by it.
    rig.set_model_scale(argc > 4 ? float(std::atof(argv[4])) : 13.6773f);
    anim::load_rig_tuning(rig.tuning, (std::string(argv[1]) + ".rig.cfg").c_str());
    if (argc > 3) anim::load_rig_tuning(rig.tuning, argv[3]);
    game::FlightTuning flight;
    const int scenario = std::atoi(argv[2]);

    const Vec3 nose = s.world_bind(j.head).translation_part();
    const Vec3 tip = s.world_bind(j.tail.back()).translation_part();
    const float length = core::length(nose - tip);
    // The wing tip: the last joint of the longest finger on side 0.
    int wing_tip = j.wing_root[0].back();
    size_t longest = 0;
    for (const auto& f : j.wing_fingers[0])
        if (f.size() > longest) { longest = f.size(); wing_tip = f.back(); }
    const int shoulder = j.wing_root[0].front();

    const Vec3 centre{0.0f, 200.0f, 0.0f};
    struct Row { float t, phase, elev; Vec3 chest, head, tail; };
    std::vector<Row> rows;
    const float period = flight.flap_period;
    for (int i = 0; i < 600; ++i) {
        const float t = float(i) / 60.0f;
        game::FlightState st = game::studio_state(game::StudioScenario(scenario), t, centre, 0.0f, 4.0f);
        rig.update(st, 1.0f / 60.0f);
        if (t < 10.0f - 2.0f * period) continue;
        const auto& w = rig.world_matrices();
        auto at = [&](int k) { return w[size_t(k)].col[3].xyz(); };
        const Vec3 d = at(wing_tip) - at(shoulder);
        const float elev = core::degrees(std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z)));
        rows.push_back({t, st.flap_phase, elev, at(j.chest), at(j.head), at(j.tail.back())});
    }
    Vec3 mc{}, mh{}, mt{};
    for (const Row& r : rows) { mc = mc + r.chest; mh = mh + r.head; mt = mt + r.tail; }
    const float n = float(rows.size());
    mc = mc * (1.0f / n); mh = mh * (1.0f / n); mt = mt * (1.0f / n);
    auto pct = [&](float v) { return 100.0f * v / length; };
    std::printf("body length %.2f units, %.1f m; columns in %% of it (x lateral, y up, z along)\n", length, length * (argc > 4 ? float(std::atof(argv[4])) : 13.6773f));
    std::printf("   t  phase  wing_elev | chest y   | head x    y    z  | tail x    y    z\n");
    float rc[2] = {1e9f, -1e9f}, rh[2] = {1e9f, -1e9f}, rt[2] = {1e9f, -1e9f}, rtx[2] = {1e9f, -1e9f};
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        const Vec3 c = r.chest - mc, h = r.head - mh, tl = r.tail - mt;
        rc[0] = std::min(rc[0], c.y); rc[1] = std::max(rc[1], c.y);
        rh[0] = std::min(rh[0], h.y); rh[1] = std::max(rh[1], h.y);
        rt[0] = std::min(rt[0], tl.y); rt[1] = std::max(rt[1], tl.y);
        rtx[0] = std::min(rtx[0], tl.x); rtx[1] = std::max(rtx[1], tl.x);
        if (i % 3 == 0)
            std::printf("%5.2f  %.2f   %+6.1f   | %+6.2f    | %+5.2f %+5.2f %+5.2f | %+5.2f %+5.2f %+5.2f\n", r.t, r.phase, r.elev,
                        pct(c.y), pct(h.x), pct(h.y), pct(h.z), pct(tl.x), pct(tl.y), pct(tl.z));
    }
    std::printf("peak-to-peak, %% of length: chest y %.2f  head y %.2f  tail y %.2f  tail x %.2f\n", pct(rc[1] - rc[0]),
                pct(rh[1] - rh[0]), pct(rt[1] - rt[0]), pct(rtx[1] - rtx[0]));
}
