# Blind critic, round 1 (2026-11-15, 13:05)

An Opus agent saw only the eight renders in this folder and the two concept
targets (`docs/concept/showcase/slice-target-floor.png` and
`slice-target-ceiling.png`). It saw no code and no notes. The method is in
`docs/SHOWCASE.md`, "What other showcases do that this build does not".

The views (1920x1080, `--model assets/embercrest-hero.glb --headless`):

| File | Flags |
|---|---|
| 1-chase | `--frames 240 --hide-panels` |
| 2-hero-close | `--frames 90 --studio 0 --inspect 60 12 8 --hide-ui` |
| 3-landed | `--frames 900 --input -0.25,0,0,0,0,0 --hide-ui` |
| 4-river | `--frames 150 --skim --hide-ui` |
| 5-mountains | `--frames 60 --cam 333.36,98.76,-2119.04,346.80,104.34,-2132.76 --hide-ui` |
| 6-forest | `--frames 60 --cam 364.56,49.75,-2109.75,345.39,44.18,-2111.03 --hide-ui` |
| 7-fire | `--frames 60 --training --attack --hide-panels` |
| 8-run | `--frames 300 --run 7 --hide-panels` |

The report follows as the critic wrote it.

The current build is well short of target-floor.jpg. Only the dragon and the haze in 8-run come close. Terrain, vegetation and water read as a low-poly prototype, the fire effect and the river spray break the picture outright, and the scene mixes at least four art styles.

## Per frame

### 1-chase.jpg
**Eye goes first to:** the yellow gate ring in the centre with the reticle inside it, not the dragon or the valley. The ring is the right target for gameplay, but it hides the vista and the dragon's head.

**Gaps to target-floor:**
- Major: the left slope, top to middle, is a smooth tan cone with brown dots. It has no cliff bands, gullies, scree fans or grass ledges. A correct version has stepped rock walls with lit faces and shadowed recesses, and grass on the flatter ledges.
- Major: there is no aerial perspective. The far range in the centre has the same contrast and saturation as the foreground. The target fades the far peaks to blue-grey and lets the valley recede.
- Major: the river is a flat dark-blue ribbon with hard green banks. The target has gravel bars, pale stony banks, colour change by depth and sky reflection.

**Defects:**
- Major: inside the yellow ring there are grey semi-transparent rings and ellipses that read as stray geometry.
- Major: the "259 m" label is cut by the ring's edge.
- Major: white streaks are scattered across the sky and the right slope. They read as scratches, not as wind.
- Minor: the brown rock dots on both slopes look pasted on. They have no contact shadow.
- Minor: the bottom HUD plate covers the dragon's hind legs and tail.

### 2-hero-close.jpg
**Eye goes first to:** the sun starburst, then the dragon. The dragon has to compete with a flare, which is wrong.

**Gaps to target-floor:**
- Major: the meadow in the right half is a uniform, untextured green, like a golf course. The target's ground has grass tone variation, path wear and scattered rocks.
- Major: the haze in the upper half is a milky grey veil over everything near the sun. The mountains lose shape, not just distance. The target keeps the near walls crisp and hazes only the far range.
- Major: the far peaks at the top centre are low-detail blobs with white tops. The target has sharp, crevassed snow peaks.

**Defects:**
- Major: the right wing membrane in the foreground is smooth, glossy and plastic. It has stretched texture and bead-like spheres on the bone.
- Minor: one small rock floats isolated in the open meadow, right of centre, with no contact shadow.
- Minor: the starburst rays are a cheap lens effect.

### 3-landed.jpg
**Eye goes first to:** the giant yellow ring at the top centre. That is wrong, because this is a landed shot and the dragon must own the frame.

**Gaps to target-floor:**
- Blocker: the ground cover is sparse, spiky low-poly blades and paper bushes spaced out on bare flat green. The target has dense, continuous grass in mixed colours.
- Major: the trees are low-poly. The tree in the centre is a lollipop or palm shape on a bare pole, and the tree line in the back is an even wall with a hard edge.
- Major: the left mountain is the same smooth tan cone with dots.

**Defects:**
- Major: the white speed streaks show in the sky while the dragon stands still.
- Major: grey rings float in front of the far mountains.
- Major: from behind, the dragon's head is a shaggy spiky mop with no readable skull or horns. The wings are half-raised like a cape, neither folded nor open.
- Minor: the lone cloud at the upper left is a cotton-ball sprite with a hard edge.

### 4-river.jpg
**Eye goes first to:** the white spray blobs at the bottom. They are the worst thing in the frame, so that is wrong.

**Gaps to target-floor:**
- Major: the water is dark navy with a small tiling ripple pattern. It has no sky or mountain reflection, no shore foam and no rapids. The target's water is turquoise, with white water and reflections.
- Major: the banks are hard grassy edges straight into the water. The target has stony, pale gravel shores.
- Major: the mountain face in the upper half is lumpy and evenly mottled. It has no rock structure or tree cover gradient.

**Defects:**
- Blocker: the spray particles have hard rectangular cutouts. Their square edges are visible against the water across the lower third, and the dragon's tail passes through them.
- Major: a grey ring is cut off at the upper-left corner.
- Minor: the cloud at the top left has hard edges.
- Minor: a flat rock shape sits pasted on the slope at the upper right.

### 5-mountains.jpg
**Eye goes first to:** the huge yellow ring over the upper right. That is wrong, because the valley wall is the subject.

**Gaps to target-floor:**
- Blocker: the whole valley wall is one sandy noise texture. It has no exposed rock, no cliff faces, no strata and no gullies with scree. It reads as a sand dune or modelling clay. The target's walls are layered grey granite with clear structure.
- Major: the snow is a thin white crust on the crest line only. The target has snowfields and couloirs that follow the shape of the slopes.
- Major: the tree line ends abruptly at the foot of the slope. Above it only scattered dots remain, and there is no forest climbing the gullies.

**Defects:**
- Major: the inside of the ring is a filled grey translucent disc. It reads as a plastic lens over the mountain.
- Major: white streaks show on the right slope.
- Minor: the rock decals look like pebbles at the scale of a mountain.
- Minor: a small yellow marker floats on the slope at the centre.

### 6-forest.jpg
**Eye goes first to:** nothing. The frame is a dark green-navy murk with no focal point. That is wrong.

**Gaps to target-floor:**
- Blocker: the exposure and the grade are too dark and too cold. Shadow in the target stays readable and warm.
- Blocker: the grass is the same sparse blades on bare ground.
- Major: the forest on the slope at the top right is a repeated pattern of identical sprites. It has no canopy mass, no depth and no light variation.

**Defects:**
- Major: the grass blades inside the tree shadows glow yellow as if in full sun, and the ground around them is shadowed.
- Major: the tree trunks at the left are saturated red.
- Major: the rock sprites in the dark sky area at the top left look like stickers.
- Minor: the bushes are blocky clusters of cards.

### 7-fire.jpg
**Eye goes first to:** the flat orange disc that covers the right third of the frame. That is wrong, and the frame is unreadable.

**Gaps to target-floor:**
- Blocker: the fire is a flat, untextured orange shape with a hard silhouette. Real fire has turbulent flame and smoke.
- Major: the dragon is a small dark shape lost in a yellow glow and HUD brackets, so the action is unreadable.
- Major: the left slope is the same tan cone.

**Defects:**
- Blocker: the opaque orange fill with a hard edge, at the right.
- Major: the target labels overlap at the centre. "BURNING" repeats three times on top of distance labels and brackets.
- Major: the speed streaks are long white bars across the whole sky.
- Major: the breath stream at the left is a grey-white smear, not fire.
- Minor: the "fly through the first ring" prompt shows during combat.

### 8-run.jpg
**Eye goes first to:** the HUD text and markers, then the dragon. The marker labels are too loud.

**Gaps to target-floor:**
- Major: the forest on the valley floor is an even carpet of identical dots. It has no clearings, no river corridor variety and no mixed tree heights.
- Major: the river is barely visible at the lower centre. In the target the river is the leading line.
- Minor: the mountains are better here, with real ridges and haze in depth. They are still smooth, with tan slopes and no rock faces.

**Defects:**
- Major: the "HOARD 1065 m (guarded)" label overlaps another label and is garbled. A red underline sits on the dragon's wing.
- Major: the off-screen marker "THE…" is clipped at the right edge.
- Major: the tutorial text under the top bar is two long lines joined with "--". It reads as debug output.
- Minor: the dragon's head is the same mop shape.

## Ten most valuable fixes

1. **Rock and cliff surfaces on the valley walls** (frames 1, 3, 4, 5, 7, 8). Replace the sand-noise slopes with stratified rock faces, scree and grass ledges. This alone closes most of the gap.
2. **Fire effect** (frame 7). Remove the opaque flat orange disc and make turbulent, textured flame with smoke.
3. **Particle edges** (frame 4, likely also 7). Remove the hard square cutouts on the spray and smoke sprites. Fade them softly where they meet geometry.
4. **Ground cover density** (frames 3, 6, 2). Replace sparse blades on bare ground with continuous grass, and vary the meadow's colour.
5. **Tree models and forest distribution** (frames 1, 3, 4, 6, 8). Use realistic conifers and broadleaf trees, vary their height, and break the tree line with clearings and climbs up gullies.
6. **Gate rings and their ghost rings** (frames 1, 3, 4, 5). Remove the grey ring and ellipse shapes and the filled translucent disc. Make the gate a thin, glowing, less dominant hoop, faded when close.
7. **Speed streaks** (frames 1, 3, 5, 7, 8). Remove them when the dragon is slow or landed, and make them subtle at speed.
8. **Water** (frames 1, 2, 4, 8). Add reflection, turquoise depth colour, foam and rapids, and gravel bars on the banks.
9. **HUD marker clutter and clipping** (frames 1, 7, 8). Stop labels from overlapping, keep clipped labels on screen, and cut the tutorial text.
10. **Exposure and atmosphere balance** (frames 6, 2, 1, 5). Lift the shadowed scenes, add distance haze where there is none, and remove the milky veil near the sun. Shadow the grass in shadow (frame 6).

## Verdicts

- **The dragon's surface and anatomy:** The dragon is the best asset in the game. From the side (frame 2) it reads as a creature. From behind, the head is a shaggy mop, and the wing membrane looks like glossy plastic.
- **The dragon's pose:** The flight pose is acceptable. The landed wings hang half-open like a cape (frame 3), and the hind legs dangle limply in the glide.
- **The terrain's large shapes:** Valley shape and ridgelines are acceptable at distance (frame 8). Up close the slopes are smooth cones with no erosion structure.
- **Rock and cliff surfaces:** They fail. They are sand-coloured noise with pebble decals, and there is no cliff anywhere.
- **Trees:** They fail. They are low-poly, cartoon or lollipop shapes, uniformly scattered.
- **Ground cover:** It fails. The grass is sparse spiky cards on a bare green plane.
- **Water:** It is weak. The water is flat and dark with a tiling ripple, with no reflection, foam or shore detail.
- **Sky and clouds:** They are weak. The blue gradient is fine, but the clouds are isolated cotton-ball sprites, and frame 5 is the only passable one.
- **Light and atmosphere:** They are inconsistent. Haze works in frame 8, is absent in frames 1 and 3, is too milky in frame 2 and is murky in frame 6. There are no light shafts and no mist.
- **Colour grade:** It is inconsistent. It is cold and dark in frame 6, warm sand in frames 1 and 5, and greyed out in frame 2. None of them matches the target's golden warm-cool split.
- **Particles and fire:** They fail. The fire is a flat orange shape and the spray has square-edged sprites.
- **The HUD:** The plates and fonts are clean and consistent. The markers and labels overlap, clip at the screen edge and clutter the centre.

## Art style

The frames read as at least four styles: a semi-realistic dragon, low-poly stylised vegetation, smooth procedural clay terrain, and toy-like gate rings with cartoon clouds. The vegetation breaks the style most, especially the grass and the lollipop trees. A close second is the terrain texture, which looks like sand under a realistic dragon.
