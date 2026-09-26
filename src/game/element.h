#pragma once

#include <cstdint>

#include "core/math.h"
#include "game/breath.h"

namespace game {

// What a breath is made of, and what it does beyond damage.
//
// Six elements, one per breath the roster already carries, and each hit
// leaves one STATUS on what it lands on: fire burns, frost chills and then
// freezes, blight corrodes, storm shocks and arcs, tide drenches, stone
// staggers. That is the whole system. There is deliberately no type chart --
// "frost beats fire" multipliers turn a dogfight into a lookup table (the
// playtest's call: Pokemon-style effectiveness would be too much). The one
// relation between elements is the obvious one: a creature shrugs off its
// own element, taking a share of the damage and none of the status.
//
// The one exception to "no interactions" is physical, not a table: water
// puts fire out. A drenched target cannot burn, and a drench douses a burn
// already going.
// (The enum itself lives in breath.h: a breath profile names its element.)

const char* element_name(Element element);
// The status an element leaves, for the HUD ("burning", "frozen", ...).
const char* element_status_name(Element element);
// Case-insensitive; `fallback` when the name is none of the six.
Element element_from_name(const char* name, Element fallback = Element::Fire);
// A mid-value, saturated colour for the element's HUD tag and its effects.
// Mid-value because the tonemap whitens anything bright (EFFECTS.md).
core::Vec3 element_colour(Element element);
// The hide hue an element's enemies are recoloured toward.
core::Vec3 element_hide(Element element);
// The element's breath: the look and the species scales, for a creature that
// breathes an element its own species file does not (a run's enemies roll
// theirs). The six are the six species profiles in assets/*.breath.cfg,
// folded to one per element; fire is the code default.
BreathProfile element_breath(Element element);

// Every dial of the status effects, beside the rest of combat's in the panel.
struct ElementTuning {
    // Share of damage a creature takes from its OWN element, and it takes
    // none of that element's status.
    float same_resist = 0.5f;
    // How much status one second of breath builds, against a fireball's 1.
    // A bite or strike carries the biter's element at `melee_weight`.
    float breath_weight = 2.0f;
    float melee_weight = 0.6f;

    // ---- fire: burn ----
    // Damage over time after the hit, refreshed by every hit. The flame's
    // afterlife: a pass that connects keeps paying after the dragon is gone.
    float burn_time = 3.0f;    // s
    float burn_dps = 7.0f;

    // ---- frost: chill, then frozen ----
    // Each hit builds chill (0..1); chill slows the target's flight and its
    // fire rate; full chill freezes it -- a frozen dragon's wings lock and
    // it drops, a frozen tower stops firing. Chill thaws when not being hit.
    float chill_per_hit = 0.34f;
    float chill_thaw = 0.30f;  // per second
    float chill_slow = 0.45f;  // slow at full chill
    float freeze_time = 1.6f;  // s, for an enemy
    // The player loses the flap and half the stick for this long: the bot
    // bite rule is that the player never loses the controls outright.
    float player_freeze_time = 0.8f;

    // ---- blight: corrode ----
    // Eats armour: the target takes more of EVERY element while it lasts,
    // plus a weak burn of its own.
    float corrode_time = 5.0f;
    float corrode_vulnerability = 0.25f;  // extra share of damage taken
    float corrode_dps = 3.0f;

    // ---- storm: shock ----
    // A hit arcs to the nearest other enemy within the chain range for a
    // share of the damage, and jams the target's weapons briefly.
    float shock_jam = 0.5f;          // s
    float shock_chain_range = 90.0f; // m
    float shock_chain_share = 0.45f;

    // ---- tide: drench ----
    // No regeneration while drenched, and a drenched target cannot burn. A
    // fireball of tide shoves what it hits.
    float drench_time = 4.0f;
    float drench_push = 10.0f;  // m/s, a fireball's

    // ---- stone: stagger ----
    // Each hit builds stagger (0..1); full stagger stuns an enemy and
    // knocks the player off line.
    float stagger_per_hit = 0.4f;
    float stagger_recover = 0.25f;  // per second
    float stagger_stun = 1.0f;      // s
    float stagger_knock = 14.0f;    // m/s, the player's
};

// What a body is suffering. Timers are seconds left; chill and stagger are
// 0..1 build-ups.
struct Status {
    float burn = 0.0f;
    float chill = 0.0f;
    float frozen = 0.0f;
    float corrode = 0.0f;
    float shock = 0.0f;
    float drench = 0.0f;
    float stagger = 0.0f;
    // Seconds since a hit last built chill or stagger, so they only recover
    // when the pressure stops.
    float since_chill = 1e9f;
    float since_stagger = 1e9f;

    // 0..1 slow on flight and fire rate: chill scales it, frozen is total.
    float slow(const ElementTuning& tuning) const;
    // Weapons cold: frozen or shocked.
    bool jammed() const { return frozen > 0.0f || shock > 0.0f; }
    bool any() const;
    void clear() { *this = Status{}; }
};

// What a hit did to a status, for the effects and the HUD call-outs.
struct StatusReport {
    bool burned = false;
    bool froze = false;      // chill just reached full
    bool corroded = false;
    bool shocked = false;    // the caller arcs the chain
    bool drenched = false;
    bool doused = false;     // a burn was put out
    bool staggered = false;  // stagger just reached full
};

// Damage multiplier for a hit of `attack` on a body of `defender` with
// `status`: the same-element resist, and corrosion's vulnerability.
float element_damage_scale(Element attack, Element defender, const Status& status,
                           const ElementTuning& tuning);

// Applies a hit's status. `weight` is 1 for a fireball, `breath_weight * dt`
// for a frame of breath, `melee_weight` for a bite. `player` picks the
// player's shorter freeze. A body of the attack's own element is immune.
StatusReport apply_element(Status& status, Element attack, Element defender, float weight,
                           bool player, const ElementTuning& tuning);

// Advances the timers and build-ups; returns the damage over time this frame
// (burn and corrode), which the caller applies through its own accounting.
float tick_status(Status& status, float dt, const ElementTuning& tuning);

}  // namespace game
