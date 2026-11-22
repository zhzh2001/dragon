# Mechanics: what to add to the loop

The discussions of new mechanics for the game, in the order they were
had. They were sections of `DIRECTION.md` until 2026-11-22. Nothing in
this file is built unless it says so. The mode they led to is
`WILDS.md`.

| Section | What is in it |
|---|---|
| Mechanics to consider, from the user's other dragon work | Twelve mechanics for the hoard run, read from the user's other directories. The user deferred all twelve |
| The user's direction on mechanics | The user's answer: loop-level changes (magic, followers, other species), discussed before anything is built |
| Loop shapes for the mechanics discussion | Three shapes for a new loop: the brood, the storm-caller, the hunt, with a recommendation and five questions |

## Mechanics to consider, from the user's other dragon work (2026-11-21, 00:59)

The user asked for new mechanics for the hoard run and for survival, drawn
from their other directories. A subagent read them (not this repository):
`~/src/cloud-llm` (a comparative study of 23 dragon works under
`results-pvc/dragons/study/`, fiction and video notes), `~/src/content-lib`
(a catalogue of 734 dragon works, 616 of them games), `~/src/agent-gaming`
(a harness for Golden Treasure: The Great Green), `~/src/SpiritGuide` (a
battle simulator for Dragon Spirits), and the older prototypes. None of
these is a decision. Each names what it would touch.

For the hoard run:

1. Fire from food. Eating prey fills the breath past full for a while,
   and a long flight without a meal shortens the breath (the study's Age
   of Fire entry: "Fatty flesh brims the bladder"). It ties the herds to
   the fight: `Combat`'s breath meter, `PreyHerds`.
2. Hoard as scale. At a cache, the dragon banks the hoard for score or
   eats a share for armour that lasts the run, and armour costs climb
   rate ("Scaled dragons eat the metal. It gets turned into armor"). One
   choice a cache: `HoardRun`, the flight tuning.
3. The sleep at the pass. Growth happens in a sleep between valleys
   (Golden Treasure's Great Sleeps). The player picks one of three boons
   on waking. Towers left standing behind raise the chance of waking to
   hunters. It gives the pass gate a decision and a reason to clear a
   valley.
4. Ground effects that do something. The six breaths now leave clouds
   (`docs/showcase/EFFECTS.md`). Make them rules: blight's cloud harms
   what stays in it, frost slows what it touches and stills a stretch of
   river, tide puts out ground fire and a tower's brazier, stone's dust
   spoils a tower's aim, storm jumps between drenched targets, fire
   spreads over dry grass. The user's notes favour systems with rules
   that can be played against each other.
5. Throat and belly. A bite on a rival's throat stops its breath for
   some seconds ("cannot use any skills requiring breath or song"), and
   a tower has a weak plate at its back. A rival can do the same to the
   player. `Combat`'s melee, the statuses.
6. Bait and false quarry. From the user's own reversal ideas: a cache
   that is bait with crossbows hidden about it, a rival that plays dead,
   tracking bolts that the roll dodges. One trap a valley from the
   second on: `RunLayout`.
7. The approach. A herd senses a dragon by sight and scatters. Coming
   out of the sun, along the mist or low behind a ridge gives the first
   strike a bonus (Golden Treasure: scent masking "doubles the time it
   takes for prey to sense your presence").
8. Carrying. A kill is carried to a ledge or a landmark and eaten there
   for the full meal; eaten where it fell, the dragon is on the ground in
   the open. The landmarks get a use.

For a survival mode (`docs/ROADMAP.md` had it as the open-world
candidate, "can't fly -> glide -> fly -> carry prey -> breathe fire"):

9. The arc of a life. Start as a hatchling that cannot fly, in one
   valley that stays. Each stage unlocks a verb. Day of Dragons and
   Golden Treasure both do this.
10. Overhunting. A herd breeds back only from what is left ("Overhunt a
    forest one year, and you will starve the next"). Hunger and seasons:
    a winter with thin herds.
11. A lair. A cave or a landmark holds the hoard and is where the dragon
    sleeps and heals (one full rest a day). Hunters come for a hoard that
    has grown.
12. Lives and what carries over. One "second wind" a run that returns to
    the valley's start (Golden Treasure's sigils), and a bestiary: a
    species hunted enough times gives a small lasting perk.

The smallest of these to try first are 1 and 4: both use what the game
already draws, and both make the elements matter outside a fight.

## The user's direction on mechanics (2026-11-21, 23:25)

The user read the twelve mechanics above and deferred them: "we'll
discuss mechanics later, I'd like more fundamental shifts to the game
loop, like casting magic, gathering followers, fighting other species,
etc."

So the twelve are tuning inside the present loop (fly, burn, land,
collect), and the user wants the loop itself to change. Nothing is to be
built from this section until that discussion. Three directions the user
named, each with what it would lean on in the engine today:

- Casting magic: abilities beyond the breath. The element system, the
  status effects and the fury meter exist; a spell with a place and a
  duration (a storm cell, a wall of ice, a summoned updraught) does not.
- Gathering followers: creatures that fly with the player. The bot
  dragons and the prey herds exist; an ally that follows, takes orders
  and can be lost does not.
- Fighting other species: enemies that are not dragons or towers. The
  roster has six rigged prey kinds and a wolf, an eagle and a raven that
  were never rigged (`docs/MODEL_GENERATION.md`, "Queued").

Bring a short list of loop shapes to that discussion, not a build.

## Loop shapes for the mechanics discussion (2026-11-22)

The user, 2026-11-22: "please discuss my more radical mechanics as
well". This section is that discussion's first half: three shapes the
loop could take, one for each thing the user named as its centre, what
each costs, and a recommendation. Nothing here is built.

What the loop is today: fly a valley from its head to its pass, burn
what guards it, land on three caches, grow through three stages, and do
it in three valleys. The player's verbs are fly, breathe, bite and land.
Every enemy is a dragon or a tower.

### Shape A: the brood (followers at the centre)

The player leads a flight. Lesser dragons join it: spared after a fight,
freed from a tower's chains, or hatched from an egg bought with hoard.
Each follower is a species with its element, so the flight is the
player's build, and a follower that dies is gone for the run. The
player's new verbs are few and broad: mark a target, call the dive,
call them back. The pass is held by a rival lord with a flight of their
own, and the valley's end is two flights meeting.

- It leans on what exists most: the bot dragons and their doctrine, the
  demo pilot, the roster of species, the six elements and their
  statuses. The five new elemental dragons get a job at once, as the
  followers and the rival lords.
- It needs: formation flying, three or four orders and a way to give
  them on a pad, followers that read as allies at a glance, and a frame
  budget for six to ten dragons close to the camera.
- The risk: followers that fight for the player make the player a
  spectator. The answer in games that do this well is that the
  followers set up and the player finishes, or the reverse.

### Shape B: the storm-caller (magic at the centre)

The player stays one dragon, and the world becomes the weapon. A spell
is a thing with a place and a duration, put there from the air: a wall
of ice across a gorge, an updraught to climb out on, a storm cell that
strikes what flies through it, a fog bank to cross a guarded ford in, a
rockfall from a cliff. Mana comes from shrines, from prey, or from the
hoard itself, so casting is a choice against growing.

- It leans on: the thermals (a summoned updraught is nearly free), the
  volumetric clouds and the mist (weather is already dials), the
  elements and statuses, and today's pieces and ground marks (ice that
  stands in the ground, rubble that lies there).
- It needs: aiming a spell at ground from a moving dragon, each spell's
  own look and sound, and enemies that react to a wall or a fog bank.
  The last is the cost: a spell that changes the space is only as good
  as the enemies' reading of the space.
- The risk: six spells, each a small effect, read as six more breaths.
  A spell must change where things can go, not only what gets hurt.

### Shape C: the hunt (other species at the centre)

Each valley is the ground of one great beast that is not a dragon, with
its lesser kin about it: a stone giant that throws boulders, a roc and
its flock, a serpent in the lake, a herd-lord on the meadows. The loop
is a hunt: find it by its signs, wear it down through its phases, land
on it and tear at a weak point, feed, grow, and take its element as a
second breath. Each beast asks for a different verb: a flock wants a
wide breath, a giant wants a strafe and a dodge, a serpent wants bait.

- It leans on: the prey herds and their flocking, the five rigged
  quadrupeds, the melee and the landing, the growth stages and the
  second breath.
- It needs: one bespoke creature a valley, each a model, a rig and
  clips (a Codex job of 400,000 to 800,000 tokens each, from this
  repository's own table), and a climbing or perching rule for landing
  on a moving body.
- It is the one that shows best. A dragon against something three times
  its size is the picture the showcase work has been building toward.
- The risk: the cost is in art, a beast at a time, and a hunt with one
  beast is a boss fight, not a loop.

### How they combine

They are not rivals. The strongest loop uses one as the spine and the
other two as its parts:

| Spine | What the others become |
|---|---|
| The brood | A hunt is each valley's end. A spell is what a follower's element gives the flight (a frost follower lets the player raise the ice wall) |
| The hunt | Followers are small scavengers that mark the trail. Spells are what the beasts' elements teach |
| The storm-caller | Followers hold what a spell has shaped. The beasts are the things worth spending mana on |

### Recommendation

The brood as the spine, built in this order, each step a playtest:

1. Two wingmates with two orders (attack my target, come back), on the
   bot dragons as they are. A week of AI work and no new art. It answers
   the first question: is leading a flight fun from a chase camera.
2. Three placed spells on what today's breath work left: the ice wall,
   the updraught, the storm cell. Given by a follower's element.
3. One beast that is not a dragon as a valley's end, the cheapest first:
   a flock (the herds' code, one small model) before a giant.

The reason for this order is cost against how much each changes a
minute of play. Step 1 changes every minute and costs the least. Step 3
shows best and costs the most, so it should come when the loop it ends
is already worth playing.

### Questions for the user

1. Is the player always the one dragon, or can the player fly a
   follower too?
2. A few named followers whose loss hurts (three to five), or a swarm
   (twenty and more)?
3. Does the run stay a fifteen-minute descent through three valleys, or
   does the player keep a valley: a nest, a territory, things that are
   still there next session?
4. Is magic only the elements made larger, or anything (summons,
   illusions, time)?
5. Which beasts would the user most want to fight? The list above is a
   guess.
