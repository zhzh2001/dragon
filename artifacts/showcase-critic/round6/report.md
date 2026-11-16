# Blind critic, round 6 (2026-11-15, 22:07)

Fourteen views on commit 507c718, to an Opus agent that saw only the
renders in this folder and the two slice targets. It did not see rounds 1
to 5. The agent could not write its own file; this is the main session's
abridgement, in the critic's wording where quoted. Regions are x range by
y range in pixels.

Since round 5: the river's share of the fire's light, the lens ghosts,
the flame's core and skin, the smoke off the plume, the fingers' bend,
the meadow's drifts. Frames 7, 9, 12 and 14 are new views; 1 and 8 now
hide the HUD.

## Scores

| Frame | Command (with `--model assets/embercrest-hero.glb --size 1920x1080 --headless --hide-ui`) | Round 5 | Round 6 |
|---|---|---|---|
| 1-run-chase | `--run 7 --frames 300` | 4.2 | 3.8 |
| 2-hero-glide | `--studio 0 --inspect 60 12 8 --frames 90` | 4.8 | 4.4 |
| 3-hero-upstroke | `--studio 1 --inspect 60 14 8 --frames 326` | 4.6 | 4.6 |
| 4-hero-downstroke-front | `--studio 1 --inspect 180 15 8 --frames 300` | 3.6 | 3.6 |
| 5-landed | `--frames 900 --input -0.25,0,0,0,0,0` | 3.8 | 3.0 |
| 6-breath-side | `--breathe --inspect 75 16 4 --frames 120` | 5.0 | 4.0 |
| 7-breath-three-quarter | `--breathe --inspect 115 34 10 --frames 150` | new view | 3.6 |
| 8-breath-turn-chase | `--run 7 --breathe --input 0.25,0.7,0,0,0,0 --frames 130` | 4.4 | 4.2 |
| 9-fireball | `--spit --inspect 84 75 5 --frames 46` | 3.0 | 2.8 |
| 10-bite | `--studio 10 --inspect 270 13 -3 --frames 40` | 5.2 | 4.3 |
| 11-river | `--frames 150 --skim` | 4.3 | 3.5 |
| 12-valley-floor | `--studio 0 --inspect 90 60 50 --frames 60` | new view | 3.0 |
| 13-glide-below | `--studio 0 --inspect 120 12 -12 --frames 90` | 5.3 | 4.5 |
| 14-breath-low-pass | `--skim --breathe --inspect 70 30 6 --frames 90` | new view | 3.9 |

The mean is 3.8, against 4.25 in round 5. No frame reaches the floor
target. This agent scored every repeated frame at or below round 5's
score, including frames nothing changed in (5, 11: 0.8 lower each). The
rounds' numbers do not show that the evening's later changes raised the
picture, and they do not show that they lowered it. The wording is the
result.

## What it confirms

- Frame 3: "the red membrane is translucent and billows between the
  fingers, and the pose reads as an upstroke". Frame 10 "does show bow
  between the fingers along the trailing edge".
- The flame is attached to the mouth in all four breath frames. "No
  hatching or banding is visible."
- Frame 14: "the flame's reflection on the water is good", "the best fire
  moment". Frame 6 "tints the dragon's chest orange".
- The fireball "is a compact bright head with a short tail. That is
  correct", but 80 px across at that distance.
- "The fire is not too red or too dark."

## What it does not

- The wings: "in every other frame the wings read as boards". The near
  wing in the glide is "a flat plank with no camber". From the front the
  downstroke is "flat paddles", read as a glide. "The finger bones are
  straight rods everywhere": the bend of 24 degrees was not seen.
- The flame: "no dark broken edge and no soot", "a smooth orange
  contour", "paint, not gas". The pale core "does not form a hot
  white-yellow point at the jaws". White blotches in the body
  (frame 6, 1500-1700 x 450-520). The skin of smoke and the plume's smoke
  were not seen, but for "some grey smoke at the far left" of frame 8.
- The fire's reflection in the water is "too uniform and blocky" and
  follows "the water's tiled ripple grid".
- The meadow: "close to one flat sheet"; the drifts "read as fine noise,
  not as natural patches". The tracks: "pale meandering lines", "paths or
  stains with no purpose".
- The river's edge in frame 12 (1350-1920 x 0-200) is "a stair-stepped
  polygon".

## The ten biggest gaps, as ranked

1. Wings as flat rigid planks with straight rod bones (2, 4, 5, 8, 11, 13).
2. Meadow and slopes flat uniform green (1, 6, 7, 9, 12).
3. Cliffs as flat horizontal strata, "a layer cake" (1, 4, 5, 6, 9, 10, 11).
4. The flame a flat orange shape, no soot, no hot point (6, 7, 8, 14).
5. Clouds as cotton-ball sprites (1, 4, 5, 8, 11).
6. The river a flat strip on wide grey sand, stair-stepped edges (1, 3, 11, 12, 13).
7. Trees small, identical, sparse (1, 4, 6, 9, 12).
8. The dragon lit flat orange-brown, weak form shading (1, 4, 5, 11).
9. Framing: the subject tiny or absent (7, 9, 12).
10. Water and spray: tiled ripples, opaque puffs (11, 14).

## The main session's notes

- The tracks were removed after this round, and the rim's grain on the
  dragon's edges was filtered (`docs/showcase/HERO.md`, "The rim's
  grain").
- Six rounds agree on the order: the wings, the ground and the cliffs,
  the flame, then the trees and the water. Tuning has moved single
  frames and not the order. The wings need geometry (a membrane mesh
  with slack in it and thinner fingers), which is an asset job.
