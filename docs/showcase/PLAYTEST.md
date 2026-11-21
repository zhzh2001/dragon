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
