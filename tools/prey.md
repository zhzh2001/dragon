# Valley prey

All positions use glTF metres. X points across the body, Y points up and +Z points toward the muzzle.
The origin lies on the ground between the hips and shoulders.
The mouth attachment follows the animated `head` matrix.

| Animal | Tripo source | Width × height × length (m) | Shoulder (m) | Triangles | Bones | Mouth at bind (X, Y, Z) | Mouth in head coordinates (X, Y, Z) |
|---|---|---|---:|---:|---:|---|---|
| [elk](../assets/props/prey_elk.glb) | `tripo-elk-01.glb` | 1.088 × 2.348 × 2.400 | 1.54 | 8,897 | 28 | (0.000, 1.365, 1.500) | (0.000, -0.235, 0.360) |

## Build

Run these commands from the worktree root on the Mac with Blender 5.3.
Set `name` to an animal in the table.

```sh
name=elk
mkdir -p build-prey-cache/tmp
export TMPDIR="$PWD/build-prey-cache/tmp"
/opt/homebrew/bin/blender -b -t 8 --factory-startup --python-exit-code 1 --python tools/build_prey.py -- "$name"
```

The default command rebuilds from the raw source, checks the exported GLB and renders it.
Add `--build-only` to build and check the asset.
Use `--verify-only` to check an existing asset.
Use `--render-only` to render an existing asset.
For a rig revision, `--cached` reuses the reduced surface and the colour bake.

The builder pins sampled vertices on the upper silhouette and hoof tips before reducing the mesh.
It transfers source colours to a new 1024 × 1024 PNG using the nearest source triangle.
It estimates directional light from neighbouring coat samples and removes that component before encoding the texture.
Each animal uses one skin and one matte material with roughness 0.88.

The skeleton uses four spine joints, two neck joints, a skull, three tail joints and four joints per leg.
Two opposing rotations lower the pelvis for the crouch. The root stays fixed.
The source mouths are closed. The skull carries the muzzle, ears and head ornaments.
The builder blends weights across the leg attachments and assigns each sole to its hoof.

The three clips use 121, 73 and 67 keys per moving joint.
The walk cycles through left hind, left fore, right hind and right fore with 66% stance.
The run uses 28% stance and staggers each limb pair.
The leg solver scales targets to leg length and counter-rotates the hooves.

## Export checks

The verifier reads the exported GLB and samples 241 poses per clip.
It checks the inverse bind matrices, normalized weights, quaternion rotations, linear interpolation and identical loop endpoints.
It measures the lowest weighted hoof vertices after skinning.
The limits are 5 cm of penetration and 6.5 cm of stance gap for every animal.
During the run, the verifier counts samples with all four soles more than 1 cm above ground.

```text
elk PASS | 1.088 x 2.348 x 2.400 m | 8897 triangles | 28 bones | bind error 1.11e-07 m
  graze 4.00 s | min sole 0.27 cm | max nearest sole 0.30 cm | stance 2.51 cm
  walk  1.20 s | min sole 0.30 cm | max nearest sole 0.30 cm | stance 0.43 cm
  run   0.55 s | min sole 0.03 cm | max nearest sole 8.00 cm | stance free flight
```

| Animal | Minimum mouth height in graze (cm) | Suspended run samples / 241 | Review renders |
|---|---:|---:|---|
| elk | 2.88 | 86 | [Sheets and stills](../artifacts/prey/elk/) |

## Visual review

The sheets show phases 0/8 through 7/8 from left to right across each row.
Blender re-imports each final GLB before rendering. A sky and a sun at 30 degrees light the ground.

The first elk graze stretched skin between the inner forelegs and the belly. The second fit removed those spikes.
The antler tines keep their shape throughout the drop. The deepest graze uses a pronounced foreleg crouch.
The short fur tufts under the neck look angular in close views.

No reviewed animal was rejected.
