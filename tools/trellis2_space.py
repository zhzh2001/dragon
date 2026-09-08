"""Drive the microsoft/TRELLIS.2 Hugging Face Space to turn one image into a GLB.

The free cloud pass of docs/MODEL_GENERATION.md step 2. The Space exposes its
whole pipeline over the Gradio API with no key of its own, so the only
credential involved is a Hugging Face token, and that is purely for ZeroGPU
quota (see the quota note below).

    uv venv /tmp/gc && uv pip install --python /tmp/gc/bin/python gradio_client
    /tmp/gc/bin/python tools/trellis2_space.py in.png out.glb --res 1024

Quota is the whole story here. Both GPU stages are decorated
`@spaces.GPU(duration=120)`, and ZeroGPU refuses a call whose *requested*
duration exceeds the quota left, not the time it would actually burn. So:

  anonymous     120 s/day  -- cannot finish even one asset (240 s requested)
  free account  ~210-300 s/day  -- one asset a day, maybe two
  PRO ($9/mo)   2400 s/day  -- iterate freely

Pass a token with --token or HF_TOKEN. Every 3D-generation Space that was live
when this was written runs on ZeroGPU or cpu-basic, so there is no dedicated-GPU
Space to sidestep the quota with -- the quota is per account, not per Space.
"""
import argparse
import os
import shutil
import sys
from pathlib import Path

from gradio_client import Client, handle_file

SPACE = "microsoft/TRELLIS.2"


def as_path(v):
    """Gradio returns a str, or a filedata dict, depending on the component."""
    if isinstance(v, (list, tuple)):
        v = v[0]
    if isinstance(v, dict):
        return v.get("path") or v.get("url")
    return v


def generate(image, out, res="1024", decimation=200000, texture=2048,
             seed=1, token=None, cache=None):
    client = Client(SPACE, hf_token=token,
                    download_files=str(cache) if cache else True)

    # The Space keeps the sampled latents in per-session state, so preprocess,
    # sampling and extraction have to run on one Client.
    client.predict(api_name="/start_session")

    prepared = as_path(client.predict(input=handle_file(image),
                                      api_name="/preprocess_image"))
    print(f"background removed -> {prepared}", flush=True)

    client.predict(image=handle_file(prepared), seed=seed, resolution=res,
                   api_name="/image_to_3d")
    print(f"sampled at {res}", flush=True)

    glb = as_path(client.predict(decimation_target=decimation,
                                 texture_size=texture,
                                 api_name="/extract_glb"))
    shutil.copy(glb, out)
    print(f"saved {out} ({Path(out).stat().st_size:,} bytes)", flush=True)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("image")
    ap.add_argument("out")
    ap.add_argument("--res", default="1024", choices=["512", "1024", "1536"])
    # Left dense on purpose: this mesh is retopo input, not the shipped asset.
    # docs/MODEL_GENERATION.md targets 25-40K tris after Quad Remesher.
    ap.add_argument("--decimation", type=float, default=200000)
    ap.add_argument("--texture", type=float, default=2048)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--token", default=os.environ.get("HF_TOKEN"))
    ap.add_argument("--cache", default=None,
                    help="directory for the Space's intermediate downloads")
    args = ap.parse_args()

    if not args.token:
        print("no HF token: anonymous ZeroGPU quota (120 s/day) cannot finish "
              "one asset. Pass --token or set HF_TOKEN.", file=sys.stderr)

    generate(args.image, args.out, res=args.res, decimation=args.decimation,
             texture=args.texture, seed=args.seed, token=args.token,
             cache=args.cache)


if __name__ == "__main__":
    main()
