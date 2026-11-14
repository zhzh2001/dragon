# Embercrest warm hide candidate

The candidate replaces the dark brown hide with rust-orange scales, lighter belly plates, and brighter copper membranes.
The change is visible in native-size engine crops, including the flight camera.
The flight crop's mean luminance increased 2.18 times.
It remains darker and more saturated than the main concept target.

## Deliverables

- Candidate: `out/embercrest-hide.glb`, kept local.
- Rebuild and structure check: `tools/hide/rebuild.py`.
- Full 1280 × 720 engine frames: `out/renders/*-before.png` and `out/renders/*-after.png`, kept local.
- Native-size comparison crops: `out/compare/{rear,side,belly,run}.jpg`.
- Concept comparisons: `out/compare/run-target.jpg` and `out/compare/run-target-matched.jpg`.
- Measurement data and check output: `out/metrics.json` and `out/check.txt`.

The comparison labels identify capture names, not anatomical directions.
`rear` uses `200 14 20`; `side` uses `90 10 5`; `belly` uses `0 12 -30`.
The imported model's orientation makes the `rear` capture show its chest.
The side capture remains clipped because the requested camera intersects the wings' projected extent.

Every before/after pair uses identical crop coordinates and preserves native pixels.
The native concept comparison preserves both images' pixel sizes despite their different framing.
The matched-width comparison also supplies a smaller concept crop for a silhouette comparison.
Every comparison JPEG is below 1 MB; the largest is 533,539 bytes.

## Texture changes

Base colour uses three monotonic curves, one per sRGB channel.
A monotonic curve preserves the ordering of dark grooves and light ridges.
The curves lift red most, green less, and retain blue for warmer neutral highlights.
They preserve every source texel's location and introduce no markings, blur, or generated detail.
Bright source belly plates remain lighter than adjacent scales.
The membranes retain their original veins and become brighter than the shadowed body.
Dark horn and claw tips remain dark; their brown bases become warmer.

The curve knots are input byte values `0, 32, 64, 96, 144, 192, 255`.

| Channel | Output values |
| --- | --- |
| Red | 0, 75, 144, 185, 222, 242, 255 |
| Green | 0, 48, 95, 137, 187, 220, 255 |
| Blue | 0, 33, 69, 105, 157, 206, 255 |

The metallic-roughness image retains its red occlusion channel exactly.
Its blue metallic channel becomes zero because hide and keratin are not metals.
Its green roughness decreases by up to 0.055, weighted smoothly across original roughness values 0.40–0.65.
This preserves the existing roughness variation and gives scale ridges a modest highlight lift.
The normal image remains byte-identical, including its channel convention.
All three images remain 4096 × 4096 PNGs.

The shader decodes base colour from sRGB before lighting it in linear HDR.
It also multiplies membrane transmission by the base colour and its hue.
These facts motivated an actual pigment lift instead of a roughness-only change.
The shader, lighting, tonemapping, and material structure remain unchanged.

## Measurements

Luminance is relative linear luminance, from 0 to 1.
The measurement decodes rendered PNG pixels with the sRGB transfer function, then applies weights `0.2126, 0.7152, 0.0722`.
Saturation is HSV saturation, `(max(R,G,B) - min(R,G,B)) / max(R,G,B)`, measured in displayed sRGB.
The values below are arithmetic means over selected dragon pixels, not whole crop rectangles.

| Capture | Pixels | Luminance before | Luminance after | Ratio | Saturation before | Saturation after |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 200 14 20 | 65,680 | 0.04319 | 0.10040 | 2.32 | 0.4164 | 0.6937 |
| 90 10 5 | 352,414 | 0.01429 | 0.03154 | 2.21 | 0.5799 | 0.7861 |
| 0 12 -30 | 144,789 | 0.02120 | 0.04906 | 2.31 | 0.5034 | 0.7026 |
| Flight, run 7 | 6,883 | 0.01276 | 0.02783 | 2.18 | 0.4519 | 0.6713 |
| Run concept target | 219,985 | 0.07671 | — | — | 0.5452 | — |

The flight candidate reaches 36.3% of the concept's mean luminance, compared with 16.6% before.
Its saturation exceeds the concept by 0.1262.
These numbers measure progress, not a lighting match: the concept uses different sun placement, framing, and geometry.
The unchanged flight camera also shows a much smaller dragon than the concept.

Masks come from aligned before/after differences of at least two byte values in any RGB channel.
The script closes one-pixel gaps, fills enclosed holes, and erodes the boundary by one pixel.
The same mask measures both versions, so colour changes cannot select different samples between them.
Unchanged isolated dark pixels and silhouette edges can be excluded; these are interior estimates rather than exact object-ID masks.
The target uses a manually traced silhouette with visible membrane holes removed and a one-pixel boundary erosion.
These masks avoid terrain, sky, and HUD pixels.
`*-mask.jpg` shows each selection with excluded pixels darkened.
`out/metrics.json` also records display-space luma and exact crop coordinates.

## Structure and reproducibility check

The rebuild checks the original against the candidate and prints:

```text
PASS: 8 accessors and 8 non-image bufferViews matched byte-for-byte.
PASS: 64 nodes, 1 skin, meshes, materials, textures, samplers, and all remaining JSON matched.
PASS: Normal PNG bytes matched. Three images retained 4096 x 4096 dimensions.
PASS: independent rebuild matched every GLB byte.
```

The check compares all accessor definitions and their containing buffer bytes.
It compares each non-image bufferView's metadata and bytes, allowing only its binary offset to move.
It then compares the entire JSON document after normalizing buffer locations and image lengths.
This covers UVs, joints, weights, tangents, nodes, skins, mesh structure, material bindings, and all other scene data.
Only the base-colour and metallic-roughness image payloads change.

Source SHA256:

```text
e00be8245790e0b159f1a1fb0e1fd51576415f48377a6811b421ab61cf9a7a0b
```

Candidate SHA256:

```text
ad2c8e48320c47506e4f983e57008d249b4b8397fd8127bc0f536187020d9d59
```

The local source copy matched the shipped model after all work.
A separate rebuild produced the same complete GLB bytes.
Python 3.14, NumPy, and Pillow were used; the scripts contain no random operations.
PNG compression determinism was checked with the installed versions, not across different library versions.

Run these commands from this worktree:

```sh
/opt/homebrew/bin/python3.14 tools/hide/rebuild.py
/opt/homebrew/bin/python3.14 tools/hide/capture.py before
/opt/homebrew/bin/python3.14 tools/hide/capture.py after
/opt/homebrew/bin/python3.14 tools/hide/compare.py
```

The rebuild first copies the shipped GLB into `out/source/`.
It also copies the shipped rig and flight profiles beside both models without changing their contents.
The engine otherwise uses different defaults for models outside the asset directory.
These sidecar copies keep the comparison's pose and flight behaviour equal.
The capture script runs the shipped binary from `~/src/game-claude` and writes screenshots only inside this worktree.
It uses the brief's exact frame, camera, run, and UI flags, then converts BMPs with `sips`.

## Trials and remaining limits

Three complete render passes were used: original, first candidate, and final candidate.
Each pass rendered all four requested views.

The first candidate gated colour lift with the existing roughness map to preserve smooth horn regions.
That dependency amplified patch boundaries and produced uneven colour lift.
The final candidate removes roughness from colour selection and raises green and blue to reduce the first candidate's red bias.
The original side render already contains angular transmission bands; the final candidate retains those bands.
Fixing their geometry or shader would exceed this texture-only task.

Normal strengthening was considered but not used.
The existing normal detail becomes readable after the pigment lift, without a new convention or noise risk.
No extra render pass was spent on that option.

The final material keeps warm membrane transmission and clear scale grooves.
Its shaded spine still reads darker than the concept's sunlit spine.
The accepted result closes the colour and value gap without claiming to reproduce the concept's lighting.
No further texture iterations followed the third pass.

## Time and usage

The timed work interval started at 2026-11-14 00:49:08 UTC, when the source copy completed.
Rebuild verification and comparison checks completed at 00:57:18 UTC, after 8 minutes 10 seconds.
Initial inspection and final report preparation fall outside that interval.
The total session took approximately 13 minutes.
An exact token count was unavailable.
