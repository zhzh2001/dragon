# F5: temporal anti-aliasing, 2026-10-13

`docs/FIDELITY.md`, "F5, as built". Each image: without (`--no-taa`) and
with.

| File | What |
|---|---|
| `flight-off-vs-on.png` | `--frames 700 --hide-ui --run 7 --autopilot`, cropped about the dragon, left off, right on: the dragon clean (its own motion vectors), the background smoother and a little softer |
| `static-edges-off-vs-on.png` | `--frames 40 --hide-ui --cam 600,70,260,7,38,-200`, a crop of the rings and the tree line, top off, bottom on |
