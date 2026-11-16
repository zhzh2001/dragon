# Blind critic, round 4 (2026-11-15, 20:25)

Twelve new views, on commit 45aa30e, to an Opus agent that saw only the
renders in this folder and the two slice targets. It was told that the
white streaks are wind lines and that gold rings and brackets are game
markers to ignore. It did not see rounds 1 to 3. The views are not the
nine of the earlier rounds: this round is about the dragon in motion and
the fire, so the scores do not compare frame for frame.

The agent could not write its own file. This is the main session's
abridgement of its report: the scores and every major item, in the
critic's wording where quoted. Regions are x range by y range in pixels.

Since round 3: the wrist hook and the root panel, the legs' trail and
tuck, the deeper wingbeat, the strikes' hold and the legs in them, the
breath as a marched volume, fireballs as comets. The sparks, the softer
rim and the final dither came after these renders.

## Scores

| Frame | Command (with `--model assets/embercrest-hero.glb --size 1920x1080 --headless`) | Score |
|---|---|---|
| 1-run-chase | `--run 7 --frames 300 --hide-panels` | 4.2 |
| 2-hero-glide | `--studio 0 --inspect 60 12 8 --frames 90 --hide-ui` | 4.6 |
| 3-hero-upstroke | `--studio 1 --inspect 60 14 8 --frames 326 --hide-ui` | 4.3 |
| 4-hero-downstroke | `--studio 1 --inspect 0 15 8 --frames 300 --hide-ui` | 4.0 |
| 5-landed | `--frames 900 --input -0.25,0,0,0,0,0 --hide-ui` | 4.4 |
| 6-breath-side | `--breathe --inspect 75 16 4 --frames 120 --hide-ui` | 4.5 |
| 7-breath-wide | `--breathe --inspect 80 120 8 --frames 120 --hide-ui` | 3.2 |
| 8-breath-turn-chase | `--run 7 --breathe --input 0.25,0.7,0,0,0,0 --frames 150 --hide-panels` | 3.6 |
| 9-fireball | `--spit --inspect 84 75 5 --frames 54 --hide-ui` | 3.0 |
| 10-bite | `--studio 10 --inspect 270 13 -3 --frames 40 --hide-ui` | 4.9 |
| 11-river | `--frames 150 --skim --hide-ui` | 4.7 |
| 12-mountains | `--frames 60 --cam 333.36,98.76,-2119.04,346.80,104.34,-2132.76 --hide-ui` | 4.0 |

The mean is 4.1. No frame reaches the floor target (5). Closest: 10, 11,
2. Furthest: 9, 7, 8.

"Four problems hold down every frame": the terrain is soft and mostly
bare grass, the forest is a sparse scatter of small trees that look like
toys, the light is a flat haze with no warm key light, and "the fire
looks drawn, not hot".

## The dragon in motion

"None of the four poses reads as a living animal in a wingbeat. The body
and the head are passable. The wings are the main problem in all four
frames."

- Glide (2), major: the near wing (1100-1920 x 120-480) "is a rigid
  straight beam" with no camber and no sag in the membrane. The far wing
  is small and folded at a sharp angle. "The two wings do not agree on a
  glide."
- Upstroke (3), major: the raised wing (790-1290 x 195-510) is "folded
  into a rigid polygon", "a paper fan, not a wing in a stroke". A
  flap-like shape under the wing near the shoulder (1060-1210 x 380-450)
  looks folded through the membrane. A pale wedge at the wing root
  (1080-1120 x 500-560) looks like the membrane's inside or a skinning
  tear.
- Downstroke (4), major: the frame is from behind, not the front as its
  label said (the main session's mistake: `--inspect 0` is the rear). The
  wings are "two thin, drooping red blades", limp for the end of a power
  stroke. The legs hang "like stumps" from this angle.
- Bite (10): "the best body pose of the four". The head points down and
  the mouth looks closed, so the bite does not read. The wing stays raised
  as a stiff panel. The hind legs are tucked out of sight.

"In all four frames the membrane is a flat, opaque sheet, the finger
bones are thick straight rods, and nothing bends between the joints. This
is the main reason the dragon looks like a model and not an animal."

Other dragon items: single bright pixels along the near wing's leading
edge and the horns (specular aliasing); black pixel noise on the mane's
spikes; landed, the folded wings "stand up beside the body as two flat
red panels" and the tail runs straight down out of frame.

## Fire

"The flame does not read as hot gas in any frame."

- Side (6): the most coherent. It starts at the mouth and widens. But it
  is "one flat orange shape with a hard dark outline and a pale centre",
  "a cartoon cut-out, not gas". The pale centre shows "a fine diagonal
  hatch or dither pattern, like a screen-door artefact".
- Wide (7): the flame is "about eight times as long as the dragon and
  widens with distance like a cloud, not a jet. Its size breaks the
  dragon's scale."
- Turn (8): the flame "is not attached to the mouth" and reads as "a
  separate orange cloud", "airbrushed, like an explosion sprite". It
  covers the target markers and their text.
- Fireball (9): "a long streak, not a ball: a beige-orange smear with a
  brown rim and a dither pattern", "more as dirty smoke than fire", no
  bright core, darker than the sunlit wall behind it.
- The fire lights nothing: no orange on the head, chest or wing
  undersides in 6, nothing on the ground or trees in 7 and 9. No smoke
  trail, shimmer or embers. It does not bloom.

The three changes the critic ranks first:

1. A value structure: a near-white core at the mouth, an orange body, a
   broken, dark, smoky edge that fades out. No hard outline, no dither
   pattern.
2. Let the flame light the scene: the head, chest and wing undersides,
   and a glow on the ground under the jet.
3. Scale and attachment: keep the jet on the mouth in the chase view,
   its length in proportion to the dragon, and the fireball "a bright,
   compact head with a trail".

## The ten biggest gaps, as ranked

1. The wing membranes are flat, opaque boards with straight rod bones and
   no sag, translucency or bend (2, 3, 4, 5, 6, 10, 11).
2. The fire is a flat, outlined orange shape with no hot core, no smoky
   edge and no light on its surroundings (6, 7, 8, 9).
3. The light is a flat haze with no low warm key light, no rim light and
   no strong cast shadows (every frame).
4. The valley floor is a large empty green plane (2, 3, 4, 6, 7, 9, 10).
5. The mountain walls are smooth, softly textured surfaces with repeated
   strata and pasted rock clumps (1, 4, 6, 7, 8, 9, 12).
6. The forest is a sparse, uniform scatter of small card trees (1, 5, 7,
   9, 11, 12).
7. In the chase view the breath is detached from the mouth and covers the
   target markers (8).
8. The fire is the wrong size: the jet is many times the dragon's length,
   and the fireball is a long smear (7, 9).
9. The wing poses do not read as stages of a wingbeat (2, 3, 4).
10. The water is a flat teal sheet with no reflections, and the spray is
    cotton puffs with straight streaks (11).

## The main session's notes

- The hard outline (item 2 of the fire) was softened in 95d0ee2, after
  these renders. The dither pattern in the flame is the march's own start
  offset, one fixed value a pixel, and is still there.
- The jet's length is the damage range, 155 m. That is a gameplay number.
  The flame could be drawn shorter than it burns, or the range shortened;
  neither is done.
- The breath's light on the dragon exists (`local_light`,
  `scene_common.hlsl`). The critic saw none of it at these settings.
