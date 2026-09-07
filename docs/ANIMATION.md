# Animation: the rig, the assets, and everything they taught us

The procedural dragon rig, the glTF import path, and the hard-won lessons from
driving two very different rigs with the same code. The studio for inspecting
it all is described at the end of the first half.

## Importing a rigged model

`anim::load_skinned_gltf` reads any rigged glTF; `anim::map_dragon_joints`
identifies the neck, tail, wing and leg chains in an arbitrary skeleton so the
same procedural animation drives it. Drop a file at `assets/dragon.glb` and it is
used automatically, falling back to the generated rig if anything is wrong.

Three things learned the hard way, all now handled:

- **Use the file's inverse bind matrices, not recomputed ones.** An exporter may
  write mesh vertices in a space that does not coincide with the joint hierarchy,
  and the file's matrices are what reconcile the two.
- **A joint whose parent lies outside the skin must absorb the whole scene
  transform above it.** Exporters routinely leave scale and orientation on nodes
  above the skeleton.
- **Sides cannot be read from bone names or from a bone's own position.** This
  asset labels its +X wing "_L", and both wing roots sit on the centreline -- the
  side only shows in the subtree, so that is what gets measured.

The rig applies rotations about **body-space axes, composed with each joint's bind
rotation**. Replacing the bind rotation destroys the rest pose, and a real rig's
bones each point along their own axis, so "rotate about local Z" means something
different for every bone.

### A second dragon: what the joint mapper had to learn

`assets/alt/prowler.glb` (a CC-BY wyvern, see ATTRIBUTION.md) is the second
rig the procedural animation drives, and it taught `map_dragon_joints` to
**skip rig plumbing by name** (ik, pole, cont, target, chain, roll, fly,
muscle, kneecap): a wing candidate nearest the root was its IK target, a thigh
candidate its corrective chain. Chains now **descend the main line** --
a branch counts only where the children are peers of the largest subtree, so
an elbow corrective hanging off the upper arm does not end the arm while six
fingers off a hand do -- and the **longest chain wins** among several leg
candidates. Nothing inside a wing is a foot whatever it is called (this rig's
wing hands are "Hand"), a leg chain **stops before the foot**, and a foot that
already hangs off its leg is **not re-anchored** -- only the body-parented
IK-target kind is. The first asset's mapping is unchanged by all of this,
which is the test: `mapped rig:` in the log must read the same for it.

The frame fix above is what made a second asset possible at all: this one also
faces +Z, and the rig detects that itself.

Three more lessons from flying it. **The ground idle is chosen by name**
(idle/stand/rest/breath/hover), and an asset with only a landing gets that
clip's LAST frame held -- a standing pose beats a landing replayed on loop
(`DragonRig::set_base_clip(clip, hold_at)`). **On the ground the authored
stance wins the whole body** (`ground_contact_`, contact not proximity): a
wyvern folds its wings into forelegs and a tail sim cannot know that; the
artist did. A quadruped keeps its wings with the rig even there -- it stands
on its legs, and this asset's authored fold drapes the membranes. And **a
straight-rested, seven-segment tail read as a robot arm** for two reasons the
first asset's short curved tail had hidden: one stiffness per point turns a
uniform load into a rigid rod pivoting at the root (`tail_tip_stiffness`
tapers the spring toward the tip so the chain curves), and a per-vertebra
bend cap generous enough for four segments let seven take the whole bend in
two -- a hinge at the base and a straight boom beyond -- so the cap is
normalised to a four-segment chain. The tell was invisible from the side and
from behind; only the top view (`--inspect 0 24 88`) showed it, after two
rounds of probing a chain that was, numerically, bending exactly as designed.
**Look from the axis the motion is in.**

### Getting a usable asset: download glTF, never route it through Blender

Drop a rigged `.glb` at `assets/dragon.glb` and it is used automatically, falling
back to the generated rig if anything is wrong. See ATTRIBUTION.md for the model
currently in use and how to obtain it.

**Download the glTF variant from the model's Sketchfab page. Do not use the
original FBX.** Blender's FBX importer mangles this rig, which was established by
rendering the model in Blender's own viewport across eight frames of its
animation -- broken at every frame, before any processing, while Sketchfab's
viewer shows it correctly. The signature is around sixty auto-generated `*_end_*`
leaf bones plus vertices flung far from the body. The glTF download needs no
Blender step at all: no pose bake, no bone reduction, no export options to get
wrong.

`artifacts/dragon_broken_fbx_import.glb` is kept as the counter-example.

### How the loader reads a skin

Joint bind transforms are derived from the file's **inverse bind matrices**, not
from the node hierarchy's TRS. An inverse bind matrix is by definition the
inverse of that joint's bind world transform, so inverting it recovers that
transform exactly, and building the skeleton from those makes "skinning at the
bind pose is the identity" true by construction for any exporter.

This matters because reading node TRS means reconstructing the same information
through a chain of conventions -- which node absorbs the scene transform, whether
the mesh node's own transform is divided out -- and two different assets
disagreed about those conventions. One skinned perfectly while the other was
deformed by 12% of its size, and a "fix" derived from the glTF spec's skinning
formula broke the one that had been working. Nothing can disagree about
`inverse(inverseBind)`.

The loader rejects an asset whose bind pose does not reconcile, reporting how far
skinning moves the average vertex at rest. That check cannot false-positive on a
legitimately-shaped creature, unlike the vertex-spread and bone-distance
heuristics it replaced -- a wing membrane is legitimately far from the bone that
drives it. What no automated check can catch is a rig that was mangled before it
reached the file, so a new asset still needs a look.

### Textures and materials

`anim::load_skinned_gltf` decodes each material's images with stb_image and
returns them free of any GPU dependency; the app uploads them via
`gfx::create_texture_from_image`, which builds a full mip chain -- a 4K texture
seen across a valley aliases into shimmering noise without one, and that reads as
a broken model rather than a sampling artefact.

Three maps are read per material: base colour, normal, and glTF's packed
occlusion-roughness-metallic (R/G/B). **Colour space is not optional and is not
recoverable from the pixels** -- base colour uploads as sRGB, the other two as
linear. A normal map read through the sRGB curve gives wrong directions and a
roughness map read that way is far too glossy; both look like shading bugs. The
loader records the space per image in `GltfLoadResult::texture_srgb`, since only
it knows which glTF slot each image came from.

Normal mapping needs a tangent frame, taken straight from glTF's `TANGENT`
attribute (xyzw, w = handedness) rather than derived. A submesh without tangents
gets no normal map -- on this asset that is the eyes, which ship neither.

The mesh is split into **submeshes**, one per glTF primitive, so each can bind its
own textures. A submesh missing a given map still has to fill the sampler slot, so
the renderer binds a 1x1 white texture and `ModelUniforms::material` (x colour,
y normal, z ORM) says which samples to actually use. The shadow map cannot serve
as that placeholder: it is a depth texture and the shader declares a colour one.

Shading is one GGX specular lobe over the wrapped diffuse term -- no IBL, no
environment probe. Enough to separate wet horn from matte membrane, which is all
the roughness map is being asked for.

Images must be decoded **before** `cgltf_free`. Their bytes live in a buffer that
free releases, and reading afterwards is a use-after-free that looked plausible --
every material appeared to share one image, because the freed pointers happened
to compare equal.

### Driving an arbitrary rig

`anim::map_dragon_joints` identifies the neck, tail, wing and leg chains by name
then structure. Wings are a shared root chain plus any number of finger chains,
and sides come from the mean X of a bone's *subtree* -- this asset labels its +X
wing "_L" and puts both wing roots on the centreline.

Rotations are applied about **body-space axes, composed with each joint's bind
rotation**, and **normalized across the chain**. All three matter: replacing the
bind rotation destroys the rest pose, a real rig's bones each point along their
own axis, and rotations down a chain add up -- so applying the flap angle at
every bone makes total bend depend on how many bones the rig happens to have. The
generated rig has two bones per wing and the imported one has five, which put the
imported dragon's wings in a steep V at rest.

### The animation studio

Judging animation from live flight means chasing a manoeuvre with the camera
while also flying it, and no two takes match. The studio (`--studio N`, or the
Studio panel) pins the dragon at one spot and plays a scripted manoeuvre on
loop. Each scenario is **dynamically consistent**: orientation is a pure
function of time and the body-frame angular velocity is finite-differenced from
that same curve, so a scenario cannot lie about its own rotation and every
physically based response reacts exactly as it would in flight. Nothing
downstream of the rig reads position kinematics, so pinning the position is
safe. Combat, rally and the HUD idle while the studio is up.

### One frame: the rig works in model space, and the model may face +Z

**Every world quantity the rig reads arrives in the engine's body frame
(forward -Z) and every bone lives in model space, and this asset faces +Z in
its own space** (the app yaws it 180 to fly). For a long time the rig applied
engine-frame gravity, airflow, pseudo-forces and the aim target straight to
model-space bones. On the generated rig the frames coincide, so every test
passed; on the real dragon it silently inverted the fore-aft axis: pitch
steering worked backwards (nose-up input LOWERED the head), the brake surge
pushed the neck the wrong way (the "neck buckles under the chest" saga was
this), the tail was upstream of its own airflow so the v^2 drag never acted on
it, and the head aim reared the head 50 degrees UP and turned it the wrong way
when locked on. `DragonRig` now measures which way the model faces from the
bind pose (head forward of tail, the same rule the app uses) and carries one
rotation, `body_to_model_`, across everything; `model_frame()` re-expresses
the FlightState so `conj(orientation)` lands in model space. Sign-bearing
constants (sweep aft, washout, roll lean, leg trail, foot hang) are written in
engine terms and multiplied by the measured facing. **When a generated test
rig and an imported asset disagree, suspect the frame before the physics** --
and probe the imported asset numerically (load the glb in a test binary), not
just the generated one.

Two more asset-facing traps found the same day. **The per-vertebra bend limit
must be relative to the rest shape**: this neck's vertebrae sit at 27-29
degrees to each other at rest, so an absolute 20-degree limit straightened
the neck permanently and clamped away most of any steer -- the "stiff neck no
slider could fix". And **measure the jaw's tip as the most forward descendant,
not the deepest**: the deepest bone under the jaw is the tongue, which points
back into the mouth, and the measured open direction came out inverted.

### Why the tail was abrupt (and how to find such things)

Three causes, found in this order, each hiding the next. The **hard joint
clamps** stopped a tail tip arriving at 25 m/s in one frame; a soft limit
(`chain_limit_stiffness`/`_damping`) now brakes progressively from half the
allowed bend, and the range clamp removes only the velocity into it. That
was not the main cause. **Pseudo-forces on a spring sized for 1 g**: a 4 g
S-turn reversal flung the tail to its constraints and it snapped back;
muscle tenses against load, so chain stiffness now saturates with the local
inertial acceleration (`chain_load_tone_accel`) -- deflection bounded, whip
gone; this took the old dragon's worst tip acceleration from 276 to 51 m/s^2.
And the Prowler's remaining one-frame kick was a **26 cm stub bone** at the
head of its tail chain, free to swing 180 degrees in a frame and drag the
chain after it; the mapper now trims stub bases (`trim_stub_base`). Method:
trace the tip's per-frame acceleration through a scripted manoeuvre
(`probe`-style, off the studio's S-turn state), bisect by zeroing one force
term at a time, and do not trust a view that cannot show the axis the motion
is in.

### Attack and speed posture

The rig takes a `RigAction` (breath 0..1, fire edge, boost) each frame from
combat, the bots, or the studio. **Fire from a closed, still mouth reads as a
particle effect stapled to a model**; the jaw, the neck and the claws are what
say the creature is doing it. The jaw (`Bone_024` here -- found as the parent
of the lower lip when nothing is named "jaw") opens with the breath and gapes
and shuts on a spit; the neck **thrusts forward and down** into the flame,
stiffens (`breath_neck_tone`) and trembles faintly on the head only, after the
aim so the aim cannot correct it away; the **spit rears the neck back and
whips it forward** through the same spring as everything else, helped by a
velocity impulse and a 7x stiffness for the gesture, because a steer alone
asks an overdamped neck to move through its spring and a spit is a snap. The
**neck carries a share of the aim** (`neck_aim_share`), curling toward the
lock through the spring while the head snaps the residual -- an animal looking
40 degrees off its body turns its whole neck. Talons open while breathing.

Speed shapes the wing on its own: past cruise the wings **sweep back and
part-fold** whether or not the tuck is held (`speed_sweep_deg`, blending into
the full tuck angles), the outer membrane **flutters** with the square of a
speed factor so cruise is calm and a dive is alive, a flare **buffets**, and
under g the tips **wash out** and the wings come a little forward. The studio's
Attack scenario now spits and breathes on an 8 s schedule and Dive releases the
tuck halfway through its cycle, so both can be watched on loop.

### Flight response: the active posture layer

The chains are passive -- they lag, swing wide and settle. On top of them sits
an active layer, because a flying animal is not a fuselage with dynamics bolted
on: the **tail steers** (rudder with yaw/roll input, elevator with pitch), the
**neck leads a manoeuvre** and lowers into the wind at speed, and the **wings
bow upward under g** and lean asymmetrically with roll input.

Steering deflects the chain's *target shape* rather than its joints, so the
spring pulls the simulation toward the deflected pose and the active motion
eases, overshoots and settles through the same integrator as everything
passive -- one system, one look. The wing roll lean is deliberately not
mirrored between sides: the same rotation about the body axis on both wings is
exactly the antisymmetric shape that produces a roll.

The **legs are pendulums**: they hang from the hips in the effective gravity of
the dragon's frame -- true gravity plus the frame's pseudo-forces at the hip --
held toward the tuck pose by a muscle spring. Braking floats them forward, a
skid slings them outward, and near the ground they extend and stop swinging. A
subtlety worth keeping: in a **coordinated** turn the legs deliberately do NOT
swing laterally, because gravity plus centrifugal force point through the body's
floor -- that is what coordinated means. The tell the pendulum fixes is the
skid, the roll transient and the brake.

**Chain aero drag is quadratic and slender-body**: the v^2 term acts across a
segment, not along it, and only where it is restoring. A segment pointing
downstream (the tail) is straightened by the flow; one pointing upstream (the
neck) is the arrow flying backwards -- aerodynamically unstable, and left to raw
physics it flutters metres wide at dive speed, so the destabilizing case is
suppressed the way real muscle would. One force law gives a tail that hangs at a
hover, streams level at cruise and pulls dead straight in a dive. Chain steering
**curls** progressively down the chain rather than rotating rigidly at the root:
a tail curves, it does not hinge like a door.

Tucking adds **droop** (`tuck_droop_deg`): sweep and fold both act in the
horizontal plane, so without it a folded wing stays at glide dihedral and the
membrane drapes below the body -- half-folded, not a stoop.

**The authored clip is a ground idle and is gated to the ground**: full
strength standing (via the smoothed ground-proximity signal), ~15% in a calm
glide, and that trace fades to zero with flight intensity (max of speed, g
excess, turn rate, tuck, brake -- max, not sum). Toes gripping ground at
100 m/s read as someone else's animation on the wrong creature.

**Limbs in flight trail, they do not dangle.** The whole leg rotates aft at
the hip (`leg_trail_deg`) before the fold bends the knee -- a flying quadruped
presses its legs back along the body, and a pure fold leaves them hanging like
landing gear. The forelegs are their own chains (`upper_arm` -> `ik_underarm`
on this asset -- the `ik_` prefix is on a *deforming* bone here, so it is not
excluded from that search) and trail the same way. The correct trail sign was
settled by rendering both and looking, not by deriving it.

**The upstroke folds the wrist** (`upstroke_fold_deg`): as the wing rises past
~20 degrees it progressively part-folds, which is real bird kinematics and what
keeps two raised wings from crossing over the spine at the top of the beat --
that, plus a flap-up ceiling of 44 degrees (`flap_up_angle_deg`), because 54
put the membranes through each other in any front view.

**The neck braces** (`neck_inertia_scale`): it feels only a third of the
frame's pseudo-forces. Under braking a full-inertia neck buckled under the
chest; a real animal holds its head as a stable platform for the eyes.

**This asset parents all four feet directly to the body root** (IK targets),
so no leg motion ever moves them -- pose the legs however you like, the feet
stay nailed to their bind position in space, which was the whole
standing-in-air look. In flight each foot is therefore **re-anchored to the end
of its leg chain** (`foot_follow`): matched by bind distance, its bind offset
carried in the anchor's frame, converted back to a body-local transform each
frame -- and that conversion must divide by the parent's scale, because the
joint below the root absorbs the scene's scale and a local position lives in
scaled space; dropping the divide sent every foot to within a metre of the
origin. On the ground the authored planted stance wins. The hang and claw curl
(`foot_hang_deg`, `toe_curl_deg`) compose on top.

**The neck is heavy**: overdamped (`neck_damping_scale`), low inertial
response (`neck_inertia_scale` 0.18), and a small lead angle -- it moves slowly
and settles without ringing, the feel of muscle rather than a spring. And the
**leg trail backs off as the tuck deepens**: fold plus full trail rotated the
thigh ~100 degrees in a dive, pointing the shin up and parking the re-anchored
feet above the wings; a stoop stows the legs under the body, not rotated past
it.

**A positional brace initializer silently disconnected all of this once.**
`ChainFeel{stiffness, gravity}` kept compiling as the struct grew, so the
neck's brace, aero gate and articulation range were defaults (range 178
degrees) for three commits of "fixes" that were dead code -- every keyframe
check was watching an unfixed sim. The call sites now assign named fields.
When a fix does not change behaviour, first verify its values actually reach
the code that runs: the neck-position recorder in the transcripts caught this
by showing the sim violating a clamp that provably worked in isolation.

**Chains have an articulation range** (`neck_range_deg`, `tail_range_deg`): no
segment may deviate further from its steered rest direction than muscle allows,
whatever the forces say. And the **v^2 aero gate is per-chain, not
per-instant** (`ChainFeel::aero`): gating on the momentary direction created a
trap where a surge that folded the neck backward made it read as "downstream",
and the drag then pinned it folded under the chest like a windsock -- a stable
attractor that only appeared MINUTES into a run. Every fresh-start keyframe
check missed it; the user's screenshot at t=94 s found it. **Verify long runs,
not just cold starts.**

**Axial chain forces are mostly suppressed** (`chain_axial_response`):
transverse forces bend a spine, axial compression only buckles it, and muscle
resists exactly that. A braking dragon's neck under full axial pseudo-force
folded under its chest. Note the correct steady state of a constant spin with
no airflow is a radial tail with NO lateral offset -- the test asserts the
onset whip, not a sustained deflection that would be wrong physics.

**The upstroke redistributes outboard**: past 20 degrees of elevation the
shoulder's share of the flap shrinks and the wrist leads, real bird
kinematics, keeping the inner membranes from crossing above the spine while
the tip still reaches the full angle.

**Muscle tone** (`chain_tone`): chain stiffness scales with flight intensity,
damping with its square root to stay near critical. That is what keeps the tail
a rudder rather than a streamer in a dive or a hard pull. The **neck has its
own profile** (`neck_stiffness_scale`, `neck_gravity_scale`): it is muscle
wrapped around a spine carrying the head the animal aims with, several times
stiffer and better supported than the tail. And `chain_max_bend_deg` is tight
(20 degrees): a spine bends a long way in total but never sharply at one
vertebra -- a looser limit accordioned the tail under hard manoeuvres.

### Authored motion under the procedural rig

The rig drives what flight determines -- wings, neck, tail, leg tuck -- and the
file's own clip supplies everything else: toes, jaw, small shifts of the body.
Without that layer the extremities are perfectly still, which reads as uncanny
even when the big motions are correct.

`DragonRig::update` resets to bind, samples the clip, then lets the procedural
pass override. Joints the rig owns are rebuilt **from bind**, so the clip cannot
fight them; the leg tuck instead **composes onto** the clip (`rotate_joint`'s
`onto_current`) so the feet keep their authored motion while still folding.

Only **rotation** tracks are imported. Translation and scale tracks would import
root motion -- fighting the flight model for control of where the dragon is --
and stretch bones the skinning assumes are rigid.

**Track keys are stored as deltas from each joint's rest rotation and composed
onto the bind rotation**, never written over it. The skeleton's bind rotations
come from the inverse bind matrices, which for this asset do not agree with the
node hierarchy's TRS -- the joint below the root absorbs a whole scene transform
its own node rotation knows nothing about. An absolute rotation discards that
transform and rolls the entire dragon onto its back, while every individual bone
still moves plausibly. A delta is identity at the clip's rest key, so the bind
pose is reproduced exactly whatever convention built the skeleton.

That bug hid from a check that measured how much each track *changes*: the
offending joint's track is constant, so it read as zero motion. **Compare a
clip's rest pose against the bind pose, not a track against itself.**

Two dead ends, recorded so they are not repeated: cutting a rig down to fit a
joint budget (weight transfer produces glitchy wings and snouts -- MAX_JOINTS is
256 for this reason), and picking a pose-bake frame by proxy metrics rather than
by rendering candidates and looking at them.
