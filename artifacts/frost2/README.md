# Frost2

Frost2 uses 68 joints and three fingers on each wing. The mesh has 80,000 triangles.

## Fit

I compared the frost turnaround with the four source previews. I plotted the candidate vertices with +Y forward and +Z up.
The side slice excludes points beyond |X| 0.125. The underside view isolates the feet below Z 0.17.
The joint overlay shows the hip, knee, hock and sole inside each hind leg.

I followed the wing ridges from the raised wrist at (0.19, 0.043, 0.386).
The bind pose lowers the arm 46 degrees and spreads the hand 18 degrees.
Both operations rotate bones and preserve their lengths.

Vertical rays through the center of the muzzle located the mouth gap.
At Y 0.311 the lower jaw ends at Z 0.352 and the palate starts at 0.363.
The five-point jaw mask follows that gap. It assigns the lower teeth to the jaw.

The first build stretched the hind-leg spikes toward the wings during the glide.
At |X| 0.065 the two wings separate from the legs on the mesh surface.
The species rigger follows connected edges from each wingtip beyond that plane. The leg spikes keep their original weights.

[Joint measurements](landmarks.jpg)

## Rebuild

Run the commands from this worktree. Keep the supplied profiles beside both models after rebuilding.

```sh
/opt/homebrew/bin/blender -b --factory-startup --python-exit-code 1 \
  --python tools/frost2_rig.py -- --stem frost2 --keep-uvs --target 80000 \
  --input assets/frost2-cand-oneshot.glb --skeleton tools/skeletons/frost2.json
build-rig-cache/venv/bin/python tools/repair_model_materials.py \
  assets/frost2-raw.glb -o assets/frost2.glb --bake-size 1024 --rough-keratin 0.42
```

## Engine inspection

The loader reported the following joint chains in all poses.

```text
mapped rig: neck 3, tail 8, wing root 3/3, fingers 3/3, legs 3/3, front legs 3/3, feet 4, jaw jaw
```

The engine fits the bind width to 19 m. Frost2 then measures 13.09 m along its body axis.
The hero measures 14.24 m by the same method. Frost2 reaches 91.9% of its length.

I used Frostvein's three profiles. The final leg trail is 19 degrees behind and 15 degrees in front.
I lowered the standing forearm from 40 to 29 degrees and tipped the hand down 5 degrees.
The rest jaw angle is -21 degrees. The attack adds 27 degrees.
The flight offset places the body center 3.114 m above the ground.

[Engine views](views.jpg) · [Hero at the same glide camera](hero.jpg)

The material bake classified 26.5% as membrane, 10.4% as keratin, 10.6% as belly and 0.3% as mouth.
The keratin category includes ice points and claws. I set its roughness to 0.42 to moderate the shine.
The repair kept all nine other binary sections intact.

The final glide keeps the hind spikes on the legs. The inner wing panel shows a small angular highlight.
Grass covers part of the forefeet in the landing views. The final engine inspection used the main checkout after its shaders compiled again.
The two builds and inspection took about 17 minutes.
