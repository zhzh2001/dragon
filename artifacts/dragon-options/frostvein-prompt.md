# Frostvein — original ice dragon

Generated 2026-09-14 with the built-in imagegen tool. This is a new creature,
not a repair or a reskin of Rimefang. The turnaround is the authoritative input.

## Generation prompt

Create a production 3D modeling turnaround sheet for an original ice dragon named Frostvein. Four equal rectangular panels in a precise 2 by 2 grid on plain medium gray background: top left orthographic FRONT, top right orthographic BACK, bottom left orthographic LEFT SIDE facing left, bottom right orthographic RIGHT SIDE facing right. No text, no borders, no scenery, no shadows extending out of panel. SAME identical dragon in identical neutral standing pose in all four panels, entire creature uncropped with ample margins. Painted realistic fantasy game creature, elegant predatory quadruped with exactly four distinct legs plus two huge bat wings spread fully sideways in a symmetrical relaxed T pose, wing fingers well separated and membranes smooth uninterrupted broad icy blue surfaces. Frost-white and pale slate-blue scales, deep blue underside, restrained sapphire vein patterns along wing membranes, swept-back pair of solid dark icy horns, small orderly dorsal ice spikes, long straight tapering tail held backwards. Solid opaque anatomy, no particle effects or transparent ice. Head projects forward on gently curved neck; mouth slightly open with visibly separate substantial lower jaw and dark true mouth cavity, small teeth, bright cyan eyes. Limbs separated from body, feet planted slightly apart with distinct claws, knees gently bent. Wings widest dimension. Side views must preserve same horizontally spread wing pose seen edge on, never fold wings. Design evokes northern fantasy ice dragons but is original, no recognizable named character. Crisp sculptural forms and neutral diffuse studio illumination.

## Plates and generation

The actual sheet has a clearly separated 2×2 layout, unlike the single-row
turnarounds expected by `tools/split_turnaround.py`. Explicit crops use the
visible panel boundaries; no subject touches a crop boundary. The side views
show more membrane than a strict orthographic T pose would. The generated mesh
must therefore be measured directly rather than assuming the illustrated pose.

```sh
sips -c 500 762 --cropOffset 1 1 artifacts/dragon-options/frostvein-turnaround.png --out artifacts/dragon-options/frostvein-views/1-front.png
sips -c 502 764 --cropOffset 0 770 artifacts/dragon-options/frostvein-turnaround.png --out artifacts/dragon-options/frostvein-views/2-back.png
sips -c 500 760 --cropOffset 512 2 artifacts/dragon-options/frostvein-turnaround.png --out artifacts/dragon-options/frostvein-views/3-left.png
sips -c 514 764 --cropOffset 510 770 artifacts/dragon-options/frostvein-turnaround.png --out artifacts/dragon-options/frostvein-views/4-right.png
python3 tools/hunyuan_oneshot.py run artifacts/dragon-options/frostvein-views assets/frostvein-cand-oneshot.glb
```

Four labelled cardinal slots only; top, bottom, and diagonal slots left empty.
Use `textureGlb` after success, as specified in `tools/hunyuan_oneshot.md`.

The first generation (`cff65d86-9a23-40c7-a1dd-5579c9d07088`) was rejected
before rig fitting: `sips --cropOffset 0 0` silently produced a centered crop
instead of the front panel. Its incorrect plate and generation record are in
`artifacts/frostvein/rejected-input/`; the local source is
`assets/frostvein-cand-first.glb`. The corrected front crop uses offset `1 1`.
All four final plates were visually inspected individually before resubmission.
Corrected generation: `4f7e34c5-0062-415c-b62a-6e5f1636f5d1`.
