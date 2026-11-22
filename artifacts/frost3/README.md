# Frost3

Frost3 has 80,000 triangles, 66 joints, four legs and four ribs per wing. The engine mapped every chain.

```text
mapped rig: neck 3, tail 8, wing root 3/3, fingers 4/4, legs 3/3, front legs 3/3, feet 4, jaw jaw
```

## Fit and motion

I measured the turned source mesh with +Y forward and +Z up. The central side projection excludes vertices beyond |X| 0.12.
I located the heels and toes from below. I matched each rib to the same tip in front, side and overhead projections.
The wrist sits at (0.231, 0.046, 0.394). Both the wing arm and the inner web connect to the shoulder.
The bind preserves their source positions. [Measured joints](landmarks.jpg)

At Y 0.339, the lower jaw ends at Z 0.3334 and the palate starts at 0.3413.
The jaw weights follow this gap. Beyond Y 0.332, connected surfaces carry their own teeth.
The crown follows the skull. The last tail bone carries all 3,070 exported vertices beyond Y −0.54.

The wing selection follows connected edges inward from each tip. The inner cut starts at |X| 0.082 below Z 0.195.
It slopes inward to |X| 0.05275 at Z 0.260. Wing influence increases across another 0.028 units.
The wing-only check compares all pairs of 41 poses. All 50,169 body vertices traveled 0.00000000 m.

The first build put the mesh and skeleton in opposite frames. The second stretched several back spikes during landing.
The third assigns each projecting point to the bone near its blade's base. The broad stretching disappeared.
At least three small tail spines still span neighboring bones, near Y −0.27, −0.34 and −0.41.
Frost3 falls short of the single-bone requirement for those spines after three builds.

## Size and profiles

At a 19 m span, Frost3 measured 17.91 m from snout to tail. I set `model_span_m` to 14.45 and `ground_offset` to 2.8728 m.
Frost3 now measures 13.62 m, or 95.7% of the 14.24 m hero. [Hero at the same camera](hero.jpg)

I copied all three Frost2 profiles, including the hero recovery fold and the frost breath.
The original beat crossed the centerline by 1.22 m. With 24° up and −82° down, the closest rib cleared it by 0.029 m.
The five-second dive probe found zero wing vertices inside the body. The standing sole heights differ by 0.021 m on level ground.
The resting jaw angle is −16°. The attack adds 25°.

## Likeness and materials

The generated ruff has fewer, thicker blades with rounded tips. The face has smaller eyes and less scale detail.
The near wing covers more of the hip in profile. Its four scallops remain distinct from the front.
The narrower tail spray has fewer needles. The generated arm and inner membrane meet inside the shoulder outline. [Sheet and bind pose](likeness.jpg)

The first bind render looked blue-grey. I lifted the source base colour before repairing the material.
For normalized sRGB values, the grade raises R, G and B to powers 0.57, 0.52 and 0.55.
The blend rises linearly from zero at luminance 0.16 to full strength at 0.44.
The vertex-sampled median rose from RGB (145, 162, 186) to (182, 199, 212).
The lit torso reads ivory with pale blue above. The ruff and membrane stay greyer than the sheet under the specified sun.
Green and violet remain along the wing edges.

The material bake assigned 22.6% to membrane, 6.0% to hard tips, 12.1% to belly and 0.9% to mouth.
The hard-tip mask includes the ice, claws and teeth. Its roughness is 0.34. The repair preserved the graded base colour.

## Rebuild

Run these commands from the worktree root. The raw and repaired models use identical profiles.

```sh
/opt/homebrew/bin/blender -b --factory-startup --python-exit-code 1 --python tools/frost3_rig.py
mv assets/frost3-raw.glb build-rig-cache/frost3-ungraded.glb
build-rig-cache/venv/bin/python tools/frost3_grade.py build-rig-cache/frost3-ungraded.glb assets/frost3-raw.glb
build-rig-cache/venv/bin/python tools/repair_model_materials.py assets/frost3-raw.glb -o assets/frost3.glb --bake-size 1024 --rough-keratin .34
/opt/homebrew/bin/blender -b --factory-startup --python-exit-code 1 --python tools/frost3_flapcheck.py
```

I inspected the 18 requested frames at 1920 × 1080. The shoulder stays joined through the beat.
The inner web forms a sharp pleat during the standing fold. The tail needles remain separate in the dive and chase views.
Grass covers the toe tips in the landing views. The additional standing view shows the feet. [Engine poses](views.jpg) · [Standing feet](feet.jpg)

Three builds and the engine inspection took about 13 minutes.

## After the job (the main session, 2026-11-22)

The glide stood 40 degrees up: the bind keeps the sheet's raised wings. `wing_rest_deg -22` in the rig profile lowers the wing in flight.
The flight profile's stroke is measured from that rest now: 46 up and -60 down, the same top and bottom as 24 and -82 from the bind.

The first grade left the hide grey-lilac with a beige belly in the game's sun. `tools/frost3_cool.py` runs on the built models, raw and repaired:

```sh
build-rig-cache/venv/bin/python tools/frost3_cool.py assets/frost3-raw.glb assets/frost3-raw.glb
build-rig-cache/venv/bin/python tools/frost3_cool.py assets/frost3.glb assets/frost3.glb
```

The base colour's median went from 182,199,212 to 166,198,224. Renders: `artifacts/elemental-round3/`. The record: `docs/MODEL_GENERATION.md`, "The third round modelled".
