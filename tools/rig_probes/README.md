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
