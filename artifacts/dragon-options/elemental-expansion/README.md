# Elemental silhouette expansion — 2026-09-28

Original concept exploration requested by the user, inspired by triangular
sails, swept wings, appealing heavy-bodied dragons, and feathered avian dragons.
`concepts.png` was generated with the built-in imagegen tool. These are new
species candidates rather than replacements for existing production assets.

| Species | Element | Silhouette and motion target |
|---|---|---|
| Sunspear | Fire | Tall triangular ochre sails, narrow bronze torso, wedge muzzle, whiplash tail; deliberate soaring with a sharp wing snap. |
| Rimeplume | Frost | Silver feather fans, slate narrow muzzle, long feather-tipped tail, light stalking legs; buoyant wingbeat and broad braking fan. |
| Galehook | Storm | Low athletic body, swept crescent wings with reinforced tips, rudder tail; fast banking and compact recovery. |
| Brinebellows | Tide | Round powerful chest and belly, sensory barbels, manta-like wings, paddle tail and broad feet; heavy buoyant flap. |
| Cairnhorn | Stone | Terraced shoulder plates, squared slate-fan wings, shovel jaw, short wedge tail; slow heavy wingbeat and planted gait. |
| Mireveil | Blight | Long stalking limbs and neck, leaf-window membranes, seedpod crown; deliberate neck tracking and sinuous tail. |

First proposed production candidates: Sunspear and Rimeplume, which test the
largest wing-shape departures. Board art is not a rigging reference: the fire
wings still have concave trailing edges, so its turnaround must enforce a
clean triangular planform. Feather wings must have layered overlapping vanes
and three structural fan branches per wing, not a dense cloud of tiny quills.
Cairnhorn needs flexible membrane between mineral ribs, not literal rock slabs.

## Production acceptance

Generate a dedicated reference/turnaround with spread wings, separated legs,
straight tail and visibly parted jaw. Inspect all four cardinal plates and
leave the Hunyuan top slot empty. Use `tools/hunyuan_oneshot.py` and retrieve
`textureGlb` only after success. Preserve job IDs and input provenance.

Each mesh needs its own measured skeleton JSON; shared anatomy is not shared
measurements. Decimate to approximately 80k triangles, use <=4 influences and
<=256 joints, repair materials with appropriate matte feather roughness, and
provide complete rig/flight/breath profiles including standing wing and stance
blocks. Preserve all current species. Require full-size side/front/rear/top
motion inspection, close jaw, ground feet, actual flight/landing and switching
from dragon.glb before calling any new species playable.

## Generation record

- Rimeplume four cardinal plates: `rimeplume-views/`, checked individually.
  Hunyuan job `ecf9227e-6649-4634-9ec9-7ac6165b89a8`, submitted 2026-09-28.
- Sunspear uses the second turnaround attempt (`sunspear-turnaround.png`):
  the first preserved conventional scallops and was rejected before 3D.
  The revised plates use a single triangular outline per wing.
  Hunyuan job `70d70143-ef26-48b5-bc13-41a53b678c9e`, submitted 2026-09-28.

Both jobs completed successfully and their `textureGlb` outputs were validated
for GLB header/length, normals, UVs and three embedded PBR images. Exact counts
and SHA256 hashes are in `generation.json`. Both now have independently measured rigs, material repairs, and game profiles.
See `../../elemental-expansion/README.md` for validation and playtest instructions.
The other four species remain concepts.
