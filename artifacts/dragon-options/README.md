# Dragon design and model-generation artifacts

Every comparison image below has its subject burned into the image itself —
an amber title bar saying what is being compared, and a caption under each
panel. `nm` in a caption means non-manifold edges, the count that decides
whether auto-weighting will work later.

The findings behind these live in `docs/MODEL_GENERATION.md`.

## Upload these

`views/` holds the five images to feed a generator, named in the order
Hunyuan Studio's 上传多视图 slots appear (see the runbook in
`docs/MODEL_GENERATION.md`):

| File | Slot |
|---|---|
| `views/1-front.png` | 正图 — front, the only required one |
| `views/2-back.png` | 背图 — back |
| `views/3-left.png` | 左图 — left |
| `views/4-right.png` | 右图 — right |
| `views/5-top.png` | 顶图 — top |

Leave 底图 and the two 45° slots empty. **Upload these as they are** — the
grey background stays on. Hunyuan runs subject segmentation itself, and the
RGBA cutouts the *local* pipeline needs are wasted work here.

For 纹理绘制 (texture), 图生纹理 takes `views/1-front.png` alone.

These are the four panels of `embercrest-turnaround.png` plus the top-down
panel of `embercrest-reference-sheet.png`, and **this is the set to use** —
it is what produced `assets/embercrest-textured.glb`, the best asset so far.

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
| `embercrest-reference-sheet.png` | Embercrest for a human: front, side, top plus head, wing-root and foot insets |
| `stormsail-reference-sheet.png`, `ironroot-reference-sheet.png` | The same for the two designs not taken forward |
| `embercrest-turnaround.png` | Embercrest for a *generator*: front, left, back, right in one pose, one scale, one eye level. The reference sheets are not usable this way — their side view is a different pose from their front, and a multi-view generator reads that as two animals |

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

## The meshes are not here

Generated GLBs and their texture sets live in `assets/` and are gitignored
(`assets/embercrest-cand-*.glb`, `assets/embercrest-textures/`) — large,
regenerable from the service, and under hosted terms nobody fetched. See
`ATTRIBUTION.md` and the pipeline section of `docs/MODEL_GENERATION.md`.
