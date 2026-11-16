# Motion and effects reference

Two studies from 2026-11-15, made at the user's request: "find some
references from established media, so that wing flapping, melee attacks,
etc. can look more natural", and for the breath and the projectiles,
"consider classic fire/physics implementations/papers".

This file holds our measurements and our reading of published work. It
holds no frame, clip or asset from another game or film, and none may be
committed (`CLAUDE.md`, "Everything committed here may go public"). The
clips were downloaded to a scratch directory, measured and left there.

## How the motion was measured

An Opus subagent found videos, took sections a few seconds long with
`yt-dlp --download-sections`, wrote frames with `ffmpeg` at the video's own
rate, tiled them into contact sheets, and counted frames. It measured the
game the same way, on frame sequences of the hero dragon rendered at 60
frames a second (`--studio 1`, `10` and `8`, `--inspect 90 16 4` and
`180 16 10`).

Limits that apply to every number below:

- A chase camera follows the body, so no reference gives the body's heave
  or pitch through a beat.
- A slow-motion clip of an animal does not state its slow-down. Animal
  numbers are ratios within a beat, not rates.
- The film clips were sampled at 6 to 8 frames a second. Their downstroke
  shares are not measured.
- The game's melee frames were 12 apart, so each melee number of the
  game's is within 12 frames.

## Sources

| Source | Kind | Used for |
|---|---|---|
| Dragontwin (an indie open-world dragon game in early access; "Exploration and Battle", "Epic Battles") | realistic game, chase camera | flapping from behind, legs, breath |
| Century: Age of Ashes, gameplay trailer | realistic game, chase camera | flapping from behind, legs |
| Day of Dragons, flight guide | realistic game | the take-off beat |
| The Legend of Spyro: Dawn of the Dragon, free flight | stylised game | flapping from behind |
| Spyro Reignited Trilogy, flying levels | stylised game | flapping from behind |
| Glyde the Dragon, "Combat Clarity" update | stylised game | the hover beat in combat |
| Monster Hunter Wilds, Rathalos moveset | realistic game | bite, tail sweep, fireball |
| Elden Ring, Flying Dragon Agheel | realistic game | breath from the air |
| House of the Dragon S1E10 (Vhagar and Arrax) | film | flapping, bite |
| The Hobbit: The Desolation of Smaug | film | Smaug's beat, coarse |
| How to Train Your Dragon, "Test Drive" | film | glide and bank |
| A bald eagle and a bat in slow motion | real animals | fold and timing ratios |
| Pennycuick 1996, J. Exp. Biol. 199:1613 | published | wingbeat rate by size |

Pennycuick's formula gives 0.55 to 0.75 beats a second for an animal 12 m
long with a 20 m span. Not usable: a Skyrim clip (the dragon too small in
frame) and a Day of Dragons fight (too chaotic). No claw swipe was found
in any reference.

## Flapping

| Source | Beats a second | Downstroke's share | Shoulder range | Upstroke | Tip lag | Hind legs |
|---|---|---|---|---|---|---|
| Dragontwin | 0.7 to 0.8 | about 60%, low confidence | wings cup down, tips below the wrist | hand flexes down and trails, 20 to 30% of span lost | visible | hang |
| Century | 1.2 to 1.4 | 44 to 50% | about +65 to -40 degrees, tips curl under | arm up, hand drooping, about 25% of span lost | 10 to 14% of the beat | hang |
| Day of Dragons, take-off | 1.2 | 44% | about +70 to -25 degrees, cupping forward at the bottom | arm leads, hand trails | visible | not seen |
| House of the Dragon, Arrax | about 0.7 | not measured | about +50 to below level | hand trails | visible | hang |
| Smaug | 0.55 to 0.6 | not measured | large | not measured | not measured | none (a wyvern) |
| Legend of Spyro | about 1.15 | about 35% | about +80 to -70 degrees | not measured | a flick | trail and hang |
| Spyro Reignited | about 2 | not measured | about +30 to -30 degrees | none | not measured | trail back |
| Glyde, hover | about 2 | about 30% | +60 to -10 degrees | not measured | not measured | hang |
| Bald eagle | (large birds 2 to 3) | about 44% | large | span drops to 50 to 60% | about 10% | tucked |
| Bat | not given | 45 to 50% | large | span drops to 30 to 40% | the hand points down as the wrist rises | hang |
| The game, before | 1.1 | 28% | arm +55 to -5, tip to -25 | none: the wing is a straight line, open like a sail | about 15 degrees at one frame | fore hang, hind bent under the belly |

The glide: How to Train Your Dragon holds 10 to 15 degrees of dihedral
with the tips turned up; Dragontwin about 10 degrees with the hand
drooping.

What differs, most visible first:

1. The beat's timing is the wrong way round. The game slams down in 28% of
   the beat and lifts slowly. Realistic games, films and animals spend 44
   to 60% of the beat on the way down. Only the stylised Glyde and Legend
   of Spyro are near the game.
2. No fold on the upstroke. Every reference folds: dragons lose 20 to 30%
   of their span, the eagle 40 to 50%, the bat 60 to 70%. The game's wing
   goes up open, face on to the air.
3. No tip lag. The references' hands trail their arms by 10 to 14% of the
   beat.
4. The stroke is shallow and sits high: about 75 degrees in all, against
   95 to 105, and the references cup the tips under and forward at the
   bottom.
5. The hind legs: the realistic sources hang them. Tucking is How to Train
   Your Dragon's and Spyro's.
6. The rate is inside the range (0.55 to 1.3 across dragons).

## Melee

Frames are at 60 a second unless marked.

| Source | Anticipation | Strike | Hold | Recovery | Body and neck | Wings | Limbs not striking |
|---|---|---|---|---|---|---|---|
| Rathalos, bite | 20 frames (head back and up, neck coiled), then 16 held | 12 | about 20, head at the ground | about 30 | lunges 0.2 to 0.3 body lengths, nose down | rise and spread in the wind-up, sweep forward and down on the strike | legs planted and braced; the tail lifts as the head drops |
| Rathalos, tail sweep | 20 to 24: turns away, wings lift | 16: a whip with the whole body turning 180 degrees or more | 30 of follow-through | about 60 | the whole body spins | half open, raised | the feet step through the spin |
| Vhagar, bite (24 a second) | 16 film frames: the jaws open on the approach | about 2 | 48: the neck thrashes | not seen | the neck extends, then shakes side to side | keep beating, raised high | hind legs hang |
| The game, before | none seen | about 24 | none | about 24 | bite: pitches from nose-up to level | held in one raised pose, then snap flat | unchanged in every frame; the tail follows |

What differs:

1. No anticipation that reads. The references take 20 to 68 frames.
2. No hold at the contact. The references hold 16 to 48 frames.
3. The limbs and the tail do not act. In the references the legs brace
   and the tail swings against the head. This is the playtester's
   "ragdoll" (feedback 0014).
4. The wings hold and then snap. In the references they wind up, balance
   the strike, and in the air keep beating.

The whole-body spin of the tail sweep was seen in Monster Hunter only. It
is that series' style, not common practice.

## Breath and projectiles in the references

| Source | Anticipation | Flame | After |
|---|---|---|---|
| Agheel | the mouth glows for 0.75 s as the head turns | 10 to 15 degrees wide at the mouth, 25 to 30 at the ground | the ground burns as a long strip for 1.5 s and more |
| Dragontwin | a puff at the target 0.6 s ahead; the neck turns down | a thin white-yellow core about 5 degrees wide at the mouth, a billowing orange ball of 30 to 35 degrees at the contact, 1 to 1.5 body lengths long, dark smoke behind | a burning line on the ground for 1.7 s and more |
| Rathalos, fireball | the head rears back for about 1.1 s, wings spread | an orange ball the size of the head; the body recoils | a large orange burst with black smoke |

The game's own breath was not measured: the studio scenario's frames did
not show it from that view.

## Fire: what the literature offers

A Sonnet subagent surveyed the literature. "Read" means it read the
source; the rest it saw in search results only.

| Technique | Source | Read | Use here |
|---|---|---|---|
| Particle systems | Reeves, "Particle Systems, a Technique for Modeling a Class of Fuzzy Objects", ACM TOG 2(2), 1983 | no | what the breath was. Its faults were in how puffs were shaded and summed |
| Soft particles | Lorach (NVIDIA), "Soft Particles", 2007 | no | fades a puff into the surface behind it |
| Video sprites in a volume texture | Nguyen, "Fire in the Vulcan Demo", GPU Gems chapter 6, 2004 | yes | it used alpha blending because additive "oversaturated colors and made fire look cartoonish", and drew flame into a quarter-resolution buffer |
| Physically based fire | Nguyen, Fedkiw and Jensen, "Physically Based Modeling and Animation of Fire", SIGGRAPH 2002 | abstract | offline. The idea to take: black-body colour for the hot gas |
| Stable fluids, vorticity confinement | Stam 1999; Fedkiw, Stam and Jensen 2001 | no | a grid solver is outside a 1 ms budget for a 40 m jet |
| Curl noise | Bridson, Hourihan and Nordenstam, "Curl-Noise for Procedural Fluid Flow", ACM TOG 26(3), 2007 | yes | swirl without a solver: velocity is the curl of a noise potential, so it has no divergence |
| Flow noise | Perlin and Neyret, SIGGRAPH Sketches 2001 | no | texture detail that seems to flow |
| Flame built on a curve | Lamorlette and Foster, "Structural Modeling of Flames for a Production Environment", SIGGRAPH 2002 | no | the basis of the next row |
| A ray-marched volume on a spine | Fuller, Krishnan, Mahrous, Hamann and Joy, "Real-time Procedural Volumetric Fire", I3D 2007 | yes | the fit for a jet: a fire profile swept about a curved spine, its coordinates pushed by noise that scrolls along the flame |
| Heat distortion | a distortion buffer applied once before the tonemap (Froyok, "Refraction", 2024, which names Doom and Dishonored 2) | yes | shimmer over the jet |

No source was found that describes how a shipped dragon's fire is made.

Formulas to hand:

- Emission and absorption, a step of length `ds`: `T = exp(-sigma * ds)`;
  `colour += transmittance * (1 - T) * emission`; `transmittance *= T`.
  Weighting by `1 - T` makes the result independent of the step. The
  pixel is then a weighted mean of the emission along the ray, so it can
  never be brighter than the hottest sample. Summed puffs have no such
  bound, which is why the particle flame went to one flat cream.
- Fuller's mapping: `u = sqrt(x^2 + y^2)`, `v = z + f(z) * turbulence(n)`,
  with `n = (x, y, z - time, time)` scaled by the flame's radius and
  length so the detail does not stretch.
- Curl noise in three dimensions: `v = curl(psi)`, `psi` three noises (one
  function at three offsets). Vortices are the noise's length across and
  turn at one over it.
- Black body: Planck's law at three wavelengths (610, 550 and 465 nm),
  divided by the largest, is a hue; a flame runs from about 1000 K (deep
  orange) to 2000 K (pale yellow). These temperatures are from general
  knowledge, not from a source the survey read.

## What was applied

See `docs/showcase/PLAYTEST.md`, "Feedback 0013 to 0015, and the turn to
the dragon", for what each number became in the rig and the effects.
