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
| HUD fonts (`assets/fonts/`): Barlow Condensed, Fira Sans | yes, OFL 1.1 | yes | yes | Licence text beside each, and in `THIRD_PARTY_NOTICES.md` |
| Hunyuan-generated roster (`assets/<species>.glb`, about 50 MB each) | **no**, gitignored for size | **yes, labelled AI-generated** | yes | Outputs are ours (clause 5.3); the label is required (3.3); see below |
| `assets/props/grazer.glb` (Hunyuan-derived, 3.5 MB) | yes, labelled | yes | yes | As above, and small enough to track |
| AI concept art and renders in `artifacts/`, `docs/concept/` | yes, labelled | no (not game content) | -- | Outputs are ours under the OpenAI, Google and Tencent terms |
| `dragon.glb` and `alt/prowler.glb`, the two Sketchfab dragons | no | no | no | Early attempts, superseded by the roster; anyone can download them (below) |
| Renders of those two in `artifacts/` | yes, credited | -- | -- | Screenshots are adapted material: CC BY-NC for the Black Dragon, CC BY for the Prowler |

**Scope of `LICENSE`.** The MIT licence covers the source code, shaders,
tools, tests, docs and the original assets. It does not cover third-party
material, which keeps its own licence: the libraries in
`THIRD_PARTY_NOTICES.md`, and the Sketchfab models and every render of them
listed here. Much of the repository was written or generated with AI tools
(`AI_DISCLOSURE.md`); where a part of it is not protected by copyright, it is
offered on the same terms, with no claim made over it.

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
- **3.6(4)**: do not use the outputs to train a competing model.

The open-weight **Hunyuan3D-2 / 2.1 community licence** is a different
document. It excludes the EU, the UK and South Korea and has a 1M-MAU
threshold. It governs only the locally generated comparison meshes
(`*-cand-hunyuan3dmv-*`), none of which ships.

The meshes stay **gitignored** (`assets/*.glb`, `assets/embercrest-textures/`)
because of their size (about 50 MB each with 4096² PBR sets), not because of
their licence. A release carries them, downsampled (`docs/RELEASE.md`).

## The two Sketchfab dragons (early attempts, not shipped)

The engine was first built around two downloaded models. The generated roster
replaced them, and neither ships or is hosted here. Both still load with
`--model` for anyone who downloads them. Use the **glTF** download and the
`.glb`; Blender's FBX importer mangles the first one's rig (`docs/ANIMATION.md`).

- `assets/dragon.glb`: **"Black Dragon with Idle Animation"** by **3DHaupt
  (dennish2010)**, https://sketchfab.com/3d-models/fb0053a2e59b43868e934c239bf4eb36,
  **CC BY-NC 4.0**. It was the default model until 2026-10-01. It is tagged
  `noai` on Sketchfab, whose terms (sections 5 and 15(a)) forbid using NoAI
  content as input to generative AI, so it is no longer rendered for the AI
  tools that build this project.
- `assets/alt/prowler.glb`: **"Prowler Dragon Variant Rig"** by **DM-913
  (SuperKapoo913)**, https://sketchfab.com/3d-models/7ee71aaf323d426bbbdf28d73d55bbd9,
  **CC BY 4.0**. A 126-bone wyvern whose wings are the forelimbs.

Renders of both remain in `artifacts/`, where they record the work they
were used for. They are credited here: the Black Dragon's renders are
non-commercial adapted material under CC BY-NC 4.0, and the Prowler's are
under CC BY 4.0. Nothing derived from either mesh is in the repository; the
public mirror's history is rewritten without the old re-exports
(`tools/release/public_excludes.txt`).

## Gold ribbon-wing test (2026-10-02, not integrated)

The later `assets/gold-direct-cand-oneshot.glb` is another unrigged Hunyuan
test from the user-approved direct-reference turnaround, with provenance in
`artifacts/dragon-options/gold-direct-test/`. It is excluded from Git and is
not part of the game or release roster. This close reference-study model
remains pending user review; no third-party reuse licence is established.

`assets/gold-ribbon-cand-oneshot.glb` and
`assets/gold-ribbon-top-cand-oneshot.glb` are unrigged Tencent Hunyuan hosted
one-shot outputs, generated from the AI-made gold reference and modelling plates
in `artifacts/dragon-options/gold-ribbon-test/`. The concept uses D&D 2024 gold
dragon anatomy as a reference, credited to Wizards of the Coast and designer
Alexander Ostrowski (`https://www.alexanderostrowski.com/2024metallicdragons`).
The local GLB is excluded from Git under the existing generated-model size
policy. No game or release roster includes it; model approval is pending.

## Embercrest-scripted (experiment, not tracked)

An original model built for this project by `tools/build_embercrest.py`. The
script generates the geometry, UVs, procedural PBR textures, skin weights
and 68-bone skeleton, and reuses no mesh, rig or texture data from either
third-party dragon. It is rebuilt with Blender as described in
`docs/EMBERCREST.md`. It is kept as a reference attempt and is not a shipped
model.
