# Status and milestone history

What is built, in the order it was built. The plan these came from is
`ROADMAP.md`; where the project goes next is `RETRO.md`.

## Status

- **M1** window, GPU device, reversed-Z depth, offscreen render + blit,
  hot-reloadable MSL pipelines, ImGui, headless screenshots, math + tests.
- **M2** free-fly debug camera, immediate-mode debug line drawing, grid.
- **M3** procedural valley terrain, analytic height queries, sky, height fog,
  directional light, directional shadow map.
- **M4** energy flight model, greybox dragon with shader-driven wing flap, chase
  camera, live telemetry.
- **M4.1** playtest fixes: keyboard/gamepad controls, roll and inversion
  recovery, asymmetric wingbeat, 5 km valley, shared tonemapping.
- **M5** chase camera: terrain-swept spring arm, turn lead, free look, first
  person, three presets.
- **M8** Dragon Rally: checkpoints, timing, splits, ghost replays, three
  generated courses, HUD, autopilot.
- **M8.1** difficulty assists: bank limit, auto-flap, inverted pitch by default.
- **M6** skeleton, GPU skinning, the procedural dragon rig, and a glTF skinned
  loader. The animation system drives an arbitrary imported skeleton, not just
  the generated one.
- **M6.1** the real dragon: glTF skinned load, inertial neck/tail chains, full
  PBR material set, and the file's authored clip layered under the rig.
- **M11** combat core: fire breath, fireballs, boost, health and regeneration,
  practice sentinels that shoot back, and the combat HUD.
- **M12** combat readability: sticky lock-on with aim assist and drop
  compensation, target cycling, threat direction, difficulty presets.
- **M14** bot dragons: a BotPilot state machine (attack / extend / evade)
  steering the same flight model the player flies, honest gunnery, terrain
  doctrine, spawn/respawn through Combat's external hostile slots.
- **M15** the match loop: countdown / fight / results / rematch, first-to-N
  deathmatch with an optional clock, weapons-cold phases, per-tier bot
  survivability (health and regeneration), match HUD.
- **M7 (part)** additive billboard particles (fire, ember trails, impact
  bursts) and fully synthesized audio -- wind that brightens with speed, flame
  roar, wingbeat whooshes, shots, hits and explosions, no sound assets at all.
- **M16 (part)** the world and the ground: exact-surface terrain queries, a
  carved river with a reflective surface, a coarse skirt past the playable
  extent, four tree and three grass kinds placed by slope, altitude and water,
  landing and take-off states, and `heft` as per-model handling.
- **Next:** see `docs/RETRO.md` -- the ranked gameplay/visual list (loadouts,
  thermals, team matches, clouds) and the D3D9-era port study. The port's first
  phase (extracting an RHI from `src/gfx`) is a pure refactor that pays for
  itself even if the port never ships.

## Open questions

- **Ground beyond the fog.** The terrain builds a coarse skirt out to three
  times the playable extent (see `WORLD.md`), and headless captures from
  inside the valley show ground continuing to the horizon. A playtest report
  of "the rolling ground behind the fog doesn't appear" was not reproduced;
  the suspicion is the height fog itself saturating before the skirt is
  reached, rather than missing geometry. Needs the altitude and heading it
  was seen from.
- **Bots all fly the player's model.** One mesh and one rig are loaded, so a
  match cannot mix a dragon and a wyvern. The renderer and rig are per-bot
  already; it is the asset loading that assumes one model.
- **Nothing collides with a tree.** Vegetation is visual only.
