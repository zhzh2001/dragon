# Gap study, 2026-10-21

Renders for `docs/GAPS.md`. In each pair, our frame is on the left and the
target from `docs/concept/gaps/` is on the right. Built at `efe3468`, headless,
on the M5.

| File | Our command |
|---|---|
| `run-vs-target.png` | `./build/dragon --headless --frames 120 --hide-ui --run 7 --screenshot OUT` |
| `breath-vs-target.png` | `./build/dragon --headless --frames 90 --hide-ui --combat --attack --inspect 60 30 5 --screenshot OUT` |
| `landing-vs-target.png` | `./build/dragon --headless --frames 900 --hide-ui --input -0.25,0,0,0,0,0 --inspect 150 22 6 --screenshot OUT` |
| `sm2-vs-target.png` | the run command with `--tier sm2` |
| `ff-vs-target.png` | the run command with `--tier ff` |
| `vista-four-tiers.png` | `--hide-ui --cam 0,260,400,300,120,-600` on modern, sm3, sm2 and ff (clockwise from top left) |
| `combat-now.png` | `--hide-panels --bots 2 --bot-range 95 --combat --attack --frames 150` |

## G1, the shape

| File | What |
|---|---|
| `g1-shape-before-after.png` | Top row before, bottom row after: the run 7 chase view, the vista, and `--cam 600,1500,2600,0,150,-1200` |
| `g1-top-before-after.png` | `--hide-vegetation --cam 0,6000,10,0,0,0`, before and after |
| `g1-feet.png` | `--input -0.25,0,0,0,0,0 --frames 900 --inspect 60 11 3`: the landed feet on the unchanged floor |

## G2, the breath

| File | What |
|---|---|
| `g2-breath-before-after.png` | `--combat --attack --inspect 60 30 5 --frames 90`, before and after |
| `g2-breath-close.png` | `--inspect 120 18 3` (the jaws and the light on the neck) and `--inspect 90 3.5 0 --inspect-head` |
| `g2-elements.png` | The same capture with `--element` fire, frost, blight (top), storm, tide, stone (bottom) |

## G4, the membranes

| File | What |
|---|---|
| `g4-membranes-before-after.png` | `--studio 0 --inspect 0 16 -12` and `--inspect 90 16 -12` on Embercrest, and Stormsail at 0: before left, after right |
| `g4-membranes-roster.png` | Frostvein, Blightmaw, Ironroot, Tidewrack at `--inspect 0 16 -12`: only the membranes take the light |
| `g4-touchdown-dust.png` | `--input -0.25,0,0,0,0,0`, the chase view at frames 296 and 310: the dust ring after a touchdown |
| `g4-speed-streaks.png` | The fire golden's frame (`--training --attack`, boosting at 91 m/s): the air streaks radiating from the heading |

## G7, rising air

| File | What |
|---|---|
| `g7-thermal-caps.png` | Top-down `--cam 0,7000,10,0,0,0`, the run 7 chase view, and a cap from below: a cumulus over each thermal along the corridor |
