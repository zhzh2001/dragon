# Rig probes

Headless, renderer-free probes that run `DragonRig` on a species and print
numbers. They are how the stances, the flight legs and the sole clearance
were screened before anything was rendered: a probe run is ~10 ms per
candidate, a render is ~2 s, so a grid of a thousand angle sets is screened
numerically and the dozen survivors are rendered. None of this replaces the
rendered inspection CLAUDE.md requires; it decides what to render.

| Probe | Prints |
|---|---|
| `stance_probe <model.glb> [override.cfg]` | Grounded: foot heights above the lowest, wing bone directions and finger tips, leg bone directions, spine pitch, and the principal axis of the skin cloud each bone owns |
| `stance_grid <model.glb> <cfg>...` | One line per cfg, same loaded mesh: hind-hand height difference, thigh/shin/upper-arm/forearm angles from vertical, spine pitch, wrist placement ahead of and outboard of the feet |
| `sink_probe <model.glb>...` | Grounded: skins the mesh on the CPU and reports how far its lowest vertex sits below the bind floor, and which bone owns it -- the `ground_lift_m` a profile needs |
| `leg_probe <model.glb> <cfg>...` | Glide: thigh and shin angles, the hind foot's height and distance behind the hip |
| `leg_swing_probe <model.glb> <scenario> <cfg>...` | Runs a studio scenario (4 s-turns, 7 brake, 2 turn) through the rig and prints the range of the thigh's fore-aft and lateral angles and the foot's path length: how much the legs MOVE, not where they hang |
| `trace_probe <model.glb> <scenario> <from_s> <to_s> [cfg [metres_per_unit]]` | Runs a studio scenario the way the app does (its flight state, scripted attacks and aim) and writes a CSV of the body-frame paths of the root, chest, neck, head, tail, right legs and right wing tip, one row a frame. The model's clips play as in the app (`DRAGON_FLIGHT_CLIPS`, `DRAGON_BODY_CLIPS`; `none` for the procedural motion), on its own beat. `plot_trace.py trace.csv out.png [--from S --to S --view side\|front\|top --joints a,b]` draws them as animators read motion: an onion skin with one dot a frame, heights over time, and speed per frame. Even dots are linear, bunched ones an ease, a gap or a speed spike a pop, flat holds stiffness, and peaks on the same frame parts moving in lockstep |
| `beat_probe <model.glb> <scenario> [cfg [metres_per_unit]]` | Runs a studio scenario and prints, over the last two beats, the wing tip's elevation and how far the chest, the head and the tail tip move, in percent of the body length: how much of the animal answers the beat, and whether a glide moves at all |
| `wing_clash_probe <model.glb> <scenario> <time_s> [cfg]` | Runs a studio scenario to a time, skins the mesh on the CPU and prints the share of the wing's vertices inside the body, the deepest, and which body joints they pass into. Written for the dive tuck (`docs/ANIMATION.md`, "The dive's folded wing went through the body"); `test_dive_wings_clear_the_body` is the same measure |
| `rib_centre_probe <model.glb> [--clip file.glb \| --no-clip] [--climb] [cfg]` | Runs the studio's flap for two beats with the model's own flight profile and its keyframed wingbeat if it has one, skins the mesh on the CPU and prints, for each rib of each wing, how near its skin past the first bone comes to the body's centre plane, and at what phase. Negative is across it: wings crossing over the back, which a render from behind shows only on the frame it happens. Then the other end of the stroke: the wing's highest skin above the back, its lowest from the belly, and how near the centre plane it comes while more than 0.5 m below the belly (a wing wrapped under the body, feedback 0035). The belly is the trunk's lowest point, so a trunk that flies nose up reads low |
| `jaw_probe <model.glb>...` | Flight and ground: does the jaw tip drop in the head frame with the breath held |
| `render_candidate.py <species> <name> < overrides.cfg` | Symlinks the asset into a scratch dir, appends the overrides to a copy of its rig profile, renders side/front/rear/top with `--studio 9` and stitches them (`RIG_PROBE_OUT` sets the scratch dir) |

Each `.cfg` is rig-profile syntax; the loader takes the last occurrence of a
key, so a candidate is the species profile plus overrides.

Build, from the repo root, against the same sources as `test_anim`:

```sh
SRCS=($(sed -n '/^add_executable(test_anim/,/^)/p' CMakeLists.txt | grep -o 'src/[a-z_/]*\.cpp' | sort -u))
INC=($(find build/_deps -maxdepth 3 -type d \( -name cgltf-src -o -name stb-src \) | sed 's/^/-I/'))
clang++ -std=c++20 -O2 -Isrc ${INC[@]} -I/opt/homebrew/include -DASSET_ROOT='"assets"' \
    tools/rig_probes/sink_probe.cpp ${SRCS[@]} -o /tmp/sink_probe -L/opt/homebrew/lib -lSDL3
```

(zsh: the array syntax above; in bash drop the `${...[@]}` parens.)
