#pragma once

#include "core/math.h"

namespace game {

// How one species' breath differs from the master dials, as multipliers rather
// than absolute numbers.
//
// The Combat panel stays the single place balance lives: a frost drake says
// "half the reach, twice the cone", not "78 metres, 24 degrees". Raise
// `breath_range` for everyone and every species moves together, which is what
// you want while tuning and what absolute per-species numbers would quietly
// break.
struct BreathScales {
    float range = 1.0f;
    float angle = 1.0f;
    float damage = 1.0f;
    // Meter cost per second. A breath that reaches further usually ought to
    // cost more to hold, and this is where that trade is expressed.
    float drain = 1.0f;
};

// A species' breath: what it does, and what it looks like doing it.
//
// The look is not decoration. A flame, a frost cone, a corrosive cloud and a
// lightning arc differ mostly in how their particles move -- whether they rise
// or fall, how fast they spread, how long they last -- and those are the
// numbers below rather than four separate emitters.
//
// **Colours want to stay mid-value and saturated.** The tonemap ends in a gamma
// encode, so anything bright desaturates toward white (see EFFECTS.md, and the
// damage flash that had to be reddened rather than brightened). A pale blue
// frost breath clips to a white smear; a deep cyan one reads as frost.
struct BreathProfile {
    BreathScales scales;

    // Hot core and cool tip, additive. Values above 1 are deliberate.
    core::Vec3 hot{2.2f, 1.5f, 0.7f};
    core::Vec3 cool{1.0f, 0.25f, 0.04f};

    // Vertical acceleration on each puff: positive billows upward like flame,
    // negative makes a heavy breath pour downward like frost or gas.
    float buoyancy = 7.0f;
    // Lateral scatter at launch, as a fraction of the launch speed. Small is a
    // jet, large is a cloud.
    float spread = 0.10f;
    float size_start = 2.0f;
    float size_end = 5.5f;
    // Puff lifetime in seconds. The launch speed is solved against this so the
    // flame's visible length stays equal to its reach, so shortening the life
    // makes a snappier breath rather than a shorter one.
    float life = 1.3f;
    // Particles per second. Denser reads as heavier.
    float rate = 260.0f;
    // Drag on each puff; higher stops the breath dead at its tip.
    float drag = 1.4f;
    // Brightness fed to the additive particle. A light source should not also
    // be lit, so this is the whole of how bright a breath is.
    float brightness = 0.85f;
};

// Flat `key value` text, one field per line, exactly like the flight and rig
// profiles. A model opts in by placing `<model>.breath.cfg` beside its glTF;
// absent or unknown keys leave the defaults untouched, so a model without one
// breathes fire like everything did before species existed.
bool save_breath_profile(const BreathProfile& profile, const char* path);
bool load_breath_profile(BreathProfile& profile, const char* path);

}  // namespace game
