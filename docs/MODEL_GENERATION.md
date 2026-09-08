# Generating a dragon: text/image-to-3D and auto-rigging, surveyed 2026-09-08

The Embercrest experiment (`EMBERCREST.md`) showed that an LLM writing
geometry by hand tops out at greybox-plus. This doc is the survey that
followed: what the generative 3D tools can do today, cloud and local, what
the two GPU boxes can run, and where the real gap is. Short version:

- **Mesh generation is usable now.** Several tools produce hero-quality
  textured sculpts from a concept image in under a minute. The best of them
  are either cheap cloud calls or open weights that fit a 16 GB card.
- **Rigging is the gap.** No tool, cloud or open, documents a winged
  quadruped. Every auto-rigger is humanoid-first; the ones that claim
  "quadruped" and "avian" have never been shown handling both on one body.
- **This engine sidesteps most of that gap.** It needs no animations, only a
  named deform skeleton with weights, because the procedural rig drives the
  chains. The workable pipeline is therefore: generate the mesh, retopo it,
  fit a skeleton *we* name (Embercrest's 68-bone rig already has the right
  names), and let an ML skinner compute only the weights.

Everything below was gathered by web research on the date above. Claims that
could not be confirmed are marked as such. Prices are USD.

## What the engine needs from an asset

Constraints from `anim/gltf_loader.cpp` and `anim/dragon_rig.cpp`, so a
candidate can be judged before it is imported:

- **A skin is mandatory.** The loader rejects a file with no skin. An
  unrigged GLB from a generator cannot even be previewed in-engine; give it
  a one-bone armature in Blender first.
- **256 joints max, 4 influences per vertex**, triangles only, no Draco.
- **Joints are found by name substring**: `neck`, `head`, `jaw`/`mandib`,
  `tail`, `wing`/`shoulder`, `thigh`/`hip`/`femur`, `upper_arm`/`foreleg`,
  `hand`/`foot`/`paw`, `chest`/`spine`/`torso`. Helper bones containing
  `_end`, `ik`, `pole`, `cont`, `target`, `chain`, `roll` are skipped. Sides
  come from bind position, not names. Wing fingers are the branches after
  the shared arm. A numbered skeleton (`bone_0..N`) maps to nothing.
- **Textures**: base colour (sRGB), metallic-roughness, and a normal map
  only where the primitive carries tangents (tick Tangents in Blender's
  exporter). `baseColorFactor` is ignored, so bake colour into the texture.
- **Scale and frame**: scaled by X extent to a 19 m wingspan, so export
  with wings spread and forward at -Z, +Y up (the glTF default).
- The optional test in `tests/test_anim.cpp` loads `assets/embercrest.glb`
  when present and checks every chain maps and responds. Point it at a new
  asset and it becomes the acceptance test for the rig.

## The two machines

| | t5810 | x99 |
|---|---|---|
| GPU | RTX 2080 Ti, 11 GB, sm_75 (Turing: no bf16, no flash-attn 2) | RTX 5060 Ti, 16 GB, sm_120 (Blackwell: needs torch 2.7+ / cu128) |
| OS, Python | Ubuntu 22.04, Python 3.10, CUDA 13.2 toolkit installed | CachyOS, Python 3.14 (too new; use `uv python install 3.12`), uv, docker, conda |
| RAM, CPU | 31 GB, 4 cores | 31 GB, 8 cores |
| Disk | 284 GB free | **20 GB free on root.** `/mnt/Data` (NTFS) has 69 GB free for weights |
| No torch on either. | sudo needs a password | sudo is passwordless |

x99 is the generation box (Blackwell is a first-class target for current
PyTorch; 16 GB clears the useful thresholds). Its disk is the binding
constraint: a TRELLIS.2 install is roughly 7 GB of torch plus 10 GB of
weights, so weights go on `/mnt/Data` or something is cleaned off root
first. t5810 is the fallback for shape-only models with fp16 paths and for
CPU/Vulkan rigging; it needs headless runs and every CUDA extension compiled
with `TORCH_CUDA_ARCH_LIST=7.5`.

## Open-weight mesh generators

The one development that matters most: **TRELLIS.2 and Pixal3D became native
ComfyUI nodes in v0.34.0 (2026-08-22).** The pipeline was re-implemented in
pure PyTorch/SciPy: no o-voxel, CuMesh, FlexGEMM or nvdiffrast compiles,
SDPA attention instead of flash-attn, an explicit bf16 to fp16 to fp32
fallback, VAE chunking, and INT8 weights (5.3 GB). That removes nearly every
install failure in the 2025 reports on both Turing and Blackwell. It is two
weeks old and nobody has yet published a Turing or 11 GB run of it.

| Model | Input, output | VRAM | Fits | Licence | Notes |
|---|---|---|---|---|---|
| **TRELLIS.2** (Microsoft, Dec 2025, 4B) | image; mesh + PBR (base, rough, metal), 4K bake | 24 GB official; ComfyUI native INT8: ~10 GB at 1024 with 8 steps, ~14 GB at 12 steps | x99 yes; t5810 at 512 only | MIT | O-Voxel representation handles **open, thin surfaces**, which is the case for bat-wing membranes where SDF models thicken or close them. Raw meshes can have small holes; hole-fill scripts ship. |
| **Pixal3D** (TencentARC, 2026) | image (multi-view option); mesh + PBR | `--low_vram` ~10-12 GB at 1024 | x99 | MIT | TRELLIS.2 backbone with pixel back-projection; front view matches the input closely. Vendors call it experimental. Also native in ComfyUI 0.34. |
| **Hunyuan3D 2.0 / 2mini / 2mv** (Tencent, 2025) | image or 1-4 views; mesh + RGB texture (no PBR) | shape 5-6 GB, with texture 12-16 GB | shape on both; texture on x99 | Tencent community licence: excludes EU/UK/KR, no training other models on outputs | #1 on community topology votes. Texture stage needs `custom_rasterizer` compiled. ComfyUI native shape nodes; kijai wrapper for texture. |
| **Hunyuan3D 2.1** | image; mesh + PBR | 10 GB shape, 21 GB paint | shape only, both | same | Last open Hunyuan. Every 16 GB report of the PBR paint stage is an OOM. 2.5/3.x/PolyGen are API-only. |
| **Hi3DGen / Stable3DGen** | image; **geometry only**, very sharp | ~8-16 GB (unstated) | both, probably | MIT, NVIDIA libs deliberately removed | xformers backend, so Turing-friendly by design. Pair with a texture pass. |
| **TripoSG** (VAST) | image; mesh only | 8 GB | both | MIT | No CUDA extensions. Occasional repetition artefacts. |
| **PartCrafter** | image; 2-16 separate part meshes | 8 GB | both | MIT | Could hand back wings, legs and body as separate shells before rigging. |
| **Hunyuan3D-Omni** | image + skeleton/points/box control; shape | 10 GB | both | Tencent | Skeleton conditioning is described for humans; untested on a winged quadruped. |
| SAM 3D Objects (Meta) | | 32 GB | no | | An "8 GB" wrapper contradicts its own README. |
| Step1X-3D, Direct3D-S2 at 1024 | | 24-29 GB | no | | |
| Sparc3D | | | | | Never released weights; became the paid Hitem3D/Hi3D. |

Independent creature benchmarks do not exist. 3D Arena (May 2025) had
TRELLIS and Hunyuan3D-2 as the top open models; topology votes favour
Hunyuan. The membrane advantage of TRELLIS.2 is an architectural inference,
not a measurement: render and look.

**Ranking for x99**: ComfyUI 0.34 native TRELLIS.2 INT8, then Hunyuan3D-2
full pipeline, then Hi3DGen for a sharper geometry pass. **For t5810**:
TRELLIS.2 native at 512, Hi3DGen, Hunyuan3D-2mini shape.

## Cloud services

| Service | Generation | Rigging | Cost for five dragons | Licence on cheap tier |
|---|---|---|---|---|
| **Tripo** (v3.1, API) | text, image, multi-view; GLB/FBX, PBR default, quad and low-poly add-ons | **Rig v2.5: biped, quadruped, hexapod, octopod, avian, serpentine, aquatic**; `tripo` or `mixamo` bone naming; free rig-check; ~25-30 credits | ~55 cr per mesh + 25-30 rig = ~$4 total at $0.01/cr; 2,000 free API credits claimed (unverified) | Free tier CC BY 4.0, non-commercial, public |
| **Rodin / Hyper3D** (Gen-2.5) | text, 1-5 images; GLB/FBX; **quad 4K-50K**, T/A-pose enforcement, PBR | **none** ("coming soon" since 2025) | fal.ai hosts it at $0.40/gen = $2; hyper3d free tier charges per download | Output use unrestricted per terms |
| **Hunyuan 3D Studio** (3.1/3.5) | text, image, up to 4 views; 8K PBR; Smart Topology quads | hosted auto-rig, "characters or animals", T-pose input, humanoid presets; skeleton undocumented | **$0**: 20 free generations/day, 1,000 promo API credits | hosted terms not fetched |
| **Meshy** (6 text, 7 image) | image; FBX/GLB/OBJ; free remesh | Humanoid, "Quadruped Dog", **Smart Rig (Beta)** for fantasy creatures, web only; API rig is humanoid only | needs one month of Pro ($20) to download current-model output | Free = CC BY 4.0 but downloads locked |
| **3D AI Studio** | aggregator: Rodin, Hunyuan, Tripo, Hi3D | own "Prism" rigger: biped, quadruped, avian, serpentine, Mixamo names | ~$0 inside 1,000 free credits/month | ownership claim unverified |
| Hi3D (ex-Hitem3D) | 1536³ geometry, 2M faces | none | | good hero sculpts, dense triangles |
| Luma Genie, CSM | discontinued (Jan 2026) | | | |
| Kaedim, Sloyd, Alpha3D, Spline, Stability SF3D | not suited: enterprise pricing, props, or dated quality | | | |

Independent 2026 reviews agree: Rodin for the best raw sculpt and quads,
Tripo for game-ready output and the only serious non-humanoid rig, Meshy the
safe default with weaker bone naming. Meshy's own page says a six-legged
dragon "needs manual rigging"; Tripo's says a dog rig "is not proof that a
tool supports wings". Expect the wings to be skinned to the spine.

The Blender MCP already has Rodin and Hunyuan integrations (Blender was not
running when checked); the bundled Rodin trial key has a daily cap and can
be swapped for a hyper3d or fal.ai key.

## Auto-rigging

The frontier moved in 2026 and it moved in our favour.

- **SkinTokens / TokenRig** (VAST, Feb 2026, MIT, successor to UniRig). One
  autoregressive model emits skeleton and weights; its RL stage rewards
  auxiliary bones for "tails, horns, wings". Blind mode outputs numbered
  bones, which is useless here. But **`--use_skeleton` takes an existing
  armature, predicts only the weights, and preserves the joint names**
  (verified in its `bpy.py`). Needs 14 GB VRAM per README, torch 2.7 cu128,
  flash-attn: x99. Known bug: exported bone tails are wrong (issue #8, fix
  posted). A wrapper claims 4 GB; unverified.
- **skin-tokens.cpp** (LocalAI, Aug 2026, Apache-2.0). C++23/GGML port,
  CPU or Vulkan, C API. `skintokens-cli skin model.gguf mesh.glb
  skeleton.glb out.glb` is exactly the weights-for-a-given-skeleton call.
  Runs on t5810 or the Mac without CUDA. Two weeks old; memory unstated.
- **UniRig** (2025, MIT, 8 GB): numbered bones, weak skinning, and its own
  README says to fix the skeleton in Blender before skinning. Superseded.
- Puppeteer, MagicArticulate, Anymate: research artefacts, old torch pins,
  numbered or text-file skeletons. RigAnything, ARMO, Rigel3D, AniGen: no
  code. Make-It-Animatable: humanoid only.
- Commercial: Tripo v2.5 and Meshy Smart Rig are the only two worth a paid
  test. Mixamo, AccuRig, Rodin's partner Uthana: humanoid only. Anything
  World lists "Winged Dragon" as coming soon.
- Blender: Rigify ships no dragon or wing metarig (basic quadruped, bird,
  cat, horse, wolf, shark); one is assembled from `basic_quadruped` plus arm
  and finger chains for wings, `basic_tail`, `super_head`. Auto-Rig Pro's
  paid Rig Library has a Dragon preset. Blender's Automatic Weights fails on
  exactly what generators produce (non-manifold, overlapping shells); Voxel
  Heat Diffuse Skinning is the classic fix but fuses jaws and fingers, and
  its Blender 5 support is unverified.
- Retopo: Quad Remesher ($79, reported working in Blender 5.0) with density
  painted high on wing fingers and jaw; Quadriflow free but needs the soup
  decimated under ~100K first. Target **25-40K triangles**, spent on the
  membranes and the head. Bake albedo and normal from the generated mesh.

## The pipeline

1. **Reference sheet.** Orthographic front, side and top of the chosen
   design with wings fully spread, legs apart and slightly bent, tail
   straight, jaw slightly open so the lower jaw is a separate volume. The
   concept-art skill does this; Embercrest panel A in
   `artifacts/dragon-options/concepts.png` is the starting design.
2. **Cloud pass first, for calibration.** Five candidates each from Rodin
   (via the Blender MCP or fal.ai) and Tripo multi-view, plus Hunyuan Studio
   for free. Run Tripo's rig on its best one with `quadruped` and `avian` to
   see what a commercial rigger does to the wings. Under $10 all in, and it
   sets the bar the local models have to reach.
3. **Local pass on x99.** ComfyUI 0.34, native TRELLIS.2 INT8, weights on
   `/mnt/Data`, 1024 cascade at 8-12 steps with `low_vram`. Compare with the
   cloud meshes in Blender, not by reasoning about them.
4. **Clean and retopo** the winner: Merge by Distance, Make Manifold, Quad
   Remesher or Quadriflow to 25-40K tris, bake textures back.
5. **Skeleton on our terms.** Fit the Embercrest skeleton from
   `tools/build_embercrest.py` to the new mesh in Blender. It already has
   the 68 bones this engine's mapper expects, by name, and the optional test
   proves every chain maps and responds.
6. **Weights by ML, names preserved.** SkinTokens `--use_skeleton
   --use_transfer` on x99, or `skin-tokens.cpp skin` anywhere. Apply the bone
   tail fix or recompute tails in Blender. Check the wing fold and the open
   jaw in Pose Mode, which is where weights bleed. Fallback: Voxel Heat for
   the body, Blender's heat weighting for jaw and fingers only.
7. **Export and accept.** glTF with skinning, tangents, deform bones only.
   Then `--bind-pose`, `--skeleton`, and studio scenarios 0 to 9 on the new
   `--model`, plus the optional rig test pointed at the new file.

## Open questions

- Nobody has run the native ComfyUI TRELLIS.2 on Turing or under 12 GB yet.
- SkinTokens' real VRAM floor (14 GB claimed, 4 GB in a wrapper) and whether
  `--use_skeleton` copes with a 68-bone skeleton; its training rigs are
  mostly under 64 bones.
- Whether any commercial rigger puts a finger chain on a winged quadruped.
- Rigel3D and AniGen (2026 papers) generate already-rigged, semantically
  named creatures from one image. Neither has code. Worth rechecking in a
  few months; if one ships, steps 4 to 6 collapse.
