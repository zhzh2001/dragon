# Generated dragon flight pose correction

2026-09-13. Profile-only changes to the six generated dragons. The reported
"ironfoot" is treated as the roster's Ironroot (`ironroot.glb`).

The flight leg driver adds rotations to an already bent sculpt: tuck affects
the hip, knee and ankle, trail adds another hip rotation, and foot hang/toe
curl add distal rotation. Reducing only the hip trail left the paws curled
upward. This pass reduces the combined fold and the paw curl per species.

| Model | Leg tuck, before → after | Hind/front trail, after | Foot hang / toe curl, after |
|---|---|---|---|
| Embercrest textured | 80 → 60 | 15 / 15 | 10 / 8 |
| Blightmaw | 80 → 55 | 15 / 15 | 10 / 8 |
| Rimefang | 65 → 45 | 15 / 15 | 10 / 8 |
| Ironroot | 20 → 15 | 25 / 25 (was 30 / 30) | 10 / 8 |
| Tidewrack | default 62 → 45 | 30 / 30 | 10 / 8 |
| Stormsail | default 62 → 45 | 30 / 30 | 10 / 8 |

Angles are degrees. Previous foot hang/toe curl inherited 30/16. Blightmaw,
Rimefang, Ironroot and Tidewrack now cap wing flap at 34 degrees and use a
14-degree upstroke fan fold (Blightmaw already used 14). This reduces the
raised membrane's crowding around horns and the pinched wrist fold.

The four-axis dive inspection also exposed crossed wing fingers on Tidewrack
and Stormsail. Their sweep/fold is now 40/10 instead of 75/45. The resulting
dive keeps a wider delta and separate fans above the legs.

## Visual evidence

All 19 cases per model were inspected as individual full-size Metal renders:
glide, upstroke and dive from side/front/rear/above; close head views in glide,
flap and dive; grounded feet; actual flight and landing through the game's
input path; and switching from `dragon.glb` onto each species. Only the
renders linked below are kept in the repository (the full set was 91 MB of
PNGs and invocation logs); `python3 tools/capture_flight_pose.py` regenerates
all of them into `final/`. These are deterministic samples, not continuous
human playtesting of every transition.

| Model | Before flight legs | After flight legs | After dive |
|---|---|---|---|
| Embercrest | [side](embercrest-before-side.png) | [side](final/embercrest-glide-side.png) | [above](final/embercrest-dive-top.png) |
| Blightmaw | [side](blightmaw-before-side.png) | [side](final/blightmaw-glide-side.png) | [above](final/blightmaw-dive-top.png) |
| Rimefang | [side](rimefang-before-side.png) | [side](final/rimefang-glide-side.png) | [above](final/rimefang-dive-top.png) |
| Ironroot | [side](ironroot-before-side.png) | [side](final/ironroot-glide-side.png) | [above](final/ironroot-dive-top.png) |
| Tidewrack | [side](tidewrack-before-side.png) | [side](final/tidewrack-glide-side.png) | [above](final/tidewrack-dive-top.png) |
| Stormsail | [side](stormsail-before-side.png) | [side](final/stormsail-glide-side.png) | [above](final/stormsail-dive-top.png) |

The wingbeat before/after the 34-degree cap was compared on `--studio 1`
frames 108 and 132 with the corrected leg settings in both (those pairs were
not kept; the capture script's `upstroke-*` cases show the capped beat). No
mesh or skin weights were rebuilt. Existing subtle
membrane creases, most visible on Blightmaw, remain; this is not a claim that
all generated surface defects have been removed. Ground foliage and course
debug lines in gameplay captures are scene geometry, not skinning artifacts.

## Reproduce and playtest

From the repository root on macOS with a Metal display available:

```sh
cmake --build build
python3 tools/capture_flight_pose.py
ctest --test-dir build --output-on-failure
./build/dragon --models assets/dragon.glb,assets/embercrest.glb,assets/blightmaw.glb,assets/rimefang.glb,assets/ironroot.glb,assets/tidewrack.glb,assets/stormsail.glb
```

Use M to switch, Space to flap, Shift to dive, and free look to inspect from
the side and rear. Check the paw height and wing/head clearance while entering
and leaving a dive, then land. The automated suite passed all 11 tests.
