# Particles and audio

## Particles and audio (M7, first half)

`gfx::ParticleSystem`: CPU-simulated, GPU-billboarded quads drawn **additively
with depth test but no depth write** -- particles are light, they sum and never
occlude, which is also why one unsorted draw call is correct. The pool is
fixed (4096, swap-remove); when full, a sampled oldest particle is replaced,
because the newest particles are the bright just-happened ones whose absence
would be noticed. Staging rides alongside the debug-line upload: **a copy pass
cannot open inside a render pass**. Emitters live in the app: flame (buoyant
puffs launched down the cone, spawn rate integrated so frame rate cannot thin
the fire), projectile ember trails, and impact bursts with upward splash on
terrain. The damage cones and hitboxes are untouched -- particles are what the
fight looks like, never what it is.

**Boost is air, not fire**: pale slipstream threads off the wingtips, faint
streaks rushing past the body, and a field-of-view surge that eases back
through the camera's own fov lag -- the world moving, not the dragon burning
(flame-coloured boost read as being on fire). The surge rides on top of
whatever camera preset is active. **Bots cry too** -- the same screech and
dying cry as the player, pitched deeper with per-bot variation so a flight
never chorusing, faded by distance to the camera. A bot's hit flash **reddens
rather than brightens** (high emissive whitens through the tonemap, and a
white flash was unreadable as damage), and a target held in the breath sheds
embers continuously, because breath has no projectile impact to detonate.

**Flame reach is solved, not tuned**: launch speed is computed against drag so
a puff's travel distance equals the damage range (v = d*k/(1-e^-kT)), because
the flame's visible length is how the player judges reach. Being inside a
hostile flame swarms embers over the player's own dragon -- the vignette says
"damage", the fire crawling on you says "burning". The damage screech is
rate-limited to one per 0.45 s: a flame deals damage every frame, and forty
overlapping cries per second was the playtest's "strange loud flame".

**The breath belongs to the species, not to combat.** `game::BreathProfile`
(`src/game/breath.h`) is loaded from `<model>.breath.cfg` beside each glTF and
says both what a breath does and how it moves. The behaviour half is
*multipliers* on the Combat panel's dials -- a frost drake is "0.75 the reach,
1.6 the cone", never "116 metres" -- so raising `breath_range` still moves every
species together, which is what you want while balancing. The look half is the
particle character, and it is what actually separates the elements: **buoyancy
is the strongest single dial**, positive billowing like flame and negative
pouring downhill like frost or a heavy gas, with `spread`, `life`, `size_end`
and `rate` deciding whether a breath reads as a jet, a cone or a cloud. One
emitter, six elements.

Combat never learns what a species is: a `BreathCone` carries the breather's
scales and an opaque source tag, and only the renderer resolves that tag to a
profile. A cone with no tag -- a sentinel drone -- keeps the cold hostile blue,
which is what makes incoming fire readable as incoming at a glance.

**Elements have effects past the flame (M26).** A breath names its element
(`element frost` in the file), and a creature can breathe an element its
species file does not: `game::element_breath` folds the six species profiles
into one preset per element, so a run's rolled frost rival breathes
frostvein's sinking mist whatever mesh it wears. What each element's hit
leaves -- burn, chill and freeze, corrode, shock, drench, stagger -- is drawn
as particles on the body in that element's motion, since the particles are
additive and nothing dark (smoke) is possible without a second pass: flame
licks rise, frost motes sink, an ice-glint shell hugs a frozen body (and its
hide is recoloured to ice), blight drips and bubbles, sparks crawl with small
arcs, water runs off, grit puffs. Storm arcs are jagged chains of points
struck for 0.12 s and re-struck, which is what lightning looks like anyway.
Bolts, their trails and their impacts take the element's hot and cool
colours; a tower's brazier plumes in its element. `COMBAT.md` "Elements" has
the rules; `--status NAME` holds one on every enemy for a capture.

Colour choice was constrained by the tonemap, not by taste. Per-channel
Reinhard plus a gamma encode sends **anything bright toward white**: a pale
blue frost breath clipped to a white smear, and fire -- many additive puffs
stacked -- read as cream. Since M23 the composite tonemaps with a
hue-preserving curve (`hue_preserve` under Grade & bloom: the luminance goes
through Reinhard, the colour ratios are kept, and a colour that would leave
the gamut is scaled back by its peak channel rather than clipped channel by
channel), so a stack of puffs keeps its RATIO however bright it gets -- which
makes the ratio the colour the player sees. The default fire is therefore a
deep orange (2.4, 1.15, 0.35), not the old yellow (2.2, 1.5, 0.7). Elemental
colours still want to be saturated -- deep cyan for frost, violet for storm --
because a desaturated ratio is a desaturated breath at every brightness now,
not only at the top. The damage flash still reddens rather than brightens:
it is drawn on the HUD, after the tonemap.

`audio::Audio` synthesizes every sound at init -- **no audio assets**, in the
same spirit as the procedural terrain. Continuous streams (wind through a
lowpass whose cutoff opens with airspeed, so a dive gets brighter rather than
merely louder; flame noise with a slow crackle) are set by level each frame
and smoothed at audio rate; one-shots (brown-noise explosion with a sub
thump, filter-swept shot whoosh, a wounded-animal screech for damage and a
longer dying one for the knock-out, a rising boost rush, wingbeat) fire from a
lock-free voice pool. Output is stereo with independent noise per channel --
identical channels collapse to mono in the head -- plus a gust LFO on the wind
and sparse crackle pops on the flame, which is what separates fire from
filtered static. Explosion loudness follows distance to the CAMERA -- the ear sits
where the player does. A tanh soft-clip keeps a busy fight loud but never
harsh. Master volume in the Engine panel; headless runs skip the device.

Every ImGui window except Combat starts **collapsed** -- one click away, not
hidden, but the screen belongs to the game.

**Handling per model.** The two dragons fly identical numbers, and the eye
insists the small quick-looking wyvern is lighter. `FlightTuning::heft` is
one knob that multiplies the mass and slows roll/pitch/yaw and control lag by
its square root, applied at use so it composes with the presets instead of
being baked into the sliders (the first version rescaled the sliders in place
and fought the preset buttons). "save for this model" writes the tuning to
`<model>.flight.cfg` beside the glTF, loaded automatically on top of
`assets/flight_tuning.cfg` when that model is used.

**Landing and taking off are states.** Auto-flap once flapped at full power
under 75 m no matter what, so the dragon could never land -- it hovered near
the ground fighting its pilot. Now: brake held low, or being down, is a
landing intent that stands the assist aside; a nose pointed down or a held
tuck is a dive and gets no assist flaps either (wings beating in a dive read
as wrong because they are); near-ground assist is proportional and only
against a sink. On the ground a slow body settles level on the surface
(yaw kept), and the first flap from the ground is a leap (`takeoff_jump`,
`takeoff_push`), because the min-airspeed assist is off on the ground and a
taxiing dragon otherwise never reached flying speed.

Bots cycle through four hides -- rust, bone, moss, violet -- so a flight is not
four copies of one dragon, and their fire leaves the animated head like the
player's ("bot recolour" slider in the Combat panel, right under the bot
skill buttons). **A multiplicative tint cannot recolour a dark texture**: the first
four bot tints were four indistinguishable greys. `ModelUniforms::recolour`
pushes the albedo toward a hue at its own luminance instead, so scales and
shading survive and "the green one" is a thing a player can say. The player
gets the same control (Dragon panel, `--hue`).

**Boost threads leave both animated wingtips**, one per side per step. The
first version drew the side from a random bit it never advanced, so whole
frames of threads landed on one wing. The outermost wing joint per side is the
one furthest from the centreline in bind -- not the last joint of the last
finger, which on this asset is a helper bound at the origin. Ignition blows a
ring of air outward so the start of a boost is an event.
