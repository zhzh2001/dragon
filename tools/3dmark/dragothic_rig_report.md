# Dragothic reference repair — 2026-10-04

The local `assets/alt/3dmark2001/dragothic.glb` and its rig/flight/breath
profiles were rebuilt against the supplied 3DMark2001 SE High Detail frames.
All UL meshes, textures, metadata dumps and images remain ignored and local.
Only original scripts and this report are committed. No engine, shader or
other-asset edits. The independently fitted skeleton remains in
`tools/skeletons/dragothic.json`.

## Repair pass 1: recover the layered materials

The previous converter picked the first image with a UV map, losing the
channel and discarding planar maps. It used `genericskin-2` as base colour,
although the LWO assigns it to DIFF; the COLR layer is the whole-dragon planar
`yly` image. Belly, horns and teeth also have planar image layers, which were
replaced by flat editor colours. Cyan spikes, pink teeth and orange underside
were fallback colours, not the benchmark's materials.

The source's enabled image layers use normal replacement, opacity 1 and
negative off. They replace the flat COLR value; multiplying those editor
colours into the images would be wrong. The converter now retains channel,
projection, transform, opacity, blend, DIFF/SPEC/GLOS/LUMI and source-point
identity. The Blender adapter bakes supported COLR and DIFF layers with
emission-only shading, preserving both original UV channels. Each material
gets its own atlas to preserve head and eye detail. Runtime atlas is UV0,
original DIFF UV1 and original primary UV2; all images are embedded.

| Clip | Carved candidate | Result / confidence |
| --- | --- | --- |
| genericskin-2 | DDS119, 256² DXT1 | Rechecked; original body/wing DIFF scale UVs |
| dnuface_side | DDS118, 512² DXT1 | Rechecked side, eye-surround and lip UVs |
| dnuface_top | DDS120, 512² DXT1 | Rechecked forehead/nose pattern |
| vatsa_yo | DDS167, 256² DXT1 | Strong match to speckled ochre belly in 033 |
| dnuface_btm | DDS181, 128² DXT1 | Central throat patch and olive perimeter align with underside UVs |
| tomatoe | DDS166, 256² DXT1 | Fiery radial red eye in 031; material `simmuborgir` |
| yly | DDS165, 1024² DXT1 | Whole-dragon planar footprint; strong visual match |
| white | DDS164, 16² DXT1 | Same solid-white function; exact archive identity unproved |
| spikes-1c / semispikes-1 | Unrecovered | Reference olive keratin substitute; ivory on teeth |
| dr_facelite | Unrecovered | Face DIFF layer omitted, not silently assigned another map |

`grbge.silma` is the eye surround and uses the face map; it is not the red
ocular surface. All 736 candidates were reviewed in local contact sheets.
The named matches are visual/UV identifications, not decrypted filenames.

Source SPEC is zero except eyes (0.265). GLOS is 0.275 for eyes, 0.15 for
body/wings, 0.4 for belly and 0.26 for teeth. The horn shader block is disabled
in this LWO. Base eye LUMI is zero, but an enabled white LUMI image overrides
it. The benchmark's renderer therefore cannot be reconstructed from the
flat surface values alone. Pass 1 uses matte ORM for zero-SPEC surfaces and
roughness 0.73 for eyes. A DIFF image is formally an intensity channel; the
baked RGB product approximates the benchmark's detail modulation. The `yly`
map already contains authored shading, which native lighting shades again.

An intermediate atlas export failed native review: edit-mode unwrap
invalidated Blender's cached UV-layer handle. The fix re-fetches that handle,
specifies the bake UV explicitly and puts its data in UV0. A global unwrap
also blurred small head islands; independent packing per material fixes it.
Neither failed export is the delivered model.

## Repair pass 2: keratin colour and retained luminosity

Full-size albedo review found that the approximate spike colour had been
entered as linear RGB, making it too pale. The final substitute is sRGB
`(0.28, 0.30, 0.18)`, explicitly converted to linear before baking. It now
reads as dark olive-brown close to the hide. This remains a reference-based
substitute for the two unrecovered spike images.

Horn ORM roughness is 0.22 in this pass, a reference-driven approximation to
the benchmark sheen rather than a claim that the LWO's zero-SPEC value
encodes it. White albedo stays unchanged; the engine's sun and ambient still
differ from the benchmark's blue-white illumination.

The enabled white LUMI layer overrides the eye's base zero luminosity.
Its red base texture is also exported as glTF `emissiveTexture`, with
`emissiveFactor [1,1,1]`. Blender reproduces self-lit red eyes; the existing
native loader ignores that field, so the native result remains red without
the benchmark glow. Emission is not painted into skin albedo.

The final asset is 7,226,428 bytes, SHA256
`110da5aedb92064f6213b1098067d2f723e80afb02fe31e0b18a901f3505d54d`.
All native captures and albedo comparison plates were refreshed after this
pass. The asset acceptance, original-UV/source-point checks and profiles'
byte-for-byte comparison with the pre-repair files pass. The generated
profiles preserve the previously validated pose and ground dimensions.

## Geometry diagnosis

The exhaustive source-point/export comparison has maximum error
**0.000002161 m** at the engine's 19 m span. Original head, horns, tail,
wing chord, four rays and scalloped edge survive unchanged. Source span is
19.4847183 units; source tail-to-snout length becomes 13.6363 m. Wing material
covers X −9.5..9.5 m and forward Z −5.803317..−0.172069 m. Head side spans
forward Z 0.712036..2.044806 m. There is no conversion stretch or shortening.

The supplied 22 LWS scenes contain **zero bone definitions**. Scenes 14/15
load `dragon_animatedHEAD.lwo` and drive `LW_MorphMixer`, rather than bones.
The source rest-joint placement requested in the brief cannot be recovered
from these scenes. The current 64-joint skeleton is a measured procedural
approximation, and is described as such.

The source's spread mesh already supplies the lateral span. Its authored
morphs change span substantially during a beat: unit up/upB span about
17.03/16.86 m; relaxed is a much more folded pose, about 12.28 m. Scenes14/15
key up at frames 0/54, upB at 8, down at 18, downB at 28 and relaxed at 42,
at 30 fps. Those are overlapping morph envelopes, not bone rotations or
independent full-mesh frames. They also translate the head vertically by
0.11–0.74 m and forward by 0.08–0.14 m. Benchmark phase, perspective and
lighting account for silhouette differences that a single static view
cannot establish as proportion errors.

The fitted chest-to-head rest vector is `(0, 0.570000, 2.698000)` m.
Studio glide at frame420 gives `(0, 0.494251, 2.708920)` m: the head drops
about 7.6 cm and projects **1.1 cm farther forward**, rather than retracting.
Removing neck gravity and streamlining changed reach by only 1–2 cm across
glide/flap/dive/brake/attack. There is no evidence for a shortened neck due
to conversion, weights or profile. Consequently no arbitrary neck length,
head scale, horn or wing-chord edits were made; rig/flight values remain the
previous independently tuned values. Original animation reproduction would
require morph playback or a measured morph-to-rig fit, which this procedural
rig does not provide.

The original fit uses source anatomical ownership masks, three neck joints,
twelve tail joints, three root joints and four three-joint rays per wing,
three hind-leg joints per side, toes and a jaw mask. Upper/lower teeth are
rigidly assigned by their broader roots, preventing jaw-plane distortion.
The mesh and armature are turned together to +Z; the loader yaws it 180°.

## Native verification and local comparisons

Built in this worktree with `cmake -S . -B build -G Ninja` and
`cmake --build build`; all 21 CTest suites pass. Updated asset acceptance
passes 64-joint mapping, 17,095 triangles, 15 surfaces, 26 embedded textures,
normalized finite weights and seven animated scenarios at 420 frames each.
Bind error is 2.10003304e−8; maximum weight-sum error 4.47e−8.
Original UV channels and source-point extras are checked numerically.

`tools/3dmark/capture_dragothic_pass2.py` reproduces the native inspection.
The final 1024×768 renders use `--no-post` to expose the warm sun without
colour grading. Reviewed full-size: glide side/front/rear/above; head front,
underjaw, top and side; grounded side/front/rear/above and close head; feet;
actual 900-frame descent/landing; Embercrest → Dragothic switch; flap and
bind comparisons. Landing telemetry reports GROUNDED at 5 and 10 seconds.
Flat-floor skin probe gives sole clearance 0.001 m and folded-wing clearance
0.076 m at stance lift 0.438 m. The rig/flight files were regenerated beside
the model; their validated geometry values did not need retuning.

Comparison plates live in ignored `artifacts/dragothic-rig/pass2/`:
`compare-side.png`, `compare-head-front.png`, `compare-head-below.png`,
`compare-head-top.png`, `compare-above.png`; separate Blender
`repaired-albedo-*` and `repaired-neutral-*` isolate material colour from sun. `compare_dragothic_pass2.py` puts those unlit views beside
the benchmark and native render in `compare-albedo-{head,side,horns}.png`.
The negative-elevation CLI parser rejects a normal `-30` token as a flag;
the capture script passes `" -30"` (leading whitespace) so SDL_atof reads it.
No parser changes were made. The rider is a separate benchmark object and
is not added to this dragon asset.

## Remaining fidelity limits

Two spike clip identities and the face DIFF map remain unrecovered; substitutes
are explicitly marked. Native eyes are red but the existing loader ignores
per-material glTF emissive data, so native glow is not reproduced. White horn
albedo is restored, while the benchmark's blue-white sheen depends on its
renderer/lighting. Source animation is morph driven; the procedural wingbeat
is an approximation. These are residual differences, not an assertion of a
pixel-exact benchmark match. No warm-light tint correction was applied.
