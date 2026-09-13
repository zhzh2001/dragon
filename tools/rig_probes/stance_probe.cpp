// Runs a species' rig grounded and prints where its feet and wing bones end up.
// usage: stance_probe <model.glb> [override.cfg]
#include <cstdio>
#include <string>
#include "anim/gltf_loader.h"
#include "anim/skeleton.h"
#include "anim/skinned_mesh.h"
#include "anim/dragon_rig.h"
#include "game/flight.h"
using core::Vec3;
int main(int argc, char** argv) {
    if (argc < 2) return 1;
    anim::Skeleton s; anim::SkinnedMeshData m;
    if (!anim::load_skinned_gltf(argv[1], s, m).ok) { std::printf("load failed\n"); return 1; }
    const anim::DragonJoints j = anim::map_dragon_joints(s);
    anim::DragonRig rig; rig.init(s, j);
    anim::load_rig_tuning(rig.tuning, (std::string(argv[1]) + ".rig.cfg").c_str());
    if (argc > 2) anim::load_rig_tuning(rig.tuning, argv[2]);
    // Wingspan-normalised scale, as the app does it: 19 m over the mesh x extent.
    Vec3 lo{1e9f,1e9f,1e9f}, hi{-1e9f,-1e9f,-1e9f};
    for (const auto& v : m.vertices) { lo.x=std::min(lo.x,v.position.x); hi.x=std::max(hi.x,v.position.x); lo.y=std::min(lo.y,v.position.y); hi.y=std::max(hi.y,v.position.y); }
    const float scale = 19.0f / (hi.x - lo.x);
    rig.set_model_scale(scale);
    game::FlightState g; g.grounded = true; g.ground_clearance = 0.0f; g.wing_tuck = 1.0f; g.wing_angle = core::radians(18.0f);
    for (int i = 0; i < 300; ++i) rig.update(g, 1.0f / 60.0f);
    std::printf("scale %.3f  mesh floor y %.3f (%.2f m below origin)\n", scale, lo.y, -lo.y*scale);
    const auto& w = rig.world_matrices();
    float floor = 1e9f;
    for (int f : j.foot_roots) floor = std::min(floor, w[size_t(f)].col[3].y);
    std::printf("feet: lowest foot joint y %.3f (%.2f m below origin); mesh sole gap %.2f m\n", floor, -floor*scale, (floor-lo.y)*scale);
    for (const auto& [name, h] : rig.foot_heights()) std::printf("  %-10s %+.2f m above the lowest\n", name.c_str(), h);
    auto dir = [&](int a, int b) { Vec3 d = core::normalize(w[size_t(b)].col[3].xyz() - w[size_t(a)].col[3].xyz()); return d; };
    for (int side = 0; side < 1; ++side) {
        const auto& r = j.wing_root[side];
        for (size_t i = 0; i + 1 < r.size(); ++i) { Vec3 d = dir(r[i], r[i+1]); { Vec3 a_ = w[size_t(r[i])].col[3].xyz(), b_ = w[size_t(r[i+1])].col[3].xyz(); std::printf("wing %s(%.2f,%.2f,%.2f)->%s(%.2f,%.2f,%.2f) dir out %.2f up %.2f z %.2f len %.3f\n", s.joint(r[i]).name.c_str(), a_.x,a_.y,a_.z, s.joint(r[i+1]).name.c_str(), b_.x,b_.y,b_.z, d.x, d.y, d.z, core::length(b_-a_)); } }
        for (const auto& f : j.wing_fingers[side]) for (size_t i = 0; i + 1 < f.size(); ++i) { Vec3 d = dir(f[i], f[i+1]); Vec3 p = w[size_t(f[i+1])].col[3].xyz(); std::printf("  %s dir out %.2f up %.2f z %.2f  tip (%.2f,%.2f,%.2f)\n", s.joint(f[i]).name.c_str(), d.x, d.y, d.z, p.x, p.y, p.z); }
        for (const auto& c : {j.leg[side], j.front_leg[side]}) for (size_t i = 0; i + 1 < c.size(); ++i) { Vec3 d = dir(c[i], c[i+1]); std::printf("leg %s dir x %.2f y %.2f z %.2f\n", s.joint(c[i]).name.c_str(), d.x, d.y, d.z); }
    }
    // Skin clouds: where the MESH limb each bone owns actually points, in bind
    // and after the pose. Principal axis of the vertices a bone dominates,
    // signed away from the joint.
    auto cloud_axis = [&](int joint, Vec3& centroid_out) {
        double sum[3]={0,0,0}, cov[3][3]={{0,0,0},{0,0,0},{0,0,0}}; double n=0;
        std::vector<Vec3> pts;
        for (const auto& v : m.vertices) {
            float wgt = 0.f; for (int k=0;k<4;++k) if (v.joints[k]==joint) wgt += v.weights[k];
            if (wgt < 0.5f) continue;
            pts.push_back(v.position); sum[0]+=v.position.x; sum[1]+=v.position.y; sum[2]+=v.position.z; n+=1;
        }
        if (n < 10) { centroid_out = Vec3{0,0,0}; return Vec3{0,0,0}; }
        Vec3 c{float(sum[0]/n),float(sum[1]/n),float(sum[2]/n)}; centroid_out = c;
        for (const Vec3& q : pts) { double d[3]={q.x-c.x,q.y-c.y,q.z-c.z}; for(int a=0;a<3;++a) for(int b=0;b<3;++b) cov[a][b]+=d[a]*d[b]; }
        // power iteration
        double v[3]={0.3,0.8,0.5};
        for (int it=0; it<60; ++it) { double u[3]={0,0,0}; for(int a=0;a<3;++a) for(int b=0;b<3;++b) u[a]+=cov[a][b]*v[b]; double l=std::sqrt(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]); if(l<1e-12) break; for(int a=0;a<3;++a) v[a]=u[a]/l; }
        Vec3 axis{float(v[0]),float(v[1]),float(v[2])};
        const Vec3 origin = s.world_bind(joint).translation_part();
        if (core::dot(axis, c - origin) < 0) axis = axis * -1.0f;
        return axis;
    };
    std::printf("skin clouds (bind axis -> posed axis), %zu verts\n", m.vertices.size());
    std::vector<int> probe_joints;
    if (j.root>=0) probe_joints.push_back(j.root); if (j.chest>=0) probe_joints.push_back(j.chest);
    for (int side=0; side<1; ++side) { for (int q : j.leg[side]) probe_joints.push_back(q); for (int q : j.front_leg[side]) probe_joints.push_back(q); for (int q : j.wing_root[side]) probe_joints.push_back(q); }
    for (int q : j.foot_roots) probe_joints.push_back(q);
    for (int q : probe_joints) {
        Vec3 c; Vec3 a = cloud_axis(q, c);
        if (core::length_sq(a) < 1e-6f) { std::printf("  %-14s (few verts)\n", s.joint(q).name.c_str()); continue; }
        const core::Quat bind_rot = core::quat_from_matrix(s.world_bind(q));
        const core::Quat posed_rot = core::quat_from_matrix(w[size_t(q)]);
        const Vec3 posed = core::rotate(posed_rot * core::conjugate(bind_rot), a);
        auto ang = [](Vec3 d){ return core::degrees(std::atan2(-d.z, -d.y)); }; // 0 = straight down, + = toward -Z (forward on a -Z model)
        std::printf("  %-14s bind axis (%.2f,%.2f,%.2f) %+.0f deg fwd of down | posed (%.2f,%.2f,%.2f) %+.0f deg | centroid y %.3f\n", s.joint(q).name.c_str(), a.x,a.y,a.z, ang(a), posed.x,posed.y,posed.z, ang(posed), c.y);
    }
    // Body pitch: root -> chest.
    if (j.root >= 0 && j.chest >= 0) { Vec3 d = dir(j.root, j.chest); std::printf("spine root->chest dir y %.2f z %.2f (%.0f deg nose-up)\n", d.y, d.z, core::degrees(std::atan2(d.y, -d.z * (j.head>=0 && w[size_t(j.head)].col[3].z < w[size_t(j.root)].col[3].z ? 1.f : -1.f)))); }
    return 0;
}
