# Brief: rig the hero dragon

You work in a git worktree of a C++ game engine (a dragon flight game). Your current directory is that worktree. You fit a deform skeleton to one static dragon mesh in headless Blender, weight it, and export a rigged glTF that the engine loads. The engine animates the bones itself, so the bone names and axes are a contract.

## Read first

- `tools/hero-build.md`: how the mesh was built. Its section "For the rigging step" has the jaw hinge, the eye centres, the lip line and the joint loop positions in the file's coordinates. Its positions were read by eye; expect 50 to 100 mm of error and measure again on the mesh.
- `docs/MODEL_GENERATION.md`: "What the engine needs from an asset", "Auto-rigging", "The pipeline".
- `docs/ANIMATION.md`: the sections on glTF import and on the frame and scale traps.
- `tools/rig_embercrest_candidate.py` and `tools/skeletons/winged-quadruped.json`: the rigger and the skeleton that the current Embercrest uses. `CLAUDE.md` says a skeleton file is per set of measurements, so write a new one.
- `src/anim/dragon_rig.cpp` and `src/anim/gltf_loader.cpp`, read only: how joints are found by name.

## Input

`assets/embercrest-hero-static.glb` (a symlink; never modify it). Six meshes: `hide`, `membrane`, `horn`, `eye_l`, `eye_r`, `mouth`. 19 m wingspan, forward -Z, +Y up, feet at Y=0, wings spread level. `hide` and `mouth` carry a `_JAW` vertex attribute that marks the lower jaw.

## Deliver

1. `tools/skeletons/embercrest-hero.json`: the skeleton, measured on this mesh. Keep every bone name the rigger and the engine key on. Add: `jaw` hinged at the measured hinge, `eye_l` and `eye_r` at the eyeball centres, `eyelid_upper_l/r` and `eyelid_lower_l/r` for a blink, and `tongue_01..03`. Check each new name against the skip list in `gltf_loader.cpp` (`_end`, `ik`, `pole`, `cont`, `target`, `chain`, `roll`) and against the substring matches, so a new bone is not taken for a neck, tail or wing bone.
2. `assets/embercrest-hero.glb`: the six meshes skinned to one armature, materials and maps unchanged, at most 4 influences a vertex, at most 256 joints, triangles only, tangents kept. Git ignores `assets/*.glb`; leave the file in `assets/` and do not force-add it.
   - `eye_l`/`eye_r` meshes: weight 1 to their eye bone.
   - `mouth`: lower gums, lower teeth and the tongue follow `jaw` and the tongue bones; the upper parts follow `head`.
   - `hide` jaw: use `_JAW` as the mask, with a soft falloff at the corner of the mouth so the cheek stretches and does not tear.
   - Lids: the six rings of 36 vertices round each eye follow the eyelid bones so the lids can close over the ball.
   - `membrane`: the continuous wing field the rigger already builds. The wing must fold without the membrane poking through the arm.
   - `horn`: horns follow `head`; each claw follows its toe.
   - The frill spikes behind the jaw are thin and dense. Weight each spike rigidly to the bone under its root (head or a neck bone), so a spike never bends.
3. `assets/embercrest-hero.glb.rig.cfg`, `.flight.cfg`: copy `assets/embercrest.glb.rig.cfg` and `.flight.cfg` unchanged. Tuning them is not your task.
4. `tools/rig_hero.py`: rebuilds the rigged file with `blender -b --factory-startup --python tools/rig_hero.py`, and has `-- --verify-only` and `-- --render`. Reuse `rig_embercrest_candidate.py` by import or by copying what you need; do not change that file.
5. Verification, failing loudly: bone count, every contract bone present, influences, no unweighted vertex, left and right weights mirror within 2%, and these pose tests measured in Blender: jaw open 35 degrees (the lower teeth clear the upper lip, no hide vertex moves that has `_JAW` 0 and lies more than 0.4 m from the hinge), a blink (the lids meet within 5 mm, the ball does not show through), eyes turned 25 degrees (no lid vertex moves), a wing folded 80 degrees at the elbow and wrist (membrane vertices stay within 0.05 m of the plane through their finger bones), each knee and elbow bent 60 degrees (volume of the limb changes less than 15%), the neck bent 30 degrees (no frill spike changes length by more than 1%).
6. `artifacts/hero-rig/`, 1920x1080: `bones.png` (the skeleton over the mesh, side and top), `jaw.png`, `blink.png`, `wingfold.png`, `crouch.png` (all four legs bent), `neck.png`, `weights-head.png` (the jaw and lid weights as colours).
7. `tools/hero-rig.md`: the record. The measured landmarks, every decision, the verification output, what is weak. Short plain sentences, active voice, no filler. Write this record fully; it is the part most often skipped.
8. If the project builds (`cmake --build build`; the build directory may be absent in a worktree, in which case skip this), do not run the game. Engine checks are the main session's.

## Rules

- Write only: `tools/skeletons/embercrest-hero.json`, `tools/rig_hero.py`, `tools/hero-rig.md`, `artifacts/hero-rig/`, `assets/embercrest-hero.glb`, `assets/embercrest-hero.glb.rig.cfg`, `assets/embercrest-hero.glb.flight.cfg`, `build-hero-rig-cache/` (add it to `.gitignore`). Never write outside this worktree. Do not touch engine source, other assets or other docs.
- Use `/opt/homebrew/bin/blender` headless only. No Blender MCP, no running Blender.
- Do not wait on standard input. Do not ask questions; decide and record.
- Budget: three passes of build, verify, render, look. Commit after each pass that verifies, staging explicit paths, so a stop loses nothing.
- Finish with a short report: what you built, the verification output, what is weak.
