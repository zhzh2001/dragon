# Combat, bots and the match loop

Fire, targeting, the AI that flies against you, and the scoring around it.

## Combat

`game::Combat` owns player resources, projectiles and the targets, and reads a
`CombatInput` -- so a bot will drive it through exactly the same struct the
player fills. It renders nothing and reads input from nothing.

### Targeting

Aiming a small fast target in three dimensions is close to impossible unaided:
a degree of nose error is tens of metres at engagement range, and the target is
manoeuvring too. So the dragon **picks a target and the shot bends toward it**.

- The lock is **sticky**: acquired only inside a narrow cone off the nose, held
  until it falls well outside a much wider one, so a turn does not drop it.
- Scoring is **angle plus a distance penalty** (`lock_distance_weight`):
  alignment alone locked a 1500 m speck dead ahead over a close target ten
  degrees off the nose, which is never the one the player meant. T or a
  right-stick click **relocks** onto the next candidate by score, wrapping.
- `aim_assist` is the fraction of the way from the nose to the intercept, and it
  is **0.9 by default**. What the player experiences is the *residual*: at 0.7 a
  shot 14 degrees off the nose at 500 m still misses by 37 m, which reads as the
  assist doing nothing. Turn it down for a harder aiming game, not to be fairer.
- The aim solution leads the target **and compensates for the drop**. At 700 m
  the flight time is 2.7 s and the fireball falls 15 m -- more than the target is
  tall, so without this every long shot passes underneath for a reason the player
  cannot see.
- The **breath cone follows the same assisted axis**, so the flame drawn is the
  flame that damages -- no hidden widening. Its assist is additionally capped by
  angle (`breath_assist_max_deg`), because a fireball bending 30 degrees is
  invisible while a flame doing it looks like a garden hose.
- The rig **turns the head toward the lock** (`DragonRig::set_aim_target`). This
  is readability, not flourish: fire leaves along the aim axis, and a head
  pointing elsewhere makes the shot look like it came from nowhere.

Being hit has to be locatable. `CombatEvents::damage_from` reports where the
round came from, the HUD holds an arc at the screen edge pointing at it for three
seconds, and **incoming fire is drawn about three times its true size** -- a
2.5 m hitbox at 400 m is a couple of pixels, and being hit by something invisible
is the least readable thing in the game. Player fire needs no such help.

Three decisions that are the milestone:

- **Hitboxes are generous and swept.** A fireball covers 3.5 m per frame at
  210 m/s; a point test tunnels straight through a target it visibly struck, so
  every hit is a swept-sphere test against the segment the projectile actually
  travelled. A near miss still lands reduced damage out to the blast radius. A
  3D dogfight is hard enough to read without demanding pixel accuracy, and a
  shot that clearly hit but did not is the worst thing an air combat game can do.
- **Boost is a flight force, not a combat one.** `Combat` owns the cooldown and
  reports `boost_active()`; `FlightInput::boost` applies it. Everything that
  pushes the dragon forward stays in the flight model.
- **The breath meter latches and does not refill while held.** Without both, an
  empty meter under a held button crosses the restart threshold every few frames
  and produces a stutter of single-frame damage.

**Tuck commits the nose** (`Assists::tuck_nose_over`): folding the wings only
sheds lift, and at level attitude that is a slow flat mush -- the dive button
dived slower than pushing the stick. Holding tuck now pitches down unless the
stick overrides; measured, RT alone reaches 78 m/s in 8 s where stick-down
alone reaches 57. A **hit flashes hot orange**, because the other bright thing
a sentinel does -- firing -- puts a blue-white bolt on top of it, and two white
flashes are indistinguishable at range.

**One gamepad button, one meaning.** The shoulders once carried rudder and
combat simultaneously: firing a fireball also yawed the dragon, and holding
breath dragged it into a slip that bled energy -- which the player read as
"auto-flap is broken", not as a binding conflict. Rudder lives on the d-pad
now. And auto-flap also protects against unintended sink (descending fast with
neither tuck nor brake held): a fight at healthy airspeed glides steadily
downhill, and a pilot busy aiming does not notice until the ground does.

Projectiles are drawn as **one opaque bolt** stretched along velocity, its
readability exaggeration tapered off near the camera. Both lessons were paid
for: a nested "glow" shell just occludes its own core in a forward opaque
pipeline, and a shot passing the chase camera at 3x exaggeration is a
screen-filling balloon that reads as a volley of different-sized projectiles.

### Melee (row 2b of `DIRECTION.md`)

The close-range answer, from a playtest: "very annoying when you are close
to the enemy but with the wrong heading." The breath cone and the fireball
both need the nose on the target, and in a turning fight the rival is very
often twenty metres away and off the nose, where the player could do nothing
but circle. So a dragon that close bites, and **melee needs no aim**:

- **Bite** (`bite_range` 26 m, `bite_half_angle_deg` 45): anything inside a
  wide cone ahead of the MOUTH -- the rig's animated head, the same point
  fire leaves from. Tested against the target's near surface like the
  breath, so jaws that visibly close on a rival count.
- **Strike** (`strike_range` 16 m): a claw or tail on anything inside a
  sphere around the body, any direction -- alongside, behind, above.
- One press (`C`, gamepad `B`) is one swing, whether or not anything is in
  reach; `melee_cooldown` 0.55 s; the swing costs `melee_lunge_speed_cost`
  (2 m/s) of airspeed. The first cut was 1.3 s and 3 m/s, and the playtest
  found it harder to land than the breath: a close pass lasts about a second,
  and one swing in it was one chance. Half a second turns a pass into a
  flurry.
- **A hit stuns and knocks** (`melee_stun` 1.6 s, `melee_knockback` 14 m/s),
  in the Spyro tradition. A stunned bot loses its controls and weapons and
  tumbles on whatever velocity the shove gave it; a stunned drone stops
  orbiting and firing; both hold their hit flash for the duration so the
  state reads at range. A bite on the player knocks but never stuns -- the
  controls stay theirs.
- **Hits chain** (`melee_combo_window` 1.4 s, `melee_combo_bonus` +0.35 per
  step, three steps): the HUD shows `x2`, `x3` over the C pip, and the camera
  jolt grows with the chain. A miss breaks it.
- The swing is heard starting -- a whoosh rising into the snap -- and a hit
  is a different sound (a crunch and a thud) with an ember burst where the
  jaws met and a jolt through the camera. The first cut's click-only snap
  vanished under the wingbeat in play.
- `melee_reach()` is the geometry, exposed for the tests and shared by the
  bots, so what the player's swing hits and what a bot's swing hits are the
  same function.

Bots swing through `Combat::hostile_melee`, buffered and resolved against the
player inside `update()` exactly like their flames; damage is its own dial
(`hostile_melee_damage`, a strike at 60%). The bot decides off the LIVE
position, like its flame -- a bite is a contact, and pretending not to see a
dragon fifteen metres away reads as blindness -- in any state, once per
`melee_cooldown` (0.9 s) stretched by its tempo. Two doctrine changes came
with it. **An aligned attack presses to bite range before it extends:** the
break-off range was 80 m in every case; a charging bot now presses to
`melee_range * 0.7`, while an off-axis pass (an overshoot about to happen)
still breaks at 80 m. And **the charge** (`charge_range` 400 m): inside that
range with the player inside the bot's own bite cone (`melee_cone_deg` 60,
read off the LIVE position -- the stale sample put a turning player 60
degrees off a nose that was in fact 30 from them, and ended every charge at
80 m), the attack clock does not run, the bot flies at the player's BODY a
quarter second ahead rather than at the firing solution, and it manages
speed to close and then to match: a boost (`charge_boost_duration` 1.1 s,
`charge_boost_cooldown` 6 s) beyond 150 m, flap inside 80 m only while slower
than the player, and the steering's overspeed brake lifted. Each of those was
a measured failure: without the charge a bot stalking at three metres a
second ran its attack clock out at 300 m; with flap alone the closest
approach in two minutes was 47 m; a boosted 76 m/s pass at a turning 45 m/s
target overshot by fifty metres every time. With speed matching one bot in a
two-minute autopilot match reached 16 m, swung four times and bit twice.
The playtest before that saw a bite once in three matches, with a player who
steers toward the rivals; `charge_range` and the speed-match band are the
dials.

**Three gestures**, chosen by where the mark is (`melee_gesture_for`: within
60 degrees of the nose the jaws, behind 125 degrees the tail, otherwise a claw
on that side) and thrown even at a miss, toward the nearest thing worth
swinging at. What a swing HITS is still `melee_reach()`'s business; the
gesture only decides how it looks. And each is the WHOLE animal -- wind-up,
strike, settle, with the body, wings, neck and tail answering the limb
(`ANIMATION.md`, "Melee is the whole animal"): the playtest called a limb
moving on a still body weak and mechanical, and it was.

- **Bite** (`RigAction::bite`): the neck lunges forward and down --
  `bite_lunge_deg` 38 with a `bite_impulse` 14 m/s kick through the chain, out
  in the first third of `bite_duration` (0.32 s) and back in the rest -- and
  the jaw gapes on the way out and snaps shut at the end of the lunge. The
  first cut, 24 degrees and 5 m/s over half a second, could not be told from
  the breath's thrust; the second, 36 degrees over the same half second, read
  as calmly eating. A strike is fast out and slow back.
- **Claw** (`RigAction::claw`, `side`): the near foreleg (hind leg on a
  wyvern) rakes forward `claw_swing_deg` 95 and out `claw_out_deg` 35 with
  the talons spread, over `claw_duration` 0.4 s. The outward component is
  what makes it visible: straight forward it stayed under the wing from every
  angle but below.
- **Tail** (`RigAction::tail`, `side`): a lateral whip toward the mark --
  `tail_whip_deg` 85 of steer and an `tail_impulse` 18 m/s kick weighted to
  the tip -- across and back over `tail_duration` 0.75 s.

**Studio scenarios 10-12** are the bench: 10 "melee" cycles bite, claw, tail
every 1.6 s at a mark weaving twenty metres off the nose; 11 "claw" and 12
"tail" swing every 1.4 / 1.6 s with sides alternating on a dead-still body,
so a frame-to-frame diff is the gesture and nothing else. Look at the claw
from the front-below (`--inspect 0 14 -25`) and the tail from above
(`--inspect 90 14 85`); scenario 8 also bites once at 7.2 s of its attack
cycle.

**The training room** (`--training`, or the button at the top of the Combat
panel) replaces the targets with six passive dummies laid out ahead of the
current heading at 60 to 420 m, staggered a strike's width to either side:
they never fire, hold still, take six drones' worth of health and come back
in 2.5 s. A straight flight with the attack held lands seven bites in the
first pass; R flies the line again. It is where the reach of a bite is
learned without being shot at, and where the dials above are tuned.

How it was verified, because it could not be seen: the `--telemetry` line
now carries `bites swung / landed / taken`. In a passive or autopilot match
neither dragon comes within 26 m for minutes at a time -- bots break off at
80 m and cannot follow a gliding player below their terrain floor -- so the
in-game path was confirmed by pinning a bot 14 m ahead for half a second in a
probe build: the player's swing landed (9 m from the mouth, 8 degrees off)
and the bot's swings took health. The reach geometry and the bot's discipline
are pinned in `test_combat` and `test_bot`. Whether the dials are right is
the playtest question for a 2-bot match: does a close fight resolve instead
of circling.

## Bots (M14)

`game::BotPilot` is a pilot, not a puppeteer: it reads the world and emits the
same `FlightInput`/fire decisions a player produces, flown by its own
`FlightModel`. Difficulty is **honest imperfection** -- the player is *sampled*
every `reaction_interval` and extrapolated in between, so a break inside the
reaction window genuinely defeats its aim; spread is error in the firing
solution, not damage dice; and the nose must actually point at the solution,
because bots aim by flying.

**Terrain contact scales with violence.** A plummet past 25 m/s of sink is
death; a scrape costs health and triggers the jink; a gentle touch is a touch
-- instantly deleting a dragon that grazed a slope read as a bug, because it
was one. The recovery reflex fires on the **physics of the pull-out**
(sink^2/2a plus margin), not a fixed height or time: 50 m of clearance is
plenty in level flight and nothing in a 70 m/s dive. Recovery flares (brake
adds drag AND lift, tightening the pull) and cancels tuck. Result: zero crash
deaths across repeated 4-bot 4-minute soaks, down from ~6.

**Bots breathe fire** on a latched burst budget, in ANY state when close and
aligned -- gating breath on the attack state left a 20 m window between
min_attack_range and breath_range that nobody ever saw a flame in. The flame
check uses the live player position (a flame visibly connects or does not;
pretending not to see reads as blindness, not fairness -- fairness lives in
the aim solution), and their heads track the player inside 350 m, which is the
tell that a flame is coming. Hostile flames damage through
`Combat::hostile_breath`, buffered and resolved in update() so attribution
goes through the one path that owns it.

**Combat reset clears the bots.** Combat::reset rebuilds the sentinel slots;
bots that survived it were left pointing at freshly spawned drones, puppeting
spheres around the sky while their dragons rendered on top. The panel toggle
now clears the bots, and update_bots refuses to drive a slot that is not
flagged external -- the belt to the button's braces.

A **manual aim** checkbox zeroes the assist (and restores the exact slider
value after), the aim cross is big enough to see, and a bot holding its flame
turns its HUD bracket red with a FLAME tag -- the head tracking is the diegetic
tell, but a tell nobody notices is not a tell.

**Fire leaves the mouth.** The app feeds the rig's animated head position to
`Combat::set_muzzle` each frame, so the player's flame and fireballs start
where the head actually is, and a small HUD cross marks where the mouth's shot
will go -- the head is unreadable from behind, and fire from an invisible
origin toward an unmarked point felt random.

Prediction is **quadratic**: position, velocity, and the acceleration measured
between the last two samples (nothing on the first -- measuring against zero
history invents a lunge). A STEADY turn or brake is the most predictable
manoeuvre there is, and a pilot who cannot lead one is not a pilot; what still
defeats the bot is CHANGING the manoeuvre inside its reaction window. Firing
solutions -- the bots' and the player's aim assist alike -- are solved for the
**inherited-velocity drift**: a round leaves at shooter velocity plus muzzle
velocity, and ignoring the drift lands every crossing shot one drift-length
behind the target. That bug hid in both solvers, found by flying recorded shots
to closest approach in a test.

The state machine is the fight's rhythm: **attack** (fly at the intercept,
fire in the cone), **extend** (out past the merge, turn, come back with
energy -- passes, not orbiting), **evade** (a jink on taking a hit). Three
doctrine rules earned by failing tests: the attack clock only runs inside gun
range, because timing out of a stern chase oscillates forever (nine seconds
closing, seven extending, no progress); terrain must be sampled **ahead along
the velocity**, not just below, or bots fly into rising slopes; and the aim
point is floor-clamped over the terrain, because following a player into the
weeds is how bots die of enthusiasm.

Each bot occupies an **external hostile slot in Combat** (`spawn_external` /
`drive_external` / `fire_hostile`), so health, lock-on, projectile sweeps, hit
flash, HUD brackets, kills and respawn timing all come free; the app owns the
body -- flight, rig, rendering (drawn as the real dragon, warmed slightly red)
-- and repositions it when the slot comes back alive.

Sentinels are **not AI**: they fly a fixed orbit and fire on a timer with
deliberate aim spread. They exist so health, aim and the cooldown rhythm can be
tuned against something that shoots back. M14's bots replace them, driving the
same flight model the player uses.

A bug worth remembering: `spawn_wave` originally set a sentinel's orbit but never
its `position`, so a freshly spawned or respawned one sat at the **world origin**
-- a live, shootable target in the middle of the map -- until its first update
moved it. The respawn path returns early, so nothing else would have placed it.
Initialise derived state at spawn, not on the first tick.

**Put the difficulty dials where they can be found.** `aim_assist` and
`sentinel_spread` decide whether combat is fun, and they spent a session inside a
collapsed ImGui header, which is the same as not existing. They are now at the
top of the panel with forgiving/standard/sharp presets beside them.

Fire is drawn **unlit** (`ModelUniforms::material.w`). The shared tonemap ends in
a gamma encode, so anything bright desaturates toward white; adding two units of
sunlight on top of a flame turns it into a white balloon. A light source should
not also be lit.

## The match loop (M15)

**Weapons-cold gating must precede everything that consumes the decision**: it
once sat between the bot's fireball and its flame, gating one and not the
other, and bots shot through the countdown. And a bot grounded for three
seconds is written off as a crash -- wedged on a slope the flight model cannot
take off from, the recovery reflex has had its fair window.

`game::Match` is pure scorekeeping and phase logic -- it consumes CombatEvents
and emits nothing but state, so the whole loop is testable without the app.
Deathmatch: the player scores kills, hostiles score by killing the player,
first to the target wins; on time expiry the leader wins and a tie is honestly
a draw. **Weapons are cold** in the countdown and on the results screen --
enforced in three places (player input, bot decisions, and the scorer itself
refusing kills outside the fight), because a kill during a countdown is a bug
wherever it comes from. The rally HUD stands down while a match runs. Enter
rematches; `--match` (with `--bots N`) starts one from the CLI.

Bots regenerate like the player does (`hostile_regen`, after a lull), scaled
by skill tier along with their health: a rookie never heals and loses wars of
attrition; an ace refuses to stay wounded. Disengage-and-recover cuts both
ways, which is the balance the player asked for.
