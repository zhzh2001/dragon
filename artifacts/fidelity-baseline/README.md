# Fidelity baseline, 2026-10-13

The modern tier at Ultra on the Mac (M5, Metal), before any of
`docs/FIDELITY.md`. Judge each F phase against these and against
`docs/concept/valley-target.png`.

| File | Command |
|---|---|
| `run-seed7.png` | `./build/dragon --headless --frames 120 --hide-panels --run 7 --screenshot OUT` |
| `valley-vista.png` | `./build/dragon --headless --frames 60 --hide-ui --cam 0,260,400,300,120,-600 --screenshot OUT` |
| `dragon-close.png` | `./build/dragon --headless --frames 40 --hide-ui --inspect 120 16 8 --screenshot OUT` |

Built at `ec9c352` plus the docs split.
