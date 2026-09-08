"""Convert a ComfyUI *UI* workflow into the *API* prompt format, and slice it.

ComfyUI ships its workflow templates (including the TRELLIS.2 / Pixal3D one)
in the format the browser editor saves -- nodes plus a link table -- but the
`/prompt` endpoint wants the API format: a flat map of node id to
`{class_type, inputs}` with links written as `[source_id, source_slot]`.
The editor does that conversion in JavaScript, so a headless run has to do it
here.

Two things make it more than a rename:

- **Widget values are positional.** A node's `widgets_values` is a bare list
  that lines up with the *widget* inputs in the order `/object_info` declares
  them, so the schema is required to name them. A widget is an input whose
  type is `INT`/`FLOAT`/`STRING`/`BOOLEAN` or a combo (a list of choices);
  everything else (`MODEL`, `IMAGE`, `VAE`, `MESH`, ...) only ever arrives
  over a link.
- **Seeds take two slots.** An input declared with `control_after_generate`
  is followed in `widgets_values` by its control mode ("fixed", "increment"),
  which is editor state and not an input. Miss it and every later widget on
  that node is off by one -- KSampler's `[43, 'fixed', 12, 1, 'euler', ...]`
  would put 'fixed' into `steps`.

`slice_to` then keeps only what a chosen output node actually depends on,
which is how the 66-node template becomes the dozen nodes of its geometry
path -- no texture bake, no models we did not download.

Slice to a node whose schema says `output_node: True`, though. ComfyUI only
executes a graph on behalf of an output node, and `MeshToFile3D` is *not*
one: it hands a `FILE_3D_GLB` to something else. `Save3DAdvanced` is the one
that writes the file and makes the branch run.

    python tools/comfy_workflow.py convert ui.json object_info.json out.json
    python tools/comfy_workflow.py convert ui.json object_info.json out.json \
        --slice MeshToFile3D
"""
from __future__ import annotations

import argparse
import json
import sys

# Inputs of these types are never widgets; they only ever come over a link.
LINK_ONLY_HINT = {
    "MODEL", "CLIP", "VAE", "CLIP_VISION", "CONDITIONING", "LATENT", "IMAGE",
    "MASK", "MESH", "VOXEL", "GUIDER", "SIGMAS", "SAMPLER", "NOISE",
    "CONTROL_NET", "STYLE_MODEL", "GLIGEN", "UPSCALE_MODEL", "PHOTOMAKER",
    "AUDIO", "WEBCAM", "POINT", "BBOX", "SEGS",
}

# There is deliberately no list of "preview" nodes to drop. In ComfyUI 0.34
# PreviewImage, MaskPreview and Preview3DAdvanced all declare real outputs and
# pass their input straight through, so the template wires the preprocessed
# image to Trellis2Conditioning *via* a PreviewImage. Dropping them by name
# silently severs the graph -- the conditioning node loses its `image` input
# and nothing upstream of it, LoadImage included, survives the slice. The rule
# that works is: keep every node the server knows about, and let `slice_to`
# discard whatever the chosen output does not reach. Nodes that exist only in
# the editor (Note, MarkdownNote) are absent from /object_info and drop out on
# their own.


def is_widget(spec) -> bool:
    """True when this input's value lives in `widgets_values`."""
    t = spec[0] if isinstance(spec, (list, tuple)) and spec else spec
    if isinstance(t, list):
        return True          # a combo: list of choices
    if not isinstance(t, str):
        return False
    if t in LINK_ONLY_HINT:
        return False
    return t in ("INT", "FLOAT", "STRING", "BOOLEAN", "COMBO")


def widget_names(schema: dict) -> list[tuple[str, dict]]:
    """Widget inputs in declaration order, with their option dicts."""
    out = []
    inp = schema.get("input", {}) or {}
    for section in ("required", "optional"):
        for name, spec in (inp.get(section) or {}).items():
            if is_widget(spec):
                opts = spec[1] if isinstance(spec, (list, tuple)) and len(spec) > 1 and isinstance(spec[1], dict) else {}
                out.append((name, opts))
    return out


def build_link_table(ui: dict) -> dict:
    """link id -> (origin node id, origin slot)."""
    table = {}
    for link in ui.get("links") or []:
        # [link_id, origin_node, origin_slot, target_node, target_slot, type]
        if isinstance(link, (list, tuple)) and len(link) >= 3:
            table[link[0]] = (link[1], link[2])
        elif isinstance(link, dict):
            table[link.get("id")] = (link.get("origin_id"), link.get("origin_slot"))
    return table


def convert(ui: dict, object_info: dict) -> dict:
    links = build_link_table(ui)
    prompt: dict[str, dict] = {}
    skipped: list[str] = []

    for node in ui.get("nodes") or []:
        ntype = node.get("type")
        nid = str(node.get("id"))

        # mode 2 = muted, 4 = bypassed. Neither should execute.
        if node.get("mode") in (2, 4):
            skipped.append(f"{nid}:{ntype}(muted/bypassed)")
            continue
        if ntype in ("Reroute", "PrimitiveNode"):
            skipped.append(f"{nid}:{ntype}(unsupported passthrough)")
            continue
        schema = object_info.get(ntype)
        if schema is None:
            skipped.append(f"{nid}:{ntype}(unknown to this server)")
            continue

        inputs: dict = {}

        # 1. widgets, positionally, honouring the control_after_generate slot
        values = list(node.get("widgets_values") or [])
        if isinstance(node.get("widgets_values"), dict):
            # some frontend versions save a name->value map; take it directly
            inputs.update(node["widgets_values"])
            values = []
        vi = 0
        for name, opts in widget_names(schema):
            if vi >= len(values):
                break
            inputs[name] = values[vi]
            vi += 1
            if opts.get("control_after_generate"):
                vi += 1        # skip 'fixed'/'increment'/'randomize'

        # 2. links override whatever a widget slot held
        for slot in node.get("inputs") or []:
            lid = slot.get("link")
            if lid is None:
                continue
            origin = links.get(lid)
            if origin is None or origin[0] is None:
                continue
            inputs[slot["name"]] = [str(origin[0]), origin[1]]

        prompt[nid] = {"class_type": ntype, "inputs": inputs}

    # drop links that point at nodes we skipped, so /prompt does not 400
    for nid, node in prompt.items():
        for name, val in list(node["inputs"].items()):
            if isinstance(val, list) and len(val) == 2 and isinstance(val[0], str):
                if val[0] not in prompt:
                    del node["inputs"][name]

    prompt["__skipped__"] = skipped  # caller prints then removes
    return prompt


def resolve_switches(prompt: dict, overrides: dict[str, bool] | None = None) -> list[str]:
    """Collapse every `ComfySwitchNode` whose selector is a constant.

    The shipped template offers TRELLIS.2 and Pixal3D as two branches behind
    switches fed by one `PrimitiveBoolean`. A switch is an ordinary node, so
    both of its inputs are real dependencies and a plain dependency slice
    keeps the losing branch alive -- which here means loading a second 5.2 GB
    UNet and a MoGe model just to throw the result away. Rewriting each switch
    to the branch its constant selects lets the following `slice_to` drop the
    other one entirely.

    `overrides` maps a PrimitiveBoolean node id to the value to force.
    """
    notes = []
    for nid, value in (overrides or {}).items():
        if nid in prompt:
            prompt[nid]["inputs"]["value"] = value
            notes.append(f"forced {nid} ({prompt[nid]['class_type']}) = {value}")

    def constant_bool(ref):
        """A literal True/False, or the value of a constant node it points at."""
        if isinstance(ref, bool):
            return ref
        if isinstance(ref, list) and len(ref) == 2 and ref[0] in prompt:
            src = prompt[ref[0]]
            if src["class_type"] in ("PrimitiveBoolean", "Boolean"):
                v = src["inputs"].get("value")
                return v if isinstance(v, bool) else None
        return None

    # id -> replacement reference, iterated so a switch feeding a switch settles
    for _ in range(8):
        replacement: dict[str, object] = {}
        for nid, node in prompt.items():
            if node["class_type"] != "ComfySwitchNode":
                continue
            picked = constant_bool(node["inputs"].get("switch"))
            if picked is None:
                continue
            branch = node["inputs"].get("on_true" if picked else "on_false")
            if branch is not None:
                replacement[nid] = branch
        if not replacement:
            break
        for nid, node in prompt.items():
            for name, val in node["inputs"].items():
                if isinstance(val, list) and len(val) == 2 and val[0] in replacement:
                    node["inputs"][name] = replacement[val[0]]
        for nid in replacement:
            notes.append(f"resolved switch {nid} -> {replacement[nid]}")
            del prompt[nid]
    return notes


def slice_to(prompt: dict, target: str) -> dict:
    """Keep only `target` (a node id or a class_type) and its dependencies."""
    ids = [target] if target in prompt else [
        nid for nid, n in prompt.items() if n.get("class_type") == target
    ]
    if not ids:
        raise SystemExit(f"no node matching {target!r}; have: "
                         f"{sorted({n['class_type'] for n in prompt.values()})}")
    keep: set[str] = set()
    stack = list(ids)
    while stack:
        nid = stack.pop()
        if nid in keep or nid not in prompt:
            continue
        keep.add(nid)
        for val in prompt[nid]["inputs"].values():
            if isinstance(val, list) and len(val) == 2 and isinstance(val[0], str):
                stack.append(val[0])
    return {k: v for k, v in prompt.items() if k in keep}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("cmd", choices=["convert"])
    ap.add_argument("ui_json")
    ap.add_argument("object_info_json")
    ap.add_argument("out_json")
    ap.add_argument("--slice", help="node id or class_type to keep the deps of")
    ap.add_argument("--set-bool", action="append", default=[],
                    metavar="NODE=true|false",
                    help="force a PrimitiveBoolean, then collapse the switches "
                         "it selects (repeatable)")
    args = ap.parse_args()

    ui = json.load(open(args.ui_json))
    oi = json.load(open(args.object_info_json))
    prompt = convert(ui, oi)
    for line in prompt.pop("__skipped__"):
        print("skipped", line, file=sys.stderr)

    overrides = {}
    for spec in args.set_bool:
        nid, _, val = spec.partition("=")
        overrides[nid] = val.strip().lower() in ("1", "true", "yes")
    for line in resolve_switches(prompt, overrides):
        print(line, file=sys.stderr)

    if args.slice:
        prompt = slice_to(prompt, args.slice)
    json.dump(prompt, open(args.out_json, "w"), indent=1)
    print(f"wrote {args.out_json}: {len(prompt)} nodes")
    for nid, n in sorted(prompt.items(), key=lambda kv: int(kv[0])):
        print(f"  {nid:>4} {n['class_type']}")


if __name__ == "__main__":
    main()
