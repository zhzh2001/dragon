# Brake and dive leg refinement

The passive hip pendulum used to add its full swing to a deliberate braking
reach, and gravity pulled dive legs toward a vertical hang. Active brake/tuck
now reduces the spring target to 20% of passive sway. The spring itself keeps
running, so releasing a posture preserves its momentum and returns to glide.
Frostvein adds 25 degrees of rearward hip trail only during tuck; its relaxed
glide remains unchanged.

The studio brake scenario also had its acceleration phase reversed: it flared
while gaining speed. Its brake signal now follows the decreasing-speed half
of the cycle. Consequently old/new brake screenshots at the same frame are
not an isolated leg comparison. Dive comparisons use the same scenario state.

Both controls are editable under Dragon / Flight response and saved in rig
profiles: `leg_posture_sway` (default 0.2) and `leg_dive_trail_deg` (default 0).

## Evidence

Frostvein dive: [before](evidence/frostvein-dive-before.png),
[after](evidence/frostvein-dive-side.png). Look at the forelegs trailing behind
the chest rather than hanging vertically below it.

| Model | Brake | Dive |
|---|---|---|
| dragon | [side](evidence/dragon-brake-side.png) | [side](evidence/dragon-dive-side.png) |
| embercrest | [side](evidence/embercrest-brake-side.png) | [side](evidence/embercrest-dive-side.png) |
| rimefang | [side](evidence/rimefang-brake-side.png) | [side](evidence/rimefang-dive-side.png) |
| frostvein | [side](evidence/frostvein-brake-side.png) | [side](evidence/frostvein-dive-side.png) |
| blightmaw | [side](evidence/blightmaw-brake-side.png) | [side](evidence/blightmaw-dive-side.png) |
| ironroot | [side](evidence/ironroot-brake-side.png) | [side](evidence/ironroot-dive-side.png) |
| stormsail | [side](evidence/stormsail-brake-side.png) | [side](evidence/stormsail-dive-side.png) |
| tidewrack | [side](evidence/tidewrack-brake-side.png) | [side](evidence/tidewrack-dive-side.png) |
| alt-prowler | [side](evidence/alt-prowler-brake-side.png) | [side](evidence/alt-prowler-dive-side.png) |

Frostvein [late tuck](evidence/frostvein-dive-phase-270.png),
[release](evidence/frostvein-dive-phase-300.png),
[recovery](evidence/frostvein-dive-phase-330.png),
[game brake](evidence/frostvein-game-brake.png),
[game dive](evidence/frostvein-game-dive.png), and
[switch from dragon](evidence/frostvein-switch-brake.png).

## Validation

- Build succeeded; all 15 CTest suites passed (18.80 seconds).
- Regression checks cover braced versus passive hip response and settling back
  to glide on the generated rig and all nine imported models, profile save/load,
  and studio brake matching deceleration.
- Inspected full-size side/front/rear/top brake and dive renders on all nine
  imports, close heads in both scenarios and glide, ground-level feet, gameplay
  brake/dive/landing, and switching from dragon during brake.
- Also inspected dragon brake phases and Frostvein brake/release and dive/release
  sequences. Other captured transition sequences remain available for review.
- Prowler is very dark in these views, limiting surface inspection; silhouette
  is readable. Vegetation partly obscures some grounded feet, even with a
  second gameplay camera angle. Existing folded-wing creases remain.

Reproduce all renders (two headless processes maximum):

```sh
python3 tools/capture_flight_legs.py
```

The complete captures and command logs are local in ignored `runtime/`; selected
full-size evidence above is retained with this report. `--models frostvein`
limits the roster; `--labels game-brake game-dive` limits the cases.

## Human playtest milestone

Launch `./build/dragon --models assets/dragon.glb,assets/frostvein.glb,assets/rimefang.glb`.
Hold Ctrl to brake, release into glide, then hold Shift to dive and release again.
Orbit with right-drag and use M to compare species. Look for excessive forward
leg swing under braking, dangling limbs during tuck, and a kick or snap when
releasing either control. Try short taps and sustained holds. The new sliders
allow adjustment of bracing strength and dive trail while playing.
