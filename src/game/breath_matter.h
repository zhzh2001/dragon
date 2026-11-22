#pragma once

// A breath of matter as pieces (docs/showcase/EFFECTS.md, "Breaths of
// matter as pieces").
//
// Frost, tide, blight and stone were each a marched cloud, then a cloud with
// particles in it. The playtester, twice: "they still look like clouds",
// "breaths just look bad in motion". A body with a soft edge is a cloud
// whatever its colour. This is the other construction: the breath is solid
// pieces with a hard outline and a highlight, each flying its own path, and
// each leaves something where it lands.
//
//   frost  ice lances that fly flat, burst into chips, and stand in the
//          ground as spikes on a patch of rime
//   stone  boulders that arc and tumble, break into rubble that bounces and
//          lies there
//   tide   slugs of water that arc down as a hose's stream does, shed drops,
//          and wet the ground
//   blight the same, thinner, as acid: it burns a dark patch
//
// This file is the motion and the bookkeeping only: no renderer, no
// particles. The app draws the pieces (gfx::WorldRenderer::draw_matter),
// lays the marks into the scene block for the terrain shader, and answers
// each impact with particles and sound. The breath's damage is still the
// combat cone's: a piece that reaches a target bursts there for show.

#include <cstdint>
#include <functional>
#include <vector>

#include "core/math.h"
#include "game/breath.h"

namespace game {

enum class MatterKind : uint8_t {
    IceLance,   // a long crystal in flight
    IceChip,    // what a lance bursts into
    IceSpike,   // a crystal standing in the ground
    Boulder,    // a rock in flight
    Rubble,     // what a boulder breaks into; bounces, then lies
    WaterSlug,  // a point on the stream's path: the app draws one tube through them all
    WaterDrop,
    AcidSlug,
    AcidDrop,
};

struct MatterPiece {
    MatterKind kind = MatterKind::IceLance;
    core::Vec3 position;
    core::Vec3 velocity;
    core::Quat orientation = core::Quat::identity();
    core::Vec3 spin_axis{0.0f, 1.0f, 0.0f};
    float spin_rate = 0.0f;  // rad/s
    // The unit mesh's half extents in metres: a crystal and a slug are long
    // in z, which lies along the flight.
    core::Vec3 scale{1.0f, 1.0f, 1.0f};
    float age = 0.0f;
    float life = 1.0f;
    float seed = 0.0f;     // 0..1: which mesh, which shade
    bool resting = false;  // on the ground, still
};

// What a breath left on the ground: a disc the terrain shader tints.
struct MatterMark {
    core::Vec3 position;
    float radius = 2.0f;
    Element element = Element::None;
    float age = 0.0f;
    float life = 20.0f;
    // 0 to 1 over its first third of a second and its last fifth.
    float strength() const;
};

// A piece met something: the app's cue for particles and sound.
struct MatterImpact {
    MatterKind kind = MatterKind::IceLance;
    core::Vec3 position;
    core::Vec3 velocity;   // the piece's, at the moment
    float size = 1.0f;     // the piece's largest half extent
    bool on_water = false;
    bool on_target = false;
    bool in_air = false;   // it ran out of reach and burst where it was
};

struct MatterTuning {
    // Frost.
    float lance_rate = 22.0f;      // a second while the breath is held
    float lance_speed = 80.0f;     // m/s, over the breather's own
    float lance_length = 1.6f;     // metres, half the crystal's length
    float lance_spread = 0.07f;    // radians
    float lance_tumble = 7.0f;     // rad/s end over end: flying point first, a lance seen from behind is a dot
    float spike_share = 0.5f;      // of the lances that reach the ground, how many leave a spike
    float spike_life = 14.0f;      // seconds
    // Stone.
    float boulder_rate = 10.0f;
    float boulder_speed = 70.0f;
    float boulder_size = 0.65f;    // metres, radius
    float boulder_spread = 0.09f;
    float rubble_life = 10.0f;
    // Tide.
    float water_speed = 60.0f;
    float water_width = 0.45f;     // metres, radius at the jaws: a torrent, for a dragon ten metres long
    float water_drops = 320.0f;    // a second, off the whole stream
    // Blight.
    float acid_speed = 52.0f;
    float acid_width = 0.3f;
    float acid_drops = 130.0f;
    // What falls is thrown above the aim, as a hose is held: it rises over
    // the line of sight and comes back down onto it near the breath's reach.
    // Thrown level, a stream was below the aim all the way, and from the
    // chase camera it was behind the dragon's own head.
    float loft_deg = 9.0f;
    // All.
    float mark_life = 22.0f;       // seconds a mark stays on the ground
    float mark_size = 1.0f;        // over each element's own radius
    int max_pieces = 420;
};

class BreathMatter {
public:
    static constexpr int kMaxMarks = 16;

    MatterTuning tuning;

    // The breath is held this frame: pieces of `element` leave the mouth.
    // `carry` is the breather's velocity, `reach` the breath's range in
    // metres: a lance that has flown it bursts in the air.
    void emit(Element element, core::Vec3 mouth, core::Vec3 direction, core::Vec3 carry, float reach, float dt);

    // One body a piece can burst against.
    struct Target {
        core::Vec3 position;
        float radius = 1.0f;
    };
    // Once a frame: the pieces move, meet the ground (`ground(x, z)` is its
    // height), the water and the targets, and the marks age. The impacts of
    // this step are impacts() until the next.
    void update(float dt, const std::function<float(float, float)>& ground, float water_level,
                const std::vector<Target>& targets);

    // A mark where something landed. Near a mark of the same element it
    // feeds that one: the mark is renewed and grows toward `radius` x 2.
    void mark(Element element, core::Vec3 ground, float radius, float life);
    // A crystal standing in the ground (the frozen air's, which throws no
    // lance): `lean` tips it off the vertical.
    void spike(core::Vec3 ground, core::Vec3 lean, float size);

    void clear();

    const std::vector<MatterPiece>& pieces() const { return pieces_; }
    const std::vector<MatterMark>& marks() const { return marks_; }
    const std::vector<MatterImpact>& impacts() const { return impacts_; }

    // The stream of a liquid breath is one tube through its slugs, youngest
    // first, and while the breath is held it begins at the mouth. True while
    // it is held: `mouth` is then where. At most kStreamNodes slugs fly.
    static constexpr int kStreamNodes = 22;
    bool stream_held(core::Vec3& mouth) const {
        mouth = stream_mouth_;
        return stream_live_;
    }

private:
    float unit();   // -1..1
    float unit01(); // 0..1
    void add(const MatterPiece& piece);
    void land(const MatterPiece& piece, core::Vec3 at, float ground, bool on_water, bool on_target, bool in_air);

    std::vector<MatterPiece> pieces_;
    std::vector<MatterMark> marks_;
    std::vector<MatterImpact> impacts_;
    float owed_[ELEMENT_COUNT] = {};
    float drops_owed_ = 0.0f;
    core::Vec3 stream_mouth_;
    bool stream_fed_ = false;   // emit() was called for a liquid since the last update()
    bool stream_live_ = false;  // and so it was at that update
    uint32_t rng_ = 0x9e3779b9u;
};

}  // namespace game
