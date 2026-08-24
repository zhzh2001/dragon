# Third-party assets

## artifacts/dragon_broken_fbx_import.glb  (not in use)

Derived from **"Black Dragon with Idle Animation"** by **dennish2010**, via
Sketchfab.

- Licence: **CC Attribution-NonCommercial (CC BY-NC)**
- Source: https://sketchfab.com/3d-models/fb0053a2e59b43868e934c239bf4eb36

Not currently used by the game, and kept only as a reference case. Blender's FBX
importer mangles this model's rig, so every export derived from it renders as a
tangle regardless of processing. Obtaining the glTF variant from Sketchfab
directly avoids the problem. See CLAUDE.md.

The game currently runs on the procedurally generated rig in
`src/anim/dragon_rig.cpp`, which needs no attribution.

**The NonCommercial term means this project cannot be sold while it uses this
asset.** Replacing it with a permissively licensed or original model would lift
that. The engine loads any rigged glTF, so swapping it is a matter of dropping in
a different file.

Attribution is required by the licence: credit dennish2010 in anything published
that includes this model.
