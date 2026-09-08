"""Generate a mesh from one image with TRELLIS.2 running locally in ComfyUI.

The local half of `docs/MODEL_GENERATION.md` step 3. Unlike the hosted Space
in `trellis2_space.py` there is no quota and no 512 cap: on the x99 box
(RTX 5060 Ti, 16 GB) a 1536 run takes ~34 s and peaks at 7.7 GB.

    # on the machine running ComfyUI, or through an ssh tunnel
    python tools/trellis2_local.py emb-front-rgba.png --res 1536 -o dragon.glb

The graph is built here rather than converted from the shipped template,
because the template carries a whole second pipeline (Pixal3D + MoGe + texture
bake + remesh) behind switches. `tools/comfy_workflow.py` does the conversion
when the rest of that pipeline is wanted; the sampler settings and the two CFG
chains below are lifted from it verbatim.

**The input image must be RGBA with the subject cut out.** ComfyUI's LoadImage
publishes `1 - alpha` as its MASK (nodes.py:1788), i.e. a mask of the
*background*, so an `InvertMask` sits between it and `ImageCropToMask`. Get
that backwards and nothing errors -- TRELLIS.2 sculpts the background plate
and hands back a flat slab with the subject in bas-relief on its face. To cut
a flat-coloured plate out beforehand:

    magick in.png -alpha set -fuzz 12% -fill none \
      -draw "alpha 0,0 floodfill" -draw "alpha W,0 floodfill" \
      -draw "alpha 0,H floodfill" -draw "alpha W,H floodfill" out.png

flood-filling from the corners rather than keying the colour globally, so
greys inside the subject survive.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
import time
import urllib.error
import urllib.request
import uuid

MODELS = {
    "unet": "trellis_2_int8_convrot.safetensors",
    # Comfy-Org/TRELLIS.2 ships dino_v3_vit_l; the combined template names
    # Pixal3D's dino_v3_L_naf_fp32, which is a genuinely different file
    # (different sha256 and size), not the same weights renamed.
    "clip_vision": "dino_v3_vit_l.safetensors",
    "shape_vae": "trellis_2_shape_vae_bf16.safetensors",
}


def build(image: str, res: str, prefix: str) -> dict:
    m = MODELS
    return {
        "122": {"class_type": "LoadImage", "inputs": {"image": image}},
        # LoadImage's MASK is the background; flip it before cropping.
        "901": {"class_type": "InvertMask", "inputs": {"mask": ["122", 1]}},
        "312": {"class_type": "ImageCropToMask", "inputs": {
            "images": ["122", 0], "masks": ["901", 0],
            "width": 1024, "height": 1024,
            # Trellis2Conditioning's tooltip asks for 1.0; the shipped
            # template's 1.1 is the Pixal3D branch's framing.
            "pad_factor": 1.0, "grow_mask": 0, "background": "#000000"}},

        "15": {"class_type": "CLIPVisionLoader", "inputs": {"clip_name": m["clip_vision"]}},
        "299": {"class_type": "Trellis2Conditioning",
                "inputs": {"clip_vision_model": ["15", 0], "image": ["312", 0]}},

        "40": {"class_type": "UNETLoader",
               "inputs": {"unet_name": m["unet"], "weight_dtype": "default"}},
        # Both CFG chains are the template's, which notes they exist to match
        # the original TRELLIS.2 pipeline's defaults.
        "199": {"class_type": "CFGOverride", "inputs": {
            "model": ["40", 0], "cfg": 1, "start_percent": 0.667, "end_percent": 1}},
        "125": {"class_type": "RescaleCFG", "inputs": {"model": ["199", 0], "multiplier": 0.7}},
        "108": {"class_type": "ModelSamplingSD3", "inputs": {"model": ["125", 0], "shift": 5}},
        "279": {"class_type": "CFGOverride", "inputs": {
            "model": ["40", 0], "cfg": 1, "start_percent": 0.769, "end_percent": 1}},
        "126": {"class_type": "RescaleCFG", "inputs": {"model": ["279", 0], "multiplier": 0.5}},

        "117": {"class_type": "VAELoader", "inputs": {"vae_name": m["shape_vae"]}},

        # stage 1 -- sparse structure
        "87": {"class_type": "EmptyTrellis2LatentStructure", "inputs": {"batch_size": 1}},
        "3": {"class_type": "KSampler", "inputs": {
            "model": ["108", 0], "positive": ["299", 0], "negative": ["299", 1],
            "latent_image": ["87", 0], "seed": 56, "steps": 12, "cfg": 7.5,
            "sampler_name": "euler", "scheduler": "normal", "denoise": 1}},
        "119": {"class_type": "VaeDecodeStructureTrellis2", "inputs": {
            "samples": ["3", 0], "vae": ["117", 0], "resolution": "32"}},

        # stage 2 -- shape latents on that structure
        "91": {"class_type": "Trellis2ShapeStage", "inputs": {
            "positive": ["299", 0], "negative": ["299", 1], "voxel": ["119", 0]}},
        "18": {"class_type": "KSampler", "inputs": {
            "model": ["126", 0], "positive": ["91", 0], "negative": ["91", 1],
            "latent_image": ["91", 2], "seed": 42, "steps": 20, "cfg": 7.5,
            "sampler_name": "euler", "scheduler": "normal", "denoise": 1}},

        # stage 3 -- upsample, then decode to a mesh
        "94": {"class_type": "Trellis2UpsampleStage", "inputs": {
            "positive": ["91", 0], "negative": ["91", 1], "shape_latent": ["18", 0],
            "vae": ["117", 0], "target_resolution": res}},
        "23": {"class_type": "KSampler", "inputs": {
            "model": ["126", 0], "positive": ["94", 0], "negative": ["94", 1],
            "latent_image": ["94", 2], "seed": 42, "steps": 12, "cfg": 7.5,
            "sampler_name": "euler", "scheduler": "simple", "denoise": 1}},
        "92": {"class_type": "VaeDecodeShapeTrellis",
               "inputs": {"samples": ["23", 0], "vae": ["117", 0]}},

        # SaveGLB is the one mesh output node that needs no browser viewport
        # state; MeshToFile3D is not an output node and will not run the graph.
        "900": {"class_type": "SaveGLB",
                "inputs": {"mesh": ["92", 0], "filename_prefix": prefix}},
    }


def submit(host: str, prompt: dict) -> str:
    body = json.dumps({"prompt": prompt, "client_id": str(uuid.uuid4())}).encode()
    req = urllib.request.Request(f"{host}/prompt", body,
                                 {"Content-Type": "application/json"})
    try:
        return json.load(urllib.request.urlopen(req, timeout=60))["prompt_id"]
    except urllib.error.HTTPError as e:
        payload = e.read().decode()
        try:
            j = json.loads(payload)
            print("error:", json.dumps(j.get("error")), file=sys.stderr)
            print("node_errors:", json.dumps(j.get("node_errors"), indent=1)[:3000],
                  file=sys.stderr)
        except ValueError:
            print(payload[:2000], file=sys.stderr)
        raise SystemExit(f"/prompt rejected the graph (HTTP {e.code})")


def wait(host: str, pid: str, timeout: float) -> dict:
    deadline = time.time() + timeout
    while time.time() < deadline:
        with urllib.request.urlopen(f"{host}/history/{pid}", timeout=30) as r:
            hist = json.load(r)
        if hist:
            entry = list(hist.values())[0]
            status = entry.get("status", {}).get("status_str")
            if status == "success":
                return entry
            if status == "error":
                raise SystemExit(f"execution failed: "
                                 f"{json.dumps(entry.get('status'))[:2000]}")
        time.sleep(5)
    raise SystemExit("timed out waiting for the prompt")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("image", help="filename already in ComfyUI's input/ dir (RGBA)")
    ap.add_argument("-o", "--out", help="copy the resulting .glb here")
    ap.add_argument("--res", default="1536", choices=["1024", "1536"],
                    help="upsample target; 1536 costs ~2 s and 1.4 GB more (default)")
    ap.add_argument("--host", default="http://127.0.0.1:8188")
    ap.add_argument("--prefix", default="3d/trellis2_local")
    ap.add_argument("--output-root", default="/mnt/Data/comfy-out",
                    help="ComfyUI's --output-directory, to find the saved file")
    ap.add_argument("--timeout", type=float, default=1800)
    args = ap.parse_args()

    pid = submit(args.host, build(args.image, args.res, args.prefix))
    print(f"queued {pid} (res {args.res})", flush=True)
    entry = wait(args.host, pid, args.timeout)

    saved = []
    for out in (entry.get("outputs") or {}).values():
        for key in ("gltf", "3d", "files", "result", "mesh"):
            for f in out.get(key) or []:
                if isinstance(f, dict) and f.get("filename"):
                    saved.append(os.path.join(args.output_root,
                                              f.get("subfolder", ""), f["filename"]))
    if not saved:
        print("finished, but no output file was reported; look under "
              f"{args.output_root}", file=sys.stderr)
        print(json.dumps(entry.get("outputs"), indent=1)[:1500], file=sys.stderr)
        return
    print("saved:", *saved, sep="\n  ")
    if args.out and os.path.exists(saved[0]):
        shutil.copy(saved[0], args.out)
        print(f"copied -> {args.out} ({os.path.getsize(args.out):,} bytes)")


if __name__ == "__main__":
    main()
