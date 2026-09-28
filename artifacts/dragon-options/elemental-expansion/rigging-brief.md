# Rigging handoff

Work ONLY in ~/.codex/worktrees/elemental-dragon-rigs/game-claude.
Read CLAUDE.md, docs/ANIMATION.md, docs/MODEL_GENERATION.md. The main session
has checked Codex usage. Do not launch another delegate or modify another
checkout. Keep engine code and production rig/flight/breath profiles for the
main session to author/tune.

Scope: new Sunspear and Rimeplume measured deform rigs. Input candidates are
assets/sunspear-cand-oneshot.glb and assets/rimeplume-cand-oneshot.glb, symlinked
read-only from the main checkout. Never overwrite source meshes. Reference
images live in the main checkout's artifacts/dragon-options/elemental-expansion.

Write only tools/skeletons/sunspear.json, tools/skeletons/rimeplume.json,
tools/sunspear_*.py, tools/rimeplume_*.py, assets/sunspear*.glb,
assets/rimeplume*.glb, assets/sunspear/, assets/rimeplume/,
artifacts/sunspear/, artifacts/rimeplume/, and artifacts/elemental-rigs/. Temporary files belong
under /tmp/elemental-rigs. No shared rigger edits unless a blocking limitation
is reported first. Use tools/rig_embercrest_candidate.py and the existing
measurement scripts as references. Each species needs independently measured
landmarks, jaw mask, membrane/feather field and bone-fit bounds. Do not copy
another species' coordinates. The skeleton name/chain contract is essential:
neck/head/jaw, tail, shoulder-elbow-wrist wings with fan branches, four legs
and feet. <=256 joints, <=4 influences, no unweighted vertices, ~80k tris,
valid UVs and tangents. Export glTF forward -Z, Y up; rigger's internal
canonical coordinates differ, so follow its transforms carefully.

Inspect raw candidate in full-size orthographic renders first. Reject broken
anatomy rather than silently rigging it. Rimeplume needs coherent feather
vanes: avoid turning each vane into a flexible membrane that crumples along
its length. Sunspear must retain triangular planform. Weight mouth without
dragging upper muzzle or feather crest. Measure source hashes and bounds.

Output raw rigged GLBs, repair their materials to plain-name GLBs using the
existing material script where viable (matte feather roughness for Rimeplume),
and record commands, stats, assumptions, full-size inspection images and
recommended initial jaw/wing/stance values in artifacts/elemental-rigs/README.md.
Main session owns engine captures and profile finalization. One measured
implementation pass plus one targeted correction per candidate; preserve
useful work and report remaining defects instead of endless iteration.
Commit scoped tracked skeleton/scripts/report files when ready; assets are
gitignored but must remain available in the worktree. Report exact paths.
