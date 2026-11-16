#!/usr/bin/env python3
"""Drive Tripo Studio's HD Model generation through chrome-use.

This drives the Studio's own page: it clicks its buttons and waits for the
model. It then downloads the file the page's viewer loaded and decodes it
with gltfpack. It does not call the Studio's API.
Chrome must run with the logged-in Tripo account (see tools/tripo_studio.md).

    tools/tripo_studio.py text "<prompt>" NAME      # text to 3D, export NAME.glb
    tools/tripo_studio.py image PLATE.png NAME      # one image to 3D
    tools/tripo_studio.py views PLATE_DIR NAME      # four plates: *front*, *left*, *right*, *back*
    tools/tripo_studio.py batch JOBS.json           # a list of jobs, skips done ones

Each model is written to OUT_DIR/NAME.glb (default assets/tripo-raw/,
git-ignored). A batch file is a JSON list of
{"name": ..., "prompt": ...}, {"name": ..., "image": ...} or
{"name": ..., "views": DIR} objects. TRIPO_SESSION names the chrome-use
session, so two batches can run in two tabs. TRIPO_BROWSER pins the
session to one Chrome profile when several are connected.
"""

import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

SESSION = os.environ.get("TRIPO_SESSION", "tripo")
# The Chrome profile that holds the Tripo login (`chrome-use browsers` lists them).
BROWSER = os.environ.get("TRIPO_BROWSER")
ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = Path(os.environ.get("TRIPO_OUT", ROOT / "assets" / "tripo-raw"))
GLTFPACK = os.environ.get("GLTFPACK", str(Path.home() / ".local/bin/gltfpack"))
GENERATE_URL = "https://studio.tripo3d.ai/workspace/generate"


def cu(*args, check=True, timeout=120):
    pin = ["--browser", BROWSER] if BROWSER else []
    r = subprocess.run(["chrome-use", "--session", SESSION, *pin, *args],
                       capture_output=True, text=True, timeout=timeout,
                       stdin=subprocess.DEVNULL)
    out = r.stdout + r.stderr
    if check and r.returncode != 0:
        raise RuntimeError(f"chrome-use {' '.join(args)} failed:\n{out}")
    return out


def snapshot():
    return cu("snapshot", "-i")


def ref_after(snap, pattern, nth=0):
    """Return the ref of the nth line matching pattern."""
    hits = [m.group(1) for line in snap.splitlines()
            if re.search(pattern, line) for m in [re.search(r"ref=(e\d+)", line)] if m]
    if len(hits) <= nth:
        raise RuntimeError(f"no element matches {pattern!r} (#{nth})")
    return "@" + hits[nth]


def input_tabs(snap):
    """The four input tabs: image, multi-view, batch, text. They are the four
    unnamed buttons after the Smart Mesh toggle."""
    lines = snap.splitlines()
    start = next(i for i, l in enumerate(lines) if "S m a r t" in l)
    refs = []
    for l in lines[start + 1:]:
        m = re.match(r"\s*- button \[ref=(e\d+)\]$", l)
        if not m:
            break
        refs.append("@" + m.group(1))
    if len(refs) != 4:
        raise RuntimeError(f"expected 4 input tabs, found {refs}")
    return refs


def credits():
    txt = cu("read", check=False)
    # The balance is the number just before the header's "Upgrade" button.
    m = re.search(r"^(\d+)\s*\n\s*Upgrade", txt, re.M)
    return int(m.group(1)) if m else None


def busy():
    txt = cu("read", check=False)
    return bool(re.search(r"generating|in queue|queuing|\d+%", txt, re.I))


def wait_done(limit_s=900):
    t0 = time.time()
    time.sleep(20)
    while time.time() - t0 < limit_s:
        if not busy():
            # Settle: the viewer loads the mesh after the progress text goes.
            time.sleep(8)
            if not busy():
                return time.time() - t0
        time.sleep(15)
    raise RuntimeError("generation did not finish in time")


def model_url():
    """The signed URL of the newest full model the viewer loaded. The viewer
    fetches the whole generation (every triangle, the 4K maps) as one
    meshopt-compressed GLB; Export only decodes that same file in the page."""
    out = cu("network", "requests", "--filter", "tripo_pbr_model_", check=False)
    urls = re.findall(r"(https://tripo-data\S+?_meshopt\.glb\S*)", out)
    return urls[-1] if urls else None


def fetch(name, url):
    """Download the viewer's file and decode it with gltfpack into a plain
    GLB (no meshopt, no quantization), the form Blender reads."""
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    packed = OUT_DIR / f".{name}.meshopt.glb"
    subprocess.run(["curl", "-sf", "-o", str(packed), url], check=True, timeout=600)
    dest = OUT_DIR / f"{name}.glb"
    subprocess.run([GLTFPACK, "-i", str(packed), "-o", str(dest), "-noq", "-kn", "-km"],
                   check=True, capture_output=True, timeout=600)
    packed.unlink()
    return dest


def generate(name, prompt=None, image=None, views=None, tpose=False):
    cu("open", GENERATE_URL)
    time.sleep(7)
    snap = snapshot()
    tabs = input_tabs(snap)
    if prompt is not None:
        cu("click", tabs[3])
        time.sleep(1.5)
        snap = snapshot()
        cu("fill", ref_after(snap, r'textbox "Describe what you want'), prompt)
        if tpose:
            cu("click", ref_after(snapshot(), r'button "T-Pose"'))
            time.sleep(1)
    elif views is not None:
        # The multi-view tab's four slots are the first four file inputs, in
        # the order front, left, right, back. Tag them so each upload names
        # its own slot.
        cu("click", tabs[1])
        time.sleep(2)
        cu("eval", "document.querySelectorAll('input[type=file]').forEach((e,i)=>e.id='tripo_in'+i)")
        for i, slot in enumerate(("front", "left", "right", "back")):
            plate = next(Path(views).glob(f"*{slot}*.png"))
            cu("upload", f"#tripo_in{i}", str(plate.resolve()))
            time.sleep(4)
        time.sleep(4)
    else:
        cu("eval", "document.querySelectorAll('input[type=file]').forEach((e,i)=>e.id='tripo_in'+i)")
        cu("upload", "#tripo_in0", str(Path(image).resolve()))
        time.sleep(6)
    snap = snapshot()
    before = credits()
    cu("network", "requests", "--clear", check=False)
    cu("click", ref_after(snap, r'button "Generate \d+"'))
    secs = wait_done()
    url = None
    for _ in range(12):
        url = model_url()
        if url:
            break
        time.sleep(5)
    if not url:
        raise RuntimeError("the viewer did not load a model")
    after = credits()
    dest = fetch(name, url)
    return {"name": name, "seconds": round(secs), "credits_before": before,
            "credits_after": after, "file": str(dest),
            "bytes": dest.stat().st_size}


def log(entry):
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    with open(OUT_DIR / "log.jsonl", "a") as f:
        f.write(json.dumps(entry) + "\n")
    print(json.dumps(entry), flush=True)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    mode = sys.argv[1]
    if mode == "text":
        log(generate(sys.argv[3], prompt=sys.argv[2]))
    elif mode == "image":
        log(generate(sys.argv[3], image=sys.argv[2]))
    elif mode == "views":
        log(generate(sys.argv[3], views=sys.argv[2]))
    elif mode == "batch":
        jobs = json.loads(Path(sys.argv[2]).read_text())
        for job in jobs:
            if (OUT_DIR / f"{job['name']}.glb").exists():
                continue
            for attempt in range(2):
                try:
                    entry = generate(job["name"], job.get("prompt"), job.get("image"), job.get("views"),
                                     job.get("tpose", False))
                    entry["job"] = job
                    log(entry)
                    break
                except Exception as e:  # keep the batch going; the log says why
                    log({"name": job["name"], "error": str(e)[:400], "attempt": attempt})
                    time.sleep(30)
    else:
        print(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main()
