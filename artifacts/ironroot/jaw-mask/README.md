# Ironroot jaw mask

`tools/skeletons/ironroot.json` is `heavy-quadruped.json` with one key
changed: `jaw_mask` was `null` and is now a measured plane. The bone list --
including the independently measured leg landmarks -- `reference_bounds` and
`wing_field` are byte-identical.

## The fault

`heavy-quadruped.json` set `jaw_mask: null` to escape Embercrest's
gap-following mask, which had welded Ironroot's mandible to the skull. Heat
weights on their own went the other way: the `jaw` bone owned the *snout*
(jaw weight 1.0 over the nose; 4546 total on `jaw` plus 255 on `jaw_tip`
against 1032 on `head`, and the chin itself only 0.2-0.7 jaw). Rotating the
jaw closed lifted the nose (`closed-before.png` against `bind.png`); the
breath pushed the snout down onto a mandible that did not move.

## Measurement

Canonical space is mesh space here (identity fit, `bone_fit_scale` 1). Vertical
rays through the head, hit z with the face normal's sign (`^` up, `v` down):

```
x +0.000 y 0.340: 0.2760v  0.3931^                       (solid: cheek/throat)
x +0.000 y 0.350: 0.2729v  0.3214^  0.3273v  0.3805^     (cavity opens)
x +0.000 y 0.370: 0.2729v  0.3097^  0.3295v  0.3647^
x +0.000 y 0.390: 0.2482v  0.2873^  0.3241v  0.3563^
x +0.000 y 0.400: 0.2576v  0.2822^  0.3202v  0.3521^
x +0.000 y 0.410: 0.3132v  0.3536^                       (mandible has ended)
x -0.010 y 0.360: 0.2796v  0.3152^  0.3189v  0.3616^
x -0.020 y 0.360: ... 0.3200^  0.3237v  0.3616^
x -0.030 y 0.350: 0.3094v  0.3280^  0.3316v  0.3427^ ... (inner corner)
```

| y | mandible top max | palate / lip min | note |
|---|---|---|---|
| .31-.345 | solid | solid | cheek; hinge-blend zone |
| .35 | .3214 (.328 at x -.03) | .3273 | corner overlaps by 0.7 mm |
| .36 | .3200 | .3189 | 1 mm overlap at the inner corner |
| .37 | .3097 | .3222 | |
| .38 | .2984 | .3196 | |
| .39 | .2895 | .3123 | |
| .40 | .2842 | .3088 | |
| .41 | -- | .3059 | line must be below |
| .42 | -- | .304 | line must be below |

The windows lie on one straight line: slope -0.63.

```
"line": [[0.35, 0.3255], [0.40, 0.294]]
```

At .36 it passes at .3192, .37 .3129, .38 .3066, .39 .3003, .41 .2877 --
inside every window except the two sub-millimetre overlaps at the inner
mouth corner, which no x-independent line can satisfy and which sit in the
blend zone.

Other keys:

- `hinge_blend [0.311, 0.351]`: hinge (jaw bone head) at y .301; the cheek is
  solid to .345, so the stretch of opening is spread over that flesh. Same
  offsets from the hinge as Embercrest's mask.
- `x_max 0.13`: head half-width is .124 at the ears; nothing else lives at
  y > .311 above `z_min`. The forefeet are behind y .30 and below z .05.
- `z_min 0.23`: the chin bottom is .247 and the neck is behind the head, not
  under it -- the box held no vertex outside head/jaw/neck ownership.
- `reclaim_above true`: 1271 snout vertices had their jaw/jaw_tip weight moved
  to head.

Numerically (evalmask.py, before rebuilding): jaw weight 4801 -> 2519; the
split cuts no mesh edges ahead of y .371 and the cuts behind that are the
closed cheek (y .311-.351, the blend zone) and the inner corner.

## Rebuild

```sh
blender --background --factory-startup --python tools/rig_embercrest_candidate.py -- \
    --input assets/ironroot-cand-oneshot.glb --stem ironroot \
    --skeleton tools/skeletons/ironroot.json --keep-uvs --target 80000
mv assets/ironroot.glb assets/ironroot-raw.glb
.venv/bin/python tools/repair_model_materials.py assets/ironroot-raw.glb -o assets/ironroot.glb
```

Default repair arguments, confirmed by repairing the previous raw and getting
the previous shipped `.glb` byte for byte. `glb_bytes` 50058168,
`ground_offset` 3.4155143512971433 and `max_weight_sum_error` 5.2e-08 are
unchanged to the digit; `.glb.flight.cfg` is unchanged.

## Verification

- Against the backup: 62 joints, same names and order; inverse bind matrices
  identical (max diff 0.0); node transforms identical; index buffer identical;
  positions within 6e-08. Outside the mask box **zero** vertices changed
  weight; inside it 4885 did, and the only bones touched anywhere are `head`,
  `jaw`, `jaw_tip`, `neck_01`, `neck_02`, `neck_03` (the neck share heat had
  smeared into the jowls, replaced by the jaw/head split as the Embercrest mask
  also does). Base colour identical; ORM/normal differ slightly.
- Engine: `mapped rig: neck 3, tail 8, wing root 3/3, fingers 3/3, legs 3/3,
  front legs 3/3, feet 4, jaw jaw`; `ctest` 11/11.
- `bind.png` / `closed-before.png` / `closed-after.png`: `--studio 0 --inspect
  90 4 0 --inspect-head` with `jaw_rest_deg -24, jaw_open_deg 24` appended to
  a scratch copy of the profile. Before, the nose lifted; after, the snout
  stays and the lower jaw closes against the upper teeth.
- `attack-sweep-new-over-old.png`: `--studio 8`, frames 60..480 step 60, new
  glb top row, old bottom row, original profile. At 180-240 the new rig gapes
  with the mandible down; the old one clamps shut around the flame.
  `attack-f240-open.png` / `attack-f240-before.png` are the full-size frame.
- Front flap (`--studio 1 --frames 200 --inspect 180 20 5`) differs from the
  old glb by 51 pixels at 2% fuzz: wings and legs untouched.
- `sections-left.png` / `sections-right.png`: cross-sections at x = 0 .. +-0.06
  coloured by the *old* jaw weight (red) / head (green), with the head (yellow)
  and jaw (magenta) bones.
