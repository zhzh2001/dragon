# Blight2

## Fit

I measured the source with `tools/frostvein_measure.py`. The coordinates use +Y forward and +Z up.
The projections in `landmarks.jpg` show the joint locations against the source mesh.

The right wing membrane tilts 61.2° from horizontal. Its wrist sits at (0.170, 0.055, 0.358).
Four finger chains follow the leading edge and three more tips. I projected both wing surfaces onto the measured membrane plane.
Each panel follows the two ribs beside it. The shoulder and wrist rotate separately to unfold the raised wing.

The front wrists sit at Y 0.111. The hind hocks sit at Y −0.113. Four toe bones follow each paw.
Eight tail bones follow the rings. The last bone carries the hooked stinger.
The centerline mouth gap runs from Z 0.347 to 0.363 at Y 0.264. The jaw split follows this gap toward the snout.
`measurements.json` contains the wing fits and the centerline mouth measurements.

## Scale and profiles

The engine fits the wingspan to 19 m. I measured body length along the forward axis of the bind pose.
The hero measures 14.24 m. Compare `bind.jpg` with [the hero at the same camera setting](../tide2/hero-bind.jpg).

Blightmaw supplied all three profiles. I kept the blight breath and set sole clearance from the exported mesh.
The sculpt already stands on four feet, so I removed the sibling's crouch and lift.
I reduced flight leg tuck to 12° and leg trail to 6°.
I set dive sweep to 40°, fold to 28° and finger convergence to 25°.

Blight2 measures 13.51 m, or 94.9% of the hero's length. Its standing soles reach the bind floor with 0.00 m penetration.
The dive probe found zero wing vertices inside the body at five seconds.

I set the standing neck pitch to 8° and lowered the folded hand to −12°. The hand fans the fingers by 3°.
I reduced neck streamlining to 12° so the head faces forward in flight.

## Jaw

The source joins the upper and lower front fangs. The first rig stretched those connections when the mouth opened.
The second rig removes the narrow bridges before decimation and closes the exposed rings into separate tips.
Each tooth follows its own jaw. The weights blend at the hinge.
I set jaw rest to −16° and opening travel to 32°. The close-up shows slightly blunt fang tips.

## Rebuild

Run these commands from the worktree root with the source GLB in place.

```sh
/opt/homebrew/bin/blender -b --factory-startup --python-exit-code 1 --python tools/blight2_rig.py
build-rig-cache/venv/bin/python tools/repair_model_materials.py assets/blight2-raw.glb -o assets/blight2.glb --rough-membrane .75
```

The material bake assigned 21.9% to membrane, 7.8% to keratin, 11.7% to belly and 0.3% to mouth.
The wings have amber skin between dark ribs. The horns, claws and stinger account for the hard tips.
I used roughness 0.75 on the membrane and 0.32 on the keratin.

## Engine inspection

I inspected all thirteen frames at 1920 × 1080. Both wingbeat frames show continuous membranes.
The standing wings form a tall pleat at each shoulder. The tail keeps its hooked tip in the dive.
The hero-to-Blight2 model switch also loaded the measured rig.

## Engine load

```text
mapped rig: neck 3, tail 8, wing root 3/3, fingers 4/4, legs 3/3, front legs 3/3, feet 4, jaw jaw
```

`blight2-raw.glb` SHA-256 `984e58df83ae667426facc59ae153b4407a23e934d9f1a3d77f229c6f187d411`.

`blight2.glb` SHA-256 `543302dc289d75697bae8cd504dcf60d7baeeb9d23dcb8025780ecd34d132703`.
