# The wilds: the survival mode

A second mode beside the hoard run: one dragon living in a world that
does not end. This file is its record, in two parts. The first is what
is built (`--wilds`, `src/game/wilds.h`). The second is a proposal for
what it should become, which waits for the user. Both were sections of
`DIRECTION.md` until 2026-11-22. The mechanics discussions that led
here are `MECHANICS.md`.

| Section | What is in it |
|---|---|
| The wilds: a first version of the survival mode | What `--wilds` is: hunger, the day and the moon, levels on the run's growth, places, sleep, a fall. What it leaves out on purpose. What the demo pilot's soaks found. Status breaths, not built |
| The wilds, second discussion: a realm of valleys | The proposal from the user's digests of ten dragon works: three loops, six mechanics, three powers with a price, the long goals, the open world in two stages, a build order, and what is least sure |
| The user's answers | What the user has said to the proposal, as it comes |

## The wilds: a first version of the survival mode (2026-11-22, 13:00)

The user, 11:55: "we need a new survival/exploration/rpg mode for new
mechanics." This section is what was built without waiting for answers,
and what was left out on purpose. The questions that decide the rest
are at the end of this file, "The user's answers".

### What it is

`--wilds [seed]`, or the Playtest panel's "The wilds". The hoard run's
valleys with the end taken out, and four things added. The rules are
`game/wilds.h`, with no renderer in them and a suite of their own.

| Part | Rule | Where it meets the game |
|---|---|---|
| Hunger | The belly empties in 7 minutes of gliding, faster flapping (x2.6) and breathing (+0.5). A grazer is two fifths of a belly. Above half, the fight's regeneration after a lull runs whole; below half, a quarter of it; empty, none, and health falls 1.2% a second to a floor of 10% | The herds, which the run already had. The valley has two more of them |
| The day | A day and a night are 12 minutes. The sun follows the clock. The night is a moon on the same arc: dim, blue | The hunters fly only by night, from a quarter of the way into it, and leave at dawn. A valley has the run's count of them. A night lived through awake is 80 experience, slept through 40 |
| Growth | Experience from a meal (12), a kill (40), a place found (60), a night. Twenty levels, each needing 1.35 times the last | The level drives the run's growth table. Size, health, reach and the stages' abilities (the charged shot, the second breath, the ram, the fury) follow the level and not the hoard |
| Places | A landmark flown within 70 m counts once. So does a hoard taken, and a pass crossed | The kit's ruins, arches, circles, stones and cairns, which stood in every valley and meant nothing |
| Sleep | N, landed, from the evening on, with no enemy within 250 m. The clock goes to dawn, half the health comes back, the state is saved | `wilds.txt` in the user directory. R returns to it |
| A fall | Not the end. The dragon wakes at the valley head at the next dawn, half fed, without its progress toward the next level | Replaces the run's "RUN LOST" |

A pass leads to the next valley and there is no last one. Each valley
deeper has the run's harder enemies.

### What it does not have, on purpose

Each of these is a decision the user has not made, so none is built.

- A world. It is a chain of valleys, one loaded at a time, entered at
  the head. There is no map, no way back through a pass, and nothing
  remembered about a valley left behind.
- A home. The dragon sleeps anywhere safe. A nest that is a place, with
  things kept there, is the first thing "survival" usually means and is
  not here.
- Anything to spend. Experience buys levels on one track. There is no
  choice in growth: no skills, no stats, no second breath chosen.
- Followers, magic and the larger beasts. The loop shapes
  (`MECHANICS.md`, "Loop shapes for the mechanics discussion") are what this mode is
  meant to carry. It carries none of them yet.
- A night sky. The night is the day's sky under a dim blue light, with
  no stars and no moon's disc. With the sun under the horizon the
  atmosphere renders wrong (orange clouds, ground lit from under), which
  is why the night is a moon.
- Balance. No number in it has been played.

### What the demo pilot found

`--wilds 7 --autopilot`, with the day at 4 minutes and the belly at 200
seconds so that five minutes of play hold a night. Each soak found one
fault, and each is fixed:

1. The pilot fell every 40 seconds. It read the wilds' hunter clock as a
   deadline already past and flew for the pass through the rivals. It
   ignores that clock in the wilds.
2. A hunter that outlived the night met the dragon at the valley head
   each time it woke from a fall: three falls in a row. Hunters now
   leave at dawn, after a fall and after a sleep.
3. A starving dragon stayed at full health. The fight's regeneration
   (6 a second after 6 seconds unhurt) mended it faster than hunger hurt
   it. Regeneration now runs by the belly.
4. The run's hunter comes at once, behind the dragon, and an enemy that
   near forbids sleep: at dusk there was no time to land. The hunter now
   flies from a quarter of the way into the night.

The last soak, 200 seconds: five meals and level 2 before midnight, the
night lived through with its hunter killed, level 3 at dawn, two
landmarks found, then the belly empty through the morning (the pilot
does not hunt because it is hungry), health down to the floor, and a
fall. The pilot does not know how to sleep.

### Status breaths, not built

The user, same message: "can also have low/no damage breath/magic that
has stronger status effects like sleep/paralysis/etc. might not be a
good idea." The doubt is fair for the fight as it is now: a rival that
is asleep is a rival not fought, and the fight is the game's best part.
In the wilds the same breath has uses the arena cannot give it. A beast
put to sleep can be passed, robbed or taken alive, and a herd held still
is a meal for a dragon too weak to chase it. So the idea belongs to this
mode or to none. It waits for the user's answer on followers: sleep and
paralysis are how most games let a player take a creature alive.

## The wilds, second discussion: a realm of valleys (2026-11-22, 15:30)

The user, 14:16: "for wilds I'm more ambitious, still take inspirations
from dragon works like wof, aof and 龙文 ... try to figure out game
loop and interesting mechanics. prefer procedurally generated open
world, dealing with people, gather followers of all sorts. need some
bigger long term goals, like ruling more land, or challenge hard
bosses? discussion only for now".

Nothing here is built. It is a proposal to argue with.

### What the sources agree on

A Sonnet subagent read the user's digests of ten works for how each
world works, and not for its plot. Wings of Fire has no digest there,
only extracted claims, so it is the thinnest. "龙文" is the genre of
the four Chinese novels (白龙之凛冬领主, 黑龙法典, 霜寒之翼,
掌控时光之龙), not one book. Six things recur across works that
otherwise share little, and each is a system a game can run:

| Pattern | Where | As a system |
|---|---|---|
| The hoard is a function | Age of Fire (metal is eaten to grow scale), the four novels (standing, and a dragon is buried with it) | One resource with three uses that compete |
| Rule is taken by challenge | Wings of Fire (a queen holds the throne until a challenger kills her), Age of Fire (pit duels, no peaceful succession), Borne of Caution (beat the strongest of a flight) | Every seat in the world can be fought for, the player's too |
| Land is held bodily | 掌控时光之龙 (scent at the border, grown young driven out), Age of Fire | A claim fades unless someone keeps it |
| People bargain or cage | Pern (tithes for protection), Age of Fire (kern for scale; rings through the hide; a master who breeds dragons as servants), the novels (vassals by contract) | Each settlement is a relationship with two ways to go wrong |
| Growth has gates | The novels (growth sleeps of years, eight life stages), Age of Fire (first fire, wings uncased) | Growing is an event with a risk, not a bar filling |
| Power costs something lasting | Wings of Fire (animus magic takes a piece of the user), Scaleshifter (healing tires the healer), Age of Fire (magic costs the caster, the subject and the world) | Magic is a trade, so it stays rare |

### The game

You are a young dragon with no land, in a region of fifteen to twenty
valleys made from one seed. Every valley has a holder: a rival dragon,
a human lord, a great beast, or nobody. The long game is to go from
landless to the ruler of a realm, and then to take or refuse the
region's high seat. It has three loops, one inside the next.

| Loop | Length | What you do | Exists now |
|---|---|---|---|
| Flight | seconds | Fly, breathe, bite, dodge | Yes |
| The day | 10 to 15 minutes | Wake at the lair, eat, do one thing that matters (raid, defend, parley, explore, challenge), bring it home, sleep | The wilds' skeleton: hunger, the clock, sleep, a save |
| The realm | hours | Take valleys, bind followers, deal with people, grow a stage, answer what the world sends back | No |

The day is the unit that is missing a point today: there is nothing to
bring home and nowhere to bring it. The realm loop gives it both.

### Six mechanics

1. The hoard is eaten, kept or given. Metal taken from a cache, a
   caravan or a rival's lair is one pile with three uses. Eaten, it is
   growth: scale, so health, for good. Kept in the lair, it is
   standing: followers come to a rich lair, people pay a rich dragon
   more readily, and thieves and slayers come for it too. Given, it
   seals a bargain with a village or another dragon. A player short of
   metal cannot do all three, and that is the decision of most days.
   (Age of Fire's metal diet, with the novels' hoard as standing.)

2. A valley is held, not owned. Beat or drive out its holder and it is
   yours while your mark is fresh. A mark fades in a few days unless
   you return or a follower is posted there. A faded valley draws a
   challenger. So the realm's size is limited by who you have to hold
   it, and followers are the way to grow, not a bonus.
   (掌控时光之龙's borders; challenge succession from three works.)

3. People are a relationship with two meters. Each settlement holds a
   fear of you and a trust in you. Raid it and you eat today: fear
   rises, and past a point it hires slayers, the run's hunters, who
   then come for you at night wherever you sleep. Protect it (answer
   its alarm when wolves, bandits or another dragon come) and trust
   rises: it pays tribute in food and metal, its lookouts warn you, its
   builders improve the lair. Neglect a protected village and trust
   turns to resentment faster than it was earned. Neither meter is
   good or evil. A dragon that rules by fear has a smaller, richer,
   more hunted realm. (Pern's tithes, Age of Fire's kern kings, the
   rebellion in Lost Illusion.)

4. Followers of four kinds, each with one job. No kind does another's.

   | Kind | Gained by | Job |
   |---|---|---|
   | Small kin (drakelings) | Feeding a hatchling at a nest you find, as Pern's fire-lizards are won | Scouts and messengers: they uncover the map and carry an alarm to you |
   | Beasts (a wolf pack, great birds) | Beating the leader and sparing it | They hold a valley's ground and hunt for the lair |
   | People (a sworn village, a kobold warren) | Trust, or a bargain sealed with metal | Tribute, builders, healers, a watch |
   | Dragons (the six elements) | Beating one and sparing it for an oath, freeing one that people hold in rings, raising an egg | Two or three fly with you as wingmates, or one holds a valley as a vassal |

   A wingmate obeys three orders: with me, take that one, hold here.
   A vassal dragon can be given the blood gift once: you spend a level
   of your own and it rises a stage, or sickens. That is the novels'
   blood conversion, with its ceiling and its risk, and it makes a
   strong follower a thing you paid for.

5. Growing a stage is a long sleep. Levels come as they do now. A new
   stage (drake, young, adult, elder, ancient) needs a full belly, a
   store of metal eaten, and a sleep of a whole season in the lair.
   The world does not wait. When you wake, a rival has grown too, a
   village has prospered or risen, a vassal has kept its valley or
   lost it. So before each sleep the question is whether the realm
   can stand a season without you. This is the mechanic I would build
   the mode around: it turns followers, people and land into one
   decision, and it gives the long game a rhythm of preparing and
   leaping. (The novels' growth sleeps; Age of Fire's gates.)

6. Deeds are worn. Each great deed dyes a band onto the wing membrane:
   a holder beaten, a village saved, a village burned, a slayer
   killed, a season slept through. Other dragons and people read them
   before you speak: a challenger thinks twice at five bands, a
   village opens or bars its gate by their colours. Reputation is on
   the model, where a flying game can show it, and not in a menu.
   (Age of Fire's laudi.)

### Magic and status breaths

Three powers, each with a price, and no spell list.

| Power | What it does | Price |
|---|---|---|
| The dread (龙威, the novels' dragon might) | A held roar. Weak things flee or yield. It is how a beast pack or a beaten dragon is made to submit without being killed | Belly, and fear in every village that hears it |
| The gift of breath | Heals a follower, as Scaleshifter's dragons heal each other and never themselves | Your own health, for a day |
| The weather | A dragon's element over one valley for a day: rain that ends a fire, a freeze that closes a pass against a challenger, a storm that grounds slayers | A level's progress. It is the storm-caller of `MECHANICS.md`'s loop shapes, kept rare |

The status breath the user doubted belongs here and nowhere else. A
sleep or a hold that deals no damage is weak in a duel and exactly
right for taking a creature alive, which is how three of the four
kinds of follower are gained. I would make it the dread, and not a
second breath.

### The long goals

The user named two, ruling more land and hard bosses. Both fit, and
they pull against each other, which is good.

- The high seat. One ancient dragon holds the region's best valley and
  takes tribute from every holder, the player included once the player
  holds land. It can be paid, served or challenged. Challenging it is
  the last fight, and winning makes the player the one others
  challenge. (Wings of Fire's throne, Age of Fire's Tyr.)
- Three elder threats, one for each kind of holder, each a fight no
  lone dragon wins: the slayers' keep (a siege: needs people or beasts
  on the ground), the cage-master who holds ringed dragons (freeing
  them is the largest single gain of followers in the game), and a
  titan beast in the far north (needs wingmates).
- An ending the player picks by what they did: the ruler, who holds
  the seat; the protector, whose valleys never rose; the terror, whose
  realm is ash and gold. No score. The wing shows which.

### The open world, in two stages

The engine makes one valley from a seed and loads one at a time. The
first stage keeps that: the region is a graph of valleys joined by
passes, with a map screen, and each valley remembers its state when
left. That is cheap and it is enough for every mechanic above. The
second stage is a seamless region, neighbours seen from a ridge and
streamed as they near. It needs the terrain to stream (CDLOD,
`docs/FIDELITY.md` F3) and is months of renderer work that changes no
rule. I would not start it until the first stage is fun.

What the first stage needs that does not exist: a settlement (a kit of
buildings, people as dots at a distance and a bell that rings), a
lair, two beasts, a map screen, a valley's saved state, and wingmates
that follow. The rivals, the hunters, the herds, the hoards, the
towers, the five elements and the six dragons exist.

### A build order, if the user says go

Each step is playable on its own and answers one question.

1. The region and the lair. A graph of valleys from the seed, state
   kept, a map, a lair to carry hoard to, metal eaten for scale. Is
   bringing things home a reason to fly?
2. Holders and marks. Valleys taken and lost. Does land held feel like
   something?
3. One village, two meters, raid or protect, slayers as the answer. Is
   dealing with people a choice?
4. Two followers: a wingmate and scouts. Does company change a fight?
5. The long sleep and seasons. Does the realm standing without you
   feel like a bet?
6. The elder threats and the seat.

### What I am least sure of

- Whether a player wants to rule at all, or to wander. Marks that fade
  punish wandering.
- The long sleep takes control away for a screen. If the report on
  waking is dull, it is a loading screen with bad news.
- People need the most new art of anything here, and are the easiest
  to make feel like a meter with a hut on it.
- Twenty valleys of four kinds may feel like four valleys. The valley
  generator needs more kinds before the map does.

## The user's answers

None yet. The seven questions that decide the build went to the user
in the session's report of 2026-11-22, 15:45:

1. To rule, or to wander? Marks that fade punish wandering.
2. Is ruling by fear a full path, or is the player a protector?
3. A hatchling in a clutch, or a young dragon already flying?
4. People: humans only, or also a dragon-worshipping small folk?
5. Is the long sleep acceptable, given it takes control away for a
   screen?
6. One element for life, or can a dragon gain a second?
7. Is the map of valleys acceptable as the first stage?
