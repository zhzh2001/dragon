# Blind critic, round 3 (2026-11-15, 16:15)

The same eight views as rounds 1 and 2, and a ninth, `9-breath.jpg`
(`--frames 90 --breathe --inspect 70 26 6 --hide-ui`), on commit 55300c8, to
an Opus agent that saw only the renders in this folder and the two slice
targets. It was told that the white streaks are wind lines, that the rings
are course gates and that the stone cage in frame 7 is a training target.
It did not see rounds 1 and 2. It was asked for a score a frame, where 5 is
`slice-target-floor.png` and 10 is `slice-target-ceiling.png`.

Since round 2: cliff bands on the walls, the braided river, the ward stone,
the breath's colour ramp, the spray as wisps and dashes.

The report follows as the critic wrote it.

# Showcase critique, round 3

## 1. Overall state against target-floor

None of the nine frames reaches target-floor. Target-floor is a sunlit, painterly-realistic valley. It has textured cliffs, dense varied forest, a braided gravel river, aerial depth and a hero dragon with readable scales. The renders share its layout: a valley, a river, a forest and a red dragon framed from behind. They fall short in surface detail and material. The cliffs are smooth, stepped terraces with a soft smudged texture. The trees are small stylised cards. The ground is flat green with noise. The fire and spray are soft blobs. The dragon is the strongest asset, and its hide and silhouette come closest to the targets. The world around it reads as a stylised low-poly game. Frames 1, 2 and 8 come closest. Frames 3, 6 and 7 are furthest away.

## 2. Frames

### 1-chase.jpg: 3.5

- The eye goes first to the gold ring and reticle in the upper centre (x 790-1135, y 260-600), then to the dragon. For a course view that is right. The dragon sits low and dark under the HUD plate, so it reads second to the ring.
- Gaps to target-floor:
  - Left cliff (x 0-560, y 0-560): a smooth tan slab with faint horizontal ledges. There is no rock structure, cracks or scree. Major.
  - Far wall (x 560-1500, y 300-560): a row of identical stepped battlements with grey flat faces. The target has jagged snow peaks with depth. Major.
  - Right wall (x 1150-1920, y 0-560): one dark flat plane in shadow with no detail. Major.
  - Forest (x 0-760, y 520-1080): uniform lollipop cards with one size and one shape. The target has dense layered conifers and broadleaf trees. Major.
  - The meadow on the right (x 1100-1920, y 700-1000) is flat green with sparse dots. Minor.
  - No haze between the near forest and the far wall. The depth reads flat. Major.
  - Dragon (x 880-1060, y 580-860): back-lit and dark. The scales do not read. The target shows a warm, sunlit hide. Major.
- Defects:
  - Several thin grey ellipses sit inside the ring (x 800-1010, y 390-550). They overlap and read as ghosted geometry, not as a clear next gate. Minor.
  - The "259 m" label is clipped by the ring's right edge (x 1080-1130, y 430-445). Minor.
  - The cloud at the top left (x 260-540, y 0-120) is a soft blob cut by the frame edge. It reads as a sprite, not as volume. Minor.

### 2-hero-close.jpg: 3.5

- The eye goes first to the sun disc (x 1020-1075, y 35-85) and its haze, then to the dragon. That is wrong for a hero frame. The dragon must lead.
- Gaps to target-floor:
  - Mountains (x 0-1920, y 0-480): faceted low-poly peaks with smooth gradient shading and no rock texture. The far peaks (x 880-1750, y 70-200) are pale cut-out shapes. Blocker for a hero shot.
  - Valley floor (x 1100-1920, y 600-1080): a flat green plane with lollipop trees and long hard shadows. There is no ground cover. Major.
  - River (x 0-1100, y 640-1080): a smooth cyan strip on a pale sand band. There are no rocks, gravel bars or current. Major.
  - Dragon hide: it reads at this distance. Scales and spines are visible on the tail (x 310-900, y 640-800). The flank (x 960-1250, y 500-700) is a smeared dark red with low contrast. Major.
- Defects:
  - The near wing (x 1100-1920, y 170-500) is a flat, glossy, plank-like slab. The membrane has no veins or translucency, and the wing exits the frame at the top right. Major.
  - The far wing (x 780-1090, y 325-540) appears to come out of the dragon's back, in front of the neck. Its root does not read as attached to a shoulder. Major.
  - At the valley end (x 1080-1440, y 400-480) the terrain stops at a flat horizontal edge with an empty plain behind it. It reads as the end of the map. Major.
  - A large milky haze wash covers the left half (x 0-900, y 0-600) and flattens the forest to grey-green. Minor.

### 3-landed.jpg: 2.5

- The eye goes first to the gold ring in the sky (x 1030-1450, y 0-250). That is wrong. A landed frame must lead with the dragon, which here is a back view framed by grass.
- Gaps to target-floor:
  - Ground cover (x 0-1920, y 590-1080): large flat grass cards and bushes like crumpled paper, each with hard polygon edges. At this distance it looks like a low-poly asset pack. Blocker.
  - Mid trees (x 1030-1920, y 300-620): flat cross-card crowns with visible card edges. The tree at x 1040-1190, y 410-610 is a thin stick with one flat crown. Major.
  - Left cliff (x 0-830, y 0-500): a smooth tan slab with faint strata and two pasted rock patches (x 490-560, y 285-330 and x 630-680, y 300-340). Major.
  - Far wall (x 840-1480, y 280-500): battlement shape with stepped grey faces. Major.
- Defects:
  - The dragon's wings (x 780-1140, y 580-890) are raised up and forward beside the head like a bat's elbows. From behind this reads as a folded umbrella, not as a resting dragon. Major.
  - Grass cards cross through the dragon's legs and wings (x 800-1100, y 780-900). There is no contact shadow or footprint. The dragon does not read as standing in the meadow. Major.
  - Three grey rings hang in mid-air (x 1170-1320, y 255-490). Without the HUD they read as floating debris. Minor.
  - The dragon's head and mane (x 910-1010, y 570-650) are a dark, hairy blob from behind. Minor.

### 4-river.jpg: 3.0

- The eye goes first to the white spray blobs (x 370-1700, y 730-1080), then to the dragon. That is wrong. The spray covers the lower third and outweighs the dragon.
- Gaps to target-floor:
  - Cliff (x 120-1920, y 0-460): stacked flat terraces like a stepped pyramid, with a blurred rock texture. Major.
  - Gravel bank (x 0-620, y 560-1080 and x 1300-1920, y 580-760): a grey noise smear with no stones. The target has round cobbles and gravel bars. Major.
  - Water (x 600-1920, y 570-1080): flat teal with no reflection of the trees or the cliff, and no current. Major.
  - Riverside trees (x 900-1920, y 260-560): flat card crowns in a single row. Major.
- Defects:
  - The spray (x 370-1700, y 730-1080) is soft round white puffs with vertical white dashes. It reads as cotton wool and rain, not as a wake. Major.
  - A grey ring edge enters at the left edge (x 0-30, y 30-200). Minor.
  - Isolated tall grass blades stand alone on the gravel (x 130-160, y 650-740; x 1720-1880, y 560-640). Minor.

### 5-mountains.jpg: 2.5

- The eye goes first to the thick gold ring band across the top right (x 1080-1920, y 0-265). That is wrong. The ring cuts the subject of the frame.
- Gaps to target-floor:
  - Mountain wall (x 0-1920, y 250-880): rounded vertical ridges like melted wax. The shading is smooth with a smudged grey-green texture. There are no rock faces, ledges or gullies. The snow caps (x 300-950, y 255-360) are white paint on the tips. Blocker.
  - Valley floor (x 0-1920, y 880-1080): a flat green plane. Minor ground variation only. Major.
  - Tree line on the slope (x 700-1300, y 700-900): sparse dotted cards in a triangle. Major.
  - Sky: the large cloud (x 1060-1660, y 0-330) is the best cloud in the set, but the blue is a flat gradient. Minor.
- Defects:
  - A dark rectangular shadow block (x 480-740, y 690-880) at the foot of the wall has straight vertical edges. Minor.
  - Two rock props are pasted flat on the slope (x 1240-1290, y 425-450 and x 1545-1640, y 535-585). They read as stickers. Minor.
  - A tower prop and a reticle sit on the slope (x 920-1000, y 570-700). They are too small to read. Minor.

### 6-forest.jpg: 2.5

- The eye goes first to the bright yellow-green tree crowns at the back (x 1100-1920, y 0-290). That is acceptable for a forest view. The foreground gravel strip (y 760-1080) takes too much of the frame for what it holds.
- Gaps to target-floor:
  - Gravel (x 0-1920, y 760-1080): grey-pink noise with no stones and no scale. It reads as carpet. Major.
  - Meadow (x 0-1920, y 290-830): dark green with sparse single grass cards and paper-like bushes. There is no density or flowers. Major.
  - Forest (x 0-1920, y 60-290): uniform small cards, all one height, arranged in a band. Yellow crowns repeat the same sprite. Major.
  - The mountain above (x 0-1150, y 0-200) is a dark blur with no form. Minor.
- Defects:
  - The bushes (for example x 800-930, y 580-710; x 1120-1260, y 640-740) are faceted blobs with flat polygon sides. Major.
  - Long dark stripes cross the meadow (x 0-1800, y 330-760). Their source trees are not in frame, and they read as a pattern, not as shadow. Minor.
  - The whole frame is grey-blue and low-contrast. It reads as underexposed, not as mountain shade. Minor.

### 7-fire.jpg: 2.0

- The eye goes first to the cluster of markers at the centre (x 850-1130, y 430-710). The overlap makes it unreadable. The near cage (x 1130-1920, y 260-1080) then pulls the eye right. Wrong: no single target reads.
- Gaps to target-floor:
  - Training cage (x 1130-1920, y 260-1080): flat untextured brown polygons and round bolts with no material. It is the largest object in frame and the least finished. Blocker.
  - The left cliff and the far wall repeat the stepped battlement shape (x 0-700, y 0-560; x 720-1300, y 410-520). Major.
  - The forest (x 0-900, y 560-900) is uniform cards. Major.
- Defects:
  - Markers overlap: two rings, four corner brackets, three "BURNING" labels and distances "340 m", "?0 m", "92 m", "241 m" stack over each other (x 850-1160, y 430-710). The labels are unreadable. Blocker.
  - The fire at the centre (x 880-1050, y 500-700) is a bright orange blob over the dragon. It hides its shape. Major.
  - Orange glow spots float on and around the cage (x 1170-1800, y 280-900). They read as lens dirt, not as burning. Major.
  - Wind lines are thick and many here (x 0-1920, all over). They cross the HUD and the target and add noise in the frame that needs clarity most. Major.
  - A white streak from the left (x 0-800, y 540-640) points into the centre and reads as a contrail or a bug. Minor.

### 8-run.jpg: 3.5

- The eye goes first to the dragon at the centre (x 570-1350, y 600-880). That is right. The HUD plate at the top and the marker labels compete but do not take over.
- Gaps to target-floor:
  - Far wall (x 700-1920, y 290-560): stepped terraces in grey-tan. This is the most convincing wall in the set, but there is no crag and no snow. Major.
  - Left slope (x 0-750, y 0-650): smooth dark green-blue with no rock. Major.
  - Forest (x 0-1920, y 560-1080): dense here and better than in other frames, but uniform cards with no variation in height. Major.
  - River (x 760-1000, y 680-1080): a pale strip with no detail. Minor.
  - The dragon is back-lit and dark again. Wing membranes are flat orange. Major.
- Defects:
  - The tutorial text (x 540-1380, y 115-165) sits on the sky with no plate. It is two long lines of grey on blue and is hard to read. Minor.
  - "THE PASS 4124 m" (x 1745-1890, y 555-590) is clipped by the frame's right edge and overlaps a ring marker. Minor.
  - "RIVAL (at post) - FROST 1302 m" (x 1195-1410, y 545-560) has a wind line crossing the text. Minor.
  - A line runs from the aim reticle (x 635-685, y 475-500) to the rival marker. It reads as a stray stroke. Minor.

### 9-breath.jpg: 3.0

- The eye goes first to the fire jet (x 1180-1920, y 320-560), then to the dragon. That is right for a breath frame.
- Gaps to target-floor:
  - Fire (x 1180-1920, y 320-560): flat cel-shaded orange and cream blobs with hard edges. There is no core, no smoke, no heat glow on the dragon or the ground. Blocker for a fire showcase.
  - Slope (x 0-1920, y 0-450): a smooth tan wall with faint stripes and green bands. Major.
  - Meadow (x 0-1500, y 650-1080): a flat green plane with a fine ripple pattern. Major.
  - The dragon (x 700-1220, y 410-640) is small in frame. The hide reads well from the side: belly plates, the tail ridge and the head are clear. This is the best dragon read in the set.
- Defects:
  - The near wing (x 1020-1220, y 415-530) folds forward over the head and the jaw. It hides the mouth at the moment of the breath. Major.
  - The fire leaves the jaw with no visible source (x 1170-1210, y 470-500). It starts as a full-width jet. Minor.
  - The dragon casts no visible shadow on the ground below. It floats. Minor.

## 3. What to fix first

1. Rock and cliff surfaces: the smooth stepped terraces and the melted ridges are in almost every frame and set the low-poly read. Frames 1, 2, 3, 4, 5, 7, 8, 9.
2. Trees and ground cover: the flat card crowns, the paper bushes and the sparse grass cards. Frames 1, 3, 4, 6, 7, 8, and every frame with forest.
3. Atmosphere and depth: no aerial haze between the near and far planes in the chase frames, but a flat milky wash in frame 2. Frames 1, 2, 7, 8.
4. Marker clutter in combat: overlapping brackets and labels. Frames 7, 8.
5. Fire and spray particles: cel blobs and cotton puffs. Frames 4, 7, 9.
6. The training cage material. Frame 7.
7. Dragon lighting from behind (back-lit and dark) and the wing poses: the far wing root, the folded wings on the ground, the wing over the mouth. Frames 1, 2, 3, 8, 9.
8. Gravel and river: noise smear, flat water with no reflection. Frames 2, 4, 6.
9. The ring's size and placement in non-course frames. Frames 3, 5.
10. The visible map end at the valley head. Frame 2.

## 4. One-line verdicts

- Dragon surface and anatomy: the best asset in the set. Scales, spines and belly plates read from the side (frame 9). Membranes are flat and glossy, and the flank smears when back-lit.
- Dragon pose: the flight pose from behind is good. The far wing root (frame 2), the folded wings on the ground (frame 3) and the wing over the jaw while breathing (frame 9) are wrong.
- Terrain large shapes: valley layouts are right. The walls repeat one stepped-battlement form, and frame 5 shows melted vertical ridges.
- Rock and cliff surfaces: smooth and smudged, with no crags, cracks or scree. The largest gap to target-floor.
- Trees: uniform small cards with one height and one crown shape. Far from the target's dense layered forest.
- Ground cover: flat green plane, single grass cards and faceted bushes. Reads as low-poly.
- Water: flat teal with no reflection, no current and no gravel bars.
- Sky and clouds: a flat blue gradient with soft blob clouds. The large cloud in frame 5 is the best one. No cloud reads as volume.
- Light and atmosphere: direct sun reads, but there is little aerial depth. Frame 2 has too much milky haze, and frame 6 is underexposed.
- Colour grade: saturated and clean, but cooler and flatter than the warm, golden target.
- Particles and fire: cel-shaded fire blobs, cotton-wool spray, and orange glow spots that read as lens dirt. Furthest behind the targets with the rocks.
- HUD: a clean, consistent kit when the frame is quiet (frames 1 and 8). Marker overlap makes it fail in combat (frame 7).

## 5. One art style?

No. The frames show three styles:

1. A detailed, semi-realistic dragon (scales, spines, horns), close to the targets.
2. A stylised low-poly world: smooth terraced cliffs, card trees, faceted bushes and flat ground.
3. Cel and cartoon effects: flat-shaded fire blobs, cotton spray and soft cloud blobs, plus the untextured flat brown cage in frame 7.

The world breaks the style most. Every frame puts the realistic dragon against smooth low-poly terrain and card forest. The fire in frames 7 and 9 and the cage in frame 7 make the break worst.

## 6. Scores

| Frame | Score |
|---|---|
| 1-chase.jpg | 3.5 |
| 2-hero-close.jpg | 3.5 |
| 3-landed.jpg | 2.5 |
| 4-river.jpg | 3.0 |
| 5-mountains.jpg | 2.5 |
| 6-forest.jpg | 2.5 |
| 7-fire.jpg | 2.0 |
| 8-run.jpg | 3.5 |
| 9-breath.jpg | 3.0 |
