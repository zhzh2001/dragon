#pragma once

#include <string>
#include <vector>

#include "core/math.h"

namespace anim {

constexpr int NO_PARENT = -1;
// Skinning matrices travel to the GPU in a uniform block, so the count is fixed.
// A dragon rig needs around 30; 64 leaves room without wasting much.
constexpr int MAX_JOINTS = 64;

struct Joint {
    std::string name;
    // Index of the parent, always LESS than this joint's own index. Keeping the
    // array topologically sorted means world transforms resolve in a single
    // forward pass with no recursion and no visiting order to get wrong.
    int parent = NO_PARENT;
    // Rest transform relative to the parent.
    core::Transform local_bind;
    // Inverse of this joint's world bind transform. Precomputed because it is
    // constant and needed for every skinning matrix, every frame.
    core::Mat4 inverse_bind = core::Mat4::identity();
};

class Skeleton {
public:
    // Appends a joint. `parent` must already exist, which enforces the sorted
    // invariant by construction.
    int add_joint(const std::string& name, int parent, const core::Transform& local_bind);

    // Computes every inverse bind matrix. Call once after the last add_joint.
    void finalize();

    int find(const std::string& name) const;
    int count() const { return int(joints_.size()); }
    const Joint& joint(int index) const { return joints_[size_t(index)]; }
    const std::vector<Joint>& joints() const { return joints_; }

    // World bind transform of a joint, derived by walking to the root.
    core::Mat4 world_bind(int index) const;

private:
    std::vector<Joint> joints_;
};

// A set of local joint transforms: one animation frame.
struct Pose {
    std::vector<core::Transform> local;

    void reset_to_bind(const Skeleton& skeleton);
    bool matches(const Skeleton& skeleton) const {
        return local.size() == skeleton.joints().size();
    }
};

// Resolves local transforms into world matrices. Single forward pass, relying on
// parents preceding children.
void compute_world_matrices(const Skeleton& skeleton, const Pose& pose,
                            std::vector<core::Mat4>& out_world);

// Skinning matrix for each joint: world_current * inverse_bind. This is what the
// vertex shader multiplies by, so a joint left at its bind pose contributes the
// identity and the mesh does not move.
void compute_skinning_matrices(const Skeleton& skeleton, const std::vector<core::Mat4>& world,
                               std::vector<core::Mat4>& out_skinning);

// Blends two poses per joint. `t` of 0 gives `a`.
void blend_poses(const Pose& a, const Pose& b, float t, Pose& out);

}  // namespace anim
