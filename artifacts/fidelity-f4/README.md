# F4, first part: the terrain's shadow at every distance, 2026-10-13

`docs/FIDELITY.md`, "F4, first part". Compare with `../fidelity-f2/`.

| File | Command (`./build/dragon --headless ... --screenshot OUT`) |
|---|---|
| `vista.png` | `--frames 40 --hide-ui --cam 0,260,400,300,120,-600` |
| `high.png` | `--frames 40 --hide-ui --cam 2000,2500,3000,-2000,400,-4000` |
| `run.png` | `--frames 120 --hide-panels --run 7` |
| `run-600.png` | `--frames 600 --hide-panels --run 7 --autopilot`: ten seconds down the valley |
| `air-shadow-sun.png`, `air-shadow-vista.png` | the terrain's shadow in the air: `--cam 0,300,0,-8200,2000,-5700` and the vista |
| `ao-vista.png`, `ao-top.png`, `ao-run.png` | the same views with the terrain's sky occlusion, the clouds and the Ultra forest (`ao-top` is `--cam 900,2600,-300,900,0,-320 --hide-vegetation`) |
