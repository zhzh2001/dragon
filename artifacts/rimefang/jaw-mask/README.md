# Rimefang jaw mask

`tools/skeletons/rimefang.json` is `winged-quadruped.json` with one key added,
`jaw_mask`. The bone list, parents, `reference_bounds` and `wing_field` are
byte-identical, so Embercrest and Blightmaw (still on the shared file) are
unaffected. The mask is a property of the head, not the anatomy.

## The fault

Rimefang had been rebuilt with `--jaw-mask none` (heat weights) on the theory
that a parted mouth gives heat something to split on. Heat gave the `jaw`
bone the *snout*: jaw weight 0.9-1.0 over the nose, 3813 total on `jaw` (plus
165 on `jaw_tip`) against 1322 on `head`. `jaw_rest_deg -24` therefore lifted
the nose like a lid (`closed-before.png` against `bind.png`) and the breath,
which rotates the jaw bone open, pushed the snout down onto a fixed mandible.

## Measurement

Everything is in canonical skeleton space (the coordinates the JSON is
authored in: `canonical(co) = (co - bone_fit_bias) / bone_fit_scale` from
`build_stats.json`, +Y forward, Z up), read off the previous raw rig with the
scripts kept in the session scratchpad (`glbskin.py`, `columns.py`, `gap.py`,
`slice.py`, `evalmask.py`).

Vertical rays through the head, hit z with the face normal's sign (`^` up,
`v` down), so a column through an open mouth reads chin `v`, mandible top /
lower tooth `^`, palate / upper tooth `v`, snout top `^`:

```
x +0.000 y 0.320: 0.3319v  0.3508^  0.3545v  0.3990^
x +0.000 y 0.340: 0.3236v  0.3438^  0.3516v  0.3904^
x +0.000 y 0.360: ... 0.3313^  0.3467v  0.3637^
x +0.000 y 0.370: 0.3099v  0.3198^  0.3440v  0.3594^
x +0.000 y 0.380: 0.3416v  0.3610^            (mandible has ended; snout only)
x -0.020 y 0.320: 0.3353v  0.3904^            (closed: cheek)
x -0.030 y 0.330: 0.3347v  0.3538^  0.3546v ...(cavity 0.8 mm: inner corner)
```

Per y, the widest mandible top and the lowest palate over |x| <= 0.03 give the
window the split line must pass through:

| y | mandible top max | palate / lip min | window |
|---|---|---|---|
| .32 | .3508 | .3538 | 3.0 mm |
| .33 | .3496 | .3524 | 2.8 mm |
| .34 | .3471 | .3504 | 3.3 mm |
| .35 | .3398 | .3479 | 8 mm |
| .36 | .3313 | .3436 | 12 mm |
| .37 | .3214 | .3409 | 20 mm |
| .38 | -- | .3381 | line must be below |
| .39 | -- | .3322 | line must be below |

The mouth line is convex: slope about -0.16 per unit y behind the teeth
(.32 to .34) and about -0.6 at the chin (.34 to .37). A single straight line
through the front windows dips below the rear mandible top by 1.5-3 mm, and
one through the rear windows runs into the upper lip at .36. Hence the
three-point line, which the rigger's plane mask now accepts:

```
"line": [[0.30, 0.3555], [0.34, 0.3485], [0.37, 0.331]]     (extrapolated past .37)
```

At .32 it passes at .3520, at .33 .3503, at .35 .3427, at .36 .3368, at .38
.3250, at .39 .3190 -- inside every window and under the snout tip.

Other keys:

- `hinge_blend [0.295, 0.335]`: hinge (jaw bone head) at y .285, cavity
  begins at .315, cheek closed to .33 at |x| .02-.03. Same offsets from the
  hinge as Embercrest's mask.
- `x_max 0.09`: head half-width is .084 at the hinge and under .045 ahead of
  the teeth; nothing else lives at y > .295 above `z_min`.
- `z_min 0.29`: the head is carried over the chest. Without a floor the box
  held 697 chest, upper-arm and forefoot vertices (z <= .215, y .295-.314); at
  .22 it still reached 110 neck-base vertices up to z .29. The chin bottom is
  .303, so .29 leaves only head, jaw and throat in the box (646 head, 2707
  jaw, 79 jaw_tip, 2 neck_03 heat-dominant vertices).
- `reclaim_above true`: moves jaw/jaw_tip weight above the line onto head.
  The plane rule alone only adds jaw below the line; here the whole snout
  had to be taken back. 1235 vertices reclaimed.

Numerically (evalmask.py, before rebuilding): jaw weight 3978 -> 2027, the
split cuts no mesh edges ahead of y .355 (it runs through air), and the only
edges it cuts ahead of the full-share point are 62 at |x| .01-.02, y
.335-.355 -- the inner cheek wall where the cavity pinches to under 3 mm and
any cut passes through flesh.

## Rebuild

```sh
blender --background --factory-startup --python tools/rig_embercrest_candidate.py -- \
    --input assets/rimefang-cand-oneshot.glb --stem rimefang \
    --skeleton tools/skeletons/rimefang.json --keep-uvs --target 80000
mv assets/rimefang.glb assets/rimefang-raw.glb
.venv/bin/python tools/repair_model_materials.py assets/rimefang-raw.glb -o assets/rimefang.glb
```

The shipped file is the raw rig through the material repair with default
arguments -- confirmed by repairing the previous raw and getting the previous
shipped `.glb` byte for byte. `glb_bytes` 48264412, `ground_offset`
5.010861505489797 and `max_weight_sum_error` 5.2e-08 are unchanged to the
digit; `.glb.flight.cfg` is unchanged.

## Verification

- Against the backup: 62 joints, same names and order; inverse bind matrices
  identical (max diff 0.0); node transforms identical; index buffer identical;
  positions within 1.2e-07. Outside the mask box **zero** vertices changed
  weight; inside it 3452 did, and the only bones touched anywhere are `head`,
  `jaw`, `jaw_tip`, `neck_02`, `neck_03`. Image payloads: base colour
  identical, ORM/normal differ slightly (the repair's wet-mouth zone follows
  jaw/head ownership).
- Engine: `mapped rig: neck 3, tail 8, wing root 3/3, fingers 3/3, legs 3/3,
  front legs 3/3, feet 4, jaw jaw`; `ctest` 11/11.
- `bind.png` / `closed-before.png` / `closed-after.png`: `--studio 0 --inspect
  90 4 0 --inspect-head` with `jaw_rest_deg -24, jaw_open_deg 24` appended to
  a scratch copy of the profile. Before, the nose lifted; after, the snout is
  where the bind pose has it and the lower jaw has risen to meet it.
- `attack-sweep-new-over-old.png`: `--studio 8`, frames 60..480 in steps of
  60, new glb top row, old bottom row, original profile. At 180-240 the new
  rig gapes with the mandible down; the old one clamps.
  `attack-f240-open.png` / `attack-f240-before.png` are the full-size frame.
- Front flap (`--studio 1 --frames 200 --inspect 180 20 5`) differs from the
  old glb by 0.6 pixels at 2% fuzz: wings and legs untouched.
- `sections-left.png` / `sections-right.png`: cross-sections at x = 0 .. +-0.06
  coloured by the *old* jaw weight (red) / head (green), with the head (yellow)
  and jaw (magenta) bones -- the picture the windows were read against.
