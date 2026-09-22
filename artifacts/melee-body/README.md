# Melee body commitment — 2026-09-22

The old anticipation curve returned to neutral before the strike, so the
motion read as a small isolated movement. This pass connects the loaded pose
to the strike and gives the body a larger role:

- Bite pulls back, then drives the torso forward/down; wings follow the lunge.
- Claw winds away, then rolls, turns and shifts toward the mark. The limb,
  wing brace and tail counter follow at different times.
- Tail coils the torso farther, counter-turns and lets the tail follow through.
- Recovery eases to rest. Switching models cancels pending attack impulses.
- Studio speed now slows the rig itself, and the melee scenarios expose body,
  duration and extremity controls beside the profile save button.

## Try it

From the repository root:

```sh
./build/dragon --models assets/dragon.glb,assets/embercrest.glb,assets/stormsail.glb --studio 10 --studio-speed 0.5 --inspect 90 24 6
```

Open Studio, compare melee/claw/tail scenarios (10/11/12), then return speed to
1. Use M to switch species. Look for a continuous wind-up into the swing,
weight transferring through the torso, and a smooth recovery. Judge the tail
from above and claw from the front. Then use the training room and C to judge
contact feel at normal speed. This is the human playtest milestone; feel is
not certified by the automated checks.

## Evidence

Build passed; all 12 CTest suites passed. `test_anim` has 1008 checks, zero
failures with the local assets installed. New tests exercise nine imported
models, all three attacks and both sides for retained load, root recovery and
attack cancellation on reinitialisation. Existing directional checks still
pass without relaxed thresholds. Missing optional assets skip asset checks.

Full-size 1280x720 Metal captures were inspected for each of the seven species,
default dragon and alternate Prowler: side/front/rear/top strike poses, attack
and glide heads, grounded feet, gameplay flight/landing and switching from the
default. Embercrest's load/release/recovery phase images were also inspected.
Half-speed CLI playback reached the expected claw pose at frame 64 (normal
speed frame 32). These are sampled visual checks, not collision-proof motion
validation. Prowler's dark material and vegetation around grounded feet limit
some inspection detail. No new severe deformation was apparent in the sampled
strike poses; existing membrane creases remain.

`poses/` retains representative body poses for every inspected model,
Embercrest timing samples, and four matching before images. `runtime/` contains
the larger local capture set (ignored). `capture-commands.json` records every
original command; its original output paths predate moving the files under
runtime. Reproduce the final suite with:

```sh
python3 tools/capture_melee.py
```

The `--binary` and `--baseline` options support comparison against another
build. The original baseline binary was copied before editing; it is local
at `/tmp/dragon-melee-before`, not a portable retained dependency.

## Remaining limits

Combat damage/stun/audio still resolve on input, before visual contact; that
existing mismatch may be more visible with the stronger anticipation. Grounded
base clips and stance can override procedural body/limb attacks, so this pass
primarily improves airborne melee. Mesh root movement is visual and does not
alter flight physics, reach or damage. Model-space side mapping and per-model
ground tuning during switching deserve separate integration coverage.

## References consulted

[The Legend of Spyro hands-on](https://www.gamespot.com/articles/the-legend-of-spyro-a-new-beginning-hands-on/1100-6156183/)
describes its aerial/melee combo direction.
[Glyde's developer Steam page](https://store.steampowered.com/app/1821600/Glyde_The_Dragon/)
describes mixing moves in combat. These informed the expressive combat goal;
this is an original procedural pass, not a frame-by-frame reconstruction of
either game's animation.
