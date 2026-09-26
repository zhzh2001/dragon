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
  in the first third of `bite_duration` (0.42 s) and back in the rest -- and
  the jaw gapes on the way out and snaps shut at the end of the lunge. The
  first cut, 24 degrees and 5 m/s over half a second, could not be told from
  the breath's thrust; the second, 36 degrees over the same half second, read
  as calmly eating. A strike is fast out and slow back.
- **Claw** (`RigAction::claw`, `side`): the near foreleg (hind leg on a
  wyvern) rakes forward `claw_swing_deg` 95 and out `claw_out_deg` 35 with
  the talons spread, over `claw_duration` 0.45 s. The outward component is
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

The gesture includes a 25% anticipation, then reaches its body strike peak
30% into the remaining time. Combat damage, stun and hit feedback still resolve
on the input edge; this animation pass does not synchronize damage with the
later visual contact. Grounded clips/stance can override the additive gesture.
These are remaining integration limits, especially when judging hit feel.

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
tell, but a tell nobody notices is not a tell. Under every bot's bracket sits
its health bar, in the dull red: whether to press. (Its aggression was drawn
there too for one build and told the player nothing they acted on; the Combat
panel's bot list prints aggression and nerve.)

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

### Personality, nerve and aerobatics

The playtest after melee: "bots feel conservative -- they flee when chased
even with a good opportunity to attack." They did, because every pilot
shared one doctrine and one answer to being hit (the jink). Two things
changed that.

**Personality.** Each pilot draws an `aggression` at reset from the tuning's
value plus or minus `aggression_spread` (rookie 0.35, veteran 0.55, ace 0.8,
spread 0.3; both are dials in the Combat panel). Its **nerve** this moment is
that aggression pushed up by good health and down by wounds (`nerve =
aggression + 0.6 * (health - 0.5)`), and the doctrine reads the nerve rather
than fixed numbers: the attack lasts `0.6 + 0.9 * nerve` times as long and
breaks off at `1.4 - 0.8 * nerve` times the range, the extend leg is shorter
for a bold pilot, and the charge reaches `0.6 + 0.8 * nerve` times as far. A
wounded pilot (`flee_health` 0.3) with nerve below 0.6 RUNS: it extends twice
as far, boosted, and does not come back until it has healed past the line.

**The answer to a hit** is decided in `update()`, where the geometry is
known, not in `notify_hit()`. Nerve above 0.65 and the shooter behind within
`flip_range` (320 m): a **flip** round to face it, and the attack goes on.
Nerve above 0.5: a dodge **roll**, staying on the attack. Otherwise the jink
(Evade) as before. Bold pilots also **boost** to close from beyond 300 m, not
only in the charge; cautious ones only boost to run.

**Aerobatics are shared code.** `game/maneuver.h`: a `Maneuver` writes
control inputs to the same flight model as the stick, with the assists that
would fight it (auto-level, the bank limit) stood down through
`FlightInput::maneuver` and the control rates scaled by `agility`. The
**roll** (`Z`, d-pad down) is one full roll in `roll_duration` (0.85 s) at
`roll_agility` (x2.4) with a `roll_dodge_impulse` (7 m/s) sideways at the
start, so it moves the dragon off its line. The **flip** (`B`, d-pad up) pulls
until the heading has reversed, then rolls out to upright; it is refused
below `flip_min_airspeed` (26 m/s), because below that it is a stall, and it
eats energy (45 to 27 m/s in the test), so it is a decision. Both are pinned
in `test_flight` (the roll goes over and comes back; the flip reverses the
heading upright, with the bank limit ON) and rendered in
`artifacts/aerobatics/`. The bots fly the same `Maneuver`; `--maneuver
roll|flip` triggers the player's at frame 30 for a capture.

**Terrain outranks nerve.** The first soak with the bolder doctrine ended on
the deck at 82 s: a longer, closer attack with the charge lifting the brake
over a rising slope. Every speed-up -- charge flap and brake, every boost --
now requires 160 m of ground under the bot, and the ten-minute soak is back
to zero grounded frames.

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

## Elements (M26)

Every breath is made of something (`src/game/element.h`): **fire, frost,
blight, storm, tide, stone** -- one per breath the roster already carried,
named by `element` in `<model>.breath.cfg` (absent is fire). Each hit leaves
one **status** on what it lands on, through one path (`Combat::hit_sentinel`
for the player's hits, `Combat::hurt_player` for everything that hurts the
player), so a fireball, a held breath, a bite, a bot's bolt and a tower's
all go through the same rules:

| Element | Status | What it does |
|---|---|---|
| fire | burn | 7 dps for 3 s after the hit, refreshed by every hit |
| frost | chill, then frozen | chill (0..1) slows flight and fire rate (up to 45%); full chill freezes -- an enemy's wings lock and it drops (a stun), a tower stops firing; 1.6 s, the player's 0.8 s (flap locked, half the stick, never the controls) |
| blight | corrode | +15% damage taken from everything, and 2 dps, for 3 s (was 25%, 3 dps, 5 s: a blight tower's bolt every 2.4 s kept the player corroded for good -- "very tricky") |
| storm | shock | weapons jammed 0.5 s; the hit arcs to the nearest other enemy within 90 m for 45%, once -- it does not chain on |
| tide | drench | for 4 s the drenched body's OWN attacks are weakened: 40% less damage, reload at half rate (the player's breath also refills at half); douses burns; a tide fireball shoves what it hits, a tide breath shoves steadily. It first stopped regeneration, which the playtest rightly called nothing: any hit already holds regeneration off. Blight makes a body take more; tide makes it deal less |
| stone | stagger | builds 0.4 a hit; full stagger stuns an enemy 1 s, knocks the player off line |

**Burn and corrosion no longer hold the player's regeneration off.** They
did, and a stream of damage-over-time meant a player under a blight tower
never healed at all -- that, more than the +25%, was what made blight
"tricky". The regeneration delay runs from the last real hit.

**There is no type chart.** The playtest's call: Pokemon-style
effectiveness would be too much, and "frost beats fire" multipliers turn a
dogfight into a lookup table. The one relation between elements is that a
creature **shrugs off its own**: half the damage (`same_resist`) and none of
the status. The one interaction is physical, not a table: water puts fire
out -- a drench douses a burn and a drenched body does not catch. A held
breath builds status at `breath_weight` (2.0) per second against a
fireball's 1 and a bite's 0.6: at 1.2 a frost breath needed 2.5 s unbroken
to freeze, and in twenty seconds of fighting bots nothing ever froze.

**Who is what.** The player breathes the species' element, or the Combat
panel's "you breathe" (`--element NAME`), and the flame takes that element's
look and species scales (`element_breath`, the six species profiles folded
to one per element). In the arena a bot of a species with a breath file
breathes its own; an unaligned one (`dragon.glb`) rolls one, and its hide
leans toward it. In a run **every enemy rolls**: rivals, hunters and every
tower. Arena drones are `None`: no element, no resistance, no status.

**How it reads.** Everything suffering a status wears it as particles in the
element's own motion (`App::emit_status`): flame licks rising, frost motes
sinking and an ice-glint shell while frozen (and the hide ices over), blight
drips and bubbles, sparks crawling with small arcs, water running off, grit.
A storm arc is a jagged chain of points struck for a tenth of a second
(`emit_arc`). A status landing hard bursts (`emit_status_burst`): ice
shattering, rock, steam off a doused flame. Bolts and impacts wear their
element's colours; a tower's brazier burns its element and plumes in its
motion, so a frost tower reads as frost from a kilometre. The HUD tags every
enemy with its element ("TOWER - FROST", "RIVAL - STORM") and what it is
suffering under its health; the player's statuses are chips over the health
plate, the breath bar is labelled with the player's element, and a freeze or
a burn tints the screen edge. `--status NAME` holds a status on every enemy,
for captures. Every dial is under Combat > element dials.
`tests/test_element.cpp` pins the names, the no-chart rule, burn and douse,
chill and freeze, a breath freezing in about 1.5 s, the storm arc (once, for
a share), a frozen player unable to fire, a frost player resisting frost, and
a frozen tower holding its fire. Renders in `artifacts/elements/`.

## Abilities (M27)

Growth scaled numbers; the playtest asked for "non trivial upgrades like
Legend of Spyro". A run now grants **one ability per stage**, announced in
the "YOU GREW" call-out; the arena grants all of them. `Combat::Abilities`
gates the three combat ones, and the second breath is the app's.

| Stage | Ability | What it is |
|---|---|---|
| young | **Charged shot** | Hold G: the fireball gathers at the mouth (0.8 s to full, an arc round the aim marker); let go, or reach full, and it leaves 2.2x the damage, 2x the blast, 1.6x the body, 2.5x the status, 15% faster. A tap is still a plain shot, on the release. |
| adult | **Second breath** | A second element (rolled per run; the next one round in the arena). U (gamepad Y) swaps; the U pip wears the colour it swaps to. The flame, the status, the resistances all follow. |
| elder | **Ram** | The boost is a weapon: while boosting, anything the body passes within 10 m (x size) takes 35 of the player's element, a 1.3 s stun and a 24 m/s shove, once per target per boost. The body is sheathed in its element while it boosts. |
| ancient | **Fury** | Damage dealt fills a meter (1 per 500 damage, 0.08 a kill; the H pip). H (left-stick click) releases a nova of the element round the dragon: 170 m, 90 damage at the centre and half at the edge, 3x status, a 1.6 s stun, a 30 m/s blast outward; prey inside it die. Spyro's Fury. |

**Every ability also grows with the dragon** (the same continuous growth as
the body): the melee's reach, stun, knockback and cooldown, the boost's force
(ahead of the heft), duration and cooldown, the fireball's damage and blast,
the dodge's kick and the manoeuvre cooldown -- an ancient's bite reaches
35% further and swings 22% sooner, its boost pushes twice as hard.
`tests/test_combat.cpp` pins the tap, the charge, one ram per boost, and a
fury that hits only inside its radius and needs unlocking.

**Sound.** Each element's status landing has its own synthesized sound
(`audio::Clip`): Ignite, Shatter (a frozen body -- inharmonic high partials
over a crunch), Hiss (acid), Zap (a crack and a falling buzz, on every storm
arc), Splash, Crack (rock), rate-limited per kind so a held breath does not
machine-gun. The held breath is voiced by its element
(`Audio::set_flame_style`): fire roars and crackles, frost hisses bright
with glassy glints, blight gurgles, storm buzzes and crackles, tide rushes
and sloshes, stone rumbles with grit. The fury has its own boom and roar.

## The hoard run (M24, row 3 of `DIRECTION.md`)

The run probe: one generated valley flown from its head to the pass at the
far end, under pressure, for a hoard. It is a MODE beside the arena, not a
replacement for it -- `--run [seed]` or "start run" at the top of the Combat
panel enters it, "leave run" returns to free play, and `--match` is exactly
what it was. The question it exists to answer is whether flying the corridor
under pressure is more fun than the free-form match, and whether altitude and
route matter; if not, the roguelite is the wrong shape (`DIRECTION.md`).

`game::HoardRun` (`src/game/hoard_run.h`) is pure layout and scorekeeping,
like `Match`: given the terrain and a seed it places the encounters, given the
player's flight state each frame it collects, banks and loses, and it owns no
bots, no combat and no rendering. `generate_run_layout` builds the corridor's
**spine** from `valley_center_x` (24 points at 150 m over the floor, the span
the valley course flies) and hangs everything off fractions of it with a
seeded xorshift, so the same seed is the same valley on every machine:

- **Caches** (3): on dry, flat ground within `cache_offset_max` (110 m) of
  the spine, one early, one mid, one near the pass, worth `cache_value` times
  1 + 0.5 x depth -- the far end pays most. Collected by being GROUNDED inside
  `cache_radius` (26 m) for `collect_time` (2.5 s); taking off drains the
  progress at twice the rate. Drawn as a gold ring lying on the ground with
  the pile in it, which sinks as it is taken; the HUD marks the nearest
  uncollected one and fills an arc while you sit on it.
- **Towers** (4): `Combat::spawn_defence` -- a sentinel with `ground` set,
  pinned to the terrain `defence_offset_min..max` (160..320 m) to alternate
  sides of the spine, never on a peak far above it. They fire the heavy bolt
  (`defence_*` tuning: 170 m/s, 14 m/s^2 of drop, 2.4 s interval, 420 m
  range, lead corrected for the drop), so a flight straight down the middle
  is inside their reach and a flight along the ridge above them is not. A
  destroyed tower stays destroyed; projectiles now carry their own gravity.
  Drawn as a stone shaft with a brazier that flashes when hit -- a prop mesh
  is row 10.
- **Rivals** (3): bots at posts over the corridor, facing back toward the
  head, DORMANT until the run wakes them: `App::update_bots` flies a dormant
  rival in a slow circle over its post through the rally autopilot's
  `steer_through`, weapons cold, and hands it to its pilot once the player is
  within `engage_range` (520 m) of the post or has shot it. Then it is a bot
  like any other, personality and all.
- **The pass gate**: a ring (`pass_radius` 70 m) at the far end facing along
  the corridor, tested as a segment crossing like a checkpoint. Crossing it
  banks the hoard.
- **The dragonslayers**: after `pressure_after` (120 s) a hunter is loosed
  700 m behind the player every `pressure_interval` (45 s) up to
  `max_hunters` (3): an ace-tempered bot in red that does not flee. The clock
  is never shown; the hunter count in the run strip is.
- **Death** ends the run and the hoard is lost; the dragon respawns at the
  head with the results up. R flies the same seed again, Enter deals a new
  valley. **Results**: hoard banked or lost, caches, kills, time, distance,
  beside the best previous run's numbers from `assets/runs.txt`
  (`RunRecords`: runs, banked, best hoard, fastest banked time).

The rally stands down in a run (no rings, no strip); the autopilot flies the
spine instead of the course, which is the soak: `--run-empty --autopilot
--frames 8400` banks at 110 s; `--run 7 --autopilot` dies at 50 s to the
rivals and towers, flying straight and never fighting back, which says the
pressure is real without saying whether it is right -- that is the
playtest's. The Combat panel's run section has every dial, and target
brackets now stop at `mark_range` (1400 m): seven hostiles down a
five-kilometre valley put seven range labels on the opening frame.

**After the first playtest (M24.1).** Three findings. *A landed dragon could
not move*, so landing inside a 26 m cache ring was luck: the stick now walks
on the ground (`FlightTuning::walk_*`, 7 m/s, turning in place; W and the pad
pushed away are forward regardless of the pitch inversion) with a stride on
the terrain IK (`ANIMATION.md`). *Towers died to a dragon that landed beside
them and breathed*: a tower was the one target that could not answer a
dragon standing still. Now stone takes `defence_breath_resist` (0.3) of the
flame, bites and strikes land but neither stun nor shove a tower, inside
`defence_close_range` (200 m) or against a grounded dragon it fires
`defence_close_rate` (2.5x) as often at a quarter of the spread, and its bolt
splashes `defence_splash` (10 m) where it hits the ground, so a near miss on
a standing dragon still costs. Fireballs from range and a bite are the
answer; sitting in the flame is not. *Rivals and hunters looked alike*: the
bracket now says who -- HUNTER in the danger red (bracket, label and edge
arrow), RIVAL or RIVAL (at post), TOWER -- and rivals no longer draw the
rust hide from the palette, so red in the world means hunter too.
`test_combat` pins the tower answers (a third of the breath, no stun or
shove, more than twice the shots at a standing dragon 150 m out than at a
cruising one 350 m out); `test_flight` pins the walk.

**Second pass (M24.2).** The second playtest: towers overpowered, the best
strategy ignoring them and the drones; hunters overpowered, fleeing the only
answer; seeds changing nothing; "interesting, not as engaging as the arena".
The diagnosis was that fighting cost and earned nothing, so avoidance was the
rational run -- and two bugs made it worse: every run also carried combat's
default wave of five drones (`Combat::reset` now takes a wave count; a run
passes 0), and the seed never reached the terrain. What changed:

- **Fighting pays.** Every cache has a GUARD tower 70..120 m from it
  (`RunDefence::guards`), and the HUD marks a cache "guarded" while it
  stands. Kills the player makes pay a bounty into the hoard (tower 40,
  rival 60, hunter 120) and 20 health back; a rival that flies into a
  mountain pays nothing. The dead stay dead in a run.
- **Towers rolled back** to one answer for the landing exploit: the tighter
  aim at a grounded or close dragon and the ground splash stay, the breath
  resist and the close fire-rate multiplier went back to neutral (both dials
  on the panel now).
- **Hunters one at a time,** veteran-tempered (aggression 0.7, flees at
  0.2), the next `pressure_interval` (50 s) after the last one falls. The
  first comes at `pressure_scale` (1.6) times the corridor's straight flight
  time at 45 m/s -- about three minutes -- instead of a fixed two minutes
  that was shorter than a straight flight.
- **Seeds deal valleys.** `apply_valley_kind` draws a kind from the seed and
  writes both the terrain (its own terrain seed, a meander phase and period,
  and by kind the floor width, the walls and the peaks) and the mix: a
  **vale** (wide, gentle), a **canyon** (narrow, tall, sharp bends, caches
  worth 1.25x, everything closer to the spine), a **gauntlet** (two more
  slope towers, one rival, a fourth cache), a **rival nest** (five rivals, no
  slope towers, rivals pay 1.25x). The valley regenerates when the seed
  changes (about 0.1 s) and the arena's comes back on "leave run". The panel
  dials are the base every kind derives from, so they never compound.
- **Growth.** Hoard taken and bounties feed a growth meter; a drake becomes a
  young dragon at 140 and an adult at 380. Each stage scales the tuning from
  the player's own: drake 0.8 heft, 0.85 health, 1.4x breath drain and
  fireball cooldown, 0.8 bite and flame; young is 1; adult 1.3 heft, 1.5
  health, 0.7 drain, 0.6 cooldown, 1.3 bite, 1.25 flame. Growing refills the
  new health plus 25. The run strip shows the stage and a bar; "YOU GREW"
  calls it out. Leaving the run restores the tuning.
  **Growth is also seen (M25.2):** the player's whole body scales with the
  stage (0.8, 1.0, 1.25) and the wings grow on top of it (0.85, 1.0, 1.15,
  on the rig's shoulders: `DragonRig::wing_growth`), so an adult's span is
  about 1.45x a young dragon's; both ease in over about two seconds on the
  "YOU GREW", and the resting height scales with the body so a big dragon
  stands on its feet. `--stage N` starts a run already grown for captures;
  `artifacts/growth/`.
  **Growth is continuous (M25.3):** `HoardRun::growth_level` runs 0..2 with
  the hoard (1 at the young threshold, 2 at the adult), and every frame the
  tuning and the size interpolate between the three rows, so each coin
  shows; the stages stay as names, the "YOU GREW" call-outs and a 25-health
  second wind. The hit body scales too (`Combat::player_size`, over
  `player_radius` 6.5 m): bolts, bites and strikes, and a bigger body's near
  side in a flame. The adult's fireball cooldown is 0.8x, not 0.6x -- the
  playtest's "a bit overpowered".

**Five stages since the descent (M26.1):** elder at 750 and ancient at
1250 after adult at 380, each a smaller step (elder 1.8x health, 1.38 size;
ancient 2.1x, 1.5), so a valley buys about a stage and the curve flattens
while the enemies' keeps climbing with the depth. Three stages had the dragon
an adult early in valley 2 with nothing left to grow into.

The unarmed autopilot now dies early in every kind -- it flies straight into
each post and never shoots -- and with `--attack` holding fire it still
kills nothing, so none of this is judged headless. The HUD is: every run
strip, call-out and marker was captured (`artifacts/run/second_pass_*`).

`tests/test_hoard_run.cpp` pins the layout (determinism, caches dry and
ordered and richer with depth, towers on the ground beside the spine, rivals
over it facing back, the gate at the end), collection (needs the ground and
the time, drains on takeoff), banking, losing, engagement, the hunter clock,
and the records. Renders in `artifacts/run/`.

### The descent, the props, the prey (M26)

**A run is three valleys.** One valley was a four-minute run with nothing
after it. Crossing a pass that is not the last **banks** what is carried
(safe from then on), and the next valley is laid out under the dragon
(`HoardRun::just_crossed`, `App::advance_valley`): its seed is
`valley_seed(run, depth)`, stepped until its kind differs from the valley
before; `apply_depth` makes it harder -- per valley deeper one more slope
tower and rival, a cache more, hoards worth +35%, the hunters' clock x0.8,
bounties +25%. **The enemies grow too** (after the first playtest of the
descent: "once valley 1 is finished, the remaining 2 become much easier" --
only the player had grown): per valley deeper every hostile hit does +40%
(`Combat::hostile_damage_scale`, bolts, flames, bites and damage over time),
towers, rivals and hunters have +50% health, and rivals and hunters +0.12
aggression. Growth, the bank, the clock and the kills
carry; health comes back at each pass; the hunters restart. A death loses
only what was carried; the result is what the passes banked, and the
records keep the best hoard whether or not the run cleared. The strip shows
"VALLEY 2/3" and the bank; a call-out names the new valley. `--valley N`
starts N valleys down; the panel has "skip to next valley" and the descent
dials.

**Taller towers, two silhouettes, two caches** (`tools/props.md`, built by
gpt-6-astra): the keep is 40 m (it stood below the 15-25 m canopy at 16 m
and read as a stump) and guards the caches; slope towers alternate it with
a 44 m spire. A tower is hit round its middle (18 / 20 m up, radius 9 / 7)
and fires from its brazier or orb (`Sentinel::muzzle_height`). Every other
cache is the trove, a ruined ring round a smaller heap, beside the 16 m
pile.

**Prey** (`src/game/prey.h`, DIRECTION.md row 6): each valley has herds of
grazers (`assets/props/grazer.glb`, Mossback decimated to 8.5 K triangles
with baked graze, walk and run clips -- `tools/grazer.md`) on flat dry
ground away from the towers. They graze and amble, lift their heads at a
low dragon within 240 m, and bolt at 17 m/s inside 150 m; a dragon above
80 m is a speck they ignore. Three ways to eat, one per verb: **swoop** --
the body within 8 m and 9 m above one snatches it (both scale with growth);
**bite** -- the jaws take the nearest in the cone; **fire** -- breath or a
fireball kills, and the carcass stays a minute to be swooped or walked onto.
A meal is 18 growth (no hoard) and 14 health. Dials under Combat > prey.
They are drawn at 1.4x the asset (a 6.3 m bison) and marked -- "too small to
notice" was the first playtest: a green diamond and "HERD x6 640 m" over
each herd out to 1.8 km, on screen only, and inside 450 m a chevron over
every animal, bright while it runs and gold over a carcass.
`tests/test_prey.cpp` pins the calm herd staying home, ignoring a high
dragon, scattering from a low one slower than a dragon flies, and the three
ways to eat.

## The demo pilot (M25)

`game::DemoPilot` (`src/game/demo_pilot.h`) plays the game through the
player's own controls, for a demo, a soak and a hands-off mode: P in play,
`--autopilot` with combat or a run, `--demo` for one random valley after
another. It is a decision layer, not a new AI. The app describes the world
each frame (`DemoWorld`: live targets by kind, the corridor waypoint, the next
cache AHEAD down the corridor and its guard, a safe point up the valley, the
health, the lock) and applies the decision; dogfights are flown by a
`BotPilot` aimed at the chosen mark, cruising and landing by the rally
autopilot's `steer_through`, walking by the ground walk. A finished run deals a
new valley and a match rematches after six seconds of results. The HUD says
AUTOPILOT and the job; the telemetry line carries the job and a per-job time
total (`demo time:`), which is how every problem below was found.

The jobs, re-decided every 0.4 s: **fight** a hunter inside 350 m or a rival
inside 250 m (a dormant one inside 260 m); **flee** to the safe point and
circle it when below 35% health, until 75%; **siege** a cache's guard tower;
**land** on an unguarded cache; **walk** the last 70 m on foot; **collect**;
**take off**; **cruise** otherwise. After one cache, or when the first hunter
is due, it **rushes** the pass: opportunistic weapons stay live, but hunts and
pursuit fights cannot turn it back. `DemoTuning::caches_before_rush` sets the cache objective.

What each headless run taught it, in order -- each was a trace, not a guess:

- **Speed.** Its dogfights climbed after the mark to a 6 m/s stall. Below
  22 m/s in the air (not on final approach) the nose goes down and the wings
  beat until 34. Its fighter's flips became rolls: a half loop at a drake's
  speed ended at 13 m/s with the mark behind it.
- **The siege.** A tower cannot be dogfought: the bot pilot's terrain floor
  holds it 130 m up. A siege is a strafing pass: climb to an ENTRY point
  (600 m out, 170 m up, chosen from eight bearings over the lowest ground,
  frozen once chosen), commit only when facing the tower, run in weaving
  across the line with fireballs inside 480 m and the flame inside 170, pull
  up at 70 m or on the clearance predicted 1.5 s ahead (25 m floor), extend
  to the far side, which is the next entry. Every one of those clauses was a
  failure first: an entry recomputed from the dragon's position moved with it
  and was circled for a minute; committing while facing away made a
  descending turn that tripped the pull-up forever; a 60 m floor pulled up at
  224 m, before the flame was ever in reach. A siege that has not killed its
  tower in 60 s gives the cache up.
- **Fights that go nowhere.** A mark that regenerates between passes can be
  traded with forever. No damage on it for 20 s and the pilot breaks off from
  that one for 30 s -- a pursuer does not pull it back -- and, while cruising
  or fleeing, whatever crosses its nose takes a fireball, the flame or a bite.
- **Objective first.** Engaging rivals at 500 m and hunters at 1.1 km spent
  seven-minute runs in dogfights; the ranges came in to 250 and 350.
- **The climb-out (M25.1, the playtest: "stuck in cycles after the first
  hoard").** Handing over to the cruise the moment the feet left the ground
  pitched a 10 m/s dragon at the corridor 150 m overhead: it stalled onto
  the floor, leapt, stalled, or skated along the ground at 40 m/s. Take-off
  is now a job that holds until the dragon has climbed out -- and the exit
  is a HEIGHT (35 m), because a scratch probe on the flight model showed
  why speed never came: from a standstill at full flap, below about 15
  degrees a dragon never leaves ground effect, and at 20 degrees it climbs
  2 m/s at a steady 19 m/s, drake or young. An exit at 28 m/s was never
  reached. It holds that attitude (steeper against rising ground, and
  steering down the slope, since level into a slope skated 130 m up a
  valley wall), re-flaps on every touchdown, and gives up after 25 s to the
  cruise and the stall guard (now 16 m/s, under a fresh climb-out's 19),
  and it climbs nearly wings-level below 25 m -- a bank spends the lift it
  does not have. A touchdown within 250 m of an unguarded cache WALKS the
  rest (about 35 s); a hop tried first exited at 12 m, began the approach
  far too low, stalled short and hopped again -- a second cycle. Braking
  starts 450 m out so it lands nearer. After all of it, twenty minutes of
  `--demo` played six valleys, each ending in 52..218 s, five of them with a
  hoard taken and the dragon grown.

Its fighter is ace-tempered (reaction 0.16 s, 1.2 degrees of spread): it
carries a drake through fights a veteran bot would lose. `tests/test_demo_pilot.cpp`
pins the choices (ground, priorities, the fall-back hysteresis, the rush, the
stalemate, the speed guard, a siege that commits only facing and fires on the
run in).

**It hunts (M26).** A herd within 650 m, nearer than the next cache and with
nothing hostile close, is a `Hunt`: a landing's glide path that never
brakes, levelling at 5 m over the animal's lead, the flame on inside 140 m.
A pass that goes by rests the hunt; one that runs 25 s rests it for 20, so
a herd is never circled for ever. Hunting stops once the pilot commits to the
pass. It flies the descent too: a crossed pass resets it into the next valley.

**Getting through a valley (M26.1).** The eight-seed headless measurement
before this pass lost all eight runs in valley 1, at 39..394 s. Five took a
cache, but six spent **zero seconds cruising**. Seed 2553442477 spent 237 s
in take-off and 110 s fighting; 3448181167 spent 106 s sieging and took no
cache. The problem was both controls that did not reach the flight model
and a pilot that kept finding another job instead of banking what it had.

- **Boost reaches the wings.** The demo returned from `read_flight_input`
  before the human input's `combat_.boost_active()` mapping. Its boost button
  consumed the ordinary cooldown and played the effect, but supplied no
  thrust. The demo input now carries that same active ability state. Take-off
  asks for a boost above 8 m while below 45 m/s; transit boosts only within
  30 degrees of the waypoint. The old siege boost request was removed:
  enabling its previously missing thrust sent seed 2391549467 past the guard
  at 81 m/s, 38 degrees off target, without firing. A siege keeps its approach
  speed and the original entry geometry.
- **A cache, then the pass.** After one cache the pilot banks it instead of
  starting another siege, hunt and climb-out. A rush cannot hunt or pursue a
  passing rival or hunter; the opportunistic weapons still fire down the
  route, and a hurt dragon flees forward. The hunter deadline still ends
  unsuccessful cache attempts. All policy dials live in `DemoTuning`.
- **Relock before shooting.** Seed 844455625 aimed at guard slot 0 while its
  sticky lock stayed on rival slot 3; the guard was still at full health at
  20 s. The pilot now taps the player's cycle-target button at 0.2 s intervals
  while its mark is inside the player's actual acquisition limits, and holds
  fire and breath while a different lock would redirect them. It cannot set
  the lock directly or widen aim assist.
- **A fresh valley is fresh.** Reset clears the skipped cache, siege budget,
  entry/run-in flags, hunt rest and disengagement timers. Their old values
  survived into another valley even though its combat slots were reused.
  Job-time totals remain cumulative for the run telemetry.

`test_demo_pilot` also checks transit past prey and pursuers with weapons
live, boost requested only after climbing clear and withheld on a turn,
cycling a sticky lock with paced taps, an unboosted siege approach, and
retrying a previously abandoned cache after reset. Combat, flight, enemy,
prey and run tuning are unchanged.
