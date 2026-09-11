# Automating the Hunyuan one-shot (图/文生3D)

The one-shot is the good path: it emits 1.5 M geometry **with** UVs and a PBR
set in a single generation, never touching the retopo or UV stages that the
staged chain forces on you (`语义UV` refuses anything over 30 K faces). It
draws on its own **20/day pool**, separate from the Studio's 30, and lives on
a different API surface — `/api/3d/creations/*`, not `/api/game3d/*`. That
separation is why the two counters never move together.

Driven through `chrome-use` against a logged-in Chrome. Every step below was
run this way for the Stormsail wyvern.

## 1. Open the one-shot panel

From `https://3d.hunyuan.tencent.com/`, the entry is the banner's 立即开始.
`@ref` clicks fail here — the home page runs a carousel that re-renders and
invalidates refs between snapshot and click. What works is dispatching a full
pointer sequence and walking up the ancestor chain, because the handler is not
always on the element holding the text:

```js
const w = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
let n, el = null;
while (n = w.nextNode()) if (n.nodeValue.trim() === '立即开始') el = n.parentElement;
let p = el;
for (let k = 0; k < 4 && p; k++) {
  for (const ev of ['pointerdown','mousedown','pointerup','mouseup','click'])
    p.dispatchEvent(new MouseEvent(ev, {bubbles:true, cancelable:true, view:window}));
  p = p.parentElement;
}
```

The panel is open when `document.querySelectorAll('input[type=file]').length`
becomes non-zero. The URL does not change — it is a modal, so do not wait on
navigation.

## 2. Switch to 多张图片 and create the slots

The one-shot takes multi-view too. Select **多张图片** with the same pointer
dispatch, then click **添加多视图** — that click is what *creates* the eight
file inputs; before it there are none.

Two things about that second click have changed since the Stormsail run:

- **The label now reads `添加多视图（Min2，Max8）`**, so an exact-string tree
  walker finds nothing. Match with `startsWith` instead of `===`.
- **Matching the text is not enough.** The handler is on the `+` at the right
  end of the row, and the four-ancestor pointer dispatch that works everywhere
  else on this site returns "clicked" while creating zero inputs. Click the
  `+` by coordinate — `chrome-use click 274 239` at the default viewport.

Either way the check is the same: the slots exist when
`document.querySelectorAll('input[type=file]').length` becomes 8. Verify it
rather than trusting the click, because both failure modes are silent.

## 3. Map slots by label, never by index

**The one-shot's slot order differs from the Studio's.** The Studio lays them
out 正 背 左 右 顶 底 左45° 右45°; the one-shot's DOM order is

    顶, 左45°, 正, 右45°, 左, 右, 背, 底

so uploading by position silently puts the front view in the top slot. Read
each input's own label instead:

```js
[...document.querySelectorAll('input[type=file]')].map((i, k) => {
  let p = i, label = '?';
  for (let d = 0; d < 6 && p; d++) {
    const m = (p.innerText || '').trim().match(/上传(正|背|左45°|右45°|左|右|顶|底)图/g);
    if (m && m.length === 1) { label = m[0]; break; }
    p = p.parentElement;
  }
  i.setAttribute('data-cu', 'os' + k);
  return {idx: k, label};
});
```

Then `chrome-use upload "input[data-cu=osN] " <file>` per slot. The React
dropzone consumes the file and clears `input.files`, so **a successful upload
reads as 0 files** — verify instead by watching that slot's placeholder label
disappear from the panel text.

Leave 顶/底/45° empty for a four-view turnaround. Face count defaults to
`1.5m`, which is what you want.

## 4. Generate and poll the right API

Submit with 立即生成 (pointer dispatch again). The badge next to `API` drops
by one — that is the 20/day pool, and it is the confirmation the job was
accepted.

Poll `/api/3d/creations/detail?creationsId=<id>`; the id appears in
`performance.getEntriesByType('resource')` right after submission. The
response carries `status`, `result[0].progress` and
`result[0].progressGeometry`. Clear `performance.clearResourceTimings()`
before submitting, or you will read the *previous* job's id back.

**Do not treat the presence of `urlResult` as completion.** It exists from the
start carrying only `invisible_wall` and `air_wall`; the keys you want appear
only at `status: "success"`. Poll for `status` or for `urlResult.textureGlb`
by name — a truthiness check on `urlResult` returns immediately and wrong.

Geometry finishes well before the job does: `progressGeometry` hit 100 at
`progress` 47, and the run then sat at 99 for about a minute while the texture
baked. Budget ~4-5 minutes per job.

**Jobs can be queued back to back.** Generation is server-side, so reloading
the page and submitting the next creature does not disturb the one in flight;
three were run this way and polled together afterwards. The badge decrements
once per submission, which is the only confirmation that a job was accepted.

## 5. Take the right URL

`result[0].urlResult` holds several, and the difference matters:

| field | what it is |
|---|---|
| `geometryGlb` | geometry only — `POSITION` and nothing else, no normals, no UVs, no images |
| **`textureGlb`** (same as `glb`) | **the one you want** — 1.5 M tris with `NORMAL`, `TEXCOORD_0` and 3 PBR images |
| `obj`, `textureObj`, `obj_url` | OBJ variants |

A naive "first `.glb` in the response" grab lands on `geometryGlb` and hands
you an untextured point-attribute-only mesh. Ask for `textureGlb` by name.

Both are plain COS HTTPS links; `curl` fetches them without auth.
