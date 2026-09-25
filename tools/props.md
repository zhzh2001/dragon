# Static Dragon Engine props

`assets/props/watchtower.glb` is a tapered grey-brown masonry tower with arrow
slits, a broad battlement parapet and a shallow dark iron brazier. Its base is
5 × 5 m; overall bounds are **5.86 × 16.00 × 5.86 m (X/Y/Z)**, including the
parapet. The bowl rim is at **Y = 16.00 m**, below the engine's fireball centre
at 16.5 m. It has **5,192 triangles**.

`assets/props/hoard_pile.glb` is a round gold heap with dark coin gaps, 176
individual coins, three cups, a timber chest lid and six faceted gems. Bounds
are **11.00 × 3.70 × 11.00 m**, with **7,082 triangles**.

Both are generated entirely in code, in metres, with Y-up and +Z forward
(the tower doorway and chest latch face forward). Each has one mesh, one
identity-rest `root` bone at the origin, full root weights, and one Principled
material with an embedded 512 × 512 PNG atlas. Origins are centred at ground
level. Geometry modifiers are baked before skin binding; no animation, morphs,
cameras or lights ship. The exporter’s root axis rotation and inverse bind
are normalized together to identity without moving vertices.

Rebuild from the repository root (tested with Blender 5.2):

```sh
blender -b --factory-startup --python tools/build_props.py
blender -b --factory-startup --python tools/build_props.py -- --verify-only
```

Verification reads the GLB buffers and re-imports each asset; failures exit
nonzero even without `--python-exit-code`. Add `--render` for inspection PNGs
in `/tmp/props-watchtower.png` and `/tmp/props-hoard_pile.png`. Both renders
were visually inspected; removing the skin from a temporary copy also
confirmed verification exits 1. Actual verification output:

```text
VERIFY watchtower: PASS
  glTF Y-up bbox metres: min=(-2.930, 0.000, -2.930) max=(2.930, 16.000, 2.930)
  dimensions X/Y/Z metres: (5.860, 16.000, 5.860); triangles=5192; bones=1 [root]
  materials=[watchtower_atlas]; textures=[watchtower_basecolor: embedded PNG 512x512, baseColor]
  single mesh/skin; root rest+inverse bind=identity; root weights=1; transforms=identity; no animations/morphs/extra nodes: PASS
  axis mapping: Blender (X,Y,Z) -> glTF (X,Z,-Y); forward authored -Y -> +Z; base/centering/budget: PASS
VERIFY hoard_pile: PASS
  glTF Y-up bbox metres: min=(-5.500, 0.000, -5.500) max=(5.500, 3.700, 5.500)
  dimensions X/Y/Z metres: (11.000, 3.700, 11.000); triangles=7082; bones=1 [root]
  materials=[hoard_pile_atlas]; textures=[hoard_pile_basecolor: embedded PNG 512x512, baseColor]
  single mesh/skin; root rest+inverse bind=identity; root weights=1; transforms=identity; no animations/morphs/extra nodes: PASS
  axis mapping: Blender (X,Y,Z) -> glTF (X,Z,-Y); forward authored -Y -> +Z; base/centering/budget: PASS
```
