# Showcase: playtest feedback, and the motion it changed

The feedback tool, each round of the playtester's reports, and what each
mark became: the legs in flight, the wingbeat, the strikes. Part of the
showcase record; `docs/SHOWCASE.md` is the index. Sections are in the
order they were written, except the keyframed strikes, which are last.

## The feedback tool (2026-11-14)

The first playtest of the hero dragon came back as a paragraph: the legs
swing loose in an attack, an artifact at "the back joint", stiff glides,
lazy attacks. A paragraph does not say which frame, which joint or which
view. So the game now takes the report itself.

F2 opens the tool in any mode. The world holds still, or runs at an
eighth or a quarter speed, or steps one frame or ten. A click on the
picture leaves a numbered mark and each mark takes a note. Save writes a
folder under the user directory, `feedback/NNNN-summary/`:

- `frame.bmp`: the frame as it was, with the HUD and no marks.
- `marked.bmp`: the same frame with the marks.
- `report.md`: the summary, a table of the marks with their place as
  fractions of the frame and their notes, the model, the studio scenario
  and its time, the flight state, the camera, the frame number, and a
  command line that returns to the model, the scenario and the view.

While the tool is open the keyboard types notes and the mouse places
marks, and neither reaches the game: R does not restart the run and a
click does not breathe fire. A gamepad still does, because it is polled.

`game/feedback.h` is the report's data and text, with a test suite.
The capture is in `App::build_feedback_ui`. A save takes two frames with
the world held still, one for each picture, and no panel is drawn on
either.

## The first playtest of the hero, and the profile pass (2026-11-14)

The user flew the hero and reported: clearer shapes, but the animation has
not caught up. The legs are too loose, most of all when swinging in an
attack. There is a texture or geometry artifact at "the back joint". The
glide is stiff beside other dragon games, which shake slightly and swing
the tail. The attacks read lazy and mechanical.

`tools/showcase/strip.sh` renders one studio scenario at evenly spaced
frames and lays the crops side by side. A screenshot shows a pose and a
strip shows timing, which is what these complaints are about.

What the strips showed, and what changed in
`assets/embercrest-hero.glb.rig.cfg`. No engine code changed.

- The legs. `artifacts/showcase-anim/bite-legs-before-after.jpg` is the
  bite, frames 14 to 58, before above and after below. After the strike's
  surge the legs dropped to vertical and swung there for half a second.
  The sculpt's legs are long straight columns and the code's limb physics
  (stiffness 35, a 45 degree range) is tuned for the old folded legs.
  Stiffness is now 110, the foot is 0.85 as stiff as the hip, the range is
  14 degrees, and less of the body's motion is passed on. In frames 42 to
  58 the legs now hold their tuck.
- The tail. `tail-sway-before-after.jpg` is the glide from above, seven
  frames over three seconds. The travelling wave was off in this profile.
  Turning it on did nothing, at 9 degrees and at 40: the wave bends the
  spring's target, and the tail's spring is a quarter as stiff at the tip
  as at the base, so the tip never caught up with a 3 second wave. With
  the tip as stiff as the base and a 38 degree wave, the tip moves a
  tail's width to each side.
- The air. Gusts are doubled (2 degrees of roll and pitch, 0.18 m of
  heave), each wing trims 3.5 degrees on its own, and the membrane's
  flutter starts at 26 m/s, where it started at 55, above the 40 m/s
  cruise. These do not show in a still. A frame with them and a frame
  without differ by 3.8% RMS.

Not fixed by this pass:

- The attacks. The bite's strip shows a wind-up, a surge and a return,
  all present and all the same weight. The head barely leaves the frill,
  so the bite has no reach, and nothing snaps. The gesture layer is
  already a set of dials and has had two rounds of tuning. This is the
  case the user's decision on authored clips was made for, and it is S4
  and S5.
- The artifact at the back joint. The likeliest place is the wing root,
  where the worker reported a 0.4 m band of stretched texture and a dark
  crease, but "back joint" can also mean the hind hip. The user is asked
  to mark it with F2.

## The first reports from the feedback tool (2026-11-14, 23:08)

The user saved four reports. They are copied to
`artifacts/showcase-feedback/` as `NNNN-marked.jpg` and `NNNN-report.md`.

| Report | Mark | Note | Reading | Kind |
|---|---|---|---|---|
| 0001 | 1, hind hip at the tail root | "artifact" | A flat rectangular plate stands off the thigh, and a seam runs where the leg's hide meets the tail's | mesh and weights |
| 0001 | 2, fore paw | "back stretch too much?" | The paw bent back on itself: the hang and the toe curl stacked on a folded wrist | profile, fixed below |
| 0002 | 1, fore shoulder | "unnatural joint" | A hard dark crease where the tucked foreleg pinches the chest | weights at the shoulder |
| 0003 | 1, belly between the forelegs | "more geometry artifacts" | The chest spike the body sculpt came with, and jagged belly plates | mesh |
| 0004 | 1, flank | "low res texture and geometry" | The flank's scales are soft beside the neck's | texture share of the atlas |
| 0004 | 2, neck | "this looks better" | The head sculpt's plates | keep |
| 0004 | 3, wing | "wings are worse" | The first sculpt's soft wing surface, as the build reported | generate the wings alone |

The tool itself had two faults, both fixed. The report's flight state was
the idle flight model's and not the studio dragon's. Its command gave the
camera's world position, and the dragon had flown on, so the command
rendered an empty hillside. The command now gives the camera as an orbit
round the dragon (`--inspect`) and the studio's frame number.
`--inspect 270 6.5 0` is the user's side view.

What changed in answer, besides the report:

- The tail swings in time with the wings, which the user asked for.
  `tail_beat_deg` is new in the rig (`anim/dragon_rig.h`): the tail's base
  follows the wingbeat's curve a fifth of a beat late, with the gate the
  body's heave uses, so it is still in a glide. It is 0 for every other
  species and 18 for the hero. `artifacts/showcase-anim/tail-on-the-beat.jpg`
  is the flap, frames 120 to 162. The Dragon panel has "tail on the beat".
- The fore paw: no hang, half the toe curl, and a tuck of 50 degrees.
  Below 42 the legs trail long under the body.

The user's second look (2026-11-15, 00:00): the tail's beat was too
minor, and the slider could not be found. Both were true. The swing went
through the tail's spring, whose stiffness is 6 /s^2 against a wingbeat's
80, so a fifteenth of it arrived. The slider was in the Dragon panel,
which is behind the older-panels checkbox. The beat is now laid on the
tail's bones after the chain has run: one swing a beat, 40 degrees at the
tip, with `tail_beat_cycles` of a wave along the tail.
`artifacts/showcase-anim/tail-on-the-beat.jpg` has the first version above
and this one below. The Playtest panel's life section now begins with
"tail" and "air": the beat, its lag and wave, the glide's sway, the gusts
and the flutter.

The same look said the legs still stretch back too far beside the older
models, and that the leg joints are strange, and that the generated model
may be to blame. It is. The body's plates asked for "four straight legs",
so the sculpt's legs are columns with no visible knee, elbow or hock. The
rig's joints were placed on them by eye, and a tuck that folds a bent leg
only swings a straight one backward. The body is generated again from
plates with the legs bent as a standing animal's are.

The mesh and weight rows are the next asset round: wings generated alone,
the flank given more of the atlas, the chest spike and the thigh plate
removed, and the shoulder's weights smoothed. The rig is then rebuilt with
its script.

## The legs in flight, joint by joint (2026-11-15, 10:34)

Feedback report 0005 (`artifacts/showcase-feedback/0005-marked.jpg`) said
of version 4's fold: "don't like the new folding, refer online/other games
for more natural leg postures in flight". Its marks are the fore leg,
"unnatural folding", and the hind leg, "looks like standing in the sky".
Both were right. The hind Z kept the foot under the hip as on the ground,
and the fore tuck pushed the elbow up past the shoulder.

Flying quadrupeds in other games and in film carry the hind legs back
along the root of the tail, loose at the knee, and hold the fore legs to
the chest. One tuck value with alternating signs cannot do that, so the
rig has a pose by joint: `leg_flight_pose 1` and an angle each for hip,
knee and hock, shoulder, elbow and wrist, positive aft, with
`leg_flight_splay_deg` for the roots. With it on, the tuck and the trail
are not applied; the brake's reach and the pendulum still ride on the
root. A profile without the keys is unchanged.

The hero carries hind 45, 5, 35 and fore 65, -140, 105, from two rounds
of candidates (`artifacts/showcase-anim/flight-legs-pose-candidates.jpg`).
`flight-legs-pose-before-after.jpg` is the bite from the side, the fold on
the left; `flight-legs-pose-views.jpg` has the glide from the side and
the front, the dive, the brake, a landing and the stand. The Dragon
panel has "flight pose by joint" and its seven sliders under "front leg
tuck".

From behind, the chase camera's side, the trailing hind feet sat wide of
the tail. `leg_flight_splay_deg` is -18: the legs lie along the tail's
root without touching it. `flight-legs-splay-candidates.jpg` has -10, -18
and -26 from behind, above and the front.

Open: from the front the fore paws show their spread claws under the
chin. The trailing-fore variant (candidates Q5 and Q6) is the other
natural choice if the tuck reads wrong in play.

## The playtest's second round of reports (2026-11-15, 11:30 to 11:37)

Reports 0006 to 0012 are in `artifacts/showcase-feedback/`. The playtester
also said of the flight postures: "I don't like any new folding postures,
maybe use old dangling and keep these as alternative postures for future
adjustments".

| Report | Mark | Kind | Answer |
|---|---|---|---|
| 0006, 0007 | fore leg, "unnatural bending", "look at the bending!" | pose | the dangle, below |
| 0006 | belly, "geometry artifacts"; flank, "not completely fixed" | mesh | open: the next mesh round |
| 0007 | wing, "self shadow aliasing" (low priority) | shadows | open |
| 0008 | "water artifacts" close to the river, and 37 fps there | water | fixed, below; the cost is down 4 ms of 16 |
| 0010 | "strange whirls?" on the river from a distance | water | fixed, below |
| 0009 | "vegetation quality can be improved": the bushes and the grass | plants | the bush and the tuft rebuilt, below; ground cover is S7 |
| 0011 | no tree shadow inside the mountain's shadow | lighting | open; one route tried and ruled out, below |
| 0011 | "rocks look unnatural" | rock | open: S6 |
| 0012 | "mountain shapes seem weird" | terrain | first step done: the spurs, below |

The dangle is the hero's default again: hind 30, 20, 30, fore 25, -20, 45,
splay -6. The legs hang a little aft of where they stand with the feet
drooping, and the limb physics swings them.
`artifacts/showcase-anim/flight-legs-dangle-candidates.jpg` has the six
tried and `flight-legs-dangle-views.jpg` the choice in the glide, the
flap, from behind, in the bite, the dive and landed. The other postures
are the Dragon panel's "flight leg posture" presets (dangle, trail and
tuck, trail both, fold), and their numbers are in the profile's comment.

## Feedback 0013 to 0015, and the turn to the dragon (2026-11-15, 17:20)

The user's direction after round 3: the visual showcase first and gameplay
later; ignore what is not in the final arena or run (the course rings, the
placeholder sphere); for the dragon's animation, study established media
(Spyro, Glyde and others) so the wingbeat and the strikes look natural;
and the breath and the projectiles are "still too simplistic", so look at
classic fire implementations and papers.

Three feedback reports came with it, six marks:

| Report | Mark | State |
|---|---|---|
| 0013 | "transparent artifacts?" on the membrane's root panel | done, "The wrist hook and the root panel" below |
| 0013 | "prefer to dangle like front legs as well" on the hind leg | done, then withdrawn: "The legs trail and tuck" below |
| 0014 | "legs are ragdoll in melee attacks" | done, "Strikes that hold, and legs that act" at the end of this file |
| 0014 | "tail too" | partly: the body and the clip now end together; the tail's own motion is unchanged |
| 0015 | "malformed wing claw" | done, the same section |
| 0015 | "wings can have a larger flapping range" | done, "A deeper, slower beat" below |

### The hind legs hang (17:20)

The fore legs hung and the hind shin lay level along the tail: the dangle
was hip 30, knee 20, hock 30, every joint turned aft. Now hip 8, knee -45,
hock 45: the thigh a little aft of where it stands, the knee opened so the
shin hangs straight down, the foot turned aft at the hock. The feet hang
2.3 m under the hips where they trailed 0.9 m under them. Chosen from six
rendered candidates (`artifacts/showcase-feedback/
0013-hind-legs-candidates.jpg`). The old values are the Dragon panel's
"dangle, hind legs trailing" preset.

The pair, from the playtester's own view and from the front:
`0013-hind-legs-hang-before-after.jpg`. The glide from behind and above,
the beat, the brake, the dive and the chase camera:
`0013-hind-legs-hang-other-views.jpg`.

`--rig key=value,...` is new: it sets rig profile fields for one run, by
the profile's own names. The six candidates were six commands.

### The wrist hook and the root panel (18:15)

A Codex job (gpt-6-astra, high, 298 k tokens, branch `hero-claws`, merged
as f8ce6cb) rebuilt the hero's wings in the build scripts.

- Each wrist carried three projections, 1.12, 0.84 and 0.30 m long. Now
  one hook a side, 0.42 m straight and 0.49 m along its curve, 0.10 m
  across at the base. The finger claws went from about 0.40 m to 0.30 m.
- The root panel, between the flank and the arm, had no colour detail and
  no normal relief. That was the pale ruled patch of feedback 0013; alpha
  blending and doubled faces were not the cause. It now carries the
  membrane's grain.

The pairs on the playtester's own views are
`artifacts/hero-claws/pair-feedback-wrist.jpg` and
`pair-feedback-root.jpg`. Codex's mesh checks pass 112 of 112 and its rig
checks 74 of 74. One limit remains: at the panel's narrow texture margins
the detail stretches by up to 257 mm.

### The legs trail and tuck (19:00)

The playtester on the hang: "I find the dangling legs worse, maybe the
previous tucked version is better? please give me some options to choose
from". Six postures were rendered side by side from the side, the front
quarter and below (`artifacts/leg-options/options.jpg`):

| | Posture | Hind hip, knee, hock | Fore shoulder, elbow, wrist |
|---|---|---|---|
| A | hang | 8, -45, 45 | 25, -20, 45 |
| B | hind bent under the belly (before 17:20) | 30, 20, 30 | 25, -20, 45 |
| C | half hang | 18, -12, 38 | 25, -20, 45 |
| D | trail and tuck | 45, 5, 35 | 65, -140, 105 |
| E | trail both | 45, 5, 35 | 40, 60, 30 |
| F | the older fold | (tuck and trail) | |

The playtester chose D: the hind legs trail back along the tail and the
fore legs fold to the chest. It is the hero's profile now
(`assets/embercrest-hero.glb.rig.cfg`). The inspection set is
`artifacts/leg-options/chosen-trail-and-tuck.jpg`: the glide from the
side, behind, the front and above, the beat, the chase camera, landed, and
the beat from below.

The realistic references hang the hind legs (`docs/MOTION_REFERENCE.md`),
and the hang was built from that. On this model it read as standing in the
air: the references are slimmer animals. A reference decides what to try.
The render on our own model decides what stays.

The playtester also asked for the postures in the panel, and for the
Playtest panel to hold only what is open for tuning. So:

- "Legs in flight" is a header of the Playtest panel: the six postures in
  one list that names the current one ("custom" once a slider moves), the
  seven joint sliders, and a save button
  (`artifacts/leg-options/playtest-panel.jpg`).
- The studio's "leg tuck" and "leg trail" sliders are gone from the glide,
  beat and dive scenarios. With the joint posture on, the rig did not read
  them.
- "Tail, limbs and strikes" keeps the tail, four limb physics dials and
  the strike clips. The air, the limbs' finer terms, the work and the
  brake clip are in the Dragon panel, behind "show the older tuning
  panels".
- "Camera and light" is last and closed.

### A deeper, slower beat (19:08)

Feedback 0015: "wings can have a larger flapping range". The reference
study (`docs/MOTION_REFERENCE.md`) gave four faults: the downstroke too
short, no fold going up, no tip lag, a shallow stroke.

Two things hid the stroke before it could be tuned.

- The hero's profile capped the shoulder at 42 degrees a side
  (`wing_flap_limit_deg`), whatever the flight model asked for.
- The studio built its beat from the default flight tuning, so a species'
  own beat, and `--flight`, never reached `--studio 1`. `studio_state` now
  takes the player's tuning.

| | Before | After |
|---|---|---|
| Beat period | 0.9 s | 1.1 s |
| Downstroke's share | 40% | 50% |
| Stroke the flight model asks for | +50 to -42 degrees | +62 to -50 |
| Shoulder limit | 42 degrees | 66 |
| Hand's lag behind the arm | 3.5% of a beat | 10% |
| Recovery elbow, wrist, fingers, droop | 11, 17, 13, 15 degrees | 16, 26, 18, 20 |
| Recovery fold, and the phase it opens by | 10 degrees, 0.68 | 14 degrees, 0.82 |
| Peak flap force | 9000 N | 7200 N |

The peak force is lower because thrust comes from the downstroke alone,
and the downstroke is 1.25 times as long. Held flap from the spawn, speed
after 20 s: 50.6 m/s before, 50.0 after.

The pair at three phases, from the front and the side:
`artifacts/wingbeat/0015-beat-before-after.jpg`. At the bottom of the
stroke the tips are now well under the belly; before, the wing stopped
near level. Mid upstroke the hand folds in; before, the wing rose open.
Ten phases of each: `0015-beat-ten-phases-before.jpg` and `-after.jpg`.
From above, the head, and the chase camera at both ends of the stroke:
`0015-beat-other-views.jpg`.

One candidate folded harder (wrist 45, fingers 30) and read as a bat: the
span fell under half. The fold kept is still deeper than the reference
dragons' 20 to 30% of span at its deepest frame. The studio's beat
scenario has "recovery wrist" and "recovery fingers" sliders for it.

Not measured: the beat's shares on frames, as the reference study measured
them. The numbers above are the settings.

## S4 and S5, first pass: keyframed strikes (2026-11-15, 11:20)

The engine side is `DragonRig::set_attack_clips`: five optional clips from
`assets/clips/<model>.attacks.glb` (`bite`, `claw_l`, `claw_r`, `tail_l`,
`tail_r`), each played once from its action's edge and added to the
finished pose after the head aim. The brake-bank trial put its clip under
the limb physics and it was smoothed away; a strike goes on last. Under a
playing clip the procedural neck, limb, wing and tail gesture drops to
`attack_clip_under` (0.15) and the bite's jaw is the clip's. The body's
pitch, roll, yaw and surge stay procedural, because a clip cannot move the
root.

Codex (gpt-6-astra, high, work account, 123k tokens, 18 minutes) authored
the hero's clips from `tools/clips/hero-attacks-brief.md`:
`tools/clips/make_hero_attacks.py`, a checker with 133 passing checks, and
`tools/clips/hero-attacks.md` with the measured axis table. Each clip has
a wind-up, a fast strike, an overshoot and a slower recovery, with the
joints down a chain peaking after their parents.

In the engine at full weight the clips overshot: the claw twisted the head
past the shoulder, the tail whip curled under the body, and the bite stood
the tail upright. The hero's profile plays them at `attack_clip 0.6`.
`artifacts/showcase-anim/strike-clips-bite-before-after.jpg` has the bite
at three frames, the procedural gesture above and the clip below;
`strike-clips-in-engine.jpg` has bite, claw and tail at four frames each.
The Dragon panel has "strike clips", "strike clip rate" and "procedural
gesture under a clip".

Not judged: how the strikes feel in motion and in a fight. Stills show
the poses and not the timing. The claw's large roll is the body's own
gesture and was there before.

## Strikes that hold, and legs that act (2026-11-15, 19:55)

Feedback 0014: "legs are ragdoll in melee attacks", "tail too". The
reference study (`docs/MOTION_REFERENCE.md`) named what was missing: a
hold at the contact, and limbs and a tail that take part.

What the code did before:

- The body's gesture (pitch, roll, yaw, surge) lasted 0.55 s for a bite or
  a claw and 0.75 s for the tail. The keyframed clip on the neck, wings
  and tail lasts 0.9 s, or 1.0 s for the tail. The body was back at rest
  while the clip was still recovering.
- The contact was held for 0.12 of the time after the wind-up: 0.05 s,
  three frames. The references hold 16 to 48 frames.
- The legs kept their flight pose through every strike.

What changed, in the rig and in the hero's profile:

| | Before | After |
|---|---|---|
| Bite and claw gesture | 0.55 s | 0.9 s, the clip's length |
| Tail gesture | 0.75 s | 1.0 s |
| Wind-up | 0.14 s (0.19 s tail) | 0.15 s (0.17 s tail) |
| Strike out | 0.12 s | 0.12 s |
| Contact held | 0.05 s, 3 frames | 0.19 s, 11 frames |
| Recovery | 0.24 s | 0.44 s |
| Legs | flight pose | below |

The wind-up is the same length on purpose: a strike lands when it did.
The combat code does not read these durations, so no hit timing changed.

The legs (`gesture_legs`, 0 to 1, off by default, 1 on the hero; it works
on a model posed joint by joint in flight):

- Bite: the hind legs draw up as the head rears, then kick back straight
  on the lunge. The fore claws thrust forward with the jaws.
- Claw: the hind legs drop and spread to brace, at half strength.
- Tail: the hind legs draw up in the coil and brace as the tail goes
  across. The fore limb on the far side reaches out.

Two shares that were constants are rig fields now: `gesture_strike_out`
(0.3) and `gesture_hold` (0.12). The defaults keep every other species as
it was. The studio's melee scenarios have three new sliders: "strike out
share", "contact hold share" and "legs in a strike".

The pairs, each strike at three moments from the side:
`artifacts/melee/0014-strikes-before-after.jpg`. At 28 frames after the
press the bite is still at full reach with the claws out; before, it was
half way back. The bite from behind and at the head, and the tail from
above and the front: `0014-strikes-other-views.jpg`.

Not done: an anticipation as long as the references' (20 frames and
more). That delays the hit and is a gameplay decision. The tail's own
motion in a bite or a claw is the clip's and was not changed. The stills
show poses; the timing needs a playtest.

## The beat, second pass: faster and deeper (2026-11-15, 22:58)

The playtester, on the retuned beat: "the wingbeat is slow, and the
flapping range is still limited".

Measured from the front (`--studio 1 --inspect 180 17 6`), as the angle of
the wingtip about the shoulder through one beat: +40 to -13 degrees, 53 in
all, on a flight model that asked for +62 to -50. The rig spends much of
the commanded angle on the fold and on the hand that trails, and the
bottom of the stroke was shallow: the tips barely went under the body.

| | Before | After |
|---|---|---|
| `flap_period` | 1.1 s | 0.85 s (1.18 beats a second, Century's rate) |
| `flap_up_angle_deg` | 62 | 66 |
| `flap_down_angle_deg` | -50 | -78 |
| `wing_flap_limit_deg` (rig) | 66 | 90 |
| The tip, from the front | +40 to -13 | +48 to -28, 76 in all |

The gain is at the bottom. The top is limited by the wings themselves:
at +90 they met over the head in a heart shape, and at +74 the tips still
touched for one frame seen from behind. Speed with the flap held is
unchanged (49.8 m/s after 15 s, both ways), so `flap_peak_force` stays.

`artifacts/wingbeat/second-pass-before-after.jpg` has five frames of each
beat from the front. The beat was looked at from the side, the rear and
above, and in the chase view with the flap held. The roster's goldens did
not move: the change is in the hero's two profiles.

## Feedback 0016: the fore legs trail, and a camera in F2 (2026-11-20, 23:38)

Report 0016, on the brake: "front legs unnatural folding. if cannot fix,
use the backwards folding". In the chest fold ("trail and tuck", chosen
on 2026-11-15) the elbow turned 140 degrees. The forearm doubled over
the upper arm and the skin cracked at the shoulder. A milder fold (50,
-95, 60) read as reaching forward. No angle mended it; new skin weights
would be an asset job.

The hero's profile is "trail both" now: fore shoulder 40, elbow 60,
wrist 30. The fore legs lie back along the belly. Before and after from
the report's camera: `artifacts/feedback-0016/forelegs-before-after.jpg`.
The inspection set (side, rear, front, above, the beat, landed, the chase
camera, the brake): `artifacts/feedback-0016/forelegs-inspection.jpg`.

The playtester also asked for F2 to be navigable like a camera mode. The
feedback tool's view is the free camera now, detached where the view was
when the tool opened. Hold the right mouse button to look. W A S D move,
Q and E go down and up, Shift is fast. The keys move the camera only
while no note has the keyboard. The camera moves on the frame's real
step, since the tool holds the world's. A mark stays where it is on the
screen, so frame the view before marking. Closing the tool returns to
the view it was opened from. Not yet tried in a window by the playtester.

## Feedback 0017 and 0018, and the third round's small asks (2026-11-21, 00:58)

- 0017, "can tuck slightly lower like this", marked on the fore leg in the
  brake. In the glide the fore leg lay pressed into the belly. The
  shoulder went from 40 to 25 degrees, and the leg hangs a hand's width
  under it. `artifacts/feedback-0016/forelegs-0017-lower.jpg`: before,
  after, the side, the brake.
- 0018, "strange shadow" and "shadow artifacts (picky though)", on the
  wing's underside. The light through a membrane took one shadow lookup,
  and the map's texels showed as a stair along each shadow's edge. It
  takes nine now, over 0.2 m. The stair is a soft band, not gone.
  `artifacts/feedback-0016/membrane-shadow-0018-before-after.jpg`. The
  first mark's panel between the arm and the flank is new geometry in
  mesh version 6 (`HERO.md`).
- "a charged shot is wrongly followed by a regular when releasing the
  button". A hold that had let a full charge go still counted its release
  as a tap. `Combat` remembers that a hold has fired; a test in
  `tests/test_combat.cpp` holds past full and lets go.
- "reduce wind lines and boost effects". Thermal wind lines: 5 a second a
  column (14), 14 to 22 m long (22 to 36), at 0.6 of their brightness.
  Speed streaks: from 65 m/s (55), 40 a second (120), at 0.6. The boost's
  wingtip threads, its streaks and its zoom scale with a new "boost
  effects" slider (Camera panel, "Field of view"), 0.4 by default: 36
  threads a second a side (90), a streak for every two, 9 degrees of zoom
  (14).

## The beat, third pass: the stroke reaches under the body (2026-11-21, 00:58)

The playtester: "the flapping range can increase more, check out other
sources". The profile said 66 degrees up and 78 down. On screen, from the
front, the tips stopped about 35 degrees under the level. The rig's
`wing_flap_limit_deg` was 90, and the hand lags the arm, so the outer
wing never saw the angle the flight model asked for.

What the sources give is in `docs/MOTION_REFERENCE.md`, "Wingbeat
amplitude". In short: take-off and climb use the largest strokes, the
downstroke below the level is what makes thrust, and a large animal
beats slower.

Now `flap_up_angle_deg` 72, `flap_down_angle_deg` -125 and the ceiling
130. The tips reach about 55 degrees under and curl in at the bottom. At
-145 the hands met under the belly. Eight frames of the beat from the
front, before over after: `artifacts/wingbeat/deeper-stroke-before-after.jpg`.
Also looked at: the side at the bottom of the stroke, from above, the
chase camera with the flap held, landed, the pull-out.

Not done: one stroke serves cruise and climb. The sources want a
shallower cruise beat (125 to 140 degrees in all) and the deep one for
take-off and climbing.

## The fourth round: report 0019, paper wings, the beat by effort (2026-11-22, 00:20)

The playtester's words, 2026-11-21, 23:25: "sorry 'thin' I meant they are
like paper, not broadness, please revert. wingbeat range is better now
but a bit unnatural (on the old model). elemental breaths still look
strange in default view (not side view) ... they still look like clouds,
e.g. frost doesn't seem like ice, tide unlike water. storm is pretty
good. please remember to check out new feedbacks each time."

A session now reads the feedback folder at the start of every round,
asked or not. Report 0019 had been saved three hours before the message
and the message did not name it.

### Report 0019: "weird shadows"

The view was a slope past the playable extent (the dragon stood at
z 2968; the extent ends at 2500), striped in pale bands hundreds of
metres long. Four suspects were tried and cleared first: the terrain's
shadow march, the gully colour, the scree colour and the mist. Leaving
out `crown_shade` removed the bands. The terrain shader clamped its
lookup of the crown shade map (the soft blot under each tree), so the
map's edge texels ran on over all the ground outside it. The shade now
fades to nothing at the map's edge. Pair:
`artifacts/feedback-0019/crown-shade-edge-before-after.jpg`.

### "Thin" meant paper

Mesh version 6 answered the wrong fault and is withdrawn
(`docs/showcase/HERO.md`, "Version 6 withdrawn, version 7 asked for").
Version 5c is the hero again.

### The beat, fourth pass: the stroke's depth follows the effort

The third pass deepened the stroke to 125 degrees for every beat. From
behind, the wings then hung under the body for a third of a level beat,
and from the side they swung under the chest. Three changes:

- The depth follows the effort (`FlightTuning::flap_cruise_down_angle_deg`,
  `flap_effort_speed`, `flap_effort_climb`). Slow, or climbing at 8 m/s,
  the downstroke ends at 125 as before. Level at 30 m/s and faster it
  ends at 85. The effort eases over about a third of a second. A species
  without the cruise key keeps one depth.
- The hand's droop on the way up is 12 degrees (was 20).
- The stroke plane's tilt is 14 degrees (the code's default is 22).

On screen the cruise beat's tips stop about 25 degrees under the level
and the climb beat's about 55. The studio's flap (`--studio 1`) is the
cruise; `--flight flap_effort_bias=1` shows the climb. The Flight panel
has "stroke up", "stroke down, climb", "stroke down, cruise" and "cruise
from". Sheets, eight frames a beat, before on top:
`artifacts/wingbeat/effort-before-after-rear.jpg` and `-side.jpg`.

Not judged in motion: the sheets are stills. Whether the beat now reads
as natural is the playtester's call. If it does not, the next suspects
are the fold at the top of the stroke (the wing is still folded when the
downstroke starts) and the even 50% split between the two half-strokes.

### The breaths

`docs/showcase/EFFECTS.md`, "A second style for each breath of matter".

## The fifth round: reports 0020 to 0024, the deep stroke back, the breath in the studio (2026-11-22)

The playtester, 2026-11-22 10:20: "please revert wingbeat change, deep
stroke looks better. please make breath usable in animation studio.
strange half transparent lens like is visible in flight but not
paused/stopped. the gap in feedback might be a bug in F2. breaths just
look bad in motion in general. please implement the breath options, we
can revert if bad." And later: "lens artifacts are in the breath I mean".

Five reports came with it, each one breath seen from the side in the
feedback tool, on `embercrest-hero-v5c.glb`:

| Report | Mark | Answer |
|---|---|---|
| 0020 | "gap is too large" (between the jaws and the flame) | Not a fault of the tool. See below |
| 0020 | "artifacts, unlike fire" | The square holes: `EFFECTS.md`, "The fire's square holes" |
| 0021 | blight: "too white, still cloud like instead of acid/poison" | Style 2, a stream of acid |
| 0022 | tide: "similarly cloud like, unlike water" | Style 2, a stream of water |
| 0023 | stone: "more like sand instead of stone, might be fine" | Style 2, boulders and rubble |
| 0024 | frost: "cloud like" | Style 2, ice lances; style 3, frozen air |

### The wingbeat

The cruise stroke of the fourth round is withdrawn. In the hero's
profiles `flap_cruise_down_angle_deg`, `flap_effort_speed` and
`flap_effort_climb` are gone, so every beat is the full 125 degrees
again, `recovery_droop_deg` is 20 again and the stroke plane's tilt is
the code's 22. The effort term stays in the code, off at 0, and the
Flight panel keeps its sliders.

### The breath in the studio

The animation studio skipped the whole fight, and the breath with it.
The Playtest panel's studio section has a "breathe" box and an element
list beside it. While the box is ticked the studio's dragon holds its
breath from its animated head, in any scenario, grounded or flying, with
no fuel to run out. `--studio N --breathe --element NAME` does the same
on a capture. The breath's own code is one function now
(`App::feed_player_breath`), called by the fight and by the studio.

### The gap in the feedback tool

The tool holds the world still by stepping it a millionth of a second a
frame. With the breath held through F2 (`DRAGON_FEEDBACK_TEST=100
--breathe`) the saved frame has the jet on the jaws, with no gap. The gap
in the five reports is the time between letting the breath go and
pressing F2: the jet flies on at about 100 m/s, so a sixth of a second is
15 m. Holding the breath while pressing F2 keeps it on the jaws.

### The lens shapes

Not found for certain from stills: the playtester sees them in motion
and not in a held frame. The only soft discs in a breath are the smoke
puffs off its far third, the second styles' chips and the ground cloud's
puffs, each a round blended sprite. Styles 2 and 3 draw none of the
three. If the shapes are still there in style 2, the tool's 1/8 speed
can hold them for a mark.

### The breaths

`docs/showcase/EFFECTS.md`, "Breaths of matter as pieces".

## The sixth round: the breaths aimed and mixed, a breath range, five rigs, the wilds (2026-11-22, 13:00)

The playtester, 2026-11-22 11:55: "bug: animation studio breath element
wrong if switched with U. all elements can combine with style 1 a bit,
make particles smaller (frost and stone), frost style 3 can also mix.
blight looks too similar to water, the arc makes it harder to aim, looks
good though. for interaction consider plants and other objects
destruction too, and create a studio/scenario for testing. please update
damage cone and sfx. can also have low/no damage breath/magic that has
stronger status effects like sleep/paralysis/etc. might not be a good
idea. feel free to use codex to rig new models and other 3d intensive
jobs. we need a new survival/exploration/rpg mode for new mechanics.
please grill me in the end". No new report came with it: 0024 is still
the last.

| Ask | Answer |
|---|---|
| The studio's element is wrong after U | Fixed. U left its swap flag set, and the studio's list set only the first choice. Picking from the list now clears the swap (`App::choose_player_element`), and the list shows the element in use |
| Mix style 1 into every breath; smaller bits; frost 3 too | `mix` (0.35), the Breath section's "cloud with the pieces": that share of the cloud rides with the pieces, on all four breaths and on frozen air. The frost's and the stone's bits are smaller |
| Blight looks like water | The acid is a thick liquid now: gobs on a thread, yellow-green, no white air in it, with a thin gas about it. The water stays a hose's stream that whitens |
| The arc is hard to aim | The throw is solved for the aimed point (`BreathMatter::solve_throw`), and a ring on the HUD shows where the pieces will land |
| The damage cone | A breath of pieces lands its damage piece by piece (`Combat::breath_piece_hit`), each piece carrying its share of a second of the breath. The cone is the cloud breaths' only |
| The sounds | A piece that lands is heard: ice shatters, rock cracks, water splashes, acid hisses, quieter with distance. The studio's breath has its voice. None of this was heard: every check was headless |
| Plants and other objects | A mark on the ground reaches the plants that stand in it: fire strips and chars, frost rimes, blight strips and withers, tide darkens, stone fells them away from the hit. Rocks take the marks too. Towers, caches and landmarks do not. Far trees (the impostors) do not, and a plant's shadow does not follow it |
| A scenario for testing | Studio 14, the breath range: the dragon stands and sweeps, four dummies on the left with the fight live, open ground and plants on the right, no fuel limit. `--studio 14 --breathe --element NAME` |
| Status breaths | Not built. `docs/DIRECTION.md`, "Status breaths, not built" |
| Codex for rigging | Two jobs rigged the five second-round dragons. `docs/MODEL_GENERATION.md`, "The five rigged" |
| A survival, exploration, RPG mode | A first version, `--wilds`. `docs/DIRECTION.md`, "The wilds" |

The breaths' record is `docs/showcase/EFFECTS.md`, "The pieces mixed,
aimed, heard, and what they do to plants". Its sheets are
`artifacts/breath-volume/mixed-chase-and-side.jpg`,
`range-six-breaths.jpg` and `range-plants.jpg`.

### The lens shapes, a second guess

The fifth round blamed soft sprites. This round found a tinted wedge of
light on the ground under the four breaths of matter: each had the
flame's local light, which a stream of water has no reason to throw.
The pieces and the frozen air throw none now. Whether that was the
shape the playtester saw is not known.

### What was checked and what was not

Checked by render: each breath in the range from behind and from the
side, the plants before and after, the five rigged dragons landed and in
the glide beside the hero, the wilds at noon, dusk, midnight and dawn.
Checked by the demo pilot: the wilds for five minutes of play.

Not checked: any sound, any of it in motion at a player's frame rate,
the wilds with a hand on the controls, a night with its hunter fought,
the save read back in a window (a headless run does not write it).

## The seventh round: reports 0025 to 0027, the breath straight, a start screen (2026-11-22, 15:30)

The playtester, 2026-11-22 14:16: "I don't like arc and second aim at
all, try to remove arc and mix into cloud style. the frost/stone
particles are still too large, no need to deal damage for each piece.
some trees (imposters?) don't react to breaths. I don't like frost and
storm new designs. for frost the color is too into aurora, keep
white-blue, aurora only on edges like previous concepts, and spikes wof
icewing style. for storm the old wyvern design is good, just improve on
it. let me check before modeling. also new models flapping is very
mechanic, need more animation work. please just add all to rosters and
add a toggle to compare old/new designs. please add a more friendly
start screen to choose mode (practice/arena/hoard/wild) and other
end-user related settings. please use ` instead of f1. for wilds I'm
more ambitious ... discussion only for now".

Three reports came with it.

| Report | Model | Mark | Answer |
|---|---|---|---|
| 0025 | blight2, flapping, from behind | "moves while flapping", on the haunch | Wing bones held 88% of that skin's weight, and it travelled 2 m a beat. A Codex job measured and removed it: `MODEL_GENERATION.md`, "The wings' weight on the body" |
| 0026 | tide2, the same view | "moves while flapping" on the thigh's fin, "moves too" on a neck fin | The same |
| 0027 | the hero, storm breath, from behind | "breath range hard to see" | Each arc ends in a flash. `EFFECTS.md`, "The storm's reach" |

| Ask | Answer |
|---|---|
| No arc, no second aim, into the cloud, smaller, no damage by the piece | Done. `EFFECTS.md`, "The pieces straight, in the cloud" |
| Trees that do not react | They were not impostors. A breath through the crowns left no mark. Fixed. `EFFECTS.md`, "Trees in the breath" |
| Frost and storm designs | Six new sheets, three each, for the user's pick. Nothing modelled. `MODEL_GENERATION.md`, "The third round" |
| The new models flap mechanically | The hero's wingbeat and life on all five profiles. `MODEL_GENERATION.md`, "The wingbeat of the five" |
| All in the roster, with a toggle | The default roster carries both designs of six species. O, the Playtest panel's "new designs" and the start screen swap the player and every rival |
| A start screen | `--menu`, and a launch with no arguments. `DIRECTION.md`, "The start screen" |
| ` for F1 | Done. F1 still works |
| The wilds, more ambitious | A proposal, nothing built. `DIRECTION.md`, "The wilds, second discussion" |

### What was checked and what was not

Checked by render: the four breaths of matter from the chase and the
side, the range with four elements, the start screen, each of the four
modes started from it, six frames of the wingbeat from behind and from
the side on each of the five models.

Checked by soak: a match of six rivals with the designs swapped every
150 frames for 1,300 frames.

Not checked: anything in motion at a player's frame rate, the start
screen with a hand on the keys or a mouse, the settings file read back
in a window, a sound.

Seen and not fixed: six rivals breathing at 60 m fill the frame with
the old puffs. The rivals' breaths are still the particles the player's
had before the marched volume. It was so before this round.

