#include "anim/skeleton.h"

#include "core/log.h"

using core::Mat4;
using core::Transform;

namespace anim {

int Skeleton::add_joint(const std::string& name, int parent, const Transform& local_bind) {
    if (parent != NO_PARENT && (parent < 0 || parent >= int(joints_.size()))) {
        LOG_ERROR("joint '%s': parent %d does not exist yet", name.c_str(), parent);
        return NO_PARENT;
    }
    if (int(joints_.size()) >= MAX_JOINTS) {
        LOG_ERROR("joint '%s': skeleton is full (%d)", name.c_str(), MAX_JOINTS);
        return NO_PARENT;
    }
    Joint joint;
    joint.name = name;
    joint.parent = parent;
    joint.local_bind = local_bind;
    joints_.push_back(joint);
    return int(joints_.size()) - 1;
}

Mat4 Skeleton::world_bind(int index) const {
    Mat4 result = joints_[size_t(index)].local_bind.to_matrix();
    int parent = joints_[size_t(index)].parent;
    while (parent != NO_PARENT) {
        result = joints_[size_t(parent)].local_bind.to_matrix() * result;
        parent = joints_[size_t(parent)].parent;
    }
    return result;
}

void Skeleton::finalize() {
    for (size_t i = 0; i < joints_.size(); ++i) {
        joints_[i].inverse_bind = core::inverse(world_bind(int(i)));
    }
}

int Skeleton::find(const std::string& name) const {
    for (size_t i = 0; i < joints_.size(); ++i) {
        if (joints_[i].name == name) return int(i);
    }
    return NO_PARENT;
}

void Pose::reset_to_bind(const Skeleton& skeleton) {
    local.resize(skeleton.joints().size());
    for (size_t i = 0; i < local.size(); ++i) local[i] = skeleton.joint(int(i)).local_bind;
}

void compute_world_matrices(const Skeleton& skeleton, const Pose& pose,
                            std::vector<Mat4>& out_world) {
    const int count = skeleton.count();
    out_world.resize(size_t(count));
    for (int i = 0; i < count; ++i) {
        const Mat4 local = pose.local[size_t(i)].to_matrix();
        const int parent = skeleton.joint(i).parent;
        // Parents always precede children, so the parent's world matrix is
        // already final by the time we reach the child.
        out_world[size_t(i)] = parent == NO_PARENT ? local : out_world[size_t(parent)] * local;
    }
}

void compute_skinning_matrices(const Skeleton& skeleton, const std::vector<Mat4>& world,
                               std::vector<Mat4>& out_skinning) {
    const int count = skeleton.count();
    out_skinning.resize(size_t(count));
    for (int i = 0; i < count; ++i) {
        out_skinning[size_t(i)] = world[size_t(i)] * skeleton.joint(i).inverse_bind;
    }
}

void blend_poses(const Pose& a, const Pose& b, float t, Pose& out) {
    const size_t count = a.local.size() < b.local.size() ? a.local.size() : b.local.size();
    out.local.resize(count);
    for (size_t i = 0; i < count; ++i) {
        out.local[i].position = core::lerp(a.local[i].position, b.local[i].position, t);
        // Rotations must slerp, not lerp: a lerp of two quaternions shortens the
        // bone and speeds unevenly through the arc.
        out.local[i].rotation = core::slerp(a.local[i].rotation, b.local[i].rotation, t);
        out.local[i].scale = core::lerp(a.local[i].scale, b.local[i].scale, t);
    }
}

}  // namespace anim
