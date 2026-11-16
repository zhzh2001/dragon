# Tripo prop conversion, 2026-11-16

Replaced the watchtower and spire. Retained both hoards and the ward after two conversion passes failed quality checks.
The old towers used flat 512-pixel atlases that differed from the scanned rocks.
The replacements use baked stone detail and three 2048-pixel maps. Engine code and effect centres remain unchanged.

| Prop | Final source | Triangles | Result |
|---|---|---:|---|
| watchtower | `tripo-watchtower-01.glb` | 19,498 | Accepted after pass 1 |
| spire tower | `tripo-spire-01.glb` | 19,655 | Accepted after pass 2 and UV export repair |
| hoard pile | Original v2 | 10,228 | Conversion failed normal-map coverage after pass 2 |
| hoard trove | Original v2 | 10,074 | Conversion retained stretched wood and crumpled surfaces after pass 2 |
| ward stone | Original v2 | 3,108 | Conversion retained stretched tips and crumpled surfaces after pass 2 |

The three retained GLBs match their original files byte-for-byte. No third conversion pass ran.
The builder skips rejected conversions and prints their reasons. An explicit rejected build exits with its reason.

[The conversion record](../../../tools/props.md#third-generation-tripo-2026-11-16) lists source transforms, effect offsets, removed geometry, and both passes.
The new helper is `tools/tripo_props.py`. `tools/build_props.py` preserves its procedural path and adds `--from-tripo`.
`AI_DISCLOSURE.md` and `ATTRIBUTION.md` record the generated assets and their provenance.

## Inspection

All ten candidates were rendered and inspected at full size. Both accepted towers passed near, 250 m, and in-game inspection.
The watchtower has a narrower shaft than v2. The slate roof and stone footing remain readable at distance.
The spire's empty iron cradle holds the engine orb. Its baked crystal was removed.
The watchtower's new iron cradle holds the engine effect at (0, 35.2, 2).

| Comparison | Near bench | 250 m bench | In game |
|---|---|---|---|
| Watchtower | [Near](watchtower_near.png) | [Far](watchtower_far.png) | [Before and after](compare-game-watchtower.png) |
| Spire | [Near](spire_tower_near.png) | [Far](spire_tower_far.png) | [Before and after](compare-game-spire_tower.png) |

The comparison crops preserve their source pixels. No image was resized.
`game-v3-*.png` shows all five final props and the 600-frame seed-7 overview.
`capture_game.py` records the close-view commands. Seed 1 supplies the spire because seed 7's first valley contains only keeps.
The hoard pile capture hides vegetation because a foreground tree blocked it. The ward capture uses training and attack at frame 1.
The root hoard and ward bench images are v2 reference copies. Their rejected conversions are under `rejected/`.

The first spire game capture exposed a UV mismatch. Its material referenced UV1, but the engine read UV0.
The export was repaired without another bake. The builder now removes unused UV maps, and the verifier requires UV0 texture references.

## Checks

| Check | Result |
|---|---|
| CMake configure and build | Passed |
| Final five-file prop verifier | Passed; output below |
| Procedural spire regression build | Passed; 1,388 triangles and original 512-pixel map |
| Rejected-pile CLI check | Expected exit 1 with the recorded reason |
| `ctest --test-dir build --output-on-failure` | 26 of 26 passed; [output](ctest.txt) |
| Metal golden comparison | Eight of eight passed; [output](golden-final.txt) |
| Python compilation and `git diff --check` | Passed |

Compared with fresh v2 captures, the run-HUD golden changed at the distant tower.
Its mean channel difference was 0.001375/255; [this image amplifies the difference 16 times](golden-run-hud-change.png).
The skeleton capture differed within a 3 × 3 pixel box, with a maximum channel difference of 3/255.
The other six captures were identical. [The measurements](golden-before-after.json) retain the exact values.
The checked-in golden baselines were not changed.

Token use was not available from this session.

## Final verifier output

```text
VERIFY watchtower: PASS
  effect centre glTF metres: (0.000, 35.200, 2.000); empty sphere radius=0.65 m: PASS
  glTF Y-up bbox metres: min=(-5.495, 0.005, -5.489) max=(5.498, 39.986, 5.497)
  dimensions X/Y/Z metres: (10.993, 39.981, 10.986); triangles=19498; bones=1 [root]
  materials=[watchtower_tripo]; textures=[watchtower_basecolor: embedded PNG 2048x2048, baseColor]
  normal and metallic-roughness: embedded PNG 2048x2048; tangent-space normal: PASS
  single mesh/skin; root rest+inverse bind=identity; root weights=1; transforms=identity; no animations/morphs/extra nodes: PASS
  axis mapping: Blender (X,Y,Z) -> glTF (X,Z,-Y); forward authored -Y -> +Z; placement/budget: PASS
VERIFY spire_tower: PASS
  effect centre glTF metres: (0.000, 42.000, 0.000); empty sphere radius=1.20 m: PASS
  glTF Y-up bbox metres: min=(-3.500, -0.005, -3.503) max=(3.498, 44.000, 3.511)
  dimensions X/Y/Z metres: (6.998, 44.005, 7.014); triangles=19655; bones=1 [root]
  materials=[spire_tower_tripo]; textures=[spire_tower_basecolor: embedded PNG 2048x2048, baseColor]
  normal and metallic-roughness: embedded PNG 2048x2048; tangent-space normal: PASS
  single mesh/skin; root rest+inverse bind=identity; root weights=1; transforms=identity; no animations/morphs/extra nodes: PASS
  axis mapping: Blender (X,Y,Z) -> glTF (X,Z,-Y); forward authored -Y -> +Z; placement/budget: PASS
VERIFY hoard_pile: PASS
  glTF Y-up bbox metres: min=(-8.000, 0.000, -8.000) max=(8.000, 5.500, 8.000)
  dimensions X/Y/Z metres: (16.000, 5.500, 16.000); triangles=10228; bones=1 [root]
  materials=[hoard_pile_atlas]; textures=[hoard_pile_basecolor: embedded PNG 512x512, baseColor]
  single mesh/skin; root rest+inverse bind=identity; root weights=1; transforms=identity; no animations/morphs/extra nodes: PASS
  axis mapping: Blender (X,Y,Z) -> glTF (X,Z,-Y); forward authored -Y -> +Z; placement/budget: PASS
VERIFY hoard_trove: PASS
  glTF Y-up bbox metres: min=(-7.000, 0.000, -7.000) max=(7.000, 4.000, 7.000)
  dimensions X/Y/Z metres: (14.000, 4.000, 14.000); triangles=10074; bones=1 [root]
  materials=[hoard_trove_atlas]; textures=[hoard_trove_basecolor: embedded PNG 512x512, baseColor]
  single mesh/skin; root rest+inverse bind=identity; root weights=1; transforms=identity; no animations/morphs/extra nodes: PASS
  axis mapping: Blender (X,Y,Z) -> glTF (X,Z,-Y); forward authored -Y -> +Z; placement/budget: PASS
VERIFY ward_stone: PASS
  glTF Y-up bbox metres: min=(-5.599, -7.950, -5.997) max=(6.035, 6.900, 5.502)
  dimensions X/Y/Z metres: (11.634, 14.850, 11.499); triangles=3108; bones=1 [root]
  materials=[ward_stone_atlas]; textures=[ward_stone_basecolor: embedded PNG 512x512, baseColor]
  single mesh/skin; root rest+inverse bind=identity; root weights=1; transforms=identity; no animations/morphs/extra nodes: PASS
  origin socket minimum triangle distance=4.383413 m; required >=4: PASS
  maximum vertex radius=8.323742 m; required <=8.5: PASS
  surface area=553.627896 m2; entirely inside radius 7.5=523.524195 m2 (94.5625% conservative bound): PASS
  outside radius 7.5: polar tips only: PASS
  origin Fibonacci rays: 1049/2000 escape (52.45%); required 30-55%: PASS
  one Principled material; embedded PNG maps; no emission: PASS
  axis mapping: Blender (X,Y,Z) -> glTF (X,Z,-Y); forward authored -Y -> +Z; placement/budget: PASS
```
