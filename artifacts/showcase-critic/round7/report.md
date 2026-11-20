# Blind critic, round 7 (2026-11-20, 18:40)

Sixteen views on commit f92cb75, to an Opus agent that saw only the
renders in this folder and the two slice targets. It did not see rounds 1
to 6. This is the main session's abridgement, in the critic's wording
where quoted. Regions are x range by y range in pixels.

Since round 6: the flame's light summed through a sensor's curve, five
wall crags in rows, the detail layer on large rocks, five landmarks, five
more rocks. Frames 15 and 16 are new views. The ground fire came after
this round.

## Scores

| Frame | Command (with `--model assets/embercrest-hero.glb --size 1920x1080 --headless --hide-ui`) | Round 6 | Round 7 |
|---|---|---|---|
| 1-run-chase | `--run 7 --frames 300` | 3.8 | 4.5 |
| 2-hero-glide | `--studio 0 --inspect 60 12 8 --frames 90` | 4.4 | 4.5 |
| 3-hero-upstroke | `--studio 1 --inspect 60 14 8 --frames 326` | 4.6 | 4.0 |
| 4-hero-downstroke-front | `--studio 1 --inspect 180 15 8 --frames 300` | 3.6 | 4.0 |
| 5-landed | `--frames 900 --input -0.25,0,0,0,0,0` | 3.0 | 3.5 |
| 6-breath-side | `--breathe --inspect 75 16 4 --frames 120` | 4.0 | 4.5 |
| 7-breath-three-quarter | `--breathe --inspect 115 34 10 --frames 150` | 3.6 | 4.0 |
| 8-breath-turn-chase | `--run 7 --breathe --input 0.25,0.7,0,0,0,0 --frames 130` | 4.2 | 4.5 |
| 9-fireball | `--spit --inspect 84 75 5 --frames 46` | 2.8 | 3.0 |
| 10-bite | `--studio 10 --inspect 270 13 -3 --frames 40` | 4.3 | 4.5 |
| 11-river | `--frames 150 --skim` | 3.5 | 3.5 |
| 12-valley-floor | `--studio 0 --inspect 90 60 50 --frames 60` | 3.0 | 3.0 |
| 13-glide-below | `--studio 0 --inspect 120 12 -12 --frames 90` | 4.5 | 5.0 |
| 14-breath-low-pass | `--skim --breathe --inspect 70 30 6 --frames 90` | 3.9 | 4.0 |
| 15-wall-row | `--run 3 --run-empty --frames 60 --cam -480,150,-2080,-692,150,-2188` | new view | 4.0 |
| 16-arch | `--run 3 --run-empty --frames 60 --sun 40,120 --cam -200,66,-1850,-243,60,-1883` | new view | 3.5 |

The mean is 4.0, against 3.8 in round 6. Two rounds by two agents do not
compare to better than half a point, so the numbers show no change.

## The five problems, in its order

1. "The rock kit reads as props on a slope, not as cliffs." The crags
   are "rounded, puffy, single-scale cracked masses", and "nothing blends
   where they meet the terrain: no scree, no ledges, no bedrock". Frames
   1, 4, 6 to 11, 15, 16.
2. The valley walls in the middle distance are "flat beige planes with
   even horizontal banding"; the far range is "stacked terraces".
3. "The ground is empty and stylised": tiled-noise grass, card crowns,
   blob bushes, a canal with stepped banks. It "does not match the
   realistic dragon".
4. "Particles and debug geometry come with no context": white streaks
   with no source (the wind lines; frames 1, 4, 5, 8, 13), course rings
   (5, 11, 14), cotton-ball spray (11, 14).
5. The wings: "flat, opaque ribbons with rod-like fingers", and they
   lose their silhouette at the top of the upstroke and in the front
   view of the downstroke.

## Verdicts

- The fire. It "reads as fire only in frame 8", where "it has a core, a
  darker fringe and a smoky tail". In frames 6, 7 and 14 it is "an even,
  cream-orange tube of constant width with soft edges", "a beam". The
  fireball is "a white disc with a smear". The reflection in the river is
  "a coarse checker of orange pixels".
- The crags. "Objects set on a slope": in frame 1 "a row of loaves along
  the wall foot"; in frame 15 "inflated, rounded masses with no joints or
  layers, and the grass runs right up to them". "The texture is good up
  close. The shapes and the join with the ground are wrong."
- The arch. "A generated mesh dropped onto a lawn": a "smeared and
  blotchy" surface with no readable blocks, and piers on a smooth slope
  with "no rubble, no fallen blocks and no foundation".
- The wings. As problem 5. Frame 13 is the best of the four and the best
  frame of the set: "the head, the horns, the mane and the leg poses are
  at floor quality".

What it said holds up: the dragon's body, head and pose in frames 6, 10
and 13; the haze in the middle distance of frame 1; the far cliff's light
and ledges in frame 11; one crag "that sits in the slope" in frame 5.
