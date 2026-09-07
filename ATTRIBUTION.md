# Third-party assets

## assets/dragon.glb

**"Black Dragon with Idle Animation"** by **dennish2010**, via Sketchfab.

- Licence: **CC Attribution-NonCommercial (CC BY-NC)**
- Source: https://sketchfab.com/3d-models/fb0053a2e59b43868e934c239bf4eb36

**The NonCommercial term means this project cannot be sold while it uses this
asset.** Replacing it with a permissively licensed or original model would lift
that. The engine loads any rigged glTF, so swapping it is a matter of dropping in
a different file.

Attribution is required by the licence: credit dennish2010 in anything published
that includes this model.

### Getting it

Not committed: 65 MB with textures embedded, re-downloadable, and redistributing
an NC asset in a repo is best avoided. From the Sketchfab page choose
**Download -> glTF**, and use the **.glb** (single file) rather than the .gltf
directory. Rename it to `assets/dragon.glb`.

Use the glTF download, **not** the original FBX. Blender's FBX importer mangles
this rig -- see `docs/ANIMATION.md`. The glTF loads directly with no Blender
step at all.

## assets/alt/prowler.glb (optional second dragon)

**"Prowler Dragon Variant Rig"** by **SuperKapoo913**, via Sketchfab.

- Licence: **CC Attribution (CC BY)**
- Source: https://sketchfab.com/3d-models/7ee71aaf323d426bbbdf28d73d55bbd9

A 126-bone wyvern (wings are the forelimbs) with base colour, normal and
metallic-roughness maps and two clips (Landing, Walk). Loaded with
`--model assets/alt/prowler.glb`; the joint mapper finds its neck, tail, wings,
legs, feet and jaw by name and structure, so the same procedural rig drives it.

### Getting it

Sketchfab serves this one as glTF. Download -> glTF, then either use the .glb
directly or, as was done here, import the glTF into Blender and export the
armature plus its two skinned meshes as a single .glb with tangents. (The glTF
importer is fine -- it is only Blender's FBX importer that mangles rigs.)
Credit SuperKapoo913 in anything published that includes this model.
