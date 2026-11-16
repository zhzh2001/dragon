#pragma once

// The breath as a volume (docs/MOTION_REFERENCE.md, "Fire: what the
// literature offers"; docs/showcase/EFFECTS.md, "The breath as a volume").
//
// The breath was a stream of camera-facing puffs. Puffs sum, and the tonemap
// scales a bright pixel back to its hue, so however the puffs were shaded
// the jet came out as one flat colour with a scalloped edge. This is the
// other construction: one body of gas, marched.
//
//   1. On the CPU the jet is a spine: a short chain of nodes let go at the
//      mouth a few times a second, each moving as a puff did (the launch
//      speed solved against drag so the jet's length is its reach, the
//      buoyancy lifting it), growing in radius with its age. The youngest
//      node is the mouth itself, so the jet stays on the jaws as the head
//      turns and curves behind them. When the breath stops, no node is at
//      the mouth and what was breathed travels on and dies.
//   2. In one fullscreen pass over the scene (breath_volume.hlsl) each pixel
//      marches its view ray through the spine's capsules, stepping by its
//      distance to them while outside. A sample's density is a round
//      profile about the spine, eaten by noise that scrolls along the jet,
//      more toward the tip (Fuller et al., "Real-time Procedural Volumetric
//      Fire", I3D 2007). The steps are weighted as emission and absorption
//      weight them, and the pixel's colour is the mean heat the ray met,
//      read through a ramp: a pale core at the jaws, the breath's hot
//      colour, its cool one at the rim. Never brighter than the ramp's top.
//
// The modern tier only: it reads the scene's depth, and the march is past
// what the retro tiers' shader models hold. They keep the puffs.

#include <string>
#include <vector>

#include "core/math.h"
#include "gfx/pipeline.h"
#include "gfx/scene_uniforms.h"

namespace gfx {

struct Camera;
class Device;

// What a jet is made of: the fields of game::BreathProfile the volume reads.
struct BreathJet {
    core::Vec3 hot{2.4f, 1.15f, 0.35f};
    core::Vec3 cool{1.0f, 0.2f, 0.03f};
    float buoyancy = 7.0f;
    float drag = 1.4f;
    float life = 1.3f;
    float size_end = 5.5f;
    float spread = 0.10f;  // the puffs' lateral scatter, as a share of the launch speed
    float brightness = 0.85f;
    float cover = 0.9f;
    float range = 60.0f;  // metres: the launch speed is solved against it
};

struct BreathVolumeSettings {
    bool enabled = true;
    int steps = 56;              // march steps a pixel may take
    float mouth_radius = 0.38f;  // metres at the jaws
    float mouth_forward = 1.0f;  // metres from the head joint to the jaws, along the aim
    float end_radius = 1.0f;     // times the puffs' own reach sideways at the tip (size_end + range x spread)
    float extinction = 0.9f;     // 1/m at full density where the jet is 1.5 m in radius, times the profile's cover;
                                 // it thins as the jet widens, so the jet is as opaque across at any width
    float noise_size = 1.3f;     // an eddy's size, in the jet's radii where it is
    float erosion = 0.95f;       // how far the noise eats the edge, in radii
    float flow = 0.55f;          // the noise's speed along the jet, times the launch speed
    float core = 4.0f;           // the pale core's gain over the hot colour
    float gain = 1.5f;           // the body's gain over the hot colour
    float smoke = 0.55f;         // how far the tip darkens to soot, 0 to 1
    float sparks = 70.0f;        // sparks thrown off the jet a second (the app emits them)

    // "key=value,..." over the fields above, by these names; `on` is enabled.
    void parse(const std::string& list);
};

class BreathVolume {
public:
    static constexpr int kMaxNodes = 16;

    bool init(Device* device, PipelineCache* pipelines);
    void shutdown();
    // False on a retro tier, or without a sampled depth target.
    bool available() const { return available_; }

    // The breath is held this frame: the mouth is here, pointing there.
    // Call before update().
    // `carry` is the breather's own velocity. A node keeps it for life, so the
    // jet reaches its range ahead of a flying dragon, as the damage cone
    // does. Launched into still air (as the puffs are) a 100 m/s jet from a
    // dragon at 40 m/s is never more than 17 m ahead of it, and the dragon
    // flies through its own flame.
    void feed(core::Vec3 mouth, core::Vec3 direction, core::Vec3 carry, const BreathJet& jet, float dt);
    // Once a frame, fed or not: the nodes move and age.
    void update(float dt);
    void clear();
    // True when a jet is in the air.
    bool active() const { return available_ && (fed_ ? !nodes_.empty() : nodes_.size() >= 2); }

    // Inside a colour pass on the scene target, no depth attachment, after
    // everything opaque (the pass reads the depth).
    void draw(Device& device, rhi::Pass* pass, const Camera& camera, const SceneUniforms& scene,
              const BreathVolumeSettings& settings, float time_seconds);

    // A fireball through the same march: a spine laid back along its path,
    // the head its youngest end, so the round is a pale ball with a torn,
    // cooling tail and not an opaque bead. No state: call it once a round a
    // frame, in the same pass as draw().
    struct Comet {
        core::Vec3 position;
        core::Vec3 velocity;
        float radius = 2.0f;  // the head's, metres
        core::Vec3 hot{2.4f, 1.15f, 0.35f};
        core::Vec3 cool{1.0f, 0.2f, 0.03f};
        float brightness = 1.0f;
    };
    void draw_comet(Device& device, rhi::Pass* pass, const Camera& camera, const SceneUniforms& scene,
                    const BreathVolumeSettings& settings, const Comet& comet, float time_seconds);

    // A point on the jet for a spark to leave from: `along` 0 at the mouth
    // to 1 at the tip. False when no jet is in the air.
    bool sample(float along, const BreathVolumeSettings& settings, core::Vec3& position, core::Vec3& velocity,
                float& radius) const;

    // For tests and probes: the spine, youngest first.
    struct Node {
        core::Vec3 position;
        core::Vec3 velocity;  // against the air it was breathed into; drag acts on this
        core::Vec3 carry;     // the breather's velocity when it was let go; kept
        float age = 0.0f;
        bool strand_start = false;  // not joined to the node let go before it
    };
    const std::vector<Node>& nodes() const { return nodes_; }

private:
    struct Uniforms {
        core::Vec4 params;     // x the near plane, y time (s), z the node count, w the march's steps
        core::Vec4 shape;      // x extinction, y 1 / noise size (per radius), z erosion, w the noise's speed (m/s)
        core::Vec4 colour;     // x core gain, y body gain, z smoke, w brightness
        core::Vec4 hot;        // rgb
        core::Vec4 cool;       // rgb
        core::Vec4 axis;       // xyz the jet's direction at the mouth
        core::Vec4 bounds;     // xyz a sphere's centre, w its radius: the whole jet
        core::Vec4 hull_a;     // xyz one end of a capsule about the whole jet, w its radius
        core::Vec4 hull_b;     // xyz the other end
        core::Vec4 nodes[kMaxNodes];  // xyz position, w radius
        core::Vec4 info[kMaxNodes];   // x age 0..1, y 1 when joined to the next (older) node
    };

    // The bounds of `count` filled nodes, then the draw.
    void submit(Device& device, rhi::Pass* pass, const SceneUniforms& scene, Uniforms& u, int count,
                const BreathVolumeSettings& settings);

    Device* device_ = nullptr;
    PipelineCache* pipelines_ = nullptr;
    PipelineHandle pipeline_ = INVALID_PIPELINE;
    rhi::Sampler* point_ = nullptr;
    bool available_ = false;

    std::vector<Node> nodes_;  // youngest first
    BreathJet jet_;
    core::Vec3 mouth_ = {};
    core::Vec3 direction_ = core::Vec3::forward();
    float owed_ = 0.0f;       // seconds since the last node was let go
    bool fed_ = false;        // feed() ran since the last update()
    bool fed_last_ = false;   // and in the frame before
};

}  // namespace gfx
