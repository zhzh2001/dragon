# Dragothic rig — 2026-10-04

The local `assets/alt/3dmark2001/dragothic.glb` loads and flies through the
engine's mapped procedural rig. Its `.rig.cfg`, `.flight.cfg` and `.breath.cfg`
are beside it. No `src/` changes. Reproduction scripts and native captures
are under `artifacts/dragothic-rig/` (see its README).

## Fit and corrections

Independently measured `tools/skeletons/dragothic.json`: 64 joints, three neck
joints, twelve tail joints, three wing-root joints and four articulated rays
per side, three hind-leg joints per side, feet/toes and jaw. Source X span
19.4847183 is normalized to 1; the engine applies scale 19.0000. Source faces
−Z; final mesh and edit bones rotate together to +Z. The loader confirms
`asset faces +Z … yawing 180`, with head ahead of tail.

The static conversion had inward winding; faces were reversed before binding.
Heat weighting pulled the torso into the wings and left unweighted vertices.
The final bind uses the six original LightWave masks for anatomical ownership,
continuous wing weights and measured longitudinal chains, capped at four
influences. The jaw uses its own lip-plane mask and hinge blend. Whole teeth
are assigned by their broader roots (28 upper, 14 lower); lips remain rigid,
so the mask cannot stretch a tooth or drag the upper lip down.

Wing rays need terminal joints for the engine to aim both physical segments;
without them the standing wings projected upward as spikes. The wrist pivot
was moved to the proximal source wrist. Stormsail's biped stance/stow blocks
were copied and retuned: horizontal body, relaxed hind limbs, small ray fan,
32° flap, restrained dive fold, jaw rest 0°/open 8°, stride 1.1 m/lift 0.28 m.
Centered bind resting height is 2.189393 m. A flat-ground mesh probe at stance
lift 0.438 m gives sole clearance 0.001 m and lowest folded wing 0.076 m.
Sunspear's amber fire block supplies the breath, range 1.15 and angle 0.8.

## Texture evidence

All 736 carved images were inspected; source UV overlays and textured previews
are local in `artifacts/dragothic-rig/textures/`. DDS decoding matches the
chosen PNG pixels exactly. Fifteen source surfaces and UVs are retained.

| Requested clip | Match | Surfaces |
| --- | --- | --- |
| genericskin-2 | dds119_256x256_DXT1 | drago1, drago.siipi |
| dnuface_side | dds118_512x512_DXT1 | paa.sivut, grbge.silma, upper/lower lips |
| dnuface_top | dds120_512x512_DXT1 | paa.top |

No defensible match for vatsa_yo, spikes-1c/semispikes-1, dnuface_btm or
tomatoe: those surfaces use their exact source COLR as embedded flat textures.
The remaining unmapped teeth/horns/mouth surfaces also use COLR. Cyan spikes,
pink teeth/interior and orange underside therefore reflect the fallback,
not a claim to have recovered the original layered LightWave appearance.
All eleven images are embedded; no external texture URI is required.

## Verification and remaining human check

Built the native engine; all 21 CTest suites pass. The asset-specific check
passes mapping, +Z orientation, span, weights, embedded material assignments
and 420 frames each of glide/flap/dive/brake/attack/ground/walk. Bind error
2.37e−8; 17,095 triangles, 11,951 loaded seam vertices, zero unweighted
vertices, maximum exported weight-sum error 4.47e−8.

The exact requested 40-frame game command produced `glide.bmp`/`glide.png`.
Every final capture was inspected at full size: prescribed side/0°/180°/above
views, wider overhead to include clipped wingtips, glide/ground/action head,
ground-level feet, 900-frame actual descent/landing, walk and eight wingbeat
phases. `switch.log` confirms Embercrest → Dragothic; `breath-wide.png` shows
the amber jet. Logs report `mapped rig: neck 3, tail 12, wing root 3/3,
fingers 4/4, legs 3/3, front legs 0/0, feet 2, jaw jaw`; no wing fallback.

Human acceptance still needs flying, braking, landing on uneven ground and
walking at the keyboard, watching wing fold transitions and foot sliding.
UL meshes, decoded textures, Blender copies and rendered images remain local
and uncommitted; only original rigging scripts, measurements and text are
committed. UL assets must not ship with the public game.
