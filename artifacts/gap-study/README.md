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
