# Showcase: effects

The breath, the projectiles and what else burns, glows or flies off. Part
of the showcase record; `docs/SHOWCASE.md` is the index. The literature
these follow is in `docs/MOTION_REFERENCE.md`. The particle system and
the per-species breath profiles are in `docs/EFFECTS.md`.

## The breath as a volume (2026-11-15, 20:11)

The user on the breath: "still too simplistic", and to look at classic
fire implementations and papers. The breath was a stream of camera-facing
puffs. Puffs sum, and the tonemap scales a bright pixel back to its hue,
so the jet came out as one flat colour with a scalloped edge however the
puffs were shaded. Two attempts to soften them were dropped
(`CRITIC.md`, "After round 3, tried and dropped: a softer flame").

The player's breath is now one body of gas, marched
(`src/gfx/breath_volume.h`, `shaders/breath_volume.hlsl`), on the modern
tier.

- The spine. Up to 14 nodes are let go at the mouth, one every eleventh of
  the breath's life. Each moves as a puff did: the launch speed is solved
  against the drag so the jet's length is the damage range, and the
  buoyancy lifts it. The mouth itself is the youngest node, so the jet
  stays on the jaws and curves behind them in a turn. When the breath
  stops, what was breathed travels on and dies.
- The march. One fullscreen pass over the scene, in the pause the clouds
  already take. A pixel's ray steps by its distance to the spine's
  capsules while outside them, and by 0.22 of the local radius inside. A
  sample's density is a round profile about the spine, moved and holed by
  noise that scrolls along the jet, more toward the tip (Fuller et al.,
  I3D 2007). Eddies are sized to the jet's radius where they are.
- The light. Steps are weighted as emission and absorption weight them,
  and what is summed is the gas's heat. The pixel's colour is the mean
  heat read through a ramp: a pale core at the jaws, the profile's hot
  colour, its cool one at the rim, darker toward the tip.

Four things were wrong on the way, each found on a render:

1. The jet was a lance two degrees across. The radius came from the puff's
   size alone, and the puffs' width was mostly their scatter. The radius
   is now a cone to `size_end + range x spread x 0.6`, 14.8 m for the
   hero's fire at 155 m.
2. The flame was not there from the chase camera. A ray beside the jet
   nears it by a fraction of its distance a step, and those steps were
   counted against the march's own. They are counted apart now.
3. The puffs are launched into still air. From a dragon at 40 m/s a jet
   launched at 259 m/s and slowed by drag is never more than a few metres
   ahead of it for long, and the dragon flies through its own flame. A
   node of the volume keeps the dragon's velocity for life, so the jet
   reaches its range ahead of the dragon, as the damage cone does. The
   puffs were not changed.
4. Summing each step's own colour gave the right hues and no shapes. Every
   boundary in the gas was averaged along the ray, and the jet was an
   airbrushed glow with less in it than the puffs had. The ramp on the
   mean heat puts an edge where the mean crosses a step, and the cover is
   read through a step too, so the tongues and the silhouette are lines.

Cost, paired on the frozen frame at 1920x1080 (`DRAGON_ABLATE_ALTERNATE=
breath_volume`): 3.7 ms from the chase camera and 1.9 ms from the side as
first built, 1.1 ms and 0.8 ms after the cuts below. Those runs were not
recorded. The recorded run of the committed build (`~/src/gpu-hist`,
`research/dragon-m5/runs/20261116-0116-mac-m5-dragon-breath-volume-cost`)
was made on a busy Mac and is noisier: 0.37 and 1.51 ms from the chase
camera, 0.95 and 1.22 ms from the side. What cut it: no noise read where no
gas can be, four octaves read once in place of eight reads, and five
spine segments tried a step in place of fifteen. The window of five is
centred on the capsule nearest in metres; centred on the nearest in
radii it stepped over the thin end of the jet from far off.

The pairs and views are in `artifacts/breath-volume/`: `before-after.jpg`
(the puffs and the volume from the side, close and wide), `views.jpg`
(close, the whole jet, the chase camera, and a turn).

`--breath key=value,...` sets the dials for a run (`on=0` is the puffs),
and the Playtest panel has a "Breath" header.

Open:

- The ramp's steps make the flame a little drawn: three flat bands. More
  variation inside the bands, sparks thrown off the jet and heat shimmer
  (`MOTION_REFERENCE.md`) are the next steps.
- Only the player's breath is a volume. A rival's breath is still puffs,
  and so is every breath on a retro tier. The cones the combat code hands
  over carry no stable identity to hang a spine on.
- Every element uses the same march with its profile's colours. Only fire
  was looked at.
- The jet does not meet the ground. The depth buffer cuts it; nothing
  splashes or burns where it lands.
- Not played. The stills show the shape and not the motion.
