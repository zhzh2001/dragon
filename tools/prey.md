# Valley prey

All positions use glTF metres. X points across the body, Y points up and +Z points toward the muzzle.
The origin lies on the ground between the hips and shoulders.
The mouth attachment follows the animated `head` matrix.

| Animal | Tripo source | Width × height × length (m) | Shoulder (m) | Triangles | Bones | Mouth at bind (X, Y, Z) | Mouth in head coordinates (X, Y, Z) |
|---|---|---|---:|---:|---:|---|---|
| [elk](../assets/props/prey_elk.glb) | `tripo-elk-01.glb` | 1.088 × 2.348 × 2.400 | 1.54 | 8,897 | 28 | (0.000, 1.365, 1.500) | (0.000, -0.235, 0.360) |
| [bighorn](../assets/props/prey_bighorn.glb) | `tripo-bighorn-01.glb` | 0.765 × 1.653 × 1.698 | 1.23 | 8,893 | 28 | (-0.370, 1.180, 0.792) | (-0.280, -0.180, 0.092) |
| [boar](../assets/props/prey_boar.glb) | `tripo-boar-01.glb` | 0.501 × 0.887 × 1.599 | 0.86 | 8,801 | 28 | (0.040, 0.399, 0.835) | (0.040, -0.131, 0.195) |
| [chamois](../assets/props/prey_chamois.glb) | `tripo-chamois-01.glb` | 0.352 × 1.199 × 1.200 | 0.92 | 8,893 | 28 | (0.123, 0.850, 0.694) | (0.033, -0.110, 0.154) |

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
It estimates directional light from neighbouring coat samples. It caps the correction to preserve coat markings.
Each animal uses one skin and one matte material with roughness 0.88.

The skeleton uses four spine joints, two neck joints, a skull, three tail joints and four joints per leg.
Two opposing rotations lower the pelvis for the crouch. The root stays fixed.
The source mouths are closed. The skull carries the muzzle, ears and head ornaments.
The builder blends weights across the leg attachments and assigns each sole to its hoof.

The three clips use 121, 73 and 67 keys per joint.
The walk cycles through left hind, left fore, right hind and right fore with 66% stance.
The run uses 28% stance and staggers each limb pair.
The leg solver scales targets to leg length and counter-rotates the hooves.

## Export checks

The verifier reads the exported GLB and samples 241 poses per clip.
It checks the inverse bind matrices, normalized weights, quaternion rotations, linear interpolation and identical loop endpoints.
Each clip keys every joint, including stationary joints. The root keeps its bind rotation.
The verifier measures the lowest weighted hoof vertices after skinning.
The limits are 5 cm of penetration and 6.5 cm of stance gap for every animal.
During the run, the verifier counts samples with all four soles more than 1 cm above ground.

```text
elk PASS | 1.088 x 2.348 x 2.400 m | 8897 triangles | 28 bones | bind error 1.11e-07 m
  graze 4.00 s | min sole 0.27 cm | max nearest sole 0.30 cm | stance 2.51 cm
  walk  1.20 s | min sole 0.30 cm | max nearest sole 0.30 cm | stance 0.43 cm
  run   0.55 s | min sole 0.03 cm | max nearest sole 8.00 cm | stance 3.85 cm
bighorn PASS | 0.765 x 1.653 x 1.698 m | 8893 triangles | 28 bones | bind error 6.76e-08 m
  graze 4.00 s | min sole 0.19 cm | max nearest sole 0.30 cm | stance 1.65 cm
  walk  1.20 s | min sole 0.30 cm | max nearest sole 0.30 cm | stance 0.40 cm
  run   0.55 s | min sole -0.05 cm | max nearest sole 6.67 cm | stance 3.41 cm
boar PASS | 0.501 x 0.887 x 1.599 m | 8801 triangles | 28 bones | bind error 5.69e-08 m
  graze 4.00 s | min sole 0.27 cm | max nearest sole 0.30 cm | stance 1.18 cm
  walk  1.20 s | min sole 0.30 cm | max nearest sole 0.30 cm | stance 0.36 cm
  run   0.55 s | min sole 0.19 cm | max nearest sole 3.68 cm | stance free flight
chamois PASS | 0.352 x 1.199 x 1.200 m | 8893 triangles | 28 bones | bind error 6.36e-08 m
  graze 4.00 s | min sole 0.27 cm | max nearest sole 0.30 cm | stance 2.68 cm
  walk  1.20 s | min sole 0.30 cm | max nearest sole 0.30 cm | stance 0.38 cm
  run   0.55 s | min sole 0.18 cm | max nearest sole 4.94 cm | stance 1.73 cm
```

| Animal | Minimum mouth height in graze (cm) | Suspended run samples / 241 | Review renders |
|---|---:|---:|---|
| elk | 2.88 | 86 | [Sheets and stills](../artifacts/prey/elk/) |
| bighorn | 2.91 | 83 | [Sheets and stills](../artifacts/prey/bighorn/) |
| boar | 0.49 | 70 | [Sheets and stills](../artifacts/prey/boar/) |
| chamois | 1.44 | 75 | [Sheets and stills](../artifacts/prey/chamois/) |

## Visual review

The sheets show phases 0/8 through 7/8 from left to right across each row.
Blender re-imports each final GLB before rendering. The renderer starts each clip from the bind pose.
It checks every imported joint against the sampled glTF transform at each review pose.
In the original ram export, switching from run to walk drew a hoof 15.8 cm below ground.
Keying stationary joints restored the same hoof to 0.3 cm above ground.
A sky and a sun at 30 degrees light the ground.

The first elk graze stretched skin between the inner forelegs and the belly. The second fit removed those spikes.
The antler tines keep their shape throughout the drop. The deepest graze uses a pronounced foreleg crouch.
The short fur tufts under the neck look angular in close views.

The first ram fit stretched the skin between the neck and shoulder during the graze.
The second fit lowers the neck pivots, spreads the head weights across the neck and widens the foreleg stance.
Both horns keep their curls through the head turn. The lowest skull vertex clears the ground by 5 mm at mid-cycle.
The ram folds its forelegs sharply and bunches its coat above the neck. The opposite-side close-up shows this fold.

The first boar pose left the snout 1.5 cm above ground.
The revised clip holds the snout at the turf for 1.52 seconds and sweeps it sideways between the tusks.
The short legs tuck close to the chest during the bound. Its belly hides much of the far pair in profile.

The first chamois fit pinched the forelegs in the deepest graze.
The second fit widens the foreleg stance and lowers the neck pivots. The muzzle reaches the ground between the forelegs.
The bent forelegs still show angular creases near the shoulders. The shoulders and hips keep their shape during walk and run.

No reviewed animal was rejected.
