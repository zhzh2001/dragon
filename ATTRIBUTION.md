# Models, licences and what ships where

Every model the game can load, its licence, and whether it may go into the
public repository, a public release, or only a personal copy. The libraries
are in `THIRD_PARTY_NOTICES.md`. How each AI tool was used is in
`AI_DISCLOSURE.md`.

The terms below were read on 2026-10-01, from the primary pages. This is a
hobby project's reading, not legal advice.

## What ships where

| Material | Public repo | Public release (free binary) | Personal copy | Why |
|---|---|---|---|---|
| Code, shaders, tools, tests, docs | yes, MIT | yes | yes | Ours (`LICENSE`) |
| Original props (`assets/props/` except `grazer.glb`), terrain and leaf textures | yes, MIT | yes | yes | Procedural, made by our scripts |
| Hunyuan-generated roster (`assets/<species>.glb`, about 50 MB each) | **no**, gitignored for size | **yes, labelled AI-generated** | yes | Outputs are ours (clause 5.3); the label is required (3.3); see below |
| `assets/props/grazer.glb` (Hunyuan-derived, 3.5 MB) | yes, labelled | yes | yes | As above, and small enough to track |
| AI concept art and renders in `artifacts/`, `docs/concept/` | yes, labelled | no (not game content) | -- | Outputs are ours under the OpenAI, Google and Tencent terms |
| `dragon.glb`, "Black Dragon" by 3DHaupt (dennish2010), CC BY-NC 4.0, tagged NoAI | **no** | **no** | yes, with credit, never sold | Non-commercial, and its Sketchfab tag forbids AI use (below) |
| Renders of `dragon.glb` in `artifacts/` | yes, as CC BY-NC material, credited | -- | -- | Screenshots are adapted material: non-commercial, credited |
| `alt/prowler.glb`, "Prowler Dragon Variant Rig" by DM-913 (SuperKapoo913), CC BY 4.0 | no (not ours to host) | optional, with credit | yes | Permissive, credit required |

Everything not ours is kept out of the MIT grant: the `LICENSE` scope
paragraph says so, and this table is the list.

## Hunyuan 3D: the generated roster

`assets/embercrest.glb`, `stormsail`, `ashcoil`, `cragjaw`, `mossback`,
`frostvein`, `rimefang`, `blightmaw`, `ironroot`, `tidewrack`, `sunspear`,
`rimeplume`.

Generated with **Tencent Hunyuan 3D** (`3d.hunyuan.tencent.com`) on its free
tier, from original concept art made for this project. They were then
decimated and rigged locally by `tools/rig_embercrest_candidate.py` and
finished by `tools/repair_model_materials.py`. The procedure is in
`docs/MODEL_GENERATION.md`; the one-shot generation step is in
`tools/hunyuan_oneshot.md`. The prompts, input plates, job IDs and source
hashes are in `artifacts/dragon-options/`, `artifacts/frostvein/` and
`artifacts/dragon-options/elemental-expansion/`.

**Terms, read 2026-10-01.** The governing document is the 《腾讯混元3D用户服务协议》
(Tencent Hunyuan 3D user service agreement), no. TEG-101170-002-05,
effective 2025-05-26, at `rule.tencent.com/rule/202501080004`, operated by
Shenzhen Tencent Computer Systems under Chinese law.

- **5.3, ownership.** The rights in what you upload and what the service
  generates belong to you, or to whoever lawfully holds them. 5.5: you use
  the output at your own judgement and risk. There is no commercial ban and
  no licence back to Tencent.
- **3.3, labelling.** Output that is published or distributed must be
  marked prominently as AI-generated. 3.6(1) and 4.6: Tencent's visible and
  metadata marks must not be removed. `AI_DISCLOSURE.md`, the README and
  every release carry the mark. Do not add a step that strips metadata
  from these files.
- **1.1, eligibility.** The service is currently offered to users in
  mainland China only. The international site, `3d.hunyuanglobal.com`, is a
  separate service under English terms (OriGen Tech, Singapore). Those
  terms also assign outputs to the user (6.3) and also require an AI label
  (6.6). **New generations should go through the international site.**
  This clause concerns the account, not who owns what was already
  generated.
- **3.6(3), automated extraction.** Extracting by automated or programmatic
  means is prohibited. `tools/hunyuan_oneshot.py` drives the site through a
  browser, so that tool is a risk to the account. The terms do not
  restrict outputs on this ground. Prefer driving generations by hand, or
  through Tencent Cloud's paid API, from here on.
- **3.6(4)**: do not use the outputs to train a competing model.

The open-weight **Hunyuan3D-2 / 2.1 community licence** is a different
document. It excludes the EU, the UK and South Korea and has a 1M-MAU
threshold. It governs only the locally generated comparison meshes
(`*-cand-hunyuan3dmv-*`), none of which ships.

The meshes stay **gitignored** (`assets/*.glb`, `assets/embercrest-textures/`)
because of their size (about 50 MB each with 4096² PBR sets), not because of
their licence. A release carries them, downsampled (`docs/RELEASE.md`).

## dragon.glb -- the original default model

**"Black Dragon with Idle Animation"** by **3DHaupt (dennish2010)**, via
Sketchfab.

- Licence: **CC Attribution-NonCommercial 4.0**
  (https://creativecommons.org/licenses/by-nc/4.0/)
- Source: https://sketchfab.com/3d-models/fb0053a2e59b43868e934c239bf4eb36
- Credit, wherever it appears: *"Black Dragon with Idle Animation" by
  3DHaupt (dennish2010), CC BY-NC 4.0, re-rigged and driven procedurally.*

**The model is tagged `noai` on Sketchfab.** Sketchfab's Terms of Use
(effective 2026-08-12, sections 5 and 15(a)) forbid using NoAI content
"as inputs to Generative AI Programs". This project's verification loop
renders the default model and shows the frames to AI coding agents, which is
exactly that use. From 2026-10-01:

- the model is not to be rendered for, or handed to, an AI tool;
- the public release does not bundle it, and the default player model is
  being moved to one of ours (`docs/RELEASE.md`).

Nothing derived from its mesh remains in the repository. The FBX re-export
that once sat in `artifacts/` is removed, and the public export's history is
rewritten without it. The existing renders in `artifacts/` are
non-commercial adapted material, credited here.

### Getting it (personal copies)

Not committed: 65 MB with textures embedded. From the Sketchfab page choose
**Download -> glTF** and use the **.glb**, renamed to `assets/dragon.glb`.
Use the glTF download, **not** the original FBX: Blender's FBX importer mangles
this rig (see `docs/ANIMATION.md`). A personal copy that includes it must
credit it as above and must never be sold, put behind a donation, or used to
earn ad revenue.

## alt/prowler.glb (optional second dragon)

**"Prowler Dragon Variant Rig"** by **DM-913 (SuperKapoo913)**, via Sketchfab.

- Licence: **CC Attribution 4.0** (https://creativecommons.org/licenses/by/4.0/)
- Source: https://sketchfab.com/3d-models/7ee71aaf323d426bbbdf28d73d55bbd9
- Credit: *"Prowler Dragon Variant Rig" by DM-913 (SuperKapoo913), CC BY
  4.0; re-exported as a single .glb and retargeted onto the procedural
  rig.*

A 126-bone wyvern (the wings are the forelimbs) with base colour, normal and
metallic-roughness maps and two clips (Landing, Walk). Load it with
`--model assets/alt/prowler.glb`. Download -> glTF from Sketchfab; use the
.glb directly, or import the glTF into Blender and export the armature
plus its two skinned meshes as one .glb with tangents.

## Embercrest-scripted (experiment, not tracked)

An original model built for this project by `tools/build_embercrest.py`. The
script generates the geometry, UVs, procedural PBR textures, skin weights
and 68-bone skeleton, and reuses no mesh, rig or texture data from either
third-party dragon. It is rebuilt with Blender as described in
`docs/EMBERCREST.md`. It is kept as a reference attempt and is not a shipped
model.
