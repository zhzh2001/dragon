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
