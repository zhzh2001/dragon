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
- The optional test in `tests/test_anim.cpp` loads `assets/embercrest.glb`
  when present and checks every chain maps and responds. Point it at a new
  asset and it becomes the acceptance test for the rig.

## The two machines

| | t5810 | x99 |
|---|---|---|
| GPU | RTX 2080 Ti, 11 GB, sm_75 (Turing: no bf16, no flash-attn 2) | RTX 5060 Ti, 16 GB, sm_120 (Blackwell: **cu130** — ComfyUI 0.34 disables its CUDA kernels below that) |
| OS, Python | Ubuntu 22.04, Python 3.10, CUDA 13.2 toolkit installed | CachyOS, Python 3.14 (too new; use `uv python install 3.12`), uv, docker, conda |
| RAM, CPU | 31 GB, 4 cores | 31 GB, 8 cores |
| Disk | 284 GB free | **20 GB free on root.** `/mnt/Data` (NTFS) has 69 GB free for weights |
| torch: none on t5810; **x99 has 2.14.0+cu130** in `~/venvs/comfy` | sudo needs a password | sudo is passwordless |

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
| **Hunyuan 3D Studio** (3.1/3.5) | text, image, up to 4 views; 8K PBR; Smart Topology quads | 绑骨蒙皮 stage, but **tested and humanoid-only**: a winged quadruped is refused with `仅支持人形标准化` | **$0**: **30** free generations/day on the web (measured), whole pipeline incl. rigging; API needs Tencent Cloud | hosted terms not fetched |
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
`assets/*.glb` rule since the hosted terms were never fetched.

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

1. **Reference sheet — done.** All three designs from
   `artifacts/dragon-options/concepts.png` have one:
   `embercrest-`, `stormsail-` and `ironroot-reference-sheet.png`, each with
   front, side and top plus head, wing-root and foot insets. Embercrest is
   the design being taken forward, because its name is already on the
   68-bone rig in step 5.

   A reference sheet is for a human, though, and a *generator* wants one
   pose per view. The sheets do not have that — their front and top views
   share a wings-spread pose but the side view is a walking pose with a
   curved tail, and a multi-view generator reads that as two animals. So
   `embercrest-turnaround.png` is the generator input: front, left, back and
   right in one pose, one scale, one eye level, flat grey, no watermark,
   which GPT Image 2 will produce if the prompt forbids perspective,
   foreshortening, cast shadows and labels *explicitly* and the existing
   sheet rides along as a subject reference. Split it into the four images an
   API wants with:

   ```sh
   T=artifacts/dragon-options/embercrest-turnaround.png   # 2158x729, 4 panels
   i=0; for n in front left back right; do
     magick "$T" -crop 539x729+$((i*539))+0 +repage -trim +repage \
       -bordercolor '#c9c9c9' -border 40 "views/tv-$n.png"; i=$((i+1))
   done
   ```
2. **Cloud pass first, for calibration.** Everything here needs an account —
   see the Spaces and cloud sections above; the one free no-signup path,
   the Blender MCP's shared Rodin key, is drained. With a Hugging Face
   token, `tools/trellis2_space.py` is the cheapest first candidate. Then
   five each from Rodin and Tripo multi-view, plus Hunyuan Studio's 20/day.
   Run Tripo's rig on its best one with `quadruped` and `avian` to see what
   a commercial rigger does to the wings. Under $10 all in, and it sets the
   bar the local models have to reach.
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

- Whether Tripo's Rig v2.5 really puts a finger chain on a winged quadruped.
  It is now the only commercial candidate left, and it needs paid API
  credits (Studio credits do not reach the API).
- Whether the horn spires are a `中` artefact that `高` avoids, or inherent
  to 低模拓扑 V1.5 on thin tapered shapes.
- Turing (t5810, 11 GB) is still unrun. x99 answered the 16 GB half: the
  native INT8 path peaks at 7.7 GB at 1536, so 11 GB looks reachable if a
  cu130 build exists for sm_75.
- SkinTokens' real VRAM floor (14 GB claimed, 4 GB in a wrapper) and whether
  `--use_skeleton` copes with a 68-bone skeleton; its training rigs are
  mostly under 64 bones.
- Whether any rigger puts a finger chain on a winged quadruped. Hunyuan is
  now excluded by test (`仅支持人形标准化`), leaving Tripo Rig v2.5 and
  SkinTokens.
- Rigel3D and AniGen (2026 papers) generate already-rigged, semantically
  named creatures from one image. Neither has code. Worth rechecking in a
  few months; if one ships, steps 4 to 6 collapse.
