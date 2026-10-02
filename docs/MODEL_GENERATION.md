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

The survey below started as web research on the date above; the sections on
Hugging Face Spaces and Hunyuan 3D Studio, and the two meshes in
`artifacts/dragon-options/`, are **measured** — generated, downloaded and
rendered the same day. Where a claim is still only a vendor's, it says so.
Prices are USD.

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
- The optional test in `tests/test_anim.cpp` loads `assets/embercrest-scripted.glb`
  when present and checks every chain maps and responds. Point it at a new
  asset and it becomes the acceptance test for the rig.

## The two machines

| | t5810 | x99 |
|---|---|---|
| GPU | RTX 2080 Ti, 11 GB, sm_75 (Turing: no bf16, no flash-attn 2) | RTX 5060 Ti, 16 GB, sm_120 (Blackwell: **cu130** — ComfyUI 0.34 disables its CUDA kernels below that) |
| OS, Python | Ubuntu 22.04, Python 3.10, CUDA 13.2 toolkit installed | CachyOS, Python 3.14 (too new; use `uv python install 3.12`), uv, docker, conda |
| RAM, CPU | 31 GB, 4 cores | 31 GB, 8 cores |
| Disk | 284 GB free | **20 GB free on root.** `/mnt/Data` (NTFS) has 69 GB free for weights |
| Torch | none | 2.14.0+cu130 in a venv |

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
| **Tripo** (v3.1, API) | text, image, multi-view; GLB/FBX, PBR default, quad and low-poly add-ons | **Rig v2.5: biped, quadruped, hexapod, octopod, avian, serpentine, aquatic**; `tripo` or `mixamo` bone naming; free rig-check; ~25-30 credits | ~55 cr per mesh + 25-30 rig = ~$4 total at $0.01/cr. **The free credits are not API credits**: a fresh account's key returns `balance: 0` from `/v2/openapi/user/balance`, because Studio and API are separate pools. Paying is the only way to reach Rig v2.5 | Free tier CC BY 4.0, non-commercial, public |
| **Rodin / Hyper3D** (Gen-2.5) | text, 1-5 images; GLB/FBX; **quad 4K-50K**, T/A-pose enforcement, PBR | **none** ("coming soon" since 2025) | fal.ai hosts it at $0.40/gen = $2; hyper3d free tier charges per download | Output use unrestricted per terms |
| **Hunyuan 3D Studio** (3.1/3.5) | text, image, up to 4 views; 8K PBR; Smart Topology quads | 绑骨蒙皮 stage, but **tested and humanoid-only**: a winged quadruped is refused with `仅支持人形标准化` | **$0**: **30** free generations/day on the web (measured), whole pipeline incl. rigging; API needs Tencent Cloud | outputs are the user's (5.3); must be labelled AI-generated (3.3); mainland-China users only (1.1); no automated extraction (3.6(3)). Read 2026-10-01, see `ATTRIBUTION.md` |
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

The Blender MCP already has Rodin and Hunyuan integrations. Its bundled Rodin
trial key is the literal string `"vibecoding"`, shared by every install, and
**it is drained**: with the key armed and the addon reporting
`Mode: MAIN_SITE. Key type: free_trial`, a multi-view generate returns
`{"error": "API_INSUFFICIENT_FUNDS"}`. Treat that integration as needing a
private hyper3d or fal.ai key. The Hunyuan side of the addon wants either a
local server (`LOCAL_API`) or Tencent `SecretId`/`SecretKey`
(`OFFICIAL_API`) — there is no bundled key at all.

To bring the addon up without a human clicking *Connect to Claude* (the
server auto-starts on load, so only the Rodin fields need setting):

```python
# blender --python this.py   -- scene props, so set them after load
import bpy, addon_utils
addon_utils.enable("addon", default_set=True, persistent=True)
def boot():
    sc = bpy.context.scene
    sc.blendermcp_use_hyper3d = True
    sc.blendermcp_hyper3d_api_key = "vibecoding"   # or a private key
    sc.blendermcp_hyper3d_mode = 'MAIN_SITE'
    if not sc.blendermcp_server_running:
        bpy.ops.blendermcp.start_server()          # EADDRINUSE = already up
bpy.app.timers.register(boot, first_interval=1.5)
```

## Hugging Face Spaces: the cheapest cloud, and its one wall

`microsoft/TRELLIS.2` is live, public, ungated, and exposes its **entire**
pipeline over the Gradio API with no key of its own — `/start_session`,
`/preprocess_image` (rembg, so a flat-grey plate is fine),
`/image_to_3d` (`resolution` 512/1024/1536, the sampler's guidance and step
counts all exposed), then `/extract_glb` (`decimation_target`,
`texture_size`). `tools/trellis2_space.py` drives it end to end; the only
credential anywhere in the path is a Hugging Face token, and that token buys
nothing but quota.

Quota is the wall. Both GPU stages are `@spaces.GPU(duration=120)`, and
ZeroGPU refuses a call whose *requested* duration exceeds the quota
remaining rather than the time it would really burn — anonymously it fails
with `120s requested vs. 178s left`. So one asset costs 240 s of
reservations against:

| Account | ZeroGPU/day | Dragons/day |
|---|---|---|
| anonymous | 120 s | **0** — cannot finish one |
| free | ~240 s | **exactly 1**, measured |
| PRO, $9/mo | 1,500 s (25 min, per the 429 itself) | ~6 |

One free account therefore unlocks not just TRELLIS.2 but Hunyuan3D-2,
Hi3DGen, TripoSG and PartCrafter, all of which have Spaces — at one asset a
day. **There is no way around the quota by picking a different Space**: every
3D-generation Space that was running when this was checked is on ZeroGPU or
`cpu-basic`, and the quota is per account, not per Space.

Two traps found by running it. **`resolution` above 512 fails**: 1024 raises a
bare `AssertionError` from the app, so the hosted demo really is 512³ whatever
the radio offers, and the failure looks like a bug rather than a limit.
And because the two stages reserve separately, **the second one can be
refused after the first has already succeeded** — a free account's second
attempt sampled fine and then died on `extract_glb` with `0s left`, leaving
the latents stranded in server-side session state with no way to pay for the
extraction. Sample and extract are one budget: don't start a second asset.

### What TRELLIS.2 actually produced

From the sheet's side view, at 512, decimated to 200K: 199,932 triangles,
`POSITION`/`NORMAL`/`TEXCOORD_0`, base colour plus metallic-roughness, no
skin (so `--model` rejects it until step 6). Rendered five ways in Workbench,
it is a real dragon and not greybox — six limbs, four legs, two separate bat
wings with distinct wing fingers, dorsal ridge, spiked tail, horned head.

- **The membrane claim is now measured, not inferred.** The wings came back
  as thin open sheets with scalloped trailing edges. No SDF thickening, no
  closed-over webbing. This is the reason to prefer TRELLIS.2 for *this*
  creature.
- **But the membranes are single-sided** (`doubleSided: false`), so in-engine
  they vanish under backface culling from below. They need a Solidify or a
  two-sided material before the asset is usable.
- **The input pose is the output pose.** The side view's wings are raised and
  swept back, so the mesh's X extent (0.517) is *half* its Z extent (0.998).
  Scaling by X to a 19 m wingspan would therefore produce a dragon roughly
  twice the intended size. Generate from a wings-level view, or re-pose
  before export.
- **The jaw is the weak spot.** The reference sheet's mouth is nearly closed,
  and the sculpt duly fused the lower jaw into the head — no separate volume
  for a `jaw`/`mandib` joint to drive. Step 1's "jaw slightly open" is not a
  nicety; it is what makes the jaw riggable.

## Hunyuan 3D Studio: the whole pipeline, hosted, 30 a day

The survey listed this as "20 free generations/day" and undersold it badly.
`3d.hunyuan.tencent.com` (the Chinese site — the global one has registration
paused) has a **3D Studio at `/studio/creation/...` whose left nav is very
nearly steps 3 to 7 of our own pipeline**, each stage separately runnable on
an asset:

| Stage | What it is | Our step |
|---|---|---|
| 概念设计 | text-to-image, image-to-multiview, with a **标准化A-pose** toggle whose tooltip is "turn this on if you need to drive the character with motion" | 1 |
| 几何生成 | image or multi-view to mesh, model `3D生成 V3.1`, face budget **1.5M / 1M / 500k / 50k** | 3 |
| 组件拆分 | split into components — the PartCrafter idea, hosted | — |
| 低模生成 | "art-grade low-poly", takes an image *or* a high-poly input | 4 (retopo) |
| UV展开 | semantic UV unwrap | 4 |
| 纹理绘制 | PBR texture paint | 4 (bake) |
| 绑骨蒙皮 | rig and skin — but **humanoid only** (`仅支持人形标准化`), despite a 生物/creature gallery card. Useless for a dragon | — |
| 动画生成 | animation from instruction or video | not needed |

The Studio counter reads **今日剩余生成次数：30** and a geometry submission
took it to 29, so 30 free runs a day, one per stage submission. The homepage
badge separately reads 20; what that one counts was not determined, so treat
30 as the Studio budget and 20 as unexplained rather than assuming they are
the same pool. This is by far the most free capacity of anything surveyed, and it
is the only free service that even claims to rig a creature. The API is the
part that needs Tencent Cloud and a one-time 100 credits; the 30/day are
web-only, hence the browser.

The multi-view upload takes eight labelled slots — 正 (front, required), 背,
左, 右, 顶, 底, 左45°, 右45° — min 2, max 8. Our four turnaround panels plus
the reference sheet's top view fill five of them, which is the best-conditioned
input we can give any generator.

### 语义UV caps at 30,000 faces, so the staged chain cannot skip retopo

`模型面数不可大于3万` — the UV stage refuses any mesh over 30 K faces. That
closes the obvious idea of running 几何生成 → 语义UV → 纹理绘制 and simply
omitting 低模生成: there is no way to get a 1.5 M mesh through UV in the
Studio. Retopo to ≤30 K is mandatory *in the staged chain*.

Two ways out, and only one is good:

- **The one-shot.** It produces 1.5 M geometry *with* `TEXCOORD_0` and three
  PBR maps in a single generation, never touching the UV or retopo stages.
  This is why the one-shot asset is the best one we have. 20/day pool.
- **Decimate locally to ≤30 K and upload** (`Upload 3D Model`, ≤150 MB), then
  run 语义UV and 纹理绘制 on that. Untested, but it is the only route that
  keeps multi-view geometry *and* gets cloud PBR.

### Verdict on 低模生成: do not use it. Decimate locally instead.

Tested by running the full chain on the corrected view plates. The retopo
stage returned **13,851 triangles**, and at that budget it erases exactly the
features the rig needs: the **wing finger bones are gone** (smooth membrane
blobs where the source has crisp separated fingers), the dorsal ridge spikes
are smoothed to a bump, and the jaw and teeth become an undefined mass. The
horn spires recorded earlier were the same stage misbehaving. It is not a
budget to tune — it is a stage to skip.

What works instead is decimating the 1.5 M geometry **locally**, which is
under our control and preserves the detail: `tools/rig_embercrest_candidate.py`
takes 1.5 M to 80 K with 61 named bones, 4 influences, no unweighted vertices
and valid tangents, and the engine loads the result and renders it textured.

So the pipeline is: **几何生成 (or the one-shot) at 1.5 M in the cloud →
decimate locally → rig locally → cloud 语义UV + 纹理绘制 if PBR is wanted.**
The Studio accepts an uploaded mesh (`Upload 3D Model`, OBJ/FBX/STL/GLB,
≤150 MB), so a locally-decimated mesh can still go back for UV and texture.

**The 20 badge is a separate pool.** The homepage counter this doc left
unexplained is the one-shot 图/文生3D allowance: 20 a day, independent of the
Studio's 30 stage submissions. The one-shot produces geometry *and* texture
together at full 1.5 M resolution with no retopo stage in the way, which is
why it yields a better asset than the staged chain.

### Better plates did not mean a better mesh

Worth recording against my own earlier recommendation. The regenerated,
proportion-corrected view plates produced a 1.5 M mesh with measurably better
proportions — wingspan/depth 1.238 against the older plates' 1.123, closer to
the design's 1.31. Rendered side by side, it is still the **worse** mesh: the
muzzle is mushy, the jaw and brow lose definition, the chest plates flatten.
The old plates, drifted and 21% too broad, gave a sharply modelled head.

Two lessons. Proportion fidelity and sculpt quality are independent axes, and
the measurement that caught the drift says nothing about the second one. And
prompting hard for "slender" bought slimness at the cost of facial structure —
the generator spent its detail budget elsewhere. Do not re-source a good mesh
to fix proportions; fix proportions on the good mesh.

### Two plate experiments that both failed

Recorded because both were my suggestions and both were wrong, which is worth
more than the successes:

- **Higher-resolution plates** (`views/`, full-frame 1536x1024) → the
  `textured-hq` one-shot, worse than the asset made from the low-resolution
  turnaround crops. Consistent with the earlier finding that the corrected
  plates give better proportions and a worse head.
- **Three consistent views instead of five** (front/back/top only, dropping
  the side plates whose wing pose contradicts them) → `textured-3v`, worse
  anatomy. The reasoning — that a multi-view generator is confused by
  contradictory wing poses — sounded right and did not survive contact. Five
  imperfect views beat three consistent ones here; the extra angles evidently
  constrain the body more than the contradiction costs.

The asset to beat remains the original one-shot from the turnaround crops.

### Four views beat five, measured two against two

The 4-view rule in `tools/hunyuan_oneshot.md` was inherited from the Stormsail
run, not measured -- and the one view-count measurement on record (three
against five, above) pointed the other way, so it was worth settling. Run on
Ashcoil, two one-shots per arm, the same four turnaround plates in both, the
fifth plate being the TOP VIEW panel cropped out of its reference sheet:

| arm | X (wingspan) | Y (length) | Z |
|---|---|---|---|
| 4 views, original | 1.1616 | 1.0678 | 0.3549 |
| 4 views, repeat | 1.1632 | 0.9978 | 0.3435 |
| 5 views, run A | 0.9969 | 1.1711 | 0.3397 |
| 5 views, run B | 0.9317 | 1.1755 | 0.3280 |

**Run-to-run variance is small enough for the comparison to mean something:**
the two 4-view runs agree on wingspan to 0.1%, and the two 5-view runs agree on
length to 0.4%. The gap *between* the arms is an order of magnitude larger --
the top plate narrows the wingspan by 14-20% and lengthens the body by 10-17%,
turning a creature wider than it is long into one longer than it is wide.

And it is worse, not merely different. Both 5-view heads have a longer, thinner
muzzle, a noisy fringed jaw line and a mane whose spikes fuse into clumps,
against two crisp 4-view heads with a clean brow and separated spikes -- the
same "mushy muzzle, jaw and brow losing definition" that the higher-resolution
plates produced. At full body the 5-view runs also lose the whip taper of the
tail, which on a serpent is most of the silhouette.

**This does not contradict the three-against-five result; it completes it.**
What the generator needs is the four cardinal views of the turnaround, because
they carry the silhouette -- dropping the two side plates is what made the
3-view run bad. The top plate adds no silhouette the sides do not already give,
and it arrives in a *different pose*: in a reference sheet the serpent's body
hangs straight down, while the turnaround extends it backward. The generator
appears to reconcile the two by splitting the difference, which is exactly what
the numbers show. **Use the four turnaround plates. Leave 顶 empty** unless the
top view is drawn in the same pose as the other four, which is the one case
this experiment does not cover and the obvious next test.

**That next test has now run, on Rimefang, and the answer is still four.** A
five-panel turnaround was generated with the top-down drawn in the *same*
standing pose as the other four, so the two arms shared four byte-identical
plates and the only variable was the fifth:

| arm | X (wingspan) | Y (length) | Z |
|---|---|---|---|
| 4 views | 0.8447 | 0.9748 | 0.7075 |
| 5 views, pose-consistent top | 0.7185 | 1.0173 | 0.6449 |

The head no longer softens -- that part *was* the pose contradiction, and
fixing the pose fixes it. But **the wingspan still narrows by 15%**, the same
direction and nearly the same magnitude as the sheet-derived top. So the
narrowing is not about a contradictory pose at all; a top-down plate is simply
taller than it is wide in frame, and the generator appears to let that bias the
fit. Two creatures, four runs in each arm's direction, one conclusion: **use
the four cardinal plates.** A fifth buys nothing measurable and costs
proportion.

Driving all of this is `tools/hunyuan_oneshot.py`, which is the runbook in
`tools/hunyuan_oneshot.md` made executable -- the three traps in that document
are all silent successes, so every step asserts what it matched.

### Triangle count is not the bottleneck: measured

Before optimising the dragon mesh for low-end GPUs, measure. Three rigged
models through `--headless --frames N`, timing the *slope* between 300 and
2400 frames so process startup and texture upload are excluded:

| Model | tris | images | startup | **ms/frame** |
|---|---|---|---|---|
| `dragon.glb` | 37,998 | 9 | 0.93 s | **5.01** |
| `embercrest.glb` | 80,000 | 3 | 0.88 s | **2.68** |
| `embercrest-selected.glb` | 80,000 | 1 | 0.21 s | 3.47 |

**The 80 K model is faster than the 38 K one.** At this scale — tens of
thousands of triangles, one or a few characters — the dragon mesh does not
predict frame time, and a low-poly dragon will not measurably help. Whatever
`dragon.glb` costs extra is not its triangles; it has nine images against
three, so material and draw-call structure is the likelier culprit.

Note also that the 4096² PBR set costs **0.88 s of startup against 0.21 s**
and nothing measurable per frame. Its real cost on a low-end card is VRAM,
not shading — three 4096² maps with mips is roughly 270 MB.

Measure the slope, not the total: on a 600-frame run the totals ranked the
PBR asset *slowest*, which was entirely its texture upload.

### One worry that turned out not to apply

This doc flags single-sided membranes needing a Solidify or two-sided material
before the asset is usable. **Not for this engine.** Every generated asset
here declares `doubleSided: true`, and the renderer sets
`SDL_GPU_CULLMODE_NONE` on all its pipelines (`src/gfx/world_renderer.cpp`,
`foliage.cpp`, `particles.cpp`, `debug_draw.cpp`), so nothing is
backface-culled. The concern is real for other engines; here it costs nothing.

### The runbook, stage by stage

Four submissions of the 30 daily credits take a set of reference views to a
UV'd, textured, quad-topology dragon. Everything below was run this way; the
per-stage caveats are expanded in the subsections that follow.

**Inputs.** Plain PNGs with the flat grey plate still on them are fine — the
geometry workflow runs `hunyuan-3d-images-subject-segmentation` before
anything else and cuts the subject out itself. This is the opposite of the
local path, where `LoadImage` + `InvertMask` + an RGBA cutout are mandatory.
Do not pre-cut for the cloud; it is wasted work.

| # | Stage | Route | Cost | Wall clock | Out |
|---|---|---|---|---|---|
| 1 | 几何生成 | `/studio/creation/geo` | 1 | ~7 min | 1.5 M faces, watertight, no UV |
| 2 | 低模生成 | `/studio/creation/poly` | 1 | ~7 min | ~20 K faces, quads |
| 3 | UV展开 | `/studio/creation/uv` | 1 | ~2.5 min | adds `UVMap` |
| 4 | 纹理绘制 | `/studio/creation/texture` | 1 | ~6 min | GLB + FBX + 4 x 4096² PBR maps |

1. **几何生成 — geometry.** Switch 上传单图 to **上传多视图**, then click the
   `添加多视图（Min2，Max8）` row, which is what creates the eight file
   inputs; they do not exist before that click. They sit in DOM order 正
   (front, required), 背, 左, 右, 顶, 底, 左45°, 右45°. The five files to put
   in the first five slots are committed, named in that order, at
   **`artifacts/dragon-options/views/`** — `1-front.png` through
   `5-top.png`; leave 底图 and the two 45° slots empty. Then leave
   模型面数 at its default 1.5M and the model at `3D生成 V3.1`, and submit
   with 立即生成.

2. **低模生成 — retopo.** It picks up stage 1 automatically. Choose 拓扑选择
   **四边面** and a face budget; `中` gave 19,977. Two traps: the horns come
   back as spires at `中` (try `高`), and **a GLB cannot carry quads** — the
   file arrives triangulated, so take the `fbxUrl` if the quad topology is
   the point.

3. **UV展开 — unwrap.** The button here is **智能展开UV**, not 立即生成.

4. **纹理绘制 — texture.** Pick **图生纹理** and give it
   `views/1-front.png`, the same reference the geometry came from
   (文生纹理 is text-only, and multi-view input is also accepted). The upload goes into the **second** file input on
   the page — index 0 belongs to the 本地模型 row above it, and uploading
   there leaves 立即生成 disabled with no error.

**Skip 绑骨蒙皮.** It returns `仅支持人形标准化` on a six-limbed dragon.

**Retrieving results — use the API, not the viewer.** The in-page 3D viewer
sits on `加载中...` for minutes on a 1.5 M mesh and there is no reliable
download button to find. Every stage's output is in one call:

```js
// in the page, so the session cookie comes along
const r = await fetch('/api/game3d/general_info/get_works_list',
    {method:'POST', credentials:'include',
     headers:{'Content-Type':'application/json'}, body:'{}'});
const t = await r.text();
// NOTE: the response is TWO concatenated JSON objects, an error line then the
// real one. Split on newlines and take the object that has data.list.
const j = t.trim().split('\n').map(l => { try { return JSON.parse(l) }
                                          catch(e) { return null } })
           .filter(Boolean).find(l => l.data && l.data.list);
const w = j.data.list[0];            // newest first; w.status 1 = running, 2 = done
```

`w.modelInfo.<stage>Rsp` then holds the URLs — `geometryGenerationRsp.glbUrl`,
`texturePaintingRsp.glbUrl` / `.fbxUrl` / `.pbrImageUrl` /
`.pbrRoughnessImageUrl` / `.pbrMetallicImageUrl` / `.pbrNormalImageUrl`. Those
are plain COS HTTPS links that `curl` fetches without auth.

**Poll that API, not the page text.** Two ways this went wrong: a "50%"
progress reading was actually the *"Save Up to 50%"* upgrade banner, and
opening another site in the adopted tab silently redirected the poll to the
wrong page so a running job read as finished. Poll `w.status` and match on the
`workFlow` string for the stage you submitted.

### Does input resolution matter? For geometry, no — measured

The obvious way to improve any of this is to feed it bigger images. For the
conditioning stage that is wasted effort, and ComfyUI's own configs say so.
`comfy/clip_vision.py` preprocesses to a fixed `image_size` square taken from
the encoder config, and the two encoders in play are:

| Encoder | Used by | `image_size` |
|---|---|---|
| `dino2_large` | Hunyuan3D-2mv | **518** |
| `dino3_large` | TRELLIS.2, Pixal3D | **224** |

So every view is resized to 518x518 or 224x224 before the model ever sees it.
The `ImageCropToMask` 1024x1024 in the TRELLIS graph is not the resolution the
conditioning uses; DINOv3 still takes 224 from it. Feeding a 4096-pixel plate
to local geometry conditioning changes nothing.

Where the pixels *do* plausibly earn their place is the cloud's 纹理绘制
stage, which emits 4096² maps and has to get surface detail from somewhere,
and in being able to see the design yourself. `artifacts/dragon-options/views/`
is therefore generated one full-frame view per image (~1450 px of dragon)
rather than cropped from a four-panel sheet (~540 px), while the measured
results elsewhere in this doc used the smaller turnaround crops — which are
reproducible from `embercrest-turnaround.png` with the `magick` loop in step 1
above, if a run needs to be repeated exactly.

### Image-to-image drift compounds, and it is invisible in a bounding box

Regenerating the five view plates at full resolution made them worse, in a way
worth writing down because the obvious check does not catch it.

Each plate was generated image-to-image from `embercrest-turnaround.png` —
which is itself one generation removed from the reference sheet. Normalising
every front view to the same wingspan and measuring body breadth across the
four feet, where the wings cannot reach:

| Front view | body breadth / wingspan |
|---|---|
| `embercrest-reference-sheet.png` (the design) | **0.283** |
| `embercrest-turnaround.png` (1 pass from it) | 0.316 (+12%) |
| first hi-res views (2 passes) | **0.341 (+21%)** |
| regenerated from the reference panels | 0.254 |

Monotonic. **Every image-to-image pass fattened the dragon**, and the drift
compounds because each generation becomes the next one's authority.

The trap is that the *bounding box does not move*. At equal wingspan the three
front views are 509, 506 and 508 px tall — within 0.6%. Wingspan-to-height,
the ratio you would naturally check, reads 1.77 for both the reference and the
drifted plate. The silhouette envelope is identical; what changed is the mass
inside it, thicker torso, broader chest, heavier legs. Only a measurement
that ignores the wings (breadth across the feet) sees it, and a human eye sees
it immediately — this was caught by being told the proportions looked wrong,
not by any check of mine.

Two rules out of it. **Always reference the earliest authority**, the original
reference sheet, never the most recent derivative. And when a generated asset
is regenerated, **measure a proportion that excludes the dominant feature** —
here the wings dominate the bbox and hid a 21% change in body mass.

A related miss in the same batch: the first top-down plate came back with a
wingspan-to-length ratio of 0.74 against the design's 1.31, wings far too
narrow for the body. Stating the target ratio in the prompt fixed it (1.51
after two attempts). Top-down is the view where wingspan is most obviously
wrong and the easiest to not look at.

### The stages are gated in order, and that order is ours

The stage routes are `/studio/creation/{concept,geo,comp,poly,uv,texture,rs,ae}`
(`rs` is 绑骨蒙皮 — `/rig` redirects to concept). They chain automatically:
open a later stage and it already reports the previous stage's output as its
input, so there is no re-uploading between stages.

**绑骨蒙皮 refuses a raw generated mesh.** With the 1.5M-face geometry as
input the rig stage loads, reports `模型面数 1500000 / 顶点数 749994`, and
leaves 立即生成 carrying `t-is-disabled`. 低模生成 with the same input is
enabled. So Hunyuan enforces retopo-before-rig — which is exactly the order
`docs/MODEL_GENERATION.md` step 4 then 5 already prescribed, arrived at
independently. Do not fight it; run the low-poly stage first.

低模生成 is also a free Quad Remesher: model `低模拓扑 V1.5`, face budget
低/中/高, and **拓扑选择 三角面 / 四边面 — quads**. That is step 4's $79
Quad Remesher line item, at one free run.

### The retopo result, and its one bad artefact

`中` + `四边面` on the 1.5M mesh returned **19,977 faces / 10,486 verts** in
about seven minutes — just under step 4's 25-40K target, and the body holds
up: wings, membranes, legs, feet, tail and dorsal ridge all survive
(`artifacts/dragon-options/embercrest-lowpoly-retopo.png`).

Three things to know before relying on it:

- **Asking for quads does not get you quads through a GLB.** The downloaded
  mesh is named `..._repair_quad.obj` yet arrives as 19,977 triangles and
  zero quads, because **glTF 2.0 cannot represent quads at all** — the
  exporter triangulates. The work record carries an `fbxUrl` beside its
  `glbUrl`; take the FBX (or OBJ) if the quad topology is the point.
- **The horns become two enormous thin spires.** They rise to roughly the
  body's own height above the head and are continuous mesh, not stray
  vertices — the farthest vertex sits at 0.936 from the median centre where
  p99 is 0.766, so no outlier filter will catch them. They double the
  model's height (Z extent 0.998 vs the source's 0.518), which means
  **the retopo output's bounding box cannot be trusted for the engine's
  scale-by-extent step** until the horns are fixed. Try `高` instead of `中`,
  or trim them in Blender.
- **1,190 non-manifold edges and 1,110 boundary edges.** The boundary edges
  are the membranes being open sheets and are expected; together they are
  precisely the input that the doc's auto-rigging section notes makes
  Blender's Automatic Weights fail. Plan on the ML skinner, not Blender's.

### 绑骨蒙皮 is humanoid-only: **仅支持人形标准化**

Answered, and the answer is no. With the 19,973-face low-poly as input the
rig stage loads it and 立即生成 loses `t-is-disabled` — so the earlier
refusal really was polycount — but submitting returns
**`仅支持人形标准化`**, "only humanoid standardisation is supported".

That is worth more than it looks. Hunyuan's own marketing and the gallery
card 绑骨蒙皮生物 ("rig and skin — creature") both imply animals, and the
cloud table's "characters or animals" came from that. It is wrong: the
stage takes humanoids only, and a six-limbed dragon is refused outright
rather than rigged badly. So the *free* rigging path is closed, and the
survey's "every auto-rigger is humanoid-first" holds up under test rather
than only in documentation.

What remains for rigging is unchanged from the survey: Tripo's Rig v2.5
(paid, the only one claiming quadruped *and* avian), or our own plan —
fit the named 68-bone Embercrest skeleton and let SkinTokens compute only
the weights. Note also the trap this stage would have set even if it had
worked: it emits its own skeleton with its own names, and this engine maps
joints by name substring, so a Hunyuan rig would have needed renaming
anyway.

### Driving it with chrome-use, since the 30/day are web-only

Three things cost real time and will cost it again:

- **`@refs` churn on every re-render.** This is a TDesign/Vue app that reuses
  nodes, so a ref captured in one snapshot is usually stale by the next click.
  Drive it with `eval`, or tag nodes (`el.setAttribute('data-cu', ...)`) and
  address them by CSS selector.
- **TDesign radios ignore `input.click()`.** Selecting 上传多视图 needs a full
  pointer sequence dispatched at the `.t-radio__label` span:
  `for (const t of ['pointerdown','mousedown','pointerup','mouseup','click'])
  label.dispatchEvent(new MouseEvent(t, {bubbles:true, cancelable:true}))`.
- **The eight file inputs do not exist until the "添加多视图" row is clicked**,
  and once `chrome-use upload` fires, the React dropzone consumes the file and
  clears `input.files` — so `files.length` reads 0 on a *successful* upload.
  Verify instead by watching that slot's placeholder label disappear from the
  panel text.

### What Hunyuan produced, and how it compares

Five slots filled (the four turnaround panels plus the sheet's top view),
`3D生成 V3.1`, 1.5M budget: **exactly 1,500,000 faces, one object, and no
UVs, no materials and no textures.** 几何生成 really is geometry only — UV
unwrap, texture paint and rigging are separate stages, each costing another
run. 27 MB of GLB, which is also why the in-browser viewer sat on
`加载中...`: don't ask for 1.5M if you want to preview it in the page.

All three candidate GLBs are kept at `assets/embercrest-cand-*.glb`
(`trellis2`, `hunyuan-1p5m`, `hunyuan-lowpoly`), gitignored by the existing
`assets/*.glb` rule (for size; the terms were read on 2026-10-01, see
`ATTRIBUTION.md`).

Rendered the same five ways as the TRELLIS mesh
(`artifacts/dragon-options/embercrest-mesh-candidates.png`, TRELLIS on top,
Hunyuan below), **Hunyuan wins clearly on the two things that matter to the
rig**:

- **The jaw is open, with teeth, as a separate volume.** The multi-view input
  included the turnaround's front panel, whose mouth is open, and the sculpt
  kept it. This is the `jaw`/`mandib` volume TRELLIS fused away.
- **Symmetry is near-perfect and the wings are the widest extent**
  (X 1.163 vs depth 1.062), because the front panel showed both wings spread
  and level. So the engine's scale-by-X-extent lands much closer to right.
- Wing fingers are individually modelled as distinct bones under the
  membrane; the dorsal ridge runs unbroken from horns to tail tip; feet have
  real toes and claws rather than TRELLIS's blobs.
- Membranes are again **thin single-sided sheets**, so the Solidify or
  two-sided-material fix is needed whichever generator wins.

The lesson is about input, not vendor: **the pose and expression you give it
is the pose and expression you get back**, and a five-view spread-wing
open-jaw input beat a one-view swept-wing closed-mouth input by more than the
model choice did. Feed generators the turnaround, not the reference sheet.

## Local generation on x99: measured, and it is the cheap path after all

TRELLIS.2 now runs locally. From the turnaround's front view it produces a
correct dragon in **34 seconds at 7.7 GB peak VRAM**, which is roughly half
what this doc estimated and leaves an RTX 5060 Ti two-thirds idle. There is
no per-day quota, no account, and no upload. For iterating on the mesh this
beats every cloud option tried.

| Run | Target res | Time | Peak VRAM | Faces |
|---|---|---|---|---|
| local TRELLIS.2 INT8 | 1024 | 32 s | 6.3 GB | 3.3 M |
| local TRELLIS.2 INT8 | 1536 | 34 s | **7.7 GB** | 7.1 M |

1536 is visibly smoother than 1024 -- cleaner membranes, less faceting on the
dorsal ridge and tail -- and costs two seconds and 1.4 GB, so there is no
reason to run 1024. Neither is close to the 16 GB ceiling; the earlier
"~10 GB at 1024, ~14 GB at 12 steps" figures were for the original repo, not
the ComfyUI native INT8 path. Result in
`artifacts/dragon-options/embercrest-local-trellis2.png`, mesh at
`assets/embercrest-cand-trellis2-local-1536.glb`.

The wings come out as the widest extent (X 0.999 against 0.943 of depth, a
ratio of 1.06), which is what the engine's scale-by-X-extent needs — the same
spread-wing front view that fixed this in the cloud fixes it locally.

### Setup, and the four things that actually cost time

Weights live on `/mnt/Data/models` (69 GB free) and the venv on root, which
had 20 GB. Point ComfyUI at them with an `extra_model_paths.yaml` naming
`base_path: /mnt/Data/models`. Only ~8 GB of weights is needed:
`trellis_2_int8_convrot` (4.89 GiB), `dino_v3_vit_l` (1.13), and the two VAEs
— all from `Comfy-Org/TRELLIS.2`. Skip the 9.63 GiB bf16 UNet.

- **ComfyUI 0.34 wants cu130, not cu128.** This doc used to say Blackwell
  needs "torch 2.7+ / cu128". On cu128 the server logs *"You need pytorch
  with cu130 or higher to use optimized CUDA operations"* and reports
  `comfy_kitchen backend cuda: disabled` — which is where the INT8 convrot
  dequant kernels live, i.e. exactly what an INT8 model needs. Its torchaudio
  wheel also links `libcudart.so.13` and fails to load, taking the whole
  server down at import. Installing torch/vision/audio from
  `download.pytorch.org/whl/cu130` (torch 2.14.0+cu130) fixes both and flips
  the CUDA backend to enabled.
- **`/mnt/Data` being NTFS is not the problem it looks like.** Mounted with
  the `ntfs3` driver it supports symlinks *and* hardlinks, so the HF cache
  needs no copy fallback. But uv's cache must sit on the *same* filesystem as
  the venv or every install silently degrades to full copies; keep the cache
  on root beside the venv, or pass `UV_LINK_MODE=copy` knowingly.
- **`pkill -f "main.py --listen"` kills the shell that runs it.** The pattern
  matches the invoking shell's own command line, so the server never starts
  and the log is not even truncated — it looks like a silent failure. Use a
  pattern that cannot match itself (`main[.]py --listen`), or just run the
  server under tmux, which is what `~/start_comfy.sh` does.
- **`LoadImage`'s MASK output is `1 - alpha`** (`nodes.py:1788`), so it marks
  the *background*. Wiring it straight into `ImageCropToMask` inverts subject
  and background, and TRELLIS.2 dutifully sculpts the plate: the first run
  returned a flat rectangular slab, 0.059 deep against 1.0 tall, with the
  dragon in shallow bas-relief on its face. Put an `InvertMask` in between.
  Nothing errors — the mesh is just wrong, which is the whole argument for
  rendering every result.

### Running it headless

The templates ship in the editor's format, so `tools/comfy_workflow.py`
converts one to the API format `/prompt` wants, and `tools/trellis2_local.py`
builds and submits the geometry graph directly. Three traps in the conversion,
all recorded in that file: widget values are positional against
`/object_info` order, a `control_after_generate` input eats a second slot,
and — the one that silently severs the graph — **`PreviewImage` in 0.34 is a
pass-through with a real output**, so dropping "preview" nodes by name loses
the image feeding `Trellis2Conditioning`.

Two more worth knowing: slice to a node whose schema says
`output_node: true`, because `MeshToFile3D` is not one and will not make the
graph run (`SaveGLB` is the mesh output that needs no browser viewport
state); and the shipped template hides a second 5.2 GB Pixal3D UNet plus MoGe
behind `ComfySwitchNode`s, which a plain dependency slice keeps alive.
Resolving the constant switch first drops that branch — the converter's
`--set-bool 316=true` picks TRELLIS.2 and takes the graph from 55 nodes to 21.

## The comparison, measured on one input

Every row below is the same input -- the turnaround's front view, cut to RGBA
-- so the differences are the tool, not the prompt.

| Path | Where | Time | Peak VRAM | Faces | Textured | Cost |
|---|---|---|---|---|---|---|
| ComfyUI TRELLIS.2 INT8 @1536 | RTX 5060 Ti 16 GB | **34 s** | 7.7 GB | 7.09 M | no | free |
| ComfyUI TRELLIS.2 INT8 @1536 | RTX 2080 Ti 11 GB | 164 s | 8.9 GB | 7.17 M | no | free |
| TRELLIS.2 HF Space @512 | ZeroGPU | ~1 min | — | 0.2 M | yes, PBR | 1/day |
| Hunyuan Studio 几何生成 | cloud | ~7 min with queue | — | 1.5 M | no | 30/day |
| Tripo v3.1 Best Quality | cloud | ~6 min | — | 1.96 M | yes | 55 cr, **export paywalled** |

The two local runs are **not** the same mesh, and every aggregate said they
were. Face counts differ by 1.1 % (7,094,360 vs 7,174,220), extents agree to
three decimals, surface area is identical to four, and mean dihedral angle
differs by one degree. Rendered side by side from one camera, though, the
Turing output is plainly coarser -- broad flat facets across the wing
membranes and along the tail where the Blackwell run is smooth, at 6.5 %
pixel RMSE (`artifacts/dragon-options/embercrest-gpu-quality-gap.png`).

The statistics missed it because the marching-cubes output carries a mass of
near-degenerate faces -- 90th-percentile dihedral is 179 degrees -- and those
swamp the smooth regions where the two actually differ. The likely cause is
precision: sm_75 has no bf16 units, so those tensors take a different path and
the isosurface comes out blockier. **Treat t5810 as a fallback that is both
4.8x slower and visibly lower quality, not as an equivalent box.** And note
which check caught this: not the face count, not the bounding box, not the
dihedral histogram, but rendering both and looking.

### Turing works, and is 4.8x slower

The open question -- can the native ComfyUI path run on the 2080 Ti and under
12 GB -- is answered **yes**. `torch 2.14.0+cu130` still ships `sm_75` in its
arch list, and ComfyUI 0.34 reports `comfy_kitchen backend cuda: disabled:
False` on Turing exactly as it does on Blackwell.

It is just slow. 164 s against 34 s for an identical graph, and it wants
*more* VRAM doing it (8.9 GB vs 7.7 GB). That is not raw throughput: the same
fp16 matmul benchmark runs **faster** on the 2080 Ti (0.13 s vs 0.18 s for
20x4096³), which its wider memory bus would predict. The gap is the quantised
path — Blackwell has native INT8/FP8 acceleration that Turing lacks, and
sm_75 has no bf16 units, so those tensors get widened and the intermediates
grow. Use t5810 when x99 is off; do not expect it to be the fast box.

### A second local model: Hunyuan3D-2mv, and the membrane question settled

ComfyUI 0.34 also has native Hunyuan3D v2 nodes, including
`Hunyuan3Dv2ConditioningMultiView` which takes **front, left, back and right**
— our four turnaround panels, the input that produced the best cloud result.
One 4.93 GB checkpoint (`tencent/Hunyuan3D-2mv`, loaded through
`ImageOnlyCheckpointLoader`, which yields model, CLIP-vision and VAE together)
and the shipped `3d_hunyuan3d_multiview_to_model` template converts cleanly
with `tools/comfy_workflow.py` — a second, independent test of that converter.

On the same 2080 Ti, against TRELLIS.2's one-view run:

| Local model | Time | Peak VRAM | Faces | Boundary loops | Non-manifold |
|---|---|---|---|---|---|
| TRELLIS.2 INT8 @1536 | 164 s | 8.9 GB | 7,174,220 | 20,619 | 166,008 |
| Hunyuan3D-2mv, 4 views | **65 s** | **5.4 GB** | **228,332** | **89** | 56,891 |

Hunyuan is 2.5x faster, uses 3.5 GB less, and returns a mesh 31x smaller that
is *already* near step 4's 25-40K target rather than 7 M triangles of
marching-cubes soup needing retopo first.

**And it settles the membrane question the survey could only infer.** Same
creature, same views, two architectures:

- TRELLIS.2: **20,619 boundary loops** — the wings really are thin open
  sheets, exactly the O-Voxel behaviour this doc predicted and the reason it
  was ranked first for a bat-winged creature.
- Hunyuan3D-2mv: **89 boundary loops**, i.e. watertight. The membranes come
  back as thin *solid* volumes, which is the SDF/occupancy thickening the
  survey warned about.

Watch the trap in reading that, though. The membranes look perforated in a
render — a regular stippled lattice across them — and the obvious conclusion
is holes. The boundary count says otherwise: 89 loops cannot be a perforated
sheet. It is quantisation relief on a solid surface from `octree_resolution`
256, so the fix is a higher octree resolution, not hole-filling. The render
found the artefact and only the measurement identified it; neither alone was
enough.

Which to prefer depends on the step. For a hero sculpt to retopo by hand,
TRELLIS.2's open membranes are truer to the design. For something already
close to a game budget with clean watertight topology, Hunyuan3D-2mv at four
views is the better start, and the wings can be re-thinned.
(`artifacts/dragon-options/embercrest-local-hunyuan3d-mv.png`,
`assets/embercrest-cand-hunyuan3dmv-local.glb`.)

### Improving the outputs: octree resolution is the one lever that mattered

The stipple on Hunyuan's membranes was diagnosed above as quantisation relief
rather than holes. Raising `VAEDecodeHunyuan3D`'s `octree_resolution` from its
default 256 to its maximum 512 **removes it completely**
(`artifacts/dragon-options/embercrest-hunyuan-octree-fix.png`, before left,
after right) and improves the mesh on the measure that matters for rigging:

| Hunyuan3D-2mv, 4 views | Time | Peak VRAM | Faces | Boundary | Non-manifold |
|---|---|---|---|---|---|
| octree 256 (default) | 65 s | 5.4 GB | 228,332 | 89 | 56,891 |
| **octree 512** | 397 s | 7.2 GB | 712,216 | 90 | **19,007** |
| octree 512 + latent 4096 | ~400 s | 7.0 GB | 748,714 | 109 | 21,966 |

Non-manifold edges fall 3x. The cost is 6x the wall clock, all of it in the
decode rather than the diffusion. Worth it: this is the mesh that goes into
retopo, and non-manifold geometry is what makes auto-weighting fail.

The third row is the useful negative result. Raising
`EmptyLatentHunyuan3Dv2`'s token budget from 3072 to 4096 — the obvious "more
detail" knob — bought 36 K more faces and made the mesh *worse*
(21,966 non-manifold against 19,007) for the same time. **The decode
resolution was the lever; the diffusion budget was not.** Leave the latent at
3072.

`assets/embercrest-cand-hunyuan3dmv-oct512.glb` is the best local mesh so
far, and `artifacts/dragon-options/embercrest-hunyuan-best.png` is what it
looks like from four sides.

### Pixal3D multi-view: a failure, and probably my fault

`Pixal3DMultiViewConditioning` looked like the obvious way to give the
TRELLIS family the four views that made Hunyuan good — it takes front, left,
back and right directly, and unlike `Pixal3DConditioning` it needs no MoGe,
only a `fov` float. Downloaded `pixal3d_multiview_int8_convrot` (5.2 GiB) and
the Pixal3D-flavoured `dino_v3_L_naf_fp32`, ran the same four RGBA views at
1536 with `fov=20`.

It came back **worse than everything else**: 13,613,326 faces, heavily
faceted with visibly torn membranes, and **1,485,327 non-manifold edges** —
two orders of magnitude worse than Hunyuan's 19,007. 410 s, 9.4 GB peak.

Two causes are plausible and this run cannot separate them:

1. **The `fov` was a guess.** The shipped template does not hardcode it; it
   derives it per-image with `MoGeGeometryToFOV`, which is the step I skipped
   as an unnecessary dependency. Pixal3D back-projects pixels using camera
   geometry, so a wrong field of view misaligns the four views against each
   other and the reconstruction tears — which is exactly what the render
   shows.
2. **Turing faceting.** TRELLIS.2 on this same card was already visibly
   coarser than on Blackwell, and Pixal3D is the same architecture; this may
   be that effect amplified.

Either way the lesson is the first one: **the template computed that fov for
a reason, and dropping a dependency because it looks optional is how you get
a confidently wrong mesh.** Re-run on x99 with MoGe wired in before judging
Pixal3D on this.

### Tripo v2.5 is the exportable tier, and it is only 15 a month

v3.1 will not export on the free plan. **v2.5 will** — the free plan allows
15 exports a month, and the download is a plain GLB. It also costs 40 credits
against v3.1's 55.

What comes back is a genuinely different trade from the local models:
143,848 faces with **a UV layer and a material already on it**, where every
local run here is geometry only. But it keeps thin open sheets like TRELLIS.2
does (24,304 boundary edges) rather than solidifying them, and its wings come
back curled rather than spread, so its X extent is 0.556 against a depth of
1.0 — the opposite of what the engine's scale-by-X-extent wants, and a
regression from the spread-wing input it was given.

### SkinTokens tested on t5810: it runs, and it is not usable yet

Tested end to end via `ComfyUI-SkinTokens` on the 2080 Ti, with our validated
80 K/62-bone asset as input and `use_skeleton` on. **It runs, and the result
is not usable.**

Getting it to run took four fixes, all worth writing down:

- **FlashAttention.** The node imports `flash_attn_interface` first and falls
  back to `flash_attn`; both are FlashAttention and FA2 needs sm_80+, so
  neither works on Turing, and there is no SDPA path upstream. Dropping a
  small `flash_attn_interface.py` shim on the venv path — SDPA behind the FA3
  signature, transposing (B,S,H,D) to (B,H,S,D) and returning `(out, None)` —
  satisfies that first import without patching any upstream file. Verified
  bit-exact against a manual SDPA reference.
- **A second, separate FA dependency.** `tokenrig.py` passes
  `attn_implementation="flash_attention_2"` straight to HuggingFace, which the
  shim cannot intercept. Patched to `"sdpa"`.
- **Two Python environments.** In "Headless (Blender)" mode the bpy server runs
  under *Blender's bundled* Python, not the ComfyUI venv, so its dependencies
  must be installed there separately: `bottle`, `scipy`, `trimesh`, `tornado`
  at import time and `dill` at request time. A missing request-time module
  surfaces as `UnpicklingError: invalid load key` in the client, because the
  server's text error page is fed to `pickle.loads`.
- Blender itself: t5810 has no sudo, so 4.5.3 goes in `$HOME/blender` and on
  `PATH`.

**VRAM is a non-issue: 4.9 GB peak, 35 s per run.** The upstream README's
14 GB is wrong for this path by a factor of three, so the 11 GB card is fine.

What comes back is the problem. With `use_transfer=False`:

| | input (gpt rig) | output |
|---|---|---|
| joints | 62, named `root`/`chest`/`wing_finger_1a_l`… | **59, named `bone_0`…`bone_58`** |
| attributes | POSITION, NORMAL, TEXCOORD_0, TANGENT, JOINTS_0, WEIGHTS_0 | POSITION, NORMAL, JOINTS_0, WEIGHTS_0 |
| verts | 73,550 | 239,950 (fully split) |
| images | 3 | **0** |

`use_skeleton` did **not** preserve our joint names, and this doc's own
constraint section says a numbered skeleton maps to nothing —
`src/anim/dragon_rig.cpp` finds joints by name substring. The bone count also
differs (59 vs 62), so a positional `bone_N` to our-name remap is not safe
either. UVs, tangents and all three textures are gone.

And `use_transfer=True`, the flag whose tooltip promises to preserve textures,
materials and mesh quality, **crashes at export**:
`KeyError: bpy_prop_collection[key]: key "Embercrest_Textured_Rig" not found`
— it looks the input armature up by name in a scene where it was never
created. That is the path that would have preserved what we need.

**Conclusion: not a replacement for the Blender rigger today.** The blocker is
not compute, it is that the one flag that preserves the asset is broken and
the one that works discards the naming the engine depends on. Worth
re-checking when the node moves; the environment work above is done and
recorded, so a retest is cheap.

The cheaper answer to rigging cost is that `tools/rig_embercrest_candidate.py`
is now parameterised and its skeleton is data, so re-rigging a new creature of
the same anatomy costs one Blender run and no LLM time at all.

### Cloud Hunyuan beats every local run, and the reason is the model generation

Tested directly, and the read that cloud looks sharper is correct — by more
than "sharper features". Same creature, head close-ups in
`artifacts/dragon-options/embercrest-cloud-vs-local-head.png` (cloud, local
2.0-mv, local 2.1):

| Hunyuan | Faces | Boundary | Non-manifold | Time |
|---|---|---|---|---|
| **cloud 几何生成 (v3.1)** | 1,500,000 | **0** | **0** | ~7 min with queue |
| local 2.0-mv, octree 512, x99 | 708,172 | 86 | 19,421 | 226 s |
| local 2.0-mv, octree 512, t5810 | 712,216 | 90 | 19,007 | 397 s |
| local 2.1, single view, x99 | 356,022 | 1,051 | 21,590 | 212 s |

**The cloud mesh is watertight and fully manifold — zero boundary edges and
zero non-manifold edges at 1.5 M faces.** No local run gets within three
orders of magnitude of that, and visually it has crisper horns, cleaner crest
spikes and finer scale relief.

I could not beat it locally, and the reason is structural rather than a
setting I missed. The cloud runs `hunyuan-3d-views2geometry-v3.1`. The newest
*open* Hunyuan is 2.1, and this doc already recorded that 2.5, 3.x and PolyGen
are API-only. Downloading 2.1 (6.9 GB) and running it confirmed the gap
rather than closing it: it scored *worse* than 2.0-mv on every measure,
because 2.1 ships no multi-view variant, so it gives up the four-view
conditioning that is worth more here than the newer weights.

One other thing this settles: for Hunyuan, **Blackwell and Turing produce
near-identical meshes** (19,421 vs 19,007 non-manifold) and differ only in
speed, 226 s against 397 s. The visible quality gap measured earlier between
those two cards was specific to TRELLIS.2, not a general property of the box.

So the honest recommendation is the one already being acted on: **use the
cloud for the hero geometry.** Local generation is for iteration — no quota,
four minutes, no browser — and the cloud for the asset that ships.

### Yes, cloud Hunyuan does UV and texture, and it beats Tripo at it

Asked and answered by running it. The Studio's 语义UV and 纹理绘制 stages
chain onto the retopo automatically, one credit each:

- **语义UV** (`hy-3d-semantic-uv-v3.0`) — ~2.5 min, adds a `UVMap`.
- **纹理绘制** (`hunyuan-3d-image2texture-v3.1`) — takes 文生纹理 (text) or
  **图生纹理** (image), so it can be driven from the same reference image the
  geometry came from. It also accepts multi-view input for texturing.

What comes back is a complete game-ready asset: a **19,965-face quad mesh
with UVs, as both GLB and FBX, plus four separate 4096x4096 maps — base
colour, roughness, metallic and normal.** The normal map matters: this
engine only samples one where the primitive carries tangents, and this is the
first thing in the whole survey that produced one.

Against Tripo v2.5, which was the reason to consider doing UV and texture by
hand: Tripo gives one material on a 144 K triangle mesh, 15 exports a month.
Hunyuan gives a 20 K quad mesh with a 4K PBR set, and geometry, retopo, UV
and texture together cost **4 of 30 daily credits**. There is no reason to do
this manually.

(`artifacts/dragon-options/embercrest-hunyuan-textured.png`, mesh at
`assets/embercrest-cand-hunyuan-cloud-textured.glb`, maps in
`assets/embercrest-textures/` — both gitignored, for size.)

### The field, ranked

`artifacts/dragon-options/embercrest-model-field.png`, one camera, left to
right: Hunyuan octree 512, TRELLIS.2, Tripo v2.5, Pixal3D multi-view.

| | Faces | Non-manifold | Textured | Time | Verdict |
|---|---|---|---|---|---|
| **Hunyuan3D-2mv, octree 512** | 712 K | **19,007** | no | 397 s | **best geometry**, free and local |
| Hunyuan3D-2mv, octree 256 | 228 K | 56,891 | no | 65 s | fast preview |
| TRELLIS.2 @1536 | 7.17 M | 166,008 | no | 164 s | only one with thin *open* membranes |
| Tripo v2.5 | 144 K | 24,304 | **yes, UV+material** | ~5 min | 15 exports/month; curls the wings |
| Pixal3D multi-view | 13.6 M | 1,485,327 | no | 410 s | broken, see above |

Your read is right: **Hunyuan is the best exportable quality**, and octree
512 widens that lead rather than narrowing it. Tripo v2.5 is the only one
handing back UVs and a material, which is worth one of its fifteen monthly
exports if a textured starting point saves more work than its curled wings
cost.

### Tripo v3.1: it will not give you the file

Tripo generated the best-looking single result of the cloud services -- 1.96 M
faces, textured, v3.1 "Best Quality", from one image for 55 of the 200 free
monthly credits. **Then it refuses to export it.** The Export dialog offers
GLB and 4K textures, and pressing Export produces no download and the banner
"Upgrade to unlock 3D model exports". So the free tier spends real credits on
a mesh you can only look at, which is the same trap this doc already recorded
for Meshy. Combined with the API pool being separate and at zero, Tripo is
unusable at $0.

Its rigger did produce the most interesting result of the day, though. The
Studio calls `POST /v2/studio/operation/pre_rig_check` before spending
anything, and for our dragon it answers:

```json
{"code":0,"message":"OK","data":{"riggable":true,"rig_type":"others"}}
```

**`rig_type: "others"`.** Tripo advertises biped, quadruped, hexapod, octopod,
avian, serpentine and aquatic; a winged quadruped matches none of them and
falls through to the catch-all. It says `riggable: true`, but the Studio then
declines to submit the 20-credit Auto Rig job at all. So Tripo neither refuses
the dragon outright the way Hunyuan does (`仅支持人形标准化`) nor claims one of
its named skeletons for it -- which is as close to a direct answer as the
survey's "expect the wings to be skinned to the spine" is going to get without
paying.

## Frameworks: is ComfyUI the right harness?

**Yes, and it is genuinely agent-drivable.** Everything in this doc was run
headlessly over its HTTP API: `GET /object_info` returns every node's full
input schema as JSON, `POST /prompt` takes the graph, `GET /history/<id>`
polls to a terminal state, and outputs land on disk where a script can pick
them up. No browser is involved. That is a better automation surface than any
of the hosted services, three of which needed a real Chrome driven through
their React UI.

The caveats are real but bounded, and all four are recorded above: templates
ship only in the editor's format, widget values are positional, `output_node`
decides what actually executes, and the worst failures are silent rather than
loud -- the inverted mask produced a confident, well-formed, completely wrong
mesh.

**trellis.cpp is not the speed play.** It is a C++/GGML implementation of the
same TRELLIS.2-4B (MIT, actively developed) with CUDA, Vulkan, ROCm and Metal
backends and a resident HTTP server, needing no torch and no CUDA extensions.
But its own README reports res-1024 at **3:16 to 7:23 on an RTX 5060 Ti** --
the same card that does 1536 in 34 s under ComfyUI's native INT8 path, so it
is roughly 6-13x slower on hardware we already have working.

Where it would earn its place is portability, not throughput:

- **The Mac.** Metal is supported and the README cites an M5 doing res-512 at
  5.6 GB peak RSS. That would make dragon iteration independent of whether a
  lab box is powered on -- which is exactly the problem that moved this work
  from x99 to t5810 mid-session.
- **A box where the torch stack fights back.** It sidesteps cu130, sm_75 and
  the whole wheel-matching problem that cost this session two rebuilds.

It is worth building for the Mac path if untethering from the lab matters.
It is not worth it to make x99 faster.

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
- **ComfyUI-SkinTokens** (`Aero-Ex/ComfyUI-SkinTokens`, active 2026-08-29).
  A ComfyUI wrapper around SkinTokens that exposes `--use_skeleton` as
  "use existing skeleton (generate skin only)" — precisely what this engine
  needs, since it supplies its own named skeleton and wants only weights.
  **This is the leading candidate**, because it runs inside the harness
  already driven headlessly by `tools/comfy_workflow.py` on x99, so it needs
  no new tooling from us. Its README does not state VRAM, weight sources, or
  whether joint names survive; SkinTokens' own `bpy.py` says they do. Untested.
- **skin-tokens.cpp** (`localai-org/skin-tokens.cpp` — note the org; this doc
  previously said `localai/`, which 404s. 183 stars, pushed 2026-09-01,
  Apache-2.0). C++/GGML port, CPU or Vulkan, C API. `skintokens-cli skin
  model.gguf mesh.glb skeleton.glb out.glb` is exactly the
  weights-for-a-given-skeleton call. **No torch and no CUDA**, so it runs on
  the Mac or t5810 — the fallback that makes rigging independent of whether a
  lab box is powered on.
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

## What generation is worth using for in this engine

Generated meshes are not a general asset source here. Measured against what
the engine actually draws:

**Vegetation: no.** The trees are procedural and tiny — `make_tree_mesh` in
`src/gfx/foliage.cpp` builds them out of cones and tubes at **spruce 38 verts
/ 35 tris, pine 30/28, broadleaf 130/206, dead 52/52, grass tuft 24 verts /
8 tris** — drawn as thousands of GPU instances with vertex colours and no
textures at all. A generated tree arrives as 1.5 M triangles plus a 4096²
PBR set. Decimating it to ~100 triangles leaves nothing of the generation
(you are back to a cone), and its texture would add a material path the
foliage pipeline deliberately does not have. Grass is re-placed around the
camera every frame, so the budget there is instance count, not mesh detail.

**More dragons: yes, and the engine now fields them.** Until 2026-09-11 every
dragon in a match was the same mesh and texture with a hue push, which cannot
change a silhouette. `--models A,B,C` loads a roster: the player flies the
first and the bots are dealt the rest, each with its own skeleton, scale,
`.rig.cfg` pose profile, `.flight.cfg` handling and flame colour. So a distinct
bot dragon is now a visual gain that actually reaches the screen. The pipeline
is proven end to end and costs about four Studio credits plus a rig.

The rule that falls out: generation pays for things that are **few on screen,
large, and want a unique silhouette and texture** — hero creatures and set
pieces. It does not pay for anything instanced in the thousands, which is
exactly what the procedural systems in this engine already do well.

**Licence: read on 2026-10-01** (`ATTRIBUTION.md` has the clauses). The
outputs belong to the user and may ship, provided they are labelled as
AI-generated. Two clauses bear on how this pipeline is driven, not on the
assets: the `3d.hunyuan.tencent.com` service is offered to mainland-China
users only, and it forbids automated extraction, which is what
`tools/hunyuan_oneshot.py` does. **Generate new creatures by hand on the
international site, `3d.hunyuanglobal.com`**, whose English terms also assign
the outputs and also require the label, or through Tencent Cloud's paid API.

## The pipeline

This replaces the original plan, which is preserved only in git history. Nearly
every step of it was superseded by measurement; the sections above say why, and
each step below links to the one that justifies it.

**Nine creatures have been through this end to end**, so the numbers are not
from one run: Embercrest, Stormsail, the three hoard-run designs of
`concepts-hoard-run.png` (Ashcoil, Cragjaw, Mossback) and the four elemental
variants (Rimefang, Blightmaw, Tidewrack, Ironroot). The one-shot's 20/day pool
has never been the limit; several were generated in each sitting.

**Shared anatomy does not mean shared measurements.** The first elemental
batch reused `winged-quadruped.json` and `winged-biped.json`, but subsequent
leg, jaw and membrane repairs showed why that shortcut is unreliable.
Matching limb and finger counts preserves the bone-naming contract; it does
not preserve joint locations, jaw boundaries or membrane gates. Measure each
generated mesh and keep its own skeleton JSON. Frostvein (2026-09-14, by
gpt-6) is the first species built that way from the start: a new ice-dragon
sculpt with its own measured 74-bone skeleton (`tools/skeletons/frostvein.json`,
`artifacts/frostvein/README.md`), rather than another Rimefang repair on the
shared file -- and it stood, flew and closed its mouth correctly on the
first engine pass where every shared-skeleton species needed several.

**Draw the mouth slightly parted in every plate.** Every sculpt before the
elemental batch came back with the mouth fused shut and no interior, which
leaves heat weighting no gap to split on: Ashcoil's mandible measured 0.62 jaw
/ 0.37 head and a jaw rotation bent the whole muzzle instead of opening it.
Asking for a parted mouth showing the teeth, in every panel of the turnaround,
produced a real mouth cavity on all four.

1. **Reference sheet, then a turnaround.** Every design has a sheet
   (`artifacts/dragon-options/*-reference-sheet.png`). A sheet is drawn for a
   human and a generator wants one pose per view, so generate a four-panel
   turnaround from the sheet and crop it. **Reference the sheet, never a
   derivative** — see "Image-to-image drift compounds". Resolution is not
   worth chasing: "Does input resolution matter" measures the encoders at a
   fixed 518 or 224 square. `artifacts/dragon-options/README.md` says which
   plates to use and why.

   The concept-art skill generates all three stages; use `--backend web`
   (GPT Image 2) throughout so the identity carries, and pass the previous
   stage with `-i <file> --ref-role subject`.

   **Cut the turnaround with `tools/split_turnaround.py`, not by hand or by
   width/4.** The panels are not equal quarters — a long-bodied animal's
   profile is three times the width of its front view — and on Ashcoil they do
   not even separate on empty columns, because one view's tail tip reaches
   past the next view's wingtip. The tool labels connected components instead,
   crops each animal to its own bounding box, repaints any neighbour that
   intrudes into that box with the background, and asserts it found exactly
   four. Without the repaint a plate carries a floating tail tip, which is
   another subject for Hunyuan's segmentation to find.

2. **One cloud generation. Nothing else.** Use the Hunyuan **one-shot**, not
   the staged chain: it emits 1.5 M triangles *with* UVs and a PBR set in a
   single run, and the staged chain cannot avoid retopo because 语义UV
   refuses meshes over 30 K faces. `tools/hunyuan_oneshot.md` automates it
   through chrome-use, including the three traps that cost a run each.
   20/day pool, separate from the Studio's 30.

   Skip: **低模生成** destroys wing fingers and ridge spikes; **Tripo** will
   not export below its paid tier and its rigger classifies a winged creature
   as `others`; **local models** lose to the cloud because it runs v3.1 and
   the newest open weights are 2.1.

3. **Decimate and rig locally, in one command.**

   ```sh
   blender --background --factory-startup --python tools/rig_embercrest_candidate.py -- \
       --input assets/<candidate>.glb --stem <name> \
       --skeleton tools/skeletons/<anatomy>.json --keep-uvs --target 80000
   ```

   That decimates 1.5 M to the target, fits the skeleton to the mesh bounds,
   heat-binds, applies the continuous wing-membrane field, repairs tangents
   and exports. It replaces the old steps 4, 5 and 6 — no Quad Remesher, no
   `build_embercrest.py` skeleton, and **no ML skinner**: SkinTokens was
   tested and discards the joint naming the engine depends on.

   Budget: 80 K is fine. "Triangle count is not the bottleneck" measured an
   80 K mesh rendering *faster* than a 38 K one.

   **Then repair the data maps.** Hunyuan's base colour is good; its ORM and
   normal are not: occlusion is a constant 1.0, roughness is one value for
   horn, hide and membrane alike (Stormsail std 0.019), and the normal map is
   nearly flat (R/G std 0.03). Under the engine's single GGX lobe that reads
   as plastic. `tools/repair_model_materials.py` bakes AO from the mesh,
   rebuilds roughness from zones the mesh can locate (thin-and-cylindrical
   is keratin, thin-and-flat is membrane, the rest is hide) and derives a
   subtle detail normal from the base colour's luminance; it writes a new
   `.glb` with only the two image payloads changed and verifies the rest is
   byte-identical. The base colour has some occlusion painted into it
   already (luminance vs baked AO, r ≈ +0.35 on both assets), so the AO is
   not turned up further than the bake gives. Needs a venv with numpy,
   Pillow, scipy and trimesh+embree; about a minute per model. No such venv
   survives on the mac, so make one (`uv venv`, then
   `uv pip install numpy pillow scipy trimesh embreex`).

   **Check `zone coverage` in the log against what the animal is made of.**
   The zones are geometric, not semantic, so they mislabel a body the
   heuristics were not written for: on Mossback the "thin and cylindrical =
   keratin" rule claimed **15.4%** of the mesh, which is not horn but the
   shaggy fur spikes, and it was handing all of it the semi-gloss 0.32 meant
   for horn and claw — glossy plastic quills on a grazing animal. Passing
   `--rough-keratin 0.55` makes the fur matte and costs a little gloss on the
   real horns and hooves, which is the right trade here. Cragjaw, by contrast,
   came out at 0.1% keratin and needed no override.

   The repaired file takes the plain name and the untouched Hunyuan output is
   kept beside it as `<name>-raw.glb`, with copies of its `.rig.cfg` and
   `.flight.cfg` so it can still be loaded for comparison. These `.glb` files
   are gitignored and the generation run is slow and cloud-bound, so nothing
   in this pipeline ever overwrites one.

4. **Accept it in the engine.**

   ```sh
   ./build/dragon --headless --frames 40 --model assets/<name>.glb \
       --bind-pose --skeleton --inspect 90 --screenshot /tmp/x.bmp
   sips -s format png /tmp/x.bmp --out /tmp/x.png
   ```

   The load line is the acceptance test — it prints `mapped rig: neck N,
   tail N, wing root N/N, fingers N/N, legs N/N, front legs N/N, feet N, jaw`.
   Every chain must map and there must be no joint warnings. Then look at
   `--studio 1` (flap), `--studio 4` (s-turns) and `--studio 8 --inspect-head`
   (jaw). Judging a rig by its build stats does not work; the stats were
   identical in a case where the mesh was visibly wrong.

## Adding a creature that is not a dragon

The seam is `tools/skeletons/*.json`, not the script. Five exist:

| File | Anatomy | Bones |
|---|---|---|
| `winged-quadruped.json` | Embercrest: four legs plus two wings | 62 |
| `winged-biped.json` | Stormsail: two hind legs, wing forelimbs, rudder tail | 55 |
| `winged-serpent.json` | Ashcoil: **no legs at all**, two wings behind the skull, the trunk is a 14-segment tail chain, four finger ribs | 44 |
| `armoured-quadruped.json` | Cragjaw: **no wings**, four splayed legs, eight-segment tail | 48 |
| `grazing-quadruped.json` | Mossback: **no wings**, four column legs, two toes per foot (cloven), five-segment stub tail | 37 |
| `heavy-quadruped.json` | Ironroot: the same six-limbed plan, but squat -- short legs under a deep body, measured independently | 62 |
| `finned-biped.json` | Tidewrack: the wyvern plan with its own membrane-field gates | 55 |

**A shared skeleton shares its author's measurements, and those do not
transfer.** This is the lesson the elemental batch paid for. A skeleton JSON
holds two different kinds of thing: per-*anatomy* topology (bone names,
parents, chain lengths) which genuinely transfers, and per-*creature*
measurements which do not -- leg bone positions, the `wing_field` gates, the
`jaw_mask` coordinates, `reference_bounds` itself. Reuse the file and you
inherit the measurements silently. Three faults out of one cause:

- **Ironroot's leg bones were not in its legs.** Its shin head probed 0.046
  *outside* the mesh against Embercrest's 0.014 inside, and the whole chain
  bunched at the belly line, because the affine fit preserves proportion but
  knows nothing about where the anatomy is and Ironroot's legs are far shorter
  relative to its body. `heavy-quadruped.json` measures its own.
- **Three creatures were rigged with Embercrest's jaw.** `winged-quadruped.json`
  does not set `jaw_mask`, so it defaults to the gap-following mask authored in
  *Embercrest* canonical space; on Blightmaw's skull that band welded the
  mandible to the head and a jaw rotation stretched the muzzle flat. The mask
  belongs to the head, not the anatomy, so the rigger now takes
  `--jaw-mask inherit|none|embercrest`; `none` is right for any sculpt modelled
  with its mouth already open.
- **Tidewrack's membrane was bound with Stormsail's gates.** Its torso edge is
  at |X| 0.045-0.055 where `span_x` assumed 0.1, so the field started outboard
  of the real flank and left the inner sheet to chest and neck bleed -- 78%
  wing-chain weight at the root, fixed to 92% by measuring its own.

The rule that falls out: **before reusing a skeleton, probe the candidate's
bone-to-surface distances against the creature the file was authored on.** A
leg bone outside the mesh is a number, not an opinion, and it takes one
Blender run to get.

Two hypotheses that measured clean, recorded so nobody re-runs them: the
fitted wing-fold axis is well conditioned on all six species
(lambda_mid/lambda_min 21-26) and agrees between the two wyverns to half a
degree; and coincident finger-rib heads do *not* defeat
`ground_stow_converge_deg`, because its share is `finger_index/(finger_count-1)`
-- an index, not a geometric measure.

Cragjaw and Mossback are both wingless quadrupeds and still get separate
files. The fit is a per-axis affine remap of the whole skeleton, so it absorbs
proportion but not a change in *structure*: a tail authored to reach the
drake's tail tip lands well past the grazer's rump, out in empty space. Share
a skeleton only when the chains have the same lengths as well as the same
names.

Each holds bone head/tail in a normalised space plus the `reference_bounds`
they were authored against; the rigger remaps them onto the actual mesh, which
is what lets one skeleton fit differently-proportioned meshes of the same
anatomy. **Measure the mesh to place bones** — probing Stormsail found its
wings carry *four* finger ribs, not the three a copied dragon skeleton would
have given it.

**Bone names are a contract.** `src/anim/dragon_rig.cpp` finds joints by name
substring, and the rigger keys its anatomy passes off the same conventions
(`wing*`, `tail*`, `toe*`, `_l`/`_r`). Follow them and the passes apply
themselves; a numbered skeleton maps to nothing.

**The engine already handles more than one body plan.** `dragon_rig.cpp:1562`
derives `quadruped` from whether the front-leg chains are empty, and
`drive_limb` tolerates empty chains, so the wyvern maps with `front legs 0/0`
and no engine change at all. Do not assume a new creature needs engine work
until an acceptance run says so.

**The engine handles more body plans than it accepts.** Ashcoil proves the
mapper needs no change for a legless animal: it reports `neck 3, tail 14, wing
root 3/3, fingers 4/4, legs 0/0, front legs 0/0, feet 0, jaw jaw` and flies,
with the lateral trunk wave visible from directly above. But the two *wingless*
creatures map correctly and are then thrown away, because
`DragonJoints::valid()` requires wing roots on both sides -- see the Open
questions in `STATUS.md`. Check the acceptance log for `falling back` and not
just for the `mapped rig:` line; a rejected asset still prints a perfect map
immediately before the engine substitutes the generated dragon.

Everything below was Embercrest-shaped, and each one is opt-out in the JSON
because a later anatomy hit it. The first two were the wyvern's; the rest came
from the wingless pair and the serpent:

- `wing_field` — the membrane weight pass. Omit it, or use a skeleton with no
  `wing*` bones, and the pass skips.
- `jaw_mask` — the gap-following jaw mask is authored in *Embercrest*
  canonical coordinates. On another head that region is the whole skull, which
  put the mandible entirely on `head`. Set `"jaw_mask": null` to keep heat
  weights, or give it a measured plane (below).
- `wing_bind_level_deg` — the correction that takes the sculpt's raised
  shoulder out of the bind pose. It was an unconditional
  `rig.pose.bones['wing_root_l']`, so it was a hard `KeyError` on any wingless
  skeleton, which is what stopped Cragjaw first. Default 35, and the pass
  skips when the `wing_root_`/`wing_wrist_` bones are absent.
- `reference_span` — `{"axis": "y", "metres": 12}`. The `ground_offset` was
  `(height/2) * 19 / x_extent`, i.e. it assumed the X extent is a 19 m
  wingspan. True of every winged asset and nonsense on a wingless one, where X
  is just body width. Default `{"axis": "x", "metres": 19}`.
- `preview` — the Cycles camera for the `artifacts/<stem>/*.png` pose renders.
  Embercrest's `ortho_scale` of 1.45 crops a serpent's trunk out of frame
  entirely, which makes the acceptance record misleading rather than merely
  ugly.

**`jaw_mask` can also be a measured plane**, which is what a generated head
usually needs: `{"type": "plane", "x_max": …, "y_min": …, "line": [[y,z],
[y,z]], "hinge_blend": [lo,hi]}`. Hunyuan sculpts the mouth *sealed*, so heat
diffusion has no gap to split on and the weights smear across the lip line —
measured on Ashcoil, the mandible band came out 0.62 jaw / 0.37 head, and a
jaw rotation bent the whole muzzle downward instead of opening it. With the
plane taken off the lip crease in the side view, the mandible drops and the
brow, eye and snout tip stay put. Two things to know: the gape reveals a
stretched crease rather than a mouth cavity, because the sculpt has no mouth
bag (Embercrest's hinge blend accepts the same artefact); and the line is
measured on the *unlevelled* mesh in canonical space, which is correct, since
the mask runs before the wing bind correction.

**The plane is per head, and two more heads needed it.** Rimefang and Ironroot
were rebuilt with `null` masks after the Blightmaw fault, on the theory that a
sculpt with a parted mouth gives heat something to split on. It does not give
it enough: on both, heat handed the whole *snout* to the `jaw` bone (jaw
weight 0.9-1.0 over the nose, 3800 and 4500 total against 1300 and 1000 on
`head`), so `jaw_rest_deg -24` lifted the nose like a lid and the breath
clamped the snout down onto a fixed mandible. `tools/skeletons/rimefang.json`
and `ironroot.json` are the shared files with a measured plane added; the bone
lists are byte-identical, so Embercrest and Blightmaw (still on
`winged-quadruped.json`) are untouched. How the line was found: cast vertical
rays through the head in canonical space and read the crossings -- chin
(normal down), mandible top or lower tooth (up), palate or upper tooth (down),
snout top (up) -- so each column gives a window the line must pass through,
and the widest-over-x mandible top and lowest palate per y bound it.
Rimefang's windows run from [.3508, .3538] at y .32 to [.321, .341] at .37,
shallow behind the teeth and steep at the chin, and **no straight line clears
both ends**, so `line` now accepts more than two points (piecewise linear,
end segments extrapolated). Ironroot's cheek is solid to y .345 and its
windows from [.3214, .3273] at .35 to [.2842, .3088] at .40 take one segment of
slope -0.63. Two more keys came out of the same measurement: `z_min`, because
Rimefang carries its head over its chest and a box that bounds only x and y
had 697 chest, upper-arm and forefoot vertices inside it (its floor is .29,
between the neck base and the .303 chin); and
`reclaim_above: true`, because the plane only ever *added* jaw below the line
and would have left the snout on the jaw bone -- it moves jaw/jaw_tip weight
above the line onto `head`, opt-in so Ashcoil's build reproduces. The windows,
the choices and the renders are in `artifacts/<name>/jaw-mask/README.md`.
The rebuild is the documented command with the per-species skeleton, and the
shipped file is that output run through `repair_model_materials.py` with its
defaults -- confirmed by re-running the repair on the previous raw rig and
getting the shipped `.glb` byte for byte.

If a further anatomy hits another hardcoded constant, the fix is the same:
lift it into the JSON with a default that preserves existing builds, then
re-run the Embercrest build and check `max_weight_sum_error` and
`ground_offset` are unchanged to the digit. That is the regression test, and
it passed across all five changes above — 5.122274160385132e-08 and
3.9933978544450914, with the `binding` string byte-identical. Run it with a
throwaway `--stem`: the real stem would overwrite the shipping
`assets/embercrest.glb` with an unrepaired rebuild.

## Open questions

Answered ones have been removed; the sections above hold the results. What is
genuinely still unknown:

- Whether the international Hunyuan site (`3d.hunyuanglobal.com`) offers
  the same one-shot and the same free pool, now that it is the route new
  generations should take (`ATTRIBUTION.md`). The hosted terms themselves were
  read on 2026-10-01.
- Whether Tripo's Rig v2.5 really puts a finger chain on a winged creature.
  It is the last commercial candidate and needs paid API credits, since
  Studio credits do not reach the API. Its free pre-check answers
  `rig_type: "others"` for our dragon, which is not encouraging.
- Whether decimating locally to under 30 K and **uploading that** to the
  Studio for 语义UV plus 纹理绘制 works. It is the only untested route that
  would keep multi-view geometry *and* get cloud PBR without 低模生成.
- Whether ComfyUI-SkinTokens' `use_transfer` path is fixable. It crashes at
  export looking the input armature up by name; that is the branch that would
  preserve UVs, textures and joint naming, and the environment work to retest
  it is already done and recorded.
- Rigel3D and AniGen (2026 papers) generate already-rigged, semantically named
  creatures from one image. Neither has code. If one ships, step 3 of the
  pipeline collapses into step 2.
- Stormsail's shoulder/wing-root membrane junction and its flight leg fold are
  heat-weight quality rather than hand-tuned. Fine for a candidate; someone
  should look at them in `--studio 1` and `--studio 5` before it ships.
- Ashcoil's own unverified corners: the inner trailing edge of the membrane
  where it meets the flank near the ground (finger 3's tip sits at z 0.013)
  was only judged at ~1280 px in engine, not in a close-up. Mossback has
  belly fur strands that stretch into thin streaks where they span a hip or
  shoulder and the leg at ±35°, and its shoulder mass sits outboard of the leg
  column, so a large forward reach would shear the fur along the hump line;
  neither was tested past 35 degrees. Cosmetic at prey distance.
