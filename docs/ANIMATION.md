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

### The wing was throwing away half of everything it was told

Four separate normalizer and axis bugs, all in `drive_wings`, all found by
measuring rather than by looking -- the poses were wrong in ways that read as
"stiff" and "unnatural" without pointing at a cause. `tools/probe_wing_geometry.py`
reads a `.glb` directly, replays `drive_wings` on it, and reports where the
membrane ends up; it is the tool to reach for before touching any of this again.

**The flap normalizer balanced rotation, not travel.** Its comment promises
"the wingtip ends up rotated by the flap angle itself, whatever the chain
length", and summing angles down a chain does get the tip BONE's orientation
right -- while the tip POSITION travels a fraction of the arc, because an
outboard joint pivots close to the tip and barely displaces it. The upstroke's
shoulder cut deliberately shifts weight out to exactly where that leverage is
worst. Add `wing_phase_lag` being applied outside the normalizer entirely, and
the dragons flew 43-49% of the commanded stroke. The normalizer now weights each
joint by its leverage over the membrane tip, measured once per side from the bind
pose in `init()`, with the moment arm taken PERPENDICULAR to the flap axis -- the
straight-line distance credits a swept wing with leverage it does not have.
83-93% after. `test_wingtip_reaches_the_commanded_flap` pins it, on the generated
rig and on both imported assets, because chain length is the variable that bites.

**Sweep and fold borrowed that same normalizer**, which is `1 / sum of decay^k`
and correct only for shares that are themselves `decay^k`. Theirs are
`(0.4 + 0.6 * progress)` and `progress * scale`, so a commanded 88 degrees of
tuck sweep arrived at the tip as 111 degrees on a three-plus-one wing and 131 on
a four-plus-two. Past 90 the tip is rotated behind straight-back and the folded
wing points inboard at the opposite flank: 71% and 99% of one wing's membrane
inside the other's. Each term divides by the sum of its own shares now. The fold
deliberately leaves the anatomical scales OUT of its normalizer, so closing the
fingers harder does not quietly open the elbow -- separating those stations is
the whole point of the profile, and a shared normalizer couples them back.

**The fold axis was body up, which is only the hinge for a wing bound level.**
The generated sculpts carry their membranes draped aft-down: the plane fitted
through the wing joints is 44.5 degrees off horizontal on Stormsail and 35.3 on
Embercrest, nearly all of it incidence. Folding those about body up rotates
segments up to 61 degrees out of their own membrane plane and shears the inner
membrane through the flank; about the fitted plane normal they stay within 6.
`wing_fold_axis_` is that normal, per side, from `init()`. The two downloaded
assets fit body up to within a degree, so nothing changed for them. One
consequence to know: on a tilted wing the fold now bleeds into elevation -- an
`upstroke_fold_deg` of 24 costs the top of the stroke real degrees, which is
physically right and worth a per-model number.

**The outermost finger bone was never driven on three of the four assets.**
`significant_children` needs a subtree of two, so any bone whose child is a leaf
ends its chain one bone early. That was written for the default asset, whose
leaves are genuine `_end` export artifacts with no vertices. Everywhere else they
are real: the wyvern's four finger ribs carry roughly half its membrane and rode
rigidly on a 30 cm stub, giving the wing no knuckle to furl at. `descend_main`
continues into a lone non-`_end` leaf -- only when the chain has already
terminated AND there is exactly one child, because a lone leaf beside a real
branch is a corrective and treating it as a branch ends the Prowler's arm at the
shoulder. The default asset's `mapped rig:` line is unchanged, which is the test.

### A fan sweeps a membrane; it never closes one

Raising the wings on the ground was not folding them, and no amount of extra
`ground_stow_fold_deg` was going to help. Every rotation in `drive_wings` runs
the same way down the chain -- progressive, same sign, normalized -- which is a
fan. A fan sweeps a membrane around. Closing one needs two things that are
deliberately neither progressive nor normalized:

**`ground_stow_close_deg` zigzags the arm.** The forearm folds back against the
humerus and the hand folds back against the forearm: the elbow takes the angle
one way and the wrist the other, and the membrane collapses into the pleats
between them. Give both hinges the same sign and the arm curls into a spiral --
a longer wing, not a shut one.

**`ground_stow_converge_deg` shuts the fan**, and this is the one that mattered.
Each finger rib swings toward the innermost by its share of the closure. It is
invisible from the code alone: the wyvern's four ribs all begin at the SAME
point on the wrist, so they receive identical rotations and can never converge,
however hard the fold is driven. That is why a "folded" wing kept its full bind
spread and stayed an open sail. Embercrest has the same problem, milder.

Both fade in with ground contact and low airspeed, so no flight pose changes.
The general lesson is worth more than the two knobs: **when a pose will not
close, check whether every joint in it is being told to rotate the same way.**

### Standing is not tucking

A tuck that closes as hard as a standing fold puts the two membranes through each
other across the chest; a standing fold as open as a comfortable stoop is a bat
cape. They are separate poses and now separate numbers: `ground_stow_sweep_deg`,
`ground_stow_fold_deg`, `ground_stow_wrist_deg` and `ground_stow_finger_deg` add
to the tuck with ground contact. The wrist rides high and the finger ribs hang
down the flank, which is what closes a membrane into a narrow bundle -- see
`docs/concept/wyvern-wing-reference.png`, stage 4.

Off by default, and profiled per model, because the right angles depend on where
a rig puts its wrist. It matters most for the two generated assets: **neither
ships an authored clip at all**, so the procedural pose is the only stance they
have, where the ground normally hands the whole body to the artist. The default
asset is the opposite case and still looks worst on the ground: its wing root is
two bones that bind at the model origin, and its authored clip is a flying idle
with the wings spread, so there is no folded stance to fall back on either way.
That one is an asset problem.

The flight model also drives `wing_tuck` to 1 while grounded. The studio's
grounded scenario always assumed that -- it sets a full tuck itself -- but the
game did not, so a landed dragon stood with its wings half open unless the player
kept holding the dive key. Lift is moot on the ground, so it costs the force
model nothing.

### A wingbeat is not a wave

Every generated species flapped like a hand waving: a flat plank hinged at
the shoulder, swinging up and down about one axis, the same shape on the way
up as on the way down. Eight frames across one beat from the front and from
the side made it obvious -- one `--studio 1 --inspect` render per phase,
stitched into a contact sheet -- where any single screenshot had looked fine.
**Judge a cycle from a sheet of its phases, never from one frame.**

The cause was structural, not a bad constant. Every shaping term was keyed to
the wing's *position* (`upstroke_fold_deg` and the shoulder cut both switch
on past 20 degrees of elevation), and position is symmetric between the two
half-strokes: the wing at +30 going up was identical to the wing at +30 coming
down. Direction is what a beat has and a wave does not. Published bat
kinematics (Hipposideros, straight level flight) give the shape: the wingtip
path is a crescent inclined ~50 degrees from horizontal, forward on the
downstroke and back on the upstroke; the wing flexes hardest a fifth of the
way into the upstroke and is fully extended again before the top; the outer
wing pitches through ~100 degrees over the cycle; and the body rides the push.
Four terms in `drive_wings`, all keyed to the beat's *phase* or *velocity*,
all gated on flap amplitude so a glide, a tuck and the ground stow are
unchanged, all sliders at the top of the Wings header:

- **Stroke plane** (`stroke_plane_tilt_deg`): the shoulder yaws the whole
  wing about the arm's up by a share of its elevation -- forward when down,
  aft when up. At the shoulder alone, because that is the joint that moves;
  distributing it down the chain would shear the membrane.
- **Recovery flex** (`recovery_elbow_deg`, `recovery_wrist_deg`,
  `recovery_finger_deg`, `recovery_droop_deg`): hinge angles at the named
  joints, riding an envelope that is zero through the downstroke, one from a
  quarter of the way into the upstroke, and zero again from
  `wing_recovery_extend_phase` so the downstroke starts taut. The hand also
  *droops* below the arm, which is the M-shaped front silhouette of every
  large flyer mid-upstroke. That envelope used to be gated on
  `wing_phase_delay` being non-zero, which left five of the six species with
  no recovery at all; it now needs only a beat.
- **Feathering** (`stroke_twist_deg`): wrist and ribs pitch leading-edge-down
  in proportion to the wing's downward speed and leading-edge-up on the way
  up -- zero at both reversals, largest mid-stroke.
- **The body answers** (`beat_heave_m`, `beat_heave_lag`, `beat_pitch_deg`):
  the root joint rises after each downstroke and the nose lifts with the push.
  Visual only: the flight model's position is untouched, so the chase camera
  does not bob.

Plus a spanwise lag on by default (`wing_phase_delay` 0.025 per joint, ~8
frames at the tip), because a wing whose every station reverses on the same
frame is the single strongest waving cue.

Two things learned getting there. **The recovery hinges turn about the arm's
up, not about the membrane plane's normal the tuck folds about.** On Stormsail
the fitted plane drapes 44 degrees, so "aft in the plane" is also "up", and
the first version hooked the hands skyward at mid-upstroke by more than the
droop could bring them back -- found by rendering the same frame with each
new term switched off in a scratch rig profile (symlink the `.glb`, edit the
`.rig.cfg` beside it). A wrist flexes level with the arm; the droop alone
decides where the hand sits vertically, so the two knobs stay independent on
every asset. And **the position-keyed shapers yield while the flex is in**:
left at full strength the shoulder cut handed the raised wing's elevation to
the wrist at exactly the moment the recovery wanted it hanging. They return
as the wing extends, so the flick open at the top and the anti-crossing at
the peak are as they were.

`test_wingbeat_is_not_a_wave` pins all four against a control tuning with
just that term off, on the generated rig: the hand is nearer the spine
mid-upstroke, further forward at the bottom and further aft at the top, the
leading edge lower going down and higher going up, the root higher after the
downstroke -- and none of it survives a tuck.

### The jaw bone may own the wrong half of the mouth

Three species were reported with a wrong attack jaw, and the bone was right on
every one of them: `test_roster_jaws_open_downward` shows the jaw tip dropping
26 degrees on the breath, in the head's frame, on all six. What differed was
the SKIN. On Rimefang and Ironroot the rigger's jaw mask -- Embercrest's
hand-authored cut applied to a head it was not drawn for, or heat weights on
a sculpt that gave heat nothing to split on -- handed the snout to the jaw
bone and left the mandible on the head, so "open" tilted the nose down onto a
fixed lower jaw and the mouth clamped shut around the flame. On Tidewrack the
weights were right and the profile was empty: a sculpt authored mouth-open
hung its gape in flight and the attack dislocated it 26 degrees further. The
two faults look alike from the cockpit and are fixed in different places --
a per-head jaw mask and a rebuild for the first, two lines of `.rig.cfg`
(`jaw_rest_deg` negative to close the sculpted gape, `jaw_open_deg` to open
back to it) for the second.

Two lessons about *finding* such things. **Small head crops lie.** Half a
dozen contact sheets at 50% were read wrongly in both directions before one
side-on render of the mouth at full size, bind beside `jaw_rest_deg -24`,
settled each species in a glance: the part that moves when the bone is
driven closed is the part the bone owns. And **geometric heuristics over an
arbitrary rig are worse than a picture**: three attempts to classify
mandible-vs-skull vertices by hinge height, by the bone axis and by weight
ownership each flagged a different, wrong set of assets, because the hinge
sits at a different place in the head on every rig. The bone-direction test
stays; the skin is judged by rendering.

### Wings on the ground, and in the dive, per species

The report was "wing folding is broken on every dragon in the dive and on
the ground; wings clip into other parts; wyverns should rest their wings on
the ground." Rendered from the front, the side and above, it was three
separate faults.

**The dive tuck was the code default on four species** (88 degrees of sweep,
52 of fold), and on these sculpts that folds the hands out sideways and
hangs the membrane through the legs, while Embercrest's shallow 42/24 left a
half-open delta. Three tucks were rendered per species at full tuck in the
studio dive; 75/45/30 with the elbow kept open and the fingers closing
harder suits Stormsail, Tidewrack, Blightmaw and Embercrest, Rimefang takes
75/30/30 with no fold scales (see below why), and Ironroot's short wings
were already an arrow at the default.

**The standing stow was Embercrest's, seeded onto every species**, and on the
others it stood the membranes up over the back like two sails. The cause is
the one that bit the recovery hinge: these sculpts bind their membrane
planes 35-45 degrees off horizontal, and the stow's sweep and fold turn
about that plane's normal, so "swept aft" is also "lifted" -- 55 degrees of
stow sweep on top of the tuck's raised the elbow high above the spine, and
the membrane between a high elbow and the flank is a sail whatever the
fingers do. Blightmaw, Rimefang and Ironroot now use a SMALL sweep (30-35),
no extra fold, and a wrist LOWERED below the shoulder (-50 to -80), which
lays the arm along the flank with the fingers hanging beside it. Embercrest
keeps its own, which still holds under its new tuck. Two more lessons about
finding this: the elbow and finger **fold scales shape the stow too**, not
only the tuck -- with them Rimefang's hands crossed over the spine, so
Rimefang's dive is the variant that needs none; and one grid row was
misread at contact-sheet size as "folded at the flanks" and chased through
four grids before a control render proved it had always been the same
spread-wing pose. **Re-render the pose you think you are refining before
you refine it.**

**A wyvern stands on its wings.** Stormsail and Tidewrack have no forelegs;
on the ground the wrist is the front foot, the arm a strut reaching down and
a little forward, the hand folded back up along the forearm with the
membrane pleated between -- the pterosaur stance. The existing knobs could
not reach it for two reasons, each now a field. The ground forces a full
tuck, and on a 44-degree membrane plane the tuck's aft sweep lifted the arm
faster than any wrist elevation could lower it: `ground_stow_tuck_share` (0
on the wyverns) hands the standing wing to the stow alone. And the zigzag
close bent the elbow as much as the wrist, when a planted arm wants a
straight elbow and only the hand folded: `ground_stow_elbow_scale` (0 on the
wyverns) is the elbow's share of it. Found from an elevation-only pose --
every other stow term zeroed, `-100` at the wrist, and the arm went down --
then built up one term at a time; the profile is a 150-degree drop, the
fingers 140 up, a 140-degree wrist close and the fan converged.

The grid that did the work is worth keeping: symlink the `.glb` into a
scratch directory beside a copy of its `.rig.cfg` with the candidate lines
appended (the loader takes the LAST occurrence of a key), render three views
per candidate with `--studio 9` or `--studio 5`, and stitch the rows. Four
candidates a round, three or four rounds a species. Nothing in the numbers
predicted which row would win; only the pictures did.

**The bench and the game disagreed about the standing wing.** Every stow above
was tuned in the studio's grounded scenario and then reported as "a messed-up
bunch" from a real landing. Landed in the game and rendered beside the bench,
four of six species stood differently: the studio pinned the wing angle at
18 degrees on the ground, the game supplied the 9-degree glide dihedral less
a load flex from a zero-g reading, and that 13-degree gap through the stow's
large in-plane rotations was enough to turn a wing folded along the flank
back into two raised sails. The stow now OWNS the standing wing: the flight
elevation, load flex, roll lean, flare, flutter and load twist all fade out
with ground contact (`flight_share` in `drive_wings`), so the bench and the
game agree by construction and a profile's stow angles are the whole pose.
The wrist angles in every profile moved by the 18 degrees the bench had been
supplying. **Whatever the bench pins, land in the game and look** --
`--input -0.25,0,0,0,0,0 --frames 900` puts any species on the ground and
stopped, and `--inspect` frames it.

**Legs clear the beat only when they trail.** Tucked under the belly with the
knees splayed, the hind legs sat exactly where the inner membrane sweeps at
the bottom of the downstroke; seen from below (`--inspect 180 16 -50` at a
bottom-of-stroke frame) the membrane passed through them. Pressed back along
the tail base (tuck 80, trail 60/40) they stay behind it, and the glide is
the more streamlined for it.

The dials for all of this now sit in the **Studio panel under the scenario
that judges them** -- the standing wing under Grounded, the tuck and legs
under Dive, the beat under Flap -- with a button that saves the profile for
the model on the stand. The Dragon panel's Wings header still has them all,
but that header is collapsed inside a collapsed panel, and a dial that
cannot be found does not exist.

### A folded wing is where its bones point; a stance is a pose the sculpt never had

The report was two screenshots of the grounded scenario: Embercrest's
folded wing curled into a hoop over its back, and Rimefang standing on its
hands with the body pitched up and the hind legs a metre in the air. They
were two different faults, and neither was a bad number.

**The angle stow could not fold these wings.** Every `ground_stow_*` field
is a hinge angle, and every hinge turns about an axis the sculpt chose --
the fitted membrane-plane normal for sweep and fold, the arm's up for the
zigzag. On a membrane bound 35-45 degrees off horizontal, "fingers up" is
also "fingers inboard", and the closed fan met the humerus in a loop; four
rounds of rendered grids per species had found the least bad set of eight
coupled angles and no better. A folded wing is better described by where
its segments POINT than by how far its hinges turn, and the reference for
that is any bat or bird at rest, drawn for this engine as
`docs/concept/standing-dragon-reference.png` (Nano Banana, orthographic,
labelled): upper arm swept back and a little up along the flank, forearm
folded forward and up so the wrist rises just above the shoulder line,
fingers folded back and hanging down the flank with the membrane pleated
between them and the tips trailing past the hips. `ground_wing_aim` turns
that description on: each segment has a body-frame direction in the profile
(`ground_wing_{arm,forearm,hand}_{sweep,elev}_deg`, sweep from straight out
toward aft, elevation from horizontal), and `drive_stance` aims each bone at
its direction in turn, shoulder outward, with whatever rotation gets it
there (`aim_bone`: the model-space rotation between the posed bone and its
target, carried into the parent's frame and composed onto the local
rotation, then the world matrices recomputed so the next bone is aimed from
where it now is). Each finger rib is aimed at the hand direction lowered by
`ground_wing_fan_deg` per rib, so the closed fan reads as pleats. The
membrane plane no longer matters, which is the point: **Embercrest's
directions folded Rimefang's wing unchanged**, where the angle stow had
needed a different recipe per sculpt. Four candidates were rendered for
Embercrest side/front/rear/top; the anatomical defaults (forearm at 50
degrees) stood the wrists up as ears, and 15/35 with the upper arm swept 75
sits the wrist at the shoulder line. The angle stow stays for the wyverns,
whose planted wrists it already reaches; a profile that aims should zero
its `ground_stow_*` lines so nothing else moves under the aim.

**Rimefang's bind pose is a leap, and the ground returns the legs to the
bind pose.** The four Hunyuan variants were generated from a rearing
turnaround -- chest high, hind legs trailing, forelegs reaching down -- and
the shared `winged-quadruped.json` skeleton was remapped into that mesh by
bounding box, so its bones do not run along the limbs (a thigh bone 23
degrees forward of vertical sits inside a mesh thigh that trails aft), and
nothing measured from the bones can find a stance. A skin-cloud probe was
tried -- the principal axis of the vertices each bone dominates -- and gave
nonsense on thick limbs. What an authored idle would have supplied is a set
of angles, so that is what `ground_stance` is: `ground_body_pitch_deg` at
the root (nose up positive, the beat_pitch convention), then
`ground_{hip,knee,ankle}_deg` and `ground_{shoulder,elbow,wrist}_deg`,
each a swing about body X with positive moving the segment's far end
forward, plus a lateral splay per limb pair. Re-posing the legs moves the
feet, and `flight.cfg`'s `ground_offset` was measured from the bind soles,
so `ground_feet_level` lifts the root inside the model until the lowest
foot joint is back on the floor the bind feet stood on: nothing outside the
rig knows the stance exists, the body centre the camera follows moves by
under a metre, and the bench and the game agree by construction (both were
rendered; `artifacts/landed_in_game.png`).

**Numbers screened the stance, pictures chose it.** The rig runs a species
grounded in a few milliseconds without the renderer, so a scratch probe
swept 1,600 angle sets and printed, per set, the height of the hind feet
over the hands, the thigh, shin, upper-arm and forearm angles from vertical
and the spine pitch; the sets with all four feet on one floor and plausible
angles numbered a dozen, and three of those were rendered. A body levelled
to the reference (-20 degrees) balanced only with the elbows hooked under
the chest, a dog about to lie down; -15 stands it like an alert cat, chest a
little high, and that is the profile. The Studio panel's Grounded section
now carries every one of these dials and a readout of each foot's height
above the lowest, so the same screening can be done by hand: drag the body
pitch, watch which foot lifts, fix it at the hip or the shoulder.

**Blightmaw was not too broken; it was rearing.** Its broad, lobed membrane
hung down the flanks to the ground like a tarp in the grounded scenario,
and the model was a candidate for regeneration. With the same stance
recipe -- body pitched down, legs under it, Embercrest's fold directions
untouched -- the same membrane folds over the back like a bat's; the pose
was hanging the wing, not the mesh.

**Ironroot took three passes.** Its skeleton (`tools/skeletons/ironroot.json`)
binds the thigh 38 degrees aft, the trailing leg of the leap, so the hip
swings 60 degrees forward where the shared skeleton's needed none. Its
short wings carry a large wrist claw, and the fold went wrong both ways
before it went right: Embercrest's directions stood the claws beside the
head like antlers; the fix, the forearm laid nearly straight forward along
the flank, collapsed the membrane into a small pouch under the shoulder
("folded too tight and small"); a forearm level with the ground put the
claws out sideways like handlebars. The wrist raised above the shoulder
line and a little forward shows the membrane as a wing along the flank
with the claws standing up behind the head, which is where this sculpt's
claws go when the wrist goes there. Its jaw was a separate fault under the
same report: `jaw_rest_deg -22` over-closed it, the mandible swung up
through the upper jaw and the teeth vanished into the skull. Four rest
angles rendered side-on with `--inspect-head`; -12 is where the mouth just
shuts. **Render the head when a stance changes** -- the neck chain and the
body pitch move where the camera sees the jaw from, and an over-closure
that hid in the glide showed on the ground.

**The wyverns rest on their wings, wrists planted wide and forward.** The
first target was a pterosaur diagram (body level, wrists under the chest),
and the second look asked for dragon references instead, which is the right
call: the grounded wyvern of modern games and films is what the player
knows, and it is a different animal -- wrists planted well AHEAD of and
WIDER than the hind feet, elbows high and out, the wing finger folded back
up along the arm, body low, neck up, tail on the ground
(`docs/concept/landed-wyvern-reference.png`). Stormsail and Tidewrack are
sculpted upright like theropods with short wing arms (shoulder 6.9 m up,
arm plus forearm 5 m), so from a standing body the wrist cannot reach the
ground at all, and the angle stow's "planted wrist" had always been a hand
held in the air beside the body. Three fields carry the stance:
`ground_wing_plant` makes the wrists count as feet for the floor, so the
fold that reaches them down is what sets the body's height;
`ground_neck_pitch_deg` and `ground_tail_pitch_deg` raise the head and drop
the tail against the lean; `ground_lift_m` raises the whole skeleton by the
hand that hangs under the planted wrist joint, which the rig cannot see. The
body reaches the reference as a crouch: hips dropped by a deep hind-leg Z,
a 20-30 degree lean, the wing arm reaching down, forward and out. The
screen ranked level crouches by how far ahead and out the wrists landed
(the batch probe prints both), because the first level crouch found had
them hanging beside the legs, which is the "wings too close to the legs"
report. A level-bodied stance was screened and rendered first; it needs 55
degrees of pitch on this torso and reads as a crawl. **A reference is a
target for the silhouette, not a set of joint angles; when the sculpt's
proportions cannot reach the pose, find the pose the same animal would
take in that body.**

**A clip belongs to the skeleton it was authored on.** The report was
"every dragon's jaw goes up into the skull on the ground, and the wyverns
revert to the old stance when I switch to them from the default asset".
Both were one bug, and neither was in the jaw or the stance: cycling the
player onto another model re-initialises the rig but left the previous
model's idle clip in place, and a model without a clip never cleared it.
A clip is sampled by joint INDEX, and on the ground the authored stance
wins the whole body, so a wyvern landed after `dragon.glb` stood in the
default asset's idle applied to a stranger's bones -- a reverted stance,
and a jaw hinge that was some other joint. `DragonRig::init` now drops the
clip and the app clears it for clipless models; a test re-initialises on
another skeleton and checks the clip is gone. The jaw itself opens downward
on the ground on every species (probe: grounded, breath held, tip in the
head frame), and two were rendered landed with the breath held to be sure.
**When a fault appears on every model at once, suspect state that outlives
the model.**

**Feet on a slope are still the flight model's problem.** The stance keeps
the lowest foot on one floor under the body centre, and the ground under
the body is sampled at one point; on a hillside the downhill feet float and
the uphill feet sink. Planting each foot on the terrain under it is a
per-limb IK problem (two-bone leg toward a terrain hit, body height and
pitch from the four contacts), the same machinery a physically settled
stance would need, and it is the next step if the grounded game matters
more than it does today.

### The second look: jaws, soles and the legs in flight

Three reports after the stance shipped, each caught by a render that had
been skipped.

**Rimefang's jaw over-closed in every scenario.** `jaw_rest_deg -20` drove
the mandible up through the upper teeth; rendered at -10, -13 and -16
side-on in the glide (`--inspect 90 3.5 0 --inspect-head`), -13 is where
the mouth just shuts. Tidewrack's snout warped as the jaw closed, which is
not a number: heat weighting had given its `jaw` bone the snout tip and a
smear over the skull top, the fault Rimefang and Ironroot were rebuilt for,
and it was rebuilt the same way on a worktree branch by a subagent
(`tools/skeletons/tidewrack.json`, `artifacts/tidewrack/jaw-mask/README.md`):
same 55 joints, inverse binds identical, jaw weight on the snout 18 mm of
displacement to zero. One caveat it found: outside the mask the wing and
chest heat weights differ from the Sep 11 build, and a rebuild with the
untouched shared skeleton gives the same difference, so the Sep 11 file came
from a toolchain state that no longer reproduces; the membrane pleats differ
by 0.4-1.8% of pixels. The old files are kept as `assets/*.sep11.bak`.
The probes that screened all of this live in `tools/rig_probes/`.

**"Feet below ground" on the flat bench was real, and small.** The stance
keeps the lowest foot JOINT on the bind floor; the paw mesh under a foot the
stance has rotated reaches below that joint. Measured exactly by skinning
the mesh on the CPU in the grounded stance and taking its lowest vertex
against the bind floor (a scratch probe, worth keeping): Rimefang 0.14 m,
Blightmaw 0.30, Ironroot 0.35, and the wyverns 2.1 and 2.6 m -- not the
feet but the TAIL, dropped 60 degrees against the lean and driven through
the ground. The wyverns' tail drop is -25 now, the remaining sink is the
hind toe, and each profile's `ground_lift_m` is the measured number; every
species now reads 0.00. **A joint on the floor is not a mesh on the floor;
measure the mesh.**

**The hind feet flew two metres above the hips.** Tuck and trail both
rotate the thigh aft -- the first joint of the chain receives both -- so
Rimefang's 80 of tuck over 60 of trail turned the thigh 117 degrees past
vertical and the feet pointed at the sky, and Ironroot's, whose skeleton
binds the thigh aft already, stood straight up. A glide probe printed the
thigh and shin angles and the foot's height and distance behind the hip for
a tuck x trail grid, screened for feet 0.6-1.4 m below the hips, 2.5-4 m
aft, thigh 40-70 aft and the shin not folded past horizontal, and the
survivors were rendered from behind and below in the glide and the flap,
which is the one view the near wing does not cover. The four quadrupeds now
stream their legs back under the tail base like a flying bird's; the
wyverns already did. Embercrest's had dangled instead, 1.9 m below, and it
took the same values. **When a chain gets two angles at the same joint,
check what their sum does before tuning either.**

### Each limb plants on the terrain under it

The flight model puts the body on the surface sampled at ONE point, so on a
hillside the downhill feet floated and the uphill feet sank, and every
stance above was tuned on a floor. The rig now takes the ground itself:
`DragonRig::set_ground(model_to_world, height)` hands it the renderer's
model transform for the frame and a surface query, and `plant_limbs` runs
at the end of the stance on ground contact. It works in two passes. First
the BODY: the contact error of every standing limb (the terrain under the
contact plus the profile's lift, minus where the contact is) is averaged
into a lift, and the fore/hind and left/right differences over their
spacing into a pitch and a roll, clamped to `ground_ik_tilt_max_deg` so a
boulder under one foot bends a leg rather than rolling the animal, and all
three smoothed so a foot crossing a terrain triangle edge does not snap the
body. Then each LIMB: a two-bone solve on hip-knee-foot, shoulder-elbow-hand
and, when the wrists are planted, shoulder-elbow-wrist -- the middle joint
bends about the limb's own plane until the chain's length matches the reach
(law of cosines), then the chain is aimed at the target with `aim_bone`,
twice, since each step disturbs the other. The foot beyond the ankle rides
the shin as posed. `ground_ik` is the weight; the Studio slider is "plant
limbs on terrain".

Tested on Rimefang over a ground rising 0.3 m per metre across the body:
a level body leaves the worst foot 0.50 m off its ground, the plant brings
it to 0.00. Landed in the game, rendered at ground level, the quadrupeds'
feet and the wyverns' wrists and feet all sit on the grass. The approach
is the rig's -- planting fades in with ground contact like the stance -- and
the ghost rig never sets a ground, so it is untouched.

This is the practical half of "physics-based postures". The other half, a
ragdoll settling under Jolt, would buy self-levelling the plant already
gives and cost the deferred physics integration plus a pose that is hard to
art-direct; it is not planned.

### Generated flight pose follow-up (2026-09-13)

The generated sculpts already contain bent knees and ankles. Their previous
flight tuck plus the inherited 30-degree foot hang and 16-degree toe curl
still lifted the paws too far. The six generated profiles now use smaller
species-specific leg folds and 10/8 foot hang/curl. The four newer models
also limit the wingbeat to 34 degrees with a 14-degree upstroke fan fold to
reduce horn crowding and wrist pinching. Tidewrack and Stormsail use a wider
40/10 dive sweep/fold because 75/45 crossed their long fingers over the spine.

See the [flight pose report](../artifacts/flight-pose-fix/README.md) for values,
before/after renders, remaining membrane creasing, and the 19-view-per-model
inspection set. `python3 tools/capture_flight_pose.py` reproduces the full-size
Metal checks, including actual flight, landing, and switching from dragon.glb.
