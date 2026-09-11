#!/usr/bin/env python3
"""Drive the Hunyuan 图/文生3D one-shot headlessly through chrome-use.

    tools/hunyuan_oneshot.py submit artifacts/dragon-options/ashcoil-views
    tools/hunyuan_oneshot.py poll <creationsId>
    tools/hunyuan_oneshot.py fetch <creationsId> assets/name-cand-oneshot.glb
    tools/hunyuan_oneshot.py run artifacts/.../views assets/name-cand-oneshot.glb

`tools/hunyuan_oneshot.md` is the prose version and the record of *why* each
step is shaped the way it is. This file exists because the flow was hand-driven
three times and every trap in that document is a step that silently succeeds
while doing the wrong thing:

  - the panel's slot order is 顶 左45° 正 右45° 左 右 背 底, which is neither the
    Studio's order nor the order a turnaround is drawn in, so slots are matched
    by reading each input's own label;
  - the React dropzone clears `input.files`, so a successful upload reads as
    zero files and has to be verified by the placeholder text disappearing;
  - `urlResult` exists from the start carrying junk keys, so completion is
    `status == "success"` and the file is `textureGlb` by name, never the
    first `.glb` in the response.

Every step asserts what it matched rather than trusting a click.
"""
import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

HOME = 'https://3d.hunyuan.tencent.com/'
SESSION = 'hunyuan'
# Plate file name -> the slot label it belongs in. 顶 is optional; the rest are
# a four-view turnaround.
SLOTS = {
    '1-front.png': '上传正图',
    '2-back.png': '上传背图',
    '3-left.png': '上传左图',
    '4-right.png': '上传右图',
    '5-top.png': '上传顶图',
}


def cu(*args, timeout=180):
    """Run chrome-use and return stdout."""
    result = subprocess.run(['chrome-use', *args, '--session', SESSION],
                            capture_output=True, text=True, timeout=timeout)
    if result.returncode != 0:
        raise SystemExit(f'chrome-use {args[0]} failed: {result.stderr.strip()}')
    return result.stdout


def js(expr, timeout=180):
    """Evaluate JS in the page and return the parsed result."""
    out = cu('eval', expr, timeout=timeout)
    # chrome-use prints a header line, then the JSON-encoded result.
    line = out.strip().splitlines()[-1]
    try:
        value = json.loads(line)
    except json.JSONDecodeError:
        return line
    return json.loads(value) if isinstance(value, str) and value[:1] in '[{' else value


# Clicking by @ref fails on this site: the home page runs a carousel that
# re-renders between snapshot and click, and the handler is not always on the
# element holding the text. Dispatch a full pointer sequence up the ancestors.
CLICK_TEXT = '''(() => {
  const want = %s;
  const w = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
  let n, el = null;
  while (n = w.nextNode()) if (n.nodeValue.trim().startsWith(want)) el = n.parentElement;
  if (!el) return false;
  let p = el;
  for (let k = 0; k < 4 && p; k++) {
    for (const ev of ['pointerdown','mousedown','pointerup','mouseup','click'])
      p.dispatchEvent(new MouseEvent(ev, {bubbles:true, cancelable:true, view:window}));
    p = p.parentElement;
  }
  return true;
})()'''


def click_text(label):
    return js(CLICK_TEXT % json.dumps(label))


def open_panel():
    cu('open', HOME, timeout=240)
    time.sleep(4)
    if not click_text('立即开始'):
        raise SystemExit('could not find 立即开始 on the home page')
    time.sleep(4)
    if not click_text('多张图片'):
        raise SystemExit('could not switch to 多张图片')
    time.sleep(3)
    # The label carries a "（Min2，Max8）" suffix and the handler lives on the +
    # at the end of the row; the text click reports success and creates nothing,
    # so click the + by coordinate and verify the inputs appeared.
    cu('click', '274', '239')
    time.sleep(3)
    count = js('document.querySelectorAll("input[type=file]").length')
    if count != 8:
        raise SystemExit(f'expected 8 file inputs after 添加多视图, got {count}')
    return count


MAP_SLOTS = '''(() => JSON.stringify([...document.querySelectorAll("input[type=file]")].map((i,k)=>{
  let p=i,label="?";
  for(let d=0;d<6&&p;d++){
    const m=(p.innerText||"").trim().match(/上传(正|背|左45°|右45°|左|右|顶|底)图/g);
    if(m&&m.length===1){label=m[0];break;}
    p=p.parentElement;
  }
  i.setAttribute("data-cu","os"+k); return {idx:k,label};
})))()'''


def submit(views_dir):
    views = Path(views_dir)
    plates = {name: views / name for name in SLOTS if (views / name).exists()}
    if '1-front.png' not in plates:
        raise SystemExit(f'{views}: 1-front.png is the one required plate')
    open_panel()

    mapping = js(MAP_SLOTS)
    by_label = {entry['label']: entry['idx'] for entry in mapping}
    print('slots:', ', '.join(f"{e['label']}={e['idx']}" for e in mapping))

    for name, path in sorted(plates.items()):
        label = SLOTS[name]
        if label not in by_label:
            raise SystemExit(f'slot {label} not present on the panel')
        cu('upload', f'input[data-cu=os{by_label[label]}] ', str(path.resolve()), timeout=240)
        time.sleep(2)
    time.sleep(3)

    # A dropzone that consumed the file clears input.files, so verify by the
    # placeholder text instead: every slot we filled must have lost its label.
    text = js('document.body.innerText')
    still = [SLOTS[n] for n in plates if SLOTS[n] in text]
    if still:
        raise SystemExit(f'these slots did not take their plate: {still}')
    print(f'uploaded {len(plates)} plate(s): {sorted(plates)}')

    cu('click', '757', '42')  # close the multi-view modal
    time.sleep(2)
    js('performance.clearResourceTimings(); 1')
    cu('click', '166', '505')  # 立即生成
    time.sleep(7)
    ids = js('''(() => JSON.stringify([...new Set(performance.getEntriesByType("resource")
        .map(e=>e.name).filter(n=>n.includes("creationsId="))
        .map(n=>n.split("creationsId=")[1].split("&")[0]))]))()''')
    if not ids:
        raise SystemExit('no creationsId appeared; the job was not accepted')
    print('creationsId', ids[-1])
    return ids[-1]


DETAIL = '''(async () => {
  const r = await fetch("/api/3d/creations/detail?creationsId=%s", {credentials:"include"});
  const j = await r.json(); const d = (j.data||j); const res = (d.result && d.result[0]) || {};
  return JSON.stringify({status: d.status ?? res.status, progress: res.progress,
                         glb: (res.urlResult||{}).textureGlb || ""});
})()'''


def poll(creations_id, interval=20, limit=60):
    for _ in range(limit):
        state = js(DETAIL % creations_id)
        status, progress = state.get('status'), state.get('progress')
        print(f'  {status} {progress if progress is not None else ""}'.rstrip(), flush=True)
        # urlResult is populated long before the job finishes, so the file
        # having a name is the only completion signal worth trusting.
        if state.get('glb'):
            return state['glb']
        if status in ('failed', 'error'):
            raise SystemExit(f'generation {status}')
        time.sleep(interval)
    raise SystemExit('timed out waiting for the generation')


def fetch(url, out_path):
    out = Path(out_path)
    out.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['curl', '-sS', '-o', str(out), url], check=True)
    print(f'{out} ({out.stat().st_size / 1e6:.1f} MB)')


ap = argparse.ArgumentParser(description=__doc__,
                             formatter_class=argparse.RawDescriptionHelpFormatter)
sub = ap.add_subparsers(dest='cmd', required=True)
s = sub.add_parser('submit'); s.add_argument('views')
s = sub.add_parser('poll'); s.add_argument('id')
s = sub.add_parser('fetch'); s.add_argument('id'); s.add_argument('out')
s = sub.add_parser('run'); s.add_argument('views'); s.add_argument('out')
args = ap.parse_args()

if args.cmd == 'submit':
    submit(args.views)
elif args.cmd == 'poll':
    print(poll(args.id))
elif args.cmd == 'fetch':
    fetch(poll(args.id), args.out)
elif args.cmd == 'run':
    fetch(poll(submit(args.views)), args.out)
