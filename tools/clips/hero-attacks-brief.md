# Brief: attack clips for the hero dragon

You author keyframed attack clips for one dragon in Blender, by script, and check them by script.
Work only in this worktree. Write only these paths: `tools/clips/make_hero_attacks.py`,
`tools/clips/check_hero_attacks.py`, `tools/clips/hero-attacks.md`, `assets/clips/embercrest-hero.attacks.glb`,
`artifacts/hero-clips/`. Do not edit `src/`. Commit on this branch with explicit paths when the checks pass.

## Why

The playtester said the attacks "read lazy and mechanical". The procedural rig moves the neck, a foreleg or the
tail on one smooth envelope. A strike needs anticipation, a fast hit, overshoot and a slower recovery, with the
whole body in it. The clips supply that. The engine adds each clip's rotations, as deltas from rest, on top of
its finished flight pose, at full weight. So the clip IS the visible motion, and its size must be large.

## Read first

- `tools/clips/make_brake_bank.py` and `tools/clips/check_clip.py`: the earlier clip, the same file format.
  Follow its export settings and its rest-pose handling exactly. `docs/ANIMATION.md`, "A keyframed overlay",
  records what the engine reads.
- `tools/skeletons/embercrest-hero.json` and `tools/hero-rig.md`: the hero's 77 joints and their names.
- `assets/embercrest-hero.glb` (symlink): the rigged model. Its armature's rest pose is the clip's rest pose.
  The model faces +Z in glTF space, Y up, about 19 m long. Left is the model's own left.

## Output

`assets/clips/embercrest-hero.attacks.glb`: the armature alone (no mesh), every joint present with the model's
exact rest pose and names, 30 fps, rotation keys only (no translation, no scale), five animations:

| name | length | timing |
|---|---|---|
| `bite` | 0.90 s | wind-up 0 to 0.16 s, strike 0.16 to 0.26 s, hold to 0.36 s, recover to 0.90 s |
| `claw_r`, `claw_l` | 0.90 s | wind-up 0 to 0.18 s, strike 0.18 to 0.30 s, follow-through to 0.42 s, recover to 0.90 s |
| `tail_r`, `tail_l` | 1.00 s | wind-up 0 to 0.22 s, whip 0.22 to 0.38 s, follow-through to 0.55 s, recover to 1.00 s |

Every joint is at its rest rotation on the first and the last frame (the engine blends nothing).
`_l` is the exact mirror of `_r`; build one and mirror it by script.

## The motion

Amplitudes are peak rotations from rest. They are targets to within about 20%.

- `bite`: the neck coils back and up in the wind-up (the chain's joints together 35 to 45 degrees, the head
  lagging), the jaw opens to 40 degrees by the end of the wind-up, the neck then throws the head forward and
  slightly down past rest by 25 to 30 degrees, the jaw snaps shut at the end of the strike and overshoots
  closed by 2 to 3 degrees, the head recoils 8 degrees and settles. The chest and spine pitch 6 to 10 degrees
  into it. Both wing roots sweep back 12 to 18 degrees on the strike and return late. The fore legs reach
  forward 20 to 30 degrees at the shoulder on the strike with the toes spread, then fold back. The tail lifts
  10 to 15 degrees at its root as a counterweight, with a wave down its length that lags the root.
- `claw_r`: the right fore leg draws back and up in the wind-up (shoulder 30 degrees aft, elbow closed 30),
  then rakes forward and down across the body's centre line (shoulder 60 to 70 degrees forward of the wind-up
  pose, elbow opening 50, wrist flicking 40, toes spread then curled). The spine and the neck twist toward the
  strike 12 to 18 degrees, the head leading. The left fore leg braces back 15 degrees. The right wing lifts 15
  degrees at its root and the left dips 10. The tail swings to the opposite side 20 to 25 degrees at its root
  with a lagging wave.
- `tail_r`: the tail coils to the left in the wind-up (the root 25 degrees, each joint adding, the tip lagging),
  then whips to the right: the root leads to 35 degrees right, and each joint down the chain peaks later and
  larger than the one before it, the tip passing 70 degrees of total bend. The hips and the lower spine twist 10
  to 15 degrees with the whip. The neck and head swing the opposite way 12 to 18 degrees for balance. The hind
  leg on the whip's side kicks back 15 degrees.

Use eased curves with overlapping action: a joint further down a chain starts and peaks one to three frames
after its parent. No joint may move linearly between two keys across a strike. The recovery overshoots rest once,
by a tenth of the strike or less.

Find each joint's bend axis by measurement, not by assumption: pose a joint in Blender, read where the child
bone's tail moved in world space, and choose the axis and sign that move it the way this brief says. Record the
axis table in `tools/clips/hero-attacks.md`.

## Checks (`tools/clips/check_hero_attacks.py`, prints PASS or FAIL per line, exits non-zero on a failure)

1. The file loads; five animations with the names and lengths above; 77 joints with the model's names.
2. Rest pose equals the model's rest pose (rotation within 0.01 degrees, translation within 0.1 mm).
3. No translation or scale keys.
4. First and last frame: every joint within 0.05 degrees of rest.
5. `_l` is the mirror of `_r` within 0.5 degrees at every key.
6. Per clip, the peak rotation of the named joints falls inside the brief's ranges, and the peak happens inside
   the brief's time window.
7. Overlap: down the neck (bite) and the tail (tail_r, claw_r), each joint's peak time is not earlier than its
   parent's.
8. No joint turns more than 25 degrees between two consecutive frames (a pop).

## Look at it

Render a strip per clip in Blender with the mesh from `assets/embercrest-hero.glb` skinned to the clip (import
the model, apply the action to its armature): eight frames across the clip, from the side and from above, into
`artifacts/hero-clips/<clip>-side.jpg` and `-top.jpg`. Look at them. If the strike does not read at a glance
in the strip, make it larger and render again. Run Blender in the foreground (`blender -b --python ...`).

## Record

`tools/clips/hero-attacks.md`: what each clip does, the axis table, the measured peaks, the check output, and
anything that does not match this brief. Stay under 110k tokens; if you are near it, commit what passes and say
what is missing.
