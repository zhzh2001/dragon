# Brief: version 6 of the hero dragon, broad wings

You work in a git worktree of a C++ game engine (a dragon flight game). Your current directory is that worktree. The task is Blender work, headless, on the hero dragon's mesh and its rig.

## The fault

The playtester on the hero's wings: "they do look too thin". Seen from above in the glide, each wing is a long narrow blade: about 8 m from the body to the tip and 3 m to 4 m from the leading edge to the trailing edge. The trailing edge leaves the body at the shoulder, so the flank behind the wing is bare. A blind critic said the same in seven rounds: "flat, opaque boards with straight rod bones", "bone rods with at most a 20 px strip of membrane" from the front.

Look first at `artifacts/hero-build/v6-fault.jpg` (the current wings from above, the front, the rear and close). The target is the classic dragon wing, built like a bat's: a broad hand of long fingers swept back, a membrane that runs from the last finger to the flank and attaches along the body back to the hip, and a trailing edge that hangs in a scallop between each pair of finger tips.

## Read first

- `tools/hero-build.md`, sections "Version 5c", "Version 5b", "Version 5", "Commands", "File contract" and "For the rigging step". The record of the mesh and its staged script `tools/build_hero.py`.
- `tools/hero-rig.md`, sections "Files and commands", "Bone contract", "Weights: Wings", "Checks that changed". The rig script is `tools/rig_hero.py`, and the skeleton is `tools/skeletons/embercrest-hero.json`.
- `docs/showcase/HERO.md`, sections "Mesh version 5b", "A membrane that billows" and "Fingers that bend". The engine bows each membrane panel in the vertex shader and bends the fingers. Both read the bind pose, so both follow a new mesh.
- `docs/showcase/CRITIC.md`, what rounds 4 to 7 say about the wings.

## What to build

1. Chord. At rest in the bind pose, measured in the wing's own plane:
   - The membrane attaches to the body from the shoulder back to the front of the hind hip, along the flank. Today it ends near the shoulder.
   - The trailing edge at the body is at least 4.5 m behind the leading edge. Today it is about 2.1 m.
   - At the wrist the chord (leading edge to the trailing edge, straight back) is at least 5 m. Today it is about 2.3 m.
   - The wingspan stays 19 m, within 0.3 m. The arm (root, elbow, wrist) does not move.
   - The wing's area, one side, grows by at least 60%. Measure and record both numbers.
2. Fingers. Lengthen and sweep back fingers 2 and 3 and the elbow rib (`wing_spur_a`, `wing_spur_b`) so they carry the new area. Finger 1 stays the leading edge. The finger tips spread evenly along the trailing edge. A finger is thickest at the wrist (about what it is now) and tapers to a third of that at its tip. Each finger has a gentle curve down toward its tip, not a straight rod. You may add a fourth finger only if the bone contract holds (see "The rig").
3. Trailing edge. Between each pair of neighbouring tips, and between the last tip and the hip, the edge is a concave arc. Its depth is 10% to 15% of the distance between the two tips. No straight segment longer than 1 m.
4. Slack. A panel between two fingers sags 3% to 5% of its width at rest, downward. The engine adds more under load, so do not model more than that.
5. Thickness. Where the membrane meets a finger or the arm it thickens into the bone over 60 mm to 100 mm, a fillet, so a bone does not read as a rod laid on a board. Keep the membrane a shell with two faces, as version 5b has it. Its thickness away from the bones stays as it is.
6. Surface. The new membrane area carries veins and wrinkles at the same texel density as the outer panels. No panel with a smoothed plain colour. The upper face stays darker and more saturated than the under face. Regenerate `assets/embercrest-hero-membrane-thickness.png` for the new layout if the script writes it.
7. The body, head, legs, tail and their maps do not change.

Choose how. Extending the present wing sheet by construction (as version 5b built the root panel) is the likely route: the sculpt `assets/embercrest-cand-hero-wing2-hunyuan.glb` has the narrow shape, and it is the source of the membrane's surface detail, not of the new outline. Record the choice.

## The rig

- Rebuild the rigged file with `tools/rig_hero.py`. Move the finger and spur bones in `tools/skeletons/embercrest-hero.json` to lie inside the new fingers, both sides mirrored.
- The bone contract holds: every existing bone name stays, and the engine's log line must still read `mapped rig: neck 3, tail 8, wing root 3/3, fingers 3/3, legs 3/3, front legs 3/3, feet 4, jaw jaw`. If you add a fourth finger, the line must read `fingers 4/4` and you must say so in the report. The engine may not fold a fourth finger well; prefer three longer fingers and a longer rib.
- The mesh's checks (86) and the rig's checks (56) must pass. A check that fails only because the wing is meant to be larger (an area, a landmark, a bound) may change; record each one with the old and the new number. The fold check is not one of those: the longest folded membrane edge stays at most 1.6 of rest.
- The flank attachment needs weights: membrane vertices on the flank follow the body's bones (spine, hip), and the sheet between the flank and the rib must not tear or pass through the hind leg when the wing folds and when the hind leg trails back (hip 45 degrees, the flight posture).

## Engine check, required

The main checkout at `~/src/game-claude` has a built engine. Run it read-only on your rigged file, from the main checkout's directory, and write screenshots into your worktree. One engine process at a time. Write nothing else in the main checkout.

`cd ~/src/game-claude && ./build/dragon --headless --hide-ui --size 1920x1080 --sun 35,235 --model <worktree>/assets/embercrest-hero.glb <VIEW> --screenshot <worktree>/artifacts/hero-build/v6-NAME.bmp`

Convert each BMP to JPEG and delete the BMP. The rig profile beside your model is read: the worktree's `assets/embercrest-hero.glb.rig.cfg` and `.flight.cfg` are tracked and current.

Views, each beside the same view of the main checkout's `assets/embercrest-hero.glb` (version 5c) as a pair, old on the left:

| NAME | VIEW |
|---|---|
| above | `--studio 0 --inspect 90 20 80 --frames 200` |
| front-beat | `--studio 1 --inspect 20 18 8 --frames 215` |
| rear-beat | `--studio 1 --inspect 160 16 15 --frames 230` |
| side-down | `--studio 1 --inspect 90 16 4 --frames 222` |
| close | `--studio 0 --inspect 120 7 30 --frames 200` |
| chase | `--frames 200` |
| landed | `--input -0.25,0,0,0,0,0 --inspect 60 11 3 --frames 900` |
| landed-rear | `--input -0.25,0,0,0,0,0 --inspect 180 14 6 --frames 900` |
| dive | `--studio 5 --inspect 90 16 6 --frames 300` |

Look at every pair at full size. The folded wing on the ground is the likely fault: longer fingers may stand over the back or pass through the body. The fold's angles are in `assets/embercrest-hero.glb.rig.cfg` (`ground_wing_*`). You may change those values, and `wing_*` fold values for the dive, to make the new wing fold; record each change with its old value. Do not change any other key.

## Budget and stopping

Two passes. Pass 1: outline, fingers, flank attachment, rig, engine pairs. Commit. Pass 2: trailing edge, slack, fillet, surface, the faults pass 1's pairs show. Commit. If the fold cannot be made to pass with the full chord, reduce the chord until it does, deliver that, and record the chord that failed and why. A wing that is 40% larger and folds is worth more than one that is 80% larger and tears.

## Record

- `tools/hero-build.md`: a new first section "Version 6: broad wings (2026-11-21)" in the style of "Version 5c": state, what changed, the measured numbers (chord at the body and the wrist, area, span, triangle counts), how, the checks that changed, what is weak.
- `tools/hero-rig.md`: a section "Mesh version 6 (2026-11-21)" with the moved bones, the verifier's output and the profile changes.
- Write short plain sentences, active voice, no filler.

## Rules

- Write only these paths: `tools/build_hero.py`, `tools/rig_hero.py`, `tools/hero-build.md`, `tools/hero-rig.md`, `tools/skeletons/embercrest-hero.json`, `assets/embercrest-hero.glb`, `assets/embercrest-hero-static.glb`, `assets/embercrest-hero-membrane-thickness.png`, `assets/embercrest-hero.glb.rig.cfg`, `artifacts/hero-build/v6-*`, `artifacts/hero-rig/v6-*`, `build-hero-cache/`. The `.glb` files are ignored by git: leave them in place in the worktree and the main session collects them.
- `build-hero-cache/` in the worktree is a copy of the stage caches. The sculpts in `assets/embercrest-cand-hero-*.glb` are symlinks: never write through them. Never write outside this worktree, except the engine screenshots, which go into the worktree anyway.
- Use the `blender` command line only (`/opt/homebrew/bin/blender`). Do not use a Blender MCP server or a running Blender. Run Blender in the foreground. One Blender process at a time: the machine is shared.
- Do not build the game engine. Do not edit engine source or other docs.
- Do not wait on standard input. Do not ask questions. Decide, and record the decision.
- Stage explicit paths, never `git add -A`. Do not push.
- Finish with a short report: the measured chord, area and span against version 5c, the checks' output, the profile keys changed, what is weak, and the minutes it took.
