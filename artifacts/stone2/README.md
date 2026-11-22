# Stone2

## Measurements

I compared the stone turnaround with the four source previews. I plotted the mesh with +Y forward and +Z up.
The side slice shows the hind knee at Y -0.008 and the hock at -0.094.
The underside view places each ankle and the three toes on its paw.
I followed the center of the tail below the amethyst crystals.

The wingtip at Y -0.473 extends much farther behind the wrist than the two tips at -0.049 and -0.138.
I measured their heights separately on the mesh. The wrist sits at (0.19, 0.119, 0.373).
The short inner spur ends at (0.314, -0.199, 0.180). The first fold left its tip hanging beside the hind knee.
The final skeleton gives the short spur its own chain. The engine counts the leading edge and three fingers per side.
The bind pose lowers the arm 39 degrees and spreads the swept hand 27 degrees.

I cut the source at several distances from its center. At |X| 0.075 the wing still connects to the foreleg.
At |X| 0.085 the wing separates from the leg and shoulder crystals.
The rigger follows connected edges from the outer tips. Only that surface receives the membrane weights.

Vertical rays through the center of the muzzle located the mouth gap.
At Y 0.319 the jaw ends at Z 0.364 and the palate begins at 0.375.
The jaw mask follows the gap through five points.

[Measured joints](landmarks.jpg)

## Rebuild

Stone2 uses Frost2's wing selection. This copy of the rigger welds exact duplicate positions before decimation.
It keeps the texture coordinates on each face corner.

```sh
/opt/homebrew/bin/blender -b --factory-startup --python-exit-code 1 \
  --python tools/stone2_rig.py -- --stem stone2 --keep-uvs --target 80000 \
  --input assets/stone2-cand-oneshot.glb --skeleton tools/skeletons/stone2.json
build-rig-cache/venv/bin/python tools/repair_model_materials.py assets/stone2-raw.glb \
  -o assets/stone2.glb --bake-size 1024 --rough-keratin 0.37 --rough-hide 0.49
```

## Engine inspection

At the engine's 19 m span, Stone2 measures 12.97 m along the body axis.
The hero measures 14.24 m by the same method. Stone2 reaches 91.1% of the hero's length.

I kept Ironroot's full rig, flight and stone breath profiles as the starting point.
Stone2 stands on its source paws. I removed Ironroot's standing hip, knee, ankle, elbow and body rotations.
The new ground offset is 2.912 m. The tail lifts 4 degrees.
The leading rib rests level. Each following rib slopes down another 5 degrees. The dive uses 48 degrees of sweep and 5 degrees of droop.

[Engine views](views.jpg) · [Hero at the same glide camera](../frost2/hero.jpg)

The bake found 16.6% membrane, 5.4% keratin, 12.2% belly and 0.5% mouth.
The keratin category mixes thin crystal edges with horns and claws. I used 0.37 roughness for that group.
The larger obsidian plates share the hide group. I set its roughness to 0.49 so the facets catch the light.

Ironroot's standing angles brought the hind paws forward under the chest. The revised stance separates the forepaws and hind paws.
[Feet on the raw model with grass hidden](feet-raw.jpg)

The first mouth mask stretched the tips of several upper teeth during the attack.
At Y 0.322 the upper teeth still connect to the mandible on the source mesh.
At Y 0.329 the upper and lower surfaces separate. Beyond this cut, each tooth follows its connected jaw surface.
The mask blends toward the hinge behind that cut.

The loader reported all chains without a joint warning.

```text
mapped rig: neck 3, tail 8, wing root 3/3, fingers 4/4, legs 3/3, front legs 3/3, feet 4, jaw jaw
```

The third fold lifted the inner spur clear of the hind leg. The long outer tips still project behind the standing body.

## Flap check

The right shoulder crystal traveled 1.57524 m at XYZ (0.122480, 0.147126, 0.282892).
It carried 83.37% right wing root, 7.63% neck_02, 6.52% right upper arm and 2.48% neck_01.
The left foreleg traveled 0.13015 m. Across the body, 8,975 vertices exceeded 2 cm.

The membrane field already stopped at the connected wing. The automatic bind left wing-root weight outside that surface.
The builder now removes that weight from both sides of the body.
At |X| 0.075, the wing still connects to the foreleg. At |X| 0.080, it separates on both sides.
The chest supports the inner web. Wing weight increases across the first 0.064 model units of the wing.
The builder uses the original shoulder weights once to lower the sculpted wing into its resting pose.
The exported model uses the corrected weights during flight.

Run the check on the final model from this worktree.

```sh
/opt/homebrew/bin/blender -b --factory-startup --python-exit-code 2 --python tools/stone2_flapcheck.py
```

The check finds the measured shoulder rim by its texture coordinates.
It compares every pair of 41 wing-only poses at the engine's 19 m span.

After the third rebuild, all 53,102 body vertices measured 0.00000 m, including the crystals and forelegs.
The raw and final exports kept all 74 joint transforms, 80,000 triangles and all corner texture coordinates.
The raw export kept the three source maps byte for byte.

Both membranes and all four fingers on each wing stayed attached throughout the captured stroke.
The first build pinched the shoulder. The second restored its resting angle. The third spread the bend along the upper wing.
A crease below each shoulder crystal remains visible on the downstroke, especially in rear frames 224 and 232.
The third build met the body motion limit but missed the smooth shoulder target.
The engine mapped every chain without a joint warning. [Compare the six rear poses](flap-weights/before-after.jpg).

Final raw SHA-256 `6956cdf6b995a722101d31429f8f2e7a349aa4cb9137d63aa6f46fee53aa87ab`.
Final material SHA-256 `d456a4b7a925719d4cbb0fc0d48163710ef1cfd7c254f3679e92d8a4d670c4b8`.
