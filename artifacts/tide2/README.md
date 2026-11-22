# Tide2

## Fit

The side and overhead projections in `landmarks.jpg` show the joints against the source vertices. Coordinates use +Y forward and +Z up.
I measured the source with `tools/frostvein_measure.py`. The fitted bounds match the source on all three axes.

I placed the hind hocks at Y −0.088 and the front wrists at Y 0.162.
Four toe bones follow the tips of each webbed foot. Eight tail bones follow the midline into the fin.
The shoulder sits at (0.046, 0.157, 0.246). The right wing surface tilts 7.6°.
I leveled the shoulder by 4°. Seven chains per wing follow the forward spur and six other tips visible from above.

Vertical rays through the mouth found a gap from Z 0.332 to 0.346 at Y 0.342.
The jaw mask follows seven samples along this gap. It assigns the upper muzzle to the skull.
`measurements.json` contains both wing fits and the centerline mouth crossings.

## Scale

The renderer fits each dragon to a 19 m wingspan. I measured nose-to-tail length along the forward axis in the bind pose.
The hero measures 14.24 m. See `hero-bind.jpg` and `bind.jpg` for the same side camera.

Tide2 measures 20.63 m, or 1.449 times the hero's length. The requested range ends at 14.24 m.
Uniformly shrinking the mesh also increases the engine's scale by the inverse amount. The body therefore keeps the same length.
I kept the source proportions and recorded the size check as failed.

## Flight and landing

Frostvein supplied the flight and rig settings. Tidewrack supplied the tide breath.
I reduced dive sweep to 38° and fold to 28°. The tail fin stays open in the dive.
I set jaw rest to −19° and opening travel to 30°. The lower jaw rotates while the muzzle holds its shape.
I raised the folded hand to 6° and spaced adjacent fingers by 3.5°. The standing probe measured 0.00 m of sole penetration.

The shared distance-based weights pinched the first membrane panel above the wrist.
`tools/tide2_rig.py` blends the panel across its two adjacent ribs. Both membrane surfaces follow the same fold.
The revised wrist still has a broad pleat beside the shoulder. The side camera crops part of the nearest wing.

## Rebuild

Run these commands from the worktree root with the measured skeleton and the source GLB in place.

```sh
/opt/homebrew/bin/blender -b --factory-startup --python-exit-code 1 --python tools/tide2_rig.py
build-rig-cache/venv/bin/python tools/repair_model_materials.py assets/tide2-raw.glb -o assets/tide2.glb --rough-keratin .45 --rough-membrane .63
```

The material bake assigned 29.6% to membrane, 7.1% to keratin, 9.6% to belly and 0.2% to mouth.
The keratin mask also catches thin fin rays. I set their roughness to 0.45 and membrane roughness to 0.63.
The repair kept the original base color and all skin data.

## Engine load

```text
mapped rig: neck 3, tail 8, wing root 3/3, fingers 7/7, legs 3/3, front legs 3/3, feet 4, jaw jaw
```

`tide2-raw.glb` SHA-256 `4bddb6395bfae675f5d8b59d14cffbde92ae85bf236888a0302553b02dc47950`.

`tide2.glb` SHA-256 `1fe4a0923b82b916f3e1d5df52b9e8f688160774aac8fda8957f0285f3fee93f`.

The final front landing view shows thin strips stretching from two cheek fins toward the folded wing.
The wing mask overlaps the outer cheek tips at their height. The third build reproduced the same joint weights.
I stopped at the three-build limit. The cheek fins and body length still fail inspection.
