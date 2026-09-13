# Tidewrack jaw mask

`tools/skeletons/tidewrack.json` is `winged-biped.json` with one key added,
`jaw_mask` (and the `name`/`comment` rewritten to say so; the obsolete
`jaw_mask_comment` that explained the null is dropped). The bone list,
parents, `reference_bounds`, `wing_prefixes` and `wing_field` are
byte-identical, so Stormsail (still on the shared file) is unaffected. The
mask is a property of the head, not the anatomy.

## The fault

Tidewrack was built with `"jaw_mask": null` (heat weights) on the theory that
its parted mouth gives heat something to split on. Heat did split it, but in
the wrong place: the mandible is jaw (chin 0.9-1.0), and so is the *tip of
the snout* -- the upper jaw ahead of the teeth carried 0.5 jaw, 250 units in
all -- plus a smear of 0.1-0.5 jaw over the skull top, cheek and jowl
*behind* the hinge (1047 units at y < .31). Totals on the old file: `jaw`
3534 + `jaw_tip` 58 against `head` 9527. So `jaw_rest_deg -22` flipped the
nose up into a lump while the mandible barely rose (`closed-before.png`
against `bind.png`), and the breath, which rotates the jaw bone open, bent
the snout tip down onto a fixed lower jaw (`breath-f290-before.png`).

## Measurement

Everything is in canonical skeleton space (the coordinates the JSON is
authored in: `canonical(co) = (co - bone_fit_bias) / bone_fit_scale` from
`build_stats.json`, +Y forward, Z up), read off the previous shipped rig
with the scripts kept in the session scratchpad (`glbskin.py`, `head.py`,
`sections.py`, `evalmask.py`, `ownership.py`). The wyvern carries its head
high: head z .52-.68, hinge (jaw bone head) at y .30, z .583, and the
chest/neck top is below z .50 ahead of y .31.

Vertical rays through the head, hit z with the face normal's sign (`^` up,
`v` down), so a column through the open mouth reads chin `v`, mandible top /
lower tooth `^`, palate / upper tooth `v`, snout top `^` (full table in
`rays-old.txt`):

```
x +0.000 y 0.310: 0.5666v  0.6549^                        (solid: cheek)
x +0.000 y 0.320: 0.5645v  0.5916^  0.6012v  0.6474^      (cavity opens)
x +0.000 y 0.340: 0.5581v  0.5814^  0.5988v  0.6324^
x +0.000 y 0.360: 0.5474v  0.5681^  0.5901v  0.6179^
x +0.000 y 0.380: 0.5447v  0.5517^  0.5792v  0.6037^
x +0.000 y 0.390: 0.5746v  0.5980^                        (mandible has ended)
x +0.000 y 0.410: 0.5681v  0.5879^
x -0.020 y 0.330: ...     0.5614v  0.6269^                (closed: cheek)
x -0.020 y 0.340: 0.5596v  0.5792^  0.5902v  0.6200^
x -0.030 y 0.330: 0.5641v  0.5871^  0.5894v  0.6195^      (cavity 2 mm: inner corner)
x -0.010 y 0.400: 0.5702v  0.5839^  0.5863v  0.5918^      (nostril)
```

Per y, the highest mandible top and the lowest palate over |x| <= 0.02 give
the window the split line must pass through:

| y | mandible top max | palate / lip min | window |
|---|---|---|---|
| .32 | .5919 | .5948 | 3 mm |
| .33 | .5891 | .5978 | 9 mm |
| .34 | .5828 | .5892 | 6 mm |
| .35 | .5747 | .5853 | 11 mm |
| .36 | .5681 | .5802 | 12 mm |
| .37 | .5618 | .5795 | 18 mm |
| .38 | .5522 | .5752 | 23 mm |
| .39 | -- | .573 | line must be below |
| .40 | -- | .5697 | line must be below |
| .41 | -- | .568 | line must be below |

The mandible hangs steeply (slope about -0.66 from .32 to .38, it is
sculpted gaping) while the palate is shallow (-0.30), so the windows widen
toward the front and the line is shallow behind the teeth, steep along the
gape: three points, -0.27 then -0.56, extrapolated at both ends.

```
"line": [[0.30, 0.5985], [0.335, 0.589], [0.38, 0.5637]]
```

At .32 it passes at .5931, .33 .5904, .34 .5862, .35 .5806, .36 .5750, .37
.5693, .38 .5637, .39 .5581, .41 .5468 -- inside every window and under the
snout tip.

Other keys:

- `hinge_blend [0.31, 0.35]`: hinge at y .30, cavity opens at .315-.32,
  cheek closed to .33 at |x| .02 and to .34 at |x| .03. Same offsets from the
  hinge as Embercrest's, Rimefang's and Ironroot's masks.
- `z_min 0.51`: the head is carried over the chest. The neck top at y .31 is
  .5004, the chin tip (y .37) is .5261; .51 sits between. Without a floor the
  box would hold the chest and neck base.
- `x_max 0.12`: the head frills reach |x| .101 at y .26-.28; nothing but
  head, jaw and neck vertices live in the box (checked by dominant bone).
- `y_min 0.26` and `reclaim_above true`: these two differ from Rimefang's
  recipe (hinge + .01) and are the measured part. Heat's jaw weight behind
  the hinge, by height (y .26-.31):

  | z | mean jaw | max | note |
  |---|---|---|---|
  | .52-.55 | .26-.41 | .61 | jowl / throat |
  | .56-.57 | .46-.49 | .98 | hinge height |
  | .58 | .37 | .62 | |
  | .59 | .28 | .42 | |
  | .60 | .20 | .25 | line runs here |
  | .61-.65 | .04-.14 | .23 | skull top, crest base |

  The first segment extrapolated backward runs at z .601 (y .29) to .609
  (y .26), where the heat jaw weight is already at or below 0.2, so
  `reclaim_above` there takes the skull top and crest base back from the jaw
  bone for the smallest possible step across the boundary; the jowl under
  the hinge (0.3-0.66) is left to heat because any boundary through it would
  be a larger step, and a throat that follows the mandible is not wrong.
  Starting the box at the head bone (y .265, rounded to .26) rather than at
  hinge + .01 = .31 is what frees the skull: with `y_min .31` (the Rimefang
  recipe, previewed as "v1") the snout was fixed but 254 units of jaw
  weight stayed on the skull top and the patch between the eye and the
  crest bobbed with the jaw.

Both variants were previewed by patching the mask into the old file's skin
(`patchglb.py`) and rendering before any Blender run, and compared
camera-free by rotating the jaw bone 22 degrees about the hinge and
measuring the linear-blend displacement per region (mm are thousandths of a
canonical unit, about 1.4 cm in the world):

| region | old: jaw wt / mean disp / max | new: jaw wt / mean / max |
|---|---|---|
| snout (y > .385) | 250 / 18.3 / 33.3 mm | 0 / 0 / 0 |
| palate, upper jaw (y .32-.385 above the cavity) | 130 / 2.3 / 9.9 | 0 / 0 / 0 |
| skull top behind hinge (y .26-.31, z > .60) | 254 / 1.8 / 4.2 | 35 / 0.2 / 4.1 |
| cheek and jowl behind hinge (z .51-.60) | 792 / 3.9 / 12.1 | unchanged |
| neck (y .22-.26) | 457 / 2.0 / 7.0 | unchanged |
| mandible (y .31-.385, below the line) | 1545 / 9.2 / 35.8 | 1322 / 8.7 / 35.8 |

The split cuts no mesh edges through air ahead of y .34; the edges it cuts
are the closed cheek in the blend zone (y .31-.33, 44+47+62 at |x| up to
.047, share 0.05-0.64), the inner cheek wall where the cavity pinches
(y .34-.36, 15+9+8 edges at |x| .017-.024), and 92 edges along the rear
extension at |x| .04-.08 where jaw weight steps from about 0.2 to 0.

## Rebuild

```sh
blender --background --factory-startup --python tools/rig_embercrest_candidate.py -- \
    --input assets/tidewrack-cand-oneshot.glb --stem tidewrack \
    --skeleton tools/skeletons/tidewrack.json --keep-uvs --target 80000
mv assets/tidewrack.glb assets/tidewrack-raw.glb
<venv>/bin/python tools/repair_model_materials.py assets/tidewrack-raw.glb -o assets/tidewrack.glb
```

The shipped file is the raw rig through the material repair with default
arguments -- confirmed first by repairing the previous raw and getting the
previous shipped `.glb` byte for byte. The rigger reports
`JAW_MASK_PLANE_RECLAIMED 1819` (Blender vertices; 3967 after the glTF
export splits them). `build_stats.json` changes only in `binding`; raw
`glb_bytes` 47560680, `ground_offset` 6.641143969038469,
`max_weight_sum_error` 5.2e-08, `unweighted_vertices` 0, `bone_fit_*` and
`source_sha256` are unchanged to the digit; `.glb.flight.cfg` is unchanged.
Weight totals: `head` 9527 -> 10498, `jaw` 3534 -> 2628, `jaw_tip` 58 -> 0
(the plane assigns the mandible to `jaw` alone, as on Rimefang). The repair
ran with `trimesh.ray.ray_pyembree`; zone coverage membrane 25.7 %, keratin
7.9 %, belly 13.1 %, mouth 0.4 %. Shipped size 51843488 (was 51844488: the
ORM and normal PNGs differ slightly, base colour identical).

## Verification

- Against the previous file: 55 joints, same names and order; inverse bind
  matrices identical (max diff 0.0); node transforms identical. Inside the
  mask box 5373 vertices changed weight and the only bones touched are
  `head`, `jaw`, `jaw_tip` and `neck_03` (113, the hinge-blend zone).
- **Outside the box this rebuild is not a byte-for-byte reproduction, and
  that is independent of the mask.** 23614 wing, chest, `neck_01` and thigh
  vertices differ from the Sep 11 file (|dw| > 0.05 on 13409, max 0.87), and
  the wing-levelling bake follows them, so 289 of 80000 triangles and the
  wing vertex positions (max 0.034 glTF units) differ too. Rebuilding with
  the untouched `winged-biped.json` into a throwaway stem gives *exactly the
  same* 23614 differences against the old file, and a second run of this
  skeleton reproduces this build to the digit, so the rigger is
  deterministic today and the Sep 11 build came from a different toolchain
  state -- not one I could find: the rigger's only diff since ed2abe0 is the
  jaw-plane code (dead when `jaw_mask` is null), `winged-biped.json` has not
  changed since Sep 10, the source hash matches, and Blender is the same
  5.2.1 install from Aug 29. Rimefang reproduced yesterday because its
  original was built in the same state as yesterday's rigger; Tidewrack's
  was not. Rendered like for like (same `.rig.cfg`), the drift is small:
  side glide (`--inspect 90 14 6`) 3246 px of 921600 differ at 24/255,
  front flap (`--studio 1 --frames 200 --inspect 180 20 5`) 5106, dive
  (`--studio 5`) 3520, grounded stance (`--studio 9`) 11394 side and 16200
  front, all in the membrane pleats and edges and the head
  (`side-old-vs-new.png`, `flap-diff-new-over-old.png`,
  `ground-*-old-vs-new.png`). The previous shipped and raw files are kept
  beside the new ones as `assets/tidewrack.glb.bak` and
  `assets/tidewrack-raw.glb.bak` (gitignored). If the wing drift matters
  for the stance tuning, the alternative is to splice only the in-box
  weights into the old file -- the head vertices are index-aligned between
  the two (positions within 6e-8 there).
- Engine: `mapped rig: neck 3, tail 11, wing root 3/3, fingers 4/4, legs
  3/3, front legs 0/0, feet 2, jaw jaw`; `ctest` 11/11 (the anim suite's
  roster jaw test included).
- `bind.png` / `closed-before.png` / `closed-after.png`: `--studio 0
  --frames 150 --inspect 90 3.5 0 --inspect-head` with the shipped profile
  (`jaw_rest_deg -22`). Before, the snout tip flipped up into a lump over a
  mandible that hardly moved; after, the snout is where the bind pose has
  it and the lower jaw has risen to meet the upper teeth.
- `breath-f290-before.png` / `breath-f290-after.png`: `--studio 8 --frames
  290`. Two traps here: `--attack` does nothing in studio scenarios other
  than 8 (`studio_action` overrides the action), and at full breath the jaw
  angle is `jaw_rest + jaw_open = 0`, i.e. the bind pose, so old and new
  render identically at frame 240. The breath switches off at 4.5 s (frame
  270) and closes with a 0.2 s time constant, so frame 290 catches the
  mouth half closed: the old file bends the snout tip down onto a fixed
  mandible, the new one lowers the mandible under an intact snout.
- `head-sheet-old-over-new.png`: the three head crops side by side, old row
  over new.
- `sections-left.png` / `sections-right.png`: cross-sections at x = 0 ..
  +-0.05 coloured by the *old* jaw weight (red) / head (green), with the head
  (yellow), jaw (magenta), jaw_tip (cyan) bones and the chosen line (blue)
  -- the picture the windows were read against.
