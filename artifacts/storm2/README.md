# Storm2

## Measurements

I compared the storm turnaround with the source previews. The side view shows four wing rays, including the leading edge.
The overhead view shows those rays nearly on top of each other. The palm faces sideways in the source.
I measured each tip in side view and sampled its X position on the mesh.
The highest hook ends at (0.453, -0.214, 0.413). The longest ray ends at (0.555, -0.295, 0.326).
The inner hook reaches Z 0.128, well below the other tips.

I traced the legs in the side and underside views. The hind hock lies near Y 0.001 and the forepaw ends near 0.295.
At |X| 0.055 the wing separates from the legs and fluke. The membrane weights follow connected wing edges beyond this cut.
The mouth opens at Y 0.386. There the mandible ends at Z 0.358 and the palate begins at 0.364.
I assigned each exposed tooth to its connected jaw surface.

The fluke has three lobes around the last tail joint. It reaches Z 0.204 above and 0.072 below, with side lobes reaching |X| 0.081.
I traced its center at Z 0.143. The final tail bone carries the whole fan beyond Y -0.536.
Its weight blends into the preceding bone between Y -0.536 and -0.490.

[Measured joints](landmarks.jpg)

## Rebuild

Storm2 uses Stone2's jaw selection and Frost2's wing selection.
Its rigger adds fluke weights and rolls the upright hand around its longest rib.
The first rig left a pale stripe across the middle panel when the hand folded. Disabling shadows left the stripe visible.
I projected both membrane faces onto the same plane and weighted each point between the two surrounding ribs.
The stripe disappeared in the same standing view.

```sh
/opt/homebrew/bin/blender -b --factory-startup --python-exit-code 1 \
  --python tools/storm2_rig.py -- --stem storm2 --keep-uvs --target 80000 \
  --input assets/storm2-cand-oneshot.glb --skeleton tools/skeletons/storm2.json
build-rig-cache/venv/bin/python tools/repair_model_materials.py assets/storm2-raw.glb \
  -o assets/storm2.glb --bake-size 1024 --rough-keratin 0.37
```

## Engine inspection

[Engine views](views.jpg) · [Hero at the same glide camera](../frost2/hero.jpg)

The bind pose lowers the shoulder 36 degrees, rolls the hand 67 degrees and spreads it 37 degrees.
The engine fits the bind width to 19 m. The resulting body length is 13.34 m.
The hero measures 14.24 m by the same method. Storm2 reaches 93.7% of that length.

I used Frostvein's complete rig and flight profiles and Stormsail's storm breath.
The first glide left the paws low. I increased the hind leg trail to 34 degrees and the foreleg trail to 30 degrees.
The standing forearm rises 30 degrees. The leading ray rises 4 degrees and each following ray slopes another 8 degrees.
Full wing aim pressed the pale panels together. I blended 90% of the standing aim to keep more space between the ribs.
The dive uses 48 degrees of sweep, 34 degrees of fold and 4 degrees of droop.
The ground offset is 2.781 m. I reduced the standing tail lift to 3 degrees and limited its flight swing to 51 degrees.

The material bake found 27.0% membrane, 8.8% keratin, 10.1% belly and 0.2% mouth.
The pale wing panels cover the largest thin surfaces. The keratin group includes the horns, wing hooks and claws.
The repair gives thin, flat tail surfaces half the membrane weight of a wing. This gives the fluke a partial membrane finish.

[Standing feet and fluke with grass hidden](feet-raw.jpg)

The jaw rests at -23 degrees. The attack adds 28 degrees and exposes the tongue beneath the upper teeth.
The engine mapped every chain without a joint warning. It counts the leading edge and three fingers on each wing.

```text
mapped rig: neck 3, tail 8, wing root 3/3, fingers 4/4, legs 3/3, front legs 3/3, feet 4, jaw jaw
```

The side glide still puts the near wing hook across part of the muzzle. The head camera shows the jaw clearly.
The paired fluke lobes appear narrow from overhead. The chase, landing and dive views show their surfaces.
