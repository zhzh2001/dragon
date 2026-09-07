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
