# Blind critic, round 5 (2026-11-15, 21:30)

Fourteen views on commit 1c89c7e, to an Opus agent that saw only the
renders in this folder and the two slice targets. It was told that the
white streaks are wind lines, that rings, brackets and HUD plates are game
markers, that frame 4 is from the front, that frame 9 has a fireball in
flight and that frame 14 is a low pass over the river. It did not see
rounds 1 to 4.

The agent could not write its own file. This is the main session's
abridgement: the scores and every major item, in the critic's wording
where quoted. Regions are x range by y range in pixels.

Since round 4: the sparks, the softer rim and the final dither; the
membrane's bow; the breath's light as a line and its moving grain; the
fireball's head; the released flame's burn-out. The river's share of the
breath's light and the lens ghosts' fix came after these renders.

## Scores

| Frame | Command (with `--model assets/embercrest-hero.glb --size 1920x1080 --headless`) | Round 4 | Round 5 |
|---|---|---|---|
| 1-run-chase | `--run 7 --frames 300 --hide-panels` | 4.2 | 4.2 |
| 2-hero-glide | `--studio 0 --inspect 60 12 8 --frames 90 --hide-ui` | 4.6 | 4.8 |
| 3-hero-upstroke | `--studio 1 --inspect 60 14 8 --frames 326 --hide-ui` | 4.3 | 4.6 |
| 4-hero-downstroke-front | `--studio 1 --inspect 180 15 8 --frames 300 --hide-ui` (round 4 was the rear, `--inspect 0`) | 4.0 | 3.6 |
| 5-landed | `--frames 900 --input -0.25,0,0,0,0,0 --hide-ui` | 4.4 | 3.8 |
| 6-breath-side | `--breathe --inspect 75 16 4 --frames 120 --hide-ui` | 4.5 | 5.0 |
| 7-breath-wide | `--breathe --inspect 80 120 8 --frames 120 --hide-ui` | 3.2 | 3.5 |
| 8-breath-turn-chase | `--run 7 --breathe --input 0.25,0.7,0,0,0,0 --frames 130 --hide-panels` (round 4 was frame 150) | 3.6 | 4.4 |
| 9-fireball | `--spit --inspect 84 75 5 --frames 50 --hide-ui` (round 4 was frame 54) | 3.0 | 3.0 |
| 10-bite | `--studio 10 --inspect 270 13 -3 --frames 40 --hide-ui` | 4.9 | 5.2 |
| 11-river | `--frames 150 --skim --hide-ui` | 4.7 | 4.3 |
| 12-mountains | `--frames 60 --cam 333.36,98.76,-2119.04,346.80,104.34,-2132.76 --hide-ui` | 4.0 | 3.8 |
| 13-glide-below | `--studio 0 --inspect 120 12 -12 --frames 90 --hide-ui` | new | 5.3 |
| 14-breath-low-pass | `--skim --breathe --frames 90 --hide-ui` | new | 4.0 |

The mean is 4.25 over fourteen frames, and 4.18 over the twelve of round 4
(4.1 then). Three frames reach the floor target (5): 13, 10 and 6. A
different agent scored each round, and four frames it scored lower show
nothing that changed (5, 11, 12) or a different view (4), so a difference
under half a point is not a measurement.

## The dragon in motion

- "The membrane now reads as membrane in 10 and 13." In 10 there is "real
  scalloped sag between the fingers at the trailing edge" and "the
  underwing glows orange". In 3 "the membrane cups between the fingers
  and shows some translucency", and the upstroke "reads as a stage of a
  wingbeat".
- The arm's panel in 13 (0-1230 x 0-480) is "a thick, uniformly lit slab
  with a leather texture", with no sag between the forearm and the first
  finger. In 2 the near wing (1100-1920 x 110-560) is "the same uniform
  red slab".
- Frame 4, from the front, major: the wings are "bone rods with at most a
  20 px strip of membrane". "A downstroke should show a broad cupped
  sheet. This one reads as a skeleton with no skin."
- Frame 5: the folded wings are "flat orange planes with no fold
  creases", and the body is "crushed into shadow".
- A bright salmon patch at the wing root in 3 (1060-1120 x 480-530) and a
  pink specular seam under the wing in 2 (1120-1250 x 400-520) look like
  lighting errors.
- The body is underlit in back and three-quarter views (1, 2, 3, 5): "a
  dark brown mass".

## Fire

"The flame does not read as hot gas."

- "I saw no hatching, banding or dither."
- Frame 6: attached to the mouth, and "it lights the wing root warmly,
  which is good". Still "a flat orange shape" with "no dark, broken,
  smoky edge".
- Frame 7: "about nine times the length of the dragon". The pale core
  sits in the middle of the plume and not at the mouth. The plume does
  not light the ground (it is about 60 m up).
- Frame 8: "a puffy cumulus cloud" with "no core and no taper".
- Frame 9: "reads as a comet with a pale head and a short tail", but 40 px
  tall at this distance, "too small to judge".
- Frame 14: from behind the flame is "a round fireball sitting on the
  head". "Dull magenta blobs float above it", "a bad-blend artefact".
  "The warm orange spill on the sand and on the wing roots is
  convincing."

## The ten biggest gaps, as ranked

1. The ground is a flat green sheet (2, 3, 6, 7, 9, 10, 13).
2. The cliffs are stacked flat terraces with smeared texture (1, 4, 6, 7,
   9, 11, 12, 14).
3. The membrane is a uniform slab when it is not backlit: no sag at the
   arm, no veins, no thickness falloff (1, 2, 4, 5, 13).
4. The fire is a flat, soft-edged shape with no dark smoky fringe and no
   turbulent breakup (6, 7, 8, 14).
5. The trees are sparse, toy-like and evenly scattered (5, 6, 7, 9, 10,
   11, 12).
6. No snow-capped distant range, no aerial perspective (2, 3, 6, 7, 9).
7. The dragon's body is underlit (1, 2, 3, 5).
8. The flame's scale and placement: it dwarfs the dragon or sits as a
   ball on its head (7, 8, 14).
9. The water is flat teal (2, 3, 11, 13, 14).
10. Framing: a tiny or absent dragon, rings in frame (5, 7, 9, 12).

The three changes it ranks first: break up the valley floor; give the
fire a dark, broken, smoky edge with a bright core at the mouth and cap
its length; give the membrane sag and thin translucent edges from every
angle, and lift the body's shadow side.

## The main session's notes

- The magenta blobs were the lens flare's ghosts of the flame. Fixed
  after these renders (`docs/showcase/EFFECTS.md`, "The river takes the
  fire's light, and the lens leaves the flame alone").
- The bow does not reach the arm's panel because the arm and the flank
  are far apart and its vertices are weighted to the arm, not to a
  finger: their membrane share is low.
- Frames 7, 9 and 12 are views chosen to show one thing from far off.
  The critic scores them as pictures, and they are poor pictures.
