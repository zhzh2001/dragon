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
| `views/1-front-正图.png` | 正图 — front, the only required one |
| `views/2-back-背图.png` | 背图 — back |
| `views/3-left-左图.png` | 左图 — left |
| `views/4-right-右图.png` | 右图 — right |
| `views/5-top-顶图.png` | 顶图 — top |

Leave 底图 and the two 45° slots empty. **Upload these as they are** — the
grey background stays on. Hunyuan runs subject segmentation itself, and the
RGBA cutouts the *local* pipeline needs are wasted work here.

For 纹理绘制 (texture), 图生纹理 takes `views/1-front-正图.png` alone.

Views 1-4 are the four panels of `embercrest-turnaround.png`; view 5 is the
top-down panel of `embercrest-reference-sheet.png`.

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
