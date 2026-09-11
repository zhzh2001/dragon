# Dragon design and model-generation artifacts

Every comparison image below has its subject burned into the image itself —
an amber title bar saying what is being compared, and a caption under each
panel. `nm` in a caption means non-manifold edges, the count that decides
whether auto-weighting will work later.

The findings behind these live in `docs/MODEL_GENERATION.md`.

## Upload these

Each creature has a `*-views/` directory holding the plates to feed a
generator, named for the slot each one goes in (see the runbook in
`docs/MODEL_GENERATION.md` and `tools/hunyuan_oneshot.md`):

| File | Slot |
|---|---|
| `1-front.png` | 正图 — front, the only required one |
| `2-back.png` | 背图 — back |
| `3-left.png` | 左图 — left |
| `4-right.png` | 右图 — right |
| `5-top.png` | 顶图 — top, **Studio only** |

| Directory | Creature | Plates |
|---|---|---|
| `views/` | Embercrest | five — a Studio run, so it has a top view |
| `stormsail-views/` | Stormsail | four |
| `ashcoil-views/` | Ashcoil | four |
| `cragjaw-views/` | Cragjaw | four |
| `mossback-views/` | Mossback | four |

**The one-shot takes four, the Studio took five.** `views/` keeps its top
plate because it fed 上传多视图 in Hunyuan Studio; every set since is a
four-view turnaround, because the one-shot wants 顶/底/45° left empty. Leave
底图 and the two 45° slots empty in both cases.

**Upload these as they are** — the grey background stays on. Hunyuan runs
subject segmentation itself, and the RGBA cutouts the *local* pipeline needs
are wasted work here.

**The slot order is not the panel order, and neither is the DOM order.** The
one-shot lays its file inputs out 顶 左45° 正 右45° 左 右 背 底, so uploading
by position puts the front view in the top slot; read each input's own label.
A turnaround is *drawn* front, left, back, right, so panels 2 and 3 swap when
they are named. `tools/split_turnaround.py` does the cutting and the naming.

For 纹理绘制 (texture), 图生纹理 takes the set's `1-front.png` alone.

`views/` is the four panels of `embercrest-turnaround.png` plus the top-down
panel of `embercrest-reference-sheet.png`, and **for Embercrest this is the
set to use** — it is what produced `assets/embercrest-textured.glb`.

A higher-resolution set was generated (one full-frame image per view, 1536
wide, ~1450 px of dragon instead of ~540) and then deleted, because it made
things worse and kept 10 MB of PNG to do it. Measured, it gave better
proportions and a worse mesh: mushy muzzle, jaw and brow losing definition.
Two one-shots confirmed it — `textured-hq` from those plates, and
`textured-3v` from front/back/top only — both worse than this set. The
prompts are in the git history if the experiment is ever worth repeating; see
"Two plate experiments that both failed" in `docs/MODEL_GENERATION.md`.

Resolution is not the lever anyway: local geometry conditioning resizes every
view to a fixed 518 or 224 square (see "Does input resolution matter" in the
same doc), so ~540 px of dragon is already above what it consumes.

## Design inputs

| File | What it is |
|---|---|
| `concepts.png` | The three original designs, A Embercrest / B Stormsail / C Ironroot. Embercrest is the one taken forward |
| `prompt.txt` | The prompt that produced `concepts.png` |
| `concepts-hoard-run.png` | The three hoard-run designs, A Ashcoil / B Cragjaw / C Mossback. All three were taken forward |
| `prompt-hoard-run.txt` | The prompt that produced it |
| `concepts-elemental.png` | The three new elemental variants, A Rimefang / B Blightmaw / C Tidewrack. All three were taken forward |
| `prompt-elemental.txt` | The prompt that produced it, and the two constraints baked into it |
| `*-reference-sheet.png` | The creature for a human: front, side and top plus detail insets. Six exist — Embercrest, Stormsail, Ironroot, Ashcoil, Cragjaw, Mossback. Ironroot is the only one never built |
| `*-turnaround.png` | The creature for a *generator*: front, left, back, right in one pose, one scale, one eye level. The reference sheets are not usable this way — their side view is a different pose from their front, and a multi-view generator reads that as two animals |

The chain is **concepts board → reference sheet → turnaround → plates**, and
each step references the step before it, never a derivative of it: image-to-image
drift compounds and is invisible in a bounding box (see `docs/MODEL_GENERATION.md`).

### The hoard-run three

Designed against the encounter table in `docs/DIRECTION.md` — one creature per
encounter kind that had no readable target yet, and each a body plan the engine
had never carried, so they separate by silhouette at flight range instead of by
hue push.

| Design | Encounter kind | Body plan |
|---|---|---|
| **Ashcoil**, a serpentine sky-wyrm | rival dragon, elite | legless: one snake trunk, two wings behind the skull, four finger ribs |
| **Cragjaw**, an armoured ground drake | ground defence | wingless quadruped, heavy plates, long tail |
| **Mossback**, a shaggy horned grazer | prey herd | wingless quadruped, hooves, stub tail |

### The elemental family

Six species on **two** skeletons. The point of the family is that a rival read
at flight range is a silhouette, and a hue push cannot change one -- but a
variant that shares its anatomy costs no rigging at all, because the rigger's
fit remaps one skeleton onto differently-proportioned meshes of the same
structure.

| Element | Design | Body plan | Skeleton |
|---|---|---|---|
| Ember | **Embercrest** | six-limbed dragon | `winged-quadruped.json` |
| Stone | **Ironroot** | six-limbed dragon, heavy | `winged-quadruped.json` |
| Frost | **Rimefang** | six-limbed dragon, lean | `winged-quadruped.json` |
| Blight | **Blightmaw** | six-limbed dragon, broad | `winged-quadruped.json` |
| Storm | **Stormsail** | wyvern | `winged-biped.json` |
| Tide | **Tidewrack** | wyvern, finned | `winged-biped.json` |

**That sharing is a constraint on the concept art, not a happy accident.** The
fit is a per-axis affine remap, so it absorbs proportion but not a change in
*structure*: the prompt has to ask for the same neck length, the same tail
length and the same number of wing finger ribs, or the variant needs a
skeleton of its own. `prompt-elemental.txt` records how that was asked for.

**Draw the mouth slightly parted.** Every sculpt before this one came back with
the mouth fused shut and no cavity, which leaves heat weighting no gap to split
on -- Ashcoil's mandible measured 0.62 jaw / 0.37 head and a jaw rotation bent
the whole muzzle. Asking for a parted mouth in every panel of the turnaround
produced a real mouth interior on all four of these.

Each species also carries `<model>.breath.cfg` (what its breath does and looks
like) and `<model>.rig.cfg` (how it moves) beside its glTF.

## Comparisons

| File | What it compares |
|---|---|
| `embercrest-mesh-candidates.png` | The first two cloud candidates — TRELLIS.2 from one side view against Hunyuan Studio from five views |
| `embercrest-lowpoly-retopo.png` | Hunyuan Studio's retopo stage, and the horn-spire artefact it introduces |
| `embercrest-local-trellis2.png` | TRELLIS.2 running locally on x99 |
| `embercrest-gpu-quality-gap.png` | The same TRELLIS.2 graph and seed on two GPUs. Blackwell smooth, Turing faceted — the aggregate statistics all agreed and only the render showed it |
| `embercrest-local-hunyuan3d-mv.png` | Hunyuan3D-2mv locally at the default octree 256, stippled membranes and all |
| `embercrest-hunyuan-octree-fix.png` | That stipple before and after raising octree_resolution to 512 |
| `embercrest-hunyuan-best.png` | The best local mesh: Hunyuan3D-2mv, four views, octree 512 |
| `embercrest-model-field.png` | All four generators side by side on one camera |
| `embercrest-cloud-vs-local-head.png` | Head detail: why the cloud wins. It runs v3.1; the newest open weights are 2.1, which has no multi-view variant |
| `embercrest-hunyuan-textured.png` | The finished cloud asset — 19,965 quads, UVs, and a 4K PBR set |
| `elemental-breath.png` | The six species breathing, one emitter driven by six `<model>.breath.cfg` files. Two rounds of retuning: the first storm violet and the first stone ochre both clipped to white, and tide landed on top of frost's cyan |
| `elemental-roster.png` | Six species in one match, each bot wearing its own mesh, rig and breath |
| `ashcoil-view-count.png` | Whether a fifth (top) plate helps the one-shot. Two runs per arm; four views win on head definition, and the top plate narrows the wingspan 14-20%. See "Four views beat five" in `docs/MODEL_GENERATION.md` |

## The meshes are not here

Generated GLBs and their texture sets live in `assets/` and are gitignored
(`assets/*.glb` covers every candidate and every rigged build; also
`assets/embercrest-textures/`) — large, regenerable from the service, and
under hosted terms nobody fetched. See `ATTRIBUTION.md` and the pipeline
section of `docs/MODEL_GENERATION.md`.

What a finished creature leaves behind in the repo is the plates above, the
skeleton in `tools/skeletons/`, and the acceptance record in
`artifacts/<name>/` — everything needed to rebuild the mesh, not the mesh.
