# Embercrest hide: a warmer, lighter texture set

The candidate is `out/embercrest-hide.glb` (not committed). It differs from
`assets/embercrest.glb` only in the pixels of its three images.

## Rebuild

```sh
/opt/homebrew/bin/python3.14 tools/hide/build_hide.py     # 30 s; writes out/embercrest-hide.glb
/opt/homebrew/bin/python3.14 tools/hide/check_mesh.py \
    ~/src/game-claude/assets/embercrest.glb out/embercrest-hide.glb
```

The build needs NumPy and Pillow only. Two builds gave the same file:
sha256 `8363e48bd069a178052f7e65916c6b785135fa2ca203586398fc648492b57bf7`.

The renders and numbers:

```sh
/opt/homebrew/bin/python3.14 tools/hide/make_key.py ~/src/game-claude/assets/embercrest.glb out/work/key
tools/hide/render_views.sh ~/src/game-claude/assets/embercrest.glb out/work/before
tools/hide/render_views.sh out/embercrest-hide.glb out/work/after
tools/hide/render_views.sh out/work/key/key-a.glb out/work/key-a
tools/hide/render_views.sh out/work/key/key-b.glb out/work/key-b
/opt/homebrew/bin/python3.14 tools/hide/measure.py out/work/before out/work/after \
    out/work/key-a out/work/key-b docs/concept/gaps/run-target.png out/compare
```

`render_views.sh` copies Embercrest's `.rig.cfg` and `.flight.cfg` beside a
model that has none. The engine finds a profile by the model's path, and
without the copy the candidate flies a different pose.

## What was wrong

The shipped base colour is dark and close to grey. On the back the median
albedo is 0.03 linear (sRGB 59,46,37); on the membrane it is 0.07. The engine
multiplies the light by the albedo before the tonemap, so the back rendered
near black. Back, limbs and wing bones sat within one stop of each other.

The engine also reads the red channel of the metallic-roughness image as
ambient occlusion (`shaders/skinned.hlsl`). The glTF material declares no
occlusion texture, but the generator's red channel is not white: its 5th
percentile is 0.27 on the head and 0.5 on the body. That darkened the ambient
light on the parts that were already darkest.

## What changed

Base colour, per texel, in linear light:

1. The luminance is split into a base (a 14-texel blur of its logarithm) and
   a detail ratio (scales, plate edges, wrinkles, veins).
2. The base goes through `0.78 * base ** 0.55`. This raises the dark back
   about 3.5 times and a light plate about 1.2 times.
3. The detail ratio is raised to the power 1.6 on the body and 1.2 on the
   membrane, then multiplied back. The lift alone would flatten the scales.
4. The hue moves 66% of the way from the texture's own toward a rust ramp:
   deep red-brown in creases, rust, orange on ridges.
5. Four regions come from the mesh, not from painting. The membrane (skin
   weights on the wing fingers, the engine's own rule) is lifted 1.5 times
   more and tinted orange. The belly plates (already light) get a 1.3 gain
   and a warm cream tint. The horns and claws (bind-pose position on the head
   and the toes) keep their colour with a 1.25 gain, so they stay dark.
6. The padding between UV charts is filled again from the charts beyond three
   texels. The detail power had turned its smears into bright specks, and the
   mip chain reads them.

Normal map: x and y times 1.5, renormalized. The convention is unchanged.

Metallic-roughness image: red (occlusion) keeps 55% of its darkening. Green
(roughness) drops up to 22% on scale ridges of the body. Blue (metallic) is
unchanged.

No mesh, skin, joint, UV, material or sampler data changed. The image sizes
are 4096x4096 RGB PNG, as before.

## Numbers

Luminance is the Rec. 709 luma of the display values, 0 to 1. Saturation is
HSV saturation. The dragon's pixels are those where two key renders differ
(the same model with a flat magenta and a flat green base colour). The
target's dragon is selected by a colour rule (hue under 32 or over 345
degrees, saturation over 0.22) in the lower part of `run-target.png`. That
rule selects for saturation, and it took in a few sandy riverbank pixels.

| View | Before: luminance / saturation | After: luminance / saturation |
|---|---|---|
| `--inspect 200 14 20` | 0.209 / 0.413 | 0.335 / 0.606 |
| `--inspect 90 10 5` | 0.111 / 0.576 | 0.182 / 0.738 |
| `--inspect 0 12 -30` | 0.134 / 0.498 | 0.224 / 0.618 |
| `--run 7`, frame 300 | 0.111 / 0.428 | 0.183 / 0.559 |
| all four views | 0.128 / 0.535 | 0.211 / 0.689 |
| `run-target.png` | target: 0.269 / 0.548 | |

Mean hue: 14.9 degrees before, 13.6 after, 14.3 in the target. The full
output, with the 10th and 90th luma percentiles, is `compare/numbers.txt`.

In the run frame the dragon is 220 pixels wide and in the valley's morning
shadow; the target's dragon is in direct low sun. The run frame went from
0.111 to 0.183 against the target's 0.269.

## Mesh check

`MESH CHECK PASSED`. All JSON for nodes (64), skins, meshes, materials,
textures, samplers, images and accessors (8) is equal. The data bytes of all
8 accessors are equal. All 8 bufferViews that are not images have equal JSON
(without `byteOffset`) and equal bytes. The full output is
`compare/mesh_check.txt`.

## Compare images

Before is on the left in each.

- `compare/rear_200_14_20.jpg`, `compare/side_90_10_5.jpg`,
  `compare/below_0_12_-30.jpg`, `compare/run7.jpg`: crops at full size.
- `compare/run7_vs_target.jpg`: the run frame before, after, and the target's
  dragon. The two game crops are enlarged three times without smoothing.

## What is not right yet

- Saturation is above the target: 0.69 against 0.55. The hide reads more
  orange than the target's weathered rust.
- The body and the belly are closer in value than before in the
  `200 14 20` view. The belly is lighter than the body, but the body's lift
  took most of the gap.
- The membrane seen at a grazing angle (`90 10 5`) is a flat orange. Its
  texture has little detail there, and the script adds none.
- The dorsal spines stay dark. The target's are rust with dark tips.
- The horn and claw regions are approximate: two smoothsteps on bind-pose
  position. The horn base blends into the skull over about 15 model
  millimetres.
- The head close up, the jaw interior and the feet were not rendered.
- Only the modern tier on Metal was rendered. The retro tiers were not
  checked.

## Tried and rejected

Three full render passes, the brief's limit.

1. Pass 1: gain 0.62, strong tints (rust sRGB 176,70,34 at 78%). Luminance
   0.174, saturation 0.765, hue 8.9. It read as a red dragon, not rust.
2. Pass 2: gain 0.90, tints toward orange at 66%. Luminance 0.211,
   saturation 0.705, hue 12.1. The body became as light as the belly, and
   the sunlit membrane read salmon.
3. Pass 3, the candidate: gain 0.78 with the membrane and belly gains raised
   to keep their level, body detail power 1.35 to 1.6, tints less saturated.

Not tried: extra normal detail derived from the albedo, and a region mask
for the dorsal spines.

Rejected before any render: a pointwise curve on the sRGB image. It flattens
the scale contrast by the same power it lifts with.

## Cost

About 15 minutes of wall time. The token count is unknown.
