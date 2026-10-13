# F1: the physically based sky, 2026-10-13

`docs/FIDELITY.md`, "F1, as built". Compare with `../fidelity-baseline/`
and `docs/concept/valley-target.png`. All at the default dials, Metal, M5.

| File | Command (`./build/dragon --headless --frames 40 --hide-ui ... --screenshot OUT`) |
|---|---|
| `vista.png` | `--cam 0,260,400,300,120,-600` |
| `sun.png` | `--cam 0,300,0,-8200,2000,-5700`: into the sun, the disc and the forward haze |
| `high.png` | `--cam 2000,2500,3000,-2000,400,-4000`: the terrain's edge hazing into the horizon |
| `low.png` | the vista with `--sun 4` |
| `noon.png` | the vista with `--sun 55` |
| `run.png` | `--frames 120 --hide-panels --run 7` |
